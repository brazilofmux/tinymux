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

The reader is line-at-a-time and self-resynchronizing (#1233).  Two
earlier defects made it lose step under concurrent engines:

  * consuming a whole batch and `break`ing at the sentinel discarded the
    remainder of that batch, so the next case's output vanished; and
  * a sentinel arriving after its case had timed out was collected into
    the *next* case's value, producing rows like
    `ZZENDPN_EMPTYZZ | (a)`.

Because every sentinel carries its own case name, a late one is now
recognised as stale: the lines banked before it are handed back to the
case that timed out (recovering a row that would otherwise read
`<NO-OUTPUT>`) and collection restarts for the case still being awaited.
A desynchronised row is worse than a slow one here, because with the
verdict column it presents as a specification violation.

Usage: probe.py <port> <corpus-file> [--login USER PASS]
"""
import collections
import os
import socket
import sys
import time

MARK = "ZZ"

# Recognised values for the optional third column.  Kept in sync with
# run.sh; see tests/parity213/README.md for what each means.
VERDICTS = {"2.13", "2.14", "both", "neither", "pin"}

# Per-case wait for its sentinel.  Overridable so selftest.py can drive the
# late-sentinel path in seconds rather than tens of them.
#
CASE_TIMEOUT = float(os.environ.get("PARITY_CASE_TIMEOUT", "10.0"))
SETTLE = 0.05


def render(collected):
    """Turn the lines banked for one case into its reported value.

    Marker-wrapped lines yield the text between the markers; anything else
    non-blank is kept verbatim (bare commands such as `say`/`@` echo their
    own output without markers).
    """
    vals = []
    for line in collected:
        if line.startswith(MARK) and line.count(MARK) >= 3:
            body = line[len(MARK):]
            _n, _, rest = body.partition(MARK)
            vals.append(rest.rsplit(MARK, 1)[0])
        elif line.strip():
            vals.append(line)
    return " | ".join(vals) if vals else ""


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
    """Line-buffered socket reader.

    Hands out one line at a time.  The previous interface returned the
    whole buffered batch, and a caller that stopped early (at a sentinel)
    silently dropped the remainder — which is how the next case lost its
    output (#1233).
    """

    def __init__(self, sock):
        self.sock = sock
        self.buf = b""
        self.lines = collections.deque()

    def _fill(self, timeout):
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

    def next_line(self, deadline):
        """Next buffered line, or None once `deadline` (monotonic) passes."""
        while True:
            if self.lines:
                return self.lines.popleft()
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return None
            self._fill(min(0.3, remaining))

    def flush(self, seconds):
        """Read and discard for `seconds` — used for the login banner."""
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            self._fill(0.3)
        self.lines.clear()


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
    r.flush(1.0)
    s.sendall(f"connect {user} {password}\r\n".encode())
    r.flush(1.5)

    # Every sentinel, so a late one can be recognised as stale rather than
    # collected into whichever case happens to be running (#1233).
    #
    sentinel_of = {f"{MARK}END{n}{MARK}": n for n, _ in corpus}

    results = {}

    # Cases that gave up waiting, oldest first, each holding the lines it
    # had already banked.  Their sentinels are still somewhere in the
    # stream; when one arrives, everything received since belongs to that
    # case, so it is rendered from its banked lines plus what has been
    # collected in the meantime (#1233).
    #
    outstanding = []

    for name, expr in corpus:
        sentinel = f"{MARK}END{name}{MARK}"
        # say/pose/@/& are commands in their own right (& sets an
        # attribute, @ covers @function/@set/etc), so they are sent bare.
        # Everything else is an expression and is wrapped in `think`.
        # A bare command still gets its sentinel, so setup lines stay in
        # step with the case they belong to.
        if expr.lstrip().startswith(("say ", "pose ", "@", "&", ":")):
            s.sendall((expr + "\r\n").encode())
        else:
            s.sendall(f"think {MARK}{name}{MARK}{expr}{MARK}\r\n".encode())
        s.sendall(f"think {sentinel}\r\n".encode())

        collected = []
        deadline = time.monotonic() + CASE_TIMEOUT
        saw = False
        while True:
            line = r.next_line(deadline)
            if line is None:
                break
            if line == sentinel:
                saw = True
                break
            stale = sentinel_of.get(line)
            if stale is not None:
                # A sentinel for an earlier case, arriving after that case
                # gave up waiting.  Everything since that case started is
                # its output, not ours: its own banked lines plus whatever
                # has been collected here.  Recovering it turns a row that
                # would have read <NO-OUTPUT> back into a real measurement.
                #
                if outstanding and outstanding[0][0] == stale:
                    stale_name, prior = outstanding.pop(0)
                    results[stale_name] = render(prior + collected)
                collected = []
                # Restart the clock as well as the buffer: the stream is
                # demonstrably running behind, and keeping the original
                # deadline would time this case out too — which is how one
                # slow case used to cascade into a run of empty rows.
                #
                deadline = time.monotonic() + CASE_TIMEOUT
                continue
            collected.append(line)
        if not saw:
            # Keep what was banked: this case's sentinel may still arrive,
            # and the recovery above needs these lines to render it.
            #
            results[name] = "<NO-OUTPUT>"
            outstanding.append((name, collected))
            continue

        results[name] = render(collected)
        time.sleep(SETTLE)

    s.close()
    for name, _expr in corpus:
        print(f"{name}\t{results.get(name, '<NO-OUTPUT>')}")


if __name__ == "__main__":
    main()
