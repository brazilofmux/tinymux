#!/usr/bin/env python3
# Live network+queue stress driver for netmux.
#
# GOAL (deliberately narrow): push the NETWORK and the COMMAND QUEUE hard.
# This is NOT a language / JIT / DBT / database exercise — workloads use the
# cheapest softcode that still generates queue entries and socket traffic, so
# the thing under load is the accept/read/write path and the scheduler, not
# the evaluator or the attribute store.
#
# A MUSH server has REACTIVE DEFENSES, and a stress harness must negotiate
# with them rather than fight them (docs/survey-queue.md):
#
#   * QueueMax (per-owner queue-depth cap): exceeding it HALTS the owner and
#     FLUSHES their entire queue.  A halted object silently queues nothing
#     (setup_que's first check), so a burst that trips the halt makes every
#     LATER command a no-op until the halt is cleared.  The driver therefore
#     (a) clears any leftover halt at startup, (b) raises @queuemax above the
#     integrity load, and (c) LOWERS it deliberately to make the shed
#     reproducible, then recovers.
#   * lnum() is LBUF-bounded (~5000 elements), so a single @dolist fan-out is
#     capped there; bigger depth comes from repeated fan-outs.
#
# Assertions (best-effort semantics — dropping under overload is CORRECT):
#   INTEGRITY  : under a raised cap, a counted fan-out dispatches every entry.
#   CONCURRENCY: many sockets enqueuing at once stay exact and uncorrupted.
#   SHED       : an over-cap burst HALTS+FLUSHES the owner (the defense fires)
#                and the server stays alive and responsive.
#   RECOVERY   : after clearing the halt, the queue works again.
#
# Driven by tests/stress/run.sh.  Usage: stress_driver.py <host> <port>

import socket
import sys
import time

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 6250

LOGIN = "connect Wizard potrzebie"

npass = 0
nfail = 0


def report(ok, msg):
    global npass, nfail
    if ok:
        npass += 1
        print("ok - %s" % msg, flush=True)
    else:
        nfail += 1
        print("not ok - %s" % msg, flush=True)


def note(msg):
    print("# %s" % msg, flush=True)


class Conn:
    def __init__(self):
        self.sock = socket.socket()
        self.sock.settimeout(5)
        self.sock.connect((HOST, PORT))
        self.buf = ""

    def send(self, line):
        self.sock.sendall(line.encode("utf-8") + b"\r\n")

    def drain(self, seconds):
        self.sock.settimeout(0.15)
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            try:
                data = self.sock.recv(65536)
                if not data:
                    break
                self.buf += data.decode("utf-8", "replace")
            except socket.timeout:
                pass

    def login(self):
        self.drain(1.0)
        self.send(LOGIN)
        self.drain(1.5)

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def probe(conn, expr, tag="Q", timeout=8.0):
    # Evaluate a small expression via an immediate `think` (never queued, so
    # it works even when the object is halted) and read back the markered
    # result.
    conn.buf = ""
    conn.send("think %s<[%s]>" % (tag, expr))
    deadline = time.monotonic() + timeout
    conn.sock.settimeout(0.15)
    while time.monotonic() < deadline:
        i = conn.buf.find(tag + "<")
        if i >= 0:
            j = conn.buf.find(">", i)
            if j >= 0:
                return conn.buf[i + len(tag) + 1:j]
        try:
            data = conn.sock.recv(65536)
            if not data:
                break
            conn.buf += data.decode("utf-8", "replace")
        except socket.timeout:
            pass
    return None


def settle(conn, expr, target, tries=40, gap=0.35):
    # Poll `expr` until it reaches `target` or stabilises; return final int.
    last = None
    stable = 0
    val = 0
    for _ in range(tries):
        cur = probe(conn, expr, "S")
        try:
            val = int(cur)
        except (TypeError, ValueError):
            val = last if last is not None else 0
        if str(val) == str(target):
            return val
        if val == last:
            stable += 1
            if stable >= 4:
                return val
        else:
            stable = 0
        last = val
        time.sleep(gap)
    return val


def main():
    try:
        primary = Conn()
    except OSError as e:
        print("not ok - could not connect to %s:%d (%s)" % (HOST, PORT, e))
        print("=== stress: 0 passed, 1 failed ===")
        return 1
    primary.login()

    # Negotiate with the defenses: clear any leftover halt and raise the
    # per-owner queue cap above the integrity/concurrency load.
    primary.send("@set me=!halt")
    primary.send("@queuemax me=10000000")
    primary.send("&BUMP me=&CTR me=[inc(get(me/CTR))]")
    primary.drain(0.4)

    # ----- Phase 0: baseline liveness -----------------------------------
    v = probe(primary, "add(2,2)", "P0")
    report(v == "4", "baseline: server responds (add(2,2)=%s)" % v)

    # ----- Phase 1: counted fan-out (INTEGRITY) -------------------------
    N = 5000
    primary.send("&CTR me=0")
    primary.drain(0.3)
    t0 = time.monotonic()
    primary.send("@dolist [lnum(%d)]=@trigger me/BUMP" % N)
    final = settle(primary, "get(me/CTR)", N, tries=60, gap=0.3)
    dt = time.monotonic() - t0
    report(final == N,
           "fan-out integrity: %d/%d entries dispatched" % (final, N))
    if dt > 0:
        note("fan-out throughput: ~%.0f queue entries/sec" % (final / dt))

    # ----- Phase 2: concurrent enqueue from many sockets (CONCURRENCY) --
    NCONN = 16
    PER = 500
    conns = []
    try:
        for _ in range(NCONN):
            c = Conn()
            c.login()
            conns.append(c)
        report(len(conns) == NCONN,
               "concurrency: %d simultaneous logins" % len(conns))
        primary.send("&CTR me=0")
        primary.drain(0.3)
        for c in conns:
            # All connections are the same Wizard (#1); every fan-out enqueues
            # into #1's single queue at once — accept/read + enqueue pressure.
            c.send("@dolist [lnum(%d)]=@trigger #1/BUMP" % PER)
        for _ in range(20):
            for c in conns:
                c.drain(0.03)
        expected = NCONN * PER
        cval = settle(primary, "get(me/CTR)", expected, tries=80, gap=0.3)
        report(cval == expected,
               "concurrent enqueue: %d/%d entries from %d sockets"
               % (cval, expected, NCONN))
    finally:
        for c in conns:
            c.close()

    # ----- Phase 3: over-cap burst provokes the shed (SHED/SURVIVAL) ----
    # Lower the cap so a fan-out reliably exceeds it; the defense should HALT
    # the owner and FLUSH the queue.  The server must NOT crash and must stay
    # responsive.  How much ran before the flush is reported, not asserted.
    primary.send("@queuemax me=3000")
    primary.send("@set me=!halt")
    primary.send("&CTR me=0")
    primary.drain(0.3)
    # One 5000-element fan-out already exceeds the 3000 cap; fire several.
    for _ in range(6):
        primary.send("@dolist [lnum(5000)]=@trigger me/BUMP")
    primary.drain(2.5)
    halted = probe(primary, "hasflag(me,Halted)", "P3H", timeout=8.0)
    report(halted == "1",
           "shed: over-cap burst halted+flushed the owner (defense fired)")
    alive = probe(primary, "add(40,2)", "P3A", timeout=8.0)
    report(alive == "42", "survival: server responsive during shed (=%s)"
           % alive)
    ran = probe(primary, "get(me/CTR)", "P3C", timeout=8.0)
    note("shed burst ran %s entries before flush (flush is WAI)" % ran)

    # A brand-new connection must still be served while #1 is halted.
    try:
        fresh = Conn()
        fresh.login()
        fv = probe(fresh, "add(7,7)", "P3F", timeout=10.0)
        report(fv == "14",
               "availability: new connection served during shed (=%s)" % fv)
        fresh.close()
    except OSError as e:
        report(False, "availability: new connection failed (%s)" % e)

    # ----- Phase 4: recovery --------------------------------------------
    primary.send("@set me=!halt")
    primary.send("@queuemax me=10000000")
    primary.send("&CTR me=0")
    primary.drain(0.3)
    primary.send("@dolist [lnum(200)]=@trigger me/BUMP")
    rec = settle(primary, "get(me/CTR)", 200, tries=30, gap=0.3)
    report(rec == 200, "recovery: queue works again after shed (%d/200)" % rec)

    primary.close()
    print("=== stress: %d passed, %d failed ===" % (npass, nfail))
    return 1 if nfail else 0


if __name__ == "__main__":
    sys.exit(main())
