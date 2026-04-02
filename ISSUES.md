# TinyMUX Project Issues Tracker

This is the top-level tracker for the TinyMUX project. It links to specialized issue trackers in sub-directories.

## Active Trackers

- **[Core Server (mux/src/)](mux/src/ISSUES.md):** Buffer overflows, signal safety, restart-file validation, error handling.
- **[Engine Module](mux/modules/engine/ISSUES.md):** JIT/DBT optimizations, Windows support, concurrent evaluation, COM issues, platform drivers.
- **[Mail Module](mux/modules/mail/ISSUES.md):** Unchecked COM interface acquisition, thread safety, memory management.
- **[Comsys Module](mux/modules/comsys/ISSUES.md):** Unchecked COM interface acquisition, missing channel consistency checks.
- **[Hydra Clients](client/ISSUES.md):** Credential storage, capability negotiation regressions, reconnect bugs.
- **[Console Client](client/console/ISSUES.md):** Stale viewport on reconnect, write context leaks, thread safety.
- **[Android Client](client/android/ISSUES.md):** Idle time wiring, inputChannel race condition.
- **[iOS Client](client/ios/ISSUES.md):** Missing SetPreferences, EventLoopGroup leak.
- **[TinyFugue Client](client/tf/ISSUES.md):** Hydra reconnect drops capabilities, hardcoded viewport.
- **[Test Infrastructure](testcases/ISSUES.md):** SHA1 hash brittleness, coverage gaps, no test discovery.
- **[Parser Research Tools](parser/ISSUES.md):** Evaluator compatibility gaps, build warnings, escape oracle curation.
- **[DB Backend Tests](tests/db/ISSUES.md):** Standalone harness breakage after file moves.
- **[WorldBuilder](tools/worldbuilder/ISSUES.md):** Test path sensitivity, file handle leak, incomplete lint function list.
- **[Docker](docker/ISSUES.md):** Outdated images referencing MUX 2.12.
- **[Debian Packaging](debian/ISSUES.md):** Missing build dependencies, outdated metadata.
- **[Windows Service](muxsvc/ISSUES.md):** Placeholder service loop, event name collisions.

## Release Scripts

### ~~`dowin32.sh` lacks error handling~~ NOT A BUG

- Survey was incorrect: `dowin32.sh` already has `set -e` (line 8) and `set -o pipefail` (line 9).

### ~~Shell injection risk in `dowin32.sh` path handling~~ FIXED

- `dirname $rel_path` now properly quoted as `dirname "$rel_path"` at all four call sites.

### Insecure external downloads in `dowin32.sh`

- **File:** `dowin32.sh:357, 368`
- **Issue:** Downloads xdelta3 and patch.exe from GitHub/SourceForge via PowerShell without signature or hash verification.
- **Risk:** Supply-chain compromise if URLs are tampered with or stale.

### ~~Inconsistent diff/patch error handling between scripts~~ FIXED

- `dowin32.sh` now checks diff exit code: 0/1 are normal (identical/different), exit code >= 2 aborts with error message.

## Survey Summary (2026-03-27)

### By Severity

| Severity | Count | Key Areas |
|----------|-------|-----------|
| Critical | ~10 | Buffer overflows in restart-file loading, signal handler safety, Docker images |
| High | ~15 | Unchecked COM acquisitions, missing error paths, credential storage, release script errors |
| Medium | ~20 | Thread safety, coverage gaps, packaging, test brittleness |
| Low | ~10 | Dead code, stubs, modernization opportunities |

### Newly Created Trackers

- `mux/src/ISSUES.md` — 18 issues across buffer safety, signal handling, error paths
- `mux/modules/mail/ISSUES.md` — 5 issues in COM error handling and memory management
- `mux/modules/comsys/ISSUES.md` — 5 issues in COM error handling and data integrity
- `testcases/ISSUES.md` — 7 issues in test infrastructure and coverage
- `docker/ISSUES.md` — 3 issues in container images
- `debian/ISSUES.md` — 5 issues in packaging
- `muxsvc/ISSUES.md` — 4 issues in Windows service code

### Updated Trackers

- `mux/modules/engine/ISSUES.md` — Added COM/module system issues (SIZE_HACK, silent storage init failure, exp3 unchecked interfaces)
- `client/ISSUES.md` — Added cross-client credential storage and spawn regex issues
- `client/android/ISSUES.md` — Added inputChannel race condition
- `client/ios/ISSUES.md` — Added EventLoopGroup fix guidance
- `client/console/ISSUES.md` — Added write context leak, grpc_ race, TerminateThread, world file parsing
- `client/tf/ISSUES.md` — Added reconnect fix pattern note
- `tools/worldbuilder/ISSUES.md` — Added file handle leak, lint gaps, SSL validation, live adapter fragility
- `parser/ISSUES.md` — Added escape oracle maintenance opportunity
