# Assessment: cross-game object transfer (`@snapshot`) — declined

**Date:** 2026-07-22
**Prompted by:** a pointer from Ashen-Shugar (RhostMUSH) on Discord — *"check out
@snapshot. It may be something useful especially as it would allow cross-loading
objects between games."*
**Outcome:** declined. TinyMUX already has the capability, in a safer form.

## The question

Should TinyMUX gain a serialize-an-object-to-a-file / load-it-into-another-game
feature, modelled on RhostMUSH's `@snapshot`?

## We already have both halves

`@decompile` (`CA_PUBLIC`, `look.cpp:2902`) renders an object as ordinary
softcode commands — `@create`, `@dig`, `@open`, `@lock`, `@set`, and its
attributes. Feed that text back into any game and the object is reconstructed.
That is cross-game object transfer, and builders use it today between arbitrary
games with no admin involvement.

Three properties make it the better mechanism, and all three are lost by any
binary/serialized alternative:

1. **The code is human-readable before it executes.** A recipient reads what
   they are about to run. A serialized image is reviewed, at best, by a tool
   nobody has written.
2. **Import goes through the normal command path.** Permissions are checked per
   command, quota is charged by `payfor`/`add_quota` the same as any build, and
   nothing reaches the database except through code that already assumes its
   input is user-supplied.
3. **Privilege cannot ride along.** `decompile_flags` (`flags.cpp:1320`) skips
   any flag carrying `CA_NO_DECOMP` and applies `check_access(player,
   fbe->listperm)` per flag, so the output cannot contain flags the viewer is
   not entitled to see — let alone set.

There is a fourth detail worth calling out because it is the crux of the whole
problem domain. `do_decomp` emits the link line **commented out**:

```
@@ @link <name>=#<dbref>
```

A raw dbref does not survive a move to another game — `#4501` there is not
`#4501` here. Someone deliberately neutralised the one field that cannot be
carried, and left it visible so a human can re-point it. That is the correct
answer to dbref portability: refuse to guess, and show your work.

## What a serialized format would have to solve

Surveyed RhostMUSH's implementation (`wiz.c:3886-4295`, `db_rw.c:1738-1857`) as
prior art. It is a plain-text positional record: name, location, contents,
exits, link, next, lock, owner, parent, pennies, four flag words, two toggle
words, six power/depower words, the zone list, then attributes.

The genuinely good idea in it — and the only part worth remembering — is that
**attributes are serialized by name rather than by number**. Attribute numbers
are per-game, so a name is what makes an object portable between two databases
with different vattr tables. Note that our softcode form already has this
property for free: `&ATTRNAME obj=value` is a name. Rhost needed a second
serializer to arrive where the ordinary distribution format already sits.

The problems a serialized importer must answer, none of which the softcode path
raises:

- **Dbref rebinding.** Parent, zone, and the dbrefs embedded in lock
  expressions all mean something different in the destination. Validating that a
  number names *some* object in the destination is not the same as it naming the
  *right* one.
- **Privilege-bearing fields.** The serialized state includes flag and power
  words. Any importer must decide what it strips, and `@clone` is the local
  precedent: it clears INHERIT and ROYALTY on the copy.
- **Quota.** Attributes arriving by file bypass the accounting that the command
  path performs naturally.
- **Atomicity.** A truncated or malformed file must not leave an object
  half-written. That means writing the prior state somewhere recoverable before
  mutating anything.
- **Versioning and provenance.** A headerless positional format cannot detect
  that the two games disagree about the record layout; the reader simply
  consumes fields at the wrong offsets. For the stated cross-game use case this
  is a likelier source of silent corruption than anything hostile.
- **An audit trail.** An operation that rewrites an object wholesale should say
  so in the log and on SiteMon.

Rhost's trust model for the load path is the operator gate: the command and all
its switches are `CA_IMMORTAL`. That is a defensible choice in their design. It
is not one we want to take on here, because it makes the security of the
database a property of what an administrator chooses to open rather than of the
code — and an administrator loading an interesting-looking object from another
game is exactly the case the feature exists to serve.

## Decision

Declined. The capability exists; the existing mechanism is safer on every axis
that matters; and 2.14 is feature-complete and in a hardening phase, which is
the wrong moment to add an inbound trust boundary that accepts a file produced
by a database we do not control.

If this is ever revisited, the requirements above are the specification, and the
one idea worth carrying forward is serialization by attribute name.

## Related

- `docs/survey-resource-defenses.md` — the RhostMUSH connection-defense survey
  and its three ports / four declines.
- `db_read` (`db_rw.c:555`) already treats flatfiles as potentially hostile:
  out-of-range dbrefs abort the load and corrupt `+A` records are skipped
  (#806). That is the local precedent for how seriously a file-borne import
  path has to take its input.
