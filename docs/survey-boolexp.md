# Survey: lock parser/evaluator (boolexp.cpp)

Audit of `mux/modules/engine/boolexp.cpp` — the `@lock` boolean-expression parser
and evaluator. Security-critical: locks gate access (enter/use/pick-up/leave/…),
parse arbitrary player `@lock` input, and evaluate recursively (indirect locks
read other objects' locks; eval locks run softcode). Methodology matches the
JIT/wild campaigns: find memory-safety / DoS / divergence, verify empirically,
fix, regression-test.

## Structure

- `parse_boolexp_E/T/F/L` — recursive-descent parser (E=OR, T=AND, F=NOT/`@`/`=`/
  `+`/`$`, L=`(E)` or object/attr leaf). Entry `parse_boolexp()`.
- `eval_boolexp()` — recursive evaluator (AND/OR/NOT, CONST, ATR, EVAL, IS,
  CARRY, OWNER, INDIR).
- `eval_boolexp_atr()` — parse + eval a stored lock key.
- `check_attr()` — attribute-lock check (uses `wild_match`).

## Finding — parser stack-overflow DoS (FIXED, #839)

`parse_boolexp_*` are mutually recursive on attacker-controlled input with **no
depth limit**. `mudconf.lock_nest_lim` bounds only the *evaluator's* indirect-lock
recursion (`BOOLEXP_INDIR`), not the parser. So `@lock me=!!!!…!#1` overflows the
C stack and SIGSEGVs the server — reproduced at ~400 `!` (a ~400-byte string).
Any player can `@lock` an object they own → trivially reachable DoS.

The threshold is low because `parse_boolexp_F` stack-allocated
`char objbuf[LBUF_SIZE]` (32 KB) for the `@obj/lockname` path, reserved for the
whole frame — ~32 KB per recursion level.

**Fix:** an RAII `ParseDepthGuard` increments a `thread_local` counter at the top
of `parse_boolexp_F` (every recursion cycle — `F→F`, `T→F`, `E→T→F`, `L→E→T→F` —
passes through it), capped at `LOCK_PARSE_MAX_DEPTH = 1024`; exceeding it returns
`TRUE_BOOLEXP` (the sentinel the parser already returns for malformed locks). Also
moved `objbuf` off the stack (heap LBUF) so frames are small and the cap has
margin (~1 MB at 1024). Verified SIGSEGV-before / survives-after across depths
500…32000 and the 1023/1024/1025 boundary; `lock_fn.mux` TC004 regression; smoke
1255/1255.

## Surfaces checked — clean

- **Object-ref fill loop** (`parse_boolexp_L` default case): the guard
  `p < buf.get() + LBUF_SIZE` is loose (off-by-one vs `- 1`), but **not
  exploitable** — `parsestore`/`objbuf` are always NUL-terminated within
  `LBUF_SIZE-1`, so the loop stops on the NUL before `p` reaches `buf+LBUF_SIZE`;
  `*p-- = '\0'` stays in bounds. Verified a 32767-char object ref doesn't OOB.
- **Evaluator recursion**: AND/OR/NOT recurse to the parse-tree depth, now bounded
  by the parser's 1024 cap; `eval_boolexp` frames are small → safe.
- **Indirect locks** (`BOOLEXP_INDIR`): guarded by `lock_nest_lim` (cycle/recursion
  protection) with proper increment/decrement and a bad-indirection check.
- **Eval locks** (`BOOLEXP_EVAL`): run softcode via `mux_exec` as the lock source
  with `bCanReadAttr`/`AF_NOEVAL` checks, `alarm_clock.alarmed` timeout bail, and
  save/restore of global registers — standard MUX eval-lock handling.
- **Attr-key type punning** (`reinterpret_cast<UTF8*>(b->sub1)`): intentional
  union-style reuse; the key is `StringClone`d in `test_atr` and freed by
  `free_boolexp`.

## Finding — oversize key missing NUL after truncate (FIXED)

`parse_boolexp` capped `n` to `sizeof(parsestore)-1` then `memcpy(..., n+1)`.
When the source was longer than the cap, that copied a full buffer of payload
bytes with **no** terminating NUL; the recursive-descent scanner could walk past
`parsestore`. Production set/eval paths usually feed LBUF-bounded strings, but
the truncate path was still wrong.

**Fix:** `memcpy(parsestore, buf, n); parsestore[n] = '\0';`

## Residual (by design / not defect)

- **Empty / unparseable stored key → open** (`eval_boolexp_atr` treats
  `TRUE_BOOLEXP` as true). Empty unlocked is intentional; corrupt keys that
  re-parse as open are the historical MUSH fail-open. `@lock` / `lock()` reject
  bad keys at set time; `AF_IS_LOCK` blocks ordinary `&` attribute sets.
  `IObjectInfo::AtrAddRaw` can still write raw lock text (trusted modules only).
- **Eval locks run softcode** (`BOOLEXP_EVAL`) with register save/restore and
  `bCanReadAttr` / `AF_NOEVAL` gates — intentional side-effect surface.
