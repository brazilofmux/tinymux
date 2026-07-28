#!/usr/bin/env python3
"""Static guard on format strings reaching mux_vsnprintf.

mux_vsnprintf implements printf by hand and does not implement every
conversion.  Historically an unimplemented one reached mux_assert(0) and
aborted the process -- #1382 produced that outage twice, in @list cache and
in astbench(), and both were fixed by rewriting the call site.

#1416 implemented the conversions people reach for and #1429 made the
remainder non-fatal.  That second change is exactly why this guard exists:
recovery echoes the offending spec and STOPS, so the failure mode is now a
silently truncated message rather than a crash.  Quieter is better in
production and worse for noticing, so the noise has to move to build time.

Two things are checked, over every wrapper that routes to mux_vsnprintf:

  1. No literal format uses a conversion mux_vsnprintf does not implement.
  2. No format argument is a runtime value.  A non-constant format cannot be
     checked by (1), and if it ever became attacker- or config-influenced it
     would be a format-string bug.  Constants assembled from literals,
     ternaries between literals, and named macros are fine.

A third check bans the C library's printf family outright, because (1) and (2)
are guards you opt into by calling a wrapper -- and code that calls snprintf()
directly is not merely unguarded, it is wrong.  mux_vsnprintf advances a whole
codepoint at a time and stops before emitting a partial one; snprintf counts
bytes, and C11 7.21.6.5 (buffer limit) and 7.21.6.1p8 (%s precision counts
BYTES) both cut mid-sequence.  Measured on "日本語\U0001f3b2" into an 8-byte buffer,
snprintf emits E6 97 A5 E6 9C AC E8 -- a lone lead byte, invalid UTF-8, sent
to a player.  Every one of these call sites interpolates player-settable text
into a fixed buffer, so that is the ordinary case and not a corner.

The ban is a ratchet, not a flag day: LEGACY freezes the count per file, so
existing code stays green while any NEW call site fails the build.  The
numbers may only go DOWN.  Lowering one is a change to this file, which is the
point -- the backlog is visible and countable instead of drifting upward.
Note that the two big offenders are loadable modules, which is not a
coincidence: stringutil.h cannot be included from a module (its inlines want
Ragel tables only the engine links), so the correct primitive was unreachable
at compile time until mux_format.h.

Exit 1 on a finding, 0 otherwise.  No output on success beyond a count.
"""
import re
import os
import sys

# Wrapper -> zero-based position of the format argument.  All of these end in
# mux_vsnprintf; verified by reading each definition rather than assumed.
WRAPPERS = {
    "tprintf": 0,            # stringutil.cpp
    "safe_tprintf_str": 2,   # stringutil.cpp
    "mux_sprintf": 2,        # stringutil.cpp
    "mux_snprintf": 2,       # stringutil.cpp (length-returning)
    "mux_fprintf": 1,        # stringutil.cpp
    "raw_broadcast": 1,      # session.cpp
    "tinyprintf": 0,         # log.cpp   (CLogFile method)
    "log_printf": 0,         # log.cpp
    "cf_log_syntax": 2,      # conf.cpp
    # Local length-returning wrapper over mux_vsnprintf in mail_mod.cpp.
    "mail_sprintf": 2,
}

# What mux_vsnprintf implements, as of #1416.
#
# Flags matter as much as conversions: the parser handles '-' (bLeft) and
# '0' (bZeroPadded) and nothing else, so '%+d' and '%#x' fall through to the
# same recovery as an unknown conversion.  An earlier version of this guard
# treated '+' as a flag and then saw a supported '%d', which waved through
# all seven forms #1429 is about -- the check has to reject the flag, not
# just the conversion character.
#
# '*' width and precision are not implemented either: the parser reads
# digits only.
OK_CONV = set("dsuxXpc%ioeEfFgG")
OK_LEN = {"", "l", "ll", "z"}
OK_FLAGS = set("-0")

# The C library's printf family.  Not a wrapper list -- a ban list.  The
# lookbehind is what lets mux_sprintf/mux_vsnprintf through.
#
BANNED = re.compile(
    r"(?<![A-Za-z0-9_])(vsnprintf|snprintf|vsprintf|sprintf)\s*\(")

# Ragel and similar emit these; the fix belongs in the .rl, not the output,
# and the output is chmod a-w on disk.  See docs/generated-files.md.
#
BAN_GENERATED = ("mux/lib/color_ops.c",)

# Frozen counts.  MAY SHRINK, MUST NOT GROW.  A file absent here must have
# zero.  Counted line-locally (text after // ignored) so the number is stable
# and cannot desync the way a whole-file comment/string stripper does.
#
BAN_LEGACY = {
    "mux/lib/mux_nls.cpp": 1,
    # comsys_mod: 0 after #1640/#1656 — columns via co_copy_field, plain
    # assembly via mux_sprintf (mux_format.h).
    #
    "mux/modules/engine/ast.cpp": 3,
    "mux/modules/engine/attrcache.cpp": 4,
    # dbt_test and the div harness are standalone binaries: tests/dbt builds
    # them with "$(COMPILE) -o $@ $(SRCS)" and links no libmux, so mux_snprintf
    # is not reachable and these cannot reach zero without changing that build.
    # Frozen rather than exempted -- an exemption would let new sites in, and
    # the count still may not grow.  Neither writes player-facing text.
    #
    "mux/modules/engine/dbt_test.cpp": 22,
    "mux/modules/engine/dbt_x64_div_harness.c": 1,
    "mux/modules/engine/functions.cpp": 1,
    "mux/modules/engine/mail.cpp": 2,
    "mux/modules/engine/match.cpp": 2,
    "mux/modules/engine/predicates.cpp": 2,
    "mux/modules/exp3/exp3.cpp": 1,
    # mail_mod: 0 after #1653 — player-facing assembly via mux_sprintf /
    # mail_sprintf (mux_format.h); columns already on co_copy_field.
    "mux/src/ganl_adapter.cpp": 1,
    "mux/src/websocket.cpp": 1,
}

SPEC = re.compile(r"%([-+ #0]*)([0-9*]*)(\.[0-9*]*)?(hh|h|ll|l|z|j|t|L|q)?(.)")
LIT = re.compile(r'"((?:[^"\\]|\\.)*)"')
CALL = re.compile(r"\b(" + "|".join(WRAPPERS) + r")\s*\(")

# A format argument is constant if it is a literal, a T(...) wrapper, a
# ternary between such, or a named macro that expands to one.  These are the
# macro names in use; adding one here is a deliberate act.
CONST_MACROS = ("ENDLINE", "MAIL_LINE", "FOLDER_LINE")

# Formats held in a variable or table, each verified by reading every
# assignment.  Adding an entry means asserting that no runtime value can
# reach it; do not add one without checking.
#
#   pFmt                  command.cpp -- switch over string literals
#   pRoomAnnounceFmt      engine_com.cpp -- two literals
#   pMonitorAnnounceFmt   engine_com.cpp -- two literals
#   tf1_case_table[...]   functions.cpp -- static const table of literals,
#                         and both indices are bounds-checked by
#                         time_format_1 (maxWidth gated to 8..11, iCase < 3)
#
ALLOWED_NONLITERAL = {
    "pFmt",
    "pRoomAnnounceFmt",
    # BuildChannelAnnounce (#1723): an M_()-resolved whole-sentence
    # announcement format.  Same class as the two above -- the catalogue is
    # the source, and check_nls.py verifies every msgstr keeps the msgid's
    # conversion types, which is the property this guard exists to protect.
    "pChannelAnnounceFmt",
    "pMonitorAnnounceFmt",
    "tf1_case_table[iCase].specs[iWidth]",
}


def split_args(inner):
    """Split on top-level commas, ignoring anything inside a string literal.

    Brackets inside literals are not brackets: T("%s] %s") would otherwise
    close a depth level that was never opened and swallow the rest of the
    argument list.
    """
    parts, depth, cur = [], 0, ""
    in_str, esc = False, False
    for ch in inner:
        if in_str:
            cur += ch
            if esc:
                esc = False
            elif "\\" == ch:
                esc = True
            elif '"' == ch:
                in_str = False
            continue
        if '"' == ch:
            in_str = True
            cur += ch
            continue
        if ch in "([":
            depth += 1
        if ch in ")]":
            depth -= 1
        if ch == "," and 0 == depth:
            parts.append(cur)
            cur = ""
        else:
            cur += ch
    parts.append(cur)
    return parts


# Wrappers that are a pure cast, so a literal inside one is still a
# compile-time constant.  T / S_ / N_ expand to reinterpret_cast<const UTF8 *>
# (mux_nls.h:25/29/33).  M_() expands to mux_gettext(...) under HAVE_NLS, so
# the *runtime* format comes from the .mo -- but the *source* literal is still
# the msgid, and that is what rule 1 checks here.
#
# Translated formats are the classic i18n format-string hazard.  This guard
# alone cannot see the .mo; tests/nls/check_nls.py is the load-bearing half
# and requires every catalogue msgstr to carry the same conversion sequence
# as its msgid (#1419 Phase 3, format-string slices).  Allowing M_ here is
# what makes those slices possible without lying to rule 1.
CONST_CAST_WRAPPERS = ("T", "S_", "N_", "M_")


def _literal_only(expr):
    """True if expr is built purely from string literals and const macros."""
    stripped = re.sub(r'\b(?:' + '|'.join(CONST_CAST_WRAPPERS) + r')\s*\(',
                      '(', expr)
    for name in CONST_MACROS:
        stripped = stripped.replace(name, '""')
    stripped = LIT.sub('""', stripped)
    return re.fullmatch(r'[\s()"]*', stripped) is not None and '"' in stripped


# MN_(singular, plural, count) is not a cast wrapper and cannot join the list
# above: its third argument is a runtime count, so the expression as written
# is not literal-only (#1622).  The first two arguments ARE the format
# literals, and both must be checked -- each is a format string in its own
# right, chosen at runtime by the catalogue's plural rule.
#
# Rewriting to (singular, plural) leaves both literals in place, so the
# conversion scan below sees the conversions of both forms rather than only
# the one that happens to be listed first.
#
MN_CALL = re.compile(r'\bMN_\s*\((.*)\)\s*$', re.S)


def _strip_mn(f):
    m = MN_CALL.match(f)
    if not m:
        return f
    args = split_args(m.group(1))
    if len(args) < 2:
        return f
    # Joined with a space, not a comma: adjacent string literals are C++
    # concatenation, which _literal_only already accepts.  A comma is not in
    # its allowed character set and would fail the very check this enables.
    return "(" + " ".join(a.strip() for a in args[:2]) + ")"


def is_constant_format(fmt):
    f = _strip_mn(fmt.strip())
    if not f:
        return True                      # parse artifact, not a call
    if re.match(r'^(const\s+)?(UTF8|char)\s*\*', f):
        return True                      # the wrapper's own definition
    if f in ALLOWED_NONLITERAL:
        return True
    if _literal_only(f):
        return True

    # A ternary is constant when both BRANCHES are literal, whatever the
    # condition is -- `(x=='#') ? T("%d") : T("%02d")` selects between two
    # checked formats and is as safe as either.
    depth, q = 0, -1
    in_str, esc = False, False
    for i, ch in enumerate(f):
        if in_str:
            if esc:
                esc = False
            elif "\\" == ch:
                esc = True
            elif '"' == ch:
                in_str = False
            continue
        if '"' == ch:
            in_str = True
        elif ch in "([":
            depth += 1
        elif ch in ")]":
            depth -= 1
        elif "?" == ch and 0 == depth:
            q = i
            break
    if q < 0:
        return False
    rest = f[q + 1:]
    depth = 0
    in_str, esc = False, False
    for i, ch in enumerate(rest):
        if in_str:
            if esc:
                esc = False
            elif "\\" == ch:
                esc = True
            elif '"' == ch:
                in_str = False
            continue
        if '"' == ch:
            in_str = True
        elif ch in "([":
            depth += 1
        elif ch in ")]":
            depth -= 1
        elif ":" == ch and 0 == depth:
            return _literal_only(rest[:i]) and _literal_only(rest[i + 1:])
    return False


def main():
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
    roots = [os.path.join(root, d) for d in ("mux/src", "mux/lib", "mux/modules")]

    files = []
    for r in roots:
        for dirpath, _, filenames in os.walk(r):
            if "/rv64/" in dirpath.replace("\\", "/"):
                continue
            for fn in filenames:
                if fn.endswith((".cpp", ".c")):
                    files.append(os.path.join(dirpath, fn))

    findings = []
    examined = 0

    # Ban check, before the wrapper checks: a raw snprintf() is not a bad
    # format string, it is the wrong function.  See BANNED above.
    #
    banned_total = 0
    for path in sorted(files):
        rel = os.path.relpath(path, root).replace("\\", "/")
        if rel in BAN_GENERATED:
            continue
        n = 0
        with open(path, encoding="utf-8", errors="replace") as fh:
            for line in fh:
                n += len(BANNED.findall(line.split("//", 1)[0]))
        banned_total += n
        allowed = BAN_LEGACY.get(rel, 0)
        if n > allowed:
            findings.append(
                "%s: %d raw snprintf/sprintf/vsnprintf call site(s), was %d. "
                "These truncate on byte boundaries and can emit half a UTF-8 "
                "sequence to a player; use mux_sprintf (mux_format.h), which "
                "is codepoint-atomic and is checked by this guard"
                % (rel, n, allowed))
        elif n < allowed:
            findings.append(
                "%s: down to %d raw call site(s) from %d -- good; lower "
                "BAN_LEGACY in %s to %d to keep the ratchet tight"
                % (rel, n, allowed, os.path.basename(__file__), n))
    for rel in sorted(BAN_LEGACY):
        if not os.path.exists(os.path.join(root, rel)):
            findings.append(
                "%s: listed in BAN_LEGACY but does not exist; remove the entry"
                % rel)

    for path in sorted(files):
        with open(path, encoding="utf-8", errors="replace") as fh:
            src = fh.read()
        rel = os.path.relpath(path, root)
        for m in CALL.finditer(src):
            name = m.group(1)
            i = m.end() - 1
            depth, j = 0, i
            while j < len(src):
                if src[j] == "(":
                    depth += 1
                elif src[j] == ")":
                    depth -= 1
                    if 0 == depth:
                        break
                j += 1
            call = src[m.start():j + 1]
            parts = split_args(call[call.index("(") + 1:-1])
            idx = WRAPPERS[name]
            if len(parts) <= idx:
                continue
            examined += 1
            fmt = parts[idx]
            line = src.count("\n", 0, m.start()) + 1

            if not is_constant_format(fmt):
                findings.append(
                    "%s:%d: %s() takes a non-constant format: %s"
                    % (rel, line, name, fmt.strip()[:60]))
                continue

            lit = "".join(LIT.findall(_strip_mn(fmt.strip())))
            for sm in SPEC.finditer(lit):
                flags = sm.group(1) or ""
                width = sm.group(2) or ""
                prec = sm.group(3) or ""
                length = sm.group(4) or ""
                conv = sm.group(5)
                if "" == conv:
                    continue
                supported = (conv in OK_CONV
                             and length in OK_LEN
                             and set(flags) <= OK_FLAGS
                             and "*" not in width
                             and "*" not in prec)
                if supported:
                    continue
                findings.append(
                    "%s:%d: %s() uses %r, which mux_vsnprintf does not "
                    "implement; it will be echoed literally and the rest of "
                    "the message dropped (#1429)"
                    % (rel, line, name, sm.group(0)))

    if findings:
        print("=== format guard: %d finding(s) ===" % len(findings))
        for f in findings:
            print("  " + f)
        return 1

    print("=== format guard: %d call sites, all constant and supported; "
          "%d legacy raw printf-family sites remain (ratchet: may only fall) "
          "==="
          % (examined, banned_total))
    return 0


if __name__ == "__main__":
    sys.exit(main())
