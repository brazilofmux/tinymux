# comsys / mail conformance

The artefact #1614 step 4 asked for: **one place that says where the two
implementations of comsys and mail must agree, and what actually checks each
one.**

`comsys` and `mail` each ship twice — built into the engine
(`mux/modules/engine/comsys.cpp`, `mail.cpp`) and as a loadable module
(`mux/modules/comsys/comsys_mod.cpp`, `mail/mail_mod.cpp`). Which one answers
is decided at startup by `discover_comsys_mail_modules`. Nobody owns keeping
them equivalent, and the divergences found so far were found by accident.

This file is deliberately useful under all three of #1614's options: it is the
**acceptance test** under (C) keep-both, the **deletion checklist** under (A)
retire-the-modules, and the **blocker list** under (B) module-only.

## Where they can diverge: the ABI is the list

The fork points are not a matter of judgement. `mudstate.pIComsysControl` and
`mudstate.pIMailControl` are consulted in exactly the places listed below, and
every one is a place the two implementations can answer differently.

**`mux_IComsysControl` — 20 methods** (`mux/include/modules.h`)

| Method | Reached by | Compared by |
|---|---|---|
| `Initialize` | startup | banner asserted by all three harnesses |
| `PlayerConnect` | login | — |
| `PlayerDisconnect` | logout | — |
| `PlayerNuke` | `@nuke` | — |
| `AddAlias` | `addcom` | conformance, mogrify |
| `DelAlias` | `delcom` | conformance **(diverges)** |
| `ClearAliases` | `clearcom` | — |
| `CreateChannel` | `@ccreate` | all three |
| `DestroyChannel` | `@cdestroy` | conformance |
| `AllCom` | `allcom` | conformance **(diverges)** |
| `ComList` | `comlist` | conformance |
| `ComTitle` | `comtitle` | conformance **(diverges)** |
| `ChanList` | `@clist`, `/full`, `/headers` | conformance **(diverges)** |
| `ChanWho` | `@cwho` | conformance (aligned via UnparseObject, #1650) |
| `CEmit` | `@cemit`, `/noheader` | conformance, handoff |
| `CSet` | `@cset` ×11 switches | conformance, handoff, mogrify |
| `EditChannel` | `@editchannel` ×4 flags | conformance (charge only) |
| `CBoot` | `@cboot` | — |
| `ProcessCommand` | speech via alias | conformance **(diverges)**, mogrify |
| `GetRevision` | softcode coherence | implicitly, all three |

**`mux_IMailControl` — 12 methods**

| Method | Reached by | Compared by |
|---|---|---|
| `Initialize` | startup | banner asserted by all three |
| `PlayerConnect` | login | — |
| `PlayerNuke` | `@nuke` | — |
| `MailCommand` | `@mail` ×32 switches | conformance (11), handoff (send/read) |
| `MaliasCommand` | `@malias` ×9 switches | conformance (4) |
| `FolderCommand` | `@folder` ×4 switches | conformance (2) |
| `CheckMail` | login, `@mail/check` | — |
| `ExpireMail` | dump cycle | — |
| `CountMail` | `@mail/stats`, softcode | conformance **(diverges)** |
| `DestroyPlayerMail` | `@destroy` on a player | — |
| `GetRevision` | softcode coherence | implicitly |
| `SoftcodeSend` | `mailsend()` | — |

## Four structural seams

Behaviours above are individual rows. These are the reasons rows keep
appearing, and they are what actually needs deciding.

### 1. Softcode always reads the engine's store

All thirteen comsys/mail softcode functions — `cwho`, `channels`, `comalias`,
`cemit`, `mail`, `mailfrom`, `mailinfo`, `maillist`, `mailreview`, `mailsize`,
`mailsubj`, `mailsend`, `malias` — read **engine** maps, never the module.

Coherence is retrofitted by a revision bridge: `ensure_comsys_softcode_sync`
(`comsys.cpp:2340`) and `ensure_mail_softcode_sync` (`mail.cpp:2576`) ask the
module for a revision number and re-run `sqlite_load_comsys()` /
`sqlite_load_mail()` when it moves.

That works **only for state the module writes through to SQLite**. Anything the
module holds in memory alone is invisible to every function above. The bridge
is also entered from a small fixed set of places — `select_channel`,
`mail_fetch`, `count_mail`, `MailList::FirstItem` and three others — so a new
read path that does not pass through one of those silently gets stale data.

*This is why `tests/comsys_handoff` asserts on the `@mail` command and not on
`mailreview()`: the softcode function reads the engine's store, which was never
the broken side.*

### 2. One concept, two stores (#1589)

Channel state straddles the relational/attribute boundary mid-concept:

| relational row | attributes on the channel object |
|---|---|
| name, header, type, charge, **`num_messages`** | **`HISTORY_%d`**, `MAX_LOG`, `LOG_TIMESTAMPS` |

**The counter is a column; the things it counts are attributes**, with no
foreign key, shared transaction, or anything else that could notice them
disagreeing. #1564 and #1585 are both that seam. Parked for 2.15 — do not start
work on it — but every row in this file that touches history or channel config
inherits the risk.

### 3. Writes that can be refused

Storage writes can fail — foreign keys, constraints, a closed database, a full
disk — and until #1620/#1629/#1634 the results were discarded, so in-memory and
stored state diverged with nothing to notice. All now checked and logged.

Until #1633/#1644 those log lines went nowhere under `muxscript`, so nothing
could assert them. They now reach the output, which is why diagnostics appear
in the conformance baseline.

### 4. Silence is the default failure mode

A missing feature in the module is not an error — it is the absence of one.
`@clist/full` printing the wrong table, comtitle vanishing from speech, and
`/dstats` answering as `/stats` are all silent. This is why the third harness
diffs whole output rather than asserting behaviours: an assertion can only
catch what somebody already thought to write a case for, and every divergence
in the table below survived precisely because nobody had.

## What checks what

| Harness | Shape | Catches |
|---|---|---|
| `make test-comsys-handoff` | write under one implementation, read under the other, one persistent DB | disagreement about **stored** state (#1564, #1585, #1587, #1620) |
| `make test-comsys-mogrify` | each side joins and speaks in one process | disagreement during **delivery** — MOGRIFY hooks, CHATFORMAT (#1572) |
| `make test-comsys-conformance` | same commands both ways, whole output diffed against a baseline | disagreement in **what a command prints** (#1631, #1637, #1639, #1640) |

They are three shapes because none subsumes another. Delivery cannot be tested
by the handoff driver — `bConnected` is runtime state set when a player joins
during *that* process and is not persisted, so a run inheriting membership
delivers to nobody. Printed output cannot be tested by either of the others,
which is the gap that hid four divergences until #1614 step 4.

The smoke corpus is **not** on this list. It scores 1561/1561 against both
implementations; it cannot tell them apart at all.

## Open divergences

Live list is `tests/comsys_conformance/known-divergences.diff`, which fails the
build when it stops being accurate in either direction. Summary:

| # | Behaviour | Engine | Module | Issue |
|---|---|---|---|---|
| 1 | `@mail` sender name | empty under muxscript (stub `TrimmedName`) | renders the name | #1637 |
| 2 | `@mail <n>` `To:` line | present | **omitted** | #1637 |
| 3 | `@mail/debug` label | `Vector capacity: 0` | `Allocated slots 0` | — |
| 4 | module lifecycle logs | silent | load / shutdown lines | — |

Closed by #1647 (items 1, 3, 4 of #1640, plus padding and speech comtitle):
`@clist/full`, comtitle on speech/join/leave, `delcom` personal confirmation,
and `@clist` description padding. Baseline shrank **39 → 29** divergent lines.

`@cwho` (item 2 of #1640) closed by #1650: `UnparseObject` on
`mux_IObjectInfo` + module Name column. Mail `/stats`/`/dstats`/`/fstats`
aligned by #1655 / #1657 (including the phantom `+1` size). Baseline is now
**19** known-divergent lines; #1640 is closed.

Row 1 is where the **module is the more correct side** for player UX
(#1637 empty From is a harness stub under muxscript).

## Adding a row

Add the command to the stream in `tests/comsys_conformance/run.sh`, run
`tests/comsys_conformance/run.sh --bless`, and **read the delta before
committing it**. A baseline blessed without reading is a record of what master
did that day, not a guard.

Keep the stream deterministic: no wall clock, no randomness, no dbref
allocation that depends on earlier output. Nondeterminism belongs out of the
stream, not into the normaliser — #1637 was an uninitialised stack read that
would have been papered over by a normaliser rule.
