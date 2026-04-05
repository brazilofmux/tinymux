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

### ~~Commented-out debug logging~~ FIXED

- **File:** `ganl_adapter.cpp`
- Deleted eight stale `//GANL_CONN_DEBUG(...)` lines (close-notify, auth-success, event-receive warning, send-on-unmapped, close-on-unmapped, handle/DESC mapping/unmapping, and unknown-DESC remove). Three of them were the entire body of an `else` branch, so the empty `else { }` blocks were collapsed along with the comments. Verified with `g++ -std=c++17 -fsyntax-only` on `ganl_adapter.cpp`.

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

### ~~Missing null check after `alloc_lbuf()` for output_prefix/output_suffix~~ FIXED

- **File:** `mux/src/net.cpp:3381-3383, 3400-3402`
- Restart loading now aborts gracefully with `mux_fclose(f); g_restarting = false; return;` if either `alloc_lbuf("set_userstring")` call fails before copying the restored prefix/suffix.

### ~~Missing null check after `alloc_lbuf()` for raw_input_buf~~ FIXED

- **File:** `mux/src/telnet.cpp:606-607`
- `process_input_helper()` now returns early if `alloc_lbuf("process_input.raw")` fails, avoiding a null dereference on `d->raw_input_buf`.

### ~~Missing null check after `MEMALLOC()` for ttype~~ FIXED

- **File:** `mux/src/telnet.cpp:1095-1096, 1487-1488`
- The TTYPE and GMCP handlers now skip the `memcpy()` when `MEMALLOC()` fails, leaving `d->ttype` null instead of dereferencing it.

## High — Buffer Safety & Static Buffer Risks

### ~~Potential buffer overflow in `encode_iac()`~~ FIXED

- **File:** `mux/src/net.cpp:198`
- `encode_iac()` now builds a dynamically sized `std::string` and `queue_string()` writes it with `queue_write_LEN()`, so telnet IAC doubling no longer depends on a fixed `2*LBUF_SIZE` scratch buffer.

### ~~Widespread use of `static` buffers in functions~~ FIXED

- **Files:** `mux/src/` (5 sites in `net.cpp`, `signals.cpp`, `stubslave.cpp`) and `mux/modules/` (38 sites across 18 files in engine + mail modules).
- All 43 `static` scratch-buffer arrays (`UTF8 buf[...]`, `char buf[...]`, `uint8_t arg[...]`) converted to `thread_local`. This is a one-word, zero-allocation, zero-behavior-change swap under the current single-threaded evaluator: `thread_local` storage has the same lifetime and performance as `static`, but each thread gets its own copy, so these functions become safe for future multi-threaded evaluation without any locking. Read-only constant tables (`aRadix64`, `aRadixPenn36`, `aRadixPenn64`, `Empty`) were intentionally left as `static` since they are immutable shared data.  All 21 modified `.cpp` files verified with `g++ -std=c++17 -fsyntax-only`.
