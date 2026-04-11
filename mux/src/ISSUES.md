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

### ~~Data loss in `Stub_PipePump` on short writes or errors~~ FIXED
- **File:** `mux/src/stubslave.cpp:32-100`
- `Stub_PipePump` now drains each dequeued block with a retry loop around
  `write(1, ...)`, correctly handling short writes and `EAGAIN`/`EWOULDBLOCK`
  instead of dropping bytes after `Pipe_GetBytes()` removes them from
  `Queue_Out`.

## High — Buffer Overflows & Memory Safety (New, 2026-04-10)

### ~~1-2 byte buffer overflow in `slave.cpp` query processing~~ FIXED
- **File:** `mux/src/slave.cpp:115-120`
- `buf` is now sized `MAX_STRING * 2 + 3`, matching the worst-case
  write of `ip (≤999) + ' ' + pHName (≤999) + '\n' + '\0'` =
  2001 bytes. Previously sized at `MAX_STRING * 2` = 2000 bytes,
  a hostname-plus-IP pair of the maximum length overran by one
  byte on the helper process stack.

## High — Protocol Safety & Limits (New, 2026-04-10)

### ~~`SBUF_SIZE` (64) is too small for modern telnet sequences (TTYPE, GMCP)~~ FIXED
- **File:** `mux/include/interface.h`, `mux/src/telnet.cpp`
- The telnet subnegotiation accumulator is now sized by a dedicated
  `TELNET_OPTION_SIZE` constant (4096) in `interface.h`, decoupled
  from `SBUF_SIZE`. Previously `d->aOption[SBUF_SIZE]` (64 bytes)
  silently truncated any GMCP JSON, MSDP, long TTYPE chain, or
  CHARSET list exceeding that limit, breaking protocol features
  for feature-rich clients. Bumping the pool-allocator `SBUF_SIZE`
  directly would have affected dozens of unrelated sites; only the
  descriptor accumulator needed to grow.

## ~~High — Network Address Parsing~~ (2026-04-10)

### ~~Undefined Behavior in IPv4 decoding~~ FIXED
- **File:** `mux/src/netaddr.cpp:67`
- `DecodeN` now promotes `*pu32` to `uint64_t` before shifting by
  `decode_IPv4_table[nType].nShift`. For `nType=3` (single-element IPv4
  like `12345678`), `nShift` is 32 and shifting a 32-bit value by its
  own width was undefined behavior. Shifting a `uint64_t` by 32 is
  well-defined; the result is masked back to 32 bits.

### ~~Broken overflow check in decimal IPv4 parsing~~ FIXED
- **File:** `mux/src/netaddr.cpp:189`
- Decimal accumulation now runs in `uint64_t` and is bounded against
  `0xFFFFFFFFUL` after each digit. The previous 32-bit
  `ul = (ul * 10) & 0xFFFFFFFFUL` followed by `if (ul < ul2)` could
  coincidentally produce a post-wrap value larger than the pre-wrap
  value (e.g., `500,000,000 * 10` wraps to `705,032,704`, which is
  greater than 500,000,000), silently accepting decimal components
  above 2^32 - 1.

## ~~Critical — Completely Broken Hex IPv4 Parsing~~ FIXED (2026-04-10)

### ~~`DecodeN` subtracts wrong offset from hex digits~~ FIXED
- **File:** `mux/src/netaddr.cpp:109-116`
- The hexadecimal branch now decodes `A-F`/`a-f` as `ch - 'A' + 10` and
  `ch - 'a' + 10`. Previously the `+ 10` was missing, so `A`..`F` mapped to
  nibbles 0..5 and every hex IPv4 literal in `@site`/`@admit`/`@nosite`
  rules silently matched the wrong address range.

## High — WebSocket Protocol Handling (New, 2026-04-10)

### ~~Truncating `static_cast<size_t>` on snprintf return~~ FIXED
- **File:** `mux/src/websocket.cpp:270-277`
- The 101 Switching Protocols send site now validates `snprintf`
  returned `n >= 0 && n < sizeof(response)` before casting to
  `size_t`. On any anomaly (encoding error or truncation) the
  function sends a `500 Internal Server Error` rejection and
  returns, matching the existing handshake-failure pattern. This
  can't happen in practice given the fixed format and 28-character
  accept key, but the previous code would have read past the stack
  buffer on a negative `snprintf` return.

### ~~64-bit WebSocket frame length silently truncates large payloads~~ FIXED
- **File:** `mux/src/websocket.cpp:342-351`
- `ws_queue_frame` now writes all 8 bytes of the RFC 6455 §5.2
  extended length, promoting `len` to `uint64_t` so the high-word
  shifts are well-defined regardless of `size_t` width. Previously
  bytes 2-5 were hard-coded to zero, silently dropping the top 32
  bits of any payload exceeding 4 GiB on 64-bit builds.

### ~~Control frames not validated against RFC 6455 §5.5~~ FIXED
- **File:** `mux/src/websocket.cpp:432-456`
- `ws_process_input` now rejects control frames (opcodes 0x8-0xF)
  with `FIN=0` or `lenByte >= 126` in `WS_PARSE_HEADER1`, before
  entering any extended-length or payload state. A fragmented PING
  or an oversized CLOSE used to pass through unchecked; a 2 MiB
  CLOSE payload was echoed back verbatim via `ws_queue_frame`,
  amplifying the attacker's bandwidth on the server side.

### ~~Fragment assembly buffer is unbounded~~ FIXED
- **File:** `mux/src/websocket.cpp` (text/binary final-fragment and
  continuation cases)
- Every `frag_buf.append(frame_buf)` site now checks that the
  assembled size will stay within `WS_MAX_PAYLOAD`. On violation
  the fragmentation state is cleared and a close frame with code
  `1009` (`WS_CLOSE_MESSAGE_TOO_BIG`, newly added in `websocket.h`)
  is sent. Previously a client could push thousands of small
  non-FIN fragments and grow `frag_buf` without bound — a memory-
  exhaustion DoS from a single connection.

### ~~Dead `op` local after continuation lookup~~ FIXED
- **File:** `mux/src/websocket.cpp` dispatch site
- The dead `op = ws->frame_opcode; if (op == CONTINUATION) op = frag_opcode;`
  block was removed. The switch already dispatches on `ws->frame_opcode`
  directly; the CONTINUATION case appends to `frag_buf` and flushes
  on FIN, and `save_command` does not distinguish text from binary,
  so no frag-opcode substitution is needed. Pure dead-code cleanup.

## High — Platform Abstraction Issues (New, 2026-04-10)

### ~~`PanicRestart` reads undefined `argv[]` slots~~ FIXED
- **File:** `mux/src/platform.cpp:313-370`
- `PanicRestart` now materializes a NULL-terminated local argv
  bounded by the declared `argc` and invokes `execv`. Previously
  `UNUSED_PARAMETER(argc)` plus seven hand-coded `execl` slots read
  `argv[0..6]` unconditionally — uninitialized/OOB memory whenever
  a caller passed fewer than seven args, and silently dropped
  anything past index 6. `argc` is bounded at 16 slots, the
  per-arg read runs before any signal-unsafe work (validating
  `execPath`/`argv` and per-slot non-null), and `execv` is on the
  POSIX.1-2008 async-signal-safe list so the crash-recovery context
  is preserved. `std::vector` is deliberately avoided here because
  heap allocation is not async-signal-safe.

### ~~`MaximizeFileDescriptors` ignores `setrlimit` failure~~ FIXED
- **File:** `mux/src/platform.cpp:292-330`
- After the setrlimit call, the function now re-reads the actual
  kernel-enforced `rlim_cur` via `getrlimit` and reports that to
  the caller. Previously the post-set `rlp.rlim_cur` reflected the
  *desired* value regardless of whether the raise succeeded, so
  `select()`/`poll()`/`epoll` sizing could outrun the real ceiling.
  Also added `INT_MAX` saturation because `rlim_t` (often 64-bit)
  can exceed the `int` return type on modern systems with an
  unlimited nofile limit.

### ~~`BootHelperProcess` close-all loop is O(rlim) on modern systems~~ FIXED
- **File:** `mux/src/platform.cpp:202-234`
- The post-fork close-all step now uses `close_range(3, ~0U, 0)`
  on glibc ≥ 2.34 (Linux 5.9+ wrapped) or `closefrom(3)` on BSDs
  and Solaris — each a single syscall instead of O(rlim)
  `mux_close()` calls. The linear loop is kept as a fallback for
  systems without either API. `maxfds` computation was pushed
  into the fallback branch so the fast paths don't carry a dead
  store.

### Non-atomic `CPlatform::m_cRef` / `CPlatformFactory::m_cRef`
- **File:** `mux/src/platform.cpp:80-94, 412-427`
- **Issue:** `AddRef`/`Release` still use bare `uint32_t m_cRef` with `m_cRef++` / `m_cRef--` — the same race pattern that was already converted to `std::atomic<uint32_t>` in comsys/mail modules. Low priority today (driver is single-threaded), but for consistency with the engine-module fix it should be atomic.
