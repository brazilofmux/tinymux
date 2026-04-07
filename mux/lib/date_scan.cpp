
#line 1 "mux/lib/date_scan.rl"
/*! \file date_scan.rl
 * \brief Unified date parser — Ragel -G2 scanner + recursive descent.
 *
 * Replaces both do_convtime() and ParseDate() with a single code path.
 * Ragel tokenizes the input; a small top-down parser assembles tokens
 * into a FIELDEDTIME plus timezone offset.
 *
 * Build: ragel -G2 -o date_scan.cpp date_scan.rl
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "core.h"
#include "timeutil.h"

#include <cstring>

// -----------------------------------------------------------------------
// Token types
// -----------------------------------------------------------------------

enum DateTokType : unsigned char
{
    DTT_EOF = 0,
    DTT_NUM,            // digit sequence; iVal = integer value, nDigits = count
    DTT_MONTH,          // month name; iVal = 1..12
    DTT_DOW,            // day-of-week name; iVal = 0..6 (Sun=0)
    DTT_TZ_NAME,        // named timezone; iVal = offset in minutes
    DTT_MERIDIAN,       // AM/PM; iVal = 0 or 12
    DTT_SUFFIX,         // ordinal suffix (st/nd/rd/th)
    DTT_T,              // letter T
    DTT_W,              // letter W
    DTT_Z,              // letter Z
    DTT_DASH,           // -
    DTT_PLUS,           // +
    DTT_COLON,          // :
    DTT_DOT,            // .
    DTT_COMMA,          // ,
    DTT_SPACE,          // whitespace (collapsed)
};

struct DateTok
{
    DateTokType type;
    unsigned char nDigits;  // for DTT_NUM: digit count
    int iVal;               // numeric value or encoded meaning
};

// Maximum tokens a date string can produce.
// "Wed, 24 Jun 1992 10:22:54.1234567 -0700" is ~20 tokens.
//
#define DATE_MAX_TOKENS 32

// -----------------------------------------------------------------------
// Token classifiers — called from committed => actions only.
// These examine the matched text (ts..te) to determine the value.
// -----------------------------------------------------------------------

static inline int toupper_ascii(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }

static int classify_month(const UTF8 *ts, const UTF8 *te)
{
    (void)te;
    // First 3 chars determine the month unambiguously.
    int c0 = toupper_ascii(ts[0]);
    int c1 = toupper_ascii(ts[1]);
    int c2 = toupper_ascii(ts[2]);
    switch (c0)
    {
    case 'J':
        if (c1 == 'A') return 1;       // Jan
        if (c2 == 'N') return 6;       // Jun
        return 7;                       // Jul
    case 'F': return 2;                 // Feb
    case 'M':
        return (c2 == 'R') ? 3 : 5;   // Mar / May
    case 'A':
        return (c1 == 'P') ? 4 : 8;   // Apr / Aug
    case 'S': return 9;                 // Sep
    case 'O': return 10;                // Oct
    case 'N': return 11;                // Nov
    case 'D': return 12;                // Dec
    }
    return 0;
}

static int classify_dow(const UTF8 *ts, const UTF8 *te)
{
    (void)te;
    int c0 = toupper_ascii(ts[0]);
    int c1 = toupper_ascii(ts[1]);
    switch (c0)
    {
    case 'S':
        return (c1 == 'U') ? 0 : 6;   // Sun=0, Sat=6
    case 'M': return 1;                 // Mon
    case 'T':
        return (c1 == 'U') ? 2 : 4;   // Tue=2, Thu=4
    case 'W': return 3;                 // Wed
    case 'F': return 5;                 // Fri
    }
    return 0;
}

static int classify_tz(const UTF8 *ts, const UTF8 *te)
{
    int len = (int)(te - ts);
    // Build a simple key from first chars for fast dispatch.
    int c0 = toupper_ascii(ts[0]);
    int c1 = (len > 1) ? toupper_ascii(ts[1]) : 0;
    int c2 = (len > 2) ? toupper_ascii(ts[2]) : 0;

    if (len == 2)
    {
        // UT
        return 0;
    }
    if (len == 3)
    {
        switch (c0)
        {
        case 'U': return 0;            // UTC
        case 'G': return 0;            // GMT
        case 'E':
            return (c2 == 'T') ? -300 : // EST
                   (c2 == 'T') ? -300 : // EET handled below
                   -240;                 // EDT
        case 'C':
            if (c1 == 'S') return -360; // CST
            if (c1 == 'D') return -300; // CDT
            if (c1 == 'E') return 60;   // CET
            break;
        case 'M':
            return (c2 == 'T') ? -420 : -360; // MST / MDT
        case 'P':
            return (c2 == 'T') ? -480 : -420; // PST / PDT
        case 'H': return -600;          // HST
        case 'A':
            if (c1 == 'S') return -240; // AST
            if (c1 == 'D') return -180; // ADT
            break;
        case 'B': return 60;            // BST
        case 'I': return 60;            // IST
        }

        // 3-letter: EST/EDT/EET
        if (c0 == 'E')
        {
            if (c1 == 'S') return -300; // EST
            if (c1 == 'D') return -240; // EDT
            if (c1 == 'E') return 120;  // EET
        }
    }
    if (len == 4)
    {
        // AKST, AKDT, CEST, EEST, AEST
        if (c0 == 'A' && c1 == 'K')
            return (c2 == 'S') ? -540 : -480; // AKST / AKDT
        if (c0 == 'C' && c1 == 'E')
            return 120;                 // CEST
        if (c0 == 'E' && c1 == 'E')
            return 180;                 // EEST
        if (c0 == 'A' && c1 == 'E')
            return 600;                 // AEST
    }
    return 0;
}

// -----------------------------------------------------------------------
// Ragel scanner
// -----------------------------------------------------------------------


#line 297 "mux/lib/date_scan.rl"



#line 177 "mux/lib/date_scan.cpp"
static const int date_scanner_start = 40;
static const int date_scanner_error = 0;

static const int date_scanner_en_main = 40;


#line 300 "mux/lib/date_scan.rl"

// -----------------------------------------------------------------------
// Run the Ragel scanner.  Returns number of tokens, or -1 on error.
// -----------------------------------------------------------------------

static int date_scan(const UTF8 *input, size_t len, DateTok *toks)
{
    const UTF8 *p   = input;
    const UTF8 *pe  = input + len;
    const UTF8 *eof = pe;
    const UTF8 *ts  = nullptr;
    const UTF8 *te  = nullptr;
    int cs  = 0;
    int act = 0;
    int ntok = 0;


    (void)ts; (void)te; (void)act; // suppress unused warnings

    
#line 201 "mux/lib/date_scan.cpp"
	{
	cs = date_scanner_start;
	ts = 0;
	te = 0;
	act = 0;
	}

#line 320 "mux/lib/date_scan.rl"
    
#line 207 "mux/lib/date_scan.cpp"
	{
	if ( p == pe )
		goto _test_eof;
	switch ( cs )
	{
tr0:
#line 1 "NONE"
	{	switch( act ) {
	case 10:
	{{p = ((te))-1;}
            if (ntok < DATE_MAX_TOKENS) {
                int c = toupper_ascii(ts[0]);
                // A=+1, B=+2, ..., I=+9
                int off = (c - 'A' + 1) * 60;
                toks[ntok++] = { DTT_TZ_NAME, 0, off };
            }
        }
	break;
	case 12:
	{{p = ((te))-1;}
            if (ntok < DATE_MAX_TOKENS)
                toks[ntok++] = { DTT_TZ_NAME, 0, 720 };  // M=+12
        }
	break;
	case 13:
	{{p = ((te))-1;}
            if (ntok < DATE_MAX_TOKENS) {
                int c = toupper_ascii(ts[0]);
                // N=-1, O=-2, ..., Y=-12
                int off = -(c - 'N' + 1) * 60;
                toks[ntok++] = { DTT_TZ_NAME, 0, off };
            }
        }
	break;
	}
	}
	goto st40;
tr1:
#line 228 "mux/lib/date_scan.rl"
	{te = p+1;{
            if (ntok < DATE_MAX_TOKENS)
                toks[ntok++] = { DTT_TZ_NAME, 0, classify_tz(ts, te) };
        }}
	goto st40;
tr2:
#line 260 "mux/lib/date_scan.rl"
	{{p = ((te))-1;}{
            if (ntok < DATE_MAX_TOKENS) {
                int c = toupper_ascii(ts[0]);
                // A=+1, B=+2, ..., I=+9
                int off = (c - 'A' + 1) * 60;
                toks[ntok++] = { DTT_TZ_NAME, 0, off };
            }
        }}
	goto st40;
tr5:
#line 218 "mux/lib/date_scan.rl"
	{{p = ((te))-1;}{
            if (ntok < DATE_MAX_TOKENS)
                toks[ntok++] = { DTT_MONTH, 0, classify_month(ts, te) };
        }}
	goto st40;
tr6:
#line 218 "mux/lib/date_scan.rl"
	{te = p+1;{
            if (ntok < DATE_MAX_TOKENS)
                toks[ntok++] = { DTT_MONTH, 0, classify_month(ts, te) };
        }}
	goto st40;
tr18:
#line 223 "mux/lib/date_scan.rl"
	{{p = ((te))-1;}{
            if (ntok < DATE_MAX_TOKENS)
                toks[ntok++] = { DTT_DOW, 0, classify_dow(ts, te) };
        }}
	goto st40;
tr20:
#line 223 "mux/lib/date_scan.rl"
	{te = p+1;{
            if (ntok < DATE_MAX_TOKENS)
                toks[ntok++] = { DTT_DOW, 0, classify_dow(ts, te) };
        }}
	goto st40;
tr27:
#line 276 "mux/lib/date_scan.rl"
	{{p = ((te))-1;}{
            if (ntok < DATE_MAX_TOKENS)
                toks[ntok++] = { DTT_TZ_NAME, 0, 720 };  // M=+12
        }}
	goto st40;
tr29:
#line 280 "mux/lib/date_scan.rl"
	{{p = ((te))-1;}{
            if (ntok < DATE_MAX_TOKENS) {
                int c = toupper_ascii(ts[0]);
                // N=-1, O=-2, ..., Y=-12
                int off = -(c - 'N' + 1) * 60;
                toks[ntok++] = { DTT_TZ_NAME, 0, off };
            }
        }}
	goto st40;
tr36:
#line 254 "mux/lib/date_scan.rl"
	{{p = ((te))-1;}{ if (ntok < DATE_MAX_TOKENS) toks[ntok++] = { DTT_T, 0, 0 }; }}
	goto st40;
tr38:
#line 255 "mux/lib/date_scan.rl"
	{{p = ((te))-1;}{ if (ntok < DATE_MAX_TOKENS) toks[ntok++] = { DTT_W, 0, 0 }; }}
	goto st40;
tr42:
#line 290 "mux/lib/date_scan.rl"
	{te = p+1;{ if (ntok < DATE_MAX_TOKENS) toks[ntok++] = { DTT_PLUS,  0, 0 }; }}
	goto st40;
tr43:
#line 293 "mux/lib/date_scan.rl"
	{te = p+1;{ if (ntok < DATE_MAX_TOKENS) toks[ntok++] = { DTT_COMMA, 0, 0 }; }}
	goto st40;
tr44:
#line 289 "mux/lib/date_scan.rl"
	{te = p+1;{ if (ntok < DATE_MAX_TOKENS) toks[ntok++] = { DTT_DASH,  0, 0 }; }}
	goto st40;
tr45:
#line 292 "mux/lib/date_scan.rl"
	{te = p+1;{ if (ntok < DATE_MAX_TOKENS) toks[ntok++] = { DTT_DOT,   0, 0 }; }}
	goto st40;
tr47:
#line 291 "mux/lib/date_scan.rl"
	{te = p+1;{ if (ntok < DATE_MAX_TOKENS) toks[ntok++] = { DTT_COLON, 0, 0 }; }}
	goto st40;
tr55:
#line 268 "mux/lib/date_scan.rl"
	{te = p+1;{
            if (ntok < DATE_MAX_TOKENS) {
                int c = toupper_ascii(ts[0]);
                // K=+10, L=+11
                int off = (c - 'A') * 60;  // K=10, L=11
                toks[ntok++] = { DTT_TZ_NAME, 0, off };
            }
        }}
	goto st40;
tr60:
#line 280 "mux/lib/date_scan.rl"
	{te = p+1;{
            if (ntok < DATE_MAX_TOKENS) {
                int c = toupper_ascii(ts[0]);
                // N=-1, O=-2, ..., Y=-12
                int off = -(c - 'N' + 1) * 60;
                toks[ntok++] = { DTT_TZ_NAME, 0, off };
            }
        }}
	goto st40;
tr66:
#line 253 "mux/lib/date_scan.rl"
	{te = p+1;{ if (ntok < DATE_MAX_TOKENS) toks[ntok++] = { DTT_Z, 0, 0 }; }}
	goto st40;
tr67:
#line 294 "mux/lib/date_scan.rl"
	{te = p;p--;{ if (ntok < DATE_MAX_TOKENS) toks[ntok++] = { DTT_SPACE, 0, 0 }; }}
	goto st40;
tr68:
#line 244 "mux/lib/date_scan.rl"
	{te = p;p--;{
            int ndig = (int)(te - ts);
            int val = 0;
            for (const UTF8 *d = ts; d < te; d++)
                val = val * 10 + (*d - '0');
            if (ntok < DATE_MAX_TOKENS)
                toks[ntok++] = { DTT_NUM, (unsigned char)ndig, val };
        }}
	goto st40;
tr69:
#line 260 "mux/lib/date_scan.rl"
	{te = p;p--;{
            if (ntok < DATE_MAX_TOKENS) {
                int c = toupper_ascii(ts[0]);
                // A=+1, B=+2, ..., I=+9
                int off = (c - 'A' + 1) * 60;
                toks[ntok++] = { DTT_TZ_NAME, 0, off };
            }
        }}
	goto st40;
tr72:
#line 233 "mux/lib/date_scan.rl"
	{te = p+1;{
            int val = (ts[0] == 'p' || ts[0] == 'P') ? 12 : 0;
            if (ntok < DATE_MAX_TOKENS)
                toks[ntok++] = { DTT_MERIDIAN, 0, val };
        }}
	goto st40;
tr75:
#line 218 "mux/lib/date_scan.rl"
	{te = p;p--;{
            if (ntok < DATE_MAX_TOKENS)
                toks[ntok++] = { DTT_MONTH, 0, classify_month(ts, te) };
        }}
	goto st40;
tr83:
#line 223 "mux/lib/date_scan.rl"
	{te = p;p--;{
            if (ntok < DATE_MAX_TOKENS)
                toks[ntok++] = { DTT_DOW, 0, classify_dow(ts, te) };
        }}
	goto st40;
tr84:
#line 276 "mux/lib/date_scan.rl"
	{te = p;p--;{
            if (ntok < DATE_MAX_TOKENS)
                toks[ntok++] = { DTT_TZ_NAME, 0, 720 };  // M=+12
        }}
	goto st40;
tr88:
#line 280 "mux/lib/date_scan.rl"
	{te = p;p--;{
            if (ntok < DATE_MAX_TOKENS) {
                int c = toupper_ascii(ts[0]);
                // N=-1, O=-2, ..., Y=-12
                int off = -(c - 'N' + 1) * 60;
                toks[ntok++] = { DTT_TZ_NAME, 0, off };
            }
        }}
	goto st40;
tr89:
#line 239 "mux/lib/date_scan.rl"
	{te = p+1;{
            if (ntok < DATE_MAX_TOKENS)
                toks[ntok++] = { DTT_SUFFIX, 0, 0 };
        }}
	goto st40;
tr96:
#line 254 "mux/lib/date_scan.rl"
	{te = p;p--;{ if (ntok < DATE_MAX_TOKENS) toks[ntok++] = { DTT_T, 0, 0 }; }}
	goto st40;
tr99:
#line 239 "mux/lib/date_scan.rl"
	{te = p;p--;{
            if (ntok < DATE_MAX_TOKENS)
                toks[ntok++] = { DTT_SUFFIX, 0, 0 };
        }}
	goto st40;
tr102:
#line 228 "mux/lib/date_scan.rl"
	{te = p;p--;{
            if (ntok < DATE_MAX_TOKENS)
                toks[ntok++] = { DTT_TZ_NAME, 0, classify_tz(ts, te) };
        }}
	goto st40;
tr103:
#line 255 "mux/lib/date_scan.rl"
	{te = p;p--;{ if (ntok < DATE_MAX_TOKENS) toks[ntok++] = { DTT_W, 0, 0 }; }}
	goto st40;
st40:
#line 1 "NONE"
	{ts = 0;}
	if ( ++p == pe )
		goto _test_eof40;
case 40:
#line 1 "NONE"
	{ts = p;}
#line 431 "mux/lib/date_scan.cpp"
	switch( (*p) ) {
		case 32u: goto st41;
		case 43u: goto tr42;
		case 44u: goto tr43;
		case 45u: goto tr44;
		case 46u: goto tr45;
		case 58u: goto tr47;
		case 65u: goto tr48;
		case 67u: goto tr50;
		case 68u: goto tr51;
		case 69u: goto tr50;
		case 70u: goto tr52;
		case 71u: goto tr53;
		case 74u: goto st23;
		case 77u: goto tr56;
		case 78u: goto tr57;
		case 79u: goto tr58;
		case 80u: goto tr59;
		case 82u: goto st63;
		case 83u: goto tr62;
		case 84u: goto tr63;
		case 85u: goto st71;
		case 87u: goto tr65;
		case 90u: goto tr66;
		case 97u: goto tr48;
		case 99u: goto tr50;
		case 100u: goto tr51;
		case 101u: goto tr50;
		case 102u: goto tr52;
		case 103u: goto tr53;
		case 106u: goto st23;
		case 109u: goto tr56;
		case 110u: goto tr57;
		case 111u: goto tr58;
		case 112u: goto tr59;
		case 114u: goto st63;
		case 115u: goto tr62;
		case 116u: goto tr63;
		case 117u: goto st71;
		case 119u: goto tr65;
		case 122u: goto tr66;
	}
	if ( (*p) < 75u ) {
		if ( (*p) < 48u ) {
			if ( 9u <= (*p) && (*p) <= 13u )
				goto st41;
		} else if ( (*p) > 57u ) {
			if ( 66u <= (*p) && (*p) <= 73u )
				goto tr49;
		} else
			goto st42;
	} else if ( (*p) > 76u ) {
		if ( (*p) < 98u ) {
			if ( 81u <= (*p) && (*p) <= 89u )
				goto tr60;
		} else if ( (*p) > 105u ) {
			if ( (*p) > 108u ) {
				if ( 113u <= (*p) && (*p) <= 121u )
					goto tr60;
			} else if ( (*p) >= 107u )
				goto tr55;
		} else
			goto tr49;
	} else
		goto tr55;
	goto st0;
st0:
cs = 0;
	goto _out;
st41:
	if ( ++p == pe )
		goto _test_eof41;
case 41:
	if ( (*p) == 32u )
		goto st41;
	if ( 9u <= (*p) && (*p) <= 13u )
		goto st41;
	goto tr67;
st42:
	if ( ++p == pe )
		goto _test_eof42;
case 42:
	if ( 48u <= (*p) && (*p) <= 57u )
		goto st42;
	goto tr68;
tr48:
#line 1 "NONE"
	{te = p+1;}
#line 260 "mux/lib/date_scan.rl"
	{act = 10;}
	goto st43;
st43:
	if ( ++p == pe )
		goto _test_eof43;
case 43:
#line 524 "mux/lib/date_scan.cpp"
	switch( (*p) ) {
		case 68u: goto st1;
		case 69u: goto st2;
		case 75u: goto st3;
		case 77u: goto tr72;
		case 80u: goto st4;
		case 83u: goto st1;
		case 85u: goto st6;
		case 100u: goto st1;
		case 101u: goto st2;
		case 107u: goto st3;
		case 109u: goto tr72;
		case 112u: goto st4;
		case 115u: goto st1;
		case 117u: goto st6;
	}
	goto tr69;
st1:
	if ( ++p == pe )
		goto _test_eof1;
case 1:
	switch( (*p) ) {
		case 84u: goto tr1;
		case 116u: goto tr1;
	}
	goto tr0;
st2:
	if ( ++p == pe )
		goto _test_eof2;
case 2:
	switch( (*p) ) {
		case 83u: goto st1;
		case 115u: goto st1;
	}
	goto tr2;
st3:
	if ( ++p == pe )
		goto _test_eof3;
case 3:
	switch( (*p) ) {
		case 68u: goto st1;
		case 83u: goto st1;
		case 100u: goto st1;
		case 115u: goto st1;
	}
	goto tr2;
st4:
	if ( ++p == pe )
		goto _test_eof4;
case 4:
	switch( (*p) ) {
		case 82u: goto tr4;
		case 114u: goto tr4;
	}
	goto tr2;
tr4:
#line 1 "NONE"
	{te = p+1;}
	goto st44;
st44:
	if ( ++p == pe )
		goto _test_eof44;
case 44:
#line 586 "mux/lib/date_scan.cpp"
	switch( (*p) ) {
		case 73u: goto st5;
		case 105u: goto st5;
	}
	goto tr75;
st5:
	if ( ++p == pe )
		goto _test_eof5;
case 5:
	switch( (*p) ) {
		case 76u: goto tr6;
		case 108u: goto tr6;
	}
	goto tr5;
st6:
	if ( ++p == pe )
		goto _test_eof6;
case 6:
	switch( (*p) ) {
		case 71u: goto tr7;
		case 103u: goto tr7;
	}
	goto tr2;
tr7:
#line 1 "NONE"
	{te = p+1;}
	goto st45;
st45:
	if ( ++p == pe )
		goto _test_eof45;
case 45:
#line 616 "mux/lib/date_scan.cpp"
	switch( (*p) ) {
		case 85u: goto st7;
		case 117u: goto st7;
	}
	goto tr75;
st7:
	if ( ++p == pe )
		goto _test_eof7;
case 7:
	switch( (*p) ) {
		case 83u: goto st8;
		case 115u: goto st8;
	}
	goto tr5;
st8:
	if ( ++p == pe )
		goto _test_eof8;
case 8:
	switch( (*p) ) {
		case 84u: goto tr6;
		case 116u: goto tr6;
	}
	goto tr5;
tr49:
#line 1 "NONE"
	{te = p+1;}
#line 260 "mux/lib/date_scan.rl"
	{act = 10;}
	goto st46;
st46:
	if ( ++p == pe )
		goto _test_eof46;
case 46:
#line 647 "mux/lib/date_scan.cpp"
	switch( (*p) ) {
		case 83u: goto st1;
		case 115u: goto st1;
	}
	goto tr69;
tr50:
#line 1 "NONE"
	{te = p+1;}
#line 260 "mux/lib/date_scan.rl"
	{act = 10;}
	goto st47;
st47:
	if ( ++p == pe )
		goto _test_eof47;
case 47:
#line 660 "mux/lib/date_scan.cpp"
	switch( (*p) ) {
		case 68u: goto st1;
		case 69u: goto st9;
		case 83u: goto st1;
		case 100u: goto st1;
		case 101u: goto st9;
		case 115u: goto st1;
	}
	goto tr69;
st9:
	if ( ++p == pe )
		goto _test_eof9;
case 9:
	switch( (*p) ) {
		case 83u: goto st1;
		case 84u: goto tr1;
		case 115u: goto st1;
		case 116u: goto tr1;
	}
	goto tr2;
tr51:
#line 1 "NONE"
	{te = p+1;}
	goto st48;
st48:
	if ( ++p == pe )
		goto _test_eof48;
case 48:
#line 687 "mux/lib/date_scan.cpp"
	switch( (*p) ) {
		case 69u: goto st10;
		case 101u: goto st10;
	}
	goto tr69;
st10:
	if ( ++p == pe )
		goto _test_eof10;
case 10:
	switch( (*p) ) {
		case 67u: goto tr9;
		case 99u: goto tr9;
	}
	goto tr2;
tr9:
#line 1 "NONE"
	{te = p+1;}
	goto st49;
st49:
	if ( ++p == pe )
		goto _test_eof49;
case 49:
#line 708 "mux/lib/date_scan.cpp"
	switch( (*p) ) {
		case 69u: goto st11;
		case 101u: goto st11;
	}
	goto tr75;
st11:
	if ( ++p == pe )
		goto _test_eof11;
case 11:
	switch( (*p) ) {
		case 77u: goto st12;
		case 109u: goto st12;
	}
	goto tr5;
st12:
	if ( ++p == pe )
		goto _test_eof12;
case 12:
	switch( (*p) ) {
		case 66u: goto st13;
		case 98u: goto st13;
	}
	goto tr5;
st13:
	if ( ++p == pe )
		goto _test_eof13;
case 13:
	switch( (*p) ) {
		case 69u: goto st14;
		case 101u: goto st14;
	}
	goto tr5;
st14:
	if ( ++p == pe )
		goto _test_eof14;
case 14:
	switch( (*p) ) {
		case 82u: goto tr6;
		case 114u: goto tr6;
	}
	goto tr5;
tr52:
#line 1 "NONE"
	{te = p+1;}
	goto st50;
st50:
	if ( ++p == pe )
		goto _test_eof50;
case 50:
#line 756 "mux/lib/date_scan.cpp"
	switch( (*p) ) {
		case 69u: goto st15;
		case 82u: goto st20;
		case 101u: goto st15;
		case 114u: goto st20;
	}
	goto tr69;
st15:
	if ( ++p == pe )
		goto _test_eof15;
case 15:
	switch( (*p) ) {
		case 66u: goto tr13;
		case 98u: goto tr13;
	}
	goto tr2;
tr13:
#line 1 "NONE"
	{te = p+1;}
	goto st51;
st51:
	if ( ++p == pe )
		goto _test_eof51;
case 51:
#line 779 "mux/lib/date_scan.cpp"
	switch( (*p) ) {
		case 82u: goto st16;
		case 114u: goto st16;
	}
	goto tr75;
st16:
	if ( ++p == pe )
		goto _test_eof16;
case 16:
	switch( (*p) ) {
		case 85u: goto st17;
		case 117u: goto st17;
	}
	goto tr5;
st17:
	if ( ++p == pe )
		goto _test_eof17;
case 17:
	switch( (*p) ) {
		case 65u: goto st18;
		case 97u: goto st18;
	}
	goto tr5;
st18:
	if ( ++p == pe )
		goto _test_eof18;
case 18:
	switch( (*p) ) {
		case 82u: goto st19;
		case 114u: goto st19;
	}
	goto tr5;
st19:
	if ( ++p == pe )
		goto _test_eof19;
case 19:
	switch( (*p) ) {
		case 89u: goto tr6;
		case 121u: goto tr6;
	}
	goto tr5;
st20:
	if ( ++p == pe )
		goto _test_eof20;
case 20:
	switch( (*p) ) {
		case 73u: goto tr17;
		case 105u: goto tr17;
	}
	goto tr2;
tr17:
#line 1 "NONE"
	{te = p+1;}
	goto st52;
st52:
	if ( ++p == pe )
		goto _test_eof52;
case 52:
#line 836 "mux/lib/date_scan.cpp"
	switch( (*p) ) {
		case 68u: goto st21;
		case 100u: goto st21;
	}
	goto tr83;
st21:
	if ( ++p == pe )
		goto _test_eof21;
case 21:
	switch( (*p) ) {
		case 65u: goto st22;
		case 97u: goto st22;
	}
	goto tr18;
st22:
	if ( ++p == pe )
		goto _test_eof22;
case 22:
	switch( (*p) ) {
		case 89u: goto tr20;
		case 121u: goto tr20;
	}
	goto tr18;
tr53:
#line 1 "NONE"
	{te = p+1;}
#line 260 "mux/lib/date_scan.rl"
	{act = 10;}
	goto st53;
st53:
	if ( ++p == pe )
		goto _test_eof53;
case 53:
#line 867 "mux/lib/date_scan.cpp"
	switch( (*p) ) {
		case 77u: goto st1;
		case 109u: goto st1;
	}
	goto tr69;
st23:
	if ( ++p == pe )
		goto _test_eof23;
case 23:
	switch( (*p) ) {
		case 65u: goto st24;
		case 85u: goto st25;
		case 97u: goto st24;
		case 117u: goto st25;
	}
	goto st0;
st24:
	if ( ++p == pe )
		goto _test_eof24;
case 24:
	switch( (*p) ) {
		case 78u: goto tr24;
		case 110u: goto tr24;
	}
	goto st0;
tr24:
#line 1 "NONE"
	{te = p+1;}
	goto st54;
st54:
	if ( ++p == pe )
		goto _test_eof54;
case 54:
#line 899 "mux/lib/date_scan.cpp"
	switch( (*p) ) {
		case 85u: goto st17;
		case 117u: goto st17;
	}
	goto tr75;
st25:
	if ( ++p == pe )
		goto _test_eof25;
case 25:
	switch( (*p) ) {
		case 76u: goto st55;
		case 78u: goto st56;
		case 108u: goto st55;
		case 110u: goto st56;
	}
	goto st0;
st55:
	if ( ++p == pe )
		goto _test_eof55;
case 55:
	switch( (*p) ) {
		case 89u: goto tr6;
		case 121u: goto tr6;
	}
	goto tr75;
st56:
	if ( ++p == pe )
		goto _test_eof56;
case 56:
	switch( (*p) ) {
		case 69u: goto tr6;
		case 101u: goto tr6;
	}
	goto tr75;
tr56:
#line 1 "NONE"
	{te = p+1;}
#line 276 "mux/lib/date_scan.rl"
	{act = 12;}
	goto st57;
st57:
	if ( ++p == pe )
		goto _test_eof57;
case 57:
#line 941 "mux/lib/date_scan.cpp"
	switch( (*p) ) {
		case 65u: goto st26;
		case 68u: goto st1;
		case 79u: goto st28;
		case 83u: goto st1;
		case 97u: goto st26;
		case 100u: goto st1;
		case 111u: goto st28;
		case 115u: goto st1;
	}
	goto tr84;
st26:
	if ( ++p == pe )
		goto _test_eof26;
case 26:
	switch( (*p) ) {
		case 82u: goto tr28;
		case 89u: goto tr6;
		case 114u: goto tr28;
		case 121u: goto tr6;
	}
	goto tr27;
tr28:
#line 1 "NONE"
	{te = p+1;}
	goto st58;
st58:
	if ( ++p == pe )
		goto _test_eof58;
case 58:
#line 970 "mux/lib/date_scan.cpp"
	switch( (*p) ) {
		case 67u: goto st27;
		case 99u: goto st27;
	}
	goto tr75;
st27:
	if ( ++p == pe )
		goto _test_eof27;
case 27:
	switch( (*p) ) {
		case 72u: goto tr6;
		case 104u: goto tr6;
	}
	goto tr5;
st28:
	if ( ++p == pe )
		goto _test_eof28;
case 28:
	switch( (*p) ) {
		case 78u: goto tr17;
		case 110u: goto tr17;
	}
	goto tr0;
tr57:
#line 1 "NONE"
	{te = p+1;}
	goto st59;
st59:
	if ( ++p == pe )
		goto _test_eof59;
case 59:
#line 1000 "mux/lib/date_scan.cpp"
	switch( (*p) ) {
		case 68u: goto tr89;
		case 79u: goto st29;
		case 100u: goto tr89;
		case 111u: goto st29;
	}
	goto tr88;
st29:
	if ( ++p == pe )
		goto _test_eof29;
case 29:
	switch( (*p) ) {
		case 86u: goto tr9;
		case 118u: goto tr9;
	}
	goto tr29;
tr58:
#line 1 "NONE"
	{te = p+1;}
	goto st60;
st60:
	if ( ++p == pe )
		goto _test_eof60;
case 60:
#line 1023 "mux/lib/date_scan.cpp"
	switch( (*p) ) {
		case 67u: goto st30;
		case 99u: goto st30;
	}
	goto tr88;
st30:
	if ( ++p == pe )
		goto _test_eof30;
case 30:
	switch( (*p) ) {
		case 84u: goto tr30;
		case 116u: goto tr30;
	}
	goto tr29;
tr30:
#line 1 "NONE"
	{te = p+1;}
	goto st61;
st61:
	if ( ++p == pe )
		goto _test_eof61;
case 61:
#line 1044 "mux/lib/date_scan.cpp"
	switch( (*p) ) {
		case 79u: goto st12;
		case 111u: goto st12;
	}
	goto tr75;
tr59:
#line 1 "NONE"
	{te = p+1;}
#line 280 "mux/lib/date_scan.rl"
	{act = 13;}
	goto st62;
st62:
	if ( ++p == pe )
		goto _test_eof62;
case 62:
#line 1057 "mux/lib/date_scan.cpp"
	switch( (*p) ) {
		case 68u: goto st1;
		case 77u: goto tr72;
		case 83u: goto st1;
		case 100u: goto st1;
		case 109u: goto tr72;
		case 115u: goto st1;
	}
	goto tr88;
st63:
	if ( ++p == pe )
		goto _test_eof63;
case 63:
	switch( (*p) ) {
		case 68u: goto tr89;
		case 100u: goto tr89;
	}
	goto tr88;
tr62:
#line 1 "NONE"
	{te = p+1;}
#line 280 "mux/lib/date_scan.rl"
	{act = 13;}
	goto st64;
st64:
	if ( ++p == pe )
		goto _test_eof64;
case 64:
#line 1083 "mux/lib/date_scan.cpp"
	switch( (*p) ) {
		case 65u: goto st31;
		case 69u: goto st34;
		case 84u: goto tr89;
		case 85u: goto st28;
		case 97u: goto st31;
		case 101u: goto st34;
		case 116u: goto tr89;
		case 117u: goto st28;
	}
	goto tr88;
st31:
	if ( ++p == pe )
		goto _test_eof31;
case 31:
	switch( (*p) ) {
		case 84u: goto tr31;
		case 116u: goto tr31;
	}
	goto tr29;
tr31:
#line 1 "NONE"
	{te = p+1;}
	goto st65;
st65:
	if ( ++p == pe )
		goto _test_eof65;
case 65:
#line 1110 "mux/lib/date_scan.cpp"
	switch( (*p) ) {
		case 85u: goto st32;
		case 117u: goto st32;
	}
	goto tr83;
st32:
	if ( ++p == pe )
		goto _test_eof32;
case 32:
	switch( (*p) ) {
		case 82u: goto st33;
		case 114u: goto st33;
	}
	goto tr18;
st33:
	if ( ++p == pe )
		goto _test_eof33;
case 33:
	switch( (*p) ) {
		case 68u: goto st21;
		case 100u: goto st21;
	}
	goto tr18;
st34:
	if ( ++p == pe )
		goto _test_eof34;
case 34:
	switch( (*p) ) {
		case 80u: goto tr34;
		case 112u: goto tr34;
	}
	goto tr29;
tr34:
#line 1 "NONE"
	{te = p+1;}
	goto st66;
st66:
	if ( ++p == pe )
		goto _test_eof66;
case 66:
#line 1149 "mux/lib/date_scan.cpp"
	switch( (*p) ) {
		case 84u: goto st35;
		case 116u: goto st35;
	}
	goto tr75;
st35:
	if ( ++p == pe )
		goto _test_eof35;
case 35:
	switch( (*p) ) {
		case 69u: goto st11;
		case 101u: goto st11;
	}
	goto tr5;
tr63:
#line 1 "NONE"
	{te = p+1;}
	goto st67;
st67:
	if ( ++p == pe )
		goto _test_eof67;
case 67:
#line 1170 "mux/lib/date_scan.cpp"
	switch( (*p) ) {
		case 72u: goto st68;
		case 85u: goto st37;
		case 104u: goto st68;
		case 117u: goto st37;
	}
	goto tr96;
st68:
	if ( ++p == pe )
		goto _test_eof68;
case 68:
	switch( (*p) ) {
		case 85u: goto tr100;
		case 117u: goto tr100;
	}
	goto tr99;
tr100:
#line 1 "NONE"
	{te = p+1;}
	goto st69;
st69:
	if ( ++p == pe )
		goto _test_eof69;
case 69:
#line 1193 "mux/lib/date_scan.cpp"
	switch( (*p) ) {
		case 82u: goto st36;
		case 114u: goto st36;
	}
	goto tr83;
st36:
	if ( ++p == pe )
		goto _test_eof36;
case 36:
	switch( (*p) ) {
		case 83u: goto st33;
		case 115u: goto st33;
	}
	goto tr18;
st37:
	if ( ++p == pe )
		goto _test_eof37;
case 37:
	switch( (*p) ) {
		case 69u: goto tr37;
		case 101u: goto tr37;
	}
	goto tr36;
tr37:
#line 1 "NONE"
	{te = p+1;}
	goto st70;
st70:
	if ( ++p == pe )
		goto _test_eof70;
case 70:
#line 1223 "mux/lib/date_scan.cpp"
	switch( (*p) ) {
		case 83u: goto st33;
		case 115u: goto st33;
	}
	goto tr83;
st71:
	if ( ++p == pe )
		goto _test_eof71;
case 71:
	switch( (*p) ) {
		case 84u: goto st72;
		case 116u: goto st72;
	}
	goto tr88;
st72:
	if ( ++p == pe )
		goto _test_eof72;
case 72:
	switch( (*p) ) {
		case 67u: goto tr1;
		case 99u: goto tr1;
	}
	goto tr102;
tr65:
#line 1 "NONE"
	{te = p+1;}
	goto st73;
st73:
	if ( ++p == pe )
		goto _test_eof73;
case 73:
#line 1253 "mux/lib/date_scan.cpp"
	switch( (*p) ) {
		case 69u: goto st38;
		case 101u: goto st38;
	}
	goto tr103;
st38:
	if ( ++p == pe )
		goto _test_eof38;
case 38:
	switch( (*p) ) {
		case 68u: goto tr39;
		case 100u: goto tr39;
	}
	goto tr38;
tr39:
#line 1 "NONE"
	{te = p+1;}
	goto st74;
st74:
	if ( ++p == pe )
		goto _test_eof74;
case 74:
#line 1274 "mux/lib/date_scan.cpp"
	switch( (*p) ) {
		case 78u: goto st39;
		case 110u: goto st39;
	}
	goto tr83;
st39:
	if ( ++p == pe )
		goto _test_eof39;
case 39:
	switch( (*p) ) {
		case 69u: goto st36;
		case 101u: goto st36;
	}
	goto tr18;
	}
	_test_eof40: cs = 40; goto _test_eof; 
	_test_eof41: cs = 41; goto _test_eof; 
	_test_eof42: cs = 42; goto _test_eof; 
	_test_eof43: cs = 43; goto _test_eof; 
	_test_eof1: cs = 1; goto _test_eof; 
	_test_eof2: cs = 2; goto _test_eof; 
	_test_eof3: cs = 3; goto _test_eof; 
	_test_eof4: cs = 4; goto _test_eof; 
	_test_eof44: cs = 44; goto _test_eof; 
	_test_eof5: cs = 5; goto _test_eof; 
	_test_eof6: cs = 6; goto _test_eof; 
	_test_eof45: cs = 45; goto _test_eof; 
	_test_eof7: cs = 7; goto _test_eof; 
	_test_eof8: cs = 8; goto _test_eof; 
	_test_eof46: cs = 46; goto _test_eof; 
	_test_eof47: cs = 47; goto _test_eof; 
	_test_eof9: cs = 9; goto _test_eof; 
	_test_eof48: cs = 48; goto _test_eof; 
	_test_eof10: cs = 10; goto _test_eof; 
	_test_eof49: cs = 49; goto _test_eof; 
	_test_eof11: cs = 11; goto _test_eof; 
	_test_eof12: cs = 12; goto _test_eof; 
	_test_eof13: cs = 13; goto _test_eof; 
	_test_eof14: cs = 14; goto _test_eof; 
	_test_eof50: cs = 50; goto _test_eof; 
	_test_eof15: cs = 15; goto _test_eof; 
	_test_eof51: cs = 51; goto _test_eof; 
	_test_eof16: cs = 16; goto _test_eof; 
	_test_eof17: cs = 17; goto _test_eof; 
	_test_eof18: cs = 18; goto _test_eof; 
	_test_eof19: cs = 19; goto _test_eof; 
	_test_eof20: cs = 20; goto _test_eof; 
	_test_eof52: cs = 52; goto _test_eof; 
	_test_eof21: cs = 21; goto _test_eof; 
	_test_eof22: cs = 22; goto _test_eof; 
	_test_eof53: cs = 53; goto _test_eof; 
	_test_eof23: cs = 23; goto _test_eof; 
	_test_eof24: cs = 24; goto _test_eof; 
	_test_eof54: cs = 54; goto _test_eof; 
	_test_eof25: cs = 25; goto _test_eof; 
	_test_eof55: cs = 55; goto _test_eof; 
	_test_eof56: cs = 56; goto _test_eof; 
	_test_eof57: cs = 57; goto _test_eof; 
	_test_eof26: cs = 26; goto _test_eof; 
	_test_eof58: cs = 58; goto _test_eof; 
	_test_eof27: cs = 27; goto _test_eof; 
	_test_eof28: cs = 28; goto _test_eof; 
	_test_eof59: cs = 59; goto _test_eof; 
	_test_eof29: cs = 29; goto _test_eof; 
	_test_eof60: cs = 60; goto _test_eof; 
	_test_eof30: cs = 30; goto _test_eof; 
	_test_eof61: cs = 61; goto _test_eof; 
	_test_eof62: cs = 62; goto _test_eof; 
	_test_eof63: cs = 63; goto _test_eof; 
	_test_eof64: cs = 64; goto _test_eof; 
	_test_eof31: cs = 31; goto _test_eof; 
	_test_eof65: cs = 65; goto _test_eof; 
	_test_eof32: cs = 32; goto _test_eof; 
	_test_eof33: cs = 33; goto _test_eof; 
	_test_eof34: cs = 34; goto _test_eof; 
	_test_eof66: cs = 66; goto _test_eof; 
	_test_eof35: cs = 35; goto _test_eof; 
	_test_eof67: cs = 67; goto _test_eof; 
	_test_eof68: cs = 68; goto _test_eof; 
	_test_eof69: cs = 69; goto _test_eof; 
	_test_eof36: cs = 36; goto _test_eof; 
	_test_eof37: cs = 37; goto _test_eof; 
	_test_eof70: cs = 70; goto _test_eof; 
	_test_eof71: cs = 71; goto _test_eof; 
	_test_eof72: cs = 72; goto _test_eof; 
	_test_eof73: cs = 73; goto _test_eof; 
	_test_eof38: cs = 38; goto _test_eof; 
	_test_eof74: cs = 74; goto _test_eof; 
	_test_eof39: cs = 39; goto _test_eof; 

	_test_eof: {}
	if ( p == eof )
	{
	switch ( cs ) {
	case 41: goto tr67;
	case 42: goto tr68;
	case 43: goto tr69;
	case 1: goto tr0;
	case 2: goto tr2;
	case 3: goto tr2;
	case 4: goto tr2;
	case 44: goto tr75;
	case 5: goto tr5;
	case 6: goto tr2;
	case 45: goto tr75;
	case 7: goto tr5;
	case 8: goto tr5;
	case 46: goto tr69;
	case 47: goto tr69;
	case 9: goto tr2;
	case 48: goto tr69;
	case 10: goto tr2;
	case 49: goto tr75;
	case 11: goto tr5;
	case 12: goto tr5;
	case 13: goto tr5;
	case 14: goto tr5;
	case 50: goto tr69;
	case 15: goto tr2;
	case 51: goto tr75;
	case 16: goto tr5;
	case 17: goto tr5;
	case 18: goto tr5;
	case 19: goto tr5;
	case 20: goto tr2;
	case 52: goto tr83;
	case 21: goto tr18;
	case 22: goto tr18;
	case 53: goto tr69;
	case 54: goto tr75;
	case 55: goto tr75;
	case 56: goto tr75;
	case 57: goto tr84;
	case 26: goto tr27;
	case 58: goto tr75;
	case 27: goto tr5;
	case 28: goto tr0;
	case 59: goto tr88;
	case 29: goto tr29;
	case 60: goto tr88;
	case 30: goto tr29;
	case 61: goto tr75;
	case 62: goto tr88;
	case 63: goto tr88;
	case 64: goto tr88;
	case 31: goto tr29;
	case 65: goto tr83;
	case 32: goto tr18;
	case 33: goto tr18;
	case 34: goto tr29;
	case 66: goto tr75;
	case 35: goto tr5;
	case 67: goto tr96;
	case 68: goto tr99;
	case 69: goto tr83;
	case 36: goto tr18;
	case 37: goto tr36;
	case 70: goto tr83;
	case 71: goto tr88;
	case 72: goto tr102;
	case 73: goto tr103;
	case 38: goto tr38;
	case 74: goto tr83;
	case 39: goto tr18;
	}
	}

	_out: {}
	}

#line 321 "mux/lib/date_scan.rl"

    if (cs == date_scanner_error)
    {
        return -1;
    }

    // Append EOF sentinel.
    //
    if (ntok < DATE_MAX_TOKENS)
    {
        toks[ntok++] = { DTT_EOF, 0, 0 };
    }
    return ntok;
}

// -----------------------------------------------------------------------
// Recursive descent parser
// -----------------------------------------------------------------------

// Parsed date result.
//
struct DateResult
{
    int iYear;
    int iMonth;         // 1-12
    int iDayOfMonth;    // 1-31
    int iDayOfYear;     // 1-366 (0 = not specified)
    int iWeekOfYear;    // 1-53  (0 = not specified)
    int iDayOfWeek;     // 0-6   (-1 = not specified)
    int iHour;
    int iMinute;
    int iSecond;
    int iFracHectoNano; // 0..9999999 (100ns units)
    int iTzMinutes;     // timezone offset in minutes (INT_MIN = not specified)
    bool bHasTime;
};

#define TZ_NOT_SPECIFIED INT_MIN

static void date_result_init(DateResult *r)
{
    memset(r, 0, sizeof(*r));
    r->iDayOfWeek = -1;
    r->iTzMinutes = TZ_NOT_SPECIFIED;
}

// Token stream cursor.
//
struct TokCursor
{
    const DateTok *toks;
    int pos;
    int ntok;
};

static inline const DateTok *peek(const TokCursor *c)
{
    return (c->pos < c->ntok) ? &c->toks[c->pos] : nullptr;
}

static inline const DateTok *peek2(const TokCursor *c)
{
    return (c->pos + 1 < c->ntok) ? &c->toks[c->pos + 1] : nullptr;
}

static inline const DateTok *consume(TokCursor *c, DateTokType expected)
{
    const DateTok *t = peek(c);
    if (t && t->type == expected)
    {
        c->pos++;
        return t;
    }
    return nullptr;
}

static inline void skip_spaces(TokCursor *c)
{
    while (peek(c) && peek(c)->type == DTT_SPACE)
    {
        c->pos++;
    }
}

static inline bool at_end(const TokCursor *c)
{
    const DateTok *t = peek(c);
    return !t || t->type == DTT_EOF;
}

// Parse fractional seconds: DOT + NUM → hectonanoseconds.
//
static bool parse_frac(TokCursor *c, DateResult *r)
{
    if (!consume(c, DTT_DOT))
    {
        return false;
    }
    const DateTok *t = consume(c, DTT_NUM);
    if (!t)
    {
        return false;
    }

    // Right-pad to 7 digits (100ns resolution).
    //
    int val = t->iVal;
    int ndig = t->nDigits;
    if (ndig > 7)
    {
        // Truncate excess digits.
        while (ndig > 7)
        {
            val /= 10;
            ndig--;
        }
    }
    else
    {
        while (ndig < 7)
        {
            val *= 10;
            ndig++;
        }
    }
    r->iFracHectoNano = val;
    return true;
}

// Parse timezone: Z | named | +/-HHMM | +/-HH:MM
//
static bool parse_timezone(TokCursor *c, DateResult *r)
{
    skip_spaces(c);
    const DateTok *t = peek(c);
    if (!t || t->type == DTT_EOF)
    {
        return true;  // no timezone is OK
    }

    if (t->type == DTT_Z)
    {
        c->pos++;
        r->iTzMinutes = 0;
        return true;
    }

    if (t->type == DTT_TZ_NAME)
    {
        c->pos++;
        r->iTzMinutes = t->iVal;
        return true;
    }

    // Military timezone letters T and W — these are also DTT_T/DTT_W
    // but in timezone position (after time), they're timezone indicators.
    //
    if (t->type == DTT_T)
    {
        c->pos++;
        r->iTzMinutes = -420;  // T = UTC-7
        return true;
    }

    if (t->type == DTT_W)
    {
        c->pos++;
        r->iTzMinutes = -600;  // W = UTC-10
        return true;
    }

    if (t->type == DTT_PLUS || t->type == DTT_DASH)
    {
        int sign = (t->type == DTT_PLUS) ? 1 : -1;
        c->pos++;

        const DateTok *n = consume(c, DTT_NUM);
        if (!n)
        {
            return false;
        }

        int hh, mm;
        if (n->nDigits == 4)
        {
            // +HHMM
            hh = n->iVal / 100;
            mm = n->iVal % 100;
        }
        else if (n->nDigits == 2)
        {
            // +HH:MM or +HH
            hh = n->iVal;
            mm = 0;
            if (consume(c, DTT_COLON))
            {
                const DateTok *m = consume(c, DTT_NUM);
                if (!m || m->nDigits != 2)
                {
                    return false;
                }
                mm = m->iVal;
            }
        }
        else
        {
            return false;
        }

        if (hh > 23 || mm > 59)
        {
            return false;
        }
        r->iTzMinutes = sign * (hh * 60 + mm);
        return true;
    }

    // Unknown token — might be trailing garbage.
    return true;
}

// Parse time part: [T|space] HH:MM[:SS[.frac]] [meridian] [TZ]
//
static bool parse_time(TokCursor *c, DateResult *r)
{
    // Optional T or space separator.
    if (!consume(c, DTT_T))
    {
        skip_spaces(c);
    }

    const DateTok *t = peek(c);
    if (!t || t->type != DTT_NUM)
    {
        // No time component.  That's OK for date-only.
        r->bHasTime = false;
        return parse_timezone(c, r);
    }

    // Hour — or compact HHMM / HHMMSS
    const DateTok *hh = consume(c, DTT_NUM);
    if (!hh)
    {
        return false;
    }

    if (hh->nDigits == 6)
    {
        // Compact HHMMSS
        r->iHour   = hh->iVal / 10000;
        r->iMinute = (hh->iVal / 100) % 100;
        r->iSecond = hh->iVal % 100;
    }
    else if (hh->nDigits == 4)
    {
        // Compact HHMM
        r->iHour   = hh->iVal / 100;
        r->iMinute = hh->iVal % 100;
        r->iSecond = 0;
    }
    else if (hh->nDigits <= 2)
    {
        // Extended: HH:MM[:SS]
        r->iHour = hh->iVal;

        if (consume(c, DTT_COLON))
        {
            const DateTok *mm = consume(c, DTT_NUM);
            if (!mm || mm->nDigits != 2)
            {
                return false;
            }
            r->iMinute = mm->iVal;

            if (consume(c, DTT_COLON))
            {
                const DateTok *ss = consume(c, DTT_NUM);
                if (!ss || ss->nDigits != 2)
                {
                    return false;
                }
                r->iSecond = ss->iVal;

                if (peek(c) && peek(c)->type == DTT_DOT)
                {
                    parse_frac(c, r);
                }
            }
        }
    }
    else
    {
        return false;
    }

    // Optional .frac (for compact forms)
    if (peek(c) && peek(c)->type == DTT_DOT)
    {
        parse_frac(c, r);
    }

    r->bHasTime = true;

    // Validate basic time ranges.
    if (r->iHour > 23 || r->iMinute > 59 || r->iSecond > 59)
    {
        return false;
    }

    // Optional meridian.
    const DateTok *mer = peek(c);
    if (mer && mer->type == DTT_MERIDIAN)
    {
        c->pos++;
        if (r->iHour == 0 || r->iHour > 12)
        {
            return false;
        }
        if (r->iHour == 12)
        {
            r->iHour = 0;
        }
        r->iHour += mer->iVal;
    }

    return parse_timezone(c, r);
}

// Parse compact time: HHMMSS[.frac][TZ] (no colons, for ISO basic).
//
static bool parse_compact_time(TokCursor *c, DateResult *r)
{
    if (!consume(c, DTT_T))
    {
        skip_spaces(c);
    }

    const DateTok *t = peek(c);
    if (!t || t->type != DTT_NUM)
    {
        r->bHasTime = false;
        return parse_timezone(c, r);
    }

    const DateTok *hms = consume(c, DTT_NUM);
    if (!hms)
    {
        return false;
    }

    if (hms->nDigits == 6)
    {
        r->iHour   = hms->iVal / 10000;
        r->iMinute = (hms->iVal / 100) % 100;
        r->iSecond = hms->iVal % 100;
    }
    else if (hms->nDigits == 4)
    {
        r->iHour   = hms->iVal / 100;
        r->iMinute = hms->iVal % 100;
        r->iSecond = 0;
    }
    else
    {
        return false;
    }

    r->bHasTime = true;

    if (r->iHour > 23 || r->iMinute > 59 || r->iSecond > 59)
    {
        return false;
    }

    // Optional .frac
    if (peek(c) && peek(c)->type == DTT_DOT)
    {
        parse_frac(c, r);
    }

    return parse_timezone(c, r);
}

// -----------------------------------------------------------------------
// Format dispatchers
// -----------------------------------------------------------------------

// ISO 8601 family: leading 4+ digit year (or signed year).
//
static bool parse_iso(TokCursor *c, DateResult *r)
{
    // Year (already validated as 4+ digits by caller).
    const DateTok *yr = consume(c, DTT_NUM);
    if (!yr)
    {
        return false;
    }

    // Handle compact forms where digits run together:
    // 8 digits = YYYYMMDD, 7 digits = YYYYDDD
    //
    if (yr->nDigits == 8)
    {
        r->iYear       = yr->iVal / 10000;
        r->iMonth      = (yr->iVal / 100) % 100;
        r->iDayOfMonth = yr->iVal % 100;
        if (r->iMonth < 1 || r->iMonth > 12 || r->iDayOfMonth < 1 || r->iDayOfMonth > 31)
        {
            return false;
        }
        return parse_time(c, r);
    }

    if (yr->nDigits == 7)
    {
        r->iYear      = yr->iVal / 1000;
        r->iDayOfYear = yr->iVal % 1000;
        if (r->iDayOfYear < 1 || r->iDayOfYear > 366)
        {
            return false;
        }
        return parse_time(c, r);
    }

    r->iYear = yr->iVal;

    const DateTok *next = peek(c);
    if (!next)
    {
        return false;
    }

    if (next->type == DTT_W)
    {
        // ISO week date: YYYYWww-D or YYYYWwwD
        c->pos++;
        const DateTok *wk = consume(c, DTT_NUM);
        if (!wk || wk->iVal < 1 || wk->iVal > 53)
        {
            return false;
        }

        if (wk->nDigits == 2)
        {
            r->iWeekOfYear = wk->iVal;

            if (consume(c, DTT_DASH))
            {
                const DateTok *dw = consume(c, DTT_NUM);
                if (!dw || dw->nDigits != 1 || dw->iVal < 1 || dw->iVal > 7)
                {
                    return false;
                }
                r->iDayOfWeek = dw->iVal % 7;
            }
            else
            {
                // Maybe compact: WwwD as 3 digits
                r->iDayOfWeek = 1;  // default Monday
            }
        }
        else if (wk->nDigits == 3)
        {
            // Compact WwwD: e.g., W151 → week 15, day 1
            r->iWeekOfYear = wk->iVal / 10;
            int dow = wk->iVal % 10;
            if (r->iWeekOfYear < 1 || r->iWeekOfYear > 53 || dow < 1 || dow > 7)
            {
                return false;
            }
            r->iDayOfWeek = dow % 7;
        }
        else
        {
            return false;
        }

        return parse_time(c, r);
    }

    if (next->type == DTT_DASH)
    {
        // Extended ISO: YYYY-MM-DD or YYYY-DDD
        c->pos++;
        const DateTok *n2 = consume(c, DTT_NUM);
        if (!n2)
        {
            return false;
        }

        if (n2->nDigits == 3)
        {
            // Ordinal date: YYYY-DDD
            if (n2->iVal < 1 || n2->iVal > 366)
            {
                return false;
            }
            r->iDayOfYear = n2->iVal;
            return parse_time(c, r);
        }

        if (n2->nDigits > 2)
        {
            return false;
        }

        // YYYY-MM-DD
        r->iMonth = n2->iVal;
        if (r->iMonth < 1 || r->iMonth > 12)
        {
            return false;
        }

        if (!consume(c, DTT_DASH))
        {
            // YYYY-MM only (no day) — date only, default day 1.
            r->iDayOfMonth = 1;
            return parse_time(c, r);
        }

        const DateTok *dd = consume(c, DTT_NUM);
        if (!dd || dd->nDigits > 2 || dd->iVal < 1 || dd->iVal > 31)
        {
            return false;
        }
        r->iDayOfMonth = dd->iVal;
        return parse_time(c, r);
    }

    if (next->type == DTT_NUM && yr->nDigits == 4)
    {
        // Basic ISO: YYYYMMDD or YYYYDDD
        const DateTok *rest = consume(c, DTT_NUM);
        if (!rest)
        {
            return false;
        }

        if (rest->nDigits == 4)
        {
            // YYYYMMDD — rest = MMDD
            r->iMonth      = rest->iVal / 100;
            r->iDayOfMonth = rest->iVal % 100;
            if (r->iMonth < 1 || r->iMonth > 12 || r->iDayOfMonth < 1 || r->iDayOfMonth > 31)
            {
                return false;
            }
            return parse_time(c, r);
        }

        if (rest->nDigits == 3)
        {
            // YYYYDDD
            if (rest->iVal < 1 || rest->iVal > 366)
            {
                return false;
            }
            r->iDayOfYear = rest->iVal;
            return parse_time(c, r);
        }

        return false;
    }

    // YYYY followed by T or space — date-only with just year?
    // Or YYYY alone.  Both are under-specified.
    return false;
}

// Month-name leading: Mmm DD[suffix][,] [YYYY] HH:MM:SS YYYY | Mmm DD HH:MM:SS YYYY
//
static bool parse_month_leading(TokCursor *c, DateResult *r)
{
    const DateTok *mon = consume(c, DTT_MONTH);
    if (!mon)
    {
        return false;
    }
    r->iMonth = mon->iVal;

    skip_spaces(c);

    const DateTok *day = consume(c, DTT_NUM);
    if (!day || day->iVal < 1 || day->iVal > 31)
    {
        return false;
    }
    r->iDayOfMonth = day->iVal;

    // Optional ordinal suffix and comma.
    if (peek(c) && peek(c)->type == DTT_SUFFIX)
    {
        c->pos++;
    }
    if (peek(c) && peek(c)->type == DTT_COMMA)
    {
        c->pos++;
    }

    skip_spaces(c);

    // Now: either YYYY then time, or time then YYYY (legacy order).
    const DateTok *n = peek(c);
    const DateTok *n2 = peek2(c);

    if (n && n->type == DTT_NUM && n2 && n2->type == DTT_COLON)
    {
        // Time first (legacy: Mmm DD HH:MM:SS YYYY)
        if (!parse_time(c, r))
        {
            return false;
        }
        skip_spaces(c);
        const DateTok *yr = consume(c, DTT_NUM);
        if (!yr)
        {
            return false;
        }
        r->iYear = yr->iVal;
        // Timezone may have been consumed by parse_time, or follows the year.
        if (r->iTzMinutes == TZ_NOT_SPECIFIED)
        {
            return parse_timezone(c, r);
        }
        return true;
    }

    // Year first: Mmm DD YYYY time
    const DateTok *yr = consume(c, DTT_NUM);
    if (!yr)
    {
        return false;
    }
    r->iYear = yr->iVal;

    return parse_time(c, r);
}

// Day-leading (European): DD[suffix] Mmm YYYY time
//
static bool parse_day_leading(TokCursor *c, DateResult *r)
{
    const DateTok *day = consume(c, DTT_NUM);
    if (!day || day->iVal < 1 || day->iVal > 31)
    {
        return false;
    }
    r->iDayOfMonth = day->iVal;

    // Optional ordinal suffix.
    if (peek(c) && peek(c)->type == DTT_SUFFIX)
    {
        c->pos++;
    }

    skip_spaces(c);

    const DateTok *mon = consume(c, DTT_MONTH);
    if (!mon)
    {
        return false;
    }
    r->iMonth = mon->iVal;

    // Optional comma.
    if (peek(c) && peek(c)->type == DTT_COMMA)
    {
        c->pos++;
    }

    skip_spaces(c);

    const DateTok *yr = consume(c, DTT_NUM);
    if (!yr)
    {
        return false;
    }
    r->iYear = yr->iVal;

    return parse_time(c, r);
}

// -----------------------------------------------------------------------
// Top-level parse dispatch
// -----------------------------------------------------------------------

static bool date_parse_tokens(const DateTok *toks, int ntok, DateResult *r)
{
    TokCursor cur = { toks, 0, ntok };
    TokCursor *c = &cur;

    date_result_init(r);
    skip_spaces(c);

    // Optional day-of-week prefix.
    if (peek(c) && peek(c)->type == DTT_DOW)
    {
        c->pos++;
        if (peek(c) && peek(c)->type == DTT_COMMA)
        {
            c->pos++;
        }
        skip_spaces(c);
    }

    const DateTok *first = peek(c);
    if (!first || first->type == DTT_EOF)
    {
        return false;
    }

    bool ok = false;

    if (first->type == DTT_NUM && first->nDigits >= 4)
    {
        // ISO family (4+ digit year leading).
        ok = parse_iso(c, r);
    }
    else if (first->type == DTT_DASH || first->type == DTT_PLUS)
    {
        // Signed year: -YYYY... or +YYYYY...
        int sign = (first->type == DTT_DASH) ? -1 : 1;
        c->pos++;
        ok = parse_iso(c, r);
        if (ok)
        {
            r->iYear *= sign;
        }
    }
    else if (first->type == DTT_MONTH)
    {
        // Month-name leading (US order / legacy).
        ok = parse_month_leading(c, r);
    }
    else if (first->type == DTT_NUM)
    {
        const DateTok *second = peek2(c);
        if (second && (second->type == DTT_MONTH ||
                       (second->type == DTT_SPACE)))
        {
            // Could be DD Mmm or DD SPACE MONTH.
            // Save position and try day-leading.
            int save = c->pos;

            // If there's a space, skip past the number and space to check
            // if a month follows.
            if (second->type == DTT_SPACE && c->pos + 2 < c->ntok &&
                c->toks[c->pos + 2].type == DTT_MONTH)
            {
                ok = parse_day_leading(c, r);
            }
            else if (second->type == DTT_MONTH)
            {
                ok = parse_day_leading(c, r);
            }

            if (!ok)
            {
                c->pos = save;
            }
        }
    }

    if (!ok)
    {
        return false;
    }

    // Must have consumed everything (except trailing whitespace and EOF).
    skip_spaces(c);
    if (!at_end(c))
    {
        return false;
    }

    return true;
}

// -----------------------------------------------------------------------
// Public API: drop-in replacement for ParseDate()
// -----------------------------------------------------------------------

// Forward declaration for week-date conversion helper.
static bool ConvertWeekDateToLinearTime(CLinearTimeAbsolute &lta, int iYear,
                                        int iWeek, int iDayOfWeek);

bool ParseDate
(
    CLinearTimeAbsolute &lt,
    UTF8 *pDateString,
    bool *pbZoneSpecified
)
{
    if (!pDateString || !*pDateString)
    {
        return false;
    }

    size_t len = strlen(reinterpret_cast<const char *>(pDateString));

    DateTok toks[DATE_MAX_TOKENS];
    int ntok = date_scan(pDateString, len, toks);
    if (ntok < 0)
    {
        return false;
    }

    DateResult dr;
    if (!date_parse_tokens(toks, ntok, &dr))
    {
        return false;
    }

    // Convert to FIELDEDTIME and then to linear time.
    //
    FIELDEDTIME ft;
    memset(&ft, 0, sizeof(ft));

    ft.iYear = static_cast<short>(dr.iYear);

    if (dr.iWeekOfYear > 0)
    {
        // ISO week date — needs special conversion.
        if (!ConvertWeekDateToLinearTime(lt, dr.iYear, dr.iWeekOfYear, dr.iDayOfWeek))
        {
            return false;
        }

        // Apply time-of-day if present.
        if (dr.bHasTime)
        {
            FIELDEDTIME ftTime;
            lt.ReturnFields(&ftTime);
            ftTime.iHour   = static_cast<unsigned short>(dr.iHour);
            ftTime.iMinute = static_cast<unsigned short>(dr.iMinute);
            ftTime.iSecond = static_cast<unsigned short>(dr.iSecond);

            // Sub-seconds.
            int frac = dr.iFracHectoNano;
            ftTime.iMillisecond = static_cast<unsigned short>(frac / 10000);
            ftTime.iMicrosecond = static_cast<unsigned short>((frac / 10) % 1000);
            ftTime.iNanosecond  = static_cast<unsigned short>((frac % 10) * 100);

            if (!lt.SetFields(&ftTime))
            {
                return false;
            }
        }
    }
    else if (dr.iDayOfYear > 0)
    {
        // Ordinal date: set Jan 1, then add (DayOfYear - 1) days.
        ft.iMonth = 1;
        ft.iDayOfMonth = 1;
        ft.iHour   = static_cast<unsigned short>(dr.iHour);
        ft.iMinute = static_cast<unsigned short>(dr.iMinute);
        ft.iSecond = static_cast<unsigned short>(dr.iSecond);

        int frac = dr.iFracHectoNano;
        ft.iMillisecond = static_cast<unsigned short>(frac / 10000);
        ft.iMicrosecond = static_cast<unsigned short>((frac / 10) % 1000);
        ft.iNanosecond  = static_cast<unsigned short>((frac % 10) * 100);

        if (!lt.SetFields(&ft))
        {
            return false;
        }

        if (dr.iDayOfYear > 1)
        {
            CLinearTimeDelta ltd;
            ltd.Set100ns(FACTOR_100NS_PER_DAY);
            lt += ltd * (dr.iDayOfYear - 1);
        }
    }
    else
    {
        // Standard date.
        ft.iMonth      = static_cast<unsigned short>(dr.iMonth);
        ft.iDayOfMonth = static_cast<unsigned short>(dr.iDayOfMonth);
        ft.iHour       = static_cast<unsigned short>(dr.iHour);
        ft.iMinute     = static_cast<unsigned short>(dr.iMinute);
        ft.iSecond     = static_cast<unsigned short>(dr.iSecond);

        int frac = dr.iFracHectoNano;
        ft.iMillisecond = static_cast<unsigned short>(frac / 10000);
        ft.iMicrosecond = static_cast<unsigned short>((frac / 10) % 1000);
        ft.iNanosecond  = static_cast<unsigned short>((frac % 10) * 100);

        int64_t i64;
        if (!FieldedTimeToLinearTime(&ft, &i64))
        {
            return false;
        }
        lt.Set100ns(i64);
    }

    // Apply timezone offset.
    if (dr.iTzMinutes != TZ_NOT_SPECIFIED)
    {
        CLinearTimeDelta ltd;
        ltd.SetSeconds(60 * dr.iTzMinutes);
        lt -= ltd;
        *pbZoneSpecified = true;
    }
    else
    {
        *pbZoneSpecified = false;
    }

    return true;
}

// -----------------------------------------------------------------------
// Week-date conversion helper
// -----------------------------------------------------------------------

static bool ConvertWeekDateToLinearTime(CLinearTimeAbsolute &lta, int iYear,
                                        int iWeek, int iDayOfWeek)
{
    // Find the Monday of ISO week 1 of iYear.
    // ISO week 1 contains January 4th.
    //
    FIELDEDTIME ftJan4;
    memset(&ftJan4, 0, sizeof(ftJan4));
    ftJan4.iYear = static_cast<short>(iYear);
    ftJan4.iMonth = 1;
    ftJan4.iDayOfMonth = 4;

    if (!lta.SetFields(&ftJan4))
    {
        return false;
    }

    // Find what day of week Jan 4 falls on.
    lta.ReturnFields(&ftJan4);
    // iDayOfWeek: 0=Sun, 1=Mon, ..., 6=Sat
    // ISO day-of-week: 1=Mon, ..., 7=Sun
    // Convert to ISO: Mon=1..Sun=7
    int jan4_iso_dow = (ftJan4.iDayOfWeek == 0) ? 7 : ftJan4.iDayOfWeek;

    // Monday of week 1 = Jan 4 minus (jan4_iso_dow - 1) days.
    CLinearTimeDelta oneDay;
    oneDay.Set100ns(FACTOR_100NS_PER_DAY);
    lta -= oneDay * (jan4_iso_dow - 1);

    // Now advance to the target week and day.
    // iDayOfWeek is 0=Sun..6=Sat from our parser, but for ISO week dates
    // the input was 1=Mon..7=Sun.  The parser stored it as iDayOfWeek % 7
    // (so 1=Mon..6=Sat, 0=Sun).  Convert back to ISO for offset calculation.
    int iso_dow = (iDayOfWeek == 0) ? 7 : iDayOfWeek;
    int totalDays = (iWeek - 1) * 7 + (iso_dow - 1);

    lta += oneDay * totalDays;

    return true;
}
