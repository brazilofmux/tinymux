#!/usr/bin/env python3
"""Softcode Linter — static analysis of MUX softcode in attribute values.

Validates:
- Balanced brackets [ ]
- Balanced parentheses ( )
- Balanced braces { }
- Known function names
- Valid substitution patterns (%#, %0-%9, %q0-%qz, %va-%vz)
- Dangerous function/command usage
- Common mistakes

Does NOT execute code — purely static analysis.
"""

import re
from pathlib import Path

# ---------------------------------------------------------------------------
# MUX Function Dictionary
# ---------------------------------------------------------------------------

# Core functions that exist in TinyMUX 2.x
# This is a representative set — not exhaustive but covers the common ones.
KNOWN_FUNCTIONS = {
    # Math
    'abs', 'add', 'bound', 'ceil', 'div', 'fdiv', 'floor', 'inc', 'dec',
    'max', 'mean', 'median', 'min', 'mod', 'mul', 'power', 'round',
    'sign', 'sqrt', 'stddev', 'sub', 'trunc',

    # String
    'after', 'alphamax', 'alphamin', 'ansi', 'art', 'before', 'capstr',
    'cat', 'center', 'comp', 'decrypt', 'delete', 'edit', 'encrypt',
    'escape', 'finditer', 'first', 'foreach', 'grab', 'graball',
    'insert', 'iter', 'itext', 'last', 'lcstr', 'left', 'linsert',
    'ljust', 'lnum', 'match', 'member', 'merge', 'mid', 'munge',
    'null', 'num', 'objeval', 'pos', 'regmatch', 'regmatchi',
    'regrab', 'regraball', 'repeat', 'replace', 'rest', 'reverse',
    'right', 'rjust', 'scramble', 'secure', 'setq', 'setr',
    'sha1', 'sort', 'sortby', 'space', 'splice', 'squish', 'step',
    'strcat', 'strdelete', 'strinsert', 'strip', 'stripaccents',
    'stripansi', 'strlen', 'strmatch', 'strmem', 'strreplace',
    'strtrunc', 'subeval', 'switch', 'switchall', 'table', 'tr',
    'trim', 'ucstr', 'unique', 'wordpos', 'words', 'wrap',

    # List
    'elements', 'extract', 'filter', 'index', 'itemize', 'ladd',
    'lattr', 'lcon', 'ldelete', 'lexits', 'lmax', 'lmin', 'lnum',
    'lreplace', 'map', 'mix', 'munge', 'revwords', 'setdiff',
    'setinter', 'setunion', 'shuffle', 'sort', 'sortby',

    # DB query
    'children', 'con', 'controls', 'create', 'entrances', 'elock',
    'exit', 'flags', 'fullname', 'get', 'get_eval', 'grep', 'grepi',
    'hasattr', 'hasattrp', 'hasflag', 'haspower', 'hastype', 'home',
    'ifelse', 'isdbref', 'isint', 'isnum', 'isword', 'lattr',
    'lattrp', 'lcon', 'lexits', 'link', 'loc', 'locate', 'lock',
    'lplayers', 'lthings', 'money', 'mudname', 'name', 'ncon',
    'nearby', 'nexits', 'next', 'nplayers', 'nthings', 'num',
    'obj', 'objeval', 'owner', 'parent', 'pemit', 'playermem',
    'pmatch', 'ports', 'r', 'room', 'type', 'visible', 'where',
    'xget', 'zone',

    # Logic/Control
    'and', 'case', 'cand', 'cor', 'eq', 'gt', 'gte', 'if', 'ifelse',
    'lt', 'lte', 'ne', 'neq', 'not', 'or', 'switch', 'switchall',
    't', 'u', 'udefault', 'ufun', 'ulocal', 'v', 'xor',

    # Time
    'convsecs', 'convtime', 'secs', 'starttime', 'time', 'timefmt',

    # Register
    'listq', 'r', 'setq', 'setr', 'unsetq',

    # Formatting
    'ansi', 'ljust', 'rjust', 'center', 'columns', 'table', 'wrap',

    # Misc
    'aposs', 'channels', 'checkpass', 'cwho', 'default', 'die',
    'doing', 'dumping', 'edefault', 'elements', 'entrances',
    'eval', 'fold', 'idle', 'isword', 'lattr', 'lflags', 'lit',
    'lnum', 'localize', 'lwho', 'mail', 'motd', 'mtime', 'mudname',
    'ncomp', 'objeval', 'pack', 'pick', 'poss', 'pueblo', 'rand',
    's', 'search', 'set', 'shl', 'shr', 'sign', 'subj', 'tel',
    'textfile', 'toss', 'type', 'unpack', 'version', 'visible',
    'width', 'wordpos', 'words', 'xget',
}

# Dangerous functions/commands that probably shouldn't be in builder specs
DANGEROUS_PATTERNS = [
    (r'@force\b', '@force — can execute arbitrary commands as another object'),
    (r'@destroy\b', '@destroy — destroys objects'),
    (r'@toad\b', '@toad — destroys players'),
    (r'@newpassword\b', '@newpassword — changes player passwords'),
    (r'@boot\b', '@boot — disconnects players'),
    (r'@shutdown\b', '@shutdown — shuts down the server'),
    (r'@restart\b', '@restart — restarts the server'),
    (r'@dump\b', '@dump — forces database dump'),
    (r'@clone\b', '@clone — may bypass quota'),
    (r'\bcreate\s*\(', 'create() — creates objects dynamically'),
]

# Valid substitution patterns
VALID_SUBS = re.compile(
    r'%(?:'
    r'#|'               # %# — enactor dbref
    r'!|'               # %! — executor dbref
    r'@|'               # %@ — caller dbref
    r'L|'               # %L — location
    r'N|n|'             # %N/%n — name
    r'S|s|'             # %S/%s — subjective pronoun
    r'O|o|'             # %O/%o — objective pronoun
    r'P|p|'             # %P/%p — possessive pronoun
    r'A|a|'             # %A/%a — absolute possessive
    r'[0-9]|'           # %0-%9 — command args
    r'q[0-9a-zA-Z]|'    # %q0-%qz — q-registers
    r'v[a-zA-Z]|'       # %va-%vz — attribute registers
    r'r|'               # %r — return (newline)
    r't|'               # %t — tab
    r'b|'               # %b — space
    r'c[a-zA-Z]|'       # %cX — ANSI color
    r'x[a-fA-F0-9]{2}|' # %xNN — hex char
    r'%'                # %% — literal percent
    r')'
)


# ---------------------------------------------------------------------------
# Lint Checks
# ---------------------------------------------------------------------------

class LintResult:
    def __init__(self):
        self.errors = []
        self.warnings = []

    @property
    def ok(self):
        return len(self.errors) == 0

    def error(self, location, msg):
        self.errors.append((location, msg))

    def warn(self, location, msg):
        self.warnings.append((location, msg))


def check_balanced(text, open_ch, close_ch, name):
    """Check that open/close characters are balanced. Returns list of issues."""
    issues = []
    depth = 0
    for i, ch in enumerate(text):
        if ch == open_ch:
            depth += 1
        elif ch == close_ch:
            depth -= 1
            if depth < 0:
                issues.append(f"Unmatched '{close_ch}' at position {i}")
                depth = 0
    if depth > 0:
        issues.append(f"Unclosed '{open_ch}' ({depth} remaining)")
    return issues


def check_functions(text):
    """Check that function names in [func()] are known."""
    issues = []
    # Find all function calls: [funcname( or [funcname(
    for m in re.finditer(r'\[(\w+)\s*\(', text):
        fname = m.group(1).lower()
        if fname not in KNOWN_FUNCTIONS:
            issues.append((m.start(), f"Unknown function: {fname}()"))
    return issues


def check_substitutions(text):
    """Check for invalid % substitution patterns."""
    issues = []
    i = 0
    while i < len(text):
        if text[i] == '%' and i + 1 < len(text):
            # Try to match a valid substitution
            rest = text[i:]
            m = VALID_SUBS.match(rest)
            if not m:
                # Invalid substitution
                snippet = text[i:i+4]
                issues.append((i, f"Invalid substitution: {snippet}"))
            else:
                i += m.end() - 1  # skip past the match
        i += 1
    return issues


def check_dangerous(text):
    """Check for dangerous function/command usage."""
    issues = []
    for pattern, desc in DANGEROUS_PATTERNS:
        for m in re.finditer(pattern, text, re.IGNORECASE):
            issues.append((m.start(), f"Dangerous: {desc}"))
    return issues


def lint_softcode(text, location=''):
    """Run all lint checks on a softcode string.

    Returns a LintResult.
    """
    result = LintResult()

    # Balanced delimiters
    for issues in [
        check_balanced(text, '[', ']', 'bracket'),
        check_balanced(text, '(', ')', 'paren'),
        check_balanced(text, '{', '}', 'brace'),
    ]:
        for issue in issues:
            result.error(location, issue)

    # Function names
    for pos, msg in check_functions(text):
        result.warn(location, msg)

    # Substitutions — check when text looks like it contains code or subs
    if '[' in text or '@' in text or ('%' in text and '%%' not in text):
        for pos, msg in check_substitutions(text):
            result.warn(location, msg)

    # Dangerous patterns
    for pos, msg in check_dangerous(text):
        result.error(location, msg)

    return result


# ---------------------------------------------------------------------------
# Spec Linting — lint all attributes in a WorldSpec
# ---------------------------------------------------------------------------

def lint_spec(spec):
    """Lint all softcode in a WorldSpec's attributes.

    Returns a LintResult with all issues across all rooms and things.
    """
    result = LintResult()

    for room_id, room in spec.rooms.items():
        for attr_name, attr_value in room.attrs.items():
            loc = f"{room_id}/{attr_name}"
            sub = lint_softcode(attr_value, loc)
            result.errors.extend(sub.errors)
            result.warnings.extend(sub.warnings)

    for thing_id, thing in spec.things.items():
        for attr_name, attr_value in thing.attrs.items():
            loc = f"{thing_id}/{attr_name}"
            sub = lint_softcode(attr_value, loc)
            result.errors.extend(sub.errors)
            result.warnings.extend(sub.warnings)

    return result


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main():
    import argparse
    parser = argparse.ArgumentParser(
        description='Softcode Linter — static analysis of MUX softcode')
    parser.add_argument('input', help='YAML spec file or raw softcode text file')
    parser.add_argument('--raw', action='store_true',
                        help='Treat input as raw softcode text (not a spec)')

    args = parser.parse_args()
    path = Path(args.input)

    if not path.exists():
        print(f"Error: File not found: {path}")
        return 1

    if args.raw:
        # Lint raw softcode text
        text = path.read_text(encoding='utf-8')
        result = lint_softcode(text, str(path))
    else:
        # Lint a YAML spec
        import yaml
        from worldbuilder import parse_spec
        spec = parse_spec(path)
        result = lint_spec(spec)

    if result.ok and not result.warnings:
        print("Softcode lint: All checks passed.")
        return 0

    for loc, msg in result.errors:
        print(f"ERROR [{loc}]: {msg}")
    for loc, msg in result.warnings:
        print(f"WARN  [{loc}]: {msg}")

    return 1 if not result.ok else 0


if __name__ == '__main__':
    exit(main())
