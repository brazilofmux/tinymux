#!/usr/bin/env python3
"""Regression test for probe.py's reader — the harness testing itself (#1233).

probe.py's failure mode was intermittent and load-dependent: under three
concurrent engines a sentinel would occasionally arrive after its case had
timed out, and the reader would attribute it to the *next* case.  "I ran it
a few times and did not see it" is not evidence that such a bug is fixed,
so this drives the same paths deterministically against a scripted stand-in
for netmux.

The server here is reactive rather than time-scripted: it replies to the
client's commands, so the test does not depend on probe.py's startup
timing (an earlier time-scripted version had its output swallowed by the
login flush).

Two shapes, both of which the pre-fix reader got wrong:

  late-sentinel   A case's sentinel arrives only after its window closes.
                  Pre-fix: that case reported <NO-OUTPUT> and its sentinel
                  was collected into the next case's value, e.g.
                  'ZZENDAZZ | 7'.  Post-fix: the late sentinel is
                  recognised as stale, the banked lines go back to the case
                  they belong to, and the next case is unaffected.

  batched         A case's output and sentinel share a TCP segment with the
                  following case's.  Pre-fix the reader consumed the whole
                  batch and discarded everything after the sentinel, losing
                  the next case entirely.

Usage: selftest.py        (exit 0 = pass, 1 = fail)
"""
import os
import socket
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
PROBE = os.path.join(HERE, "probe.py")
CASE_TIMEOUT = 1.0          # what probe.py is told to use
MARK = "ZZ"

# Faked evaluation results, keyed by case name.
VALUES = {"A": "3", "B": "7", "C": "11"}


def serve(sock, mode):
    """Accept one connection and answer probe.py's commands.

    `mode` selects which pathological delivery to reproduce.
    """
    conn, _ = sock.accept()
    conn.sendall(b"Welcome.\r\n")
    conn.settimeout(30)
    buf = b""
    held = []               # batched mode: output withheld to coalesce

    def line_for(name):
        return f"{MARK}{name}{MARK}{VALUES[name]}{MARK}\r\n"

    def sentinel_for(name):
        return f"{MARK}END{name}{MARK}\r\n"

    try:
        while True:
            try:
                d = conn.recv(65536)
            except socket.timeout:
                break
            if not d:
                break
            buf += d
            while b"\n" in buf:
                raw, _, buf = buf.partition(b"\n")
                cmd = raw.decode("utf-8", "replace").strip()
                if not cmd.startswith("think "):
                    continue
                body = cmd[len("think "):]

                if body.startswith(f"{MARK}END"):
                    name = body[len(MARK) + 3:-len(MARK)]
                    if mode == "late" and name == "A":
                        # Hold A's sentinel past its window.  Blocking here
                        # also mirrors a genuinely slow server: the client's
                        # later commands queue in the socket meanwhile.
                        time.sleep(CASE_TIMEOUT + 0.6)
                        conn.sendall(sentinel_for(name).encode())
                    elif mode == "batch":
                        held.append(sentinel_for(name))
                        if name == "B":
                            conn.sendall("".join(held).encode())
                            held.clear()
                    else:
                        conn.sendall(sentinel_for(name).encode())
                else:
                    name = body[len(MARK):].partition(MARK)[0]
                    if name not in VALUES:
                        continue
                    if mode == "batch":
                        held.append(line_for(name))
                    else:
                        conn.sendall(line_for(name).encode())
    except OSError:
        pass
    finally:
        time.sleep(0.2)
        try:
            conn.close()
        except OSError:
            pass


def run_probe(mode, names):
    """Run probe.py against the stand-in server; return {name: value}."""
    srv = socket.socket()
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", 0))
    srv.listen(1)
    port = srv.getsockname()[1]

    t = threading.Thread(target=serve, args=(srv, mode), daemon=True)
    t.start()

    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as fh:
        fh.write("\n".join(f"{n}|[add(1,2)]" for n in names) + "\n")
        corpus = fh.name

    env = dict(os.environ, PARITY_CASE_TIMEOUT=str(CASE_TIMEOUT))
    proc = subprocess.run([sys.executable, PROBE, str(port), corpus],
                          capture_output=True, text=True, env=env,
                          timeout=120)
    os.unlink(corpus)
    srv.close()

    if proc.returncode != 0:
        print(f"# probe.py exited {proc.returncode}: {proc.stderr.strip()}")

    results = {}
    for line in proc.stdout.splitlines():
        if "\t" in line:
            name, _, value = line.partition("\t")
            results[name] = value
    return results


def check(label, results, expected):
    ok = True
    for name, want in expected.items():
        got = results.get(name)
        if got != want:
            print(f"not ok - {label}: {name}={got!r}, want {want!r}")
            ok = False
    if ok:
        print(f"ok - {label}")
    return ok


def main():
    print("==> probe.py reader selftest (#1233)")

    # A's sentinel lands after A's window; A must still be recovered and B
    # must not inherit it.
    late = run_probe("late", ["A", "B", "C"])
    r1 = check("late sentinel recovered; following case unaffected",
               late, {"A": "3", "B": "7", "C": "11"})

    # A and B delivered in one segment; neither may be dropped.
    batch = run_probe("batch", ["A", "B"])
    r2 = check("batched cases both recovered from one segment",
               batch, {"A": "3", "B": "7"})

    passed = sum(1 for x in (r1, r2) if x)
    print(f"=== probe selftest: {passed} passed, {2 - passed} failed ===")
    return 0 if (r1 and r2) else 1


if __name__ == "__main__":
    sys.exit(main())
