#!/usr/bin/env python3
# Live scenario test for translated-block retention across program
# alternation (#2129).
#
# Why this cannot be a smoke test: every other JIT measurement in the tree
# evaluates ONE expression repeatedly, which is the only workload where a
# one-program block cache ever hits.  The defect was invisible to all of
# them by construction.  This driver holds several distinct compiled
# expressions in flight at once and asserts on the mechanism directly:
#
#   1. Correctness under alternation — every expression returns its exact
#      value on every round, including after slot eviction has relocated
#      its code to a different guest base.  This is the differential check
#      on the JAL re-aiming: a relocation bug produces wrong output or a
#      dead server here, not a slow one.
#   2. Retention — with a working set within jit_code_slots, alternating
#      programs must NOT grow dbt_code_used (host bytes of translation).
#      Growth means re-translation, i.e. the #2129 behaviour is back.
#   3. Eviction — with a working set larger than the slot count, results
#      stay correct while slot_evict advances (the LRU path is exercised,
#      not just the warm path).
#   4. The jit_code_slots knob works at runtime — @admin jit_code_slots 1
#      restores the old one-program behaviour (dbt_code_used grows under
#      alternation again), which is the A/B lever for measurements.
#
# On a build without --enable-jit there is no mechanism to test; the
# driver reports that and passes vacuously, same convention as
# jit_perms.py.
#
# Driven by tests/scenario/run.sh.  Usage: jit_alternation.py [host] [port]

import socket
import sys
import time

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 6250

WIZ_LOGIN = "connect Wizard potrzebie"


def sendline(sock, line):
    sock.sendall(line.encode("utf-8") + b"\r\n")


def read_for(sock, marker, timeout=5.0):
    sock.settimeout(0.3)
    deadline = time.monotonic() + timeout
    buf = ""
    while time.monotonic() < deadline:
        try:
            data = sock.recv(8192)
            if not data:
                break
            buf += data.decode("utf-8", "replace")
            if marker is not None and marker in buf:
                return buf
        except socket.timeout:
            pass
    return buf


def connect(login, timeout=2.0):
    sock = socket.socket()
    sock.settimeout(5)
    sock.connect((HOST, PORT))
    read_for(sock, None, 1.0)
    sendline(sock, login)
    read_for(sock, None, timeout)
    return sock


def drain(sock):
    # Swallow anything already buffered so the next read cannot mistake a
    # stale reply for a fresh one.
    sock.settimeout(0.02)
    try:
        while sock.recv(65536):
            pass
    except socket.timeout:
        pass


def probe(sock, expr):
    # The compiled unit is the WHOLE evaluated argument, literal prefixes
    # included — a unique tag inside the @pemit text would make every probe
    # a distinct program and turn this into a test of an unbounded working
    # set (found the hard way: 41 evictions for a "4-program" phase).  So
    # the marker is FIXED, the expression text stays exactly stable across
    # rounds, and stale-reply protection comes from draining the socket
    # before sending: each probe waits for its own reply, so anything
    # buffered beforehand is leftover noise, never this probe's answer.
    drain(sock)
    sendline(sock, "@pemit me=RSLT<%s>" % expr)
    buf = read_for(sock, "RSLT<", 5.0)
    for line in buf.splitlines():
        line = line.strip()
        if line.startswith("RSLT<") and line.endswith(">"):
            return line[len("RSLT<"):-1]
    return None


def jitstats(sock):
    raw = probe(sock, "[jitstats()]") or ""
    stats = {}
    for tok in raw.split():
        if "=" in tok:
            k, _, v = tok.partition("=")
            try:
                stats[k] = int(v)
            except ValueError:
                pass
    return stats


# The issue's own probe shape: compiled ITER with native arithmetic, one
# distinct constant per program so every program has a distinct right
# answer.  iter(lnum(4), add(mul(##,2),C)) over "0 1 2 3" is
# "C C+2 C+4 C+6".
def expr_for(c):
    return "[iter(lnum(4),add(mul(##,2),%d))]" % c


def expected_for(c):
    return " ".join(str(2 * e + c) for e in range(4))


def main():
    npass = nfail = 0

    def check(ok, msg, detail=""):
        nonlocal npass, nfail
        if ok:
            npass += 1
            print("ok %d - %s" % (npass + nfail, msg))
        else:
            nfail += 1
            print("not ok %d - %s%s" % (npass + nfail, msg, detail))

    try:
        wiz = connect(WIZ_LOGIN)
    except OSError as e:
        print("not ok - could not connect to %s:%d (%s)" % (HOST, PORT, e))
        return 1

    stats = jitstats(wiz)
    if "eval_attempts" not in stats:
        print("# jit: not detected — nothing to test on this build")
        print("=== jit-alternation scenario: 0 passed, 0 failed (skipped) ===")
        return 0
    if "slot_hit" not in stats:
        print("not ok - jitstats() reports no slot counters; "
              "engine predates #2129?")
        return 1
    print("# jit: active (slots enabled)")

    # Make the A/B knob state explicit rather than trusting the default.
    sendline(wiz, "@admin jit_code_slots=7")
    read_for(wiz, None, 0.5)

    # --- Phase 1+2: retention within the slot count -----------------------
    # 4 distinct programs, working set < 7 slots.
    consts = [11, 230, 3007, 40001]

    # Warm: compile + translate + slot-assign each program.
    for c in consts:
        got = probe(wiz, expr_for(c))
        check(got == expected_for(c),
              "warmup C=%d evaluates correctly" % c,
              " (wanted %r, got %r)" % (expected_for(c), got))

    before = jitstats(wiz)
    rounds = 10
    for _ in range(rounds):
        for c in consts:
            got = probe(wiz, expr_for(c))
            if got != expected_for(c):
                check(False, "alternation C=%d" % c,
                      " (wanted %r, got %r)" % (expected_for(c), got))
                break
    else:
        check(True, "all %d alternating evaluations exact"
              % (rounds * len(consts)))
    after = jitstats(wiz)

    handled = after.get("eval_handled", 0) - before.get("eval_handled", 0)
    check(handled >= rounds * len(consts),
          "alternating evaluations took the compiled route",
          " (eval_handled +%d, wanted >= %d)"
          % (handled, rounds * len(consts)))

    # The mechanism claim: every switch found its program still resident.
    misses = after.get("slot_miss", 0) - before.get("slot_miss", 0)
    check(misses == 0,
          "every program switch kept its translations (slot_miss +0)",
          " (slot_miss +%d)" % misses)

    # Secondary bound.  Late BLOB blocks may still translate lazily (cold
    # tier-2 paths, e.g. a longer-digit itoa branch first taken as stat
    # values grow) — that is legitimate lazy work of a few hundred bytes.
    # Re-translating programs on every switch is ~5 KB per switch, two
    # orders of magnitude above this bound.
    used_growth = after.get("dbt_code_used", 0) - before.get("dbt_code_used", 0)
    check(0 <= used_growth < 4096,
          "no program re-translation across %d switches" % (rounds * len(consts)),
          " (dbt_code_used grew %d bytes)" % used_growth)

    evicts = after.get("slot_evict", 0) - before.get("slot_evict", 0)
    check(evicts == 0,
          "no slot evictions with a working set inside the slot count",
          " (slot_evict +%d)" % evicts)

    # --- Phase 3: working set larger than the slot count ------------------
    # 10 round-robin programs over 7 slots is the LRU's worst case: every
    # evaluation misses, every miss relocates into a reused slot.  The
    # assertion is correctness — the counters just prove the path ran.
    big = [500 + 7 * i for i in range(10)]
    ok_all = True
    for _ in range(3):
        for c in big:
            got = probe(wiz, expr_for(c))
            if got != expected_for(c):
                ok_all = False
                check(False, "oversized set C=%d" % c,
                      " (wanted %r, got %r)" % (expected_for(c), got))
    if ok_all:
        check(True, "oversized working set stays exact under eviction")
    evicted = jitstats(wiz)
    check(evicted.get("slot_evict", 0) > after.get("slot_evict", 0),
          "oversized working set exercised the eviction path",
          " (slot_evict %d -> %d)"
          % (after.get("slot_evict", 0), evicted.get("slot_evict", 0)))

    # --- Phase 4: the runtime knob restores the old behaviour -------------
    sendline(wiz, "@admin jit_code_slots=1")
    read_for(wiz, None, 0.5)

    knob_consts = [777, 888]
    for c in knob_consts:                     # warm under the new setting
        probe(wiz, expr_for(c))
    k_before = jitstats(wiz)
    for _ in range(4):
        for c in knob_consts:
            got = probe(wiz, expr_for(c))
            check_ok = got == expected_for(c)
            if not check_ok:
                check(False, "knob=1 C=%d" % c,
                      " (wanted %r, got %r)" % (expected_for(c), got))
    k_after = jitstats(wiz)
    k_growth = k_after.get("dbt_code_used", 0) - k_before.get("dbt_code_used", 0)
    check(k_growth > 0,
          "jit_code_slots=1 re-translates on alternation (old behaviour)",
          " (dbt_code_used grew %d bytes)" % k_growth)

    sendline(wiz, "@admin jit_code_slots=7")
    read_for(wiz, None, 0.5)

    # Programs must never be pinned by the relocation scan with current
    # codegen; nonzero means a lowering lost slotting silently.
    check(jitstats(wiz).get("slot_pinned", 0) == 0,
          "no program pinned by the relocation scan",
          " (slot_pinned=%d)" % jitstats(wiz).get("slot_pinned", 0))

    sendline(wiz, "QUIT")
    try:
        wiz.close()
    except OSError:
        pass

    print("=== jit-alternation scenario: %d passed, %d failed ==="
          % (npass, nfail))
    return 1 if nfail else 0


if __name__ == "__main__":
    sys.exit(main())
