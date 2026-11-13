
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>

#define MAX_LINES 10000
#define MAX_LINE 4096
#define VERSION "0.4"

typedef enum {
    LANG_PYTHON,
    LANG_C,
    LANG_CPP,
    LANG_BASH
} Language;

char *lines[MAX_LINES];

int line_count = 1;
int cy = 0, cx = 0;
int row_offset = 0, col_offset = 0;
int dirty = 0;

Language language = LANG_PYTHON;

char filename[512] = "untitled.py";
char status_message[256] = "Welcome to Toben Engine v0.4 Early Access";

const char *python_keywords[] = {
    "def", "class", "return", "if", "elif", "else",
    "for", "while", "in", "import", "from", "as",
    "try", "except", "finally", "with", "pass",
    "break", "continue", "lambda", "yield", "raise",
    "global", "nonlocal", "assert", "del", "async",
    "await", "and", "or", "not", "is", "True",
    "False", "None", "print", "self", "super", NULL
};

const char *c_keywords[] = {
    "int", "char", "float", "double", "void",
    "short", "long", "signed", "unsigned", "if",
    "else", "switch", "case", "default", "for",
    "while", "do", "break", "continue", "return",
    "struct", "union", "enum", "typedef", "const",
    "static", "extern", "sizeof", "volatile",
    "register", "auto", "goto", "restrict",
    "_Bool", NULL
};

const char *cpp_keywords[] = {
    "alignas", "alignof", "and", "asm", "auto",
    "bool", "break", "case", "catch", "char",
    "class", "const", "constexpr", "consteval",
    "constinit", "continue", "decltype", "default",
    "delete", "do", "double", "else", "enum",
    "explicit", "export", "extern", "false", "float",
    "for", "friend", "goto", "if", "inline", "int",
    "long", "mutable", "namespace", "new", "noexcept",
    "not", "nullptr", "operator", "private",
    "protected", "public", "register", "return",
    "short", "signed", "sizeof", "static",
    "static_assert", "struct", "switch", "template",
    "this", "thread_local", "throw", "true", "try",
    "typedef", "typeid", "typename", "union",
    "unsigned", "using", "virtual", "void",
    "volatile", "wchar_t", "while", "override",
    "final", "concept", "requires", "co_await",
    "co_return", "co_yield", NULL
};

const char *bash_keywords[] = {
    "if", "then", "else", "elif", "fi",
    "for", "while", "until", "do", "done",
    "case", "esac", "in", "function", "select",
    "time", "coproc", "return", "exit", "break",
    "continue", "export", "local", "readonly",
    "declare", "typeset", "unset", "shift",
    "source", "eval", "exec", "trap", "set",
    "echo", "printf", "read", "true", "false",
    "test", "alias", "unalias", "jobs", "fg",
    "bg", "wait", "kill", NULL
};

/* LANGUAGE SYSTEM */

int is_c_family(void) {
    return language == LANG_C || language == LANG_CPP;
}

void detect_language(void) {
    const char *ext = strrchr(filename, '.');

    if (!ext) {
        language = LANG_PYTHON;
        return;
    }

    if (!strcmp(ext, ".cpp") ||
        !strcmp(ext, ".cc") ||
        !strcmp(ext, ".cxx") ||
        !strcmp(ext, ".hpp") ||
        !strcmp(ext, ".hh") ||
        !strcmp(ext, ".hxx")) {

        language = LANG_CPP;

    } else if (!strcmp(ext, ".c") ||
               !strcmp(ext, ".h")) {

        language = LANG_C;

    } else if (!strcmp(ext, ".sh") ||
               !strcmp(ext, ".bash")) {

        language = LANG_BASH;

    } else {
        language = LANG_PYTHON;
    }
}

const char *language_name(void) {
    switch (language) {
        case LANG_C: return "C";
        case LANG_CPP: return "C++";
        case LANG_BASH: return "BASH";
        default: return "PYTHON";
    }
}

int is_keyword(const char *word) {
    const char **list;

    switch (language) {
        case LANG_C:
            list = c_keywords;
            break;
        case LANG_CPP:
            list = cpp_keywords;
            break;
        case LANG_BASH:
            list = bash_keywords;
            break;
        default:
            list = python_keywords;
            break;
    }

    for (int i = 0; list[i]; i++) {
        if (!strcmp(word, list[i]))
            return 1;
    }

    return 0;
}

/* FILE MANAGEMENT */

void init_lines(void) {
    lines[0] = calloc(MAX_LINE, 1);

    if (!lines[0]) {
        fprintf(stderr, "Toben: Memory allocation failed\n");
        exit(1);
    }
}

void load_file(void) {
    FILE *f = fopen(filename, "r");

    if (!f)
        return;

    free(lines[0]);
    line_count = 0;

    char buffer[MAX_LINE];

    while (fgets(buffer, sizeof(buffer), f) &&
           line_count < MAX_LINES) {

        buffer[strcspn(buffer, "\r\n")] = '\0';

        lines[line_count] = calloc(MAX_LINE, 1);

        if (!lines[line_count])
            break;

        strncpy(lines[line_count], buffer, MAX_LINE - 1);
        line_count++;
    }

    fclose(f);

    if (line_count == 0)
        init_lines();

    dirty = 0;
}

void save_file(void) {
    FILE *f = fopen(filename, "w");

    if (!f) {
        snprintf(status_message, sizeof(status_message),
                 "ERROR: Cannot save file");
        return;
    }

    for (int i = 0; i < line_count; i++)
        fprintf(f, "%s\n", lines[i]);

    if (fclose(f) != 0) {
        snprintf(status_message, sizeof(status_message),
                 "ERROR: Failed to finish saving");
        return;
    }

    dirty = 0;

    snprintf(status_message, sizeof(status_message),
             "Saved: %s", filename);
}

/* COLORS */

void init_colors(void) {
    start_color();
    use_default_colors();

    init_pair(1, COLOR_BLACK, COLOR_CYAN);
    init_pair(2, COLOR_YELLOW, -1);
    init_pair(3, COLOR_GREEN, -1);
    init_pair(4, COLOR_RED, -1);
    init_pair(5, COLOR_CYAN, -1);
    init_pair(6, COLOR_MAGENTA, -1);
    init_pair(7, COLOR_BLUE, -1);
}

/* SYNTAX HIGHLIGHTING */

void draw_line(int row, int index, int *block_comment) {
    const char *s = lines[index] + col_offset;
    int col = 0;

    const char *original = lines[index];
    int first = 0;

    while (original[first] == ' ' ||
           original[first] == '\t')
        first++;

    if (is_c_family() && original[first] == '#') {
        attron(COLOR_PAIR(6));
        mvaddnstr(row, 5, s, COLS - 5);
        attroff(COLOR_PAIR(6));
        return;
    }

    while (*s && col < COLS - 5) {

        if (is_c_family() && *block_comment) {
            attron(COLOR_PAIR(4));

            if (s[0] == '*' && s[1] == '/') {
                mvaddch(row, col + 5, *s++);
                col++;

                if (*s) {
                    mvaddch(row, col + 5, *s++);
                    col++;
                }

                *block_comment = 0;
            } else {
                mvaddch(row, col + 5, *s++);
                col++;
            }

            attroff(COLOR_PAIR(4));
            continue;
        }

        if (is_c_family() &&
            s[0] == '/' && s[1] == '*') {

            attron(COLOR_PAIR(4));

            mvaddch(row, col + 5, *s++);
            col++;

            mvaddch(row, col + 5, *s++);
            col++;

            attroff(COLOR_PAIR(4));

            *block_comment = 1;
            continue;
        }

        if (((language == LANG_PYTHON ||
              language == LANG_BASH) && *s == '#') ||
            (is_c_family() &&
             s[0] == '/' && s[1] == '/')) {

            attron(COLOR_PAIR(4));

            while (*s && col < COLS - 5) {
                mvaddch(row, col + 5, *s++);
                col++;
            }

            attroff(COLOR_PAIR(4));
            break;
        }

        if (*s == '\'' || *s == '"') {
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

/* EDITOR DISPLAY */

void draw(void) {
    erase();

    int height = LINES - 3;
    int block_comment = 0;

    for (int i = 0; i < row_offset; i++) {
        if (is_c_family()) {
            const char *p = lines[i];

            while (*p) {
                if (!block_comment &&
                    p[0] == '/' && p[1] == '*') {
                    block_comment = 1;
                    p += 2;
                } else if (block_comment &&
                           p[0] == '*' && p[1] == '/') {
                    block_comment = 0;
                    p += 2;
                } else {
                    p++;
                }
            }
        }
    }

    for (int i = 0; i < height; i++) {
        int index = row_offset + i;

        if (index >= line_count)
            break;

        attron(COLOR_PAIR(5));
        mvprintw(i, 0, "%4d ", index + 1);
        attroff(COLOR_PAIR(5));

        draw_line(i, index, &block_comment);
    }

    attron(COLOR_PAIR(1));

    mvprintw(LINES - 3, 0,
             " TOBEN ENGINE v%s | %s | %s | %s ",
             VERSION,
             language_name(),
             filename,
             dirty ? "MODIFIED" : "SAVED");

    clrtoeol();
    attroff(COLOR_PAIR(1));

    mvprintw(LINES - 2, 0,
             " CTRL+S Save | CTRL+R Build & Run | CTRL+X Exit ");

    mvprintw(LINES - 1, 0, " %s ", status_message);
    clrtoeol();

    int screen_y = cy - row_offset;
    int screen_x = cx - col_offset + 5;

    if (screen_y >= 0 && screen_y < height &&
        screen_x >= 5 && screen_x < COLS)
        move(screen_y, screen_x);

    refresh();
}

/* TEXT EDITING */

void insert_char(int c) {
    int len = strlen(lines[cy]);

    if (len >= MAX_LINE - 2 || cx > len)
        return;

    memmove(lines[cy] + cx + 1,
            lines[cy] + cx,
            len - cx + 1);

    lines[cy][cx++] = c;
    dirty = 1;
}

void new_line(void) {
    if (line_count >= MAX_LINES)
        return;

    char *new_line_buffer = calloc(MAX_LINE, 1);

    if (!new_line_buffer)
        return;

    for (int i = line_count; i > cy + 1; i--)
        lines[i] = lines[i - 1];

    strcpy(new_line_buffer, lines[cy] + cx);

    lines[cy + 1] = new_line_buffer;
    lines[cy][cx] = '\0';

    cy++;
    cx = 0;
    line_count++;
    dirty = 1;
}

void backspace(void) {
    if (cx > 0) {
        int len = strlen(lines[cy]);

        memmove(lines[cy] + cx - 1,
                lines[cy] + cx,
                len - cx + 1);

        cx--;
        dirty = 1;

    } else if (cy > 0) {
        int previous_len = strlen(lines[cy - 1]);
        int current_len = strlen(lines[cy]);

        if (previous_len + current_len < MAX_LINE - 1) {
            strcat(lines[cy - 1], lines[cy]);

            free(lines[cy]);

            for (int i = cy; i < line_count - 1; i++)
                lines[i] = lines[i + 1];

            line_count--;
            cy--;
            cx = previous_len;
            dirty = 1;
        }
    }
}

void delete_char(void) {
    int len = strlen(lines[cy]);

    if (cx < len) {
        memmove(lines[cy] + cx,
                lines[cy] + cx + 1,
                len - cx);

        dirty = 1;

    } else if (cy < line_count - 1 &&
               len + strlen(lines[cy + 1]) < MAX_LINE - 1) {

        strcat(lines[cy], lines[cy + 1]);
        free(lines[cy + 1]);

        for (int i = cy + 1; i < line_count - 1; i++)
            lines[i] = lines[i + 1];

        line_count--;
        dirty = 1;
    }
}

/* BUILD AND RUN SYSTEM */

void run_process(char *program, char **args) {
    pid_t pid = fork();

    if (pid == 0) {
        execvp(program, args);
        perror("Toben: execution failed");
        _exit(127);
    }

    if (pid < 0) {
        perror("Toben: fork failed");
        return;
    }

    int status;

    if (waitpid(pid, &status, 0) < 0)
        perror("Toben: waitpid failed");
}

void build_and_run(void) {
    if (dirty)
        save_file();

    if (dirty) {
        snprintf(status_message, sizeof(status_message),
                 "Cannot run: save failed");
        return;
    }

    endwin();

    printf("\n");
    printf("====================================\n");
    printf("       TOBEN ENGINE v%s\n", VERSION);
    printf("       EARLY ACCESS BUILD\n");
    printf("====================================\n");
    printf("Language: %s\n", language_name());
    printf("File: %s\n\n", filename);

    if (language == LANG_PYTHON ||
        language == LANG_BASH) {

        const char *interpreter =
            language == LANG_BASH ? "bash" : "python3";

        char *args[] = {
            (char *)interpreter,
            filename,
            NULL
        };

        printf("[TOBEN] Running %s...\n\n",
               language_name());

        run_process((char *)interpreter, args);

    } else {
        const char *compiler =
            language == LANG_CPP ? "clang++" : "clang";

        char *output = "/tmp/toben_program";

        char *compile_args[] = {
            (char *)compiler,
            "-Wall",
            "-Wextra",
            language == LANG_CPP ? "-std=c++17" : "-std=c11",
            filename,
            "-o",
            output,
            NULL
        };

        printf("[TOBEN] Compiling with %s...\n\n", compiler);

        pid_t pid = fork();

        if (pid == 0) {
            execvp(compiler, compile_args);
            perror("Compiler execution failed");
            _exit(127);
        }

        if (pid < 0) {
            perror("Toben: fork failed");
        } else {
            int status;

            if (waitpid(pid, &status, 0) < 0) {
                perror("Toben: waitpid failed");
            } else if (WIFEXITED(status) &&
                       WEXITSTATUS(status) == 0) {

                printf("\n[TOBEN] Build successful!\n");
                printf("[TOBEN] Running program...\n\n");

                char *run_args[] = {
                    output,
                    NULL
                };

                run_process(output, run_args);

            } else {
                printf("\n[TOBEN] Build failed.\n");
            }
        }
    }

    printf("\n\n[TOBEN] Execution finished.");
    printf("\nPress ENTER to return to editor...");

    int input;
    while ((input = getchar()) != '\n' && input != EOF) {
    }

    initscr();
    raw();
    noecho();
    keypad(stdscr, TRUE);
    init_colors();

    snprintf(status_message, sizeof(status_message),
             "Execution finished");
}

/* MAIN */

int main(int argc, char **argv) {
    if (argc > 1)
        snprintf(filename, sizeof(filename), "%s", argv[1]);

    detect_language();

    init_lines();
    load_file();

    initscr();
    raw();
    noecho();
    keypad(stdscr, TRUE);
    init_colors();

    while (1) {
        int height = LINES - 3;

        if (cy < row_offset)
            row_offset = cy;

        if (cy >= row_offset + height)
            row_offset = cy - height + 1;

        if (cx < col_offset)
            col_offset = cx;

        if (cx >= col_offset + COLS - 5)
            col_offset = cx - COLS + 6;

        draw();

        int ch = getch();

        if (ch == 19) {
            save_file();

        } else if (ch == 24) {
            break;

        } else if (ch == 18) {
            build_and_run();

        } else if (ch == KEY_UP && cy > 0) {
            cy--;

            if ((size_t)cx > strlen(lines[cy]))
                cx = strlen(lines[cy]);

        } else if (ch == KEY_DOWN && cy < line_count - 1) {
            cy++;

            if ((size_t)cx > strlen(lines[cy]))
                cx = strlen(lines[cy]);

        } else if (ch == KEY_LEFT) {
            if (cx > 0) {
                cx--;
            } else if (cy > 0) {
                cy--;
                cx = strlen(lines[cy]);
            }

        } else if (ch == KEY_RIGHT) {
            if ((size_t)cx < strlen(lines[cy])) {
                cx++;
            } else if (cy < line_count - 1) {
                cy++;
                cx = 0;
            }

        } else if (ch == KEY_HOME) {
            cx = 0;

        } else if (ch == KEY_END) {
            cx = strlen(lines[cy]);

        } else if (ch == KEY_BACKSPACE ||
                   ch == 127 || ch == 8) {
            backspace();

        } else if (ch == KEY_DC) {
            delete_char();

        } else if (ch == '\n' || ch == KEY_ENTER) {
            new_line();

        } else if (ch == '\t') {
            for (int i = 0; i < 4; i++)
                insert_char(' ');

        } else if (ch >= 32 && ch <= 126) {
            insert_char(ch);
        }
    }

    endwin();

    if (dirty)
        fprintf(stderr, "Toben: Warning: unsaved changes lost.\n");

    for (int i = 0; i < line_count; i++)
        free(lines[i]);

    return 0;
}
