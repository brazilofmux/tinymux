# libmux (`mux/lib/`) — Open Issues

Created: 2026-04-10.

`mux/lib/` is the libmux shared library — stringutil, mathutil, alloc,
hash, SHA1, UTF-8, color primitives, and the Ragel date scanner. Issues
here affect every caller: driver, engine module, comsys, mail, and the
standalone tests under `tests/libmux/`.

## Critical — Ragel Date Scanner Bugs (New, 2026-04-10)

The `date_scan.rl` Ragel source drives both the flexible softcode `ParseDate`
path and the fixed-format `fun_convtime`/`fun_isodate` code. A full audit of
the newly-unified Ragel -G2 scanner + recursive descent parser surfaced the
following problems. Line numbers are against `date_scan.rl`; regenerated
`date_scan.cpp` line numbers differ.

### 32-bit integer overflow in `digit+` token accumulator
- **File:** `mux/lib/date_scan.rl:244-250`
- **Issue:** `val = val * 10 + (*d - '0')` with no bound check. `val` is a
  plain `int` — 10+ digit inputs (e.g., `"9999999999-01-01"`) overflow and
  are stored as negative years, bypassing every downstream range check.
- **Fix:** Clamp `ndig` (e.g., reject tokens with `ndig > 9`) or accumulate
  into `int64_t` and saturate before building the `DTT_NUM` token.

### Narrowing cast `int → short` on year without range validation
- **File:** `mux/lib/date_scan.rl:1139`
- **Issue:** `ft.iYear = static_cast<short>(dr.iYear)` truncates silently
  when `dr.iYear` falls outside `INT16_MIN..INT16_MAX`. Combined with the
  overflow above, parsing `"9999999999-01-01"` stores a nonsense year in
  the `FIELDEDTIME` record and then feeds it into
  `FieldedTimeToLinearTime`.
- **Fix:** Validate `dr.iYear` against the tighter `[-27256, 30826]` range
  that `timeutil.cpp` already enforces elsewhere; reject before narrowing.

### Time-of-day fields not checked against zero
- **File:** `mux/lib/date_scan.rl:625, 690` (approximate)
- **Issue:** `iHour`, `iMinute`, `iSecond` are checked for the upper bound
  (`> 23`, `> 59`) but not for `>= 0`. A token like `"-5:30:00"` (the
  scanner does not reject the leading minus because of tokenization order)
  stores a negative hour and propagates into arithmetic that assumes
  unsigned.

### 12-hour "12:30 AM" is rejected
- **File:** `mux/lib/date_scan.rl:635-643` (approximate)
- **Issue:** For `iHour == 12` the code sets `iHour = 0` then *adds*
  `mer->iVal` (0 for AM, 12 for PM). But the upper-bound check fires before
  the AM/PM adjustment runs, so the parser rejects `"12:30:00 AM"` entirely.
- **Fix:** Apply the meridian shift first, then validate.

### Month-specific day limits deferred to downstream
- **File:** `mux/lib/date_scan.rl:842` (approximate)
- **Issue:** The day-of-month check is a blanket `iDayOfMonth > 31`. The
  parser accepts `"Feb 31"` and `"Apr 31"` and relies on `isValidDate()` at
  ~line 1211 to catch them. Fine in theory, but the delayed check means
  some callers that use the intermediate record (e.g., partial date
  completion) see an invalid state they don't expect.

### ISO week-date conversion runs with `iWeekOfYear == 0`
- **File:** `mux/lib/date_scan.rl:1141-1148` (approximate)
- **Issue:** If `parse_iso()` sets `bHasWeek` without ever assigning a
  week value, `ConvertWeekDateToLinearTime()` runs with `iWeekOfYear == 0`
  and produces a date seven days *before* the ISO-year start, silently
  wrapping into the previous year.
- **Fix:** Require `dr.iWeekOfYear >= 1` as a precondition for the
  week-date branch.

### Sub-second hecto-nanosecond conversion loses precision silently
- **File:** `mux/lib/date_scan.rl:1150-1162` (approximate)
- **Issue:** `iFracHectoNano` (range 0..9,999,999) is split into
  millisecond / microsecond / nanosecond fields via narrowing casts into
  `unsigned short`. Values near the upper bound wrap around the field
  limits (e.g., `(frac / 10) % 1000` after narrowing). No test case
  exercises the edge.

### Timezone offset sign semantics are correct but fragile
- **File:** `mux/lib/date_scan.rl:1222` (approximate)
- **Issue:** `ltd.SetSeconds(60 * dr.iTzMinutes); lt -= ltd;` relies on the
  scanner having stored the offset with the right sign. A change to the
  tokenizer that inverts the sign (e.g., to align with `strftime`'s
  convention) would silently corrupt every parsed timestamp. Add a doc
  comment documenting the invariant, and consider switching to an explicit
  `SetSeconds(-60 * minutes)` / `lt += ltd` form so the sign is local to
  the assignment.

## Opportunities

### No dedicated date-parser fuzz/boundary tests in `tests/libmux/`
- **File:** `tests/libmux/test_libmux.cpp`
- **Issue:** The smoke tests at `testcases/convtime_fn.mux`,
  `testcases/parsedate_*.mux` cover happy paths, but there is no native
  harness hitting the scanner directly for overflow, narrowing, or
  negative-zero corner cases. Add a small unit-test driver under
  `tests/libmux/` that links the generated `date_scan.cpp` and runs
  adversarial inputs (large numbers, negative signs, empty strings,
  truncated ISO forms, century-rollover weeks).

### ZWJ emoji sequences still unimplemented (see memory)
- **Issue:** `utf/` never finished GB11 ZWJ cluster support; the color
  width calculation and `strdistance()` treat ZWJ sequences as their
  constituent code points. Tracked in
  `memory/project_zwj_deferred.md`; kept here as a pointer so the
  libmux tracker lists it.
