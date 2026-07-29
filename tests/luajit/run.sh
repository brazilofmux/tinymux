#!/bin/bash
#
#   run.sh — Lua JIT differential harness (#1423, #1426, #1512).
#
#   Three contracts, three case lists:
#
#   SURVIVE_CASES  — process must not die.  Result mismatch is *reported*
#                    (often table/bridge bugs still open) but does not fail.
#
#   AGREE_CASES    — result must match the interpreter.  Decline is OK:
#                    interpreter answers.  Used for named-bridge shapes where
#                    fail-closed decline is the correct compile-path outcome
#                    until the bridge is implemented (#1512).
#
#   EXEC_CASES     — result must match AND lua_run_ok must advance on the
#                    jit path.  A decline that still "agrees" is a FAILURE:
#                    that is the vacuity #1426 named.  These chunks avoid
#                    globals/stdlib so they can compile without the Lua-VM
#                    bridge ECALLs.
#
#   NESTED_CASES   — the EXEC contract with jit_eval_brackets ON, the
#                    production default.  Until #1326 the Lua JIT could not
#                    run there at all: fun_lua is ECALLed from a DBT program
#                    and run_cached_program refused the nested run, so every
#                    chunk fell back to the VM and every result still matched.
#                    Nothing covered that path, which is why it stayed broken.
#
#   Config: before #1326, jit_eval_brackets 0 was required for the compiled
#   Lua path to execute at all.  It is still what EXEC uses, to isolate
#   lowering from nesting; NESTED is the tier that holds the default config.
#
#   DECLINE BUDGET: AGREE allows a decline, because the interpreter answering
#   is correct behaviour.  That makes 32-of-34-declining and 0-of-34-declining
#   the same colour, so the count is ratcheted below rather than left to be
#   eyeballed: it may fall, never rise.
#
#   Deliberately not smoke: smoke cannot set that config without changing
#   the softcode-JIT surface, and result-only smoke is green when the
#   compiled path runs nothing.
#
set -u

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
BIN="$REPO_ROOT/mux/game/bin"
WORK="$SCRIPT_DIR/work"

if command -v timeout >/dev/null 2>&1; then
    TIMEOUT="timeout 60"
elif command -v gtimeout >/dev/null 2>&1; then
    TIMEOUT="gtimeout 60"
else
    TIMEOUT=""
fi

if [ ! -x "$BIN/muxscript" ]; then
    echo "SKIP: $BIN/muxscript not found (run 'make install' first)."
    exit 0
fi

# ---------------------------------------------------------------------------
# SURVIVE — table ops and anything that has historically aborted (#1423).
# Result divergence is reported, not fatal.
#
# As of #1518 these also happen to *agree* with the interpreter -- but by
# declining, not by lowering correctly.  The four #1424 shapes are therefore
# also listed in AGREE_CASES below, where agreement is enforced; see the note
# there (#1557).
# ---------------------------------------------------------------------------
SURVIVE_CASES=(
    'local t={10,20,30} return t[2]'
    'local t={a=7} return t.a'
    'local t={} t[1]=5 return t[1]'
    'local t={} t.x=5 return t.x'
    'local t={1,2,3} return #t'
    'local t={1,2,3} table.insert(t,4) return #t'
    'local t={10,20,30} return t[1]+t[3]'
    'local t={} for i=1,3 do t[i]=i*10 end return t[2]'
)

# ---------------------------------------------------------------------------
# AGREE — must match interpreter; decline OK (bridge / not-yet-lowered).
# Prefer `local x=f(...) return x` over `return f(...)` (OP_TAILCALL declines).
# ---------------------------------------------------------------------------
AGREE_CASES=(
    'local x=tostring(42) return x'
    'local x=type(42) return x'
    'local x=tonumber("17") return x'
    'local x=math.floor(3.7) return x'
    'local x=math.max(3,9) return x'
    'local x=math.min(3,9) return x'
    'local x=string.upper("ab") return x'
    'local x=string.lower("AB") return x'
    'local x=string.sub("hello",2,3) return x'
    'local x=string.format("%d",7) return x'
    'local x=string.len("hello") return x'
    'local x=table.concat({1,2,3},",") return x'
    'local x=select("#",1,2,3) return x'
    'local x=math.pi return x'
    'local x=math.maxinteger return x'
    'local x=math.abs(-7) return x'
    'local x=tonumber(mux.args[1]) return x'
    'local x=tonumber(mux.args[1]) return x+1'
    'local x=tonumber(mux.args[1]) return x*2'
    'local x=math.floor(3.7) return x+1'
    'local x=string.len("hello") return x*2'
    'return math.huge'
    'return os.time()>0'
    # Runtime ^ used to answer pow(0,0)==1 here (#1538).  The strict form of
    # both chunks now lives in EXEC: once #1561 loads tier2 on the Lua path
    # they compile and run, so "agreement, decline allowed" is strictly
    # weaker than what EXEC already pins.
    'local a=mux.args[1]+0 return (a ^ 2) == 4'
    'local a=mux.args[1]+0 local b=mux.args[2]+0 return (a ^ b) == 8'
    # All three loop forms EXECUTE under the back-edge budget (#1732);
    # none may invent a wrong answer.
    'local s=0 for i=1,4 do s=s+i end return s'
    'local i=0 repeat i=i+1 until i>=5 return i'
    # Budget exhaustion: compiled path hits ECALL_LUA_LIMITED and must
    # surface the same "#-1 LUA ERROR: instruction limit exceeded" as the
    # interpreter (Phase 3 / #1732).  No re-run, no POST-ENTRY DECLINE.
    'local s=0 for i=1,100000000 do s=s+1 end return s'
    # ---- #1424's four shapes, whose symptoms #1518 closed (#1557) ----
    #
    # Used to return stack-index garbage (22) or abort.  Now agree by
    # declining / run-fail, not by correct lowering (#1519).  Fatal here so
    # containment cannot regress silently.  Re-measure lua_run_ok when the
    # bridge is brought up -- green alone does not mean they execute.
    'local t={1,2,3} return #t'
    'local t={1,2,3} table.insert(t,4) return #t'
    'local t={10,20,30} return t[2]'
    'local t={a=7} return t.a'
    # ---- math.type: the subtype as Lua reports it (#1488) ----
    #
    # The only check that the subtype is right *inside* Lua rather than
    # just in the rendered result, and the case that showed `^` was float
    # all along and the "8" came from the constant fold.  Floats now travel
    # as call arguments (raw bits over the FMV.X.D lane), so these execute;
    # math.type("3.0") is nil, which is why they could never be smuggled
    # as rendered text.
    'return math.type(3.0)'
    'return math.type(2^3)'

    # mux.* bridge semantics under default-on (#1745, TC013/TC014's
    # shape).  mux.name is PURE and executes compiled, pcalling the same
    # bridge C function the interpreter calls.  mux.eval is EFFECTFUL --
    # arbitrary softcode -- and must DECLINE in every form: the chunk is
    # the rerun unit, so an effect delivered compiled plus any later
    # runtime decline would be delivered again by the interpreter re-run
    # (proved empirically in #1750's adversarial review, statement-form
    # pemit + declining read).  Assert the ANSWER here; if an eval case
    # ever starts executing, the exactly-once argument must be re-made,
    # not waved through.
    'local x=mux.eval("add(2,3)") return x'
    'return mux.eval("add(10,20)")'

    # Escape voids the plain proof (#1751 Phase 1): after t passes as a
    # call argument the callee may have installed a metatable, so a typed
    # read of t[2] is no longer provable and the chunk is ineligible --
    # the interpreter answers.  The EXEC pin for CALL_VOID+proven reads
    # is the pre-escape spelling above.
    'local t={5} table.insert(t,6) return t[2]'
    # ABSENT-KEY read on a proven table: the value is nil, which a typed
    # integer slot cannot carry.  Continuing with 0 was a measured silent
    # wrong answer (jit "0" vs interp ""); loud until nil is
    # representable.  Counted post_entry_loud.
    'local t={} t[1]=5 return t[2]'

    # Multi-value truncation at the chunk boundary: string.find returns TWO
    # values and the interpreter's own chunk call is lua_pcall(L, 0, 1, 0),
    # so both routes must keep exactly the first.  Declines today (a number
    # comes back where CALL_STR demands a string), but if a numeric call
    # result ever compiles, this is the case that catches a route that asks
    # for a different result count than the interpreter does.
    'return string.find("ab","b")'
)

# #1751 Phase 0: post-entry decline is loud (error string), not silent re-run.
# AGREE may count those as post_entry_loud rather than diverge, with a budget
# that may only fall as Phases 1–3 empty the FAIL sites.
#
# Count of AGREE + NESTED_AGREE + STATE chunks that commit a loud
# POST-ENTRY DECLINE (not silent re-run).  MAY FALL, MUST NOT RISE as
# Phases 1–3 empty FAIL sites.  Long-term target is 0.
#
# Phase 3: LIMITED is interpreter-identical (not POST-ENTRY DECLINE).
# Budget for-loop / while-true match both routes.  Remaining loud under
# revised Phase 1/2 (restacked 2026-07-29): string.find, absent-key pin,
# STATE e1/e2 → 4.
POST_ENTRY_LOUD_BUDGET=4

# How many AGREE chunks are expected to decline rather than execute.
#
# A decline is safe (the interpreter answers) and so cannot be a hard error
# -- but it is also not coverage, and a pass/fail count cannot tell the two
# apart.  Freezing the number is what makes progress and regression both
# visible: lowering it is the measure of #1519's bridge work landing, and a
# rise means something that used to compile stopped.
#
# MAY FALL, MUST NOT RISE.  Same ratchet as BAN_LEGACY in
# tests/format/check_formats.py (#1631/#1653).
#
# After Phase 0, silent decline is only pre-entry (compile refuse / never
# entered).  Remaining: the two mux.eval pins -- effectful members are
# compile-ineligible (#1750), so the interpreter answers honestly.
# Post-entry sites that used to silent-decline now count under
# POST_ENTRY_LOUD_BUDGET instead.
# Phase 2 revision re-baseline: +2 for select (arity above the typed
# encoding) and os.time (zero-arg unclaimed call), both pre-entry
# ineligible now that the general call path is gone -- the interpreter
# answers each, including its own error for os.time, on both legs.
AGREE_DECLINE_BUDGET=5

# ---------------------------------------------------------------------------
# EXEC — must match AND lua_run_ok must advance (#1426).
# No globals/stdlib: pure arithmetic / compare / branch on mux.args.
# ---------------------------------------------------------------------------

EXEC_CASES=(
    # Integer-keyed table store and load on the dedicated-opcode path
    # (#1519).  These are the first table chunks to EXECUTE rather than
    # decline: HIR_LUA_NEWTABLE and HIR_LUA_SETI now lower to numbered
    # ECALLs that carry the stack index in a register, where the mechanism
    # they replace marshalled it through guest memory as a decimal string
    # and never completed.
    #
    # In EXEC rather than AGREE deliberately: agreement alone is what a
    # decline also produces, so only lua_run_ok > 0 distinguishes "the JIT
    # computed this" from "the interpreter did" (#1426).
    'local t={} t[1]=5 return t[1]'
    'local t={} t[1]=7 t[2]=9 return t[1]+t[2]'

    # Table CONSTRUCTORS, which lower through SETLIST rather than the
    # SETTABI stores above -- a separate path that was still emitting the
    # named ECALL and so compiled and then failed on every call.
    'local t={10,20,30} return t[1]+t[3]'
    'local t={4,5} return t[2]'

    # `#t` -- #1424's original symptom, fixed at the root rather than
    # declined.  It answered 22 for a three-element table because the
    # lowering measured a stack INDEX that had been marshalled out as a
    # decimal string: strlen of the text, not the length of the table.
    # ECALL_LUA_LEN_INT asks the VM, and the index stays in a register.
    'local t={1,2,3} return #t'
    'local t={} t[1]=9 t[2]=8 return #t'

    # String-keyed fields.  TWO DISTINCT KEYS on purpose: with one key a
    # broken implementation still looks right, and the first version of this
    # was broken exactly that way -- the SCONST key lives as loc[].addr with
    # in_reg=false, so passing it through ra_get_reg left a1 holding the same
    # stale address on every call and every read returned the LAST value
    # written.  `t.a` alone answered 7 correctly while `t.a+t.b` answered 8
    # instead of 7.
    'local t={a=3,b=4} return t.a+t.b'
    'local t={} t.x=5 t.y=6 return t.x*10+t.y'

    # Library call: GETGLOBAL -> GETFIELD_REF -> CALL_INT, three ECALLs
    # sharing live Lua stack state inside one compiled run.  Every earlier
    # increment touched at most two, and #1519 flagged that discipline as
    # needing proof rather than assumption.
    #
    # TWO ARGUMENTS on purpose, and asymmetric ones: max(3,9) and min(3,9)
    # give different answers, so an implementation that passes only the
    # first argument, or swaps them, cannot pass both.  One argument would
    # prove as little here as one key did for table fields.
    'local x=math.max(3,9) return x'
    'local x=math.min(3,9) return x'
    'local x=math.abs(-7) return x'

    # String RESULTS.  The result lands in the output slot the allocator
    # gives any TY_STRING value and the ECALL is told that slot's size --
    # ECALL_ORD (#1679) is the reason a string-producing ECALL states its
    # bound rather than assuming one.
    #
    # string.rep takes a string AND an integer, so it covers MIXED argument
    # kinds: an implementation that treated every argument as one kind
    # cannot pass it.  (string.sub would have been the obvious choice and is
    # wrong here -- three arguments, and the ceiling is two, so it declines
    # and could never be an EXEC case.)
    'local x=string.upper("ab") return x'
    'local x=string.lower("AB") return x'
    'local x=string.rep("ab",2) return x'

    # BARE globals -- the callee is the global itself, a function rather
    # than a library table, so the handle comes from GETGLOBAL instead of
    # GETFIELD_REF.
    #
    # tostring and tonumber are the pair that matters: identical argument
    # shapes, opposite result types.  HIR result types are static and Lua's
    # are not, so the lowering routes on the callee NAME -- an
    # implementation that inferred the result type from the arguments
    # cannot pass both.
    #
    # tonumber("17") additionally takes a STRING and returns an INTEGER,
    # which is why the two call ECALLs share one argument encoding.
    'local x=tostring(42) return x'
    'local x=tonumber("17") return x'
    'local x=type(42) return x'

    # TAILCALL -- `return f(...)` with no local temporary, the most common
    # one-liner shape.  Lua compiles it to OP_TAILCALL plus a dead trailing
    # `RETURN A 0`, so these prove three things the local-temporary cases
    # above cannot: the tail call emits the return half at all (a lowering
    # that emits only the call answers empty), the dead multret return is
    # recognized as dead instead of declining the chunk, and both result
    # types survive the shape.  Asymmetric max/min arguments and the
    # tostring/tonumber pair carry the same discipline as their EXEC
    # originals -- an implementation that passes one argument, swaps them,
    # or infers the result type from the arguments cannot pass the set.
    'return math.max(3,9)'
    'return math.min(3,9)'
    'return tostring(42)'
    'return tonumber("17")'
    'return string.rep("ab",2)'
    'return math.type(3)'

    # FLOAT arguments -- raw double bits over the FMV.X.D lane, kind 2 in
    # the two-bit argument encoding.  A constant float to each result
    # variant (floor returns an integer, type returns a string), then a
    # RUNTIME float, which a constant-folding accident cannot fake.  Two
    # floor calls with different fractions catch an implementation that
    # loads one argument slot and reuses it.
    'local x=math.floor(3.7) return x'
    'local x=math.type(3.0) return x'
    'local x=math.floor(2.5)+math.floor(3.25) return x'
    'local x=math.floor(mux.args[1]+0.5) return x'

    # VALUE members of a library table -- math.pi is a float, math.maxinteger
    # an integer, so the referent's member claim picks GETFIELD_FLT or
    # GETFIELD (INT) on the library table instead of a reference nothing
    # could consume.  Each value is USED in arithmetic, not just returned:
    # a slot that was never written (the #1159 address-0 shape) or a stale
    # register cannot survive an add against it.
    'local x=math.pi return x*2'
    'local x=math.maxinteger return x-1'

    # Call-surface: three arguments (string.sub), a HANDLE argument
    # (kind 3 -- the ECALL pushes the value the stack index refers to,
    # which is table.concat's table), and a call for EFFECT (CALL_VOID,
    # nresults=0 -- table.insert).  The insert case reads the INSERTED
    # element, not #t: a void call that silently never ran still leaves
    # #t plausible-looking, but t[2] answers nil.
    'local x=string.sub("world",2,4) return x'
    'local x=table.concat({10,20},"-") return x'
    'local t={5} local x=t[1] table.insert(t,6) return x'

    # Numeric FOR loops under the back-edge budget (#1732).  The
    # accumulator is the loop-carried q-reg routing's whole reason to
    # exist; a*10+i is ORDER-sensitive, so swapped or repeated iterations
    # cannot pass; and the zero-iteration case pins the entry->latch->exit
    # path where the body never runs -- the shape where a reload of a
    # never-stored q-register would resurrect whatever the surrounding
    # command left in %q.
    'local s=0 for i=1,4 do s=s+i end return s'
    'local a=0 for i=1,3 do a=a*10+i end return a'
    'local s=7 for i=1,0 do s=99 end return s'

    # while/repeat (#1732): back edges through backward JMP, the same
    # budget and q-reg routing as the numeric for, with the loop
    # condition in the user's own bytecode rather than FORLOOP's.  The
    # geometric case pins a condition on the CARRIED value (s<100, not a
    # counter), which a stale-reload bug answers wrongly.
    'local i=0 while i<10 do i=i+1 end return i'
    'local i=0 repeat i=i+1 until i>=5 return i'
    'local s=1 while s<100 do s=s*2 end return s'

    # RUNTIME bounds (#1732): `for i=1,n` with n from mux.args.  The
    # zero-trip decision and the wraparound guard are branches now, not
    # compile-time facts, and the runtime zero-trip case (init 3 > limit
    # 1 under a positive step) is the path a constant-fold accident
    # cannot fake.  Step stays constant -- it fixes the comparison's
    # direction.  Args are (2,3).
    'local s=0 for i=1,mux.args[1]+0 do s=s+i end return s'
    'local s=0 for i=mux.args[1]+0,4 do s=s+1 end return s'
    'local s=7 for i=mux.args[2]+0,1 do s=99 end return s'
    'local s=0 for i=mux.args[2]+0,1,-1 do s=s+i end return s'

    # A PURE mux bridge member executes compiled, pcalling the same C
    # function the interpreter calls (#1745/#1750).  Equality with the
    # interpreter is the assertion, so no name is hardcoded; EXEC's
    # lua_run_ok requirement is what proves the compiled path ran.
    'local x=mux.name(1) return x'

    'return mux.args[1] + mux.args[2]'
    'local x=mux.args[1]+0 return x*2'
    'local a=mux.args[1]+0 return a+1'
    'local a=mux.args[1]+0 return a-1'
    'local a=mux.args[1]+0 return a*3'
    'local a=mux.args[1]+0 return -a'
    'local a=mux.args[1]+0 return a//2'
    'local a=mux.args[1]+0 return a%2'
    'local a=mux.args[1]+0 return a < 5'
    'local a=mux.args[1]+0 return a > 5'
    'local a=mux.args[1]+0 return a == 2'
    'local a=mux.args[1]+0 return a ~= 3'
    'local a=mux.args[1]+0 return a <= 2'
    'local a=mux.args[1]+0 return a >= 2'
    'local a=1 local b=2 return a < b'
    'local a=1 local b=2 return a > b'
    'local a=mux.args[1]+0 if a < 5 then return 7 else return 8 end'
    'local a=mux.args[1]+0 if a > 5 then return 7 else return 8 end'
    'local a=mux.args[1]+0 local t=(a<5) return t'
    'local a=mux.args[1]+0 local t=(a==2) return t'
    'local a=mux.args[1]+0 local t=(a>1) return t'
    'local a=mux.args[1]+0 local b=mux.args[2]+0 return a+b*2'
    'local a=mux.args[1]+0 return (a+1)*(a-1)'
    # ---- #1488: integral floats keep the ".0" (Lua float subtype) ----
    'return 3.0'
    'local a=1.5 local b=2.5 return a+b'
    'local a=1.5 return a+1.25'
    'local a=mux.args[1]+0.0 return a+0.0'
    # ---- #1561 + #1538: runtime ^ must compile (tier2 loaded) and agree ----
    'local a=mux.args[1]+0 return a ^ 2'
    'local a=mux.args[1]+0 local b=mux.args[2]+0 return a ^ b'

    # ---- Integer/float subtype (#1488) ----
    #
    # Lua 5.4's integer/float distinction is observable -- tostring(3.0) is
    # "3.0", and one float operand makes the whole expression float -- and
    # the compiled path used to discard it in three places at once.  These
    # are EXEC rather than AGREE on purpose: every one of them executed
    # compiled code while answering wrongly, so a decline here would hide
    # the bug rather than fix it.
    #
    # Lua constant-folds arithmetic on literals, so most of these reach the
    # JIT already collapsed into a single LOADF -- which is exactly how the
    # bug stayed hidden.  `return 4 / 2` lowered to a bare ICONST 2, with no
    # FDIV anywhere in the HIR to notice.
    'return -3.0'
    'return 1e3'
    'return 2.0 + 2.0'
    'return 4 / 2'
    'return 7 / 2'
    'return 7 // 2'
    'return 7.0 // 2.0'
    # Non-integral: pins the format itself ("%.14g", not "%.17g"), which the
    # ".0" cases cannot see.
    'return 2^0.5'
    'return 2 ^ 3'
    'return 3.0 == 3'
    'return 1/0 > 0'
    # Runtime floats, so the ECALL formatter is exercised and not just the
    # compile-time fold.  The two must agree, which is why they share
    # lua_format_double().
    'local a=mux.args[1]+0 return a / 2'
    'local a=mux.args[1]+0 return a * 1.0'
    'local a=mux.args[1]+0 return a + 0.0'
    'local a=mux.args[1]+0 return a//1'

    # ---- string -> number coercion (#1425) ----
    #
    # Already correct; pinned because the reported symptom ("3" + 4 == 6) was
    # an arithmetic answer that was simply wrong, and nothing else covers it.
    'return "3" + 4'
    'return "3" * 2'
    'return "10" - 1'
    'return "2" + "5"'
    'local a=mux.args[1].."" return a + 4'
)

# ---------------------------------------------------------------------------
# NESTED — the EXEC contract under jit_eval_brackets ON (#1326).
# ---------------------------------------------------------------------------
# NESTED: the same contract as EXEC, but with jit_eval_brackets ON -- the
# production default, and the configuration in which the Lua JIT could not
# run at all until #1326.
#
# With brackets compiled, fun_lua is ECALLed from inside a DBT program, so
# run_cached_program is re-entered.  It used to refuse outright, meaning
# `lua_jit 1` on its own executed nothing: every chunk fell back to the Lua
# VM and every assertion here passed by way of the interpreter.  Nothing in
# the tree covered that path, which is exactly why it stayed broken.
#
# Keep these few and cheap.  The point is not lowering coverage -- EXEC above
# does that -- it is that the nested path executes at all.
#
NESTED_CASES=(
    'return 1'
    'return mux.args[1] + mux.args[2]'
    'local a=mux.args[1]+0 return a*3'

    # HIR_ITOA on INT64_MIN.  Negating to get a magnitude wraps for exactly
    # this input, digits come out negative, and '0' + (-d) writes below '0';
    # the rendered value was -'..--).0-*(+,))+(0().
    #
    # run_one passes (2,3), so `-mux.args[1]` is only -2 and never reaches
    # the one input in 2^64 that fails -- multiply into the overflow instead.
    # 2 * 2^62 wraps to INT64_MIN, and rendering it is what exercises itoa.
    'local a=mux.args[1]+0 return a*4611686018427387904'
)

# NESTED_AGREE — brackets ON, must match the interpreter, decline allowed.
#
# The two shapes that regressed when nesting was first enabled and which are
# now DECLINED rather than compiled.  They cannot go in NESTED_CASES: that
# tier requires lua_run_ok > 0, and a decline is exactly what makes these
# correct.  Asserting the answer is the point -- a decline that still returns
# the wrong thing would be a real failure, and only a comparison sees it.
#
# Both produced WRONG ANSWERS, not crashes, and both were invisible to a
# tier that only ran chunks it happened to get right (#1326 review).
#
NESTED_AGREE_CASES=(
    # `#mux.args`: the table is carried as an SCONST holding its own name, so
    # OP_LUA_LEN measured the sentinel -- 8, being strlen("mux.args"), where
    # the interpreter answers the argument count.
    'return #mux.args'

    # Unbounded loop: instruction limits live in the Lua VM hook, which the
    # compiled path does not have.  Answered an empty string instead of
    # "#-1 LUA ERROR: instruction limit exceeded".
    'while true do end'
)

# ---------------------------------------------------------------------------
# STATE — purity oracle (#1751 Phase 0.5 / exhibits on the issue).
#
# Multi-step: setup chunk, action chunk, then a state probe.  Answer-only
# differential harnesses miss these: both routes can return the same string
# while persistent Lua state or delivered effects diverge under
# decline-then-rerun.
#
# Contract under Phase 0:
#   * process lives
#   * STATE (persistent globals) matches across legs — the double-mutation
#     detector (exhibit 1: N must not be 2 when the interpreter left 1)
#   * action RESULT matches, OR the JIT leg is a loud
#     POST-ENTRY DECLINE (honest fail; no silent re-run)
#   * if both legs succeed without loud fail, effect markers (PING) match
#
# When later bins empty the FAIL sites, both legs succeed with matching
# state and effects — the oracle stays green either way.
# ---------------------------------------------------------------------------
STATE_NAMES=(
    e1_double_mutation
    e2_coroutine_effect
    e3_metatable_typed_read
)
STATE_SETUP=(
    'N=0 function bump() N=N+1 end return "set"'
    'CO=coroutine.create(function() mux.pemit(1,"PING") end) return "set"'
    'T=setmetatable({},{__index=function() return "s" end}) return "set"'
)
STATE_ACTION=(
    'bump() local y=mux.get(4000,"Q") return tostring(N)'
    'coroutine.resume(CO) return "r"'
    'return T[1]'
)
STATE_PROBE=(
    'return tostring(N)'
    'return "ok"'
    'return T[2]'
)

keep_work() {
    local rc="$1" tag="$2"
    if [ "$rc" = "0" ] || [ ! -d "$WORK" ]; then
        return 0
    fi
    local dest="$SCRIPT_DIR/work.fail.$tag.$$"
    rm -rf "$dest"
    if mv "$WORK" "$dest" 2>/dev/null; then
        printf '%s' "$dest"
    fi
}

# $1 = lua_jit, $2 = setup chunk, $3 = action chunk, $4 = state probe,
# $5 = tag.  Multi-step purity run (#1751 Phase 0.5).
# Echoes: rc|setup|action|state|pings|post_decline|kept
run_state() {
    local mode="$1" setup="$2" action="$3" probe="$4" tag="${5:-state}"
    rm -rf "$WORK"; mkdir -p "$WORK/data" "$WORK/logs" "$WORK/text"
    ( cd "$WORK" || exit 1
      ulimit -c unlimited 2>/dev/null || true
      ln -s "$BIN" bin
      cp "$REPO_ROOT/mux/game/alias.conf" "$REPO_ROOT/mux/game/compat.conf" . 2>/dev/null
      cat > p.conf <<EOF
input_database  data/p.db
output_database data/p.db.new
crash_database  data/p.db.CRASH
mail_database   data/mail.db
comsys_database data/comsys.db
port 2879
mud_name LuaJitState
command_quota_increment 200000
command_quota_max 200000
include alias.conf
include compat.conf
lua_jit $mode
jit_eval_brackets 0
EOF
      {
        echo "@create probe"
        echo "&SETUP probe=$setup"
        echo "&ACTION probe=$action"
        echo "&PROBE probe=$probe"
        echo "think SETUP|[lua(probe/SETUP)]"
        echo "think RESULT|[lua(probe/ACTION)]"
        echo "think STATE|[lua(probe/PROBE)]"
        echo "think STATS|[jitstats()]"
        echo "@shutdown"
      } > in.txt
      LD_LIBRARY_PATH="$BIN" $TIMEOUT "$BIN/muxscript" -g . -c p.conf < in.txt > out.log 2>&1
      echo "$?" > rc.txt
    )
    local rc setup_res action_res state_res pings postd kept stats
    rc=$(cat "$WORK/rc.txt" 2>/dev/null || echo 99)
    setup_res=$(grep -oE "^SETUP\|.*" "$WORK/out.log" 2>/dev/null | head -1)
    setup_res=${setup_res#SETUP|}
    setup_res=$(printf '%s' "$setup_res" | tr -d '\r')
    action_res=$(grep -oE "^RESULT\|.*" "$WORK/out.log" 2>/dev/null | head -1)
    action_res=${action_res#RESULT|}
    action_res=$(printf '%s' "$action_res" | tr -d '\r')
    state_res=$(grep -oE "^STATE\|.*" "$WORK/out.log" 2>/dev/null | head -1)
    state_res=${state_res#STATE|}
    state_res=$(printf '%s' "$state_res" | tr -d '\r')
    # Effect marker from exhibit 2: mux.pemit(1,"PING") prints a bare PING
    # line.  muxscript logs are CRLF; match optional CR.  grep -c prints 0
    # even when it exits 1 on no match — do not `|| echo 0` (that doubles).
    pings=$(grep -cE $'^PING\r?$' "$WORK/out.log" 2>/dev/null)
    pings=${pings:-0}
    stats=$(grep -oE "^STATS\|.*" "$WORK/out.log" 2>/dev/null | head -1)
    stats=${stats#STATS|}
    postd=$(printf '%s' "$stats" | grep -oE 'lua_post_entry_decline=[0-9]+' | head -1 | cut -d= -f2)
    : "${postd:=0}"
    kept=$(keep_work "$rc" "$tag")
    echo "$rc|$setup_res|$action_res|$state_res|$pings|$postd|$kept"
}

split_state() {
    R_RC=${1%%|*}; local r=${1#*|}
    R_SETUP=${r%%|*}; r=${r#*|}
    R_RES=${r%%|*}; r=${r#*|}
    R_STATE=${r%%|*}; r=${r#*|}
    R_PINGS=${r%%|*}; r=${r#*|}
    R_POSTD=${r%%|*}; r=${r#*|}
    R_KEPT=$r
}

# $1 = lua_jit, $2 = chunk, $3 = tag, $4 = jit_eval_brackets (default 0)
# Echoes: rc|result|run_ok|run_fail|compile_ok|compile_fail|kept
run_one() {
    local mode="$1" chunk="$2" tag="${3:-run}" brackets="${4:-0}"
    rm -rf "$WORK"; mkdir -p "$WORK/data" "$WORK/logs" "$WORK/text"
    ( cd "$WORK" || exit 1
      ulimit -c unlimited 2>/dev/null || true
      ln -s "$BIN" bin
      cp "$REPO_ROOT/mux/game/alias.conf" "$REPO_ROOT/mux/game/compat.conf" . 2>/dev/null
      cat > p.conf <<EOF
input_database  data/p.db
output_database data/p.db.new
crash_database  data/p.db.CRASH
mail_database   data/mail.db
comsys_database data/comsys.db
port 2879
mud_name LuaJitDiff
command_quota_increment 200000
command_quota_max 200000
include alias.conf
include compat.conf
lua_jit $mode
jit_eval_brackets $brackets
EOF
      {
        echo "@create probe"
        echo "&A probe=$chunk"
        echo "think HASJIT|[strmatch(jitstats(),*eval_attempts=*)]"
        echo "think RESULT|[lua(probe/A,2,3)]"
        echo "think STATS|[jitstats()]"
        echo "@shutdown"
      } > in.txt
      LD_LIBRARY_PATH="$BIN" $TIMEOUT "$BIN/muxscript" -g . -c p.conf < in.txt > out.log 2>&1
      echo "$?" > rc.txt
    )
    local rc res kept stats rok rfail cok cfail
    rc=$(cat "$WORK/rc.txt" 2>/dev/null || echo 99)
    res=$(grep -oE "^RESULT\|.*" "$WORK/out.log" 2>/dev/null | head -1)
    res=${res#RESULT|}
    res=$(printf '%s' "$res" | tr -d '\r')
    stats=$(grep -oE "^STATS\|.*" "$WORK/out.log" 2>/dev/null | head -1)
    stats=${stats#STATS|}
    rok=$(printf '%s' "$stats" | grep -oE 'lua_run_ok=[0-9]+' | head -1 | cut -d= -f2)
    rfail=$(printf '%s' "$stats" | grep -oE 'lua_run_fail=[0-9]+' | head -1 | cut -d= -f2)
    cok=$(printf '%s' "$stats" | grep -oE 'lua_compile_ok=[0-9]+' | head -1 | cut -d= -f2)
    cfail=$(printf '%s' "$stats" | grep -oE 'lua_compile_fail=[0-9]+' | head -1 | cut -d= -f2)
    : "${rok:=0}" "${rfail:=0}" "${cok:=0}" "${cfail:=0}"
    kept=$(keep_work "$rc" "$tag")
    echo "$rc|$res|$rok|$rfail|$cok|$cfail|$kept"
}

split6() {
    R_RC=${1%%|*}; local r=${1#*|}
    R_RES=${r%%|*}; r=${r#*|}
    R_OK=${r%%|*}; r=${r#*|}
    R_FAIL=${r%%|*}; r=${r#*|}
    R_COK=${r%%|*}; r=${r#*|}
    R_CFAIL=${r%%|*}; r=${r#*|}
    R_KEPT=$r
}

# Preflight.
split6 "$(run_one 1 'return 1' baseline)"
if [ "$R_RC" != "0" ]; then
    echo "FAIL: baseline 'return 1' did not survive (rc=$R_RC)."
    [ -n "$R_KEPT" ] && echo "      evidence: $R_KEPT"
    exit 1
fi
if ! grep -q "^HASJIT|1" "$WORK/out.log" 2>/dev/null; then
    echo "SKIP: engine JIT not present in this build (configure --enable-jit)."
    rm -rf "$WORK"
    exit 0
fi
if [ "$R_OK" = "0" ]; then
    echo "FAIL: baseline 'return 1' did not advance lua_run_ok under"
    echo "      lua_jit 1 + jit_eval_brackets 0 (got ok=$R_OK fail=$R_FAIL)."
    echo "      The EXEC contract cannot be checked; see #1426 / #1326."
    exit 1
fi

# True if results agree for differential purposes.
# Exact match, or both are LUA ERROR with the same core message after
# stripping the interpreter's `#dbref/ATTR:line:` prefix and a trailing
# parenthetical (e.g. `(global 'os')`) that the JIT ECALL path does not
# always reconstruct (#1751 Phase 1 total GETGLOBAL/GETFIELD).
#
results_match() {
    local a="$1" b="$2"
    if [ "$a" = "$b" ]; then
        return 0
    fi
    case "$a" in
        '#-1 LUA ERROR:'*) ;;
        *) return 1 ;;
    esac
    case "$b" in
        '#-1 LUA ERROR:'*) ;;
        *) return 1 ;;
    esac
    local na nb
    na=$(printf '%s' "$a" | sed -E \
        -e 's/^#-1 LUA ERROR: //' \
        -e 's/^#[0-9]+\/[^:]+:[0-9]+: //' \
        -e 's/ \([^)]*\)$//')
    nb=$(printf '%s' "$b" | sed -E \
        -e 's/^#-1 LUA ERROR: //' \
        -e 's/^#[0-9]+\/[^:]+:[0-9]+: //' \
        -e 's/ \([^)]*\)$//')
    [ "$na" = "$nb" ]
}

crashes=0
surv_div=0
agree_wrong=0
agree_declined=0
agree_executed=0
post_entry_loud=0
exec_wrong=0
exec_no_run=0
exec_post_entry=0
nested_wrong=0
nested_no_run=0
n=0

printf '\n== SURVIVE (crash = fail; result diverge = report) ==\n'
printf '%-48s %-4s %-10s %-10s %-8s %s\n' chunk rc interp jit run_ok verdict
for chunk in "${SURVIVE_CASES[@]}"; do
    n=$((n+1))
    split6 "$(run_one 0 "$chunk" "s0-$n")"; irc=$R_RC; ires=$R_RES
    split6 "$(run_one 1 "$chunk" "s1-$n")"; jrc=$R_RC; jres=$R_RES; jok=$R_OK; jkept=$R_KEPT
    verdict="ok"
    if [ "$jrc" != "0" ]; then
        verdict="FAIL: died rc=$jrc"
        [ -n "$jkept" ] && verdict="$verdict evidence=$jkept"
        crashes=$((crashes+1))
    elif [ "$irc" != "0" ]; then
        verdict="FAIL: interp died rc=$irc"
        crashes=$((crashes+1))
    elif [ "$ires" != "$jres" ]; then
        verdict="diverges interp=$ires jit=$jres"
        surv_div=$((surv_div+1))
    fi
    printf '%-48s %-4s %-10s %-10s %-8s %s\n' \
        "${chunk:0:48}" "$jrc" "${ires:0:10}" "${jres:0:10}" "$jok" "$verdict"
done

printf '\n== AGREE (must match; decline OK) ==\n'
printf '%-48s %-4s %-10s %-10s %-8s %s\n' chunk rc interp jit run_ok verdict
for chunk in "${AGREE_CASES[@]}"; do
    n=$((n+1))
    split6 "$(run_one 0 "$chunk" "a0-$n")"; irc=$R_RC; ires=$R_RES
    split6 "$(run_one 1 "$chunk" "a1-$n")"; jrc=$R_RC; jres=$R_RES; jok=$R_OK; jfail=$R_FAIL; jkept=$R_KEPT
    verdict="ok"
    if [ "$jrc" != "0" ]; then
        verdict="FAIL: died rc=$jrc"
        [ -n "$jkept" ] && verdict="$verdict evidence=$jkept"
        crashes=$((crashes+1))
    elif [ "$irc" != "0" ]; then
        verdict="FAIL: interp died rc=$irc"
        crashes=$((crashes+1))
    elif printf '%s' "$jres" | grep -q 'LUA JIT POST-ENTRY DECLINE'; then
        # Phase 0: committed loud fail on the compiled path (#1751).
        verdict="ok (post-entry decline loud; Phase 0)"
        post_entry_loud=$((post_entry_loud+1))
    elif ! results_match "$ires" "$jres"; then
        verdict="FAIL: diverges interp=$ires jit=$jres"
        agree_wrong=$((agree_wrong+1))
    elif [ "$jok" = "0" ]; then
        verdict="ok (declined/compile-fail; interp answered)"
        agree_declined=$((agree_declined+1))
    else
        agree_executed=$((agree_executed+1))
    fi
    printf '%-48s %-4s %-10s %-10s %-8s %s\n' \
        "${chunk:0:48}" "$jrc" "${ires:0:10}" "${jres:0:10}" "$jok" "$verdict"
done

printf '\n== EXEC (must match AND lua_run_ok>0) ==\n'
printf '%-48s %-4s %-10s %-10s %-8s %s\n' chunk rc interp jit run_ok verdict
for chunk in "${EXEC_CASES[@]}"; do
    n=$((n+1))
    split6 "$(run_one 0 "$chunk" "e0-$n")"; irc=$R_RC; ires=$R_RES
    split6 "$(run_one 1 "$chunk" "e1-$n")"; jrc=$R_RC; jres=$R_RES; jok=$R_OK; jfail=$R_FAIL; jkept=$R_KEPT
    verdict="ok"
    if [ "$jrc" != "0" ]; then
        verdict="FAIL: died rc=$jrc"
        [ -n "$jkept" ] && verdict="$verdict evidence=$jkept"
        crashes=$((crashes+1))
    elif [ "$irc" != "0" ]; then
        verdict="FAIL: interp died rc=$irc"
        crashes=$((crashes+1))
    elif printf '%s' "$jres" | grep -q 'LUA JIT POST-ENTRY DECLINE'; then
        # Phase 0: compiled path entered then failed loud — not a silent
        # re-run.  Still fails EXEC (must execute and match) until bins empty.
        verdict="FAIL: post-entry decline loud (Phase 0; #1751)"
        exec_post_entry=$((exec_post_entry+1))
    elif [ "$ires" != "$jres" ]; then
        verdict="FAIL: diverges interp=$ires jit=$jres"
        exec_wrong=$((exec_wrong+1))
    elif [ "$jok" = "0" ]; then
        # The #1426 vacuity: agree by declining / compile-fail / never running.
        verdict="FAIL: lua_run_ok=0 (compiled path did not execute; #1426)"
        exec_no_run=$((exec_no_run+1))
    fi
    printf '%-48s %-4s %-10s %-10s %-8s %s\n' \
        "${chunk:0:48}" "$jrc" "${ires:0:10}" "${jres:0:10}" "$jok" "$verdict"
done

printf '\n== NESTED (brackets ON: must match AND lua_run_ok>0) ==\n'
printf '%-48s %-4s %-10s %-10s %-8s %s\n' chunk rc interp jit run_ok verdict
for chunk in "${NESTED_CASES[@]}"; do
    n=$((n+1))
    split6 "$(run_one 0 "$chunk" "n0-$n" 1)"; irc=$R_RC; ires=$R_RES
    split6 "$(run_one 1 "$chunk" "n1-$n" 1)"; jrc=$R_RC; jres=$R_RES; jok=$R_OK; jfail=$R_FAIL; jkept=$R_KEPT
    verdict="ok"
    if [ "$jrc" != "0" ]; then
        verdict="FAIL: died rc=$jrc"
        [ -n "$jkept" ] && verdict="$verdict evidence=$jkept"
        crashes=$((crashes+1))
    elif [ "$irc" != "0" ]; then
        verdict="FAIL: interp died rc=$irc"
        crashes=$((crashes+1))
    elif [ "$ires" != "$jres" ]; then
        verdict="FAIL: diverges interp=$ires jit=$jres"
        nested_wrong=$((nested_wrong+1))
    elif [ "$jok" = "0" ]; then
        verdict="FAIL: lua_run_ok=0 nested under bracket JIT (#1326)"
        nested_no_run=$((nested_no_run+1))
    fi
    printf '%-48.48s %-4s %-10.10s %-10.10s %-8s %s\n' \
        "$chunk" "$jrc" "$ires" "$jres" "$jok" "$verdict"
done

printf '\n== NESTED_AGREE (brackets ON: must match; decline OK) ==\n'
printf '%-48s %-4s %-10s %-10s %-8s %s\n' chunk rc interp jit run_ok verdict
for chunk in "${NESTED_AGREE_CASES[@]}"; do
    n=$((n+1))
    split6 "$(run_one 0 "$chunk" "na0-$n" 1)"; irc=$R_RC; ires=$R_RES
    split6 "$(run_one 1 "$chunk" "na1-$n" 1)"; jrc=$R_RC; jres=$R_RES; jok=$R_OK; jkept=$R_KEPT
    verdict="ok"
    if [ "$jrc" != "0" ]; then
        verdict="FAIL: died rc=$jrc"
        [ -n "$jkept" ] && verdict="$verdict evidence=$jkept"
        crashes=$((crashes+1))
    elif [ "$irc" != "0" ]; then
        verdict="FAIL: interp died rc=$irc"
        crashes=$((crashes+1))
    elif printf '%s' "$jres" | grep -q 'LUA JIT POST-ENTRY DECLINE'; then
        # Phase 0: loud post-entry decline is honest; not silent re-run (#1751).
        verdict="ok (post-entry decline loud; Phase 0)"
        post_entry_loud=$((post_entry_loud+1))
    elif [ "$ires" != "$jres" ]; then
        verdict="FAIL: diverges interp=$ires jit=$jres"
        nested_wrong=$((nested_wrong+1))
    elif [ "$jok" != "0" ]; then
        verdict="ok (executed)"
    else
        verdict="ok (declined; interp answered)"
    fi
    printf '%-48.48s %-4s %-10.10s %-10.10s %-8s %s\n' \
        "$chunk" "$jrc" "$ires" "$jres" "$jok" "$verdict"
done

printf '\n== STATE (purity oracle: persistent state + effects; #1751 Phase 0.5) ==\n'
printf '%-22s %-4s %-10s %-10s %-8s %-8s %s\n' case rc i_state j_state i_ping j_ping verdict
state_wrong=0
state_loud=0
si=0
while [ "$si" -lt "${#STATE_NAMES[@]}" ]; do
    name="${STATE_NAMES[$si]}"
    setup="${STATE_SETUP[$si]}"
    action="${STATE_ACTION[$si]}"
    probe="${STATE_PROBE[$si]}"
    n=$((n+1))
    split_state "$(run_state 0 "$setup" "$action" "$probe" "st0-$name")"
    irc=$R_RC; ires=$R_RES; istate=$R_STATE; ipings=$R_PINGS
    split_state "$(run_state 1 "$setup" "$action" "$probe" "st1-$name")"
    jrc=$R_RC; jres=$R_RES; jstate=$R_STATE; jpings=$R_PINGS; jkept=$R_KEPT; jpostd=$R_POSTD
    verdict="ok"
    jloud=0
    if printf '%s' "$jres" | grep -q 'LUA JIT POST-ENTRY DECLINE'; then
        jloud=1
    fi
    if [ "$jrc" != "0" ]; then
        verdict="FAIL: died rc=$jrc"
        [ -n "$jkept" ] && verdict="$verdict evidence=$jkept"
        crashes=$((crashes+1))
    elif [ "$irc" != "0" ]; then
        verdict="FAIL: interp died rc=$irc"
        crashes=$((crashes+1))
    elif [ "$istate" != "$jstate" ]; then
        # Exhibit 1 shape: silent double-mutation (interp N=1, jit N=2).
        verdict="FAIL: state diverges interp=$istate jit=$jstate (purity)"
        state_wrong=$((state_wrong+1))
    elif [ "$jloud" = "1" ]; then
        # Phase 0: loud fail is honest.  State already matched.
        verdict="ok (post-entry loud; state pure)"
        state_loud=$((state_loud+1))
        post_entry_loud=$((post_entry_loud+1))
    elif [ "$ires" != "$jres" ]; then
        verdict="FAIL: result diverges interp=$ires jit=$jres"
        state_wrong=$((state_wrong+1))
    elif [ "$ipings" != "$jpings" ]; then
        # Both routes "succeeded" but effects differ (exhibit 2 silent loss).
        verdict="FAIL: effect count interp=$ipings jit=$jpings"
        state_wrong=$((state_wrong+1))
    else
        verdict="ok (matched; pure)"
    fi
    # Surface counter for exhibit paths that commit EFFECT_REFUSED outside
    # RunCompiled (must still advance lua_post_entry_decline).
    if [ "$jloud" = "1" ] && [ "${jpostd:-0}" = "0" ]; then
        verdict="FAIL: loud result but lua_post_entry_decline=0"
        state_wrong=$((state_wrong+1))
    fi
    printf '%-22s %-4s %-10s %-10s %-8s %-8s %s\n' \
        "$name" "$jrc" "${istate:0:10}" "${jstate:0:10}" "$ipings" "$jpings" "$verdict"
    si=$((si+1))
done

rm -rf "$WORK"
total=$((${#SURVIVE_CASES[@]} + ${#AGREE_CASES[@]} + ${#EXEC_CASES[@]} + ${#NESTED_CASES[@]} + ${#NESTED_AGREE_CASES[@]} + ${#STATE_NAMES[@]}))
echo
echo "chunks: $total   crashes: $crashes   survive_diverges: $surv_div"
echo "agree_wrong: $agree_wrong   exec_wrong: $exec_wrong   exec_no_run: $exec_no_run   exec_post_entry: $exec_post_entry"
echo "nested_wrong: $nested_wrong   nested_no_run: $nested_no_run   (of ${#NESTED_CASES[@]} NESTED chunks, brackets ON)"
echo "state_wrong: $state_wrong   state_loud: $state_loud   (of ${#STATE_NAMES[@]} STATE purity cases)"

# Executed vs declined, separately (#1426).
#
# A declined chunk agrees with the interpreter because the interpreter
# answered it -- correct behaviour, and indistinguishable from coverage in a
# pass/fail count.  Reporting them apart stops "38/38 agree" standing in for
# "the compiler saw 38 chunks", which it does not: it saw the executed ones.
#
echo "agree_executed: $agree_executed   agree_declined: $agree_declined   post_entry_loud: $post_entry_loud   (of ${#AGREE_CASES[@]} AGREE + STATE loud)"

decline_budget_fail=0
if [ "$agree_declined" -gt "$AGREE_DECLINE_BUDGET" ]; then
    echo "  DECLINE BUDGET: $agree_declined declined, budget $AGREE_DECLINE_BUDGET."
    echo "  Something that used to compile now declines.  The results still"
    echo "  match because the interpreter answered, so only this count sees it."
    decline_budget_fail=1
elif [ "$agree_declined" -lt "$AGREE_DECLINE_BUDGET" ]; then
    echo "  DECLINE BUDGET: down to $agree_declined from $AGREE_DECLINE_BUDGET -- good."
    echo "  Lower AGREE_DECLINE_BUDGET in this file to $agree_declined to keep"
    echo "  the ratchet tight, in the same commit as whatever improved it."
    decline_budget_fail=1
fi

post_entry_budget_fail=0
if [ "$post_entry_loud" -gt "$POST_ENTRY_LOUD_BUDGET" ]; then
    echo "  POST-ENTRY LOUD BUDGET: $post_entry_loud > $POST_ENTRY_LOUD_BUDGET (#1751 Phase 0)."
    post_entry_budget_fail=1
elif [ "$post_entry_loud" -lt "$POST_ENTRY_LOUD_BUDGET" ] && [ "$post_entry_loud" -gt 0 ]; then
    echo "  POST-ENTRY LOUD BUDGET: down to $post_entry_loud from $POST_ENTRY_LOUD_BUDGET -- good."
    echo "  Lower POST_ENTRY_LOUD_BUDGET in this file to $post_entry_loud."
    post_entry_budget_fail=1
fi

if [ "$crashes" -ne 0 ] || [ "$agree_wrong" -ne 0 ] \
   || [ "$exec_wrong" -ne 0 ] || [ "$exec_no_run" -ne 0 ] \
   || [ "$exec_post_entry" -ne 0 ] \
   || [ "$nested_wrong" -ne 0 ] || [ "$nested_no_run" -ne 0 ] \
   || [ "$state_wrong" -ne 0 ] \
   || [ "$decline_budget_fail" -ne 0 ] \
   || [ "$post_entry_budget_fail" -ne 0 ]; then
    echo "=== tests/luajit: FAILED ==="
    if [ "$crashes" -ne 0 ]; then
        echo "  process deaths: preserved work.fail.* dirs next to this script"
        ls -d "$SCRIPT_DIR"/work.fail.*."$$" 2>/dev/null | sed 's/^/    /' || true
    fi
    if [ "$exec_no_run" -ne 0 ]; then
        echo "  EXEC cases with lua_run_ok=0: the compiled path never ran."
        echo "  Fix config (jit_eval_brackets 0) or the lowering, not the assert."
        echo "  See #1426."
    fi
    if [ "$nested_no_run" -ne 0 ]; then
        echo "  NESTED cases with lua_run_ok=0: the Lua JIT did not execute"
        echo "  under jit_eval_brackets 1.  That is the #1326 reentrancy"
        echo "  refusal in run_cached_program, not a lowering problem -- the"
        echo "  same chunks execute with brackets off.  Do NOT 'fix' this by"
        echo "  setting brackets 0 here: brackets on is the production"
        echo "  default, and covering it is the entire point of this tier."
    fi
    if [ "$state_wrong" -ne 0 ]; then
        echo "  STATE purity oracle failed (#1751 exhibits / Phase 0.5)."
        echo "  Persistent Lua state or delivered effects diverged, or a loud"
        echo "  fail did not advance lua_post_entry_decline."
    fi
    if [ "$agree_wrong" -ne 0 ] || [ "$exec_wrong" -ne 0 ] || [ "$nested_wrong" -ne 0 ]; then
        echo "  Result mismatch vs interpreter — see #1512 / open lua/jit bugs."
    fi
    exit 1
fi
echo "=== tests/luajit: PASSED ==="
echo "  SURVIVE: no crashes ($surv_div reported divergences, non-fatal)"
echo "  AGREE:   all match interpreter (decline allowed)"
echo "  EXEC:    all match and lua_run_ok advanced (#1426)"
echo "  STATE:   purity oracle green (#1751 Phase 0.5; $state_loud loud-ok)"
exit 0
