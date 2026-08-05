#!/usr/bin/env python3
"""
driver.py — measure how evaluation cost GROWS with input size, and assert the
            growth class rather than any absolute number.

Why a growth test and not a threshold test
------------------------------------------
tests/perf compares ns/call against a per-machine baseline, so it needs one
baseline per box and its tolerances had to be hand-calibrated (#2046).  An
exponent needs neither.  Doubling N costs 2.0x if the algorithm is linear and
4.0x if it is quadratic on EVERY machine -- fast or slow, busy or idle --
because the hardware cancels out of the ratio.  That makes this the portable
half of performance coverage: it cannot tell you the server got 10% slower, but
it can tell you an O(n) loop became an O(n^2) one, which is the failure that
actually ruins a live game.

Runs under muxscript, so there is no network, no port and no server lifecycle.

Two routes, each read from the instrument that is honest about it
----------------------------------------------------------------
  interp   astbench()'s `ast=` field.  fun_astbench calls ast_eval_node
           directly on the parsed tree, so the construct really does run on
           the interpreter.
  jit      rvbench()'s `cached=` field -- compile cache plus block cache, which
           is what a warm server executes.  rvbench returns
           "#-1 COMPILATION FAILED" when the expression will not compile, so a
           bail cannot be mistaken for a fast result.

Deliberately NOT used, and both of these have already misled us:

  * astbench's `jit=` field.  fun_astbench calls jit_eval unconditionally, and
    when the lowerer has no case for the construct jit_eval returns false
    immediately -- so the timing loop measures a function that does nothing and
    reports a spectacular number.  citer() reads a flat 2.7us at every N
    through that field, which is not speed, it is absence (CITER has no case in
    hir_lower.cpp).  astbench cannot notice: its `result=` comes from a third
    ast_eval_node call, never from the JIT.
  * rvbench's `native=` field.  It calls mux_exec, which dispatches to the JIT
    for any JIT-eligible expression -- and iter() is eligible.  So for iter()
    it is a SECOND measurement of the JIT, differing from `cached=` only by
    mux_exec's per-call dispatch overhead (strlen, AST-cache lookup, the
    jit_can_handle tree walk).  Reading it as "the interpreter" is exactly what
    made #2052 look like a defect shared by both routes when only one route has
    it.

Tags are suffixes, not prefixes
-------------------------------
fun_astbench resets the caller's output cursor with `*bufc = buff` after
warming the compile cache -- the start of the whole buffer, not the point where
astbench began writing.  So `think TAG [astbench(...)]` loses TAG.  Emitting
the tag after the call sidesteps it on trees that do not have the fix.

Reading the output
------------------
Ratios are per DOUBLING of N: 1.0x constant, 2.0x linear, 4.0x quadratic.  A
ratio near 1.0 usually means the measurement never rose above fixed overhead,
so nothing is classified unless its smallest-N time clears FLOOR_US.
"""

import os
import re
import subprocess
import sys
import tempfile
import shutil

# A case is not classified unless its smallest-N measurement is at least this
# many microseconds.  Below it the number is dominated by per-call overhead and
# everything reads as "constant" -- which is how a quadratic loop passes a
# growth test.  Linear is 2.0x; a 1.0x reading is usually evidence the
# measurement is broken, not evidence the code is fast.
FLOOR_US = 20.0

# Growth classes as (low, high) bounds on the fitted EXPONENT b in t ~ N**b.
# 0 is constant, 1 is linear, 2 is quadratic.
#
# The exponent is fitted by least squares across every point rather than read
# off consecutive ratios, because one bad point must not decide the verdict.
# The first case measured in a muxscript process pays cold compile-cache and
# allocator costs the later ones do not; with per-ratio bands that alone
# produced a 1.42x first step on an otherwise clean 1.97x/2.03x line and failed
# the run.  A perf test that fails on a busy laptop gets disabled, and a
# disabled test is worse than none.
#
# The bands are wide and do NOT touch: a genuinely noisy measurement lands in a
# gap and is reported unclassified, rather than being quietly promoted into a
# neighbouring class.
CLASSES = {
    "constant":  (-0.30, 0.35),
    "linear":    (0.72, 1.38),
    "quadratic": (1.70, 2.40),
}

# Shape probes.  NEVER time a generated list without first counting it.
# `repeat(a ,N)` trims the trailing space inside the argument and yields ONE
# element, so an iter() over it ran once regardless of N and looked beautifully
# flat.  And an LBUF is 32768 bytes: lnum(8000) is about 39KB of digits, so it
# silently truncates and the "list of 8000" is nothing of the kind.  A growth
# measurement over input that is not the size it claims is worse than no
# measurement, because it looks like a result.
#
# Keys are matched as literal substrings of the case template, so they must
# name the generator precisely enough to tell two uses of the same function
# apart: `repeat(a,N)` builds an N-BYTE string with no words in it, while
# `repeat(a%b,N)` builds an N-WORD list.  Keying on "repeat" alone would check
# the wrong one and still pass, which is the failure mode this whole section
# exists to prevent.
#
# probe expression -> expected value, evaluated at every N the case uses.
PROBES = {
    "lnum(":       ("words(lnum({N}))",         "{N}"),
    "repeat(a,":   ("strlen(repeat(a,{N}))",    "{N}"),
    "repeat(a%b,": ("words(repeat(a%b,{N}))",   "{N}"),
}

# (expression template, route, sizes, expected class, xfail issue or None)
#
# `xfail` records a growth class we know is wrong and have not yet fixed.  It
# does not silence the case: an xfail that starts PASSING fails the run, because
# that means the fix landed and this table is now lying.
CASES = [
    # --- regression guards for #2052 (fixed) ----------------------------
    # iter() on the JIT was quadratic three times over: element i was
    # re-extracted from the head of the list (EXTRACT per element), the
    # accumulator was rebuilt by STRCAT every iteration (and copied again
    # by the string PHI), and split_token strlen'd the whole list per
    # element.  Fixed by the cursor walk + the pinned-buffer APPEND
    # accumulator (#2072).  Both routes must now read linear; the two lines
    # are the same expression over the same list, so each is the other's
    # tightest control.
    ("iter(lnum({N}),1)",  "interp", [500, 1000, 2000, 4000], "linear", None),
    ("iter(lnum({N}),1)",  "jit",    [500, 1000, 2000, 4000], "linear", None),

    # --- controls -------------------------------------------------------
    # A list walk with no per-element evaluation.  Present so that a change
    # making EVERY case read quadratic is visibly the harness's fault and not
    # the server's -- and so the jit column is known to be capable of reading
    # linear at all.
    # Sizes start at 1000: below that ladd() does not clear FLOOR_US, and a
    # control that always SKIPs controls nothing.  They stop at 4000 because
    # lnum() output has to fit in one LBUF -- see PROBES.
    ("ladd(lnum({N}))",    "interp", [1000, 2000, 4000], "linear", None),
    ("ladd(lnum({N}))",    "jit",    [1000, 2000, 4000], "linear", None),

    # --- regression guard for a fixed defect ----------------------------
    # shuffle() was O(n^2) in word count (#2057): its single-char-delimiter
    # branch called co_extract per word, and co_extract addresses word i by
    # re-walking from the head.  Fixed by building the word table once with
    # co_split_words.  1.92 -> 0.92; at N=16000, 731 822us -> 384us.
    #
    # Interpreter route only, and both reasons are worth recording rather than
    # discovering again:
    #   * rvbench returns "#-1 COMPILATION FAILED" here, so the jit route
    #     cannot reach the sizes that clear FLOOR_US.  The cause is #2070:
    #     any expression containing a repeat() whose OUTPUT reaches 12256
    #     bytes hard-fails to compile.  Nothing to do with shuffle -- bare
    #     repeat(a%b,8000) fails identically, and shuffle() over the same
    #     8000 words supplied from an attribute compiles fine.  The boundary
    #     is a byte count, not a word count: it moves with the unit width
    #     (6127 ok / 6128 fail at 2 bytes per word, 4085 / 4090 at 3), which
    #     is why N=8000 looked like the threshold and is not.
    #   * below 8000 the jit numbers track the interpreter's within ~5%,
    #     because the lowerer has no SHUFFLE case and ecalls straight back
    #     into fun_shuffle.  It would be the same code measured twice.
    #
    # 16000 words is 32000 bytes, which fits one LBUF with ~700 to spare --
    # the probe is what proves that rather than this comment.
    ("shuffle(repeat(a%b,{N}))", "interp", [2000, 4000, 8000, 16000],
     "linear", None),

    # citer() splits on code points instead of a delimiter and has no case in
    # hir_lower.cpp, so it is interpreter-only.  It covers the loop shape
    # iter() shares without iter()'s element extraction.
    ("citer(repeat(a,{N}),1)", "interp", [500, 1000, 2000, 4000], "linear", None),

    # map() with a computation body, ahead of its lowering (#2080).  Both
    # routes are linear TODAY (~610ns/element interp, ~1.1us/element on the
    # jit route via the ECALL back into fun_map); these guard the class so
    # the lowering -- whose promise is a smaller constant, which this
    # harness deliberately does not measure -- cannot regress the exponent.
    # The attribute comes from SETUP.
    ("map(me/GROWTH.MAPB,lnum({N}))", "interp", [250, 500, 1000, 2000], "linear", None),
    ("map(me/GROWTH.MAPB,lnum({N}))", "jit",    [250, 500, 1000, 2000], "linear", None),

    # The INLINE path (#2080): a literal #dbref/attr with executor == thing
    # is the one shape the MAP lowering compiles.  #1 is the Wizard and is
    # stable in every database, and muxscript runs as #1, so this exercises
    # the inlined body + pinned accumulator rather than the ECALL fallback
    # the me/ spelling takes (name references never inline).
    #
    # Sizes start at 500 and run to 4000 (the lnum()-in-one-LBUF ceiling)
    # because this path carries ~73us of per-call fixed cost -- gate ECALL,
    # CARGS save/restore, WORDS -- which drags the log-log exponent below
    # the linear band when small sizes dominate the fit (b=0.68 over
    # 250..2000, ~13% fixed share at the top size; b~0.95 over 500..4000,
    # ~7%).  Size cases so fixed costs amortize; do NOT loosen the class
    # bands instead -- a guard that accepts sub-linear cannot tell a fixed
    # cost amortizing from the case silently ceasing to do the work, which
    # is one of the two failures this harness exists to catch (#2094
    # review).
    ("map(#1/GROWTH.MAPB,lnum({N}))", "jit", [500, 1000, 2000, 4000], "linear", None),

    # filter() (#2080): same inline shape as MAP plus the keep diamond and
    # the kept-count register; same fixed-cost amortization argument for the
    # sizes.  The predicate keeps all but three elements, so the append path
    # is exercised at nearly every iteration.
    ("filter(me/GROWTH.FILP,lnum({N}))", "interp", [500, 1000, 2000, 4000], "linear", None),
    # Restored with #2106.  This case was held out because rvbench()ing an
    # inlined program and then a fun_map/fun_filter-ECALL program in the same
    # process crashed (or, on glibc, silently ran the outer program against
    # the wrong guest buffer): run_compiled took s_vm[0] without claiming it,
    # so the nested per-element mux_exec re-entered run_cached_program, read a
    # depth still at 0, picked the same context and reset the DBT under the
    # outer run's live frames.  Its presence here is a regression test for
    # that -- the harness runs every case in ONE muxscript, so a recurrence
    # reports UNMEASURED for everything after this line, which is how #2106
    # was found in the first place.
    ("filter(#1/GROWTH.FILP,lnum({N}))", "jit", [500, 1000, 2000, 4000], "linear", None),

    # fold() (#2080).  A numeric reduction, so the accumulator stays short
    # and the inline arm runs at every element -- the string-building case
    # that crosses the 256-byte CARGS limit is covered for CORRECTNESS by
    # fold_fn TC007, not here, because its per-element ECALL fallback is a
    # constant-factor story rather than a complexity one.
    ("fold(me/GROWTH.FOLDB,lnum({N}),0)", "interp", [500, 1000, 2000, 4000], "linear", None),
    ("fold(#1/GROWTH.FOLDB,lnum({N}),0)", "jit", [500, 1000, 2000, 4000], "linear", None),
]

AST_RE = re.compile(r"ast=([0-9.]+)us")
CACHED_RE = re.compile(r"cached=([0-9.]+)ns/call")
TAG_RE = re.compile(r"^(.*)\s+@@(\w+)\.([ARP])\s*$")


def iters_for(n, base):
    return max(5, int(base / n))


def probes_for(cases):
    """(tag, probe expr, expected) for every generator/N actually used."""
    # Tags are built from the generator's INDEX, not its name: a key like
    # "repeat(a%b," is not usable as a tag, and hash() is per-process
    # randomised.
    order = list(PROBES.keys())
    out, seen = [], set()
    for tmpl, _route, sizes, _exp, _xf in cases:
        for gi, gen in enumerate(order):
            if gen not in tmpl:
                continue
            pexpr, pwant = PROBES[gen]
            for n in sizes:
                key = (gi, n)
                if key in seen:
                    continue
                seen.add(key)
                out.append(("p%dg%d" % (n, gi),
                            pexpr.format(N=n), pwant.format(N=n)))
    return out


# Commands emitted once at the top of the muxscript stream, before probes.
# Direct stdin lines are NOT %-substituted, so attribute bodies here keep
# their %0 verbatim.
SETUP = [
    "&GROWTH.MAPB me=[add(%0,1)]",
    "&GROWTH.FILP me=[gt(%0,3)]",
    "&GROWTH.FOLDB me=[add(%0,%1)]",
]


def build_script(pairs, probes):
    """One muxscript command stream covering every probe and (tag, expr) pair."""
    lines = list(SETUP)
    for tag, expr, _want in probes:
        lines.append("think [%s] @@%s.P" % (expr, tag))
    # Warm-up, discarded.  The first measurement in a muxscript process pays
    # cold compile-cache, block-cache and allocator costs the rest do not, and
    # whichever case happened to be first would carry a depressed smallest-N
    # time -- inflating its first ratio and understating its exponent.  Run
    # every expression once before anything is recorded so no case is
    # penalised for its position in the list.
    for tag, expr in pairs:
        lines.append("think [astbench(%s,3)] @@warm.W" % expr)
        lines.append("think [rvbench(%s,3)] @@warm.W" % expr)
    for tag, expr in pairs:
        lines.append("think [astbench(%s,%d)] @@%s.A" % (expr, iters_for_expr(expr, 200000), tag))
        lines.append("think [rvbench(%s,%d)] @@%s.R" % (expr, iters_for_expr(expr, 20000), tag))
    lines.append("@shutdown")
    return "\n".join(lines) + "\n"


def iters_for_expr(expr, base):
    m = re.search(r"(\d{2,})", expr)
    return iters_for(int(m.group(1)) if m else 100, base)


def parse(out):
    """tag -> {route: microseconds}.  Failed measurements are simply absent."""
    got = {}
    for line in out.splitlines():
        m = TAG_RE.match(line)
        if not m:
            continue
        body, tag, kind = m.group(1), m.group(2), m.group(3)
        slot = got.setdefault(tag, {})
        if "#-1" in body:
            slot["error"] = body.strip()[:60]
            continue
        if kind == "P":
            slot["probe"] = body.strip()
        elif kind == "A":
            mm = AST_RE.search(body)
            if mm:
                slot["interp"] = float(mm.group(1))
        else:
            mm = CACHED_RE.search(body)
            if mm:
                slot["jit"] = float(mm.group(1)) / 1000.0
    return got


def exponent(sizes, times):
    """Least-squares slope of log2(time) against log2(N): the b in t ~ N**b."""
    import math
    xs = [math.log(n, 2) for n in sizes]
    ys = [math.log(t, 2) for t in times]
    mx = sum(xs) / len(xs)
    my = sum(ys) / len(ys)
    num = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
    den = sum((x - mx) ** 2 for x in xs)
    return num / den if den else 0.0


def classify(b):
    """The class whose band contains the fitted exponent, or None."""
    for name, (lo, hi) in CLASSES.items():
        if lo <= b <= hi:
            return name
    return None


def run_muxscript(repo, binary, script):
    work = tempfile.mkdtemp(prefix="growth.")
    try:
        os.mkdir(os.path.join(work, "g.d"))
        os.symlink(binary, os.path.join(work, "bin"))
        for f in ("alias.conf", "compat.conf"):
            shutil.copy(os.path.join(repo, "mux", "game", f), work)
        with open(os.path.join(work, "g.conf"), "w") as fh:
            fh.write(
                "input_database g.d/g.db\n"
                "output_database g.d/g.db.new\n"
                "crash_database g.d/g.db.CRASH\n"
                "mail_database g.d/mail.db\n"
                "comsys_database g.d/comsys.db\n"
                "port 2861\n"
                "mud_name GrowthMUX\n"
                "command_quota_increment 1000000\n"
                "command_quota_max 1000000\n"
                "player_queue_limit 100000\n"
                "function_invocation_limit 1000000\n"
                "include alias.conf\n"
                "include compat.conf\n"
            )
        env = dict(os.environ)
        env["LD_LIBRARY_PATH"] = binary
        env["DYLD_LIBRARY_PATH"] = binary
        proc = subprocess.run(
            [os.path.join(binary, "muxscript"), "-g", ".", "-c", "g.conf"],
            input=script, cwd=work, env=env,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            universal_newlines=True, errors="replace", timeout=3600,
        )
        return proc.stdout
    finally:
        shutil.rmtree(work, ignore_errors=True)


def main():
    repo = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    binary = os.path.join(repo, "mux", "game", "bin")
    if not os.access(os.path.join(binary, "muxscript"), os.X_OK):
        print("SKIP: %s/muxscript not found -- run 'make install' first." % binary)
        print("      Nothing was measured; this is not a pass.")
        return 0

    # Every case is measured in ONE muxscript process.  A case measured in a
    # different process than its neighbours is, as far as noise is concerned,
    # a case measured on a different machine.
    pairs, index = [], {}
    for ci, (tmpl, _route, sizes, _exp, _xf) in enumerate(CASES):
        for ni, n in enumerate(sizes):
            tag = "c%dn%d" % (ci, ni)
            index[(ci, ni)] = tag
            pairs.append((tag, tmpl.format(N=n)))

    probes = probes_for(CASES)
    out = run_muxscript(repo, binary, build_script(pairs, probes))
    got = parse(out)
    if not got:
        print("FAIL: muxscript produced no measurements.")
        print(out[-2000:])
        return 1

    # Shape first.  If the inputs are not the size they claim, every ratio
    # below is a number about something else.
    bad = []
    for tag, expr, want in probes:
        saw = got.get(tag, {}).get("probe")
        if saw != want:
            bad.append((expr, want, saw))
    if bad:
        print("FAIL: test input is not the shape it claims.")
        for expr, want, saw in bad:
            print("  %-28s expected %-8s got %s" % (expr, want, saw))
        print()
        print("  Nothing was timed.  An LBUF is 32768 bytes; a list that")
        print("  overflows it truncates silently and still looks like a list.")
        return 1
    print("=== input shape: %d probe(s) ok ===" % len(probes))
    print()

    print("=== growth per doubling of N ===")
    print("    1.0x constant   2.0x linear   4.0x quadratic")
    print()
    failures = 0
    for ci, (tmpl, route, sizes, expected, xfail) in enumerate(CASES):
        times = [got.get(index[(ci, ni)], {}).get(route) for ni in range(len(sizes))]
        label = "%-25s %-7s" % (tmpl.replace("{N}", "N"), route)

        if any(t is None for t in times):
            err = next((got.get(index[(ci, ni)], {}).get("error")
                        for ni in range(len(sizes))
                        if got.get(index[(ci, ni)], {}).get("error")), None)
            print("  %s  UNMEASURED %s" % (label, "(%s)" % err if err else ""))
            failures += 1
            continue
        if times[0] < FLOOR_US:
            print("  %s  SKIP: %.1fus at N=%d is under the %.0fus floor"
                  % (label, times[0], sizes[0], FLOOR_US))
            continue

        ratios = [times[i + 1] / times[i] for i in range(len(times) - 1)]
        b = exponent(sizes, times)
        actual = classify(b)
        detail = "N=%d..%d %8.1f->%-9.1fus b=%.2f [%s]" % (
            sizes[0], sizes[-1], times[0], times[-1], b,
            " ".join("%.2fx" % r for r in ratios))

        if actual == expected:
            if xfail:
                print("  %s  XPASS %s" % (label, detail))
                print("  %s        ^ #%d looks FIXED -- drop the xfail in CASES."
                      % (" " * 33, xfail))
                failures += 1
            else:
                print("  %s  ok    %s %s" % (label, detail, expected))
        else:
            name = actual or "unclassified"
            if xfail:
                print("  %s  xfail %s %s, want %s (#%d)"
                      % (label, detail, name, expected, xfail))
            else:
                print("  %s  FAIL  %s %s, want %s"
                      % (label, detail, name, expected))
                failures += 1

    print()
    if failures:
        print("FAIL: %d case(s)." % failures)
        return 1
    print("PASS: all cases grew as expected; known defects still xfail.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
