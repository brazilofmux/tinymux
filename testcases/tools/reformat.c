
#line 1 "reformat.rl"
/*
 * reformat.rl -- Ragel -G2 MUX softcode reformatter
 *
 * The inverse of unformat: takes single-line MUX commands (e.g., the
 * output of unformat) and re-introduces indentation based on brace
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
 * so the output is a valid .mux file that unformat can read back.
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


#line 144 "reformat.rl"



#line 73 "reformat.c"
static const int reformat_start = 1;
static const int reformat_first_final = 1;
static const int reformat_error = -1;

static const int reformat_en_main = 1;


#line 147 "reformat.rl"

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

    
#line 100 "reformat.c"
	{
	cs = reformat_start;
	ts = 0;
	te = 0;
	act = 0;
	}

#line 169 "reformat.rl"
    
#line 106 "reformat.c"
	{
	if ( p == pe )
		goto _test_eof;
	switch ( cs )
	{
tr0:
#line 106 "reformat.rl"
	{{p = ((te))-1;}{
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
        }}
	goto st1;
tr1:
#line 77 "reformat.rl"
	{te = p+1;{
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
        }}
	goto st1;
tr2:
#line 141 "reformat.rl"
	{te = p+1;{ emit_pending(); putchar(*ts); }}
	goto st1;
tr3:
#line 133 "reformat.rl"
	{te = p+1;{ emit_pending(); pdepth++; putchar('('); }}
	goto st1;
tr4:
#line 134 "reformat.rl"
	{te = p+1;{ emit_pending(); if (pdepth > 0) pdepth--; putchar(')'); }}
	goto st1;
tr5:
#line 124 "reformat.rl"
	{te = p+1;{
            emit_pending();
            putchar(';');
            if (pdepth == 0 && bdepth == 0) {
                schedule_break(depth);
            }
        }}
	goto st1;
tr6:
#line 137 "reformat.rl"
	{te = p+1;{ emit_pending(); bdepth++; putchar('['); }}
	goto st1;
tr7:
#line 138 "reformat.rl"
	{te = p+1;{ emit_pending(); if (bdepth > 0) bdepth--; putchar(']'); }}
	goto st1;
tr8:
#line 96 "reformat.rl"
	{te = p+1;{
            emit_pending();
            putchar('{');
            if (pdepth == 0 && bdepth == 0) {
                depth++;
                schedule_break(depth);
            }
        }}
	goto st1;
tr10:
#line 106 "reformat.rl"
	{te = p;p--;{
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
        }}
	goto st1;
st1:
#line 1 "NONE"
	{ts = 0;}
	if ( ++p == pe )
		goto _test_eof1;
case 1:
#line 1 "NONE"
	{ts = p;}
#line 206 "reformat.c"
	switch( (*p) ) {
		case 40: goto tr3;
		case 41: goto tr4;
		case 59: goto tr5;
		case 91: goto tr6;
		case 93: goto tr7;
		case 123: goto tr8;
		case 125: goto tr9;
	}
	goto tr2;
tr9:
#line 1 "NONE"
	{te = p+1;}
	goto st2;
st2:
	if ( ++p == pe )
		goto _test_eof2;
case 2:
#line 223 "reformat.c"
	if ( (*p) == 44 )
		goto st0;
	goto tr10;
st0:
	if ( ++p == pe )
		goto _test_eof0;
case 0:
	if ( (*p) == 123 )
		goto tr1;
	goto tr0;
	}
	_test_eof1: cs = 1; goto _test_eof; 
	_test_eof2: cs = 2; goto _test_eof; 
	_test_eof0: cs = 0; goto _test_eof; 

	_test_eof: {}
	if ( p == eof )
	{
	switch ( cs ) {
	case 2: goto tr10;
	case 0: goto tr0;
	}
	}

	}

#line 170 "reformat.rl"

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

        /* Skip blank lines (unformat inserts these between commands). */
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
