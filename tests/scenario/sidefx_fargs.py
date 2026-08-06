#!/usr/bin/env python3
# Live spot-probe for the side-effect softcode functions converted in the
# #2136 const-fargs campaign (link/tel/pemit/trigger/wipe/destroy).
#
# These wrappers now hand FargCopy/FargVec copies to their command handlers
# (do_link, do_teleport, do_pemit_*, do_trigger, do_wipe, do_destroy), and
# the smoke suite deliberately avoids side effects, so nothing else checks
# that the copies actually reach the handler intact and the side effect
# lands.  Each probe performs the effect against a live object and reads
# the state back.
#
# Driven by tests/scenario/run.sh.  Usage: sidefx_fargs.py [host] [port]

import re
import socket
import sys
import time

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 6250

LOGIN = "connect Wizard potrzebie"


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


def main():
    sock = socket.socket()
    sock.settimeout(5)
    try:
        sock.connect((HOST, PORT))
    except OSError as e:
        print("not ok - could not connect to %s:%d (%s)" % (HOST, PORT, e))
        return 1

    read_for(sock, None, 1.0)
    sendline(sock, LOGIN)
    read_for(sock, None, 2.0)

    failures = 0

    def probe(name, commands, marker, expect):
        nonlocal failures
        for cmd in commands:
            sendline(sock, cmd)
        buf = read_for(sock, marker, 5.0)
        if expect in buf:
            print("ok - %s" % name)
        else:
            failures += 1
            print("not ok - %s" % name)
            print("  wanted: %r" % expect)
            print("  in: %r" % buf[-500:])

    # A scratch object; %q registers do not survive across separate queue
    # entries, so capture the dbref here and splice it into every command.
    sendline(sock, "think CREATED<[create(FargProbe, 10)]>")
    buf = read_for(sock, ">", 5.0)
    m = re.search(r"CREATED<(#\d+)>", buf)
    if not m:
        print("not ok - create() did not return a dbref: %r" % buf[-300:])
        return 1
    obj = m.group(1)
    print("ok - create() made the probe object (%s)" % obj)

    # pemit(): the message must arrive on this connection.
    probe("pemit() delivers to a live player",
          ["think pemit(%#, PEMITSENT-[add(2,3)])"],
          "PEMITSENT-5", "PEMITSENT-5")

    # trigger(): FargCopy(obj/attr) + FargVec argv reach the queued action.
    probe("trigger() passes obj/attr and %0/%1 argv",
          ["&TPROBE %s=@pemit %%#=TRIGGED<%%0><%%1>" % obj,
           "think trigger(%s/TPROBE, alpha, beta)" % obj],
          "TRIGGED<", "TRIGGED<alpha><beta>")

    # Where is the Wizard?  Earlier drivers may have moved us, so 'here'
    # is whatever loc(%#) says, not #0.
    sendline(sock, "think WHERE<[loc(%#)]>")
    buf = read_for(sock, "WHERE<", 5.0)
    m = re.search(r"WHERE<(#\d+)>", buf)
    here = m.group(1) if m else "#0"

    # link(): set the probe's home, read it back.
    probe("link() sets home through the copied args",
          ["think link(%s, here)LINKDONE" % obj,
           "think LINKHOME<[home(%s)]>" % obj],
          "LINKHOME<", "LINKHOME<%s>" % here)

    # tel(): teleport the probe into this room, read location back.
    probe("tel() moves the object",
          ["think tel(%s, here)TELDONE" % obj,
           "think TELLOC<[loc(%s)]>" % obj],
          "TELLOC<", "TELLOC<%s>" % here)

    # wipe(): remove the attribute set for the trigger probe.
    probe("wipe() removes attributes",
          ["think wipe(%s/TPROBE)WIPEDONE" % obj,
           "think WIPED<[hasattr(%s, TPROBE)]>" % obj],
          "WIPED<", "WIPED<0>")

    # destroy(): the object gets the GOING flag.
    probe("destroy() queues the object for destruction",
          ["think destroy(%s)DESTDONE" % obj,
           "think GOING<[hasflag(%s, GOING)]>" % obj],
          "GOING<", "GOING<1>")

    sendline(sock, "QUIT")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
