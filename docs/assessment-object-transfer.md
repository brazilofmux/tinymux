# Assessment: cross-game object transfer — declined

**Date:** 2026-07-22
**Prompted by:** a suggestion from Ashen-Shugar (RhostMUSH) that a
serialize-an-object-to-a-file feature would be useful for moving objects
between games.
**Outcome:** declined. TinyMUX already has the capability, in a safer form.

## The question

Should TinyMUX gain a feature that serializes an object to a file so it can be
loaded into a different game?

## We already have both halves

`@decompile` (`CA_PUBLIC`, `look.cpp:2902`) renders an object as ordinary
softcode commands — `@create`, `@dig`, `@open`, `@lock`, `@set`, and its
attributes. Feed that text back into any game and the object is reconstructed.
That is cross-game object transfer, and builders use it today between arbitrary
games with no admin involvement.

Three properties make it the better mechanism, and all three are given up by any
binary or serialized alternative:

1. **The code is human-readable before it executes.** A recipient reads what
   they are about to run. A serialized image is reviewed, at best, by a tool
   nobody has written.
2. **Import goes through the normal command path.** Permissions are checked per
   command, quota is charged by `payfor`/`add_quota` exactly as for any build,
   and nothing reaches the database except through code that already assumes its
   input is user-supplied.
3. **Privilege cannot ride along.** `decompile_flags` (`flags.cpp:1320`) skips
   any flag carrying `CA_NO_DECOMP` and applies `check_access(player,
   fbe->listperm)` per flag, so the output cannot contain flags the viewer is
   not entitled to see — let alone set.

There is a fourth detail worth calling out, because it is the crux of the whole
problem domain. `do_decomp` emits the link line **commented out**:

```
@@ @link <name>=#<dbref>
```

A raw dbref does not survive a move to another game — `#4501` there is not
`#4501` here. Someone deliberately neutralised the one field that cannot be
carried, and left it visible so a human can re-point it. That is the correct
answer to dbref portability: refuse to guess, and show your work.

## What a serialized format would have to solve

One idea from the prior art is worth remembering: **serialize attributes by name
rather than by number.** Attribute numbers are per-game, so a name is what makes
an object portable between two databases with different vattr tables. Our
softcode form already has this property for free — `&ATTRNAME obj=value` is a
name.

The requirements any serialized importer would have to meet, none of which the
softcode path raises:

- **Dbref rebinding.** Parent, zone, and the dbrefs embedded in lock
  expressions all mean something different in the destination. Validating that a
  number names *some* object there is not the same as it naming the *right* one.
- **Privilege-bearing state.** A full serialization of an object necessarily
  includes its flag and power words. An importer has to decide explicitly what
  it refuses to carry; `@clone` is the local precedent, clearing INHERIT and
  ROYALTY on the copy.
- **Quota.** Attributes arriving by file bypass the accounting the command path
  performs naturally.
- **Atomicity.** A truncated or malformed file must not leave an object
  half-written, which means preserving the prior state somewhere recoverable
  before mutating anything.
- **Versioning and provenance.** A positional format with no header cannot
  detect that two games disagree about the record layout; the reader simply
  consumes fields at the wrong offsets. For the cross-game use case this is a
  likelier source of silent corruption than anything hostile.
- **An audit trail.** An operation that rewrites an object wholesale should say
  so in the log and on SiteMon.

## Decision

Declined. The capability exists, the existing mechanism is safer on every axis
that matters, and 2.14 is feature-complete and in a hardening phase — the wrong
moment to add an inbound trust boundary that accepts a file produced by a
database we do not control.

If this is ever revisited, the requirements above are the specification, and the
one idea worth carrying forward is serialization by attribute name.

## Related

- `docs/survey-resource-defenses.md` — the RhostMUSH connection-defense survey
  and its three ports / four declines.
- `db_read` (`db_rw.c:555`) already treats flatfiles as potentially hostile:
  out-of-range dbrefs abort the load and corrupt `+A` records are skipped
  (#806). That is the local precedent for how seriously a file-borne import path
  has to take its input.
