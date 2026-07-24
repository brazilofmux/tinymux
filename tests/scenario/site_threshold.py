#!/usr/bin/env python3
# Live scenario test for per-control-group site thresholds (@admin *_site).
#
# The graduated-threshold feature lets a site rule engage only once N
# connections from a source key already exist ("forbid once 8 are up").  The
# threshold is stored PER CONTROL GROUP -- permit/register/forbid,
# sitemon/nositemon, guest/noguest, suspect/trust -- because one *_site
# directive names exactly one group.
#
# The regression this guards: when the threshold was a single per-NODE field,
# applying an unrelated rule to the same subnet (e.g. sitemon_site after
# forbid_site 8) silently reset the forbid's threshold to 0, turning
# "forbid at 8+" into "forbid ALWAYS".  A muxscript smoke test cannot reach
# this -- it needs a live server with a real access list -- so it lives here.
#
# Driven by tests/scenario/run.sh.  Usage: site_threshold.py [host] [port]

import socket
import sys
import time

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 6250

# Starter-DB #1 is "Wizard"; the traditional starter password is "potrzebie".
LOGIN = "connect Wizard potrzebie"

# TEST-NET-3 (RFC 5737) -- documentation-only space, so it cannot collide with
# anything a default config already lists.
SUBNET = "203.0.113.0/24"


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


def site_status(sock, tag):
    # Run @list site_information and return the status column of the SUBNET
    # row.  A trailing @pemit marker tells us the listing is complete rather
    # than guessing with a fixed sleep.
    sendline(sock, "@list site_information")
    sendline(sock, "@pemit me=ENDLIST-%s" % tag)
    buf = read_for(sock, "ENDLIST-%s" % tag, 5.0)
    for line in buf.splitlines():
        if SUBNET in line:
            # Format is "%-50s %s" (address then status).
            return " ".join(line.split(SUBNET, 1)[1].split())
    return None


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
    read_for(sock, None, 2.0)          # drain post-login room/prompt text

    # (setup command, tag, expected status column).
    cases = [
        # A graduated forbid records its threshold.
        ("@admin forbid_site=%s 8" % SUBNET,
         "a", "Forbid (at 8+ conns)"),

        # THE REGRESSION: an unrelated group applied to the same subnet must
        # not disturb the forbid's threshold.  Pre-fix this printed a bare
        # "Forbid SiteMon" -- threshold silently reset to 0 (forbid always).
        ("@admin sitemon_site=%s" % SUBNET,
         "b", "Forbid (at 8+ conns) SiteMon"),

        # Re-applying the SAME group does still adopt the new threshold
        # (the original fix -- this must not be a silent no-op).
        ("@admin forbid_site=%s 4" % SUBNET,
         "c", "Forbid (at 4+ conns) SiteMon"),

        # An exempting control in that group replaces forbid and reports the
        # opposite direction ("while under" rather than "at N+").
        ("@admin permit_site=%s 3" % SUBNET,
         "d", "Permit (while under 3 conns) SiteMon"),
    ]

    failures = 0
    n = 0
    for cmd, tag, expected in cases:
        n += 1
        sendline(sock, cmd)
        read_for(sock, None, 0.5)
        got = site_status(sock, tag)
        if got == expected:
            print("ok %d - %s -> %s" % (n, cmd, expected))
        else:
            failures += 1
            print("not ok %d - %s" % (n, cmd))
            print("    expected: %r" % expected)
            print("    got:      %r" % got)

    # ------------------------------------------------------------------
    # Live engage-at-N (not just @list text).  Uses 127.0.0.1/32 so the
    # scenario host's own sockets count toward the threshold.  The Wizard
    # login already holds one DESC from that key.
    #
    # Restriction: forbid engages when existing conns >= N (count is taken
    # before the new DESC is inserted).  With Wizard already connected (1),
    # forbid_site 127.0.0.1/32 2 allows one more socket (existing=1) and
    # refuses the next (existing=2).  Raise max_preauth_sitecons so the
    # preauth cap does not fire first.
    # ------------------------------------------------------------------
    def try_hold_conn(timeout=1.0):
        """Connect and return the socket if still open after settle, else None."""
        s = socket.socket()
        s.settimeout(3)
        try:
            s.connect((HOST, PORT))
        except OSError:
            try:
                s.close()
            except OSError:
                pass
            return None
        # Refuse path: fcache_rawdump then close → recv returns b''.
        # Accept path: socket stays open (welcome/negotiation may arrive).
        deadline = time.monotonic() + timeout
        s.settimeout(0.2)
        while time.monotonic() < deadline:
            try:
                chunk = s.recv(4096)
                if not chunk:
                    try:
                        s.close()
                    except OSError:
                        pass
                    return None
            except socket.timeout:
                pass
            except OSError:
                try:
                    s.close()
                except OSError:
                    pass
                return None
        return s

    LOOPBACK = "127.0.0.1/32"
    sendline(sock, "@admin max_preauth_sitecons=10")
    sendline(sock, "@admin max_lastsite_cnt=100")
    sendline(sock, "@admin forbid_site=%s 2" % LOOPBACK)
    read_for(sock, None, 0.5)

    held = []
    try:
        n += 1
        s1 = try_hold_conn()
        if s1 is not None:
            held.append(s1)
            print("ok %d - live forbid: 2nd concurrent conn from loopback accepted" % n)
        else:
            failures += 1
            print("not ok %d - live forbid: 2nd concurrent conn was refused (expected accept)" % n)

        n += 1
        s2 = try_hold_conn()
        if s2 is None:
            print("ok %d - live forbid: 3rd concurrent conn from loopback refused" % n)
        else:
            held.append(s2)
            failures += 1
            print("not ok %d - live forbid: 3rd concurrent conn was accepted (expected refuse)" % n)
    finally:
        for s in held:
            try:
                s.close()
            except OSError:
                pass
        sendline(sock, "@admin reset_site=%s" % LOOPBACK)
        read_for(sock, None, 0.3)

    sendline(sock, "QUIT")
    sock.close()

    print("1..%d" % n)
    if failures:
        print("FAIL: %d of %d site-threshold checks failed." % (failures, n))
        return 1
    print("PASS: all %d site-threshold checks passed." % n)
    return 0


if __name__ == "__main__":
    sys.exit(main())
