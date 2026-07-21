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

Usage: gen.py <count> <batch_size> <out_dir>  (also writes <out_dir>/manifest.txt)
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
    # ~25% of roots exercise custom delimiters / output separators.
    if random.random() < 0.25:
        return gen_delim_test()
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
    Mid-program setq-then-read of long values is deliberately NOT
    generated until the #996 step-2 fix lands.
    Returns (preamble_command, expression)."""
    n = random.choice([40, 120, 250, 255, 256, 260, 300, 500])
    pre = f"think [setq(9,repeat(x,{n}))]"
    shape = i % 4
    if shape == 0:
        e = "strcat(%q9,END)"
    elif shape == 1:
        e = "strcat(strlen(%q9),:,mid(%q9,5,7))"
    elif shape == 2:
        e = "strcat(before(%q9,zz),:,strlen(%q9))"
    else:
        e = "strcat(reverse(%q9),D)"
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
    with open(f"{out}/manifest.txt", "w") as man:
        for i in range(count):
            pre = None
            if longreg and i % 5 == 2:
                pre, e = longreg_case(i)
            else:
                e = gen_root(colored=(i % 3 == 0))
                if brackets:
                    e = bracket_wrap(i, e, gen_root(colored=False))
            exprs.append(e)
            preambles.append(pre)
            man.write(f"{i}\t{e}\n")

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
            for i in range(start, min(start + batch, count)):
                e = exprs[i]
                # State preamble (long-register shapes): identical in
                # both processes so both sides read the same registers.
                if preambles[i] is not None:
                    fj.write(preambles[i] + "\n")
                    fi.write(preambles[i] + "\n")
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
