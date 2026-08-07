#!/usr/bin/env python3
#
# proto_detect.py — live scenario test for #2193 (proto_detect_window).
#
# A plain connection holds DS_NEED_PROTO until the client's first byte, so
# telnet negotiation cannot corrupt a WebSocket handshake (#1074).  A classic
# MUD client never sends that byte -- it waits for the server to speak -- so
# it sits in total silence until the grace window ages it out.  The clients
# the detection exists for pay none of the window; the primary audience pays
# all of it, on every connect.
#
# That window used to be a hardcoded 500ms literal in two places.  It is now
# `proto_detect_window` in the driver basket, and 0 disables detection.
#
# Four things are checked, and only a real socket can check them:
#
#   1. A silent client waits about the configured window (default 500ms).
#   2. A client that speaks first is served immediately -- confirming the
#      cost falls only on silence, which is the whole shape of the issue.
#   3. With the window at 0, a silent client is served immediately too.
#      This is 2.13's behaviour, and the right setting for a port that
#      never serves WebSocket.
#   4. Setting it back restores the wait -- so the live push works in both
#      directions and this driver leaves the shared server as it found it.
#
# 3 and 4 also exercise cf_live_driver_int: proto_detect_window is read from
# g_dc on the accept path, so a runtime @admin that did not re-pull the
# basket would report success and change nothing until restart (#1222).
#
# Driven by tests/scenario/run.sh.  Usage: proto_detect.py [host] [port]

import socket
import sys
import time

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 6250

WIZ_LOGIN = "connect Wizard potrzebie"

# The configured window in the harness server is the shipped default.
DEFAULT_WINDOW_MS = 500

# A waiting connection must land near the window; an immediate one must land
# nowhere near it.  The gap between these is wide on purpose -- this asserts
# "waited" vs "did not wait", not the millisecond count, because the count is
# scheduler-dependent on a loaded box and pinning it would make the test
# flaky without making it more informative.
WAITED_MIN_MS = 350
WAITED_MAX_MS = 2500
IMMEDIATE_MAX_MS = 200


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
    sendline(sock, "@pemit me=%s<%s>" % (tag, expr))
    buf = read_for(sock, tag + "<", 5.0)
    for line in buf.splitlines():
        if line.startswith(tag + "<") and line.rstrip().endswith(">"):
            return line.strip()[len(tag) + 1:-1]
    return None


def first_byte_ms(speak_first=False, timeout=6.0):
    """Milliseconds from connect() to the server's first byte.

    Returns None if the server never spoke.  With speak_first, a byte is sent
    the moment the connection is up -- standing in for a client that does not
    wait for the banner.  "\r\n" is deliberately not a prefix of "GET ", so
    the #1800 partial-preface buffering does not hold the connection.
    """
    sock = socket.socket()
    sock.settimeout(timeout)
    start = time.monotonic()
    try:
        sock.connect((HOST, PORT))
        if speak_first:
            sock.sendall(b"\r\n")
        try:
            data = sock.recv(4096)
        except socket.timeout:
            return None
        if not data:
            return None
        return (time.monotonic() - start) * 1000.0
    finally:
        try:
            sock.close()
        except OSError:
            pass


def main():
    npass = nfail = 0

    def check(ok, msg, detail=""):
        nonlocal npass, nfail
        if ok:
            npass += 1
            print("ok %d - %s%s"
                  % (npass + nfail, msg, ("  # %s" % detail) if detail else ""))
        else:
            nfail += 1
            print("not ok %d - %s%s"
                  % (npass + nfail, msg, (" (%s)" % detail) if detail else ""))

    try:
        wiz = socket.socket()
        wiz.settimeout(5)
        wiz.connect((HOST, PORT))
        read_for(wiz, None, 1.5)
        sendline(wiz, WIZ_LOGIN)
        read_for(wiz, None, 2.0)
    except OSError as e:
        print("not ok - could not connect to %s:%d (%s)" % (HOST, PORT, e))
        return 1

    configured = probe(wiz, "PDW", "[config(proto_detect_window)]")
    if configured != str(DEFAULT_WINDOW_MS):
        # Not a failure of the mechanism -- but every threshold below is
        # written against the default, so say so rather than measure noise.
        print("not ok - server is not at the default window (config=%r)"
              % configured)
        return 1

    # 1. Silent client pays the window.
    silent = first_byte_ms()
    check(silent is not None
          and WAITED_MIN_MS <= silent <= WAITED_MAX_MS,
          "silent client waits the configured proto-detect window",
          "%s ms (window %d)"
          % ("none" if silent is None else "%.0f" % silent, DEFAULT_WINDOW_MS))

    # 2. Talking client does not.  This is the asymmetry the issue is about:
    #    the same server, the same window, one connection pays and one does
    #    not, decided entirely by who speaks first.
    talker = first_byte_ms(speak_first=True)
    check(talker is not None and talker < IMMEDIATE_MAX_MS,
          "client that speaks first is served without waiting",
          "%s ms" % ("none" if talker is None else "%.0f" % talker))

    # 3. Window 0 disables detection.
    sendline(wiz, "@admin proto_detect_window=0")
    read_for(wiz, None, 1.5)
    now = probe(wiz, "Z", "[config(proto_detect_window)]")
    check(now == "0", "engine accepted the runtime @admin to 0",
          "config=%r" % now)

    disabled = first_byte_ms()
    check(disabled is not None and disabled < IMMEDIATE_MAX_MS,
          "#2193 window 0 serves a silent client at accept (2.13 behaviour)",
          "%s ms" % ("none" if disabled is None else "%.0f" % disabled))

    # 4. And back, so the live push is shown to work in both directions and
    #    the next driver on this shared server sees the default again.
    sendline(wiz, "@admin proto_detect_window=%d" % DEFAULT_WINDOW_MS)
    read_for(wiz, None, 1.5)
    restored = first_byte_ms()
    check(restored is not None
          and WAITED_MIN_MS <= restored <= WAITED_MAX_MS,
          "restoring the window restores the wait (live push, both ways)",
          "%s ms" % ("none" if restored is None else "%.0f" % restored))

    try:
        wiz.close()
    except OSError:
        pass

    print("=== proto-detect scenario: %d passed, %d failed ===" % (npass, nfail))
    return 1 if nfail else 0


if __name__ == "__main__":
    sys.exit(main())
