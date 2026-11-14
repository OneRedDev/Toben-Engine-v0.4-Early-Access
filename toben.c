
#define _POSIX_C_SOURCE 200809L

#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdint.h>
#include <limits.h>

#define VERSION "0.6"
#define HISTORY_LIMIT 32
#define HISTORY_MEMORY (8 * 1024 * 1024)

typedef enum {
    LANG_PYTHON,
    LANG_C,
    LANG_CPP,
    LANG_BASH,
    LANG_RUST
} Language;

typedef struct {
    char **lines;
    size_t count;
    int cy, cx;
    int dirty;
    size_t bytes;
} Snapshot;

typedef struct {
    Snapshot items[HISTORY_LIMIT];
    int count;
    size_t bytes;
} History;

char **lines = NULL;
size_t line_count = 0;
size_t capacity = 0;

int cy = 0, cx = 0;
int row_offset = 0;
int col_offset = 0;
int dirty = 0;

Language language = LANG_PYTHON;

char filename[512] = "untitled.py";
char status_message[256] = "Welcome to Toben Engine";

History undo_stack = {0};
History redo_stack = {0};

/* LANGUAGE */

void detect_language(void) {
    const char *ext = strrchr(filename, '.');

    if (!ext) {
        language = LANG_PYTHON;
    } else if (!strcmp(ext, ".c") || !strcmp(ext, ".h")) {
        language = LANG_C;
    } else if (!strcmp(ext, ".cpp") ||
               !strcmp(ext, ".cc") ||
               !strcmp(ext, ".cxx") ||
               !strcmp(ext, ".hpp")) {
        language = LANG_CPP;
    } else if (!strcmp(ext, ".sh") ||
               !strcmp(ext, ".bash")) {
        language = LANG_BASH;
    } else if (!strcmp(ext, ".rs")) {
        language = LANG_RUST;
    } else {
        language = LANG_PYTHON;
    }
}

const char *language_name(void) {
    switch (language) {
        case LANG_C: return "C";
        case LANG_CPP: return "C++";
        case LANG_BASH: return "BASH";
        case LANG_RUST: return "RUST";
        default: return "PYTHON";
    }
}

int is_c_family(void) {
    return language == LANG_C || language == LANG_CPP;
}

int is_keyword(const char *word) {
    const char *keywords =
        " int char float double void short long unsigned signed "
        " if else switch case default for while do break continue "
        " return struct union enum typedef const static extern "
        " sizeof volatile auto class public private protected "
        " namespace using template typename virtual new delete "
        " bool true false nullptr try catch throw "
        " def lambda yield import from as pass global nonlocal "
        " async await and or not is None self "
        " then elif fi done until function export local readonly "
        " fn let mut pub impl trait match mod use crate self "
        " i8 i16 i32 i64 i128 isize u8 u16 u32 u64 u128 usize "
        " f32 f64 String Vec Option Result ";

    char pattern[160];

    if (strlen(word) + 3 >= sizeof(pattern))
        return 0;

    snprintf(pattern, sizeof(pattern), " %s ", word);

    return strstr(keywords, pattern) != NULL;
}

/* MEMORY */

int reserve_lines(size_t needed) {
    if (needed <= capacity)
        return 1;

    size_t new_capacity = capacity ? capacity : 16;

    while (new_capacity < needed) {
        if (new_capacity > SIZE_MAX / 2)
            return 0;

        new_capacity *= 2;
    }

    if (new_capacity > SIZE_MAX / sizeof(char *))
        return 0;

    char **temp = realloc(
        lines,
        new_capacity * sizeof(char *)
    );

    if (!temp)
        return 0;

    lines = temp;
    capacity = new_capacity;

    return 1;
}

void init_document(void) {
    if (!reserve_lines(1)) {
        fprintf(stderr, "Toben: memory allocation failed\n");
        exit(1);
    }

    lines[0] = strdup("");

    if (!lines[0]) {
        fprintf(stderr, "Toben: memory allocation failed\n");
        exit(1);
    }

    line_count = 1;
}

void free_document(void) {
    for (size_t i = 0; i < line_count; i++)
        free(lines[i]);

    free(lines);

    lines = NULL;
    line_count = 0;
    capacity = 0;
}

/* HISTORY */

void free_snapshot(Snapshot *s) {
    if (!s->lines)
        return;

    for (size_t i = 0; i < s->count; i++)
        free(s->lines[i]);

    free(s->lines);
    memset(s, 0, sizeof(*s));
}

void clear_history(History *h) {
    for (int i = 0; i < h->count; i++)
        free_snapshot(&h->items[i]);

    h->count = 0;
    h->bytes = 0;
}

int create_snapshot(Snapshot *s) {
    memset(s, 0, sizeof(*s));

    if (line_count > SIZE_MAX / sizeof(char *))
        return 0;

    s->lines = calloc(line_count, sizeof(char *));

    if (!s->lines)
        return 0;

    s->count = line_count;
    s->cy = cy;
    s->cx = cx;
    s->dirty = dirty;
    s->bytes = line_count * sizeof(char *);

    if (s->bytes > HISTORY_MEMORY) {
        free_snapshot(s);
        return 0;
    }

    for (size_t i = 0; i < line_count; i++) {
        size_t len = strlen(lines[i]) + 1;

        if (len > HISTORY_MEMORY - s->bytes) {
            free_snapshot(s);
            return 0;
        }

        s->lines[i] = strdup(lines[i]);

        if (!s->lines[i]) {
            free_snapshot(s);
            return 0;
        }

        s->bytes += len;
    }

    return 1;
}

void drop_oldest(History *h) {
    if (!h->count)
        return;

    h->bytes -= h->items[0].bytes;
    free_snapshot(&h->items[0]);

    for (int i = 0; i < h->count - 1; i++)
        h->items[i] = h->items[i + 1];

    h->count--;

    memset(&h->items[h->count], 0, sizeof(Snapshot));
}

int push_snapshot(History *h) {
    Snapshot s;

    if (!create_snapshot(&s))
        return 0;

    while (h->count > 0 &&
           (h->count >= HISTORY_LIMIT ||
            h->bytes > HISTORY_MEMORY - s.bytes)) {
        drop_oldest(h);
    }

    h->items[h->count++] = s;
    h->bytes += s.bytes;

    return 1;
}

void record_undo(void) {
    if (!push_snapshot(&undo_stack)) {
        snprintf(status_message, sizeof(status_message),
                 "Undo history memory limit reached");
    }

    clear_history(&redo_stack);
}

void restore_snapshot(Snapshot *s) {
    free_document();

    lines = s->lines;
    line_count = s->count;
    capacity = line_count;

    s->lines = NULL;

    cy = s->cy;
    cx = s->cx;
    dirty = s->dirty;

    memset(s, 0, sizeof(*s));

    if (cy < 0)
        cy = 0;

    if ((size_t)cy >= line_count)
        cy = (int)line_count - 1;

    size_t len = strlen(lines[cy]);

    if (cx < 0)
        cx = 0;

    if ((size_t)cx > len)
        cx = (int)len;

    row_offset = 0;
    col_offset = 0;
}

void undo(void) {
    if (!undo_stack.count) {
        snprintf(status_message, sizeof(status_message),
                 "Nothing to undo");
        return;
    }

    if (!push_snapshot(&redo_stack)) {
        snprintf(status_message, sizeof(status_message),
                 "Cannot create redo snapshot");
        return;
    }

    Snapshot *s = &undo_stack.items[undo_stack.count - 1];

    undo_stack.bytes -= s->bytes;
    undo_stack.count--;

    restore_snapshot(s);

    snprintf(status_message, sizeof(status_message), "Undo");
}

void redo(void) {
    if (!redo_stack.count) {
        snprintf(status_message, sizeof(status_message),
                 "Nothing to redo");
        return;
    }

    if (!push_snapshot(&undo_stack)) {
        snprintf(status_message, sizeof(status_message),
                 "Cannot create undo snapshot");
        return;
    }

    Snapshot *s = &redo_stack.items[redo_stack.count - 1];

    redo_stack.bytes -= s->bytes;
    redo_stack.count--;

    restore_snapshot(s);

    snprintf(status_message, sizeof(status_message), "Redo");
}

/* FILE MANAGEMENT */

void load_file(void) {
    FILE *f = fopen(filename, "r");

    if (!f)
        return;

    free_document();

    char *buffer = NULL;
    size_t buffer_size = 0;
    ssize_t length;

    int failed = 0;

    while ((length = getline(&buffer, &buffer_size, f)) != -1) {
        if (length > 0 && buffer[length - 1] == '\n')
            buffer[--length] = '\0';

        if (length > 0 && buffer[length - 1] == '\r')
            buffer[--length] = '\0';

        if (!reserve_lines(line_count + 1)) {
            failed = 1;
            break;
        }

        lines[line_count] = strdup(buffer);

        if (!lines[line_count]) {
            failed = 1;
            break;
        }

        line_count++;
    }

    free(buffer);

    if (ferror(f))
        failed = 1;

    fclose(f);

    if (line_count == 0)
        init_document();

    dirty = 0;

    clear_history(&undo_stack);
    clear_history(&redo_stack);

    if (failed) {
        snprintf(status_message, sizeof(status_message),
                 "Warning: file loaded partially");
    }
}

void save_file(void) {
    FILE *f = fopen(filename, "w");

    if (!f) {
        snprintf(status_message, sizeof(status_message),
                 "ERROR: Cannot save file");
        return;
    }

    int failed = 0;

    for (size_t i = 0; i < line_count; i++) {
        if (fprintf(f, "%s\n", lines[i]) < 0) {
            failed = 1;
            break;
        }
    }

    if (fclose(f) != 0)
        failed = 1;

    if (failed) {
        snprintf(status_message, sizeof(status_message),
                 "ERROR: Save failed");
        return;
    }

    dirty = 0;

    snprintf(status_message, sizeof(status_message),
             "Saved: %s", filename);
}

/* COLORS */

void init_colors(void) {
    if (!has_colors())
        return;

    start_color();
    use_default_colors();

    init_pair(1, COLOR_BLACK, COLOR_CYAN);
    init_pair(2, COLOR_YELLOW, -1);
    init_pair(3, COLOR_GREEN, -1);
    init_pair(4, COLOR_RED, -1);
    init_pair(5, COLOR_CYAN, -1);
    init_pair(6, COLOR_MAGENTA, -1);
}

/* SYNTAX HIGHLIGHTING */

void draw_line(int row, size_t index) {
    const char *s = lines[index] + col_offset;
    int col = 0;

    while (*s && col < COLS - 5) {
        if (((language == LANG_PYTHON ||
              language == LANG_BASH) && *s == '#') ||
            ((is_c_family() || language == LANG_RUST) &&
             s[0] == '/' && s[1] == '/')) {

            attron(COLOR_PAIR(4));

            while (*s && col < COLS - 5) {
                mvaddch(row, col + 5, *s++);
                col++;
            }

            attroff(COLOR_PAIR(4));
            break;
        }

        if (is_c_family() &&
            s[0] == '/' && s[1] == '*') {

            attron(COLOR_PAIR(4));

            while (*s && col < COLS - 5) {
                mvaddch(row, col + 5, *s);

                if (s[0] == '*' && s[1] == '/') {
                    col++;
                    mvaddch(row, col + 5, s[1]);
                    s += 2;
                    col++;
                    break;
                }

                s++;
                col++;
            }

            attroff(COLOR_PAIR(4));
            continue;
        }

        if (*s == '"' || *s == '\'') {
            char quote = *s;

            attron(COLOR_PAIR(3));

            mvaddch(row, col + 5, *s++);
            col++;

            while (*s && col < COLS - 5) {
                char c = *s++;

                mvaddch(row, col + 5, c);
                col++;

                if (c == '\\' && *s) {
                    mvaddch(row, col + 5, *s++);
                    col++;
                } else if (c == quote) {
                    break;
                }
            }

            attroff(COLOR_PAIR(3));
            continue;
        }

        if (isdigit((unsigned char)*s)) {
            attron(COLOR_PAIR(6));

            while ((isalnum((unsigned char)*s) ||
                    *s == '.' || *s == '_') &&
                   col < COLS - 5) {
                mvaddch(row, col + 5, *s++);
                col++;
            }

            attroff(COLOR_PAIR(6));
            continue;
        }

        if (isalpha((unsigned char)*s) || *s == '_') {
            char word[128];
            int n = 0;

            while ((isalnum((unsigned char)*s) ||
                    *s == '_') && n < 127) {
                word[n++] = *s++;
            }

            word[n] = '\0';

            int keyword = is_keyword(word);

            if (keyword)
                attron(COLOR_PAIR(2));

            for (int i = 0; i < n && col < COLS - 5; i++)
                mvaddch(row, col++ + 5, word[i]);

            if (keyword)
                attroff(COLOR_PAIR(2));

            continue;
        }

        mvaddch(row, col + 5, *s++);
        col++;
    }
}

/* DISPLAY */

void draw(void) {
    erase();

    int height = LINES - 3;

    for (int i = 0; i < height; i++) {
        size_t index = (size_t)row_offset + (size_t)i;

        if (index >= line_count)
            break;

        attron(COLOR_PAIR(5));
        mvprintw(i, 0, "%4zu ", index + 1);
        attroff(COLOR_PAIR(5));

        draw_line(i, index);
    }

    attron(COLOR_PAIR(1));

    mvprintw(LINES - 3, 0,
             " TOBEN ENGINE v%s | %s | %s | %s ",
             VERSION, language_name(), filename,
             dirty ? "MODIFIED" : "SAVED");

    clrtoeol();
    attroff(COLOR_PAIR(1));

    mvprintw(LINES - 2, 0,
             " CTRL+S Save | CTRL+R Run | CTRL+G Go to Line | CTRL+Z Undo | CTRL+Y Redo | CTRL+X Exit ");

    mvprintw(LINES - 1, 0, " %s ", status_message);
    clrtoeol();

    int screen_y = cy - row_offset;
    int screen_x = cx - col_offset + 5;

    if (screen_y >= 0 && screen_y < height &&
        screen_x >= 5 && screen_x < COLS) {
        move(screen_y, screen_x);
    }

    refresh();
}

/* EDITING */

void insert_text(const char *text) {
    size_t amount = strlen(text);
    size_t len = strlen(lines[cy]);

    if (!amount || (size_t)cx > len)
        return;

    if (amount > (size_t)INT_MAX - (size_t)cx)
        return;

    if (len > SIZE_MAX - amount - 1)
        return;

    char *new_line = malloc(len + amount + 1);

    if (!new_line)
        return;

    memcpy(new_line, lines[cy], (size_t)cx);
    memcpy(new_line + cx, text, amount);

    memcpy(new_line + cx + amount,
           lines[cy] + cx,
           len - (size_t)cx + 1);

    record_undo();

    free(lines[cy]);
    lines[cy] = new_line;

    cx += (int)amount;
    dirty = 1;
}

void insert_char(int c) {
    char text[2] = {(char)c, '\0'};
    insert_text(text);
}

void new_line(void) {
    size_t len = strlen(lines[cy]);

    if ((size_t)cx > len)
        return;

    char *left = malloc((size_t)cx + 1);
    char *right = strdup(lines[cy] + cx);

    if (!left || !right) {
        free(left);
        free(right);
        return;
    }

    memcpy(left, lines[cy], (size_t)cx);
    left[cx] = '\0';

    if (!reserve_lines(line_count + 1)) {
        free(left);
        free(right);
        return;
    }

    record_undo();

    free(lines[cy]);
    lines[cy] = left;

    memmove(&lines[cy + 2],
            &lines[cy + 1],
            (line_count - (size_t)cy - 1) * sizeof(char *));

    lines[cy + 1] = right;

    line_count++;
    cy++;
    cx = 0;
    dirty = 1;
}

void backspace(void) {
    if (cx > 0) {
        size_t len = strlen(lines[cy]);

        record_undo();

        memmove(lines[cy] + cx - 1,
                lines[cy] + cx,
                len - (size_t)cx + 1);

        cx--;
        dirty = 1;

    } else if (cy > 0) {
        size_t previous_len = strlen(lines[cy - 1]);
        size_t current_len = strlen(lines[cy]);

        if (previous_len > SIZE_MAX - current_len - 1)
            return;

        char *merged = malloc(previous_len + current_len + 1);

        if (!merged)
            return;

        memcpy(merged, lines[cy - 1], previous_len);
        memcpy(merged + previous_len,
               lines[cy], current_len + 1);

        record_undo();

        free(lines[cy - 1]);
        free(lines[cy]);

        lines[cy - 1] = merged;

        memmove(&lines[cy],
                &lines[cy + 1],
                (line_count - (size_t)cy - 1) * sizeof(char *));

        line_count--;
        cy--;
        cx = (int)previous_len;
        dirty = 1;
    }
}

void delete_char(void) {
    size_t len = strlen(lines[cy]);

    if ((size_t)cx < len) {
        record_undo();

        memmove(lines[cy] + cx,
                lines[cy] + cx + 1,
                len - (size_t)cx);

        dirty = 1;

    } else if ((size_t)cy + 1 < line_count) {
        size_t next_len = strlen(lines[cy + 1]);

        if (len > SIZE_MAX - next_len - 1)
            return;

        char *merged = malloc(len + next_len + 1);

        if (!merged)
            return;

        memcpy(merged, lines[cy], len);
        memcpy(merged + len, lines[cy + 1], next_len + 1);

        record_undo();

        free(lines[cy]);
        free(lines[cy + 1]);

        lines[cy] = merged;

        memmove(&lines[cy + 1],
                &lines[cy + 2],
                (line_count - (size_t)cy - 2) * sizeof(char *));

        line_count--;
        dirty = 1;
    }
}

/* GO TO LINE */

void go_to_line(void) {
    char input[32];

    echo();

    mvprintw(LINES - 1, 0, " Go to line: ");
    clrtoeol();
    refresh();

    int result = getnstr(input, sizeof(input) - 1);

    noecho();

    if (result == ERR || !input[0]) {
        snprintf(status_message, sizeof(status_message),
                 "Go to line cancelled");
        return;
    }

    char *end;
    long target = strtol(input, &end, 10);

    if (*end || target < 1 ||
        (unsigned long)target > line_count) {
        snprintf(status_message, sizeof(status_message),
                 "Invalid line number");
        return;
    }

    cy = (int)(target - 1);
    cx = 0;

    snprintf(status_message, sizeof(status_message),
             "Moved to line %ld", target);
}

/* PROCESS EXECUTION */

int run_process(char *program, char **args) {
    pid_t pid = fork();

    if (pid == 0) {
        execvp(program, args);
        perror("Toben: execution failed");
        _exit(127);
    }

    if (pid < 0) {
        perror("Toben: fork failed");
        return -1;
    }

    int status;

    if (waitpid(pid, &status, 0) < 0)
        return -1;

    if (WIFEXITED(status))
        return WEXITSTATUS(status);

    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);

    return -1;
}

/* BUILD AND RUN */

void build_and_run(void) {
    if (dirty)
        save_file();

    if (dirty) {
        snprintf(status_message, sizeof(status_message),
                 "Cannot run: save failed");
        return;
    }

    endwin();

    int result = -1;

    if (language == LANG_PYTHON) {
        char *args[] = {"python3", filename, NULL};
        result = run_process("python3", args);

    } else if (language == LANG_BASH) {
        char *args[] = {"bash", filename, NULL};
        result = run_process("bash", args);

    } else if (language == LANG_C) {
        char *args[] = {
            "clang", "-std=c11", filename,
            "-o", "/tmp/toben_program", NULL
        };

        result = run_process("clang", args);

        if (result == 0) {
            char *run_args[] = {"/tmp/toben_program", NULL};
            result = run_process("/tmp/toben_program", run_args);
        }

    } else if (language == LANG_CPP) {
        char *args[] = {
            "clang++", "-std=c++17", filename,
            "-o", "/tmp/toben_program", NULL
        };

        result = run_process("clang++", args);

        if (result == 0) {
            char *run_args[] = {"/tmp/toben_program", NULL};
            result = run_process("/tmp/toben_program", run_args);
        }

    } else if (language == LANG_RUST) {
        char *args[] = {
            "rustc", filename,
            "-o", "/tmp/toben_program", NULL
        };

        result = run_process("rustc", args);

        if (result == 0) {
            char *run_args[] = {"/tmp/toben_program", NULL};
            result = run_process("/tmp/toben_program", run_args);
        }
    }

    printf("\nToben: process finished with code %d\n", result);
    printf("Press Enter to return to Toben...");
    fflush(stdout);

    int ch;
    while ((ch = getchar()) != '\n' && ch != EOF)
        ;

    reset_prog_mode();
        refresh();
    clear();
    init_colors();
}

/* MAIN */

int main(int argc, char **argv) {
    if (argc > 1) {
        if (strlen(argv[1]) >= sizeof(filename)) {
            fprintf(stderr, "Toben: filename too long\n");
            return 1;
        }

        snprintf(filename, sizeof(filename), "%s", argv[1]);
    }

    detect_language();
    init_document();
    load_file();

    initscr();
    raw();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(1);

    init_colors();

    int running = 1;
    while (running) {
        int height = LINES - 3;

        if (height < 1)
            height = 1;

        if (cy < row_offset)
            row_offset = cy;

        if (cy >= row_offset + height)
            row_offset = cy - height + 1;

        if (cx < col_offset)
            col_offset = cx;

        if (cx >= col_offset + COLS - 6)
            col_offset = cx - COLS + 7;

        draw();

        int key = getch();

        switch (key) {
            case 19: /* CTRL+S */
                save_file();
                break;

            case 18: /* CTRL+R */
                build_and_run();
                break;

            case 7: /* CTRL+G */
                go_to_line();
                break;

            case 26: /* CTRL+Z */
                undo();
                break;

            case 25: /* CTRL+Y */
                redo();
                break;

            case 24: /* CTRL+X */
                running = 0;
                break;

            case KEY_UP:
                if (cy > 0) {
                    cy--;
                    size_t len = strlen(lines[cy]);
                    if ((size_t)cx > len)
                        cx = (int)len;
                }
                break;

            case KEY_DOWN:
                if ((size_t)cy + 1 < line_count) {
                    cy++;
                    size_t len = strlen(lines[cy]);
                    if ((size_t)cx > len)
                        cx = (int)len;
                }
                break;

            case KEY_LEFT:
                if (cx > 0) {
                    cx--;
                } else if (cy > 0) {
                    cy--;
                    cx = (int)strlen(lines[cy]);
                }
                break;

            case KEY_RIGHT:
                if ((size_t)cx < strlen(lines[cy])) {
                    cx++;
                } else if ((size_t)cy + 1 < line_count) {
                    cy++;
                    cx = 0;
                }
                break;

            case KEY_HOME:
                cx = 0;
                break;

            case KEY_END:
                cx = (int)strlen(lines[cy]);
                break;

            case KEY_PPAGE:
                cy -= height;
                if (cy < 0)
                    cy = 0;
                break;

            case KEY_NPAGE:
                cy += height;
                if ((size_t)cy >= line_count)
                    cy = (int)line_count - 1;
                break;

            case KEY_BACKSPACE:
            case 127:
            case 8:
                backspace();
                break;

            case KEY_DC:
                delete_char();
                break;

            case '\n':
            case KEY_ENTER:
                new_line();
                break;

            case '\t':
                insert_text("    ");
                break;

            default:
                if (key >= 32 && key <= 126)
                    insert_char(key);
                break;
        }
    }

    endwin();

    if (dirty) {
        printf("Toben: unsaved changes. Save before exit? (y/n): ");
        fflush(stdout);

        int answer = getchar();

        if (answer == 'y' || answer == 'Y')
            save_file();
    }

    clear_history(&undo_stack);
    clear_history(&redo_stack);
    free_document();

    return 0;
}
