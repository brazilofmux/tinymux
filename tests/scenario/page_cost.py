#!/usr/bin/env python3
#
# page_cost.py — live scenario test for #1187 (page_check charge ordering).
#
# page_check() used to call payfor() before it checked whether the target was
# connected or whether the PAGE-lock allowed the page.  Those checks return
# false with no refund, so a page that was never delivered still cost the
# sender page_cost.  do_page() runs page_check() once per recipient, so a
# multi-target page multiplied the loss.
#
# This cannot be reached from the smoke corpus: it needs two real player
# sessions and a mortal sender (payfor() exempts wizards outright, so a
# Wizard-only test would pass no matter what the code did).
#
# The pair of assertions matters.  "Failed page does not charge" alone would
# also pass if the fix had simply stopped charging for pages, so the
# successful page is checked to still deduct exactly page_cost.
#
# Driven by tests/scenario/run.sh.  Usage: page_cost.py [host] [port]

import socket
import sys
import time

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 6250

# Starter-DB #1 is "Wizard"; the traditional starter password is "potrzebie".
WIZ_LOGIN = "connect Wizard potrzebie"

# Mortals created for this run.  The scenario DB is a throwaway copy of the
# starter DB, so fixed names cannot collide across runs.
SENDER = "pagesend"
SENDER_PW = "hunter2"
TARGET = "pagerecv"
TARGET_PW = "hunter2"

# Two players that are created but never connect.  Used for the offline and
# multi-recipient cases: distinct names avoid do_page()'s duplicate-dbref
# removal, and never connecting avoids racing an @boot against the page.
OFFLINE1 = "pageoff1"
OFFLINE2 = "pageoff2"
OFFLINE_PW = "hunter2"

# starting_money defaults to 0, which would make every page fail for lack of
# funds and hide the defect.  Fund the sender well above page_cost.
START_MONEY = 1000

# compat.conf ships "page_cost 0", so the scenario server starts with pages
# free -- which cannot distinguish a charge from a refund.  Set it at runtime
# rather than depending on the config default.
PAGE_COST = 5


def sendline(sock, line):
    sock.sendall(line.encode("utf-8") + b"\r\n")


def read_for(sock, marker, timeout=5.0):
    # Poll rather than sleep: softcode replies come off the command queue and
    # can lag the trigger by a loop iteration.
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
    # Evaluate `expr` and echo it back inside a unique marker so partial reads
    # and queue lag cannot be mistaken for a result.
    sendline(sock, "@pemit me=%s<%s>" % (tag, expr))
    buf = read_for(sock, tag + "<", 5.0)
    for line in buf.splitlines():
        if line.startswith(tag + "<") and line.rstrip().endswith(">"):
            return line.strip()[len(tag) + 1:-1]
    return None


def money(sock, tag):
    got = probe(sock, tag, "[money(me)]")
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

    # Make pages cost something, fund new players, then create both mortals.
    sendline(wiz, "@admin page_cost=%d" % PAGE_COST)
    read_for(wiz, None, 1.0)
    sendline(wiz, "@admin starting_money=%d" % START_MONEY)
    read_for(wiz, None, 1.0)
    sendline(wiz, "@pcreate %s=%s" % (SENDER, SENDER_PW))
    read_for(wiz, None, 1.0)
    sendline(wiz, "@pcreate %s=%s" % (TARGET, TARGET_PW))
    read_for(wiz, None, 1.0)
    sendline(wiz, "@pcreate %s=%s" % (OFFLINE1, OFFLINE_PW))
    read_for(wiz, None, 1.0)
    sendline(wiz, "@pcreate %s=%s" % (OFFLINE2, OFFLINE_PW))
    read_for(wiz, None, 1.0)

    try:
        sender = connect("connect %s %s" % (SENDER, SENDER_PW))
    except OSError as e:
        print("not ok - sender could not connect (%s)" % e)
        return 1

    cost = probe(sender, "PC", "[config(page_cost)]")
    try:
        cost = int(cost)
    except (TypeError, ValueError):
        print("not ok - could not read page_cost (%r)" % cost)
        return 1

    if cost <= 0:
        print("not ok - page_cost is %d; the test cannot distinguish a "
              "charge from a refund" % cost)
        return 1

    start = money(sender, "M0")
    if start is None:
        print("not ok - could not read sender money")
        return 1
    check(start > cost, "sender is funded above page_cost",
          "money=%r cost=%d" % (start, cost))

    # 1. Target exists but is not connected.  page_check() rejects, so nothing
    #    is delivered and nothing should be charged.
    sendline(sender, "page %s=hello" % OFFLINE1)
    read_for(sender, "not connected", 5.0)
    after_offline = money(sender, "M1")
    check(after_offline == start,
          "#1187 page to an offline player does not charge",
          "before=%r after=%r cost=%d" % (start, after_offline, cost))

    # 2. Same page, now that the target is connected, must still cost exactly
    #    page_cost -- otherwise the fix merely stopped charging.
    try:
        target = connect("connect %s %s" % (TARGET, TARGET_PW))
    except OSError as e:
        print("not ok - target could not connect (%s)" % e)
        return 1

    before_ok = money(sender, "M2")
    sendline(sender, "page %s=hello" % TARGET)
    read_for(target, "hello", 5.0)
    after_ok = money(sender, "M3")
    check(after_ok is not None and before_ok is not None
          and before_ok - after_ok == cost,
          "a delivered page still costs exactly page_cost",
          "before=%r after=%r cost=%d" % (before_ok, after_ok, cost))

    # 3. Multi-recipient amplification: page_check() runs once per target, so
    #    an all-offline multi-page used to charge once per recipient.  Two
    #    distinct names, so do_page()'s duplicate removal cannot collapse them.
    before_multi = money(sender, "M4")
    sendline(sender, "page %s %s=hello" % (OFFLINE1, OFFLINE2))
    read_for(sender, "not connected", 5.0)
    after_multi = money(sender, "M5")
    check(after_multi == before_multi,
          "#1187 multi-recipient page to offline players does not charge",
          "before=%r after=%r cost=%d" % (before_multi, after_multi, cost))

    for s in (wiz, sender, target):
        try:
            s.close()
        except OSError:
            pass

    print("=== page-cost scenario: %d passed, %d failed ===" % (npass, nfail))
    return 1 if nfail else 0


if __name__ == "__main__":
    sys.exit(main())
