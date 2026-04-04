# MUX Core Server (mux/src/) — Open Issues

Updated: 2026-03-27

## ~~Critical — Buffer Overflows & Memory Safety~~ FIXED

### ~~Buffer overflow in `load_restart_db()` memcpy calls~~ FIXED

- All `memcpy()` calls into fixed-size descriptor fields (`d->addr`, `d->doing`, `d->username`) and `alloc_lbuf()` buffers (`output_prefix`, `output_suffix`) now clamp to the destination size before copying.

### ~~Unvalidated array index from restart file~~ FIXED

- `num_main_game_ports` is now validated against `MAX_LISTEN_PORTS` (or `MAX_LISTEN_PORTS * 2` with SSL) before the loop. Out-of-range values close the file and abort restart gracefully.

### ~~Missing null check after `getstring_noalloc()`~~ NOT A BUG

- `getstring_noalloc()` returns a static buffer, never null. No fix needed.

### ~~Missing null check after `ConvertToUTF8()`~~ NOT A BUG

- `ConvertToUTF8()` returns a static buffer, never null. The real risk was the unbounded copy, now fixed above.

## Critical — Signal Handler Safety

### ~~Non-async-signal-safe calls in crash signal handler~~ FIXED

- Crash signals (SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGTRAP, etc.) now use only async-signal-safe functions: `check_panicking()` (volatile write + `signal`/`kill`), `PanicRestart()` (`fork` + `execl`), `_exit()`. Removed: `Flush()`, `log_signal()`, `drv_Report()`, `drv_PresyncDatabaseSigsegv()`, `final_stubslave()`, `final_modules()`, `raw_broadcast()`, and `GetBCanRestart()` COM call.
- Restart decision uses cached `g_bCanRestart` (volatile sig_atomic_t) set by the GANL main loop from `GetBCanRestart()` before entering the event loop.
- SIGABRT handler also stripped to `_exit(134)` — no logging or engine calls.

### ~~Non-async-signal-safe calls in normal signal handlers~~ FIXED

- All normal signal handlers converted to flag-based deferral. Signal handler now only sets `volatile sig_atomic_t` flags (`g_restart_flag`, `g_dump_flag`, `g_sigchld_flag`, `g_shutdown_signal`/`g_shutdown_flag`). The GANL main loop polls these flags and performs restart, dump, child reaping, and shutdown broadcast in safe context.
- `signal_desc()` and `log_signal()` made non-static for use by the main loop.

### ~~`g_shutdown_flag` is not volatile~~ FIXED

- Changed from `bool` to `volatile sig_atomic_t` in `bsd.cpp` and `driverstate.h`. All write sites updated.

### ~~`g_panicking` is not volatile~~ FIXED

- Changed from `bool` to `volatile sig_atomic_t` in `bsd.cpp` and `driverstate.h`. All write sites updated.

### ~~`g_dump_child_pid` race condition~~ FIXED

- Changed from `volatile pid_t` to `volatile sig_atomic_t` in `bsd.cpp` and `driverstate.h`. Added explicit casts in `ganl_adapter.cpp`.

## High — Unchecked Return Values & Error Paths

### ~~`getsockname()` failure crashes server~~ FIXED

- Replaced `mux_assert(0)` with `mux_fclose(f); g_restarting = false; return;`

### ~~File not closed on invalid restart version~~ FIXED

- Replaced `mux_assert(0)` with `mux_fclose(f); g_restarting = false; return;`

### ~~`fgets()` return value unchecked~~ FIXED

- Now checks `fgets()` return and `strncmp()` result; closes file and returns on failure.

### ~~Unvalidated enum/range values from restart file~~ FIXED

- `d->height` and `d->width` now clamped to 1..512 (default to 24x78 on out-of-range). `d->encoding` clamped to 0..255 (default to `g_dc.default_charset`). Applied in both version 2 and version 3+ paths.

## Medium — Code Quality

### ~~`ISOUTOFMEMORY` macro terminates instead of recovering~~ FIXED

- Removed `ISOUTOFMEMORY` macro entirely from `config.h`. All 23 call sites replaced with site-appropriate handling: truly unrecoverable allocations (buffer pools, db array, anum table, markbuf) use `mux_assert` or `OutOfMemory`; recoverable sites (queue entries, commands, mail, guests, vattrs, config, restart ttype, forward lists) log the failure and return gracefully.

### ~~Inconsistent error handling patterns~~ MOSTLY RESOLVED

- Remaining `mux_assert(0)` sites in `load_restart_db()` were previously fixed. OOM handling now consistently uses per-site recovery.

### ~~Integer overflow potential in buffer calculations~~ FALSE ALARM

- `getstring_noalloc()` reads into a static `buf[2*LBUF_SIZE + 20]` (~65KB). The returned `nBuffer` is always bounded by this buffer size, so `nBuffer+1` cannot overflow `size_t`.

### ~~Volatile counters in slave.cpp lack atomicity~~ PARTIALLY FIXED

- Changed from `volatile int` to `volatile sig_atomic_t`. The `++` in a signal handler is still technically non-atomic, but `sig_atomic_t` is the POSIX-sanctioned type for this pattern.

## Low — Dead Code & Technical Debt

### TODO: Windows console signal handler

- **File:** `modules.cpp:1255`
- **Issue:** `// TODO: SetConsoleCtrlHandler that maps events to PlatformSignal.`

### Commented-out debug logging

- **File:** `ganl_adapter.cpp:744, 1114, 1853, 3058, 3071, 3142, 3152, 3156`
- **Issue:** Multiple commented-out logging statements scattered through networking code.

### ~~Telnet state array magic number~~ FIXED

- Added `static constexpr int NVT_TABLE_SIZE = 256` to `descriptor_data` in `interface.h`. All loop bounds in `net.cpp` now use `DESC::NVT_TABLE_SIZE` instead of bare `256`.

## Critical — Buffer Overflows (New, 2026-04-04)

### ~~Buffer overflow in telnet USER environment variable~~ FIXED

- **File:** `mux/src/telnet.cpp:1221`
- The NEW-ENVIRON `USER` path now clamps `nVarval` to `sizeof(d->username) - 1` and re-terminates the temporary buffer before copying into `d->username[11]`.

### ~~Unbounded `set_doing_all()` and `set_doing_least_idle()`~~ FIXED

- **File:** `mux/src/net.cpp:1080, 1093`
- Both functions now clamp `len` to `SIZEOF_DOING_STRING - 1` before copying into descriptor `doing[]` buffers.

## High — Null Pointer Dereferences (New, 2026-04-04)

### Missing null check after `alloc_lbuf()` for output_prefix/output_suffix

- **File:** `mux/src/net.cpp:3381-3383, 3400-3402`
- **Issue:** `alloc_lbuf("set_userstring")` return values are not checked for null before `memcpy()` dereference during restart file loading.
- **Impact:** Crash on OOM during restart.

### Missing null check after `alloc_lbuf()` for raw_input_buf

- **File:** `mux/src/telnet.cpp:606-607`
- **Issue:** `alloc_lbuf("process_input.raw")` not checked before `d->raw_input_at = d->raw_input_buf` assignment and subsequent use.
- **Impact:** Crash on OOM during telnet input processing.

### Missing null check after `MEMALLOC()` for ttype

- **File:** `mux/src/telnet.cpp:1095-1096, 1487-1488`
- **Issue:** `MEMALLOC(nTermType+1)` and `MEMALLOC(nClient+1)` results not checked before `memcpy()` in TTYPE and GMCP handlers respectively.
- **Impact:** Crash on OOM during telnet negotiation.

## High — Buffer Safety & Static Buffer Risks

### Potential buffer overflow in `encode_iac()`

- **File:** `mux/src/net.cpp:198`
- **Issue:** The `encode_iac()` function copies data into a static `Buffer[2*LBUF_SIZE]` without checking if the doubled IAC characters will exceed the buffer size. If an input string contains many `NVT_IAC` (0xFF) characters, the `memcpy()` and `pBuffer += n` calls will overflow the static buffer.
- **Impact:** Memory corruption and potential crash during network output processing.
- **Recommendation:** Use `LBuf` RAII or add explicit bounds checks before `memcpy`.

### Widespread use of `static` buffers in functions

- **File:** Multiple files (see `grep` results for `static UTF8 ...[...LBUF_SIZE]`)
- **Issue:** Over 30 instances of `static UTF8 buffer[LBUF_SIZE]` or `2*LBUF_SIZE` were found in function scopes (e.g., `encode_iac`, `queue_string`, `Log.tinyprintf` wrappers).
- **Impact:** 
    - **Thread Safety:** These functions are not thread-safe. While the server is currently single-threaded for evaluation, this prevents future multi-threading and can cause subtle bugs if functions are ever called re-entrantly (e.g., via a signal or a nested evaluation that triggers another log/network call).
    - **Memory Safety:** Static buffers persist for the life of the process, increasing the "always-on" memory footprint unnecessarily.
- **Recommendation:** Migrate these to `LBuf` (stack-allocated via the engine's pool) or `std::string` where appropriate.
