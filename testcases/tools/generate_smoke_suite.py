#!/usr/bin/env python3
"""Generate auto-discovered smoke suite list overrides and cleanup hooks.

Reads top-level `testcases/*.mux` files, excludes harness/setup files that are
not themselves smoke tests, and emits formatted MUX commands that override
`&suite.list.1` / `&suite.list.2` plus per-test `&suite.cleanup.<name>` attrs
on the `smoke` object.
"""

import os
from pathlib import Path
import collections
import re
import sys


EXCLUDED = {"0upload.mux", "smoke.mux"}
CREATE_RE = re.compile(r"@create\s+([A-Za-z0-9_]+)(?:=\S+)?(?=[;\s]|$)")
CCREATE_RE = re.compile(r"@ccreate\s+([A-Za-z0-9_]+)(?=[;\s]|$)")
PCREATE_RE = re.compile(r"@pcreate\s+([A-Za-z0-9_]+)=")
DIG_RE = re.compile(r"@dig\s+([A-Za-z0-9_]+)(?:=\S+)?(?=[;\s]|$)")
OPEN_RE = re.compile(r"@open\s+([A-Za-z0-9_]+)=")
ATTRIB_SET_RE = re.compile(r"attrib_set\((test_[A-Za-z0-9_]+)/([A-Za-z0-9_]+)\s*,")


def discover_names(tc_dir: Path):
    # SMOKE_EXCLUDE drops named tests from the generated suite.
    #
    # Added for the sanitizer run (#1440).  rvbench_fn issues 55 rvbench()
    # calls at 10000 iterations each; instrumented that is ~25ms per
    # iteration, so the file alone would take hours and the harness reports
    # an idle-hang long before it finishes.  Excluding it is honest -- the
    # suite manifest and the verdict count both come from the generator, so
    # an excluded file is excluded from what the run is measured against
    # rather than silently missing from it.
    #
    excluded = set(EXCLUDED)
    for name in os.environ.get("SMOKE_EXCLUDE", "").split():
        excluded.add(name if name.endswith(".mux") else name + ".mux")
    return sorted(
        path.stem for path in tc_dir.glob("*.mux")
        if path.name not in excluded
    )


def wrap_names(names, width=78):
    lines = []
    current = []
    current_len = 0
    for name in names:
        extra = len(name) + (1 if current else 0)
        if current and current_len + extra > width:
            lines.append(" ".join(current) + " \\")
            current = [name]
            current_len = len(name)
        else:
            current.append(name)
            current_len += extra
    if current:
        lines.append(" ".join(current))
    return lines


def emit_suite_attr(attr_name, names):
    print(f"&{attr_name} smoke=")
    for line in wrap_names(names):
        print(f"  {line}")
    print("-")


def unique_reversed(items):
    seen = set()
    result = []
    for item in reversed(items):
        if item not in seen:
            seen.add(item)
            result.append(item)
    return result


def collect_cleanup_manifest(tc_dir: Path, test_name: str):
    path = tc_dir / f"{test_name}.mux"
    main_obj = f"test_{test_name}"
    literal_objects = []
    literal_players = []
    literal_channels = []
    stored_dbref_attrs = []

    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if line.startswith("#"):
            continue

        for name in CREATE_RE.findall(raw_line):
            if name not in {main_obj, "smoke"}:
                literal_objects.append(name)

        literal_channels.extend(CCREATE_RE.findall(raw_line))

        literal_players.extend(PCREATE_RE.findall(raw_line))

        literal_objects.extend(DIG_RE.findall(raw_line))

        literal_objects.extend(OPEN_RE.findall(raw_line))

        for owner, attr in ATTRIB_SET_RE.findall(raw_line):
            if owner == main_obj:
                stored_dbref_attrs.append(attr)

    commands = []
    for attr in unique_reversed(stored_dbref_attrs):
        commands.append(f"@destroy/override [get({main_obj}/{attr})]")
    for name in unique_reversed(literal_objects):
        commands.append(f"@destroy/override {name}")
    # Players last, after any player-owned fixture objects; the * prefix makes
    # player-name matching work from the smoke object's cleanup context.
    for name in unique_reversed(literal_players):
        commands.append(f"@destroy/override *{name}")
    for name in unique_reversed(literal_channels):
        commands.append(f"@cdestroy {name}")
    return commands


def emit_cleanup_attr(test_name: str, commands):
    print(f"&suite.cleanup.{test_name} smoke=")
    if commands:
        for command in commands[:-1]:
            print(f"  {command};")
        print(f"  {commands[-1]}")
    print("-")


# One expected verdict per tr.tc* label that has a SUCCEEDED branch.
#
# Not "Succeeded or Failed": a label with only a Failed branch is a GUARD, not
# a case.  cron_fn's tr.guard logs TC00x solely when the cron chain stalls, so
# in a healthy run it correctly never fires -- but counting it made the run
# report a one-verdict deficit and turned master red (no issue; found while
# verifying #1485).  A label carrying a Succeeded branch is a case and must
# report every run; one carrying only Failed exists to be silent.
#
# Labels whose only output is diagnostic (banner cases, the benchmark files
# that report numbers) are not counted, so they do not need to be special
# cases.  Cases that can report "Skipped" instead of a verdict on some build
# -- the JIT and Lua ones -- are counted here, and Smoke treats Skipped as
# conclusive, so a build without those features does not read as a loss.
#
_VERDICT_LOG = re.compile(r"@log\s+smoke=\s*(TC[0-9]+[a-z]?)[:.]?([^\n;]*)")

# Wording that means "this run did not succeed".  Deliberately loose and
# case-insensitive, because the corpus does not use one spelling:
#
#   nested_depth  "probes failed (pre=...)"        lowercase
#   paginate_fn   "Third mail failed."             lowercase
#   parser_fn     "Skipped (divergence observed)"  a parity probe whose
#                                                  non-success outcome is Skipped
#   shl_fn        "Legacy behavior."               documents the old behaviour
#
# A case-sensitive test for "Failed" reports all four as unable to fail, which
# is wrong -- they each have a real non-success branch.  Reading only the
# capitalised verdict word is the mistake to avoid here.
_NON_SUCCESS = re.compile(r"fail|skipped|legacy|not (?:run|available)", re.I)


def classify_labels(tc_dir: Path):
    """Yield (filename, label, has_success, has_non_success) per tr.tc* label.

    Single parse shared by the expected-verdict count and the vacuous-case
    check, so the two cannot drift apart in how they read the corpus.
    """
    for path in sorted(tc_dir.glob("*.mux")):
        if path.name == "0upload.mux":
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        by_label = collections.defaultdict(list)
        for m in _VERDICT_LOG.finditer(text):
            by_label[m.group(1)].append(m.group(2))
        for label, msgs in sorted(by_label.items()):
            yield (
                path.name,
                label,
                any("Succeeded" in m for m in msgs),
                any(_NON_SUCCESS.search(m) for m in msgs),
            )


def count_expected_verdicts(tc_dir: Path) -> int:
    return sum(
        1 for _, _, has_success, _ in classify_labels(tc_dir) if has_success
    )


def main() -> int:
    tc_dir = Path(__file__).resolve().parent.parent
    discovered = discover_names(tc_dir)
    requested = [Path(arg).stem for arg in sys.argv[1:]]
    if requested:
        discovered_set = set(discovered)
        missing = [name for name in requested if name not in discovered_set]
        if missing:
            print("ERROR: unknown smoke tests:", ", ".join(missing), file=sys.stderr)
            return 1
        names = sorted(dict.fromkeys(requested))
    else:
        names = discovered
    if not names:
        print("ERROR: no discoverable smoke tests found", file=sys.stderr)
        return 1

    midpoint = (len(names) + 1) // 2
    first = names[:midpoint]
    second = names[midpoint:]

    print("#")
    print("# Auto-generated smoke suite overrides.")
    print("# Generated by testcases/tools/generate_smoke_suite.py.")
    print("#")
    # `&attr smoke=...` resolves a bare name through the executor's inventory
    # and the contents of the executor's room -- there is no
    # location-independent lookup.  `smoke` is pinned to #0, so stand there
    # before writing.  Without this, any earlier file that moves #1 and does
    # not move it back makes these two writes silently do nothing, and the
    # suite runs whatever was stored before them (#1391).
    print("@tel me=#0")
    print("-")
    emit_suite_attr("suite.list.1", first)
    emit_suite_attr("suite.list.2", second)
    # Also record the names in a plain file, outside the upload stream.
    #
    # The runtime SUITE-EXPECTED is computed from the attributes the upload
    # stores, so it agrees with whatever the database ended up holding --
    # including when the upload lost names, which is how #1387 hid 46 test
    # files behind a clean "Dispatched: 261 / 261".  A count that came
    # through the same path it is meant to validate cannot detect a loss on
    # that path.  This one never enters the database.
    manifest = os.environ.get("SMOKE_SUITE_MANIFEST")
    if manifest:
        with open(manifest, "w", encoding="utf-8") as fh:
            # How many verdict lines the run should produce, counted from the
            # sources rather than from the run (#1396).  Smoke's totals are
            # grep counts over the log, so a case that dispatches and then
            # logs neither verdict is counted in neither total and the run
            # still reports ALL TESTS PASSED.  Reintroducing #1386's defect
            # lost 149 assertions exactly that way.
            #
            # One per tr.tc* label that has at least one verdict-logging
            # branch.  Extra diagnostic @log lines in the same case do not
            # count -- only whether the label can report a verdict at all.
            fh.write("#expected-verdicts %d\n" % count_expected_verdicts(tc_dir))
            for name in first + second:
                fh.write(name + "\n")
    for name in names:
        emit_cleanup_attr(name, collect_cleanup_manifest(tc_dir, name))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
