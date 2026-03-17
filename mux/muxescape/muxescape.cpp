
#line 1 "muxescape.rl"
/*! \file muxescape.rl
 * \brief Escape plain text for use in TinyMUX/MUSH command arguments.
 *
 * Reads UTF-8 or ASCII text from stdin or a single file and writes a form
 * that survives MUX softcode parsing while preserving spacing:
 *
 * - spaces -> %b or [space(n)]
 * - tabs -> %t or [repeat(%t,n)]
 * - newlines -> %r or [repeat(%r,n)]
 * - structural characters like []{}(),;#%\\ are backslash-escaped
 *
 * This is intended for preparing message bodies for commands such as @mail,
 * page, +job, or softcode package interfaces where the text will be parsed.
 *
 * Build:
 *   ragel -G2 -o muxescape.cpp muxescape.rl
 *   c++ -std=c++17 -O2 -Wall -Wextra -o muxescape muxescape.cpp
 *
 * Usage:
 *   muxescape [file]
 *   cat message.txt | muxescape
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <string>
#include <vector>

static void emit_spaces_run(const unsigned char *ts, const unsigned char *te)
{
    size_t count = static_cast<size_t>(te - ts);
    if (count <= 3) {
        for (size_t i = 0; i < count; ++i) {
            std::fputs("%b", stdout);
        }
    } else {
        std::fprintf(stdout, "[space(%zu)]", count);
    }
}

static void emit_repeat_run(const char *token, size_t count)
{
    if (count <= 3) {
        for (size_t i = 0; i < count; ++i) {
            std::fputs(token, stdout);
        }
    } else {
        std::fprintf(stdout, "[repeat(%s,%zu)]", token, count);
    }
}

static size_t logical_newlines(const unsigned char *ts, const unsigned char *te)
{
    size_t count = 0;
    const unsigned char *p = ts;
    const unsigned char *end = te;
    while (p < end) {
        if (*p == '\r') {
            ++count;
            ++p;
            if (p < end && *p == '\n') {
                ++p;
            }
        } else if (*p == '\n') {
            ++count;
            ++p;
        } else {
            ++p;
        }
    }
    return count;
}

static void emit_escaped_segment(const unsigned char *ts, const unsigned char *te)
{
    for (const unsigned char *p = ts; p < te; ++p) {
        std::fputc('\\', stdout);
        std::fputc(*p, stdout);
    }
}

static void emit_literal_segment(const unsigned char *ts, const unsigned char *te)
{
    if (te > ts) {
        std::fwrite(ts, 1, static_cast<size_t>(te - ts), stdout);
    }
}

static bool read_all(FILE *fp, std::vector<char> &buf)
{
    char chunk[4096];
    while (true) {
        size_t n = std::fread(chunk, 1, sizeof(chunk), fp);
        if (n > 0) {
            buf.insert(buf.end(), chunk, chunk + n);
        }
        if (n < sizeof(chunk)) {
            if (std::ferror(fp)) {
                return false;
            }
            break;
        }
    }
    return true;
}


#line 144 "muxescape.rl"



#line 112 "muxescape.cpp"
static const int muxescape_start = 0;
static const int muxescape_first_final = 0;
static const int muxescape_error = -1;

static const int muxescape_en_main = 0;


#line 147 "muxescape.rl"

static void process_buffer(const unsigned char *data, size_t len)
{
    const unsigned char *p = data;
    const unsigned char *pe = data + len;
    const unsigned char *eof = pe;
    const unsigned char *ts = nullptr;
    const unsigned char *te = nullptr;
    int cs = 0;
    int act = 0;

    (void)eof;
    (void)act;

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
    
#line 136 "muxescape.cpp"
	{
	cs = muxescape_start;
	ts = 0;
	te = 0;
	act = 0;
	}

#line 166 "muxescape.rl"
    
#line 142 "muxescape.cpp"
	{
	if ( p == pe )
		goto _test_eof;
	switch ( cs )
	{
tr5:
#line 133 "muxescape.rl"
	{te = p;p--;{
        emit_literal_segment(ts, te);
    }}
	goto st0;
tr6:
#line 121 "muxescape.rl"
	{te = p;p--;{
        emit_repeat_run("%t", static_cast<size_t>(te - ts));
    }}
	goto st0;
tr7:
#line 125 "muxescape.rl"
	{te = p;p--;{
        emit_repeat_run("%r", logical_newlines(ts, te));
    }}
	goto st0;
tr8:
#line 117 "muxescape.rl"
	{te = p;p--;{
        emit_spaces_run(ts, te);
    }}
	goto st0;
tr9:
#line 129 "muxescape.rl"
	{te = p;p--;{
        emit_escaped_segment(ts, te);
    }}
	goto st0;
st0:
#line 1 "NONE"
	{ts = 0;}
	if ( ++p == pe )
		goto _test_eof0;
case 0:
#line 1 "NONE"
	{ts = p;}
#line 178 "muxescape.cpp"
	switch( (*p) ) {
		case 9u: goto st2;
		case 10u: goto st3;
		case 13u: goto st3;
		case 32u: goto st4;
		case 35u: goto st5;
		case 37u: goto st5;
		case 44u: goto st5;
		case 59u: goto st5;
		case 123u: goto st5;
		case 125u: goto st5;
	}
	if ( (*p) > 41u ) {
		if ( 91u <= (*p) && (*p) <= 93u )
			goto st5;
	} else if ( (*p) >= 40u )
		goto st5;
	goto st1;
st1:
	if ( ++p == pe )
		goto _test_eof1;
case 1:
	switch( (*p) ) {
		case 13u: goto tr5;
		case 32u: goto tr5;
		case 35u: goto tr5;
		case 37u: goto tr5;
		case 44u: goto tr5;
		case 59u: goto tr5;
		case 123u: goto tr5;
		case 125u: goto tr5;
	}
	if ( (*p) < 40u ) {
		if ( 9u <= (*p) && (*p) <= 10u )
			goto tr5;
	} else if ( (*p) > 41u ) {
		if ( 91u <= (*p) && (*p) <= 93u )
			goto tr5;
	} else
		goto tr5;
	goto st1;
st2:
	if ( ++p == pe )
		goto _test_eof2;
case 2:
	if ( (*p) == 9u )
		goto st2;
	goto tr6;
st3:
	if ( ++p == pe )
		goto _test_eof3;
case 3:
	switch( (*p) ) {
		case 10u: goto st3;
		case 13u: goto st3;
	}
	goto tr7;
st4:
	if ( ++p == pe )
		goto _test_eof4;
case 4:
	if ( (*p) == 32u )
		goto st4;
	goto tr8;
st5:
	if ( ++p == pe )
		goto _test_eof5;
case 5:
	switch( (*p) ) {
		case 35u: goto st5;
		case 37u: goto st5;
		case 44u: goto st5;
		case 59u: goto st5;
		case 123u: goto st5;
		case 125u: goto st5;
	}
	if ( (*p) > 41u ) {
		if ( 91u <= (*p) && (*p) <= 93u )
			goto st5;
	} else if ( (*p) >= 40u )
		goto st5;
	goto tr9;
	}
	_test_eof0: cs = 0; goto _test_eof; 
	_test_eof1: cs = 1; goto _test_eof; 
	_test_eof2: cs = 2; goto _test_eof; 
	_test_eof3: cs = 3; goto _test_eof; 
	_test_eof4: cs = 4; goto _test_eof; 
	_test_eof5: cs = 5; goto _test_eof; 

	_test_eof: {}
	if ( p == eof )
	{
	switch ( cs ) {
	case 1: goto tr5;
	case 2: goto tr6;
	case 3: goto tr7;
	case 4: goto tr8;
	case 5: goto tr9;
	}
	}

	}

#line 167 "muxescape.rl"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
}

int main(int argc, char *argv[])
{
    const char *path = nullptr;
    if (argc == 2) {
        path = argv[1];
        if (0 == std::strcmp(path, "--help") || 0 == std::strcmp(path, "-h")) {
            std::fprintf(stderr, "Usage: muxescape [file]\n");
            return 0;
        }
    } else if (argc > 2) {
        std::fprintf(stderr, "Usage: muxescape [file]\n");
        return 1;
    }

    FILE *fp = stdin;
    if (path) {
        fp = std::fopen(path, "rb");
        if (!fp) {
            std::perror(path);
            return 1;
        }
    }

    std::vector<char> data;
    if (!read_all(fp, data)) {
        if (path) {
            std::fclose(fp);
        }
        std::fprintf(stderr, "muxescape: failed to read input\n");
        return 1;
    }
    if (path) {
        std::fclose(fp);
    }

    if (!data.empty()) {
        process_buffer(reinterpret_cast<const unsigned char *>(data.data()), data.size());
    }

    return std::ferror(stdout) ? 1 : 0;
}
