# Campaign brief: the const-fargs flip (#2136 phases 2-4)

Branch: `feat/2136-const-fargs`.  **Phases 2-3 are COMPLETE: the branch
builds clean** — every violation the flip exposed has been classified and
converted (funceval, funceval2, functions, funmath, help, mail, session,
powers, levels, predicates, conf, walkdb, stringutil, timeutil/date_scan,
exp3, mux_main).  funcweb.cpp needed nothing.  Remaining work is the
verification battery below and phase 4.

**Trap found on the way (record for any future signature flip): an
old-signature definition does not fail the build — it becomes a C++
OVERLOAD.**  The table/callers reference the new-signature symbol, which
stays undefined, and nothing notices until `dlopen(RTLD_NOW)` — and only in
hosts that don't accidentally provide the symbol themselves.  `delim_check`
(functions.cpp), the three conn_bridge softcode bridges, the dbt_spike
stub, and exp3's `Call` were all silently shadowed this way; muxscript was
the only host that failed, because netmux's own net.cpp copy of
fun_siteinfo resolved the flat-namespace lookup.  After any signature
flip, grep for the OLD spelling (`UTF8 \*fargs\[\]`) — the compiler alone
will not find these.

## What is already done

- `FUNCTION`/`XFUNCTION`/`FUN::fun`/`delim_check` and the module interfaces
  (`mux_IFunction::Call`, `FunHost`/`FunDoing`/`FunSiteinfo`) take
  **`const UTF8 * const fargs[]`**.  Double-const is load-bearing: it makes
  the evaluator's `UTF8 *fargs[]` convert *implicitly* (C++ qualification
  conversion needs const at both levels), so builder/owner sites need zero
  casts — and slot reassignment inside bodies becomes a compile error too.
- Cleared: `mux/src/modules.cpp`, `mux/src/net.cpp` (via `lookup_player`
  const), `engine/player.cpp`, `engine/comsys.cpp` (via `select_channel`
  const — note the `const_cast` at its 3179 call site was *removed*, the
  flip deleting casts as it goes), `engine/ast.cpp` call site (implicit
  after double-const).
- New idiom in `functions.h`: **`FargCopy`** — RAII LBUF copy of one farg,
  `operator UTF8*`, for callees that legitimately scribble (see class 3).

## The four error classes and their recipes

1. **Read-only helper takes `UTF8 *`** (`lookup_player`, `select_channel`,
   `scan_zone`, `xlate`, `hasattr_handler`, ...): const-ify the parameter,
   chase the transitive errors inside it the same way.  This SPREADS the
   contract and is the most common class.  Verify read-only by reading the
   body — pointer *advancing* is fine, writes through are not.
2. **Genuine tokenizer of fargs** (`trim_space_sep(fargs[i], ...)` /
   `split_token` walks — 8 in funceval.cpp alone): the phase-2 findings.
   Convert with the established idioms: `list2arr_nd(...)` when it splits
   into an array, `trim_space_sep(list_copy_for_split(scratch, fargs[i]),
   sep)` with an `LBuf` for walks (see `fun_ledit` post-#2158 for the
   worked example).
3. **Command-handler wrapper** (`do_link`, `do_pemit_*`, `do_trigger`,
   `help_helper`, ...): handlers own and mutate command text by design;
   const-poisoning that layer trades one honest copy for hundreds of
   casts.  Wrap each farg at the call: `do_link(..., FargCopy(fargs[0]),
   FargCopy(fargs[1]), ...)`.  The temporary outlives the synchronous call.
4. **Slot reassignment** (`fargs[i] = x` — "read-only variable is not
   assignable"): the body wants scratch slots.  Give it a local
   `const UTF8 *args[N]` copy of the array (cheap, pointers only) or
   restructure; do NOT const_cast.

`const_cast` budget for the whole campaign: **zero** in function bodies.
The single permitted cast family is an *owner* freeing buffers it built
(`free_lbuf(const_cast<UTF8*>(p))`) if any owner ends up holding const
views; none has been needed so far.

## How to work it

Per-file syntax-only harvest (fast, complete — the real build stops at the
first failing TU):

    cd mux/modules/engine && g++ -std=c++17 -fsyntax-only -ferror-limit=400 \
      -I../../include -I../../ganl/include -I../../sqlite -I../../lua54 \
      -DWOD_REALMS -DREALITY_LVLS -DTINYMUX_JIT -DSSL_ENABLED \
      $(pkg-config --cflags-only-I openssl libpcre2-8) FILE.cpp 2>&1 | grep error:

Inventory: exhausted — the harvest reports zero errors on every TU and
`make install` is green.  Two idioms were added along the way and are now
available to future conversions:

- `trim_space_sep_n(str, sep, &len)` (functions.h) — non-destructive trim
  for (pointer, length) consumers; the co_* scanners all take (p, len), so
  most "trim then scan" sites need no copy at all.
- `FargVec` (functions.h) — the argv counterpart of FargCopy, for
  CS_ARGV-style handlers (do_trigger, do_verb) and fun_mix's list table.
- `countwords()` and `DecodeListOfIntegers()` were rewritten non-destructive
  (same tokenization, no trailing-NUL trim) and now take const.

Bonus deletions the flip paid for: #2157's fun_munge list1 copy (the walk
now tokenizes private copies, so %0 is the pristine farg), engine_com's
"help_helper expects a mutable topic" copy, five const_casts
(process_sex×4, sha1_helper), and fun_index's in-place NUL write.

## Verification battery (all of it, not a subset)

- `make test EXPECT_CONFIG="jit=yes"` — **DONE on macOS arm64**: 35 passed,
  0 failed, stubslave-teardown skipped (stubslave=no build).
- `make test-scenario` — **DONE**, including the new
  `tests/scenario/sidefx_fargs.py`, written for this campaign: live-object
  probes of the class-3 wrappers smoke deliberately never exercises
  (pemit/trigger/link/tel/wipe/destroy — trigger also covers FargVec argv).
- `make test-poison` (Linux) — **OUTSTANDING**: the past-count/garbage-read
  classes the copy conversions can introduce are invisible to a clean
  allocator; this is the suite built for exactly this campaign
  (#2149/#2150).  Phase 4 must not merge without a poisoned green run.

## The endgame (phase 4) — DONE, pending the Linux poison leg

`jit_compiler.cpp`'s #2135 ECALL argument copy, its `ptr < CARGS_BASE`
predicate (`guest_addr_outlives_ecall`), `ecall_arg_or_copy`, and
`jit_internal_abi_fn` are deleted — both ECALL paths hand guest memory to
callees directly, and the fargs arrays are `const UTF8 *` (which also
removed the mux_exec cast on the ufun path).  The fragile-predicate caveat
in run_cached_program now cites the const contract instead of the copy.
The #2128 reproduction is smoke TC013 (map_fn.mux, asserted 3/3/3 three
times precisely because twice cannot distinguish stable from
identically-corrupted).

**Do not merge to master until `make test-poison` is green on a Linux box**
— that suite (#2149/#2150) is the net built for exactly the bug classes
this campaign's copies could have introduced or removed.
