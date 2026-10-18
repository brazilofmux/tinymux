#!/usr/bin/env python3
#
# page_noeval.py — live scenario test for #2310 (page/noeval with no '=').
#
# page is CS_TWO_ARG|CS_INTERP.  /noeval clears `interp`, which governs only
# the text after '='; ARG1 was always evaluated, because for most two-arg
# commands ARG1 names a target.  page's third form has no '=', and do_page
# uses the lone ARG1 as the *message* -- so `page/noeval <message>` was
# evaluated anyway, silently ignoring the switch that help advertised.
#
# The cases are chosen so a wrong fix cannot pass:
#
#   - the evaluated controls (no /noeval) must still evaluate, so "never
#     evaluate a lone ARG1" fails;
#   - the lone form must produce exactly what the '=' form produces for the
#     same text, braces and %-substitutions included, so a fix that strips
#     differently from ARG2's /noeval path fails;
#   - with '=', the recipient list must still be evaluated under /noeval:
#     only the message is literal.
#
# Smoke cannot cover this: test cases run as a created object, and only a
# player can be paged.
#
# Driven by tests/scenario/run.sh.  Usage: page_noeval.py [host] [port]

import socket
import sys
import time

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 6250

WIZ_LOGIN = "connect Wizard potrzebie"


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


def received(sock, command, tag):
    """Send a page command whose message starts with `tag`; return the text
    the recipient saw after 'pages: ', or None."""
    sendline(sock, command)
    buf = read_for(sock, "You paged", 5.0)
    for line in buf.splitlines():
        marker = "pages: " + tag
        i = line.find(marker)
        if i >= 0:
            return line[i + len("pages: "):].rstrip("\r")
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
        s = connect(WIZ_LOGIN)
    except OSError as e:
        print("not ok - could not connect to %s:%d (%s)" % (HOST, PORT, e))
        return 1

    expr = "[add(1,2)]"

    got = received(s, "page me=PA " + expr, "PA")
    check(got == "PA 3", "page evaluates the message (control)",
          " (got %r)" % got)

    got = received(s, "page/noeval me=PB " + expr, "PB")
    check(got == "PB " + expr, "page/noeval <players>=<message> is literal",
          " (got %r)" % got)

    got = received(s, "page/noeval =PC " + expr, "PC")
    check(got == "PC " + expr, "page/noeval =<message> is literal",
          " (got %r)" % got)

    got = received(s, "page/noeval PD " + expr, "PD")
    check(got == "PD " + expr,
          "#2310 page/noeval <message> (no '=') is literal",
          " (got %r)" % got)

    got = received(s, "page PE " + expr, "PE")
    check(got == "PE 3",
          "#2310 page <message> (no '=') still evaluates without /noeval",
          " (got %r)" % got)

    # Parity: the lone form must treat braces, %-subs and functions exactly
    # as the '=' form's /noeval path does.
    tricky = " {brace} %n [add(1,2)] "
    eq_form = received(s, "page/noeval =PF" + tricky, "PF")
    lone = received(s, "page/noeval PF" + tricky, "PF")
    check(eq_form is not None and lone == eq_form,
          "#2310 lone /noeval message matches the '=' form exactly",
          " ('=' %r, lone %r)" % (eq_form, lone))

    # Only the message is literal: with '=', the recipient list is still
    # evaluated, so [name(me)] must resolve to this player.
    got = received(s, "page/noeval [name(me)]=PG " + expr, "PG")
    check(got == "PG " + expr,
          "page/noeval still evaluates the recipient list",
          " (got %r)" % got)

    s.close()
    print("=== page-noeval scenario: %d passed, %d failed ===" % (npass, nfail))
    return 1 if nfail else 0


if __name__ == "__main__":
    sys.exit(main())
