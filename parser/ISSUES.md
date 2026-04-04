# Parser Research Tools — Open Issues

Updated: 2026-03-27

## Bugs

### ~~Percent-escape handling still fails two documented MUX 2.13 cases~~ FIXED

- `noevalPass()` now mirrors the evaluator’s existing `NODE_ESCAPE` + `NODE_SUBST` sequence handling, so deferred FN_NOEVAL bodies in `switch()` and `iter()` collapse `\\%b` to `%b` before the second evaluation pass. `./test_eval.sh` now passes the documented `mux213` brace-body cases.

## Opportunities

### ~~`test_eval.sh` fails noisily when `./eval` is missing~~ FIXED

- `test_eval.sh` now enables `set -euo pipefail`, builds `./eval` on demand with `make eval`, and exits once with an actionable error if the build fails instead of emitting one identical failure per test case.

### ~~`make` is not warning-clean under `-Wall -Wextra`~~ FIXED

- Changed `token_name()` from `static` to `inline` in `mux_parse.h` — eliminates unused-function warning in translation units that don't call it.

### Escape oracle corpus requires manual curation

- **File:** `escape_oracle_cases.txt`
- **Issue:** Cases must be manually marked `confirmed` after live-engine checks. Stale guesses accumulate over time with no automated validation mechanism.
- **Opportunity:** Script an automated check that runs cases against a live engine and flags divergences.
