# MUX Softcode Formatting Tools

Two Ragel -G2 tools for working with MUX softcode (`.mux` files):

- **`unformat`** -- joins indented, human-readable softcode into the
  single-line format that MUX servers expect (replaces `unformat.pl`).
- **`reformat`** -- the inverse: takes single-line commands and
  re-introduces indentation based on brace structure.

Together they provide a near-perfect round-trip:

```
unformat *.mux | reformat > readable.mux   # flatten then re-indent
unformat readable.mux                      # identical to original
```

## Building

```sh
ragel -G2 -o unformat.c unformat.rl && cc -O2 -o unformat unformat.c
ragel -G2 -o reformat.c reformat.rl && cc -O2 -o reformat reformat.c
```

`Makesmoke` builds `unformat` automatically if the binary is missing or
stale.

---

# unformat

`unformat` joins continuation lines into single-line commands.

## Usage

```sh
./unformat file1.mux [file2.mux ...] > output.txt
```

Output goes to stdout.  Warnings and errors go to stderr.

## Formatting Rules

### Commands

A line that starts with a **non-whitespace** character begins a new
command.  Subsequent lines that start with **whitespace** are
continuation lines: the leading whitespace is stripped and the content
is appended directly to the current command with **no space inserted**.

A line containing only `-` (dash) ends the current command.

```
# Example: the following formatted softcode...
&CMD-TEST me=$@test:
    @switch hasflag(%#, wizard)=1, {
        @pemit %#=
            Test worked.
    }, {
        @pemit %#=You ain't no wizard!
    }
-

# ...produces this single line:
# &CMD-TEST me=$@test:@switch hasflag(%#, wizard)=1, {@pemit %#=Test worked.}, {@pemit %#=You ain't no wizard!}
```

### Space-Backslash Continuation (` \`)

Because continuation lines join **without** inserting a space, you need
a way to signal "there should be a space here."  A trailing **space
followed by backslash** (` \`) at the end of a line inserts a space at
the join point.  The ` \` itself is consumed and not emitted.

```
&greeting obj=You have 5 \
    coins remaining.
-
# produces: &greeting obj=You have 5 coins remaining.
```

A **bare backslash** at the end of a line (no preceding space) is
emitted literally.  Backslash is a valid softcode escape character
(`\n`, `\t`, etc.), so only the ` \` (space-backslash) form is special.

```
&test obj=line one\n \
    line two
-
# produces: &test obj=line one\n line two
#                          ^^ literal \n    ^ space from ' \'
```

### Comments

Lines starting with `#` are comments and are skipped entirely.  The
exception is `#include`:

```
# This is a comment.
#include helpers.mux
```

### `#include`

`#include <filename>` inserts the contents of `<filename>` at that
point.  Cycle detection prevents the same file from being included
twice; a warning is emitted on stderr if a duplicate include is
attempted.

### Empty Lines

Blank lines (empty or whitespace-only) are skipped and do not affect
the output.

## Warnings

`unformat` emits warnings to stderr for common mistakes.  These do not
affect the output -- the file is still processed normally.

### Missing `-` Terminator

If a new command starts while the previous command has no `-` end
marker, or if a file ends with an unterminated command:

```
foo.mux:12: warning: command has no '-' terminator
```

The command is still emitted, but the missing marker often indicates a
formatting mistake (e.g., the next command was accidentally swallowed as
a continuation).

### Digit-Alpha Merge

If a continuation join produces a digit immediately followed by a letter
with no space between them:

```
foo.mux:15: warning: digit-alpha merge '5c' at join -- did you mean to end the previous line with ' \'?
```

This catches the most common class of accidental token merges.  The
output is still produced as-is, but the warning tells you where to look.

### `#include` File Not Found

```
foo.mux:3: error: can't open 'missing.mux'
```

Processing continues with the remaining input.

## Migration from `unformat.pl`

`unformat` is a drop-in replacement.  It produces byte-identical output
to the Perl script on all existing `.mux` files.  The two new features
(` \` continuation and warnings) are purely additive -- no existing
files need to change.

To adopt ` \` in place of fragile trailing spaces, change lines like:

```
# Old style: trailing space (invisible, tools strip it)
&attr obj=You have 5
    coins.
-
```

to:

```
# New style: explicit ' \' (visible, tools leave it alone)
&attr obj=You have 5 \
    coins.
-
```

---

# reformat

`reformat` is the inverse of `unformat`: it takes single-line MUX
commands and re-introduces indentation based on brace and semicolon
structure.

## Usage

```sh
./reformat [file ...]              # reads stdin if no files given
./unformat *.mux | ./reformat      # re-indent from flat form
```

## How It Works

`reformat` walks each input line character by character, tracking three
depth counters:

| Counter | Characters | Purpose |
|---------|-----------|---------|
| `depth` | `{` `}` | Brace nesting -- drives indentation |
| `pdepth` | `(` `)` | Parenthesis nesting -- suppresses breaks inside function calls |
| `bdepth` | `[` `]` | Bracket nesting -- suppresses breaks inside eval brackets |

Breaks are inserted **only** when `pdepth == 0` and `bdepth == 0`:

| Pattern | Action |
|---------|--------|
| `{` | Emit `{`, increase depth, break to new line |
| `}` | Decrease depth, break to new line, emit `}` |
| `;` | Emit `;`, break to new line (same depth) |
| `},{` | Kept together on one line at the outer depth (common `@if`/`@switch` pattern) |
| `{}` | Kept together, no break (empty braces) |

All continuation lines are indented with `4 + depth * 4` spaces,
ensuring they start with whitespace for `unformat` compatibility.

## Example

Input (single line):
```
&test obj=@if expr={@log ok;@trig me/done},{@log fail;@trig me/done}
```

Output:
```
&test obj=@if expr={
        @log ok;
        @trig me/done
    },{
        @log fail;
        @trig me/done
    }
-
```

Nested example:
```
&test obj=@if outer={@if inner={deep;deeper},{shallow}},{other}
```

Becomes:
```
&test obj=@if outer={
        @if inner={
            deep;
            deeper
        },{
            shallow
        }
    },{
        other
    }
-
```

## Round-Trip

The `reformat` output is a valid `.mux` file.  Running it back through
`unformat` produces the original single-line commands:

```sh
# Start with flat commands
echo '@if 1={a;b},{c}' > flat.txt

# reformat, then unformat -- identical to original
cat flat.txt | ./reformat | ./unformat /dev/stdin 2>/dev/null | head -1
# Output: @if 1={a;b},{c}
```

The round-trip is exact for all command content.  Only blank-line
spacing between commands may differ (unformat's "extraspace" vs
reformat's single-line `-` terminators).
