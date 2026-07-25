#!/usr/bin/env python3
"""Generate random typed, nested softcode expressions for the JIT differential
fuzzer (see run.sh).  Each expression is emitted twice per test:

  J side: @if strlen(setr(0,EXPR))={...sha1(r(0))...}   -> JIT result (the @if
          condition is a pure function tree, so mux_exec compiles it).
  I side: @pemit #1=I~id~[sha1(EXPR)]~...               -> interpreter result
          (the eval-bracket in the @pemit argument makes mux_exec bail the JIT).

SOUNDNESS: the two sides only agree when EXPR has no eval-context-sensitive
constructs.  In particular a *bracket-less mid-string* function call (e.g.
"foo add(2,3) bar") is correctly left literal when bare but is evaluated inside
[...], which would be a false positive.  So the generator NEVER embeds a
function inside literal text: every node is either a pure-literal leaf or a
function-call node, and colored leaves are a single ansi(<color>,<words>) call
wrapping the whole list.  Do not "simplify" leaf generation to inline functions.

CORPUS SHAPES (all default-on; see the block above DELIMS for the rationale):
  ~20%  custom delimiters / output separators
  ~15%  float arithmetic with RUNTIME operands
  ~15%  ifelse()/switch() over a runtime condition, mixed-type arms
  ~14%  nested evaluation -- u()/ulocal() consumed by an outer call
  rest  string/list function trees

The float, branch and nested shapes exist because three consecutive defects
escaped this fuzzer, all for the same reason: a corpus of string and list
functions over literal operands.  #1143 (float PHI arms copied raw double
bits as a C string), #1157 (a bare %N condition folded to 0) and #1159 (FP
slots pinned at address 0 in nested compiles) are each invisible unless the
operand is unknown at COMPILE time -- a folded float allocates no FP slot
and a folded condition emits no branch.  Hence v(fz.*) rather than literals
or %q: v() is an ECALL the compiler cannot see through, whereas
compile-time %q tracking can fold %q back to a constant, which is exactly
what made ifelse(%q0,...) look healthy while ifelse(%0,...) was broken.

Usage: gen.py <count> <batch_size> <out_dir>
       (also writes <out_dir>/manifest.txt and <out_dir>/setup.txt)
"""
import random
import sys

WORDS = ["ab", "cd", "ef", "gh", "ij", "kl", "x", "yy", "zzz"]
COLORS = ["r", "g", "b", "h", "y", "rh", "c", "m"]

# UTF-8 corpus mode (--utf8): multi-byte words drive the byte-vs-cluster
# divergence class through every position/length-taking function shape
# (the rv64_wordpos/rv64_delete family).  All are plain literals — safe
# to embed anywhere a WORDS entry is (no commas, spaces, %, or braces).
#   é       2-byte Latin
#   héllo   mixed ASCII/2-byte
#   日本    3-byte CJK pair
#   👋🏻      emoji + skin-tone modifier: one grapheme, two code points
#   café    trailing 2-byte
UTF8_WORDS = ["é", "héllo", "日本",
              "\U0001f44b\U0001f3fb", "café"]


def leaf_list(colored):
    n = random.randint(1, 5)
    ws = " ".join(random.choice(WORDS) for _ in range(n))
    # Colored leaf is ONE ansi() call wrapping the whole list — never an
    # ansi() embedded mid-string (that would be a bracket-less mid-string call).
    if colored and random.random() < 0.5:
        return f"ansi({random.choice(COLORS)},{ws})"
    return ws


def leaf_word():
    return random.choice(WORDS)


def leaf_int():
    return str(random.choice([0, 1, 2, 3, 4, 5, 6, -1, -2, 9, 99]))


# name -> (return_type, [arg type chars])
#   S/L string-or-list, W word, I int, i small-int
FUNCS = {
    "lcstr": ("S", ["S"]), "ucstr": ("S", ["S"]), "reverse": ("S", ["S"]),
    "trim": ("S", ["S"]), "squish": ("S", ["L"]), "stripansi": ("S", ["S"]),
    "first": ("S", ["L"]), "rest": ("L", ["L"]), "last": ("S", ["L"]),
    "revwords": ("L", ["L"]), "mid": ("S", ["S", "I", "I"]),
    "strtrunc": ("S", ["S", "I"]), "repeat": ("S", ["W", "i"]),
    "ljust": ("S", ["S", "i"]), "rjust": ("S", ["S", "i"]),
    "center": ("S", ["S", "i"]), "before": ("S", ["S", "W"]),
    "after": ("S", ["S", "W"]), "edit": ("S", ["S", "W", "W"]),
    "secure": ("S", ["S"]), "ldelete": ("L", ["L", "I"]),
    "replace": ("L", ["L", "I", "W"]), "insert": ("L", ["L", "I", "W"]),
    "extract": ("L", ["L", "I", "I"]), "elements": ("L", ["L", "I"]),
    "remove": ("L", ["L", "W"]), "setunion": ("L", ["L", "L"]),
    "setdiff": ("L", ["L", "L"]), "setinter": ("L", ["L", "L"]),
    "cat": ("S", ["S", "S"]), "strcat": ("S", ["S", "S"]),
    "strlen": ("I", ["S"]), "words": ("I", ["L"]), "pos": ("I", ["W", "S"]),
    "member": ("I", ["L", "W"]), "wordpos": ("I", ["S", "I"]),
    "add": ("I", ["I", "I"]), "sub": ("I", ["I", "I"]),
}
_by_ret = {}
for _n, (_rt, _a) in FUNCS.items():
    _by_ret.setdefault(_rt, []).append(_n)


def funcs_returning(t):
    if t in ("S", "L", "W"):
        return _by_ret.get("S", []) + _by_ret.get("L", [])
    return _by_ret.get(t, [])


def gen(t, depth, colored):
    # Leaf when out of depth or by chance — but the ROOT (depth>=1) is always
    # a function call (callers pass depth>=1 and a function-returning type).
    if depth <= 0 or random.random() < 0.3:
        if t == "I":
            return leaf_int()
        if t == "i":
            return str(random.randint(0, 4))
        if t == "W":
            return leaf_word()
        return leaf_list(colored)
    cands = funcs_returning(t)
    if not cands:
        return leaf_int() if t == "I" else leaf_list(colored)
    fn = random.choice(cands)
    _, args = FUNCS[fn]
    return f"{fn}({','.join(gen(a, depth - 1, colored) for a in args)})"


# ---------------------------------------------------------------
# Float / branch / nested-evaluation corpus
#
# These three shapes are grouped because they share one requirement: the
# value has to be unknown at COMPILE time.  A folded float never allocates
# an FP slot and a folded condition never emits a branch, so a corpus of
# constants exercises none of the machinery below and reports a vacuous
# pass.  That is not hypothetical — #1143 (float PHI arms), #1157 (bare %N
# conditions) and #1159 (FP slots in nested compiles) were all missed by
# this fuzzer, and all three are foldable-away if the operands are literal.
#
# FZ_SETUP is written to the top of every batch file, in both processes.
# v() is an ECALL, so the compiler cannot see through it — that is what
# makes these values genuinely runtime.  %q was deliberately NOT used:
# compile-time %q tracking can fold it back to a constant, which is the
# trap that made ifelse(%q0,...) look healthy while ifelse(%0,...) was
# broken (#1157).
# ---------------------------------------------------------------
FZ_SETUP = [
    "&fz.s1 me=abc",
    "&fz.s2 me=abcdefg",
    "&fz.s3 me=ab",
    "&fz.l1 me=ab cd ef",
    "&fz.l2 me=x y",
]

# Runtime integers: strlen/words of an attribute the compiler cannot fold.
RUNTIME_INTS = [
    "strlen(v(fz.s1))",     # 3
    "strlen(v(fz.s2))",     # 7
    "strlen(v(fz.s3))",     # 2
    "words(v(fz.l1))",      # 3
    "words(v(fz.l2))",      # 2
]

FLOAT_LITS = ["0.5", "1.5", "2.25", "-0.75", "12.75", "3.0", "-2.5", "100.125"]

# Float-returning functions.  "F" = float subtree, "i" = small int literal.
# Domain errors (sqrt of a negative, fdiv by zero) are deliberately left
# reachable: both sides run the same binary, so a domain result is still a
# byte-comparable oracle, and idiv/fdiv by a runtime zero is its own defect
# class (#1146).
FLOAT_FUNCS = {
    "fdiv": ["F", "F"], "mul": ["F", "F"], "add": ["F", "F"],
    "sub": ["F", "F"], "fmod": ["F", "F"],
    "abs": ["F"], "sqrt": ["F"], "trunc": ["F"],
    "floor": ["F"], "ceil": ["F"], "sign": ["F"],
    "round": ["F", "i"], "power": ["F", "i"],
}


def runtime_int():
    return random.choice(RUNTIME_INTS)


def gen_float(depth):
    """Float expression tree.  Leaves are a mix of literals and
    literal*runtime products; the product form is what forces a real FP
    slot instead of a compile-time fold."""
    if depth <= 0 or random.random() < 0.35:
        r = random.random()
        if r < 0.4:
            return random.choice(FLOAT_LITS)
        if r < 0.85:
            return f"mul({random.choice(FLOAT_LITS)},{runtime_int()})"
        return runtime_int()
    fn = random.choice(list(FLOAT_FUNCS))
    args = []
    for a in FLOAT_FUNCS[fn]:
        args.append(gen_float(depth - 1) if a == "F"
                    else str(random.randint(0, 3)))
    return f"{fn}({','.join(args)})"


def gen_float_root():
    """Guarantee at least one runtime operand, so the expression cannot
    fold to a constant and skip FP slot allocation entirely."""
    e = gen_float(random.randint(1, 3))
    if "v(fz." not in e:
        e = f"mul({e},{runtime_int()})"
    return e


def branch_arm(colored):
    """Arms are deliberately type-MIXED.  A PHI whose arms disagree on type
    is the #1143 surface: float arms silently collapsed to a string PHI and
    the raw double bits were copied as a C string."""
    r = random.random()
    if r < 0.4:
        return gen_float(1)
    if r < 0.7:
        return runtime_int()
    return leaf_word() if random.random() < 0.5 else leaf_list(colored)


def gen_branch(colored):
    """ifelse()/switch() over a runtime condition with mixed-type arms."""
    cond = random.choice([
        runtime_int(),
        f"gt({runtime_int()},2)",
        f"lt({runtime_int()},3)",
        "v(fz.s1)",
        f"sub({runtime_int()},{runtime_int()})",
    ])
    if random.random() < 0.5:
        return f"ifelse({cond},{branch_arm(colored)},{branch_arm(colored)})"
    m1, m2 = random.choice([("2", "3"), ("3", "7"), ("0", "1")])
    return (f"switch({runtime_int()},{m1},{branch_arm(colored)},"
            f"{m2},{branch_arm(colored)},{branch_arm(colored)})")


# Outer wrappers for nested-evaluation shapes.  The wrapper is the entire
# point: a bare u() at the prompt compiles through the one-shot compiler and
# was always correct, while a u() consumed by an enclosing COMPILED
# expression routes the inner compile through the shared heap — the path
# that had every FP slot address pinned at 0 (#1159).
NEST_WRAPPERS = [
    "strcat(<,{0},>)",
    "add({0},1)",
    "strlen({0})",
    "first({0})",
    "iter(1,{0})",
    "switch(1,1,{0},zz)",
    "ifelse(1,{0},zz)",
]


def gen_nested(i, colored):
    """u()/ulocal() of a generated attribute body, consumed by an outer
    call.  Returns (preamble_commands, expression).

    Oracle note: the I side's eval-bracket bails the JIT for the OUTER
    expression only; the inner body still evaluates through mux_exec on
    both sides.  That is exactly the asymmetry that exposes this class —
    outer-compiled routes the inner through the shared heap, outer-
    interpreted routes it through the one-shot compiler."""
    name = f"fz.u{i}"
    shape = i % 5
    if shape == 0:
        body = f"mul({random.choice(FLOAT_LITS)},strlen(%0))"
        args = f",{leaf_word()}"
    elif shape == 1:
        body = "fdiv(strlen(%0),strlen(%1))"
        args = f",{leaf_word()},{leaf_word()}"
    elif shape == 2:
        # Bare %N as an ifelse condition (#1157): xlate() truth, not atol().
        body = f"ifelse(%0,{branch_arm(colored)},{branch_arm(colored)})"
        args = f",{random.choice(['1', '0', 'abc', '#5', '0.5', '0abc', ''])}"
    elif shape == 3:
        body = gen_float_root()
        args = ""
    else:
        body = f"add(strlen(%0),1)"   # int body: the control shape
        args = f",{leaf_word()}"
    call = random.choice(["u", "ulocal"])
    inner = f"{call}(me/{name}{args})"
    return [f"&{name} me={body}"], random.choice(NEST_WRAPPERS).format(inner)


# Single-char delimiters and output separators (incl. multi-char osep, the
# class where ldelete/extract had bugs).  All parser-safe — no ;[]%(){}, or space.
DELIMS = ["-", "|", "@", ".", ":", "/"]
OSEPS = ["<>", "::", "%b~%b", "==", "-+-"]


def delim_list(d):
    # ~30% of elements are EMPTY: for a non-space delimiter, empty list
    # elements are real words (split_token semantics, #789) and several
    # walkers historically collapsed them.
    def elem():
        return "" if random.random() < 0.3 else random.choice(WORDS)
    # 1-element lists included: handle_sets special-cases two identical
    # single-element lists (it emits LIST1's copy; the general merge tie
    # takes LIST2's), which 2+-element lists can never reach.
    return d.join(elem() for _ in range(random.randint(1, 5)))


def gen_delim_test():
    """Single-call delim/osep exerciser with leaf lists built using the
    chosen delimiter (kept flat so the list/delimiter stay consistent)."""
    d = random.choice(DELIMS)
    lst = delim_list(d)
    pos = str(random.choice([1, 2, 3, -1, 0, 9]))
    w = leaf_word()
    osep = random.choice(OSEPS)
    fn = random.choice([
        f"ldelete({lst},{pos},{d})",
        f"ldelete({lst},{pos},{d},{osep})",
        f"replace({lst},{pos},{w},{d})",
        f"replace({lst},{pos},{w},{d},{osep})",
        f"insert({lst},{pos},{w},{d},{osep})",
        f"extract({lst},{pos},2,{d})",
        f"extract({lst},{pos},2,{d},{osep})",
        f"elements({lst},1 -1,{d},{osep})",
        f"remove({lst},{w},{d})",
        f"first({lst},{d})", f"rest({lst},{d})", f"last({lst},{d})",
        f"member({lst},{w},{d})",
        f"setunion({lst},{delim_list(d)},{d})",
        f"setdiff({lst},{delim_list(d)},{d},{osep})",
        f"setinter({lst},{delim_list(d)},{d})",
    ])
    return fn


def gen_root(colored):
    r = random.random()
    # ~20% of roots exercise custom delimiters / output separators.
    if r < 0.20:
        return gen_delim_test()
    # ~15% float arithmetic with runtime operands (real FP slots, #1159).
    if r < 0.35:
        return gen_float_root()
    # ~15% branch shapes with mixed-type PHI arms (#1143).
    if r < 0.50:
        return gen_branch(colored)
    # Otherwise force the root to be a (possibly nested) function call.
    while True:
        e = gen(random.choice(["S", "L"]), random.randint(1, 4), colored)
        if "(" in e:        # ensure it is actually a call, not a bare leaf
            return e


def longreg_case(i):
    """Long-register read shape (#996): a preamble command sets %q9 in
    BOTH processes (whichever route evaluates it, setq maintains the
    authoritative global_regs, which is what the NEXT command's entry
    marshal reads), then the measured expression reads %q9.  Lengths
    straddle the 256-byte SUBST_SLOT so both the entry-marshal decline
    (bail_longreg) and the normal short path are exercised.
    Mid-program setq-then-read shapes (the #996 step-2 surface: the
    long value is created INSIDE the measured program by SETQ_SYNC and
    read back through the long-bit diamond) run without a preamble.
    Returns (preamble_command, expression)."""
    n = random.choice([40, 120, 250, 255, 256, 260, 300, 500])
    pre = f"think [setq(9,repeat(x,{n}))]"
    shape = i % 6
    if shape == 0:
        e = "strcat(%q9,END)"
    elif shape == 1:
        e = "strcat(strlen(%q9),:,mid(%q9,5,7))"
    elif shape == 2:
        e = "strcat(before(%q9,zz),:,strlen(%q9))"
    elif shape == 3:
        e = "strcat(reverse(%q9),D)"
    elif shape == 4:
        pre = None
        e = f"strcat(setq(9,repeat(x,{n})),%q9,END)"
    else:
        pre = None
        e = f"strcat(setq(9,repeat(x,{n})),strlen(%q9))"
    return pre, e


def bracket_wrap(i, e, e2):
    """Wrap generated exprs in eval-bracket compositions (Phase 4 corpus:
    the jit_eval_brackets surface).  Shapes: bracket embedded in literal
    text, adjacent brackets, and a pure bracketed call."""
    shape = i % 3
    if shape == 0:
        return f"x [{e}] y"
    if shape == 1:
        return f"[{e}] [{e2}]"
    return f"[{e}]"


def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 200
    batch = int(sys.argv[2]) if len(sys.argv) > 2 else 50
    out = sys.argv[3] if len(sys.argv) > 3 else "."
    brackets = "--brackets" in sys.argv
    longreg = "--longreg" in sys.argv
    if "--utf8" in sys.argv:
        WORDS.extend(UTF8_WORDS)
    # Deterministic by default; override via SEED env for variety across runs.
    import os
    random.seed(int(os.environ.get("SEED", "1")))

    exprs = []
    preambles = []
    # Attribute definitions the MINIMIZER needs.  It replays a single
    # expression in a fresh process, so without these v(fz.*) is empty and
    # u(me/fz.uN) is undefined -- every nested finding would reduce to "no
    # divergence" and be misfiled as STATE-DEPENDENT.  Only "&" commands go
    # here: they are idempotent and order-independent, unlike the longreg
    # setq preamble, whose whole point is the preceding command's state.
    setup_cmds = list(FZ_SETUP)
    with open(f"{out}/manifest.txt", "w") as man:
        for i in range(count):
            pre = None
            if longreg and i % 5 == 2:
                p, e = longreg_case(i)
                pre = [p] if p is not None else None
            elif i % 7 == 3:
                # ~14% nested evaluation (#1157/#1159).  Not bracket-wrapped:
                # these carry their own outer call, which is what routes the
                # inner compile through the shared heap.
                pre, e = gen_nested(i, colored=(i % 3 == 0))
            else:
                e = gen_root(colored=(i % 3 == 0))
                if brackets:
                    e = bracket_wrap(i, e, gen_root(colored=False))
            exprs.append(e)
            preambles.append(pre)
            if pre:
                setup_cmds.extend(c for c in pre if c.startswith("&"))
            man.write(f"{i}\t{e}\n")

    with open(f"{out}/setup.txt", "w") as f:
        f.write("\n".join(setup_cmds) + "\n")

    # J and I sides are emitted into SEPARATE batch files: the J batches
    # run in a workspace whose conf may set jit_eval_brackets, while the
    # I batches always run with the toggle off, so the eval-bracket in
    # the @pemit arg reliably bails the JIT and the I side stays the
    # production interpreter route with production flags.  (An earlier
    # attempt forced the I side through asteval({...}) in-process, but
    # fun_asteval is not flag-faithful to the embedding context — e.g.
    # it trims a trailing space after an empty-yielding bracket that the
    # production route preserves — which manufactured false LOGIC
    # divergences.  Only the split-process design keeps the oracle
    # byte-faithful.)
    b = 0
    for start in range(0, count, batch):
        with open(f"{out}/bJ{b}.txt", "w") as fj, \
             open(f"{out}/bI{b}.txt", "w") as fi:
            # Shared runtime-value attributes, identical in both processes.
            # Every batch file is run against a freshly wiped DB, so these
            # must be re-stated per batch rather than once per corpus.
            for cmd in FZ_SETUP:
                fj.write(cmd + "\n")
                fi.write(cmd + "\n")
            for i in range(start, min(start + batch, count)):
                e = exprs[i]
                # State preamble (long-register and nested shapes):
                # identical in both processes so both sides see the same
                # registers and attribute bodies.
                if preambles[i] is not None:
                    for cmd in preambles[i]:
                        fj.write(cmd + "\n")
                        fi.write(cmd + "\n")
                # J side: JIT result captured via the @if condition into r(0).
                fj.write(
                    f"@if strlen(setr(0,{e}))="
                    f"{{@pemit #1=J~{i}~[sha1(r(0))]~[sha1(stripansi(r(0)))]~}},"
                    f"{{@pemit #1=J~{i}~[sha1(r(0))]~[sha1(stripansi(r(0)))]~}}\n")
                # I side: interpreter result (eval-bracket bails the JIT
                # in the toggle-off workspace this file runs in).
                fi.write(
                    f"@pemit #1=I~{i}~[sha1({e})]~[sha1(stripansi({e}))]~\n")
            fj.write("@wait 4=@shutdown\n")
            fi.write("@wait 4=@shutdown\n")
        b += 1
    print(f"{count} expressions in {b} batch(es) of {batch}")


if __name__ == "__main__":
    main()
