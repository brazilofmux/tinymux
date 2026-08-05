#!/usr/bin/env python3
#
# cpu_budget.py — live scenario test for max_cmdsecs / lag_limit (#1597).
#
# `alarm_clock.set(...)` is reached from exactly two places, both of which
# need a real server: the queue runner (cque.cpp:303) and the descriptor
# command path (net.cpp:2629).  muxscript reaches neither, and
# mux/script/mux_main.cpp explicitly does `alarm_clock.alarmed.store(false)`,
# so under the smoke harness the alarm is never armed and every route that
# honours it is untested.  tests/scenario is where the suite already goes
# when a path needs a real descriptor, so it is where this belongs.
#
# WHAT TO ASSERT, which is the part that is easy to get wrong (#1602):
#
# Both mechanisms fire, together, at the budget:
#
#     #-1 CPU LIMITED                        (ast.cpp:2182)
#     GAME: Expensive activity abbreviated.  (net.cpp:2638)
#
# ast.cpp:2182 emits its marker when a function is entered while the alarm is
# already set; safe_str has written it into the buffer by then, so the abort
# does not suppress it.  What the abort DOES lose is the literal text around
# it -- `@pemit me=TAG<expr>` comes back as a bare "#-1 CPU LIMITED", with no
# TAG< wrapper.  So an assertion anchored on a tag can never match on the
# aborted path and will burn its whole timeout, which is what #1602 was.
# These cases wait for the notice, which needs no wrapper to be recognizable.
#
# Timing was measured with a probe either side of drv_ProcessCommand: with
# lag_limit 1 the alarm fires at 1.001s, so these cases are fast and cannot
# hang the harness.
#
# The expensive expression's SIZE is measured on the running box rather than
# baked in -- see the note on CAL_SIZES (#2112).  A hardcoded size is a claim
# about how fast this tree evaluates softcode, and that claim goes stale every
# time someone lands a lowering.
#
# Driven by tests/scenario/run.sh.  Usage: cpu_budget.py [host] [port]

import socket
import sys
import time

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 6250

WIZ_LOGIN = "connect Wizard potrzebie"

# The expensive expression is SIZED AT RUNTIME, not baked in (#2112).
#
# It used to be a literal with N=600 and a comment saying "costs ~1.4s of
# evaluation" against a 1s budget.  The nested-iter work in #2072/#2076/#2078
# then made that shape ~13x cheaper -- measured at 0.09s -- so it completed in
# a twelfth of the budget, no abort was ever emitted, and the case sat failing
# for days.  `test-scenario` is opt-in and outside `make test`, so nothing
# noticed.
#
# Picking a bigger constant only moves the trap: the next iter() improvement
# stales it again, and a size that measures near the budget is flaky the day it
# lands.  So measure on the box we are actually running on, and choose a size
# whose real cost clears the budget by a wide margin.
#
# Every intermediate stays well inside LBUF and the expression terminates on
# its own, so the "completes under a high budget" control is a real completion
# rather than a second abort.  Deliberately NOT sized to need
# function_invocation_limit raised: doing that removes the guard that normally
# bounds runaway softcode, and expressions large enough to need it were
# measured running unbounded.
def expensive(n):
    return "[strlen(iter(lnum(1,%d),[strlen(iter(lnum(1,%d),##))]))]" % (n, n)


# Ladder of candidate sizes, cheapest first.  The top is bounded by LBUF, not
# by taste: the outer iter emits N copies of a 5-digit length plus separators,
# so N=5000 already produces ~30KB against a 32768-byte LBUF and N=6000 cannot
# fit.  Cost grows ~N^2 here.
CAL_SIZES = [600, 1200, 2000, 3000, 4000, 5000]

# How far over the budget the chosen size must measure.  3x keeps the abort
# unambiguous on a box that is merely busy, without needing a size near LBUF.
CAL_FACTOR = 3.0

ABORT_NOTICE = "Expensive activity abbreviated"

# The Lua route answers with the softcode token rather than the descriptor
# notice: lua() returns a value, so the abort surfaces through the module's
# m_bCpuLimited path (#1591).  Same budget, same text as the AST evaluator
# and the JIT.
CPU_LIMITED = "#-1 CPU LIMITED"

# A Lua pattern that backtracks exponentially: 25 lazy "a-" segments against
# 26 a's, with a "b" that never matches.  Small strings, no memory pressure,
# and every second of it is spent inside string.find -- a C function, where
# lua_sethook(LUA_MASKCOUNT) does not fire.  Before the step budget in
# lstrlib.c this ran past 45s with the process pinned at 100%.
LUA_RUNAWAY = ("local s=string.rep('a',26) return tostring(s:find('"
               + "a-" * 25 + "b'))")

# lag_limit 1 aborts at ~1.0s; 8s is slack for a loaded CI box without
# risking a long hang if the mechanism regresses.
ABORT_WAIT = 8.0
# The high-budget control needs to outlast a ~4.8s completion.
COMPLETE_WAIT = 30.0
HIGH_BUDGET = 120


def sendline(sock, line):
    sock.sendall(line.encode("utf-8") + b"\r\n")


def read_for(sock, marker, timeout):
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


def connect(login, timeout=2.5):
    sock = socket.socket()
    sock.settimeout(5)
    sock.connect((HOST, PORT))
    read_for(sock, None, 1.0)
    sendline(sock, login)
    read_for(sock, None, timeout)
    return sock


def probe(sock, tag, expr, timeout=5.0):
    """Evaluate and return the value inside tag<...>, or None if no reply."""
    sendline(sock, "@pemit me=%s<%s>" % (tag, expr))
    buf = read_for(sock, tag + "<", timeout)
    for line in buf.splitlines():
        if line.startswith(tag + "<") and line.rstrip().endswith(">"):
            return line.strip()[len(tag) + 1:-1]
    return None


def main():
    npass = nfail = 0

    def check(ok, msg, detail=""):
        nonlocal npass, nfail
        if ok:
            npass += 1
            print("ok %d - %s" % (npass + nfail, msg))
        else:
            nfail += 1
            print("not ok %d - %s%s"
                  % (npass + nfail, msg, (" (%s)" % detail) if detail else ""))

    try:
        wiz = connect(WIZ_LOGIN)
    except OSError as e:
        print("not ok - could not connect to %s:%d (%s)" % (HOST, PORT, e))
        return 1

    try:
        # 1. The knob takes.  Without this a failure below is ambiguous
        #    between "the budget is not enforced" and "the budget was never
        #    set", which is the ambiguity that made #1602 hard to read.
        sendline(wiz, "@admin lag_limit=1")
        read_for(wiz, None, 1.0)
        got = probe(wiz, "L", "[config(lag_limit)]")
        check(got == "1", "lag_limit reads back as 1 after @admin",
              "got=%r" % (got,))

        # 2. A cheap command is unaffected by a 1s budget.  Guards against a
        #    regression that aborts everything, which would make case 3 pass
        #    for the wrong reason.
        got = probe(wiz, "C", "[add(2,3)]")
        check(got == "5", "a cheap command is unaffected by a 1s budget",
              "got=%r" % (got,))

        # 3. Size the expensive expression against THIS box (#2112), with the
        #    budget out of the way so we are timing completion and not an
        #    abort.  This doubles as the control that cases 4-5 need: the
        #    chosen size is one we watched finish and return a number.
        sendline(wiz, "@admin lag_limit=%d" % HIGH_BUDGET)
        read_for(wiz, None, 1.0)

        chosen = chosen_cost = chosen_val = None
        for n in CAL_SIZES:
            t0 = time.monotonic()
            val = probe(wiz, "K%d" % n, expensive(n), COMPLETE_WAIT)
            dt = time.monotonic() - t0
            if val is None or not val.isdigit() or int(val) <= 0:
                # Ran out of LBUF (or the shape stopped working); the ladder
                # cannot go higher, so stop with what we have.
                print("# calibrate: N=%d returned %r — stopping the ladder"
                      % (n, val))
                break
            print("# calibrate: N=%-5d %6.3fs  result=%s" % (n, dt, val))
            chosen, chosen_cost, chosen_val = n, dt, val
            if dt >= CAL_FACTOR * 1.0:
                break

        if chosen is None or chosen_cost < CAL_FACTOR * 1.0:
            # SKIP rather than pass.  A silent pass here is exactly how this
            # case spent days not testing the budget: if nothing in range
            # costs more than the budget, we cannot make the alarm fire and
            # must say so instead of asserting on a completion.
            print("SKIP: no size in %r costs %.0fx a 1s budget on this box "
                  "(best: N=%s at %.3fs) — cannot exercise the abort."
                  % (CAL_SIZES, CAL_FACTOR, chosen, chosen_cost or 0.0))
            print("=== cpu-budget scenario: %d passed, %d failed, rest skipped ==="
                  % (npass, nfail))
            return 1 if nfail else 0

        EXPENSIVE = expensive(chosen)
        check(True, "calibrated: N=%d costs %.2fs, %.1fx a 1s budget"
                    % (chosen, chosen_cost, chosen_cost))

        # 4. THE case.  An expensive command is cut short, and the player is
        #    told so.  Wait on ABORT_NOTICE, not on a TAG< marker: the abort
        #    still delivers "#-1 CPU LIMITED", but the TAG wrapping is lost.
        sendline(wiz, "@admin lag_limit=1")
        read_for(wiz, None, 1.0)
        t0 = time.monotonic()
        sendline(wiz, "@pemit me=X<%s>" % EXPENSIVE)
        buf = read_for(wiz, ABORT_NOTICE, ABORT_WAIT)
        elapsed = time.monotonic() - t0
        check(ABORT_NOTICE in buf,
              "an expensive command is stopped by the 1s budget",
              "no notice within %.1fs; unbudgeted this size costs %.2fs"
              % (elapsed, chosen_cost))

        # 5. And it was stopped promptly rather than merely finishing.  The
        #    bar is the measured unbudgeted cost, not a constant: the abort
        #    has to beat the expression finishing on its own, which is the
        #    only thing that distinguishes "stopped" from "was quick".
        check(elapsed < chosen_cost,
              "the stop happens on the budget, not on completion",
              "took %.1fs with a 1s budget, but the expression only costs "
              "%.2fs unbudgeted" % (elapsed, chosen_cost))

        # 6. Control: the identical expression completes under a high budget
        #    and returns a number.  Without this, cases 4-5 would also pass
        #    if the expression were simply broken or unsupported.  Taken from
        #    the calibration run rather than paying for it twice.
        sendline(wiz, "@admin lag_limit=%d" % HIGH_BUDGET)
        read_for(wiz, None, 1.0)
        got = chosen_val
        check(got is not None and got.isdigit() and int(got) > 0,
              "the same command completes under a %ds budget" % HIGH_BUDGET,
              "got=%r" % (got,))

        # 6-9. The same budget on the Lua route (#1591).  The instruction
        #      hook cannot reach a C function, so string.find was unbounded
        #      even after the alarm poll landed; lstrlib.c now carries a step
        #      budget beside its existing recursion-depth guard.
        #
        #      Control first, and a skip rather than a failure if lua() is
        #      not available in this build -- an absent module is not a
        #      regression, and a test that cannot tell the two apart is worse
        #      than no test.
        sendline(wiz, "@admin lag_limit=1")
        read_for(wiz, None, 1.0)
        sendline(wiz, "&CPUOK me=return 42")
        read_for(wiz, None, 0.6)
        got = probe(wiz, "LO", "[lua(me/CPUOK)]")
        if got != "42":
            print("# lua() unavailable here (got %r) -- skipping Lua cases"
                  % (got,))
        else:
            check(True, "lua() answers under a 1s budget (control)")

            sendline(wiz, "&CPUPAT me=%s" % LUA_RUNAWAY)
            read_for(wiz, None, 0.6)

            # Wait on the token, not a TAG< marker, for the same reason as
            # case 3: the aborted command loses the literal text around its
            # result.  That is exactly what #1602 got wrong.
            t0 = time.monotonic()
            sendline(wiz, "@pemit me=P<[lua(me/CPUPAT)]>")
            buf = read_for(wiz, CPU_LIMITED, ABORT_WAIT)
            elapsed = time.monotonic() - t0
            check(CPU_LIMITED in buf,
                  "a runaway Lua pattern is stopped by the 1s budget",
                  "no CPU-limited answer within %.1fs" % elapsed)

            check(elapsed < 6.0,
                  "the Lua stop happens on the budget, not on completion",
                  "took %.1fs with a 1s budget" % elapsed)

            # And the budget did not cost ordinary pattern matching.  Without
            # this, the two cases above would also pass if the step budget
            # simply broke string.find outright.
            sendline(wiz, "&CPUFIND me=return tostring(string.find("
                          "'hello world','wor'))")
            read_for(wiz, None, 0.6)
            got = probe(wiz, "LF", "[lua(me/CPUFIND)]")
            check(got == "7",
                  "an ordinary Lua pattern is unaffected by the budget",
                  "got=%r" % (got,))

    finally:
        # Leave the budget where the rest of the suite expects it.  Other
        # drivers share this server.
        try:
            sendline(wiz, "@admin lag_limit=%d" % HIGH_BUDGET)
            read_for(wiz, None, 1.0)
            wiz.close()
        except OSError:
            pass

    print("=== cpu-budget scenario: %d passed, %d failed ===" % (npass, nfail))
    return 1 if nfail else 0


if __name__ == "__main__":
    sys.exit(main())
