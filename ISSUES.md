# TinyMUX Project Issues Tracker

This is the top-level index for TinyMUX issue trackers. Per-tracker
files keep their full FIXED / FALSE ALARM / NOT A BUG history for
context; this file only summarises what is still open. When a tracker
lists zero open items, the file is kept as a historical record of the
audit passes that closed it.

Last refreshed: 2026-05-22 (post Apple Silicon JIT enablement and April 2026 safety audits).

## Trackers With Open Items

| Tracker | Open | Summary |
|---|---|---|
| [Core Server (`mux/src/`)](mux/src/ISSUES.md) | 1 | Windows console `SetConsoleCtrlHandler` TODO (platform.cpp:142). All April 2026 buffer/UB/signal/WebSocket/netaddr/Stub_PipePump/PanicRestart criticals documented as FIXED in sub-tracker. |
| [libmux (`mux/lib/`)](mux/lib/ISSUES.md) | 10 | Ragel `date_scan.rl`: 8 bugs (32-bit digit overflow, `int→short` year narrowing, negative time-of-day fields, "12:30 AM" rejection, `iWeekOfYear==0` week-date, sub-second narrowing loss, TZ sign fragility) + 2 opportunities (dedicated fuzz tests; ZWJ emoji clusters). |
| [Engine Module](mux/modules/engine/ISSUES.md) | ~17 | `alloc_lbuf` RAII remainder (~90 sites); dynamic `cargs` for `ulambda` JIT; critical RV64 JAL 21-bit offset overflow + HIR `iv.value` bounds; attr-cache stale-code leak (unbounded heap growth); Lua `m_cRef` non-atomic + `s_next_key` + `LuaAlloc` drift + snprintf cast; 5+ SQLite hygiene (null blobs, reset ordering, WAL BUSY, ROLLBACK, LoadAllAttrNames); numeric 9-digit threshold TODO. |
| [GANL Networking](mux/ganl/ISSUES.md) | 9 | OpenSSL session ptr deref outside lock + `sslObjectsToFree` drain race; `recv()==0` / EOF mishandled (no Closing state); setsockopt (REUSE*, IPV6_V6ONLY, nonblock) return codes ignored; telnet subnegotiation stall on missing IAC SE + IAC IAC drop on full buf; no CA trust logging for STARTTLS. (Plus minor CHARSET client-list TODO.) |
| [SQLSlave Module](mux/modules/sqlslave/ISSUES.md) | 9 | Non-atomic `m_cRef`/`g_c*` counters; `ConnectionHelper` derefs `m_pServer` w/o nullcheck; `Connect()` delete[] ownership contract (ABI hazard for out-of-proc); MySQL errors silent (`real_connect`, `next_result`, options); reconnect hook empty; broad `catch(...)`. |
| [Test Infrastructure](testcases/ISSUES.md) | 5 | `isjson({"a":1})` JIT (preserves brace-group) vs legacy `parse_to` (strips) divergence — open which is "correct" (non-JIT path is the semantic bug per investigation); 84% SHA1 hash snapshots (brittle); edge/unicode/permission/overflow coverage gaps; single happy-path case per function norm. (Apple Silicon JIT now enabled 2026-05-02; re-probe recommended.) |

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
- [Web Client](client/web/ISSUES.md)
- [Win32 GUI Client](client/win32gui/ISSUES.md)
- [Hydra Clients (aggregate)](client/ISSUES.md)

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
