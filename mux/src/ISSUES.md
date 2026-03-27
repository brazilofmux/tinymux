# MUX Core Server (mux/src/) — Open Issues

Updated: 2026-03-27

## Critical — Buffer Overflows & Memory Safety

### Buffer overflow in `load_restart_db()` memcpy calls

- **File:** `net.cpp:3334, 3340, 3346`
- **Issue:** `memcpy()` copies `nBufferUnicode+1` bytes from `ConvertToUTF8()` into fixed-size descriptor fields without bounds checking:
  - `d->addr` is 51 bytes
  - `d->doing` is `SIZEOF_DOING_STRING`
  - `d->username` is 11 bytes
- **Risk:** Heap corruption if a corrupted `restart.db` produces oversized strings.

### Unvalidated array index from restart file

- **File:** `net.cpp:3121-3125`
- **Issue:** `num_main_game_ports = getref(f)` is read from untrusted file with no bounds check before use as loop count over `main_game_ports[]`.
- **Fix:** Validate `num_main_game_ports <= MAX_LISTEN_PORTS` before the loop.

### Missing null check after `getstring_noalloc()`

- **File:** `net.cpp:3222-3223`
- **Issue:** Return value assumed valid: `temp[0]` is dereferenced without a null check.
- **Risk:** Crash on corrupted restart file.

### Missing null check after `ConvertToUTF8()`

- **File:** `net.cpp:3333-3346`
- **Issue:** `pBufferUnicode` used in `memcpy()` without null check after conversion.

## Critical — Signal Handler Safety

### Non-async-signal-safe calls in `sighandler()`

- **File:** `signals.cpp:374-569`
- **Issue:** Signal handler calls logging functions (`log_text()`, `Flush()`), engine methods (`GetBCanRestart()`), and module teardown (`final_modules()`). These are not async-signal-safe per POSIX.
- **Risk:** Deadlock if signal arrives while a lock is held; data corruption from re-entrant heap operations.
- **Fix:** Signal handler should only set atomic flags; complex work should happen in the main loop.

### `g_shutdown_flag` is not volatile

- **File:** `bsd.cpp:35`
- **Issue:** `bool g_shutdown_flag = false` is written from `signals.cpp:476` (signal handler) and read from `ganl_adapter.cpp:1745` (main loop). Without `volatile` or `sig_atomic_t`, the compiler may cache the value.
- **Risk:** Shutdown signal silently ignored.

### `g_panicking` is not volatile

- **File:** `bsd.cpp:37`
- **Issue:** Same pattern — written in signal handler at `signals.cpp:335`, read at `signals.cpp:322`.

### `g_dump_child_pid` race condition

- **File:** `bsd.cpp:38`
- **Issue:** `volatile pid_t` but accessed without atomic operations between signal handler (`signals.cpp:431`) and main thread (`ganl_adapter.cpp:1884`).
- **Mitigation:** `volatile` provides visibility on most platforms but is technically not sufficient per C++ standard. Should use `sig_atomic_t` or `std::atomic`.

## High — Unchecked Return Values & Error Paths

### `getsockname()` failure crashes server

- **File:** `net.cpp:3127-3131`
- **Issue:** On failure, `mux_assert(0)` is called instead of graceful error handling.
- **Risk:** DoS via corrupted restart file.

### File not closed on invalid restart version

- **File:** `net.cpp:3144-3151`
- **Issue:** When restart file version is unrecognized, `mux_assert(0)` fires without closing the file handle first.

### `fgets()` return value unchecked

- **File:** `net.cpp:3108-3109`
- **Issue:** `fgets(buf, 3, f)` return not checked; uninitialized buffer fed to `strncmp()` assertion on next line.

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

### Volatile counters in slave.cpp lack atomicity

- **File:** `slave.cpp:151-153, 165, 253-254`
- **Issue:** `volatile int nChildrenStarted/EndedSIGCHLD/EndedMain` use `++` from signal handler — compound operations are not atomic.

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
