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

## Critical — Protocol Logic & Data Integrity (New, 2026-04-10)

### Data loss in `Stub_PipePump` on short writes or errors
- **File:** `mux/src/stubslave.cpp:32-100`
- **Issue:** `Stub_PipePump` removes bytes from `Queue_Out` into a local `arg` buffer. If the subsequent `write(1, ...)` fails with a non-retryable error (or `EINTR`, which is currently unhandled for `write`), those bytes are lost forever.
- **Impact:** Corruption of inter-process communication between `netmux` and `stubslave`.

## High — Buffer Overflows & Memory Safety (New, 2026-04-10)

### 1-2 byte buffer overflow in `slave.cpp` query processing
- **File:** `mux/src/slave.cpp:115-120`
- **Issue:** `char buf[MAX_STRING * 2]` is 2000 bytes. Concatenating an IP address (up to 999 chars) and a hostname (up to 999 chars) with a space, newline, and null terminator can reach 2001 bytes, overflowing `buf` by 1 byte.
- **Impact:** Potential crash or memory corruption in the helper `slave` process.

## High — Protocol Safety & Limits (New, 2026-04-10)

### `SBUF_SIZE` (64) is too small for modern telnet sequences (TTYPE, GMCP)
- **File:** `mux/include/alloc.h`, `mux/src/telnet.cpp`
- **Issue:** `d->aOption` is a fixed 64-byte buffer. Modern clients frequently send TTYPE strings or GMCP JSON payloads exceeding this limit, leading to silent truncation and broken protocol features.
- **Impact:** Broken GMCP and TTYPE support for feature-rich clients.

## High — Network Address Parsing (New, 2026-04-10)

### Undefined Behavior in IPv4 decoding
- **File:** `mux/src/netaddr.cpp:69`
- **Issue:** `DecodeN` shifts `*pu32` (a 32-bit `in_addr_t`) by `decode_IPv4_table[nType].nShift` bits. For `nType=3` (single-element IPv4 address like `12345678`), `nShift` is 32. Shifting a 32-bit value by 32 bits is undefined behavior in C++.
- **Impact:** Unpredictable IPv4 parsing results for single-number address formats.

### Broken overflow check in decimal IPv4 parsing
- **File:** `mux/src/netaddr.cpp:189`
- **Issue:** The overflow check `if (ul < ul2)` after `ul = (ul * 10) & 0xFFFFFFFFUL` is insufficient. A 32-bit multiply-by-10 can wrap around multiple times or wrap to a value larger than the original (e.g., `500,000,000 * 10` wraps to `705,032,704`, which is `> 500,000,000`).
- **Impact:** Acceptance of invalid, overflowing IPv4 address components.

## ~~Critical — Completely Broken Hex IPv4 Parsing~~ FIXED (2026-04-10)

### ~~`DecodeN` subtracts wrong offset from hex digits~~ FIXED
- **File:** `mux/src/netaddr.cpp:109-116`
- The hexadecimal branch now decodes `A-F`/`a-f` as `ch - 'A' + 10` and
  `ch - 'a' + 10`. Previously the `+ 10` was missing, so `A`..`F` mapped to
  nibbles 0..5 and every hex IPv4 literal in `@site`/`@admit`/`@nosite`
  rules silently matched the wrong address range.

## High — WebSocket Protocol Handling (New, 2026-04-10)

### Truncating `static_cast<size_t>` on snprintf return
- **File:** `mux/src/websocket.cpp:270-277`
- **Issue:** `int n = snprintf(response, sizeof(response), ... );` then `queue_write_LEN(d, ..., static_cast<size_t>(n));`. No check that `n >= 0` or `n < sizeof(response)`. A negative `snprintf` return (encoding error) is cast to a huge `size_t`, leading to a massive out-of-bounds read from the stack `response[256]` buffer. A truncated return (`n == 256`) is accepted silently with unterminated output.

### 64-bit WebSocket frame length silently truncates large payloads
- **File:** `mux/src/websocket.cpp:342-351`
- **Issue:** For `len > 65535`, `ws_queue_frame` writes the payload length as an 8-byte big-endian field, but bytes 2-5 are hard-coded to zero:
  ```cpp
  hdr[2] = 0; hdr[3] = 0; hdr[4] = 0; hdr[5] = 0;
  hdr[6] = static_cast<uint8_t>((len >> 24) & 0xFF);
  ```
  On a 64-bit build, any server-side frame exceeding 4 GiB would silently drop the top 32 bits. This is not exploitable today (LBUF-bounded content), but it is incorrect, and it will bite the moment MUX sends a very large WebSocket payload.

### Control frames not validated against RFC 6455 §5.5
- **File:** `mux/src/websocket.cpp:521-595`
- **Issue:** Per RFC 6455 §5.5, control frames (PING/PONG/CLOSE) must have `FIN=1` and payload length ≤ 125. `ws_process_input` never enforces either. A fragmented control frame (`FIN=0`) is silently accepted into the continuation-fragment buffer and then mis-dispatched; a 2-MiB CLOSE payload is echoed back verbatim via `ws_queue_frame`, amplifying the frame server-side.
- **Impact:** Protocol violation, potential DoS amplification, and broken fragmentation state after an adversarial control frame.

### Fragment assembly buffer is unbounded
- **File:** `mux/src/websocket.cpp:530-553, 557`
- **Issue:** `ws->frag_buf.append(ws->frame_buf)` has no cap. Each individual frame is capped at `WS_MAX_PAYLOAD` (when the client sends a 64-bit length), but a client can send thousands of small non-FIN fragments and push `frag_buf` arbitrarily large.
- **Impact:** Memory-exhaustion DoS via repeated fragments from a single connected WebSocket client.

### Dead `op` local after continuation lookup
- **File:** `mux/src/websocket.cpp:515-521`
- **Issue:** `uint8_t op = ws->frame_opcode; if (op == WS_OPCODE_CONTINUATION) op = ws->frag_opcode;` computes `op`, then the switch immediately below switches on `ws->frame_opcode` (not `op`). The local is dead. Harmless but suggests the continuation dispatch was intended to reuse the fragment opcode — as written, continuation text/binary frames land in the `WS_OPCODE_CONTINUATION` case and behave correctly only by accident.

## High — Platform Abstraction Issues (New, 2026-04-10)

### `PanicRestart` reads undefined `argv[]` slots
- **File:** `mux/src/platform.cpp:332-340`
- **Issue:** `execl(execPath, argv[0], argv[1], ..., argv[6], nullptr)` unconditionally dereferences `argv[0..6]` regardless of `argc`. If the caller passes fewer than 7 saved args, the function reads uninitialized/OOB memory during a crash-recovery path — exactly when the process is already in a bad state. Argv beyond index 6 is silently dropped.
- **Fix:** Use `execv(execPath, argv)` with a proper NULL-terminated vector, or switch on `argc`.

### `MaximizeFileDescriptors` ignores `setrlimit` failure
- **File:** `mux/src/platform.cpp:296-304`
- **Issue:** `setrlimit(RLIMIT_NOFILE, &rlp)` return value is discarded. On failure, `*pLimit` still reports the desired `rlim_max`, which the rest of the server then treats as authoritative. `select()`/`poll()`/`epoll` sizing can outrun the actual descriptor ceiling.

### `BootHelperProcess` close-all loop is O(rlim) on modern systems
- **File:** `mux/src/platform.cpp:212-215`
- **Issue:** `for (int i = 3; i < maxfds; i++) mux_close(i);` after `fork()`. On systems with `ulimit -n` raised to 1M+, the child spends noticeable wall time closing nonexistent fds before `execlp`. Prefer `closefrom(3)` (BSD/Linux 5.11+) or `FD_CLOEXEC` on known fds.

### Non-atomic `CPlatform::m_cRef` / `CPlatformFactory::m_cRef`
- **File:** `mux/src/platform.cpp:80-94, 412-427`
- **Issue:** `AddRef`/`Release` still use bare `uint32_t m_cRef` with `m_cRef++` / `m_cRef--` — the same race pattern that was already converted to `std::atomic<uint32_t>` in comsys/mail modules. Low priority today (driver is single-threaded), but for consistency with the engine-module fix it should be atomic.

