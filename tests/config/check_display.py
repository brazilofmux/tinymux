#!/usr/bin/env python3
"""Every conftable handler must be a deliberate choice in cf_display().

cf_display() renders config() by comparing tp->interpreter against a list of
known handlers.  The list is by function IDENTITY, and an unrecognised
handler falls through to safe_noperm() -- so config() answers
"#-1 PERMISSION DENIED" for a parameter the caller is fully entitled to read.

That failure is silent and it has already happened.  #1222 converted fourteen
knobs from cf_int to cf_live_driver_int so runtime @admin would take effect,
which fixed the write path and broke the read path: max_players,
idle_timeout, retry_limit, output_limit, nospam_connect and nine others have
answered PERMISSION DENIED ever since.  Nothing failed, because nothing
checks.  #1613 was about to add the two Lua limits the same way.

The trap is that converting a knob to a live-push handler is exactly the
right fix for a different bug, so the person doing it has no reason to look
at a rendering function in another part of the file.

So: every handler used in the conftable must appear in one of the two lists
below.  Adding a handler forces a decision -- render it, or say why it
cannot be rendered.  Neither list is a place to put something to make this
script quiet; NOT_RENDERABLE means the handler genuinely has no scalar at
tp->loc to print.

Exit 1 on a finding, 0 otherwise.
"""
import os
import re
import sys

# Handlers whose destination is not a scalar cf_display could print: alias
# tables, site lists, hooks, permission words, bit sets, include directives.
# config() on these has always answered PERMISSION DENIED and this guard does
# not propose changing that -- only that it stay a decision rather than an
# accident.
#
NOT_RENDERABLE = {
    "cf_access", "cf_acmd_access", "cf_alias", "cf_attr_access",
    "cf_attr_name_alias", "cf_badname", "cf_cf_access", "cf_cmd_alias",
    "cf_flag_access", "cf_flag_name", "cf_flagalias", "cf_func_access",
    "cf_function_alias", "cf_function_name", "cf_helpfile", "cf_hook",
    "cf_include", "cf_modify_bits", "cf_module", "cf_ntab_access",
    "cf_option", "cf_pool_limit", "cf_poweralias", "cf_pronoun_group",
    "cf_raw_helpfile", "cf_rlevel", "cf_set_flags", "cf_site",
}


def main():
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
    path = os.path.join(root, "mux", "modules", "engine", "conf.cpp")
    with open(path, encoding="utf-8", errors="replace") as fh:
        src = fh.read()

    used = set(re.findall(r'\{T\("[^"]+"\),\s*(cf_\w+)', src))
    if not used:
        print("=== config display guard: conftable not found in conf.cpp ===")
        return 1

    try:
        lo = src.index("void cf_display")
        hi = src.index("void cf_list", lo)
    except ValueError:
        print("=== config display guard: cf_display/cf_list not found ===")
        return 1
    rendered = set(re.findall(r"tp->interpreter == (cf_\w+)", src[lo:hi]))

    findings = []
    for h in sorted(used - rendered - NOT_RENDERABLE):
        n = len(re.findall(r'\{T\("[^"]+"\),\s*%s\b' % h, src))
        findings.append(
            "%s handles %d conftable knob(s) but cf_display() does not "
            "render it, so config() on each answers PERMISSION DENIED for a "
            "reader who has permission.  Add it beside cf_int if it stores a "
            "plain int (the live-push handlers do), or to NOT_RENDERABLE in "
            "%s if its destination is not a printable scalar."
            % (h, n, os.path.basename(__file__)))

    # A handler that stops being used, or becomes renderable, should not
    # leave a stale exemption behind claiming it cannot be printed.
    for h in sorted(NOT_RENDERABLE - used):
        findings.append(
            "%s is in NOT_RENDERABLE but no longer appears in the conftable; "
            "remove the entry." % h)
    for h in sorted(NOT_RENDERABLE & rendered):
        findings.append(
            "%s is in NOT_RENDERABLE but cf_display() renders it; remove the "
            "entry." % h)

    if findings:
        print("=== config display guard: %d finding(s) ===" % len(findings))
        for f in findings:
            print("  " + f)
        return 1

    print("=== config display guard: %d handlers, %d rendered, %d "
          "deliberately not ===" % (len(used), len(used & rendered),
                                    len(used & NOT_RENDERABLE)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
