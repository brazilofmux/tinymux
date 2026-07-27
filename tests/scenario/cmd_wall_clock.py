#!/usr/bin/env python3
#
# cmd_wall_clock.py — live scenario test for #1597 (max_cmdsecs / lag_limit).
#
# `alarm_clock.set(mudconf.max_cmdsecs)` is reached from exactly one place,
# cque.cpp's queued command path.  muxscript never gets there — mux_main.cpp
# explicitly stores false into alarm_clock.alarmed — so the entire wall-clock
# budget is unexercised by the smoke corpus, on every route that honours it:
# the AST evaluator, the softcode JIT's dbt_run -3 path, and the PCRE2
# pre-check.  That blind spot is plausibly why #1591 survived (Lua referenced
# alarm_clock nowhere at all) and it is the same shape as #1571, where the
# DBT's poll was unreachable inside a chained loop for as long as chaining had
# existed.
#
# The mechanism only exists in the queue, so this has to be a real server
# driven over a socket — which is what tests/scenario is for.
#
# Three assertions, because the interesting one alone would not be worth much:
#
#   1. expensive + low budget  -> #-1 CPU LIMITED       (the bound fires)
#   2. expensive + high budget -> a real answer         (it is the *budget*
#                                                        doing it, not the
#                                                        expression failing)
#   3. cheap + low budget      -> a real answer         (it does not fire on
#                                                        everything)
#
# Without (2) this would pass if the expression simply errored, and without
# (3) it would pass if the alarm fired unconditionally.
#
# On the burner: the expression has to cost real wall time while keeping every
# intermediate string inside LBUF, which rules out the obvious candidates.
# `iter(lnum(1,N))` is not usable — the list itself overruns LBUF somewhere
# between N=2000 and N=8000, and what you measure after that is the overflow,
# not the CPU.  A pathological regex is not usable either: PCRE2 already
# bounds its own backtracking, and `regmatch(a{26},(a+)+b)` returns
# immediately.  Nested iter keeps the strings small and multiplies the work:
# measured at roughly 350 ms per evaluation at 300x300, scaling with the
# product, so 600x600 is comfortably over a one-second budget while still
# terminating on its own if the bound never fires.
#
# NOT YET REGISTERED in run.sh's driver list, deliberately.  Assertion (1)
# fails on master: a command that should be cut off at one second was still
# running at twenty, with lag_limit verified as 1 and the same expression
# completing under a high budget.  That is #1602, and it is a defect in the
# server, not in this file.
#
# Registering it now would turn `make test-scenario` red for a bug it only
# reports.  Add it to the DRIVER list in run.sh in the same change that fixes
# #1602 -- at which point this becomes the regression test for it.
#
# Run standalone against a live server meanwhile:
#   ./cmd_wall_clock.py 127.0.0.1 6250
#
# Driven by tests/scenario/run.sh.  Usage: cmd_wall_clock.py [host] [port]

import socket
import sys
import time

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 6250

WIZ_LOGIN = "connect Wizard potrzebie"

# The budget under test, in seconds, and one high enough that the same
# expression is expected to finish.
LOW_BUDGET = 1
HIGH_BUDGET = 120

# ~1.4 s of evaluation, all intermediates well inside LBUF.  Terminates on its
# own, so a failure reports rather than wedging the server for the rest of the
# scenario run.
EXPENSIVE = "[strlen(iter(lnum(1,600),[strlen(iter(lnum(1,600),##))]))]"
CHEAP = "[add(1,2)]"

# What every route answers when the wall clock runs out — the AST evaluator
# writes it directly, and handle_dbt_run_status writes the same string for the
# JIT's -3 return, so this assertion is route-independent.
CPU_LIMITED = "#-1 CPU LIMITED"


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


def probe(sock, tag, expr, timeout=20.0):
    # Generous timeout: the whole point is that one of these takes real time,
    # and on the failing path it takes longer still.
    sendline(sock, "@pemit me=%s<%s>" % (tag, expr))
    buf = read_for(sock, tag + "<", timeout)
    for line in buf.splitlines():
        if line.startswith(tag + "<") and line.rstrip().endswith(">"):
            return line.strip()[len(tag) + 1:-1]
    return None


def set_budget(sock, secs):
    sendline(sock, "@admin lag_limit=%d" % secs)
    read_for(sock, None, 1.0)


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

    # (3) first, and under the low budget: if a cheap command is already being
    # cut off then the two results below mean nothing, and saying so here is
    # clearer than letting (1) pass for the wrong reason.
    set_budget(wiz, LOW_BUDGET)

    # Before anything else: did the knob take?  Without this the two results
    # below can fail for the most boring reason there is -- a budget that was
    # never actually lowered -- and would look like the server ignoring it.
    shown = probe(wiz, "CFG", "[config(lag_limit)]")
    check(shown == str(LOW_BUDGET),
          "lag_limit reads back as %d after @admin" % LOW_BUDGET,
          "got %r" % shown)

    cheap = probe(wiz, "CHP", CHEAP)
    check(cheap == "3",
          "cheap command is unaffected by a %ds budget" % LOW_BUDGET,
          "got %r, wanted '3'" % cheap)

    # (1) the assertion this test exists for.
    started = time.monotonic()
    limited = probe(wiz, "EXP", EXPENSIVE)
    elapsed = time.monotonic() - started
    check(limited == CPU_LIMITED,
          "expensive command hits the %ds wall-clock budget" % LOW_BUDGET,
          "got %r after %.1fs, wanted %r" % (limited, elapsed, CPU_LIMITED))

    # (2) the control.  Same expression, budget it cannot exceed: proves the
    # answer above came from the budget and not from the expression being
    # broken, unsupported, or over some other limit.
    set_budget(wiz, HIGH_BUDGET)
    finished = probe(wiz, "FIN", EXPENSIVE, timeout=60.0)
    ok_finished = (finished is not None
                   and finished != CPU_LIMITED
                   and finished.isdigit())
    check(ok_finished,
          "same command completes under a %ds budget" % HIGH_BUDGET,
          "got %r, wanted a number" % finished)

    # Leave the server as we found it for whatever runs next.
    set_budget(wiz, HIGH_BUDGET)

    print("# %d passed, %d failed" % (npass, nfail))
    return 1 if nfail else 0


if __name__ == "__main__":
    sys.exit(main())
