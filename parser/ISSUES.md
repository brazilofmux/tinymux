# Parser Research Tools — Open Issues

Updated: 2026-03-27

## Bugs

### Percent-escape handling still fails two documented MUX 2.13 cases

- **Evidence:** `./test_eval.sh` currently reports `146 passed, 2 failed`.
- **Cases:** `parser/test_eval.sh:155` expects `[switch(1,1,{\\%b})]` to produce a single space for the `mux213` profile, but the evaluator returns `b`. `parser/test_eval.sh:156` expects `[iter(a b,{\\%b})]` to produce three spaces, but the evaluator returns `b b`.
- **Impact:** The standalone evaluator still diverges from the escape-study expectations in `FN_NOEVAL` contexts, which makes it risky to use as an oracle for engine-porting work.
- **Likely scope:** `eval.cpp` percent-substitution handling inside brace-preserving and iterator/switch noeval paths.

## Opportunities

### `make` is not warning-clean under `-Wall -Wextra`

- **Evidence:** `make` in `parser/` succeeds, but `parse.cpp` and `eval.cpp` both warn that `token_name(TokenType)` in `parser/mux_parse.h:78` is defined but unused.
- **Impact:** Small warning drift makes it harder to notice new parser-tool regressions in CI or local builds.

### Escape oracle corpus requires manual curation

- **File:** `escape_oracle_cases.txt`
- **Issue:** Cases must be manually marked `confirmed` after live-engine checks. Stale guesses accumulate over time with no automated validation mechanism.
- **Opportunity:** Script an automated check that runs cases against a live engine and flags divergences.
