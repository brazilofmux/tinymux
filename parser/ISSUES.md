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

### ~~Escape oracle corpus requires manual curation~~ FIXED

- **File:** `escape_oracle_cases.txt`, `validate_live_oracle.sh`
- `parser/validate_live_oracle.sh` is a new automated check that runs every corpus case against the real `engine.so` (via `mux/game/bin/muxscript -e 'think <expr>'`), extracts the value, and compares it byte-for-byte against the `mux214` column. It reports `PASS`/`DRIFT` per row, flags rows where `live_status != confirmed`, and exits non-zero on any drift. An optional third argument accepts a built mux2.13_N `muxscript` binary so the same run can compare against the historical reference engine when one is available.
- The initial run flagged five drifted rows (`plain_single_unknown`, `plain_double_unknown`, `plain_single_known`, `plain_double_known`, `plain_triple_known`). Reviewing `mux2.13_13/src/eval.cpp:2439-2460` (the backslash handler) and `mux2.13_13/src/eval.cpp:1680-1699` (the percent handler under `EV_EVAL` off) showed that the live mux2.14 engine agrees with the 2.13 ground truth on all five — the corpus columns were wrong, not the engines. Corpus updated to match; `./eval`'s theoretical model is intentionally left alone and `test_eval.sh` still exercises that model exactly as before. The validator now passes 9/9 and becomes the authoritative cross-check whenever someone touches either side.
