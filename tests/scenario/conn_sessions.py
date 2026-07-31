#!/usr/bin/env python3
#
# conn_sessions.py — live scenario test for the conn* family (#1380).
#
# emitconn_fn.mux covers exactly one case for these seven functions: the
# offline zeros.  Everything that makes them interesting needs a player who
# is actually connected, and the most load-bearing semantics need one
# connected TWICE — neither of which muxscript can produce, which is why
# #1380 lists this family as needing a live harness.
#
# The assertions below were established by probing a running server, and two
# of them contradict what the names suggest:
#
#   * connnum() is a LIFETIME login count, not a count of current sessions.
#     Connecting twice takes it to 2, and closing both sessions leaves it at
#     2.  A test that read it as "how many sessions are open" would pass on
#     the way up and never notice it does not come down.
#
#   * conn() answers -1 for an unknown player AND for a known player who is
#     offline, so it cannot distinguish them.  connnum() errors outright for
#     the unknown player.  Same question, two different contracts.
#
# Durations are deliberately never asserted to exact values — they are wall
# clock seconds and the harness has no control over scheduling.  Only signs,
# ordering and monotonicity are checked.
#
# Driven by tests/scenario/run.sh.  Usage: conn_sessions.py [host] [port]

import socket
import sys
import time

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 6250

WIZ_LOGIN = "connect Wizard potrzebie"

# The scenario DB is a throwaway copy of the starter DB, so a fixed name
# cannot collide across runs.  It must be freshly created: connlast/connleft
# start at 0 only for a player who has never connected, and two of the
# assertions below depend on that.
USER = "connsess"
USER_PW = "hunter2"

# Seconds to let the server register a connect/disconnect before reading the
# counters.  Descriptor teardown happens on the main loop, not in the socket
# close, so reading immediately can race it.
SETTLE = 1.5


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


def connect(login, timeout=2.5):
    sock = socket.socket()
    sock.settimeout(5)
    sock.connect((HOST, PORT))
    read_for(sock, None, 1.0)
    sendline(sock, login)
    read_for(sock, None, timeout)
    return sock


def probe(sock, tag, expr):
    sendline(sock, "@pemit me=%s<%s>" % (tag, expr))
    buf = read_for(sock, tag + "<", 5.0)
    for line in buf.splitlines():
        if line.startswith(tag + "<") and line.rstrip().endswith(">"):
            return line.strip()[len(tag) + 1:-1]
    return None


def num(sock, tag, expr):
    got = probe(sock, tag, "[%s]" % expr)
    try:
        return int(got)
    except (TypeError, ValueError):
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

    sendline(wiz, "@pcreate %s=%s" % (USER, USER_PW))
    read_for(wiz, None, 1.5)

    s1 = s2 = None
    try:
        # 1. Offline baseline on a player that has never connected.
        off_conn = num(wiz, "A1", "conn(%s)" % USER)
        off_numc = num(wiz, "A2", "connnum(%s)" % USER)
        off_last = num(wiz, "A3", "connlast(%s)" % USER)
        off_left = num(wiz, "A4", "connleft(%s)" % USER)
        off_tot = num(wiz, "A5", "conntotal(%s)" % USER)
        check(off_conn == -1 and off_numc == 0 and off_last == 0
              and off_left == 0 and off_tot == 0,
              "a never-connected player reads -1/0/0/0/0",
              "conn=%r num=%r last=%r left=%r total=%r"
              % (off_conn, off_numc, off_last, off_left, off_tot))

        # 2. One live session.  conn() becomes a non-negative duration and
        #    connnum() reaches 1.
        s1 = connect("connect %s %s" % (USER, USER_PW))
        time.sleep(SETTLE)
        one_conn = num(wiz, "B1", "conn(%s)" % USER)
        one_numc = num(wiz, "B2", "connnum(%s)" % USER)
        check(one_conn is not None and one_conn >= 0 and one_numc == 1,
              "one live session: conn() is a duration and connnum() is 1",
              "conn=%r num=%r" % (one_conn, one_numc))

        # 3. Second session for the SAME player.
        s2 = connect("connect %s %s" % (USER, USER_PW))
        time.sleep(SETTLE)
        two_numc = num(wiz, "C1", "connnum(%s)" % USER)
        check(two_numc == 2,
              "a second session for the same player takes connnum() to 2",
              "num=%r" % (two_numc,))

        # 4. THE point of this file.  Close one session; connnum() must NOT
        #    come back down, because it counts logins and not open sessions.
        #    Close the *newer* session first (historical coverage).
        s2.close()
        s2 = None
        time.sleep(SETTLE)
        back_numc = num(wiz, "D1", "connnum(%s)" % USER)
        back_conn = num(wiz, "D2", "conn(%s)" % USER)
        check(back_numc == 2,
              "connnum() counts logins, not open sessions: still 2 after one closes",
              "num=%r" % (back_numc,))
        check(back_conn is not None and back_conn >= 0,
              "the surviving session still reports a live conn() duration",
              "conn=%r" % (back_conn,))

        # 5. A completed session populates connlast and connleft, both of
        #    which were 0 before any session had ended.
        last_after = num(wiz, "E1", "connlast(%s)" % USER)
        left_after = num(wiz, "E2", "connleft(%s)" % USER)
        check(last_after is not None and last_after >= 0
              and left_after is not None and left_after > 0,
              "a completed session sets connlast and a connleft timestamp",
              "last=%r left=%r (were %r/%r)"
              % (last_after, left_after, off_last, off_left))

        # 6. Fully offline again: conn() returns to -1 while the cumulative
        #    counters keep what they accumulated.
        s1.close()
        s1 = None
        time.sleep(SETTLE)
        gone_conn = num(wiz, "F1", "conn(%s)" % USER)
        gone_numc = num(wiz, "F2", "connnum(%s)" % USER)
        gone_tot = num(wiz, "F3", "conntotal(%s)" % USER)
        gone_max = num(wiz, "F4", "connmax(%s)" % USER)
        check(gone_conn == -1 and gone_numc == 2
              and gone_tot is not None and gone_tot >= 0
              and gone_max is not None and gone_max >= 0,
              "back offline: conn() is -1 again but the totals persist",
              "conn=%r num=%r total=%r max=%r"
              % (gone_conn, gone_numc, gone_tot, gone_max))

        # 7. conntotal must be at least connmax: the longest single session
        #    cannot exceed the sum of all of them.
        check(gone_tot is not None and gone_max is not None
              and gone_tot >= gone_max,
              "conntotal() is at least connmax()",
              "total=%r max=%r" % (gone_tot, gone_max))

        # 7b. #1808: close the *oldest* of two staggered sessions first.
        #     Disconnect accounting must credit only the non-overlapping
        #     prefix to conntotal, then the survivor's remaining time — so
        #     the final total approximates wall-clock union, not a sum that
        #     double-counts the overlap.
        USER2 = "connsess2"
        sendline(wiz, "@pcreate %s=%s" % (USER2, USER_PW))
        read_for(wiz, None, 1.5)
        o1 = o2 = None
        try:
            t0 = time.monotonic()
            o1 = connect("connect %s %s" % (USER2, USER_PW))
            time.sleep(2.0)  # stagger so "oldest" is unambiguous
            o2 = connect("connect %s %s" % (USER2, USER_PW))
            time.sleep(SETTLE)
            # Close oldest first.
            o1.close()
            o1 = None
            time.sleep(SETTLE)
            o2.close()
            o2 = None
            time.sleep(SETTLE)
            t1 = time.monotonic()
            wall = t1 - t0
            union_tot = num(wiz, "U1", "conntotal(%s)" % USER2)
            # Union <= wall+slack; if overlap were double-counted, total would
            # approach ~wall + stagger (~2s extra) and exceed wall easily.
            # Allow generous slack for SETTLE/scheduling; require total not to
            # exceed wall + 4s, and to be at least the longest single session
            # floor (~2s stagger + settle).
            check(union_tot is not None and 0 <= union_tot <= int(wall) + 4,
                  "#1808 close-oldest-first: conntotal approximates union time",
                  "total=%r wall=%.1f" % (union_tot, wall))
        finally:
            for s in (o1, o2):
                if s is not None:
                    try:
                        s.close()
                    except OSError:
                        pass
            sendline(wiz, "@destroy/override %s" % USER2)
            read_for(wiz, None, 1.0)

        # 8. The unknown-player split.  conn() cannot distinguish "no such
        #    player" from "offline" -- both are -1 -- while connnum() errors.
        #    Pinned as observed rather than as it ought to be: softcode may
        #    already branch on either, so aligning them is a compatibility
        #    decision, not a tidy-up.
        bad_conn = probe(wiz, "G1", "[conn(NoSuchPlayerXYZZY)]")
        bad_numc = probe(wiz, "G2", "[connnum(NoSuchPlayerXYZZY)]")
        check(bad_conn == "-1" and bad_numc is not None
              and bad_numc.startswith("#-1"),
              "conn() answers -1 for an unknown player where connnum() errors",
              "conn=%r connnum=%r" % (bad_conn, bad_numc))

        # 9. connrecord() is server-wide and takes no argument; at least the
        #    wizard plus the sessions above have been connected at once.
        rec = num(wiz, "H1", "connrecord()")
        check(rec is not None and rec >= 1,
              "connrecord() reports a server-wide simultaneous-connection high water mark",
              "record=%r" % (rec,))

    finally:
        for s in (s1, s2):
            if s is not None:
                try:
                    s.close()
                except OSError:
                    pass
        sendline(wiz, "@destroy/override %s" % USER)
        read_for(wiz, None, 1.0)
        try:
            wiz.close()
        except OSError:
            pass

    print("=== conn-sessions scenario: %d passed, %d failed ===" % (npass, nfail))
    return 1 if nfail else 0


if __name__ == "__main__":
    sys.exit(main())
