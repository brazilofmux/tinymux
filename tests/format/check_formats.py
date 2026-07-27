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
    "mux_fprintf": 1,        # stringutil.cpp
    "raw_broadcast": 1,      # session.cpp
    "tinyprintf": 0,         # log.cpp   (CLogFile method)
    "log_printf": 0,         # log.cpp
    "cf_log_syntax": 2,      # conf.cpp
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
# compile-time constant.  All three expand to reinterpret_cast<const UTF8 *>
# (mux_nls.h:25/29/33), which is why the S_() rename in #1480 was a no-op at
# runtime but broke this guard until S_ was listed here.
#
# M_() is deliberately NOT in this list.  It expands to mux_gettext(...), so
# the string comes out of the .mo catalog at run time -- the conversions in it
# are chosen by whoever wrote the translation, not by anything in this tree.  A
# translated format string is the classic i18n format-string hazard, so a
# format argument wrapped in M_() is a genuine finding rather than a false
# positive, and must keep failing.
CONST_CAST_WRAPPERS = ("T", "S_", "N_")


def _literal_only(expr):
    """True if expr is built purely from string literals and const macros."""
    stripped = re.sub(r'\b(?:' + '|'.join(CONST_CAST_WRAPPERS) + r')\s*\(',
                      '(', expr)
    for name in CONST_MACROS:
        stripped = stripped.replace(name, '""')
    stripped = LIT.sub('""', stripped)
    return re.fullmatch(r'[\s()"]*', stripped) is not None and '"' in stripped


def is_constant_format(fmt):
    f = fmt.strip()
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

            lit = "".join(LIT.findall(fmt))
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

    print("=== format guard: %d call sites, all constant and supported ==="
          % examined)
    return 0


if __name__ == "__main__":
    sys.exit(main())
