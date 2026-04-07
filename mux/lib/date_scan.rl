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

%%{
    machine date_scanner;

    alphtype unsigned char;

    # Patterns (no embedded actions — all token emission is in => blocks).
    #
    month_name = /jan/i ( /uary/i )?
               | /feb/i ( /ruary/i )?
               | /mar/i ( /ch/i )?
               | /apr/i ( /il/i )?
               | /may/i
               | /jun/i ( /e/i )?
               | /jul/i ( /y/i )?
               | /aug/i ( /ust/i )?
               | /sep/i ( /tember/i )?
               | /oct/i ( /ober/i )?
               | /nov/i ( /ember/i )?
               | /dec/i ( /ember/i )? ;

    dow_name = /sun/i ( /day/i )?
             | /mon/i ( /day/i )?
             | /tue/i ( /sday/i )?
             | /wed/i ( /nesday/i )?
             | /thu/i ( /rsday/i )?
             | /fri/i ( /day/i )?
             | /sat/i ( /urday/i )? ;

    tz_name = /utc/i | /gmt/i | /ut/i
            | /est/i | /edt/i | /cst/i | /cdt/i
            | /mst/i | /mdt/i | /pst/i | /pdt/i
            | /akst/i | /akdt/i | /hst/i
            | /ast/i | /adt/i
            | /bst/i | /ist/i | /cet/i | /cest/i
            | /eet/i | /eest/i | /aest/i ;

    meridian = /am/i | /pm/i ;
    suffix   = /st/i | /nd/i | /rd/i | /th/i ;

    # The scanner: longest-match.  All token emission happens in committed
    # => action blocks — no speculative > or % actions.
    #
    main := |*

        month_name => {
            if (ntok < DATE_MAX_TOKENS)
                toks[ntok++] = { DTT_MONTH, 0, classify_month(ts, te) };
        };

        dow_name => {
            if (ntok < DATE_MAX_TOKENS)
                toks[ntok++] = { DTT_DOW, 0, classify_dow(ts, te) };
        };

        tz_name => {
            if (ntok < DATE_MAX_TOKENS)
                toks[ntok++] = { DTT_TZ_NAME, 0, classify_tz(ts, te) };
        };

        meridian => {
            int val = (ts[0] == 'p' || ts[0] == 'P') ? 12 : 0;
            if (ntok < DATE_MAX_TOKENS)
                toks[ntok++] = { DTT_MERIDIAN, 0, val };
        };

        suffix => {
            if (ntok < DATE_MAX_TOKENS)
                toks[ntok++] = { DTT_SUFFIX, 0, 0 };
        };

        digit+ => {
            int ndig = (int)(te - ts);
            int val = 0;
            for (const UTF8 *d = ts; d < te; d++)
                val = val * 10 + (*d - '0');
            if (ntok < DATE_MAX_TOKENS)
                toks[ntok++] = { DTT_NUM, (unsigned char)ndig, val };
        };

        /[Zz]/ => { if (ntok < DATE_MAX_TOKENS) toks[ntok++] = { DTT_Z, 0, 0 }; };
        /[Tt]/ => { if (ntok < DATE_MAX_TOKENS) toks[ntok++] = { DTT_T, 0, 0 }; };
        /[Ww]/ => { if (ntok < DATE_MAX_TOKENS) toks[ntok++] = { DTT_W, 0, 0 }; };

        # Military timezone single letters (A-I, K-Z except those above).
        # Longest match ensures multi-char names win over these.
        #
        /[AaBbCcDdEeFfGgHhIi]/ => {
            if (ntok < DATE_MAX_TOKENS) {
                int c = toupper_ascii(ts[0]);
                // A=+1, B=+2, ..., I=+9
                int off = (c - 'A' + 1) * 60;
                toks[ntok++] = { DTT_TZ_NAME, 0, off };
            }
        };
        /[KkLl]/ => {
            if (ntok < DATE_MAX_TOKENS) {
                int c = toupper_ascii(ts[0]);
                // K=+10, L=+11
                int off = (c - 'A') * 60;  // K=10, L=11
                toks[ntok++] = { DTT_TZ_NAME, 0, off };
            }
        };
        /[Mm]/ => {
            if (ntok < DATE_MAX_TOKENS)
                toks[ntok++] = { DTT_TZ_NAME, 0, 720 };  // M=+12
        };
        /[NnOoPpQqRrSsUuVvXxYy]/ => {
            if (ntok < DATE_MAX_TOKENS) {
                int c = toupper_ascii(ts[0]);
                // N=-1, O=-2, ..., Y=-12
                int off = -(c - 'N' + 1) * 60;
                toks[ntok++] = { DTT_TZ_NAME, 0, off };
            }
        };

        '-'    => { if (ntok < DATE_MAX_TOKENS) toks[ntok++] = { DTT_DASH,  0, 0 }; };
        '+'    => { if (ntok < DATE_MAX_TOKENS) toks[ntok++] = { DTT_PLUS,  0, 0 }; };
        ':'    => { if (ntok < DATE_MAX_TOKENS) toks[ntok++] = { DTT_COLON, 0, 0 }; };
        '.'    => { if (ntok < DATE_MAX_TOKENS) toks[ntok++] = { DTT_DOT,   0, 0 }; };
        ','    => { if (ntok < DATE_MAX_TOKENS) toks[ntok++] = { DTT_COMMA, 0, 0 }; };
        space+ => { if (ntok < DATE_MAX_TOKENS) toks[ntok++] = { DTT_SPACE, 0, 0 }; };

    *|;
}%%

%% write data nofinal;

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

    %% write init;
    %% write exec;

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
