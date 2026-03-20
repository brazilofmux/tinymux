
#line 1 "unformat.rl"
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


#line 295 "unformat.rl"



#line 156 "unformat.c"
static const int unformat_start = 16;
static const int unformat_first_final = 16;
static const int unformat_error = -1;

static const int unformat_en_main = 16;


#line 298 "unformat.rl"

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

    
#line 207 "unformat.c"
	{
	cs = unformat_start;
	}

#line 344 "unformat.rl"
    
#line 210 "unformat.c"
	{
	if ( p == pe )
		goto _test_eof;
	switch ( cs )
	{
tr1:
#line 254 "unformat.rl"
	{
        flush_command();
        in_command = 1;
        pending_space = 0;
        cmd_start_line = line_number;
        cmd_start_file = current_file;
        emit_segment(mark, (size_t)(p - mark), 0);
    }
#line 156 "unformat.rl"
	{ line_number++; }
	goto st16;
tr4:
#line 156 "unformat.rl"
	{ line_number++; }
	goto st16;
tr6:
#line 262 "unformat.rl"
	{
        emit_segment(mark, (size_t)(p - mark), 1);
    }
#line 156 "unformat.rl"
	{ line_number++; }
	goto st16;
tr19:
#line 158 "unformat.rl"
	{ do_include(inc_start, p); }
#line 156 "unformat.rl"
	{ line_number++; }
	goto st16;
tr20:
#line 171 "unformat.rl"
	{ flush_command_ex(1); }
#line 156 "unformat.rl"
	{ line_number++; }
	goto st16;
st16:
	if ( ++p == pe )
		goto _test_eof16;
case 16:
#line 247 "unformat.c"
	switch( (*p) ) {
		case 9: goto st1;
		case 10: goto tr4;
		case 32: goto st1;
		case 35: goto st3;
		case 45: goto tr23;
	}
	goto tr21;
tr21:
#line 253 "unformat.rl"
	{ mark = p; }
	goto st0;
st0:
	if ( ++p == pe )
		goto _test_eof0;
case 0:
#line 262 "unformat.c"
	if ( (*p) == 10 )
		goto tr1;
	goto st0;
st1:
	if ( ++p == pe )
		goto _test_eof1;
case 1:
	switch( (*p) ) {
		case 9: goto st1;
		case 10: goto tr4;
		case 32: goto st1;
	}
	goto tr2;
tr2:
#line 253 "unformat.rl"
	{ mark = p; }
	goto st2;
st2:
	if ( ++p == pe )
		goto _test_eof2;
case 2:
#line 282 "unformat.c"
	if ( (*p) == 10 )
		goto tr6;
	goto st2;
st3:
	if ( ++p == pe )
		goto _test_eof3;
case 3:
	switch( (*p) ) {
		case 10: goto tr4;
		case 105: goto st5;
	}
	goto st4;
st4:
	if ( ++p == pe )
		goto _test_eof4;
case 4:
	if ( (*p) == 10 )
		goto tr4;
	goto st4;
st5:
	if ( ++p == pe )
		goto _test_eof5;
case 5:
	switch( (*p) ) {
		case 10: goto tr4;
		case 110: goto st6;
	}
	goto st4;
st6:
	if ( ++p == pe )
		goto _test_eof6;
case 6:
	switch( (*p) ) {
		case 10: goto tr4;
		case 99: goto st7;
	}
	goto st4;
st7:
	if ( ++p == pe )
		goto _test_eof7;
case 7:
	switch( (*p) ) {
		case 10: goto tr4;
		case 108: goto st8;
	}
	goto st4;
st8:
	if ( ++p == pe )
		goto _test_eof8;
case 8:
	switch( (*p) ) {
		case 10: goto tr4;
		case 117: goto st9;
	}
	goto st4;
st9:
	if ( ++p == pe )
		goto _test_eof9;
case 9:
	switch( (*p) ) {
		case 10: goto tr4;
		case 100: goto st10;
	}
	goto st4;
st10:
	if ( ++p == pe )
		goto _test_eof10;
case 10:
	switch( (*p) ) {
		case 10: goto tr4;
		case 101: goto st11;
	}
	goto st4;
st11:
	if ( ++p == pe )
		goto _test_eof11;
case 11:
	switch( (*p) ) {
		case 9: goto st12;
		case 10: goto tr4;
		case 32: goto st12;
	}
	goto st4;
st12:
	if ( ++p == pe )
		goto _test_eof12;
case 12:
	switch( (*p) ) {
		case 9: goto tr17;
		case 10: goto tr4;
		case 32: goto tr17;
	}
	goto tr16;
tr16:
#line 157 "unformat.rl"
	{ inc_start = p; }
	goto st13;
st13:
	if ( ++p == pe )
		goto _test_eof13;
case 13:
#line 382 "unformat.c"
	if ( (*p) == 10 )
		goto tr19;
	goto st13;
tr17:
#line 157 "unformat.rl"
	{ inc_start = p; }
	goto st14;
st14:
	if ( ++p == pe )
		goto _test_eof14;
case 14:
#line 392 "unformat.c"
	switch( (*p) ) {
		case 9: goto tr17;
		case 10: goto tr19;
		case 32: goto tr17;
	}
	goto tr16;
tr23:
#line 253 "unformat.rl"
	{ mark = p; }
	goto st15;
st15:
	if ( ++p == pe )
		goto _test_eof15;
case 15:
#line 405 "unformat.c"
	if ( (*p) == 10 )
		goto tr20;
	goto st0;
	}
	_test_eof16: cs = 16; goto _test_eof; 
	_test_eof0: cs = 0; goto _test_eof; 
	_test_eof1: cs = 1; goto _test_eof; 
	_test_eof2: cs = 2; goto _test_eof; 
	_test_eof3: cs = 3; goto _test_eof; 
	_test_eof4: cs = 4; goto _test_eof; 
	_test_eof5: cs = 5; goto _test_eof; 
	_test_eof6: cs = 6; goto _test_eof; 
	_test_eof7: cs = 7; goto _test_eof; 
	_test_eof8: cs = 8; goto _test_eof; 
	_test_eof9: cs = 9; goto _test_eof; 
	_test_eof10: cs = 10; goto _test_eof; 
	_test_eof11: cs = 11; goto _test_eof; 
	_test_eof12: cs = 12; goto _test_eof; 
	_test_eof13: cs = 13; goto _test_eof; 
	_test_eof14: cs = 14; goto _test_eof; 
	_test_eof15: cs = 15; goto _test_eof; 

	_test_eof: {}
	}

#line 345 "unformat.rl"

    if (cs < 16) {
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
