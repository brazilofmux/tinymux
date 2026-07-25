#!/usr/bin/env python3
#
# driver_config_sync.py — live scenario test for #1222.
#
# Config knobs that live in the DRIVER_CONFIG basket (g_dc) are read by the
# driver, not from mudconf.  The engine only re-pulls that basket when a
# config handler calls the sync callback, so a runtime "@admin <knob>=..."
# through a handler that does not sync updates mudconf -- config() reports
# the new value, and the change is logged as a success -- while the driver
# keeps using the boot-time value until restart.
#
# player_starting_room is the observable member of that set: the driver reads
# g_dc.start_room when it moves a newly created player (net.cpp check_conn).
# So creating a player after an @admin of that knob shows directly whether
# the basket was re-pulled.
#
# lag_limit, lag_maximum and timeslice share the same defect and the same fix
# (cf_seconds), but their effects are timing-dependent and not worth pinning
# in a scenario driver.
#
# Driven by tests/scenario/run.sh.  Usage: driver_config_sync.py [host] [port]

import socket
import sys
import time

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 6250

# Starter-DB #1 is "Wizard"; the traditional starter password is "potrzebie".
WIZ_LOGIN = "connect Wizard potrzebie"

NEWPLAYER = "cfgsync1"
NEWPLAYER_PW = "hunter2"


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
    read_for(sock, None, 1.0)          # drain negotiation + welcome
    sendline(sock, login)
    read_for(sock, None, timeout)      # drain post-login room text
    return sock


def probe(sock, tag, expr):
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
            print("not ok %d - %s%s"
                  % (npass + nfail, msg, (" (%s)" % detail) if detail else ""))

    try:
        wiz = connect(WIZ_LOGIN)
    except OSError as e:
        print("not ok - could not connect to %s:%d (%s)" % (HOST, PORT, e))
        return 1

    # A fresh room to point new players at.  Anything but the boot-time
    # starting room will do.
    # num() cannot find a room by name (it is not in the player's vicinity),
    # so ask for the last room this player dug.
    sendline(wiz, "@dig CfgSyncRoom")
    read_for(wiz, None, 1.5)
    room = probe(wiz, "RM", "[lastcreate(me,r)]")
    if not room or not room.startswith("#") or room.startswith("#-"):
        print("not ok - could not create/locate the test room (%r)" % room)
        return 1

    boot_room = probe(wiz, "BR", "[config(player_starting_room)]")

    # Enable player creation from the login screen, then repoint the starting
    # room at runtime.  Both are @admin sets on a live server.
    sendline(wiz, "@admin player_creation=yes")
    read_for(wiz, None, 1.0)
    sendline(wiz, "@admin player_starting_room=%s" % room)
    read_for(wiz, None, 1.0)

    # The engine side must agree the set took -- if it did not, the rest of
    # the test would be measuring the wrong thing.
    now = probe(wiz, "NR", "[config(player_starting_room)]")
    check(now == room.lstrip("#") or now == room,
          "engine accepted the runtime @admin of player_starting_room",
          "boot=%r now=%r room=%r" % (boot_room, now, room))

    # Create a player from the login screen.  check_conn() moves it to
    # g_dc.start_room, so where it lands says whether the driver basket was
    # re-pulled.
    try:
        newbie = connect("create %s %s" % (NEWPLAYER, NEWPLAYER_PW), timeout=3.0)
    except OSError as e:
        print("not ok - new player could not connect (%s)" % e)
        return 1

    where = probe(newbie, "LOC", "[num(loc(me))]")
    check(where == room,
          "#1222 driver honours a runtime @admin of player_starting_room",
          "expected=%r landed=%r boot=%r" % (room, where, boot_room))

    for s in (wiz, newbie):
        try:
            s.close()
        except OSError:
            pass

    print("=== driver-config-sync scenario: %d passed, %d failed ==="
          % (npass, nfail))
    return 1 if nfail else 0


if __name__ == "__main__":
    sys.exit(main())
