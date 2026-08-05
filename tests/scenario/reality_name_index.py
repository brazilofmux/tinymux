#!/usr/bin/env python3
# Live scenario test: the exact-name index must not swallow a partial match
# when REALITY_LVLS declines the exact one (#2110, regression from #2058).
#
# Why this cannot be a smoke test: it needs a NON-DEFAULT reality
# configuration.  At the shipped defaults every def_*_rx and def_*_tx is 1, so
# IsReal() is 1 & 1 for every pair, promote_match() never takes its
# REALITY_LVLS early return, and the index fast path and the walk agree on
# every input.  `make test` was 36/36 green on the commit that introduced the
# bug.  run.sh therefore starts a SECOND server for this driver with
# def_thing_tx 2, so THINGs are invisible to PLAYERs.
#
# What it pins:
#   1. A query that exactly names a reality-hidden object must still find a
#      VISIBLE partial match further down the same contents list.  The index
#      short-circuits the walk on an exact hit; that is only sound if the hit
#      actually promoted, and promote_match() returns without promoting when
#      the candidate is not IsReal() to the looker.
#   2. The ordinary index path still works (exact match on a visible object).
#   3. Dbref lookup is unaffected -- the control that says a failure in (1) is
#      the name path and not the fixture.
#
# On a build without --enable-realitylvls the hiding cannot happen, every
# assertion below degenerates to "the walk and the index agree", and the test
# passes without discriminating.  It reports which case it is running so a
# green result is not over-read.
#
# Driven by tests/scenario/run.sh.  Usage: reality_name_index.py [host] [port]

import re
import socket
import sys
import time

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 6250

WIZ_LOGIN = "connect Wizard potrzebie"

# Names chosen so that HIDDEN is an exact match for the query and VISIBLE is
# only a partial one.  They must land in different index buckets, which is the
# whole point: the fast path consults the query's bucket alone.
HIDDEN_NAME = "sword"
VISIBLE_NAME = "swordsman"
VISIBLE_PW = "hunter2"


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


def probe(sock, tag, expr):
    # Echo the result inside a unique marker so queue lag and partial reads
    # cannot be mistaken for a value.
    sendline(sock, "@pemit me=%s<%s>" % (tag, expr))
    buf = read_for(sock, tag + "<", 5.0)
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
            print("not ok %d - %s%s" % (npass + nfail, msg, detail))

    try:
        wiz = socket.create_connection((HOST, PORT), timeout=10)
    except OSError as e:
        print("not ok - could not connect to %s:%d (%s)" % (HOST, PORT, e))
        return 1
    read_for(wiz, None, 1.5)
    sendline(wiz, WIZ_LOGIN)
    read_for(wiz, None, 2.0)

    room = probe(wiz, "ROOM", "[loc(me)]")
    if not room or not room.startswith("#"):
        print("not ok - could not resolve the Wizard's location (got %r)" % room)
        return 1

    # Create the THING and move it by DBREF.  Its name is exactly what is
    # under test, so resolving it by name here would beg the question.
    sendline(wiz, "@create %s" % HIDDEN_NAME)
    m = re.search(r"#(\d+)", read_for(wiz, "reated", 5.0))
    if not m:
        print("not ok - @create %s did not report a dbref" % HIDDEN_NAME)
        return 1
    hidden = "#" + m.group(1)
    sendline(wiz, "@tel %s=%s" % (hidden, room))
    read_for(wiz, None, 1.0)

    sendline(wiz, "@pcreate %s=%s" % (VISIBLE_NAME, VISIBLE_PW))
    m = re.search(r"#(\d+)", read_for(wiz, "#", 5.0))
    if not m:
        print("not ok - @pcreate %s did not report a dbref" % VISIBLE_NAME)
        return 1
    visible = "#" + m.group(1)
    sendline(wiz, "@tel %s=%s" % (visible, room))
    read_for(wiz, None, 1.0)

    # Establish the fixture before asserting on it: both objects really are in
    # the room, and they really do have the names this test reasons about.
    contents = probe(wiz, "LCON", "[lcon(%s)]" % room) or ""
    fixture_ok = (hidden in contents.split()) and (visible in contents.split())
    check(fixture_ok,
          "fixture: hidden %s and visible %s are both in %s" % (hidden, visible, room),
          " (lcon = %r)" % contents)
    if not fixture_ok:
        return 1
    check(probe(wiz, "NH", "[name(%s)]" % hidden) == HIDDEN_NAME,
          "fixture: %s is named %r" % (hidden, HIDDEN_NAME))
    check(probe(wiz, "NV", "[name(%s)]" % visible) == VISIBLE_NAME,
          "fixture: %s is named %r" % (visible, VISIBLE_NAME))

    # Is the reality configuration actually hiding anything?  If it is not,
    # the discriminating case below cannot fail and the run must say so.
    seen = probe(wiz, "SEE", "[lcon(%s)]" % room) or ""
    hiding = probe(wiz, "HID", "[num(%s)]" % HIDDEN_NAME)
    print("# reality: query %r resolves to %s (hidden object is %s, visible is %s)"
          % (HIDDEN_NAME, hiding, hidden, visible))

    # (3) Control first: dbref lookup never goes through match_list's name
    # path, so it must be unaffected however the rest turns out.
    check(probe(wiz, "BYD", "[num(%s)]" % hidden) == hidden,
          "control: %s still resolves by dbref" % hidden)

    # (2) The ordinary index path: an exact match on a VISIBLE object.
    check(probe(wiz, "VIS", "[num(%s)]" % VISIBLE_NAME) == visible,
          "exact match on a visible object resolves to %s" % visible)

    # (1) The discriminating case.  The exact-named object is hidden, so the
    # answer must be the visible PARTIAL match -- never #-1, which is what the
    # index returns when it short-circuits on a promote that never landed.
    got = probe(wiz, "PAR", "[num(%s)]" % HIDDEN_NAME)
    check(got == visible,
          "query %r finds the visible partial %s, not the hidden exact match"
          % (HIDDEN_NAME, visible),
          " (got %r; #-1 means the index skipped the rest of the container"
          " after a declined promote -- #2110)" % got)

    wiz.close()
    print("=== reality name index: %d passed, %d failed ===" % (npass, nfail))
    return 1 if nfail else 0


if __name__ == "__main__":
    sys.exit(main())
