# TinyMUX Project Issues Tracker

This is the top-level index for TinyMUX issue trackers. Per-tracker
files keep their full FIXED / FALSE ALARM / NOT A BUG history for
context; this file only summarises what is still open. When a tracker
lists zero open items, the file is kept as a historical record of the
audit passes that closed it.

Last refreshed: 2026-04-10.

## Trackers With Open Items

| Tracker | Open | Summary |
|---|---|---|
| [Core Server (`mux/src/`)](mux/src/ISSUES.md) | 16 | Windows console signal-handler TODO; `Stub_PipePump` data loss; `slave.cpp` overflow; `SBUF_SIZE` limits; `netaddr.cpp` UB/overflow *and* critical broken-hex IPv4 parse; websocket snprintf/RFC 6455 gaps; `platform.cpp` `PanicRestart` argv UB. |
| [libmux (`mux/lib/`)](mux/lib/ISSUES.md) | 8 | Ragel date scanner: digit-token overflow, narrowing year cast, time lower bounds, "12:30 AM", week-date 0, sub-second narrowing, TZ sign fragility. |
| [Engine Module](mux/modules/engine/ISSUES.md) | 15 | `alloc_lbuf` RAII migration remainder; dynamic-cargs `ulambda` JIT; JIT JAL offset overflow (critical); HIR bounds checks; attr-cache leak on invalidation; Lua refcount/Alloc bugs; SQLite error-path hygiene. |
| [GANL Networking](mux/ganl/ISSUES.md) | 8 | OpenSSL lifetime races; `recv()==0` handling; setsockopt return codes; telnet subnegotiation timeout gap; STARTTLS CA trust. |
| [SQLSlave Module](mux/modules/sqlslave/ISSUES.md) | 8 | Non-atomic refcount; unchecked `m_pServer`; ownership contract on `Connect()`; MySQL error propagation. |
| [Hydra Clients (aggregate)](client/ISSUES.md) | 1 | GMCP handled as raw JSON only (structured hooks deferred). |
| [Web Client](client/web/ISSUES.md) | 2 | localStorage credentials; no browser-level regression harness. |
| [Win32 GUI Client](client/win32gui/ISSUES.md) | 1 | No Linux-side build validation for the VS target. |
| [Test Infrastructure](testcases/ISSUES.md) | 6 | SHA1→semantic migration still in progress; edge-case coverage gaps; single-test-per-function norm; no auto-discovery; no parallel/isolation; no orphaned-object cleanup. |

## Fully Closed Trackers (history preserved)

These trackers have no open items left; they are kept for their audit
history and FIXED entries.

- [Mail Module](mux/modules/mail/ISSUES.md)
- [Comsys Module](mux/modules/comsys/ISSUES.md)
- [Hydra Proxy](mux/proxy/ISSUES.md)
- [Android Client](client/android/ISSUES.md)
- [TinyFugue Client](client/tf/ISSUES.md)
- [Docker](docker/ISSUES.md)
- [Debian Packaging](debian/ISSUES.md)
- [WorldBuilder](tools/worldbuilder/ISSUES.md)
- [Parser Research Tools](parser/ISSUES.md)
- [Console Client](client/console/ISSUES.md)
- [iOS Client](client/ios/ISSUES.md)
- [DB Backend Tests](tests/db/ISSUES.md)

## Build System (top-level)

### ~~Subshell array variable loss in `dowin32.sh`~~ FIXED

- Both `find` loops in `process_distribution()` already use process substitution (`done < <(find ...)`), so the arrays are populated correctly in the parent shell.

### ~~Weak SSL/crypto library detection in configure.ac~~ FIXED

- **File:** `mux/configure.ac:273-274`
- `configure.ac` now probes `SSL_new` in `-lssl` and `EVP_sha256` in `-lcrypto`, so `HAVE_LIBSSL`/`HAVE_LIBCRYPTO` reflect real OpenSSL entry points instead of a meaningless `main` symbol. OpenSSL remains mandatory via the existing `PKG_CHECK_MODULES([OPENSSL], [openssl], ...)` failure path.

### ~~Unquoted variable expansions in `dowin32.sh`~~ FIXED

- All variable expansions in `[ -e ... ]` tests, `rm` commands, and `ls` display lines are now properly double-quoted. Glob suffixes kept outside the quotes to preserve expansion.

### ~~Insecure external downloads in `dowin32.sh`~~ FIXED

- Generated `get_xdelta3.bat` and `get_patch.bat` now verify SHA256 hashes of downloaded archives via PowerShell `Get-FileHash` before extracting. Hash mismatches abort with an error and delete the untrusted download. Expected hashes are defined as `XDELTA3_SHA256` and `PATCH_SHA256` variables in `dowin32.sh`.

### ~~`dowin32.sh` lacks error handling~~ NOT A BUG

- Survey was incorrect: `dowin32.sh` already has `set -e` (line 8) and `set -o pipefail` (line 9).

### ~~Shell injection risk in `dowin32.sh` path handling~~ FIXED

- `dirname $rel_path` now properly quoted as `dirname "$rel_path"` at all four call sites.

### ~~Inconsistent diff/patch error handling between scripts~~ FIXED

- `dowin32.sh` now checks diff exit code: 0/1 are normal (identical/different), exit code >= 2 aborts with error message.
