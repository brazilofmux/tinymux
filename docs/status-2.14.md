# TinyMUX 2.14 — status

**2026-07-21: 2.14 is feature-complete.** Zero open issues, zero open
pull requests, all suites green on both development platforms (macOS
arm64, Linux x86-64; the Android client builds on the Windows box).

What "feature-complete" means here: the feature surface is frozen.
Remaining work for the 2.14 line is hardening — test cases, stress and
soak testing, differential sweeps, and fixes for whatever those pushes
surface. New capability belongs to a future line.

## The standing verification estate

- `make test` — smoke suite (1319 TCs, run under both `jit_eval_brackets`
  states), GANL engine harness, netaddr units, the JIT q-register oracle,
  and iOS Titan parser tests.
- `testcases/tools/jit_diff/` — differential fuzzer with composable
  corpus modes (`JITDIFF_BRACKETS`, `JITDIFF_UTF8`, `JITDIFF_LONGREG`,
  `SEED=n`) and `soak.sh` for extended runs.
- `make test-scenario` — live-socket wildcard-capture coverage (opt-in).
- `docs/plan-jit-evalbracket-lift.md` — the completed JIT guard-lift
  campaign record, including the probe/carrier methodology that the
  hardening phase should reuse.

## Known-untested edges (documented, not blocking)

- Android Hydra session resume (#762, PR #1006) is built but not
  runtime-tested — no AVD/live Hydra server on the build box; the
  specific edge is Connect-after-resume with an active link (behavior
  shared verbatim with the Console client).
- `function_recursion_limit` has a documented one-level skew across
  ECALL boundaries (see the plan doc's #1002 closeout).

## Candidate hardening directions

Differential/stress campaigns modeled on the JIT push, aimed at
surfaces that have not had one: @-command side effects, the task queue,
comsys; long netmux-under-socket-load soaks; corpus growth over the
existing sweep modes.
