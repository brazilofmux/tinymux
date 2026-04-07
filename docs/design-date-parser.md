# Design: Unified Date Parser

## Status

Implemented (2026-04-06).  Replaced both `do_convtime()` and
`ParseDate()` with a unified Ragel -G2 scanner + recursive descent
parser in `mux/lib/date_scan.rl`.

## Background

`convtime()` previously had two independent code paths:

1. **`do_convtime()`** — Hand-written parser for the legacy format
   `[Ddd] Mmm DD HH:MM:SS[.frac] YYYY`.  Works correctly.

2. **`ParseDate()`** — 1650-line multi-pass constraint-satisfaction
   engine.  Was completely broken from 2.13 through 2.14 due to a
   `mux_min(int,int)` truncating `size_t`.  Nobody noticed for months.

Having two parsers means two sub-second algorithms, two sets of
validation rules, two things to maintain, and the ability for one to
be broken without anyone knowing.  The fix is to make everything
one thing.

## Design Principle

One parser, always used, always tested.  If it breaks, every
`convsecs()`/`convtime()` round-trip fails and 900+ smoke tests
scream.  No silent fallback to a different code path.

## Architecture

```
convtime(input, [zone], [precision])
  → date_parse(input) → FIELDEDTIME + zone info
  → apply timezone logic
  → return epoch string
```

`date_parse()` is the unified entry point.  It replaces both
`lta.SetString()` (which called `do_convtime()`) and `ParseDate()`.

### Ragel -G2 Scanner

The scanner tokenizes the input into a flat array of typed tokens:

```c
enum DateToken {
    TOK_EOF,
    TOK_NUM,        // sequence of digits, value + digit count stored
    TOK_MONTH,      // Jan..Dec / January..December → 1..12
    TOK_DOW,        // Sun..Sat / Sunday..Saturday → 0..6
    TOK_TZ_NAME,    // UTC, GMT, EST, PST, CST, MST, EDT, PDT, CDT, MDT,
                    // HST, AKST, AKDT, BST, IST, CET, CEST, EET, EEST,
                    // AEST, AST, ADT, UT → offset in minutes
    TOK_TZ_MIL,    // A..Z (except J) → offset in minutes
    TOK_MERIDIAN,   // AM=0, PM=12
    TOK_SUFFIX,     // st, nd, rd, th
    TOK_T,          // letter T (ISO separator)
    TOK_W,          // letter W (ISO week prefix)
    TOK_Z,          // letter Z (UTC indicator)
    TOK_DASH,       // -
    TOK_PLUS,       // +
    TOK_COLON,      // :
    TOK_DOT,        // .
    TOK_COMMA,      // ,
    TOK_SPACE,      // whitespace (collapsed)
};
```

Ragel handles the name matching via DFA alternation.  Month names
(24 alternations), day-of-week (14), timezone abbreviations (~25),
and meridian (2) compile into a single goto-driven state machine.
This is a jump table — faster than the old `ParseThreeLetters()`
hash comparison or the `PD_TextTable` linear scan.

Numbers carry their digit count, which the parser uses for
disambiguation (1-2 digits = day/hour/minute/second, 3 digits =
day-of-year, 4 digits = year, 5+ digits = extended year).

### Recursive Descent Parser

After scanning, the parser dispatches on the leading token(s):

```
parse_datetime(tokens):
    skip optional TOK_DOW [TOK_COMMA] [TOK_SPACE]

    if peek TOK_NUM(4+ digits) or TOK_PLUS or TOK_DASH(followed by digits(4+))
        → parse_iso(tokens)

    if peek TOK_MONTH
        → parse_month_leading(tokens)

    if peek TOK_NUM(1-2 digits) and peek+1 is TOK_MONTH
        → parse_day_leading(tokens)

    → fail
```

Each handler is a straight left-to-right walk consuming expected
tokens.  No backtracking.

#### parse_iso — ISO 8601 family

```
year = consume NUM(4+ digits)

if peek TOK_W                          → ISO week date
    consume W, NUM(2) week
    optional DASH, NUM(1) day-of-week
elif peek DASH + NUM(3)                → ISO ordinal
    consume DASH, NUM(3) day-of-year
elif peek DASH + NUM(1-2)              → ISO extended
    consume DASH, NUM(1-2) month, DASH, NUM(1-2) day
elif peek NUM(4) (MMDD compact)        → ISO basic
    split NUM into month(2) + day(2)
elif peek NUM(3) (DDD compact)         → ISO basic ordinal
    day-of-year = NUM

parse_time_part(tokens)                 (see below)
```

#### parse_month_leading — Mmm DD[,] YYYY time [TZ]

```
month = consume TOK_MONTH
day   = consume NUM(1-2)
optional TOK_SUFFIX, TOK_COMMA
optional TOK_SPACE
year  = consume NUM (any digit count for year)
parse_time_part(tokens)
```

This covers:
- `Jan 01 00:00:00 2000` (after time, year — legacy order variant)
- `January 1st, 2026 10:43:00 UTC`
- `Apr 6 2026 10:43:00 EST`

Note: The legacy `do_convtime()` format puts time before year:
`Mmm DD HH:MM:SS YYYY`.  The unified parser needs to handle both
orders.  After consuming month and day, if the next token looks like
a time (NUM:NUM), parse time then year.  If it looks like a year
(4-digit NUM), parse year then time.

```
if peek NUM + TOK_COLON               → time first, then year
    parse_time_part(tokens)
    year = consume NUM
else
    year = consume NUM
    parse_time_part(tokens)
```

#### parse_day_leading — DD Mmm YYYY time [TZ]

```
day   = consume NUM(1-2)
optional TOK_SUFFIX
month = consume TOK_MONTH
optional TOK_COMMA
year  = consume NUM
parse_time_part(tokens)
```

#### parse_time_part — HH:MM:SS[.frac] [TZ]

```
optional TOK_T or TOK_SPACE (separator)
hour   = consume NUM(1-2)
consume TOK_COLON
minute = consume NUM(2)
optional:
    consume TOK_COLON
    second = consume NUM(2)
    optional:
        consume TOK_DOT
        frac = consume NUM (1-9 digits, right-pad to 7 with zeros)
optional TOK_MERIDIAN (adjust hour: 12→0, add PM offset)
parse_timezone(tokens)
```

#### parse_timezone

```
if peek TOK_Z                          → UTC (offset = 0)
if peek TOK_TZ_NAME                    → named offset
if peek TOK_TZ_MIL                     → military offset
if peek TOK_PLUS or TOK_DASH           → numeric offset
    sign = consume
    offset = consume NUM(4) as HHMM, or NUM(2) HH + COLON + NUM(2) MM
else                                   → no timezone specified
```

## Sub-second Handling

Unified across all formats.  After the seconds, if a `.` follows:

1. Consume up to 9 digits.
2. Right-pad with zeros to exactly 7 digits.
3. Store as hectonanoseconds (100ns units).
4. Split into milliseconds (digits 1-3), microseconds (4-6),
   nanoseconds (7, ×100) for FIELDEDTIME.

This replaces both `ParseDecimalSeconds()` in `do_convtime()` and
the broken inline parser in the old `PD_GetFields()`.

## Supported Formats

### ISO 8601

```
2026-04-06T10:43:00Z
2026-04-06T10:43:00+0000
2026-04-06T10:43:00-07:00
2026-04-06T10:43:00.1234567Z
2026-04-06 10:43:00 UTC
20260406T104300Z
2026-096T10:43:00Z
2026W15-1T10:43:00Z
2026-04-06
-1605-120T12:34:56Z
```

### Legacy / Name-based

```
Jan 01 00:00:00 2000                     convsecs() output
Wed Jun 24 10:22:54 1992                 with day-of-week
Jun 24 10:22:54.123456 1992              with sub-seconds
April 6, 2026 10:43:00 UTC              full month, comma
Apr 6 2026 10:43:00 EST                 year before time
January 1st 2000 00:00:00 Z             ordinal suffix
6 Apr 2026 10:43:00 GMT                 European order
Mon, 6 Apr 2026 10:43:00 +0000          RFC 2822-like
Apr 6 2026 10:43:00pm Z                 AM/PM
```

### Explicitly Not Supported

```
04/06/2026        ambiguous (MM/DD vs DD/MM)
6.4.2026          ambiguous (DD.MM vs MM.DD)
Apr 6 26          ambiguous (year 26 vs day 26?)
```

## File Layout

```
mux/lib/date_scan.rl       Ragel source (scanner + parser in one file)
mux/lib/date_scan.cpp       Generated (read-only, chmod a-w)
mux/include/timeutil.h       ParseDate() signature unchanged
```

The old `mux/lib/timeparser.cpp` (1650-line deduction engine) has
been deleted.  `do_convtime()` remains in `timeutil.cpp` and is
still called by `CLinearTimeAbsolute::SetString()` as a first-try
fast path in `fun_convtime()`.

## Testing

- `testcases/convtime_fn.mux` — legacy format (7 SHA1-based tests)
- `testcases/parsedate_fn.mux` — 30 tests: ISO 8601, free-form,
  timezones, sub-seconds, boundaries, rejections
- All 998 smoke tests pass

## Size

- `date_scan.rl`: 1277 lines (scanner + recursive descent parser)
- Replaces `timeparser.cpp`: 1653 lines (multi-pass deduction engine)
