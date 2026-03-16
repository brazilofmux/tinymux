/*
 * reformat.rl -- Ragel -G2 MUX softcode reformatter
 *
 * The inverse of unreformat: takes single-line MUX commands (e.g., the
 * output of unreformat) and re-introduces indentation based on brace
 * structure, producing human-readable .mux files.
 *
 * Break rules (only when paren_depth == 0 AND bracket_depth == 0):
 *   - After '{'   -- increase indent, break
 *   - Before '}'  -- decrease indent, break
 *   - After ';'   -- break at same indent
 *   - '},{' kept together on one line (common @if/@switch pattern)
 *   - '{}' kept together (empty braces, no break)
 *
 * All continuation lines start with whitespace (4 + depth*4 spaces)
 * so the output is a valid .mux file that unreformat can read back.
 *
 * Usage: reformat [file ...]   (reads stdin if no files given)
 *        Output goes to stdout.
 *
 * Build: ragel -G2 -o reformat.c reformat.rl
 *        cc -O2 -o reformat reformat.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INDENT_WIDTH 4

static int depth;
static int pdepth;          /* parenthesis depth */
static int bdepth;          /* bracket depth */
static int pending_break;   /* break scheduled before next content */
static int break_depth;     /* indent depth for the pending break */

static void emit_spaces(int n)
{
    int i;
    for (i = 0; i < n; i++)
        putchar(' ');
}

/* If a break is pending, emit newline + indentation now. */
static void emit_pending(void)
{
    if (pending_break) {
        putchar('\n');
        emit_spaces(INDENT_WIDTH + break_depth * INDENT_WIDTH);
        pending_break = 0;
    }
}

/* Schedule a break before the next content character. */
static void schedule_break(int d)
{
    pending_break = 1;
    break_depth = d;
}

/* Emit a break immediately (for '}' which needs to appear on the
 * new line, not deferred until next content). */
static void emit_break_now(int d)
{
    putchar('\n');
    emit_spaces(INDENT_WIDTH + d * INDENT_WIDTH);
    pending_break = 0;
}

%%{
    machine reformat;

    main := |*

        # Close-open brace: },{ — common @if/@switch case transition.
        # Keep on one line, dedented to the outer level.
        '},{' => {
            if (pdepth == 0 && bdepth == 0 && depth > 0) {
                depth--;
                if (pending_break) {
                    /* Empty block before: {},{  */
                    pending_break = 0;
                } else {
                    emit_break_now(depth);
                }
                fputs("},{", stdout);
                depth++;
                schedule_break(depth);
            } else {
                emit_pending();
                fputs("},{", stdout);
            }
        };

        # Open brace: increase indent, schedule break.
        '{' => {
            emit_pending();
            putchar('{');
            if (pdepth == 0 && bdepth == 0) {
                depth++;
                schedule_break(depth);
            }
        };

        # Close brace: decrease indent, break before '}'.
        '}' => {
            if (pdepth == 0 && bdepth == 0 && depth > 0) {
                depth--;
                if (pending_break) {
                    /* Empty braces: {} — no break, keep together. */
                    pending_break = 0;
                    putchar('}');
                } else {
                    emit_break_now(depth);
                    putchar('}');
                }
            } else {
                emit_pending();
                putchar('}');
            }
        };

        # Semicolon: break after, same indent level.
        ';' => {
            emit_pending();
            putchar(';');
            if (pdepth == 0 && bdepth == 0) {
                schedule_break(depth);
            }
        };

        # Parentheses: track depth, no reformatting.
        '(' => { emit_pending(); pdepth++; putchar('('); };
        ')' => { emit_pending(); if (pdepth > 0) pdepth--; putchar(')'); };

        # Brackets: track depth, no reformatting.
        '[' => { emit_pending(); bdepth++; putchar('['); };
        ']' => { emit_pending(); if (bdepth > 0) bdepth--; putchar(']'); };

        # Everything else: emit directly.
        any => { emit_pending(); putchar(*ts); };

    *|;
}%%

%% write data;

static void reformat_line(const char *line, size_t len)
{
    const char *p = line;
    const char *pe = line + len;
    const char *eof = pe;
    const char *ts;
    const char *te;
    int cs, act;

    depth = 0;
    pdepth = 0;
    bdepth = 0;
    pending_break = 0;

    (void)reformat_en_main;
    (void)reformat_error;
    (void)reformat_first_final;
    (void)eof;
    (void)act;

    %% write init;
    %% write exec;

    /* Discard any trailing pending break (e.g., line ends with ;). */
    pending_break = 0;
}

static void process_stream(FILE *fp)
{
    char *line = NULL;
    size_t cap = 0;
    ssize_t len;

    while ((len = getline(&line, &cap, fp)) > 0) {
        /* Strip trailing newline/CR. */
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            len--;

        /* Skip blank lines (unreformat inserts these between commands). */
        if (len == 0) continue;

        reformat_line(line, (size_t)len);
        printf("\n-\n");
    }
    free(line);
}

int main(int argc, char *argv[])
{
    int i;

    if (argc < 2) {
        process_stream(stdin);
    } else {
        for (i = 1; i < argc; i++) {
            FILE *fp = fopen(argv[i], "r");
            if (!fp) {
                fprintf(stderr, "reformat: can't open %s: ", argv[i]);
                perror(NULL);
                continue;
            }
            process_stream(fp);
            fclose(fp);
        }
    }

    return 0;
}
