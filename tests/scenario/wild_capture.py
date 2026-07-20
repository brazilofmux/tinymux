#!/usr/bin/env python3
# Live scenario test for the wildcard capture path ($-command %0..%9).
#
# muxscript's minimal REPL does not wire match_mine/listen, so it cannot drive
# $-commands or ^-listens (see docs/survey-wild-matching.md) -- the smoke suite
# and wild_test.cpp therefore never exercise the end-to-end capture plumbing,
# only wild() in isolation.  This connects to a real netmux, logs in as the
# starter-DB Wizard, installs $-commands whose actions echo their %0..%9
# captures back via @pemit, types matching commands, and asserts the captures.
#
# Driven by tests/scenario/run.sh.  Usage: wild_capture.py [host] [port]

import socket
import sys
import time

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 6250

# Starter-DB #1 is "Wizard"; the traditional starter password is "potrzebie".
LOGIN = "connect Wizard potrzebie"


def sendline(sock, line):
    sock.sendall(line.encode("utf-8") + b"\r\n")


def read_for(sock, marker, timeout=5.0):
    # Accumulate decoded output until `marker` appears or `timeout` elapses.
    # $-command actions run from the command queue, so the reply can lag the
    # trigger by a loop iteration; poll rather than sleep a fixed amount.
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


def main():
    sock = socket.socket()
    sock.settimeout(5)
    try:
        sock.connect((HOST, PORT))
    except OSError as e:
        print("not ok - could not connect to %s:%d (%s)" % (HOST, PORT, e))
        return 1

    read_for(sock, None, 1.0)          # drain telnet negotiation + welcome
    sendline(sock, LOGIN)
    read_for(sock, None, 2.0)          # drain the post-login room/prompt text

    # Install the $-commands on the player (#1); a player's own $-commands are
    # always in match scope, so this needs no room/location setup.  Each action
    # pemits its captures back to the enactor (%#), angle-delimited so empty vs
    # populated captures are unambiguous.
    setup = [
        "&cap.single me=$wildcapfoo *:@pemit %#=CAP1<%0>",
        "&cap.multi me=$wildcapgreet * from *:@pemit %#=CAP2<%0><%1>",
        "&cap.qmark me=$wildcappick ? of *:@pemit %#=CAP3<%0><%1>",
        "&cap.starq me=$wildcapword *?:@pemit %#=CAP4<%0><%1>",
    ]
    for cmd in setup:
        sendline(sock, cmd)
    read_for(sock, None, 1.0)

    # (trigger, expected capture echo).  Values verified against this tree.
    cases = [
        ("wildcapfoo hello world",       "CAP1<hello world>"),     # *  -> %0
        ("wildcapgreet alice from bob",  "CAP2<alice><bob>"),      # * * -> %0 %1
        ("wildcappick 3 of hearts",      "CAP3<3><hearts>"),       # ? * -> %0 %1
        ("wildcapword abc",              "CAP4<ab><c>"),           # *?  -> %0 %1 (#838)
    ]

    npass = nfail = 0
    for i, (trigger, expected) in enumerate(cases, 1):
        sendline(sock, trigger)
        got = read_for(sock, expected, 5.0)
        if expected in got:
            npass += 1
            print("ok %d - %s -> %s" % (i, trigger, expected))
        else:
            nfail += 1
            print("not ok %d - %s (wanted %r; last output %r)"
                  % (i, trigger, expected, got[-200:]))

    try:
        sock.close()
    except OSError:
        pass

    print("=== wild-capture scenario: %d passed, %d failed ===" % (npass, nfail))
    return 1 if nfail else 0


if __name__ == "__main__":
    sys.exit(main())
