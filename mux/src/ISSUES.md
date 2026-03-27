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

### `g_dump_child_pid` race condition

- **File:** `bsd.cpp:38`
- **Issue:** `volatile pid_t` but `pid_t` is not guaranteed to be `sig_atomic_t`-sized. Works in practice on Linux/BSD but not strictly portable.

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

### `ISOUTOFMEMORY` macro terminates instead of recovering

- **File:** `net.cpp:3225-3226` and multiple other sites
- **Issue:** OOM after `MEMALLOC()` triggers `mux_assert`-style abort. A long-running server may prefer to gracefully disconnect the session.

### Inconsistent error handling patterns

- **File:** `net.cpp` (throughout `load_restart_db()`)
- **Issue:** Mix of `mux_assert(0)`, return codes, and silent fallthrough makes the code unpredictable under corruption.

### Integer overflow potential in buffer calculations

- **File:** `net.cpp:3225-3227, 3262, 3309, 3323, 3346`
- **Issue:** `nBuffer+1` or `nBufferUnicode+1` could overflow if the value read from file is `SIZE_MAX`.

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
