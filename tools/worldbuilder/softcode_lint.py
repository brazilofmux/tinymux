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

# Authoritative function list extracted from builtin_function_list[] in
# mux/modules/engine/functions.cpp.  To regenerate:
#
#   grep -oP '\{T\("([A-Z_0-9]+)"\)' mux/modules/engine/functions.cpp \
#     | sed 's/{T("//;s/")//' | grep -v '^_' | sort -u \
#     | awk '{printf "    '\''%s'\'',\n", tolower($0)}'
#
# Internal GOD-only helpers (prefixed with _) are excluded.
#
KNOWN_FUNCTIONS = {
    '@@', 'abs', 'accent', 'acos', 'add', 'addrlog', 'after', 'allof',
    'alphamax', 'alphamin', 'and', 'andbool', 'andflags', 'ansi', 'aposs',
    'art', 'asin', 'astbench', 'asteval', 'atan', 'atan2', 'attrcnt',
    'attrib_set', 'band', 'baseconv', 'beep', 'before', 'benchmark',
    'between', 'bittype', 'bnand', 'bor', 'bound', 'bxor', 'cand',
    'candbool', 'cansee', 'caplist', 'capstr', 'case', 'caseall', 'cat',
    'cbuffer', 'cdesc', 'ceil', 'cemit', 'center', 'cflags', 'chanfind',
    'chaninfo', 'channels', 'chanobj', 'chanuser', 'chanusers', 'children',
    'choose', 'chr', 'citer', 'clone', 'cmds', 'cmogrifier', 'cmsgs',
    'colordepth', 'columns', 'comalias', 'comp', 'comtitle', 'con',
    'config', 'conn', 'connlast', 'connleft', 'connlog', 'connmax',
    'connnum', 'connrecord', 'conntotal', 'controls', 'convsecs',
    'convtime', 'cor', 'corbool', 'cos', 'cowner', 'cpad', 'crc32',
    'crc32obj', 'create', 'crecall', 'cstatus', 'ctime', 'ctu', 'cusers',
    'cwho', 'dec', 'decode64', 'decrypt', 'default', 'delete', 'delextract',
    'destroy', 'die', 'digest', 'digittime', 'dist2d', 'dist3d',
    'distribute', 'doing', 'dumping', 'dynhelp', 'e', 'edefault', 'edit',
    'elements', 'elock', 'emit', 'encode64', 'encrypt', 'entrances', 'eq',
    'error', 'escape', 'etimefmt', 'eval', 'exit', 'exp', 'exptime',
    'extract', 'fcount', 'fdepth', 'fdiv', 'filter', 'filterbool',
    'findable', 'first', 'firstof', 'flags', 'floor', 'floordiv', 'fmod',
    'fold', 'foreach', 'fullname', 'garble', 'get', 'get_eval', 'gmcp',
    'grab', 'graball', 'grep', 'grepi', 'gt', 'gte', 'hasattr', 'hasattrp',
    'hasflag', 'haspower', 'hasquota', 'hasrxlevel', 'hastxlevel',
    'hastype', 'height', 'hmac', 'home', 'host', 'iabs', 'iadd', 'idiv',
    'idle', 'if', 'ifelse', 'ilev', 'imul', 'inc', 'index', 'insert',
    'inum', 'inzone', 'isalnum', 'isalpha', 'isdbref', 'isdigit', 'isign',
    'isint', 'isjson', 'isnum', 'isobjid', 'israt', 'isub', 'isword',
    'itemize', 'iter', 'itext', 'jitstats', 'json', 'json_mod',
    'json_query', 'ladd', 'land', 'last', 'lastcreate', 'lattr',
    'lattrcmds', 'lattrp', 'lcmds', 'lcon', 'lcstr', 'ldelete', 'ledit',
    'letq', 'lexits', 'lflags', 'link', 'linsert', 'list', 'listq',
    'listrlevels', 'lit', 'ljust', 'lmath', 'lmax', 'lmin', 'ln', 'lnum',
    'loc', 'localize', 'locate', 'lock', 'lockdecode', 'lockencode', 'log',
    'lor', 'lpad', 'lparent', 'lplayers', 'lports', 'lpos', 'lrand',
    'lreplace', 'lrest', 'lrooms', 'lt', 'lte', 'lthings', 'lua', 'lwho',
    'mail', 'mailcount', 'mailflags', 'mailfrom', 'mailinfo', 'maillist',
    'mailreview', 'mailsend', 'mailsize', 'mailstats', 'mailsubj',
    'malias', 'map', 'mapsql', 'match', 'matchall', 'max', 'mean',
    'median', 'member', 'merge', 'mid', 'min', 'mix', 'mod', 'money',
    'moniker', 'moon', 'motd', 'mtime', 'mudname', 'mul', 'munge', 'name',
    'ncon', 'nearby', 'neq', 'nexits', 'next', 'not', 'nplayers', 'nsemit',
    'nsoemit', 'nspemit', 'nsremit', 'nthings', 'null', 'num', 'obj',
    'objeval', 'objid', 'objmem', 'oemit', 'or', 'orbool', 'ord',
    'orflags', 'owner', 'pack', 'parenmatch', 'parent', 'pemit', 'pfind',
    'pi', 'pickrand', 'playmem', 'pmatch', 'pocvm2', 'poll', 'ports',
    'pos', 'pose', 'poss', 'power', 'powers', 'printf', 'prompt', 'r',
    'rand', 'regedit', 'regeditall', 'regeditalli', 'regediti', 'reglattr',
    'reglattri', 'regmatch', 'regmatchi', 'regnattr', 'regnattri', 'regrab',
    'regraball', 'regraballi', 'regrabi', 'regrep', 'regrepi', 'remainder',
    'remit', 'remove', 'repeat', 'replace', 'rest', 'restarts',
    'restartsecs', 'restarttime', 'reverse', 'revwords', 'right', 'rjust',
    'rloc', 'roman', 'room', 'round', 'rpad', 'rserror', 'rsnext',
    'rsprev', 'rsrec', 'rsrecnext', 'rsrecprev', 'rsrelease', 'rsrows',
    'rvbench', 'rxlevel', 's', 'sandbox', 'scramble', 'search', 'secs',
    'secure', 'set', 'setdiff', 'setinter', 'setq', 'setr', 'setunion',
    'sha1', 'shl', 'shr', 'shuffle', 'sign', 'sin', 'singletime',
    'siteinfo', 'sort', 'sortby', 'sortkey', 'soundex', 'soundlike',
    'space', 'spellnum', 'splice', 'sql', 'sqrt', 'squish', 'startsecs',
    'starttime', 'stats', 'stddev', 'step', 'strallof', 'strcat',
    'strdelete', 'strdistance', 'strfirstof', 'strinsert', 'strip',
    'stripaccents', 'stripansi', 'strlen', 'strmatch', 'strmem',
    'strreplace', 'strtrunc', 'sub', 'subeval', 'subj', 'subnetmatch',
    'successes', 'switch', 'switchall', 't', 'table', 'tan', 'tel',
    'terminfo', 'textfile', 'time', 'timefmt', 'tr', 'trace', 'translate',
    'trigger', 'trim', 'trunc', 'txlevel', 'type', 'u', 'ucstr',
    'udefault', 'ulambda', 'ulocal', 'unique', 'unpack', 'unsetq',
    'url_escape', 'url_unescape', 'v', 'vadd', 'valid', 'vcross', 'vdim',
    'vdot', 'verb', 'version', 'visible', 'vmag', 'vmul', 'vsub', 'vunit',
    'where', 'while', 'width', 'wipe', 'wordpos', 'words', 'wrap',
    'wrapcolumns', 'writetime', 'xget', 'xor', 'zchildren', 'zexits',
    'zfun', 'zone', 'zrooms', 'zthings', 'zwho',
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
    (r'@pemit\b[^\n]*%#', '@pemit with %# — may reflect enactor-controlled input or secrets'),
    (r'@trig(?:ger)?\b', '@trigger — can recurse or fire side effects immediately'),
    (r'\bcreate\s*\(', 'create() — creates objects dynamically'),
    (r'%\(', '%(...) substitution — evaluates dynamic text; escape user input first'),
    (r'\bsetr\s*\(', 'setr() — writes q-registers from dynamic text; escape user input first'),
    (r'\bmail(?:send|review|from|subj|flags|stats|count|info|list)?\s*\(',
     'mail*() — accesses mail state; review authentication and data exposure'),
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
