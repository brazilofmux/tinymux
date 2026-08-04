#!/usr/bin/env python3
#
# driver.py — Drive a live netmux through named, separately-measured phases.
#
# WHY THIS EXISTS.  The only perf harness we had (testcases/tools/PerfSmoke)
# profiles `muxscript` running the smoke suite: no network, no descriptors, no
# database writes, and no dump.  It is a fine *compiler* benchmark and a
# misleading *game* benchmark — measured on 2026-08-04, 46-50% of that profile
# is JIT compilation, 4.3% is JIT-generated code executing, and all of sqlite3
# is 0.04%.  A profile of a real game has to include the paths muxscript can
# never reach, and it has to keep them apart, because a single blended
# percentage answers a question nobody asked.
#
# So: each phase does ONE kind of work, is timed on its own, and records its
# CLOCK_MONOTONIC window so `perf report --time` can slice the same run into
# per-phase profiles.  Phases are never averaged together here; the mix that
# approximates any particular game is a modelling assumption, and it belongs to
# whoever is reading the numbers, not to the harness.
#
# NEGOTIATING WITH THE SERVER'S DEFENSES (see tests/stress/stress_driver.py):
# QueueMax halts and flushes an owner who exceeds it, and a halted object
# silently queues nothing.  run.sh raises the quotas; this driver additionally
# avoids queueing constructs in the throughput phases, so what is being timed
# is the command path rather than the scheduler's shedding behaviour.
#
# Usage: driver.py <host> <port> <phases.json out> [--scale N]

import json
import socket
import sys
import time

HOST = sys.argv[1]
PORT = int(sys.argv[2])
OUTPATH = sys.argv[3]
SCALE = 1.0
if "--scale" in sys.argv:
    SCALE = float(sys.argv[sys.argv.index("--scale") + 1])
SERVER_PID = None
if "--pid" in sys.argv:
    SERVER_PID = int(sys.argv[sys.argv.index("--pid") + 1])

TICK = 0.01          # os.sysconf('SC_CLK_TCK') is 100 on every Linux we run on


def cpu_times():
    """(user, sys) seconds consumed by the server so far, from /proc.

    Sampling cannot see kernel time on this class of box: kptr_restrict blocks
    /proc/kallsyms, so every in-kernel sample lands in an unsymbolized
    [unknown] bucket that was 72-100% of each phase on the first run.  For a
    server that spends its life in epoll_wait/read/write that makes the
    profile unreadable and every user-space number look trivially small.
    /proc/<pid>/stat gives the split exactly and for free, so the harness
    accounts for kernel time here and profiles only user space."""
    if SERVER_PID is None:
        return (0.0, 0.0)
    try:
        with open("/proc/%d/stat" % SERVER_PID) as f:
            data = f.read()
        # comm can contain spaces and parens; fields are positional after ')'.
        fields = data[data.rindex(")") + 2:].split()
        return (int(fields[11]) * TICK, int(fields[12]) * TICK)
    except (OSError, ValueError, IndexError):
        return (0.0, 0.0)

LOGIN = "connect Wizard potrzebie"

# Number of connections held open for the idle phase.  Enough that the event
# loop has real work to poll, small enough to stay well inside any fd limit.
IDLE_CONNS = 32
IDLE_SECONDS = 8.0

# The starter database is about a dozen objects.  Profiling the storage layer
# against it measures nothing -- the first run's @dump took 90ms, because there
# was nothing to write.  Build a database with some actual content first, and
# treat building it as its own measured phase (object creation is a real cost).
OBJECTS = 1500
ATTRS_PER_OBJECT = 12

phases = []


def scaled(n):
    return max(1, int(n * SCALE))


class Conn:
    def __init__(self):
        self.s = socket.create_connection((HOST, PORT), timeout=20)
        self.s.settimeout(20)
        self.buf = b""

    def send(self, line):
        self.s.sendall(line.encode("utf-8", "replace") + b"\n")

    def login(self):
        self.send(LOGIN)
        self.drain(0.5)

    def drain(self, seconds):
        end = time.monotonic() + seconds
        self.s.settimeout(0.2)
        try:
            while time.monotonic() < end:
                try:
                    b = self.s.recv(65536)
                except socket.timeout:
                    continue
                if not b:
                    break
                self.buf += b
        finally:
            self.s.settimeout(20)

    def discard(self):
        """Read whatever is pending without blocking, and throw it away."""
        self.s.setblocking(False)
        try:
            while True:
                try:
                    if not self.s.recv(262144):
                        break
                except (BlockingIOError, socket.timeout):
                    break
        finally:
            self.s.setblocking(True)
            self.s.settimeout(20)

    def wait_for(self, token, timeout=900.0):
        """Read until `token` appears.  Commands are processed in order, so a
        sentinel echoed back means every command sent before it has run."""
        end = time.monotonic() + timeout
        tok = token.encode()
        while tok not in self.buf:
            if time.monotonic() > end:
                raise RuntimeError("timed out waiting for %s" % token)
            try:
                b = self.s.recv(65536)
            except socket.timeout:
                continue
            if not b:
                raise RuntimeError("connection closed waiting for %s" % token)
            self.buf += b
        self.buf = self.buf[self.buf.index(tok) + len(tok):]

    def close(self):
        try:
            self.s.close()
        except OSError:
            pass


def phase(name, note, fn):
    """Run fn(), recording its monotonic window and whatever count it returns."""
    u0, s0 = cpu_times()
    t0 = time.monotonic()
    count = fn()
    t1 = time.monotonic()
    u1, s1 = cpu_times()
    rec = {
        "name": name,
        "note": note,
        "start": t0,
        "stop": t1,
        "seconds": t1 - t0,
        "count": count,
        "user_s": round(u1 - u0, 3),
        "sys_s": round(s1 - s0, 3),
    }
    if count:
        rec["per_sec"] = count / (t1 - t0) if t1 > t0 else 0.0
        rec["usec_each"] = (t1 - t0) * 1e6 / count
    phases.append(rec)
    print("  %-16s %8.3fs  usr=%6.2f sys=%6.2f  %-8s %s"
          % (name, rec["seconds"], rec["user_s"], rec["sys_s"],
             ("%d" % count) if count else "-",
             ("%.1f/s" % rec["per_sec"]) if count else note))
    return rec


def batch(c, lines, tag, chunk=256):
    """Send `lines`, then a sentinel, and wait for the sentinel to come back.

    A sentinel goes out with EVERY chunk and is waited for, which bounds the
    server's unread output to one chunk.  An earlier version sent the whole
    phase and drained opportunistically between chunks; at 1000 commands that
    worked, and at 20000 it wedged -- the replies backed up faster than the
    opportunistic drain cleared them, and the phase stopped measuring command
    cost and started measuring output buffering.  Waiting per chunk costs one
    round-trip per 256 commands, which is noise against the work."""
    tok = "SENT%s" % tag
    for i in range(0, len(lines), chunk):
        for ln in lines[i:i + chunk]:
            c.send(ln)
        c.send("@pemit me=%s" % tok)
        c.wait_for(tok)
    return len(lines)


def main():
    c = Conn()
    c.login()
    c.send("@pemit me=HELLO")
    c.wait_for("HELLO")

    # ---- create: object creation, and the database this run needs ----------
    nobj = scaled(OBJECTS)
    phase("create", "@create x%d" % nobj,
          lambda: batch(c, ["@create perfobj%d" % i for i in range(nobj)], "C"))

    # ---- attrwrite: the database write path --------------------------------
    # Spread across every object rather than hammering one, so this exercises
    # the attribute store rather than one object's attribute list.
    writes = [
        "&perf%d perfobj%d=value %d for attribute %d" % (a, o, o, a)
        for o in range(nobj)
        for a in range(ATTRS_PER_OBJECT)
    ]
    phase("attrwrite", "&attr x%d over %d objects" % (len(writes), nobj),
          lambda: batch(c, writes, "W"))

    # ---- dump: the storage dump path ---------------------------------------
    # muxscript cannot reach this at all, and against the starter database it
    # is meaningless -- hence the create/attrwrite phases above.
    def do_dump():
        c.send("@dump")
        c.send("@pemit me=DUMPED")
        c.wait_for("DUMPED", timeout=600)
        return 0

    phase("dump", "@dump", do_dump)

    # ---- attrread: the database read path ----------------------------------
    reads = [
        "@pemit me=[get(perfobj%d/perf%d)]" % (i % nobj, i % ATTRS_PER_OBJECT)
        for i in range(scaled(20000))
    ]
    phase("attrread", "get() x%d across %d objects" % (len(reads), nobj),
          lambda: batch(c, reads, "R"))

    # ---- idle: connected sessions, no commands -----------------------------
    # Measures the event-loop and timer floor with descriptors attached.  The
    # connect and login cost is deliberately OUTSIDE the window: the first run
    # of this harness put it inside, and reported a 24s "idle" phase that was
    # 16s of logging in.  There is no throughput here, only a floor.
    idle = []
    for _ in range(IDLE_CONNS):
        ic = Conn()
        ic.login()
        idle.append(ic)
    time.sleep(0.5)

    def do_idle():
        time.sleep(IDLE_SECONDS)
        return 0

    phase("idle", "%d idle sessions" % IDLE_CONNS, do_idle)

    for ic in idle:
        ic.close()
    idle.clear()
    c.send("@pemit me=REAPED")
    c.wait_for("REAPED")
    time.sleep(0.5)

    # ---- dispatch: built-in command, no softcode evaluation ----------------
    n = scaled(20000)
    phase("dispatch", "look",
          lambda: batch(c, ["look"] * n, "D"))

    # ---- softcode_arith: JIT-friendly arithmetic ---------------------------
    # Not constant-foldable: name(me) is not known at compile time, so the
    # compiled code actually has to run.
    n = scaled(20000)
    expr = "@pemit me=[add(mul(strlen(name(me)),7),3)]"
    phase("softcode_arith", "add/mul/strlen",
          lambda: batch(c, [expr] * n, "A"))

    # ---- softcode_list: iter()/list functions ------------------------------
    # The prediction under test: list functions get little or no JIT benefit
    # because everything round-trips through string<->number marshalling.  If
    # that is right, the per-command cost here is far above softcode_arith
    # even though the arithmetic per command is only 50 multiplies.
    # Larger than it looks it needs to be: at 2000 this phase spent 0.10s of
    # server CPU and repeated to only 29% run to run, against 1.0% for a phase
    # that spends a full second.  Stability here tracks work done.
    n = scaled(8000)
    expr = "@pemit me=[iter(lnum(50),mul(##,2))]"
    phase("softcode_list", "iter(lnum(50),mul(##,2))",
          lambda: batch(c, [expr] * n, "L"))

    c.send("@pemit me=BYE")
    c.wait_for("BYE")
    c.close()

    with open(OUTPATH, "w") as f:
        json.dump({"phases": phases, "scale": SCALE}, f, indent=2)


if __name__ == "__main__":
    try:
        main()
    except Exception as e:            # noqa: BLE001 - report and fail the run
        print("driver failed: %s" % e, file=sys.stderr)
        sys.exit(1)
