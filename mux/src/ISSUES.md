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

### Non-async-signal-safe calls in `sighandler()`

- **File:** `signals.cpp:374-569`
- **Issue:** Signal handler calls logging functions (`log_text()`, `Flush()`), engine methods (`GetBCanRestart()`), and module teardown (`final_modules()`). These are not async-signal-safe per POSIX.
- **Risk:** Deadlock if signal arrives while a lock is held; data corruption from re-entrant heap operations.
- **Fix:** Signal handler should only set atomic flags; complex work should happen in the main loop. This is a larger refactor deferred for now.

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

### Unvalidated enum/range values from restart file

- **File:** `net.cpp:3192-3193, 3218-3219, 3230, 3242-3243`
- **Issue:** `getref(f)` values used directly for `d->height`, `d->width`, `d->encoding` without range validation.

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

### Telnet state array magic number

- **File:** `net.cpp:3077-3080, 3197-3204, 3212-3216`
- **Issue:** Hard-coded loop bound `256` must match `nvt_him_state[]`/`nvt_us_state[]` array sizes. No `static_assert` or named constant ties them together.
