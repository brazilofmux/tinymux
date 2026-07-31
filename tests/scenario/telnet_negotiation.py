#!/usr/bin/env python3
# Live scenario test for telnet NVT option negotiation and subnegotiation
# parsing (mux/src/telnet.cpp).
#
# Why this cannot be a smoke test: muxscript speaks to the engine, not to a
# socket, so nothing in testcases/ can drive IAC sequences.  Before this
# driver, `grep -rn CHARSET testcases/ mux/ganl/tests/` returned nothing —
# telnet negotiation had no automated coverage at all, which is how the
# Pass 6 defects below reached master.
#
# The properties pinned here are ones whose correctness turned out to be
# accidental rather than designed:
#
#   #1128  enable_us()/disable_us() consulted him_state while mutating
#          us_state.  The opening negotiation only worked because
#          InitializeTelnetOptions happens to call enable_us(EOR) *before*
#          enable_him(EOR) -- swapping two adjacent lines would have
#          silently deleted the initial WILL.  enable_us(SGA) was dead code
#          that could never fire, and the server never answered a client's
#          WILL BINARY with its own.
#
#   #1132  The CHARSET REQUEST tokenizer subtracted 1 unconditionally, so
#          the *last* token lost a byte unless the list had a trailing
#          separator.  RFC 2066 lists normally do not, so a client offering
#          ";UTF-8" parsed as "UTF-" and was answered REJECT -- UTF-8
#          negotiation failed outright for the common single-charset case.
#
#   #1131  An oversized subnegotiation must be discarded, must not be
#          parsed as a truncated prefix, and must not reset the parser to
#          Normal -- in Normal the remaining SB payload is accepted as
#          typed input and an embedded LF submits it as a command.
#
# Driven by tests/scenario/run.sh.  Usage: telnet_negotiation.py [host] [port]

import socket
import sys
import time

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 6250

LOGIN = "connect Wizard potrzebie"

IAC, SE, SB = 0xFF, 0xF0, 0xFA
WILL, WONT, DO, DONT = 0xFB, 0xFC, 0xFD, 0xFE

OPT_BINARY, OPT_SGA, OPT_TTYPE = 0x00, 0x03, 0x18
OPT_EOR, OPT_NAWS, OPT_CHARSET, OPT_MSSP, OPT_GMCP = 0x19, 0x1F, 0x2A, 0x46, 0xC9

SB_REQUEST, SB_ACCEPT, SB_REJECT = 1, 2, 3

OPT_NAMES = {OPT_BINARY: "BINARY", OPT_SGA: "SGA", OPT_TTYPE: "TTYPE",
             OPT_EOR: "EOR", OPT_NAWS: "NAWS", OPT_CHARSET: "CHARSET",
             OPT_MSSP: "MSSP", OPT_GMCP: "GMCP"}
CMD_NAMES = {WILL: "WILL", WONT: "WONT", DO: "DO", DONT: "DONT"}


def sendline(sock, line):
    sock.sendall(line.encode("utf-8") + b"\r\n")


def read_for(sock, pred, timeout=4.0):
    # Accumulate raw bytes until `pred(buf)` is satisfied or timeout.  The
    # server answers negotiation from the main loop, so a reply can lag the
    # trigger by an iteration; poll rather than sleep a fixed amount.
    sock.settimeout(0.3)
    deadline = time.monotonic() + timeout
    buf = b""
    while time.monotonic() < deadline:
        try:
            data = sock.recv(8192)
            if not data:
                break
            buf += data
            if pred is not None and pred(buf):
                return buf
        except socket.timeout:
            pass
    return buf


def negotiations(buf):
    """Extract [(CMD, OPT), ...] from a raw byte stream."""
    out = []
    i = 0
    while i < len(buf):
        if buf[i] == IAC and i + 2 < len(buf) and buf[i + 1] in CMD_NAMES:
            out.append((CMD_NAMES[buf[i + 1]],
                        OPT_NAMES.get(buf[i + 2], str(buf[i + 2]))))
            i += 3
        elif buf[i] == IAC and i + 1 < len(buf) and buf[i + 1] == SB:
            # Skip the subnegotiation body.
            j = i + 2
            while j + 1 < len(buf) and not (buf[j] == IAC and buf[j + 1] == SE):
                j += 1
            i = j + 2
        else:
            i += 1
    return out


def subnegotiations(buf, option):
    """Extract payloads of IAC SB <option> ... IAC SE as bytes."""
    out = []
    i = 0
    while i + 2 < len(buf):
        if buf[i] == IAC and buf[i + 1] == SB and buf[i + 2] == option:
            j = i + 3
            body = b""
            while j + 1 < len(buf):
                if buf[j] == IAC and buf[j + 1] == SE:
                    break
                body += bytes([buf[j]])
                j += 1
            out.append(body)
            i = j + 2
        else:
            i += 1
    return out


def connect(login=False):
    s = socket.socket()
    s.settimeout(5)
    s.connect((HOST, PORT))
    opening = read_for(s, lambda b: b"\xff" in b, 2.0)
    if login:
        sendline(s, LOGIN)
        read_for(s, None, 2.0)
    return s, opening


def charset_request(sock, offer):
    """Send IAC SB CHARSET REQUEST <offer> IAC SE; return (verb, name)."""
    sock.sendall(bytes([IAC, SB, OPT_CHARSET, SB_REQUEST])
                 + offer.encode("ascii") + bytes([IAC, SE]))
    buf = read_for(sock, lambda b: subnegotiations(b, OPT_CHARSET) != [], 4.0)
    for body in subnegotiations(buf, OPT_CHARSET):
        if body and body[0] in (SB_ACCEPT, SB_REJECT):
            verb = "ACCEPT" if body[0] == SB_ACCEPT else "REJECT"
            return verb, body[1:].decode("ascii", "replace")
    return None, ""


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
        s, opening = connect()
    except OSError as e:
        print("not ok - could not connect to %s:%d (%s)" % (HOST, PORT, e))
        return 1

    # ---- Opening negotiation -------------------------------------------
    neg = negotiations(opening)
    check(("WILL", "EOR") in neg, "server offers WILL EOR", " (got %r)" % neg)
    check(("WILL", "CHARSET") in neg, "server offers WILL CHARSET",
          " (got %r)" % neg)
    check(("DO", "NAWS") in neg, "server requests DO NAWS", " (got %r)" % neg)

    # ---- #1128: enable_us(SGA) must actually fire ----------------------
    # Reachable only via set_us_state(EOR, YES), i.e. after the client
    # accepts the server's WILL EOR.  Pre-fix enable_us switched on
    # him_state, so this was dead code and no WILL SGA was ever sent.
    s.sendall(bytes([IAC, DO, OPT_EOR]))
    neg = negotiations(read_for(
        s, lambda b: b.find(bytes([IAC, WILL, OPT_SGA])) >= 0, 3.0))
    check(("WILL", "SGA") in neg,
          "#1128 server sends WILL SGA after client DO EOR",
          " (got %r)" % neg)

    # ---- #1128: client WILL BINARY must draw a server WILL BINARY ------
    # set_him_state writes him_state BEFORE calling enable_us(BINARY), so
    # pre-fix him_state was already YES and enable_us matched no case.
    s.sendall(bytes([IAC, WILL, OPT_BINARY]))
    neg = negotiations(read_for(
        s, lambda b: b.find(bytes([IAC, WILL, OPT_BINARY])) >= 0, 3.0))
    check(("WILL", "BINARY") in neg, "#1128 server answers WILL BINARY",
          " (got %r)" % neg)
    s.close()

    # ---- #1132: single charset, no trailing separator ------------------
    # The common RFC 2066 shape.  Pre-fix this parsed as "UTF-" and drew a
    # REJECT, so client-initiated UTF-8 failed outright.
    s, _ = connect()
    verb, name = charset_request(s, ";UTF-8")
    check(verb == "ACCEPT" and name == "UTF-8",
          "#1132 CHARSET ';UTF-8' (no trailing sep) accepted as UTF-8",
          " (got %r %r)" % (verb, name))
    s.close()

    # ---- #1132: multi-charset, last token must be intact ---------------
    # The server honours the client's ordering (first match wins), so the
    # only known name is placed last: matching it proves the final token
    # was not truncated.  Pre-fix this parsed as "UTF-" and drew a REJECT.
    s, _ = connect()
    verb, name = charset_request(s, ";NO-SUCH-CHARSET;UTF-8")
    check(verb == "ACCEPT" and name == "UTF-8",
          "#1132 CHARSET last token in a list is intact",
          " (got %r %r)" % (verb, name))
    s.close()

    # ---- #1132: trailing separator must still work ---------------------
    # Both loop exit conditions are true at once here; the -1 must be kept.
    s, _ = connect()
    verb, name = charset_request(s, ";UTF-8;")
    check(verb == "ACCEPT" and name == "UTF-8",
          "#1132 CHARSET with trailing separator still accepted",
          " (got %r %r)" % (verb, name))
    s.close()

    # ---- #1132: an unknown charset must be rejected, not truncated -----
    # Guards the opposite direction: a token must not lose bytes and match
    # a shorter known name by accident.
    s, _ = connect()
    verb, _name = charset_request(s, ";NO-SUCH-CHARSET")
    check(verb == "REJECT", "#1132 unknown charset rejected",
          " (got %r)" % verb)
    s.close()

    # ---- #1131: oversized SB must not inject commands ------------------
    # Overflow must not reset the parser to Normal: there, leftover SB
    # payload is accepted as typed input and an embedded LF submits it.
    s, _ = connect(login=True)
    s.sendall(bytes([IAC, SB, OPT_NAWS]) + b"A" * 5000
              + b"\r\n@pemit me=SBINJECT\r\n")
    out = read_for(s, lambda b: b"SBINJECT" in b, 3.0)
    check(b"SBINJECT" not in out,
          "#1131 oversized SB payload is not executed as a command")
    s.close()

    # ---- #1131: parser recovers once the SB is terminated --------------
    # Staying in the SB state must not strand the connection.
    s, _ = connect(login=True)
    s.sendall(bytes([IAC, SB, OPT_NAWS]) + b"A" * 5000 + bytes([IAC, SE]))
    sendline(s, "@pemit me=SBRECOVER")
    out = read_for(s, lambda b: b"SBRECOVER" in b, 4.0)
    check(b"SBRECOVER" in out,
          "#1131 NVT recovers after the oversized SB is terminated")
    s.close()

    # ---- #1131: an overflowed SB must not be parsed as truncated -------
    # Apply a valid 80x25 NAWS, then an overflowing one that must not take.
    s, _ = connect(login=True)
    s.sendall(bytes([IAC, SB, OPT_NAWS, 0, 80, 0, 25, IAC, SE]))
    sendline(s, "@pemit me=DIM1<[width(me)]x[height(me)]>")
    out = read_for(s, lambda b: b"DIM1<" in b, 4.0)
    check(b"DIM1<80x25>" in out, "NAWS subnegotiation applied",
          " (got %r)" % out[-80:])

    s.sendall(bytes([IAC, SB, OPT_NAWS, 0, 99, 0, 44]) + b"B" * 5000
              + bytes([IAC, SE]))
    sendline(s, "@pemit me=DIM2<[width(me)]x[height(me)]>")
    out = read_for(s, lambda b: b"DIM2<" in b, 4.0)
    check(b"DIM2<80x25>" in out,
          "#1131 overflowed NAWS discarded, not applied as a truncated prefix",
          " (got %r)" % out[-80:])
    s.close()

    # ---- #1811: duplicate DO MSSP must not replay the status frame ------
    # After the first DO, us_state(MSSP) is YES; a second DO must be
    # ignored (RFC 1143) and must not call set_us_state side effects again.
    #
    s, opening = connect()
    s.sendall(bytes([IAC, DO, OPT_MSSP]))
    first = read_for(s, lambda b: subnegotiations(b, OPT_MSSP) != [], 4.0)
    n_first = len(subnegotiations(first, OPT_MSSP))
    check(n_first >= 1, "#1811 first DO MSSP yields an MSSP subnegotiation",
          " (got %d)" % n_first)
    s.sendall(bytes([IAC, DO, OPT_MSSP]))
    second = read_for(s, None, 1.5)
    n_second = len(subnegotiations(second, OPT_MSSP))
    check(n_second == 0,
          "#1811 duplicate DO MSSP does not replay MSSP status",
          " (got %d extra SB(s))" % n_second)
    s.close()

    # ---- #1811: duplicate WILL TTYPE must not re-request TTYPE ----------
    s, _ = connect()
    s.sendall(bytes([IAC, WILL, OPT_TTYPE]))
    first = read_for(s, lambda b: subnegotiations(b, OPT_TTYPE) != [], 4.0)
    n_first = len(subnegotiations(first, OPT_TTYPE))
    check(n_first >= 1, "#1811 first WILL TTYPE yields TTYPE SEND",
          " (got %d)" % n_first)
    s.sendall(bytes([IAC, WILL, OPT_TTYPE]))
    second = read_for(s, None, 1.5)
    n_second = len(subnegotiations(second, OPT_TTYPE))
    check(n_second == 0,
          "#1811 duplicate WILL TTYPE does not re-SEND",
          " (got %d extra SB(s))" % n_second)
    s.close()

    print("=== telnet-negotiation scenario: %d passed, %d failed ==="
          % (npass, nfail))
    return 1 if nfail else 0


if __name__ == "__main__":
    sys.exit(main())
