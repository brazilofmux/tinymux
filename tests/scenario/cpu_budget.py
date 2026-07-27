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
# A runaway command typed at a descriptor does NOT come back as
# "#-1 CPU LIMITED".  It is abandoned, and the wrapper tells the player
#
#     GAME: Expensive activity abbreviated.        (net.cpp:2638)
#
# "#-1 CPU LIMITED" is a different mechanism -- ast.cpp:2182 emits it when a
# function call is *entered* while the alarm is already set, so it lands
# inside a result that still gets returned.  Both are real; only the second
# produces output from the aborted command.
#
# The practical consequence for this file: an aborted command emits NO
# marker, because the command that would have printed it is the one being
# killed.  Waiting for a marker therefore always runs to the timeout.  Every
# case below that expects an abort waits for the notice instead.
#
# Timing was measured with a probe either side of drv_ProcessCommand: with
# lag_limit 1 the alarm fires at 1.001s, so these cases are fast and cannot
# hang the harness.  The same expression completes in ~4.8s under a high
# budget, which is what the control asserts.
#
# Driven by tests/scenario/run.sh.  Usage: cpu_budget.py [host] [port]

import socket
import sys
import time

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 6250

WIZ_LOGIN = "connect Wizard potrzebie"

# Costs ~1.4s of evaluation, every intermediate well inside LBUF, and it
# terminates on its own -- so the "completes under a high budget" control is
# a real completion rather than a second abort.  Deliberately NOT sized to
# need function_invocation_limit raised: doing that removes the guard that
# normally bounds runaway softcode, and expressions large enough to need it
# were measured running unbounded.
EXPENSIVE = "[strlen(iter(lnum(1,600),[strlen(iter(lnum(1,600),##))]))]"

ABORT_NOTICE = "Expensive activity abbreviated"

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

        # 3. THE case.  An expensive command is cut short, and the player is
        #    told so.  Waiting on ABORT_NOTICE, not on a marker: the marker
        #    never arrives because the @pemit is what gets abandoned.
        t0 = time.monotonic()
        sendline(wiz, "@pemit me=X<%s>" % EXPENSIVE)
        buf = read_for(wiz, ABORT_NOTICE, ABORT_WAIT)
        elapsed = time.monotonic() - t0
        check(ABORT_NOTICE in buf,
              "an expensive command is stopped by the 1s budget",
              "no notice within %.1fs" % elapsed)

        # 4. And it was stopped promptly rather than merely finishing.  The
        #    budget is 1s; anything past a few seconds means the abort is not
        #    tied to it.  Loose on purpose -- this is wall clock on a shared
        #    box, so it pins the order of magnitude, not the millisecond.
        check(elapsed < 6.0,
              "the stop happens on the budget, not on completion",
              "took %.1fs with a 1s budget" % elapsed)

        # 5. Control: the identical expression completes under a high budget
        #    and returns a number.  Without this, cases 3-4 would also pass
        #    if the expression were simply broken or unsupported.
        sendline(wiz, "@admin lag_limit=%d" % HIGH_BUDGET)
        read_for(wiz, None, 1.0)
        got = probe(wiz, "H", EXPENSIVE, COMPLETE_WAIT)
        check(got is not None and got.isdigit() and int(got) > 0,
              "the same command completes under a %ds budget" % HIGH_BUDGET,
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
