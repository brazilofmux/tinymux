/*
 * unformat.rl -- Ragel -G2 replacement for unformat.pl
 *
 * Takes formatted MUX softcode (.mux files) and joins continuation lines,
 * strips comments, and handles #include directives.  Output is one long
 * line per command, suitable for uploading into a MUX server.
 *
 * Formatting rules:
 *   - Lines starting with '#' are comments (skipped), except #include.
 *   - #include <file> includes that file (with cycle detection).
 *   - Lines starting with non-whitespace begin a new command.
 *   - Continuation lines (leading whitespace) have whitespace stripped
 *     and are appended directly -- NO space is inserted.
 *   - A trailing ' \' (SPACE BACKSLASH) before newline inserts a space
 *     at the join point.  The ' \' is consumed, not emitted.  A bare
 *     trailing '\' without a preceding space is emitted literally
 *     (backslash is a valid softcode escape character).
 *   - A line containing only '-' ends the current command.
 *   - Empty/blank lines are skipped.
 *   - Warning (stderr): digit-alpha merge at a continuation join point
 *     without ' \' -- almost always a missing space.
 *
 * Usage: unformat file1.mux [file2.mux ...]
 *        Output goes to stdout; warnings go to stderr.
 *
 * Build: ragel -G2 -o unformat.c unformat.rl
 *        cc -O2 -o unformat unformat.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_INCLUDES 256
#define MAX_LINE 65536

static const char *included_files[MAX_INCLUDES];
static int num_included;

static int in_command = 0;
static int pending_space = 0;
static char last_emitted = 0;
static int line_number = 0;
static int cmd_start_line = 0;     /* line where current command began */
static const char *cmd_start_file = "";
static const char *current_file = "";

/* ---------- helpers ---------- */

static int is_digit(char c) { return c >= '0' && c <= '9'; }
static int is_alpha(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static int include_once(const char *path)
{
    int i;
    for (i = 0; i < num_included; i++) {
        if (strcmp(included_files[i], path) == 0) {
            return 0;
        }
    }
    if (num_included >= MAX_INCLUDES) {
        fprintf(stderr, "unformat: too many #include files\n");
        return 0;
    }
    included_files[num_included++] = strdup(path);
    return 1;
}

static void process_file(const char *filename);

static void flush_command_ex(int explicit_end)
{
    if (in_command) {
        if (!explicit_end) {
            fprintf(stderr,
                    "%s:%d: warning: command has no '-' terminator\n",
                    cmd_start_file, cmd_start_line);
        }
        putchar('\n');
        putchar('\n');  /* extraspace */
        in_command = 0;
        pending_space = 0;
        last_emitted = 0;
    }
}

/* Convenience: flush without explicit '-'. */
static void flush_command(void)
{
    flush_command_ex(0);
}

static void do_include(const char *start, const char *end)
{
    char path[MAX_LINE];
    size_t len;

    while (start < end && (*start == ' ' || *start == '\t'))
        start++;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
        end--;
    len = (size_t)(end - start);
    if (len == 0 || len >= MAX_LINE) return;
    memcpy(path, start, len);
    path[len] = '\0';
    if (include_once(path)) {
        process_file(path);
    } else {
        fprintf(stderr,
                "%s:%d: warning: file '%s' included more than once\n",
                current_file, line_number, path);
    }
}

/* Emit one character, tracking last_emitted. */
static void emit(char c)
{
    putchar(c);
    last_emitted = c;
}

/* Called at the start of a continuation segment.
 * Handles pending_space from a prior trailing ' \',
 * and warns on digit-alpha merges.
 */
static void join_check(char first_char)
{
    if (pending_space) {
        emit(' ');
        pending_space = 0;
    } else if (in_command && is_digit(last_emitted) && is_alpha(first_char)) {
        fprintf(stderr,
                "%s:%d: warning: digit-alpha merge '%c%c'"
                " at join -- did you mean to end the"
                " previous line with ' \\'?\n",
                current_file, line_number,
                last_emitted, first_char);
    }
}

/*
 * Ragel scanner.
 *
 * Each line is classified at BOL.  Content characters are emitted
 * through emit().  Only ' \' (space-backslash) immediately before
 * newline triggers a pending space; a bare trailing '\' (no preceding
 * space) is emitted literally.
 */

%%{
    machine unformat;

    action nl           { line_number++; }
    action inc_mark     { inc_start = fpc; }
    action do_include   { do_include(inc_start, fpc); }
    action emit_char    { emit(fc); }
    action start_cmd {
        flush_command();
        in_command = 1;
        emit(fc);
    }
    action start_cmd_dash {
        flush_command();
        in_command = 1;
        emit('-');
        emit(fc);
    }
    action end_cmd { flush_command_ex(1); }
    action cont_first {
        join_check(fc);
        emit(fc);
    }

    # -- Line content building blocks --
    #
    # ' \' (space-backslash) at EOL: consume both, set pending_space.
    # Bare '\' at EOL (no preceding space): emit '\' literally.
    # '\' mid-line: always emitted literally.

    # Normal content character (not \ or \n or space).
    nchar = (any - '\n\\ ') $emit_char;

    # Space that is NOT part of ' \' at EOL.
    # We handle this by: when we see ' ', peek ahead.
    # In Ragel we express: space followed by '\' then '\n' is the
    # continuation marker.  Space followed by anything else is normal.
    sp_normal = ' ' $emit_char;

    # Backslash mid-line: '\' followed by non-newline.  Emit '\'.
    bs_mid = '\\' $emit_char (any - '\n') $emit_char;

    # Bare backslash at EOL (no preceding space): '\' then '\n'.
    # We only reach this if there was no space before the '\'.
    # We emit the '\' literally.
    bs_eol = '\\' @emit_char '\n' @nl;

    # Space-backslash at EOL: ' \' then '\n'.  Consume both, set
    # pending_space.  The space is NOT emitted.
    sp_bs_eol = ' \\' '\n' @{ pending_space = 1; } @nl;

    # Plain newline.
    eol = '\n' @nl;

    # Content: a mix of normal chars, spaces, and backslashes,
    # terminated by one of the EOL variants.
    #
    # Ordering matters for the alternation at the end: sp_bs_eol must
    # be tried before sp_normal (the space) would consume the ' '.
    # We achieve this by using longest-match via the priority operator
    # or by restructuring: accumulate non-terminal chars, then match
    # the line ending.
    #
    # Restructured approach: the body is everything up to the last
    # 0-2 characters before \n.  We handle the endings explicitly.

    # A "safe" character: not \n, not \, not space.
    safe = any - '\n\\ ';

    # Content token: safe char, or space not followed by \\n, or
    # backslash not at eol.
    # We'll build the body as a loop with priority-guarded exits.

    # Mid-line content (not the final 0-2 chars before \n):
    mid = safe $emit_char
        | ' ' $emit_char
        | '\\' $emit_char;

    # Line tail variants (tested with priorities for longest match):
    tail = sp_bs_eol        # ' \' '\n' — continuation space
         | bs_eol           # '\n' with bare '\'
         | eol              # plain newline
         ;

    # The problem: mid consumes ' ' and '\' greedily, so by the time
    # we reach tail, the ' \' of sp_bs_eol is already consumed.
    #
    # Fix: pull the last space and/or backslash out of mid using
    # Ragel's longest-match (|*) or manual lookahead.
    #
    # Simplest correct approach: buffer the last 1-2 chars and decide
    # at \n time.  Ragel can't easily do lookahead, so we use a tiny
    # buffer in C.

    # -- REVISED: segment-based approach --
    #
    # Instead of char-by-char emit with Ragel trying to do lookahead,
    # capture each line's content as a segment (mark..fpc), and let C
    # handle the trailing ' \' detection.  This is cleaner.

    action mark { mark = fpc; }
    action cmd_start_seg {
        flush_command();
        in_command = 1;
        pending_space = 0;
        cmd_start_line = line_number;
        cmd_start_file = current_file;
        emit_segment(mark, (size_t)(fpc - mark), 0);
    }
    action cont_seg {
        emit_segment(mark, (size_t)(fpc - mark), 1);
    }

    # -- Line types --

    # Content of a line (everything before \n).
    content = (any - '\n')*;

    blank_line   = [ \t]* '\n' @nl;
    comment_body = (any - '\n')* '\n' @nl;
    include_line = '#include' [ \t]+
                   ( (any - '\n')+ >inc_mark )
                   '\n' @do_include @nl;
    comment_line = '#' comment_body;
    end_marker   = '-' '\n' @end_cmd @nl;

    # Command start: first char is non-ws, non-#, non-newline.
    # '-' alone is end_marker; '-' followed by more text is a command.
    cmd_start = ((any - [ \t\n#\-]) (any - '\n')*) >mark;
    dash_start = ('-' (any - '\n') (any - '\n')*) >mark;
    cmd_line = (cmd_start | dash_start) '\n' @cmd_start_seg @nl;

    # Continuation: leading whitespace stripped, then content.
    cont_ws    = [ \t]+;
    cont_body  = ((any - [ \t\n]) (any - '\n')*) >mark;
    cont_line  = cont_ws cont_body '\n' @cont_seg @nl
               | cont_ws '\n' @nl;  # all-whitespace continuation = blank

    line = blank_line | include_line | comment_line
         | end_marker | cont_line | cmd_line;

    main := line**;
}%%

%% write data;

/*
 * Emit a captured line segment.  Detects trailing ' \' and handles
 * the join logic (pending_space, digit-alpha warning).
 */
static void emit_segment(const char *seg, size_t len, int is_continuation)
{
    int has_sp_bs = 0;

    /* Check for trailing ' \' (space-backslash). */
    if (len >= 2 && seg[len - 1] == '\\' && seg[len - 2] == ' ') {
        has_sp_bs = 1;
        len -= 2;  /* strip the ' \' */
    }

    if (is_continuation && len > 0) {
        join_check(seg[0]);
    }

    /* Emit the segment content. */
    if (len > 0) {
        size_t i;
        for (i = 0; i < len; i++) {
            emit(seg[i]);
        }
    }

    if (has_sp_bs) {
        pending_space = 1;
    }
}

static void process_buffer(const char *buf, size_t len)
{
    const char *p = buf;
    const char *pe = buf + len;
    const char *eof __attribute__((unused)) = pe;
    const char *inc_start = NULL;
    const char *mark = NULL;
    int cs;

    (void)unformat_en_main;
    (void)unformat_first_final;
    (void)unformat_error;

    %% write init;
    %% write exec;

    if (cs < %%{ write first_final; }%%) {
        fprintf(stderr, "%s:%d: parse error at offset %ld\n",
                current_file, line_number, (long)(p - buf));
    }
}

static void process_file(const char *filename)
{
    FILE *fp;
    char *buf;
    long fsize;
    const char *save_file;
    int save_line;

    fp = fopen(filename, "r");
    if (!fp) {
        /* If called from #include, current_file/line_number point
         * to the directive.  If called from main, they're empty/0.
         */
        if (current_file[0]) {
            fprintf(stderr,
                    "%s:%d: error: can't open '%s'\n",
                    current_file, line_number, filename);
        } else {
            fprintf(stderr,
                    "unformat: error: can't open '%s'\n", filename);
        }
        return;
    }

    fseek(fp, 0, SEEK_END);
    fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (fsize <= 0) {
        fclose(fp);
        return;
    }

    buf = (char *)malloc((size_t)fsize);
    if (!buf) {
        fprintf(stderr, "unformat: out of memory\n");
        fclose(fp);
        return;
    }

    fsize = (long)fread(buf, 1, (size_t)fsize, fp);
    fclose(fp);

    save_file = current_file;
    save_line = line_number;
    current_file = filename;
    line_number = 1;

    process_buffer(buf, (size_t)fsize);

    current_file = save_file;
    line_number = save_line;

    free(buf);
}

int main(int argc, char *argv[])
{
    int i;

    if (argc < 2) {
        fprintf(stderr, "usage: unformat file1.mux [file2.mux ...]\n");
        return 1;
    }

    for (i = 1; i < argc; i++) {
        process_file(argv[i]);

        /* Flush any unterminated command while file context
         * is still meaningful for the warning message.
         */
        if (in_command) {
            current_file = argv[i];
            /* line_number is already restored, but cmd_start_file
             * and cmd_start_line still point to the right place.
             */
            flush_command();
        }
        putchar('\n');
    }

    printf("\nthink Uploaded.\n");

    for (i = 0; i < num_included; i++) {
        free((void *)included_files[i]);
    }

    return 0;
}
