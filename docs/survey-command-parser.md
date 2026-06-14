# Survey: command parser/dispatcher (command.cpp)

Audit of the command parser in `mux/modules/engine/command.cpp` — the second of
the three parsers the boolexp header names (functions = eval.cpp, commands =
command.cpp, locks = boolexp.cpp). It processes every line of player input:
`process_command` (decompose → match → dispatch) and `process_cmdent` (permission
+ argument parsing + handler call). Methodology matches the boolexp/wild/JIT
campaign. **Result: CLEAN — no memory-safety or unbounded-recursion bug found.**

## Why it's well-hardened (unlike boolexp's parser)

**Recursion is structurally bounded.** The boolexp DoS (#839) was unbounded
parser recursion reachable by any player. command.cpp has no equivalent:
- Regular commands in action lists are **queued** (`wait_que` → bounded by the
  command-queue cycle limits), not run on the C stack.
- The only direct stack-recursion paths — `@assert/inline`, `@break/inline`,
  `@lua/inline` (`process_command`/Eval called directly) — are **`CA_WIZARD`**
  (the `/inline` switch is wizard-only; `@lua/inline` re-checks `Wizard`), and
  even then bounded by the LBUF command-string length (~2048 levels of
  `@break/inline 1=…`; verified 500 survives, longer chains truncate at the LBUF
  input cap, rc=0, no crash).
- `@train` has its own `train_nest_lev` guard.
- Function/`[u()]` nesting inside commands is bounded by `func_nest_lim` /
  `func_invk_lim` (reset per top-level command) and the AST evaluator's
  `nStackNest`/`bStackLimitReached` spam guard (incremented in ast.cpp).

**Buffers are bounded.**
- `process_command`'s working buffers (`preserve_cmd`, `SpaceCompressCommand`,
  `LowerCaseCommand`) are `thread_local UTF8[LBUF_SIZE]` — not stack arrays, so
  deep call chains don't balloon the stack.
- The space-compress loop and command-name extraction use the loose
  `q < buf + LBUF_SIZE` guard (off-by-one vs `-1`, like boolexp's object-ref
  loop) but are **not exploitable**: command input is always NUL-terminated
  within `LBUF_SIZE-1`, so the loop stops on the NUL before `q` reaches the end.
  Verified: a 32760-char single-token command and a max-length spaced command
  both survive (no OOB, rc=0).
- Fixed buffers all use bounded fills: `switch_buff[200]` (`mux_strncpy`),
  `pkg[SBUF_SIZE]` in `handle_gmcp` (`nPkg` clamped), `result[8000]` in `do_lua`
  (`sizeof(result)` passed to the Lua control), `check2[UTF8_SIZE4+1]`,
  `qbuff[I64BUF_SIZE]`.

**Argument parsing delegates to bounded helpers.** `process_cmdent` splits args
via `parse_to` / `parse_arglist(..., args, MAX_ARG, ..., &nargs)` — the shared,
`MAX_ARG`-bounded splitters — into `alloc_lbuf` buffers that are freed on every
path. `CS_*` calling-sequence handling is table-driven.

## Surfaces examined

`process_command` (decompose, prefix/single-letter leadins, `@icmd`/hook checks,
switch stripping), `process_cmdent` (perms, switch loop, `CS_NO_ARG`/`CS_ONE_ARG`/
`CS_TWO_ARG`/`CS_ARGV` arg parsing, hooks), `handle_gmcp` (telnet GMCP → A_GMCP),
`do_lua`, `do_assert`/`do_break`/`do_train` (recursion paths), `is_prefix_cmd`,
`cmdtest`/`zonecmdtest`. All bounded or wizard-gated. No fix needed.
