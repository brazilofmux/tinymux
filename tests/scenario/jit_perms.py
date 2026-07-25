#!/usr/bin/env python3
# Live scenario test for function-permission enforcement on the JIT path
# (#1124) and the internal-helper exemption that fix depends on.
#
# Why this cannot be a smoke test: the smoke suite and the JIT q-register
# oracle both run as #1 (Wizard/God), so check_access passes for them no
# matter what.  A regression that re-opens the bypass, OR one that breaks
# the helper exemption and denies the compiler its own CA_GOD helpers, is
# invisible unless the caller is an actual mortal.  This creates one.
#
# What it pins:
#   1. A mortal cannot reach a CA_WIZARD builtin through JIT-compiled
#      softcode.  ast.cpp has always enforced fp->perms; ecall_invoke_fun
#      did not, so engine_api_table (which holds every builtin) was a way
#      around it.  Control-tested: with the gate removed, lports() returns
#      real descriptor data to the mortal here.
#   2. A mortal can still use u()/u()-inlining, which is the path that
#      ECALLs _SAVE_QREGS / _RESTORE_QREGS / _WRITE_CARG.  Those are
#      CA_GOD on purpose, and the gate exempts them by their leading
#      underscore.  A blanket check_access would break every mortal's
#      softcode, and nothing else in the estate would notice.
#   3. A wizard is still allowed through (the gate is not over-broad).
#
# Note: on a build without --enable-jit these assertions still hold via
# the AST path, so the test passes but is not discriminating.  It reports
# whether the JIT is live so a green run is not over-read.
#
# Driven by tests/scenario/run.sh.  Usage: jit_perms.py [host] [port]

import socket
import sys
import time

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 6250

# Starter-DB #1 is "Wizard"; the traditional starter password is "potrzebie".
WIZ_LOGIN = "connect Wizard potrzebie"

# Mortal created for this run.  The scenario DB is a throwaway copy of the
# starter DB, so a fixed name cannot collide across runs.
MORTAL = "jitperm"
MORTAL_PW = "hunter2"

NOPERM = "#-1 PERMISSION DENIED"


def sendline(sock, line):
    sock.sendall(line.encode("utf-8") + b"\r\n")


def read_for(sock, marker, timeout=5.0):
    # Poll rather than sleep: softcode replies come off the command queue
    # and can lag the trigger by a loop iteration.
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
    # Evaluate `expr` and echo it back inside a unique marker so partial
    # reads and queue lag cannot be mistaken for a result.
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
        wiz = connect(WIZ_LOGIN)
    except OSError as e:
        print("not ok - could not connect to %s:%d (%s)" % (HOST, PORT, e))
        return 1

    # Report whether the JIT is actually compiling, so a green run on a
    # non-JIT build is not read as proof the gate works.
    jit = probe(wiz, "JITQ", "[jitstats()]") or ""
    print("# jit: %s" % ("active" if "eval_attempts" in jit else
                         "not detected (assertions still valid via AST)"))

    sendline(wiz, "@pcreate %s=%s" % (MORTAL, MORTAL_PW))
    read_for(wiz, "created", 3.0)

    # A wizard must still be able to call the restricted builtin.
    wiz_lports = probe(wiz, "WLP", "[lports()]")
    check(wiz_lports is not None and NOPERM not in wiz_lports,
          "wizard may still call lports()",
          " (got %r)" % wiz_lports)
    wiz.close()

    try:
        mortal = connect("connect %s %s" % (MORTAL, MORTAL_PW))
    except OSError as e:
        print("not ok - mortal could not connect (%s)" % e)
        return 1

    # Install the attributes first; u() forces evaluation through the
    # compiled path rather than the caller's immediate expression.
    for cmd in [
        "&fn.lports me=[lports()]",
        "&fn.cachestats me=[cachestats()]",
        "&fn.inner me=inlined-ok",
        "&fn.outer me=[u(fn.inner)]",
    ]:
        sendline(mortal, cmd)
    read_for(mortal, None, 1.0)

    # 1. CA_WIZARD builtins must be denied for a mortal.  With the #1124
    #    gate removed, lports() returns live descriptor data here.
    got = probe(mortal, "MLP", "[u(fn.lports)]")
    check(got is not None and NOPERM in got,
          "mortal denied CA_WIZARD lports() via JIT",
          " (wanted %r, got %r)" % (NOPERM, got))

    got = probe(mortal, "MCS", "[u(fn.cachestats)]")
    check(got is not None and NOPERM in got,
          "mortal denied CA_WIZARD cachestats() via JIT",
          " (wanted %r, got %r)" % (NOPERM, got))

    # 2. The internal-helper exemption must survive: u() inlining ECALLs
    #    _SAVE_QREGS / _RESTORE_QREGS / _WRITE_CARG, all CA_GOD.
    got = probe(mortal, "MU", "[u(fn.outer)]")
    check(got == "inlined-ok",
          "mortal u() inlining still works (CA_GOD helpers exempt)",
          " (wanted 'inlined-ok', got %r)" % got)

    # 3. A public builtin is unaffected.
    got = probe(mortal, "MADD", "[add(2,3)]")
    check(got == "5",
          "mortal may still call public builtins",
          " (wanted '5', got %r)" % got)

    sendline(mortal, "QUIT")
    try:
        mortal.close()
    except OSError:
        pass

    print("=== jit-perms scenario: %d passed, %d failed ===" % (npass, nfail))
    return 1 if nfail else 0


if __name__ == "__main__":
    sys.exit(main())
