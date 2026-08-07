#!/usr/bin/env python3
#
# restart_helpers.py — live scenario test for #2192.
#
# @restart execs in place.  Whatever the old image failed to tear down is
# inherited by the new one, and nothing later cleans it up, so a leak here is
# permanent and accumulates one generation per restart.
#
# Two independent failures produced that, and each needs its own assertion
# because fixing either one alone still leaves a leak:
#
#   - The parent's end of the stubslave socketpair was not FD_CLOEXEC, so the
#     exec carried it into the new image.  The old stubslave never saw EOF and
#     parked in poll() FOREVER -- a live orphan, plus a dead fd in the new
#     process's table.
#   - Helpers that exit during teardown are inherited as zombies.  The reap is
#     SIGCHLD-driven, and in steady state no child ever exits, so nothing
#     triggers it and the zombie is permanent.
#
# Measured on the unfixed tree, three @restarts: 3 live orphan stubslaves,
# 3 zombie slaves, 3 leaked fds.  With CLOEXEC but no reap: 6 zombies.  The
# check below fails on either.
#
# This runs LAST in run.sh's driver list and restarts the shared server, so
# nothing after it may depend on that server.
#
# Usage: restart_helpers.py [host] [port] [netmux_pid]

import re
import socket
import subprocess
import sys
import time

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 6250
NETMUX_PID = int(sys.argv[3]) if len(sys.argv) > 3 else 0

WIZ_LOGIN = "connect Wizard potrzebie"

# Helper process names as they appear in ps(1).
HELPERS = ("stubslave", "slave")


def cmdline_of(pid):
    """Full argv of `pid` as a string, or "" if it cannot be read.

    `ps -o args=` rather than /proc so this also means something on macOS.
    """
    try:
        out = subprocess.run(["ps", "-o", "args=", "-p", str(pid)],
                             capture_output=True, text=True, timeout=10).stdout
    except (OSError, subprocess.SubprocessError):
        return ""
    return out.strip()


def children_of(pid):
    """[(pid, stat, comm)] for every direct child of `pid`.

    `ps -eo` rather than `--ppid`: the latter is a GNU extension that macOS
    ps does not accept, and this harness runs there too.
    """
    try:
        out = subprocess.run(["ps", "-eo", "pid=,ppid=,stat=,comm="],
                             capture_output=True, text=True, timeout=10).stdout
    except (OSError, subprocess.SubprocessError):
        return None
    kids = []
    for line in out.splitlines():
        parts = line.split(None, 3)
        if len(parts) < 4:
            continue
        try:
            cpid, cppid = int(parts[0]), int(parts[1])
        except ValueError:
            continue
        if cppid == pid:
            # comm may carry a path on some platforms; keep the basename.
            kids.append((cpid, parts[2], parts[3].strip().split("/")[-1]))
    return kids


def helpers(kids):
    return [k for k in kids if any(h in k[2] for h in HELPERS)]


def zombies(kids):
    return [k for k in kids if k[1].startswith("Z")]


def drain(sock, seconds=1.2):
    sock.settimeout(seconds)
    buf = b""
    try:
        while True:
            d = sock.recv(65536)
            if not d:
                break
            buf += d
    except Exception:
        pass
    return buf.decode("utf-8", "replace")


def main():
    npass = nfail = 0

    def check(ok, msg, detail=""):
        nonlocal npass, nfail
        if ok:
            npass += 1
            print("ok %d - %s%s"
                  % (npass + nfail, msg, ("  # %s" % detail) if detail else ""))
        else:
            nfail += 1
            print("not ok %d - %s%s"
                  % (npass + nfail, msg, (" (%s)" % detail) if detail else ""))

    if NETMUX_PID <= 0:
        print("ok - restart-helpers  # SKIP no netmux pid supplied")
        return 0

    before = children_of(NETMUX_PID)
    if before is None:
        print("ok - restart-helpers  # SKIP ps(1) unavailable")
        return 0

    before_helpers = helpers(before)
    if not any("stubslave" in k[2] for k in before_helpers):
        # Not a --enable-stubslave build (or helpers run in-process).  There
        # is no leak to observe; skip loudly rather than pass vacuously.
        print("ok - restart-helpers  # SKIP no stubslave child "
              "(tree not built --enable-stubslave?)")
        return 0

    before_pids = sorted(k[0] for k in before_helpers)
    before_cmdline = cmdline_of(NETMUX_PID)

    try:
        s = socket.socket()
        s.settimeout(5)
        s.connect((HOST, PORT))
        drain(s, 1.5)
        s.sendall(WIZ_LOGIN.encode() + b"\r\n")
        drain(s, 2.0)
    except OSError as e:
        print("not ok - could not connect to %s:%d (%s)" % (HOST, PORT, e))
        return 1

    # @restart is refused for the first 15s of uptime (timer.cpp arms
    # mudstate.bCanRestart on a deferred task).  By the time this driver runs
    # the server has been up for the whole suite, but retry rather than assume.
    restarted = False
    for _ in range(12):
        s.sendall(b"@restart\r\n")
        reply = drain(s, 2.0)
        if "just started" not in reply:
            restarted = True
            break
        time.sleep(3)
    try:
        s.close()
    except OSError:
        pass

    if not restarted:
        print("not ok - server never accepted @restart (still warming up?)")
        return 1

    # Wait for the successor to come back up on the same port.
    back = False
    for _ in range(40):
        time.sleep(0.5)
        probe = socket.socket()
        probe.settimeout(0.5)
        ok = probe.connect_ex((HOST, PORT)) == 0
        probe.close()
        if ok:
            back = True
            break
    check(back, "server is serving again after @restart")
    if not back:
        return 1

    time.sleep(2.0)   # let the old generation finish exiting

    after = children_of(NETMUX_PID)
    after_helpers = helpers(after or [])
    after_pids = sorted(k[0] for k in after_helpers)

    # The restart really happened: a fresh helper generation, not the old one.
    check(bool(after_pids) and after_pids != before_pids,
          "helpers were respawned (restart actually took effect)",
          "before=%s after=%s" % (before_pids, after_pids))

    # #2192 proper.  Both halves of the fix are needed to satisfy this: without
    # CLOEXEC the old stubslave is still alive (S) and counted here; with
    # CLOEXEC but no reap it is a zombie and counted by the next check.
    check(len(after_helpers) == len(before_helpers),
          "#2192 no helper generation leaked across @restart",
          "before=%d after=%d (%s)"
          % (len(before_helpers), len(after_helpers),
             ", ".join("%d/%s/%s" % k for k in after_helpers)))

    zs = zombies(after or [])
    check(not zs,
          "#2192 no zombie helpers inherited across @restart",
          "zombies=%s" % ([k[0] for k in zs],))

    # #2199, checked here because this driver already pays for a restart.
    # do_restart() rebuilds its execl argv from mudconf.pid_file and
    # mudconf.log_dir; those were read in three places and written in none,
    # so every restart exec'd with an empty -p and dropped whatever pidfile
    # path the operator gave.  run.sh starts this server with -p, so the
    # successor's argv is the direct evidence.
    after_cmdline = cmdline_of(NETMUX_PID)
    if "-p" not in before_cmdline:
        print("ok - #2199 pidfile argument preserved  # SKIP server not started with -p")
    else:
        m = re.search(r"-p\s+(\S+)", before_cmdline)
        want = m.group(1) if m else None
        got = re.search(r"-p\s+(\S+)", after_cmdline)
        check(want is not None and got is not None and got.group(1) == want,
              "#2199 -p value survives the restart exec",
              "before=%r after=%r" % (before_cmdline, after_cmdline))

    print("=== restart-helpers scenario: %d passed, %d failed ==="
          % (npass, nfail))
    return 1 if nfail else 0


if __name__ == "__main__":
    sys.exit(main())
