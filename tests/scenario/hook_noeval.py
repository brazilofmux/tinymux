#!/usr/bin/env python3
#
# hook_noeval.py — live scenario test for #1280 (NOEVAL boolean hooks).
#
# process_hook() returns a bool: true means "proceed", false means the PERMIT
# hook denies or the IGNORE hook swallows the command.  Under AF_NOEVAL (or a
# NOEVAL hook_obj) it used to skip the content entirely and return true, so a
# NOEVAL permit hook allowed every command — a permission control silently
# disabled, and indistinguishable from having no hook configured.
#
# The fix derives the boolean from the *unevaluated* text, matching
# eval_boolexp, which is the only other boolean consumer in the NOEVAL work
# (4cb904fcc) and compares raw text rather than short-circuiting.
#
# This cannot live in the smoke corpus: @admin hook_obj / hook_cmd are CA_GOD,
# and smoke test cases run as a created object, not as #1.
#
# Driven by tests/scenario/run.sh.  Usage: hook_noeval.py [host] [port]

import socket
import sys
import time

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 6250

WIZ_LOGIN = "connect Wizard potrzebie"

HOOKOBJ = "hook_noeval_obj"


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


def probe(sock, tag, expr):
    sendline(sock, "@pemit me=%s<%s>" % (tag, expr))
    buf = read_for(sock, tag + "<", 5.0)
    for line in buf.splitlines():
        if line.startswith(tag + "<") and line.rstrip().endswith(">"):
            return line.strip()[len(tag) + 1:-1]
    return None


def cmd(sock, line, settle=1.0):
    sendline(sock, line)
    read_for(sock, None, settle)


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

    # Every driver in run.sh shares one throwaway server, so the hook must be
    # torn down on every exit path or later @dig calls stay blocked.
    def cleanup():
        cmd(wiz, "@admin hook_cmd=@dig")
        cmd(wiz, "@admin hook_obj=#-1")

    try:
        cmd(wiz, "@create %s" % HOOKOBJ, 1.5)
        obj = probe(wiz, "OB", "[num(%s)]" % HOOKOBJ)
        if not obj or not obj.startswith("#") or obj.startswith("#-"):
            print("not ok - could not create/locate the hook object (%r)" % obj)
            return 1

        # A permit hook whose literal text is "0" — i.e. "deny" — and marked
        # NOEVAL so the content is data rather than code.
        cmd(wiz, "&P_@dig %s=0" % HOOKOBJ)
        cmd(wiz, "@set %s/P_@dig=NO_EVAL" % HOOKOBJ)
        cmd(wiz, "@admin hook_obj=%s" % obj)
        cmd(wiz, "@admin hook_cmd=@dig permit")

        # Assert the flag actually took.  The attribute flag is NO_EVAL, not
        # NOEVAL (which is a command switch); an earlier draft of this test
        # used the wrong name, silently set nothing, and passed against the
        # unfixed build because the hook was being evaluated normally.  'E'
        # is AF_NOEVAL in decode_attr_flags.
        attrflags = probe(wiz, "FL", "[flags(%s/P_@dig)]" % HOOKOBJ)
        check(attrflags is not None and "E" in attrflags,
              "permit hook attribute is actually marked NO_EVAL",
              "flags=%r (must contain E for AF_NOEVAL)" % attrflags)

        # With the hook denying, @dig must not create anything.
        before = probe(wiz, "R0", "[lastcreate(me,r)]")
        cmd(wiz, "@dig HookNoEvalRoom", 1.5)
        after = probe(wiz, "R1", "[lastcreate(me,r)]")
        check(after == before,
              "#1280 NOEVAL permit hook denying '0' blocks the command",
              "lastcreate before=%r after=%r" % (before, after))

        # Flip the literal to "1" and the same hook must now allow it —
        # otherwise the fix would just be blocking NOEVAL hooks outright
        # rather than reading their content.
        cmd(wiz, "&P_@dig %s=1" % HOOKOBJ)
        cmd(wiz, "@set %s/P_@dig=NO_EVAL" % HOOKOBJ)
        before2 = probe(wiz, "R2", "[lastcreate(me,r)]")
        cmd(wiz, "@dig HookNoEvalRoom2", 1.5)
        after2 = probe(wiz, "R3", "[lastcreate(me,r)]")
        check(after2 != before2,
              "#1280 NOEVAL permit hook allowing '1' still permits",
              "lastcreate before=%r after=%r" % (before2, after2))
    finally:
        cleanup()
        try:
            wiz.close()
        except OSError:
            pass

    print("=== hook-noeval scenario: %d passed, %d failed ===" % (npass, nfail))
    return 1 if nfail else 0


if __name__ == "__main__":
    sys.exit(main())
