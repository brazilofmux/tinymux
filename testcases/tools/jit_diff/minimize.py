#!/usr/bin/env python3
"""Shrink JIT-vs-interpreter divergent expressions to minimal reproducers.

Reads divergent expressions (one per line) on stdin, greedily replaces
function-call subtrees with typed leaves while the divergence persists
(testing candidates by actually running them through muxscript), then prints
the deduplicated minimal reproducers with their JIT and interpreter values.

Driven by run.sh; needs env JITDIFF_WORK (scratch dir with bin/ + exp.conf)
and JITDIFF_MUX (muxscript invocation prefix, e.g. "timeout 90 /path/muxscript").
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen import FUNCS  # noqa: E402  (typed arg signatures)

WORK = os.environ["JITDIFF_WORK"]
MUX = os.environ["JITDIFF_MUX"].split()


def split_args(s):
    """Split a comma-separated arg list at paren depth 0."""
    out, depth, start = [], 0, 0
    for i, c in enumerate(s):
        if c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
        elif c == ',' and depth == 0:
            out.append(s[start:i])
            start = i + 1
    out.append(s[start:])
    return out


def parse_call(s):
    """Return (name, [args]) for 'name(...)', else (None, None)."""
    s = s.strip()
    p = s.find('(')
    if p <= 0 or not s.endswith(')'):
        return None, None
    name = s[:p]
    if not name.replace('_', '').isalnum():
        return None, None
    return name, split_args(s[p + 1:-1])


def typed_leaf(t):
    return {"I": "1", "i": "1", "W": "x"}.get(t, "ab cd")


def subtrees(expr):
    """Yield every nested function-call sub-expression (for hoisting)."""
    name, args = parse_call(expr)
    if name is None:
        return
    for a in args:
        if parse_call(a)[0] is not None:
            yield a.strip()
        yield from subtrees(a)


def variants(expr):
    """Yield simpler forms: (a) hoist each call-valued subtree to the root,
    and (b) replace each call-valued argument with a typed leaf (recursively).
    Hoisting collapses wrappers like repeat(cat(...,rjust(ab,1)),3) to
    rjust(ab,1); leaf-replacement strips a buggy call's complex arguments."""
    # (a) Hoist subtrees — the biggest reductions.
    yield from subtrees(expr)

    # (b) Replace call-valued arguments with typed leaves.
    name, args = parse_call(expr)
    if name is None:
        return
    sig = FUNCS.get(name.lower())
    argtypes = sig[1] if sig else ["S"] * len(args)
    for i, a in enumerate(args):
        t = argtypes[i] if i < len(argtypes) else "S"
        if parse_call(a)[0] is not None:
            na = args[:]
            na[i] = typed_leaf(t)
            yield f"{name}({','.join(na)})"
        for sub in variants(a):
            na = args[:]
            na[i] = sub
            yield f"{name}({','.join(na)})"


def run_batch(exprs):
    """Run exprs through muxscript; return {idx: (jit_strip, int_strip)}."""
    lines = []
    for i, e in enumerate(exprs):
        lines.append(
            f"@if strlen(setr(0,{e}))="
            f"{{@pemit #1=J~{i}~[sha1(stripansi(r(0)))]~}},"
            f"{{@pemit #1=J~{i}~[sha1(stripansi(r(0)))]~}}")
        lines.append(f"@pemit #1=I~{i}~[sha1(stripansi({e}))]~")
    lines.append("@wait 4=@shutdown")
    bf = os.path.join(WORK, "min_batch.txt")
    with open(bf, "w") as f:
        f.write("\n".join(lines) + "\n")
    for f in os.listdir(os.path.join(WORK, "data")):
        if f.startswith("exp.sqlite"):
            os.remove(os.path.join(WORK, "data", f))
    with open(bf) as inp:
        out = subprocess.run(MUX + ["-g", WORK, "-c", "exp.conf"],
                             stdin=inp, capture_output=True, text=True).stdout
    res = {}
    for ln in out.splitlines():
        for tag in ("J", "I"):
            pre = tag + "~"
            if pre in ln:
                seg = ln[ln.index(pre):]
                parts = seg.split("~")
                if len(parts) >= 3:
                    idx = parts[1]
                    res.setdefault(idx, {})[tag] = parts[2]
    diff = {}
    for idx, d in res.items():
        if "J" in d and "I" in d:
            diff[int(idx)] = (d["J"], d["I"], d["J"] != d["I"])
    return diff


def diverges(expr):
    d = run_batch([expr])
    return 0 in d and d[0][2]


def shrink(expr):
    cur = expr
    improved = True
    while improved:
        improved = False
        cands = list(dict.fromkeys(variants(cur)))  # unique, keep order
        if not cands:
            break
        res = run_batch(cands)
        diverging = [(len(cands[i]), cands[i]) for i in res if res[i][2]]
        if diverging:
            best = min(diverging)[1]
            if len(best) < len(cur):
                cur = best
                improved = True
    return cur


def show_value(expr):
    """Return the stripansi'd JIT and interp values for display."""
    bf = os.path.join(WORK, "min_val.txt")
    with open(bf, "w") as f:
        f.write(
            f"@if strlen(setr(0,{expr}))="
            f"{{@pemit #1=VJ~<[stripansi(r(0))]>}},"
            f"{{@pemit #1=VJ~<[stripansi(r(0))]>}}\n")
        f.write(f"@pemit #1=VI~<[stripansi({expr})]>\n")
        f.write("@wait 3=@shutdown\n")
    for fn in os.listdir(os.path.join(WORK, "data")):
        if fn.startswith("exp.sqlite"):
            os.remove(os.path.join(WORK, "data", fn))
    with open(bf) as inp:
        out = subprocess.run(MUX + ["-g", WORK, "-c", "exp.conf"],
                             stdin=inp, capture_output=True, text=True).stdout
    vj = vi = "?"
    for ln in out.splitlines():
        if "VJ~<" in ln:
            vj = ln[ln.index("VJ~<") + 4:].rsplit(">", 1)[0]
        elif "VI~<" in ln:
            vi = ln[ln.index("VI~<") + 4:].rsplit(">", 1)[0]
    return vj, vi


def main():
    seen = {}
    for line in sys.stdin:
        e = line.strip()
        if not e:
            continue
        m = shrink(e)
        seen.setdefault(m, e)  # first original that reduced to this minimal
    print(f"{len(seen)} distinct minimal reproducer(s):")
    for m in sorted(seen, key=len):
        vj, vi = show_value(m)
        print(f"  {m}")
        print(f"      JIT=<{vj}>  INTERP=<{vi}>")


if __name__ == "__main__":
    main()
