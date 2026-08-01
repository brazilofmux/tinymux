
#line 1 "ansify.rl"
/*! \file ansify.rl
 * \brief Convert TinyMUX percent-color substitutions to ANSI escapes.
 *
 * Replaces the historic FTP contrib ansify.l (flex) with a Ragel -G2 scanner,
 * matching how other TinyMUX offline tools (muxescape, color_ops) are built.
 *
 * Build:
 *   ragel -G2 -C -o ansify.c ansify.rl
 *   cc -O2 -Wall -Wextra -std=c11 -o ansify ansify.c
 *
 * Usage:
 *   ansify [options] [file ...]
 *   ansify < connect.mux > connect.txt
 */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ANSIFY_VERSION "1.1.0"

#define ESC_NORMAL  "\033[0m"
#define ESC_HILITE  "\033[1m"
#define ESC_UNDER   "\033[4m"
#define ESC_BLINK   "\033[5m"
#define ESC_INVERSE "\033[7m"

#define ESC_FG_BLACK   "\033[30m"
#define ESC_FG_RED     "\033[31m"
#define ESC_FG_GREEN   "\033[32m"
#define ESC_FG_YELLOW  "\033[33m"
#define ESC_FG_BLUE    "\033[34m"
#define ESC_FG_MAGENTA "\033[35m"
#define ESC_FG_CYAN    "\033[36m"
#define ESC_FG_WHITE   "\033[37m"

#define ESC_BG_BLACK   "\033[40m"
#define ESC_BG_RED     "\033[41m"
#define ESC_BG_GREEN   "\033[42m"
#define ESC_BG_YELLOW  "\033[43m"
#define ESC_BG_BLUE    "\033[44m"
#define ESC_BG_MAGENTA "\033[45m"
#define ESC_BG_CYAN    "\033[46m"
#define ESC_BG_WHITE   "\033[47m"

/* Cap offline tool input so a huge redirect cannot OOM the host. */
#define ANSIFY_MAX_INPUT (16u * 1024u * 1024u)

static int g_strip;

static void emit_esc(const char *s)
{
    if (!g_strip && s && *s)
        fputs(s, stdout);
}

static void emit_bytes(const unsigned char *ts, const unsigned char *te)
{
    if (te > ts)
        fwrite(ts, 1, (size_t)(te - ts), stdout);
}

static const char *letter_escape(int ch)
{
    switch (ch) {
    case 'n': return ESC_NORMAL;
    case 'h': return ESC_HILITE;
    case 'u': return ESC_UNDER;
    case 'f': return ESC_BLINK;
    case 'i': return ESC_INVERSE;

    /* Foreground: modern x + legacy k for black */
    case 'x':
    case 'k': return ESC_FG_BLACK;
    case 'r': return ESC_FG_RED;
    case 'g': return ESC_FG_GREEN;
    case 'y': return ESC_FG_YELLOW;
    case 'b': return ESC_FG_BLUE;
    case 'm': return ESC_FG_MAGENTA;
    case 'c': return ESC_FG_CYAN;
    case 'w': return ESC_FG_WHITE;

    /* Background: modern X + legacy K for black */
    case 'X':
    case 'K': return ESC_BG_BLACK;
    case 'R': return ESC_BG_RED;
    case 'G': return ESC_BG_GREEN;
    case 'Y': return ESC_BG_YELLOW;
    case 'B': return ESC_BG_BLUE;
    case 'M': return ESC_BG_MAGENTA;
    case 'C': return ESC_BG_CYAN;
    case 'W': return ESC_BG_WHITE;

    default:  return NULL;
    }
}

static int hex_nibble(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int parse_hex_rgb(const char *p, size_t n, int *r, int *g, int *b)
{
    if (n == 3) {
        int r1 = hex_nibble(p[0]);
        int g1 = hex_nibble(p[1]);
        int b1 = hex_nibble(p[2]);
        if (r1 < 0 || g1 < 0 || b1 < 0)
            return -1;
        *r = r1 * 17;
        *g = g1 * 17;
        *b = b1 * 17;
        return 0;
    }
    if (n == 6) {
        int r1 = hex_nibble(p[0]), r2 = hex_nibble(p[1]);
        int g1 = hex_nibble(p[2]), g2 = hex_nibble(p[3]);
        int b1 = hex_nibble(p[4]), b2 = hex_nibble(p[5]);
        if (r1 < 0 || r2 < 0 || g1 < 0 || g2 < 0 || b1 < 0 || b2 < 0)
            return -1;
        *r = r1 * 16 + r2;
        *g = g1 * 16 + g2;
        *b = b1 * 16 + b2;
        return 0;
    }
    return -1;
}

/*
 * Body is the bytes between '<' and '>' of %x<body> / %c<body>.
 * Forms: #RGB, #RRGGBB, bg#…, #…;bg, N, bgN  (N = 0..255).
 * Returns 1 if handled, 0 if the body should be passed through raw.
 */
static int try_emit_extended(const unsigned char *body, const unsigned char *body_end)
{
    char buf[64];
    size_t n = (size_t)(body_end - body);
    char *p;
    int is_bg = 0;
    size_t plen;

    if (n >= sizeof(buf))
        return 0;
    memcpy(buf, body, n);
    buf[n] = '\0';
    p = buf;

    if ((p[0] == 'b' || p[0] == 'B') && (p[1] == 'g' || p[1] == 'G')) {
        is_bg = 1;
        p += 2;
    }

    plen = strlen(p);
    if (plen >= 3
        && (p[plen - 3] == ';' || p[plen - 3] == ',')
        && (p[plen - 2] == 'b' || p[plen - 2] == 'B')
        && (p[plen - 1] == 'g' || p[plen - 1] == 'G')) {
        is_bg = 1;
        p[plen - 3] = '\0';
    }

    if (*p == '#') {
        int r, g, b;
        p++;
        if (parse_hex_rgb(p, strlen(p), &r, &g, &b) == 0) {
            if (!g_strip) {
                if (is_bg)
                    printf("\033[48;2;%d;%d;%dm", r, g, b);
                else
                    printf("\033[38;2;%d;%d;%dm", r, g, b);
            }
            return 1;
        }
        return 0;
    }

    {
        char *end = NULL;
        long idx = strtol(p, &end, 10);
        if (end != p && *end == '\0' && idx >= 0 && idx <= 255) {
            if (!g_strip) {
                if (is_bg)
                    printf("\033[48;5;%ldm", idx);
                else
                    printf("\033[38;5;%ldm", idx);
            }
            return 1;
        }
    }
    return 0;
}


#line 246 "ansify.rl"



#line 207 "ansify.c"
static const int ansify_start = 2;
static const int ansify_first_final = 2;
static const int ansify_error = -1;

static const int ansify_en_main = 2;


#line 249 "ansify.rl"

static void process_buffer(const unsigned char *data, size_t len)
{
    const unsigned char *p = data;
    const unsigned char *pe = data + len;
    const unsigned char *eof = pe;
    const unsigned char *ts = NULL;
    const unsigned char *te = NULL;
    int cs = 0;
    int act = 0;

    (void)eof;
    (void)act;
    (void)ansify_en_main;
    (void)ansify_error;
    (void)ansify_first_final;

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
    
#line 238 "ansify.c"
	{
	cs = ansify_start;
	ts = 0;
	te = 0;
	act = 0;
	}

#line 271 "ansify.rl"
    
#line 248 "ansify.c"
	{
	if ( p == pe )
		goto _test_eof;
	switch ( cs )
	{
tr0:
#line 229 "ansify.rl"
	{{p = ((te))-1;}{
        emit_bytes(ts, te);
    }}
	goto st2;
tr2:
#line 210 "ansify.rl"
	{te = p+1;{
        /* Match is three bytes: % x|c letter */
        const char *esc = letter_escape((int)ts[2]);
        if (esc)
            emit_esc(esc);
        else
            emit_bytes(ts, te);
    }}
	goto st2;
tr3:
#line 219 "ansify.rl"
	{te = p+1;{
        /* % x|c < body >   body is ts+3 .. te-1 */
        if (te - ts >= 4
            && try_emit_extended(ts + 3, te - 1)) {
            /* handled */
        } else {
            emit_bytes(ts, te);
        }
    }}
	goto st2;
tr4:
#line 229 "ansify.rl"
	{te = p+1;{
        emit_bytes(ts, te);
    }}
	goto st2;
tr6:
#line 229 "ansify.rl"
	{te = p;p--;{
        emit_bytes(ts, te);
    }}
	goto st2;
tr7:
#line 206 "ansify.rl"
	{te = p+1;{
        putchar('%');
    }}
	goto st2;
st2:
#line 1 "NONE"
	{ts = 0;}
	if ( ++p == pe )
		goto _test_eof2;
case 2:
#line 1 "NONE"
	{ts = p;}
#line 309 "ansify.c"
	if ( (*p) == 37u )
		goto tr5;
	goto tr4;
tr5:
#line 1 "NONE"
	{te = p+1;}
	goto st3;
st3:
	if ( ++p == pe )
		goto _test_eof3;
case 3:
#line 321 "ansify.c"
	switch( (*p) ) {
		case 37u: goto tr7;
		case 67u: goto st0;
		case 88u: goto st0;
		case 99u: goto st0;
		case 120u: goto st0;
	}
	goto tr6;
st0:
	if ( ++p == pe )
		goto _test_eof0;
case 0:
	switch( (*p) ) {
		case 60u: goto st1;
		case 71u: goto tr2;
		case 75u: goto tr2;
		case 77u: goto tr2;
		case 82u: goto tr2;
		case 107u: goto tr2;
		case 114u: goto tr2;
		case 117u: goto tr2;
	}
	if ( (*p) < 98u ) {
		if ( (*p) > 67u ) {
			if ( 87u <= (*p) && (*p) <= 89u )
				goto tr2;
		} else if ( (*p) >= 66u )
			goto tr2;
	} else if ( (*p) > 99u ) {
		if ( (*p) < 109u ) {
			if ( 102u <= (*p) && (*p) <= 105u )
				goto tr2;
		} else if ( (*p) > 110u ) {
			if ( 119u <= (*p) && (*p) <= 121u )
				goto tr2;
		} else
			goto tr2;
	} else
		goto tr2;
	goto tr0;
st1:
	if ( ++p == pe )
		goto _test_eof1;
case 1:
	if ( (*p) == 62u )
		goto tr3;
	goto st1;
	}
	_test_eof2: cs = 2; goto _test_eof; 
	_test_eof3: cs = 3; goto _test_eof; 
	_test_eof0: cs = 0; goto _test_eof; 
	_test_eof1: cs = 1; goto _test_eof; 

	_test_eof: {}
	if ( p == eof )
	{
	switch ( cs ) {
	case 3: goto tr6;
	case 0: goto tr0;
	case 1: goto tr0;
	}
	}

	}

#line 272 "ansify.rl"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
}

static int read_all(FILE *fp, unsigned char **out, size_t *out_len)
{
    unsigned char *buf = NULL;
    size_t cap = 0;
    size_t len = 0;
    unsigned char chunk[4096];

    for (;;) {
        size_t n = fread(chunk, 1, sizeof(chunk), fp);
        if (n > 0) {
            if (len + n > ANSIFY_MAX_INPUT) {
                fprintf(stderr, "ansify: input exceeds %u byte limit\n",
                        ANSIFY_MAX_INPUT);
                free(buf);
                return -1;
            }
            if (len + n > cap) {
                size_t ncap = cap ? cap * 2 : 8192;
                while (ncap < len + n)
                    ncap *= 2;
                {
                    unsigned char *nb = (unsigned char *)realloc(buf, ncap);
                    if (!nb) {
                        free(buf);
                        fprintf(stderr, "ansify: out of memory\n");
                        return -1;
                    }
                    buf = nb;
                    cap = ncap;
                }
            }
            memcpy(buf + len, chunk, n);
            len += n;
        }
        if (n < sizeof(chunk)) {
            if (ferror(fp)) {
                free(buf);
                fprintf(stderr, "ansify: read error: %s\n", strerror(errno));
                return -1;
            }
            break;
        }
    }

    *out = buf;
    *out_len = len;
    return 0;
}

static int process_file(FILE *fp)
{
    unsigned char *data = NULL;
    size_t len = 0;

    if (read_all(fp, &data, &len) != 0)
        return 1;
    if (len > 0)
        process_buffer(data, len);
    free(data);
    return 0;
}

static void usage(FILE *out)
{
    fputs(
        "Usage: ansify [options] [file ...]\n"
        "\n"
        "Convert TinyMUX percent-color codes (%x / %c) to ANSI escapes.\n"
        "With no files, read stdin. Multiple files are concatenated.\n"
        "\n"
        "Options:\n"
        "  -h, --help      show this help\n"
        "  -v, --version   show version\n"
        "  -s, --strip     remove color codes instead of expanding them\n"
        "\n"
        "Single-letter codes (after %x or %c):\n"
        "  n normal  h hilite  u underline  f blink  i inverse\n"
        "  fg: x|k r g y b m c w     (x and legacy k = black)\n"
        "  bg: X|K R G Y B M C W     (X and legacy K = black)\n"
        "\n"
        "Extended:\n"
        "  %x<#RRGGBB>   %x<#RGB>       truecolor foreground\n"
        "  %x<bg#RRGGBB> %x<#RRGGBB;bg>  truecolor background\n"
        "  %x<N>         %x<bgN>        xterm-256 fg / bg (N = 0..255)\n"
        "  %%                             literal percent\n"
        "\n"
        "Source is ansify.rl (Ragel). Regenerate with:\n"
        "  ragel -G2 -C -o ansify.c ansify.rl\n",
        out);
}

int main(int argc, char **argv)
{
    int i;
    int rc = 0;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] != '-' || strcmp(a, "-") == 0)
            break;
        if (strcmp(a, "--") == 0) {
            i++;
            break;
        }
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            usage(stdout);
            return 0;
        }
        if (strcmp(a, "-v") == 0 || strcmp(a, "--version") == 0) {
            printf("ansify %s\n", ANSIFY_VERSION);
            return 0;
        }
        if (strcmp(a, "-s") == 0 || strcmp(a, "--strip") == 0) {
            g_strip = 1;
            continue;
        }
        fprintf(stderr, "ansify: unknown option '%s'\n", a);
        usage(stderr);
        return 2;
    }

    if (i >= argc)
        return process_file(stdin);

    for (; i < argc; i++) {
        FILE *fp;
        if (strcmp(argv[i], "-") == 0) {
            if (process_file(stdin) != 0)
                rc = 1;
            continue;
        }
        fp = fopen(argv[i], "rb");
        if (!fp) {
            fprintf(stderr, "ansify: cannot open '%s': %s\n", argv[i],
                    strerror(errno));
            rc = 1;
            continue;
        }
        if (process_file(fp) != 0)
            rc = 1;
        fclose(fp);
    }

    return rc;
}
