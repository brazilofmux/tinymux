#!/usr/bin/env python3
"""Shrink JIT-vs-interpreter divergent expressions to minimal reproducers.

Reads divergent expressions (one per line) on stdin, greedily replaces
function-call subtrees with typed leaves while the divergence persists
(testing candidates by actually running them through muxscript), then prints
the deduplicated minimal reproducers with their JIT and interpreter values.

Isolation discipline (#784): candidates are screened in shared batches for
speed, but every ACCEPTED reduction is confirmed in a fresh single-expression
muxscript process, and the original expression is confirmed in isolation
before shrinking.  An expression that diverges only with other expressions'
JIT state in the process (the #778 class) is reported as STATE-DEPENDENT
with its original form instead of a misleading "minimal" reproducer whose
displayed values are equal.

Driven by run.sh; needs env JITDIFF_WORK (scratch dir with bin/ + exp.conf)
and JITDIFF_MUX_BIN (path to muxscript; may contain spaces).  Optional
JITDIFF_TIMEOUT (seconds per muxscript run, default 90).  The legacy
JITDIFF_MUX prefix string is still accepted as a fallback.
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen import FUNCS  # noqa: E402  (typed arg signatures)

WORK = os.environ["JITDIFF_WORK"]
if os.environ.get("JITDIFF_MUX_BIN"):
    MUX = [os.environ["JITDIFF_MUX_BIN"]]
else:
    # Legacy: a command-prefix string.  Splitting breaks on paths with
    # spaces; prefer JITDIFF_MUX_BIN.
    MUX = os.environ["JITDIFF_MUX"].split()
TIMEOUT = float(os.environ.get("JITDIFF_TIMEOUT", "90"))

# Candidates per muxscript process.  Mirrors run.sh's batching: large
# batches overflow the command queue ("Run away") and silently drop
# results, which reads as "does not diverge".
BATCH = 50


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


def run_mux(script_path, conf="exp.conf"):
    """Run one fresh muxscript process on a script file; return stdout."""
    for fn in os.listdir(os.path.join(WORK, "data")):
        if fn.startswith("exp.sqlite"):
            os.remove(os.path.join(WORK, "data", fn))
    with open(script_path) as inp:
        try:
            proc = subprocess.run(MUX + ["-g", WORK, "-c", conf],
                                  stdin=inp, capture_output=True, text=True,
                                  timeout=TIMEOUT)
            out = proc.stdout
        except subprocess.TimeoutExpired as e:
            out = e.stdout.decode("utf-8", "replace") if e.stdout else ""
            print("WARNING: muxscript timed out during minimization; "
                  "treating missing results as non-diverging.",
                  file=sys.stderr)
    if "Run away" in out:
        # Queue overflow: results past the limit were dropped and would
        # silently read as "does not diverge".
        print("WARNING: queue overflow ('Run away') in a minimizer batch — "
              "some candidates were not evaluated.", file=sys.stderr)
    return out


def run_chunk(exprs, base):
    """Run one chunk through fresh muxscript processes; return
    {global_idx: (j, i, differs)}.  base offsets the indices so chunks
    merge cleanly.

    J and I sides run in SEPARATE processes with separate confs,
    mirroring run.sh: the J side under exp.conf (which may enable
    jit_eval_brackets), the I side under int.conf (toggle always off,
    so the eval-bracket bail keeps the production interpreter route).
    Running both in one toggle-on process compared JIT against JIT and
    misclassified deterministic divergences as STATE-DEPENDENT."""
    jlines = []
    ilines = []
    for i, e in enumerate(exprs):
        gi = base + i
        jlines.append(
            f"@if strlen(setr(0,{e}))="
            f"{{@pemit #1=J~{gi}~[sha1(stripansi(r(0)))]~}},"
            f"{{@pemit #1=J~{gi}~[sha1(stripansi(r(0)))]~}}")
        ilines.append(f"@pemit #1=I~{gi}~[sha1(stripansi({e}))]~")
    jlines.append("@wait 4=@shutdown")
    ilines.append("@wait 4=@shutdown")
    jf = os.path.join(WORK, "min_batch_j.txt")
    iff = os.path.join(WORK, "min_batch_i.txt")
    with open(jf, "w") as f:
        f.write("\n".join(jlines) + "\n")
    with open(iff, "w") as f:
        f.write("\n".join(ilines) + "\n")
    out = run_mux(jf) + run_mux(iff, conf="int.conf")
    res = {}
    for ln in out.splitlines():
        for tag in ("J", "I"):
            pre = tag + "~"
            if pre in ln:
                seg = ln[ln.index(pre):]
                parts = seg.split("~")
                # Validate the index: stray J~/I~ text in evaluated output
                # must not crash the minimizer.
                if len(parts) >= 3 and parts[1].isdigit():
                    res.setdefault(parts[1], {})[tag] = parts[2]
    diff = {}
    for idx, d in res.items():
        if "J" in d and "I" in d:
            diff[int(idx)] = (d["J"], d["I"], d["J"] != d["I"])
    return diff


def run_batch(exprs):
    """Run exprs in chunks of BATCH, each in a fresh muxscript process;
    return {idx: (jit_sha, int_sha, differs)}."""
    diff = {}
    for base in range(0, len(exprs), BATCH):
        diff.update(run_chunk(exprs[base:base + BATCH], base))
    return diff


def diverges(expr):
    """Standalone check: does expr diverge alone in a fresh process?"""
    d = run_batch([expr])
    return 0 in d and d[0][2]


def shrink(expr):
    """Greedy reduction.  Candidates are screened in shared batches, but a
    reduction is only ACCEPTED after it diverges standalone, so batch-state
    artifacts cannot steer the result (#784)."""
    cur = expr
    improved = True
    while improved:
        improved = False
        cands = list(dict.fromkeys(variants(cur)))  # unique, keep order
        if not cands:
            break
        res = run_batch(cands)
        diverging = sorted((len(cands[i]), cands[i])
                           for i in res if res[i][2])
        for ln, cand in diverging:
            if ln >= len(cur):
                break
            if diverges(cand):
                cur = cand
                improved = True
                break
            # Batch artifact: diverged with neighbors, not alone — try the
            # next-shortest candidate.
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
    out = run_mux(bf)
    vj = vi = "?"
    for ln in out.splitlines():
        if "VJ~<" in ln:
            vj = ln[ln.index("VJ~<") + 4:].rsplit(">", 1)[0]
        elif "VI~<" in ln:
            vi = ln[ln.index("VI~<") + 4:].rsplit(">", 1)[0]
    return vj, vi


def main():
    seen = {}
    state_dep = []
    for line in sys.stdin:
        e = line.strip()
        if not e:
            continue
        # Confirm the original diverges in ISOLATION before shrinking.
        # If it only diverges alongside the rest of its fuzz batch, the
        # bug is order/state-dependent (the #778 class): shrinking in
        # isolated processes cannot reproduce it and would print a
        # "minimal" form whose JIT and interp values are equal.
        if not diverges(e):
            state_dep.append(e)
            continue
        seen.setdefault(shrink(e), e)
    print(f"{len(seen)} distinct minimal reproducer(s):")
    for m in sorted(seen, key=len):
        vj, vi = show_value(m)
        print(f"  {m}")
        print(f"      JIT=<{vj}>  INTERP=<{vi}>")
    if state_dep:
        print(f"{len(state_dep)} STATE-DEPENDENT expression(s) — diverged "
              "in the fuzz batch but NOT in isolation; reproduce by "
              "replaying the whole batch (b*.txt in the work dir):")
        for e in state_dep:
            print(f"  {e}")


if __name__ == "__main__":
    main()
