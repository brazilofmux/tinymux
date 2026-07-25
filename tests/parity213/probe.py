#!/usr/bin/env python3
"""Drive a live netmux over a socket and report one result per corpus line.

Both engines under comparison are driven through this same script — a real
server on a scratch port — so a difference in the output cannot be an
artifact of one side running under muxscript and the other under netmux.

Output: one `NAME<TAB>VALUE` line per corpus entry, in corpus order.
A line that produced nothing reports `<NO-OUTPUT>` rather than being
dropped, so a silently-missing shape cannot masquerade as agreement.

Reading is driven by a per-case sentinel rather than fixed sleeps: each
case is followed by `think ZZEND<name>ZZ`, and the reader waits for that
sentinel before moving on.  Sleep-based collection produced intermittent
missing rows when several servers were running at once, and an
intermittently missing row reads as a divergence, which is worse than no
harness at all.

Usage: probe.py <port> <corpus-file> [--login USER PASS]
"""
import socket
import sys
import time

MARK = "ZZ"

# Recognised values for the optional third column.  Kept in sync with
# run.sh; see tests/parity213/README.md for what each means.
VERDICTS = {"2.13", "2.14", "both", "neither", "pin"}
CASE_TIMEOUT = 10.0     # per-case wait for its sentinel
SETTLE = 0.05


def read_corpus(path):
    out = []
    with open(path) as fh:
        for line in fh:
            line = line.rstrip("\n")
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            if "|" not in line:
                continue
            # NAME|expression[|verdict].  The verdict column is consumed
            # by run.sh, not here.  It is recognised by VALUE rather than
            # by field count: '|' is a common MUX delimiter, so an
            # expression may legitimately contain one and a positional
            # rule would truncate it.
            parts = line.split("|")
            name = parts[0].strip()
            if len(parts) > 2 and parts[-1].strip() in VERDICTS:
                expr = "|".join(parts[1:-1])
            else:
                expr = "|".join(parts[1:])
            out.append((name, expr))
    return out


class Reader:
    """Line-buffered socket reader."""

    def __init__(self, sock):
        self.sock = sock
        self.buf = b""
        self.lines = []

    def pump(self, timeout):
        self.sock.settimeout(timeout)
        try:
            d = self.sock.recv(65536)
        except socket.timeout:
            return False
        if not d:
            return False
        self.buf += d
        while b"\n" in self.buf:
            raw, _, self.buf = self.buf.partition(b"\n")
            self.lines.append(raw.decode("utf-8", "replace").rstrip("\r"))
        return True

    def drain(self, seconds):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            if not self.pump(0.3):
                continue

    def take(self):
        out, self.lines = self.lines, []
        return out


def main():
    port = int(sys.argv[1])
    corpus = read_corpus(sys.argv[2])
    user, password = "Wizard", "potrzebie"
    if "--login" in sys.argv:
        i = sys.argv.index("--login")
        user, password = sys.argv[i + 1], sys.argv[i + 2]

    s = socket.socket()
    s.settimeout(10)
    s.connect(("127.0.0.1", port))
    r = Reader(s)
    r.drain(1.0)
    r.take()
    s.sendall(f"connect {user} {password}\r\n".encode())
    r.drain(1.5)
    r.take()

    results = {}
    for name, expr in corpus:
        sentinel = f"{MARK}END{name}{MARK}"
        # say/pose/@... are commands in their own right; everything else is
        # an expression and is wrapped in `think` with markers.
        if expr.lstrip().startswith(("say ", "pose ", "@", ":")):
            s.sendall((expr + "\r\n").encode())
        else:
            s.sendall(f"think {MARK}{name}{MARK}{expr}{MARK}\r\n".encode())
        s.sendall(f"think {sentinel}\r\n".encode())

        collected = []
        deadline = time.monotonic() + CASE_TIMEOUT
        saw = False
        while time.monotonic() < deadline and not saw:
            r.pump(0.3)
            for line in r.take():
                if line == sentinel:
                    saw = True
                    break
                collected.append(line)
        if not saw:
            results[name] = "<NO-OUTPUT>"
            continue

        vals = []
        for line in collected:
            if line.startswith(MARK) and line.count(MARK) >= 3:
                body = line[len(MARK):]
                _n, _, rest = body.partition(MARK)
                vals.append(rest.rsplit(MARK, 1)[0])
            elif line.strip():
                vals.append(line)
        results[name] = " | ".join(vals) if vals else ""
        time.sleep(SETTLE)

    s.close()
    for name, _expr in corpus:
        print(f"{name}\t{results.get(name, '<NO-OUTPUT>')}")


if __name__ == "__main__":
    main()
