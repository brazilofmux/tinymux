# TinyMUX Project Issues Tracker

This is the top-level tracker for the TinyMUX project. It links to specialized issue trackers in sub-directories.

## Active Trackers

- **[Core Server (mux/src/)](mux/src/ISSUES.md):** Telnet USER overflow, set_doing overflow, null derefs after alloc_lbuf/MEMALLOC, encode_iac bounds, static buffers.
- **[Engine Module](mux/modules/engine/ISSUES.md):** strcat overflow in fun_rxlevel/txlevel, SQLite error paths, JIT global state safety, dbt_reset null check.
- **[GANL Networking](mux/ganl/ISSUES.md):** Unbounded buffer growth, dangling pointers in select engine, NAWS validation, TTYPE length, negotiation timeout.
- **[Mail Module](mux/modules/mail/ISSUES.md):** Non-atomic m_cRef, mail_to_list pointer arithmetic, strdup null check, shutdown use-after-free.
- **[Comsys Module](mux/modules/comsys/ISSUES.md):** Non-atomic m_cRef, strncpy usage.
- **[Hydra Clients](client/ISSUES.md):** Credential storage, capability negotiation regressions, reconnect bugs.
- **[Console Client](client/console/ISSUES.md):** Write context leaks, CHARSET negotiation DoS.
- **[TinyFugue Client](client/tf/ISSUES.md):** grpc_ race, /update shell injection, gRPC channel null check.
- **[Web Client](client/web/ISSUES.md):** ANSI parser bounds checks, localStorage credentials.
- **[Android Client](client/android/ISSUES.md):** Idle time wiring, inputChannel race condition.
- **[iOS Client](client/ios/ISSUES.md):** Missing SetPreferences, EventLoopGroup leak.
- **[Test Infrastructure](testcases/ISSUES.md):** SHA1 brittleness, mktemp trap leak, omega skip, error context.
- **[Parser Research Tools](parser/ISSUES.md):** Evaluator compatibility gaps, build warnings, escape oracle curation.
- **[DB Backend Tests](tests/db/ISSUES.md):** Standalone harness breakage after file moves.
- **[WorldBuilder](tools/worldbuilder/ISSUES.md):** Test path sensitivity, file handle leak, incomplete lint function list.
- **[Docker](docker/ISSUES.md):** Outdated images referencing MUX 2.12.
- **[Debian Packaging](debian/ISSUES.md):** Missing build dependencies, outdated metadata.
- **[Windows Service](muxsvc/ISSUES.md):** Placeholder service loop, event name collisions.

## Build System (New, 2026-04-04)

### Subshell array variable loss in `dowin32.sh`

- **File:** `dowin32.sh:102-154`
- **Issue:** Arrays `binary_files`, `text_files`, `new_files`, and `removed_files` are modified inside `find | while read` loops. Due to bash subshell semantics, modifications are lost when the pipe ends. The arrays are always empty at lines 151-154, breaking the entire Windows patch generation.
- **Impact:** Windows distribution patch files (`binary_files.txt`, etc.) are always empty.
- **Recommendation:** Use process substitution `while read ... < <(find ...)` instead of piped while loops.

### ~~Weak SSL/crypto library detection in configure.ac~~ FIXED

- **File:** `mux/configure.ac:273-274`
- `configure.ac` now probes `SSL_new` in `-lssl` and `EVP_sha256` in `-lcrypto`, so `HAVE_LIBSSL`/`HAVE_LIBCRYPTO` reflect real OpenSSL entry points instead of a meaningless `main` symbol. OpenSSL remains mandatory via the existing `PKG_CHECK_MODULES([OPENSSL], [openssl], ...)` failure path.

### Unquoted variable expansions in `dowin32.sh`

- **File:** `dowin32.sh:77-78, 85`
- **Issue:** Path variables like `$CHANGES_DIR`, `$DISTRO_DIR`, `$NEW_DIR` used unquoted in `cp` and `chmod` commands. Breaks on paths containing spaces.
- **Recommendation:** Quote all variable expansions.

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

## Survey Summary (2026-04-04 refresh)

### By Severity

| Severity | Count | Key Areas |
|----------|-------|-----------|
| Critical | ~14 | Buffer overflows (telnet USER, set_doing, encode_iac), signal safety (FIXED), restart-file (FIXED), dowin32.sh subshell bug |
| High | ~22 | Null derefs (alloc_lbuf, MEMALLOC), unbounded GANL buffers, non-atomic refcounts, credential storage, strcat in fun_rxlevel/txlevel |
| Medium | ~28 | SQLite error paths, JIT global state, NAWS validation, CHARSET DoS, test script reliability, build system gaps |
| Low | ~12 | Dead code, stubs, negotiation timeouts, modernization opportunities |

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
