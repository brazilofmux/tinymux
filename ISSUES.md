# TinyMUX Project Issues Tracker

This is the top-level index for TinyMUX issue trackers. Per-tracker
files keep their full FIXED / FALSE ALARM / NOT A BUG history for
context; this file only summarises what is still open. When a tracker
lists zero open items, the file is kept as a historical record of the
audit passes that closed it.

Last refreshed: 2026-04-05.

## Trackers With Open Items

| Tracker | Open | Summary |
|---|---|---|
| [Core Server (`mux/src/`)](mux/src/ISSUES.md) | 3 | Widespread `static` buffers in functions; commented-out debug logging in `ganl_adapter.cpp`; Windows console signal-handler TODO. |
| [Engine Module](mux/modules/engine/ISSUES.md) | 3 | Migration from manual `alloc_lbuf`/`free_lbuf` to RAII; `s_arenas` JIT global state (partially mitigated via `thread_local`); dynamic-cargs `ulambda` JIT support. |
| [Hydra Clients (aggregate)](client/ISSUES.md) | 2 | GMCP handled as raw JSON only; cross-client plaintext credential storage. |
| [Console Client](client/console/ISSUES.md) | 1 | Pending overlapped-write teardown is unsafe. |
| [iOS Client](client/ios/ISSUES.md) | 2 | Viewport sizing uses coarse screen-bounds estimate; terminal size not refreshed after connect. |
| [Web Client](client/web/ISSUES.md) | 3 | ANSI 256-color/truecolor parser missing bounds checks; localStorage credentials; no browser-level regression harness. |
| [Win32 GUI Client](client/win32gui/ISSUES.md) | 2 | No Linux-side build validation for the VS target; plaintext credentials in world storage. |
| [Test Infrastructure](testcases/ISSUES.md) | 6 | SHA1→semantic migration still in progress; edge-case coverage gaps; single-test-per-function norm; no auto-discovery; no parallel/isolation; no orphaned-object cleanup. |
| [Parser Research Tools](parser/ISSUES.md) | 1 | Escape oracle corpus requires manual curation. |
| [WorldBuilder](tools/worldbuilder/ISSUES.md) | 1 | Live adapter carries undocumented server-format assumptions. |
| [Windows Service](muxsvc/ISSUES.md) | 2 | Service main loop is a placeholder; outdated service pattern. |

## Fully Closed Trackers (history preserved)

These trackers have no open items left; they are kept for their audit
history and FIXED entries.

- [Mail Module](mux/modules/mail/ISSUES.md)
- [Comsys Module](mux/modules/comsys/ISSUES.md)
- [GANL Networking](mux/ganl/ISSUES.md)
- [Hydra Proxy](mux/proxy/ISSUES.md)
- [Android Client](client/android/ISSUES.md)
- [TinyFugue Client](client/tf/ISSUES.md)
- [Docker](docker/ISSUES.md)
- [Debian Packaging](debian/ISSUES.md)
- [DB Backend Tests](tests/db/ISSUES.md)

## Build System (top-level)

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

### Insecure external downloads in `dowin32.sh`

- **File:** `dowin32.sh:357, 368`
- **Issue:** Downloads xdelta3 and patch.exe from GitHub/SourceForge via PowerShell without signature or hash verification.
- **Risk:** Supply-chain compromise if URLs are tampered with or stale.

### ~~`dowin32.sh` lacks error handling~~ NOT A BUG

- Survey was incorrect: `dowin32.sh` already has `set -e` (line 8) and `set -o pipefail` (line 9).

### ~~Shell injection risk in `dowin32.sh` path handling~~ FIXED

- `dirname $rel_path` now properly quoted as `dirname "$rel_path"` at all four call sites.

### ~~Inconsistent diff/patch error handling between scripts~~ FIXED

- `dowin32.sh` now checks diff exit code: 0/1 are normal (identical/different), exit code >= 2 aborts with error message.
