---
title: History of TinyMUX
date: March 2026
---

# History of TinyMUX

TinyMUX descends from a lineage of multi-user virtual worlds stretching
back to 1978. This document traces the genealogy from the original MUD
through the MUSH family to the present day.

## Prehistory: MUD1 (1978–1999)

Roy Trubshaw and Richard Bartle created MUD (Multi-User Dungeon) at the
University of Essex in 1978 on a DEC PDP-10. Trubshaw named it in
tribute to the "Dungeon" variant of Zork, which he had greatly enjoyed.
The first versions were written in MACRO-10 assembly; in late 1979,
Trubshaw rewrote it in BCPL (version 3) for maintainability. When
Trubshaw graduated in 1980, Bartle took over, fleshing out the game
database, adding the persona communication system, and introducing the
concepts of points and wizards. That same year, Essex connected to
ARPANET, making MUD1 the first Internet multiplayer online game.

In 1983, Essex allowed outside access via British Telecom's Packet
Switch Stream between 2 am and 7 am nightly, and MUD became popular
worldwide. CompuNet licensed MUD1 in late 1984. CompuServe licensed it
in 1987, pressuring Bartle to close the original Essex instance; the
MUD account was deleted that October. MUD1 ran on CompuServe as
"British Legends" until late 1999 when it was retired in Y2K cleanup.

Trubshaw and Bartle formed Multi-User Entertainment Ltd. and developed
MUD2 (released 1985). In 2000, Viktor Toth rewrote the BCPL source to
C++ and reopened MUD1 alongside MUD2 on british-legends.com. The
PDP-10 source code was released on GitHub in 2020.

## AberMUD (1987–1989)

Alan Cox, Richard Acott, Jim Finnis, and Leon Thrane wrote AberMUD at
the University of Wales, Aberystwyth in 1987. The first version was
written in B for a Honeywell mainframe; Cox was a player of MUD1 and
the gameplay was heavily influenced by Bartle's original. In late 1988,
Cox ported it to C (AberMUD2) so it could run on Unix at Southampton
University — a turning point in virtual world history. In January 1989,
Michael Lawrie sent a licensed copy to Vijay Subramaniam and Bill
Wisner, both American Essex MIST players; Wisner subsequently spread
AberMUD around the world.

As Richard Bartle wrote: "AberMUD spread across university computer
science departments like a virus. Identical copies appeared on
thousands of Unix machines. The three most important [imitators] were
TinyMUD, LPMUD and DikuMUD."

## LPMUD and DikuMUD (1989–1990)

Lars Pensjö created LPMUD in 1989, introducing in-game programming
via the LPC language. DikuMUD, created by Sebastian Hammer, Tom Madsen,
Katja Nyboe, Michael Seifert, and Hans Henrik Stærfeldt at DIKU
(Datalogisk Institut, Københavns Universitet) in 1990, became the
dominant combat-oriented MUD family. These represent parallel lineages
to TinyMUX and are not direct ancestors.

## TinyMUD (1989)

TinyMUD was inspired by Monster, a multi-user adventure game created by
Richard Skrenta for the VAX in November 1988. Monster pioneered the
approach of allowing players to build the game world. James Aspnes
created TinyMUD as a portable, stripped-down version of Monster,
written in C for Unix. He announced it to a few friends on August 19,
1989; its port number, 4201, was Aspnes' office number at Carnegie
Mellon University.

Unlike the combat-oriented AberMUD and its descendants, TinyMUD
emphasized social interaction and player-driven building. The "D" was
said to stand for "Dimension" or "Domain" rather than "Dungeon," to
distance it from the hack-and-slash tradition. Any player could create
rooms, exits, and objects. The code was small, freely distributed, and
quickly spawned dozens of variants. The first TinyMUD database,
"TinyMUD Classic" (also known as "Islandia"), ran from August 1989 to
April 1990.

TinyMUD is the common ancestor of the entire MUSH/MUX/MUSE family.
Its descendants include TinyMUCK (which added the MUF programming
language), TinyMUSH (which added attribute-based softcode), TinyMUSE,
and eventually TinyMUX. UberMUD, UnterMUD, and MOO were inspired by
TinyMUD but are not direct descendants.

## TinyMUSH 1.0 (1990)

Larry Foard extended TinyMUD with a softcode programming language —
attribute-stored expressions evaluated at runtime, with a syntax
similar to Lisp. The name "MUSH" was a backronym later expanded as
"Multi-User Shared Hallucination." TinyMUSH 1.0 introduced the
fundamental concepts that define the MUSH family to this day:
attribute-based programming, locks, zone-based administration, and
evaluated expressions using `[]` function calls and `%`-substitutions.

Several variants emerged from TinyMUSH 1.0:

 - **TinyCWRU** — Glenn Crocker's fork at Case Western Reserve
   University.
 - **TinyTIM** — a long-running social MUSH.
 - **MicroMUSH** — which led to TinyMUSE (MicroMUSE).

## PernMUSH / PennMUSH (1991–1995)

JT Traub (Moonchilde) released PernMUSH in January 1991, derived from
MicroMUSE. Lydia Leong (Amberyl) released PernMUSH 1.16 in January
1992. PernMUSH was renamed to PennMUSH by Amberyl and reached version
1.50 patch 10 in June 1994, after which Alan Schwartz (Javelin) assumed
maintenance in early 1995. Javelin maintained PennMUSH until July 2006
when Raevnos (Alex) took over. PennMUSH remains actively maintained
today — the longest-running MUSH codebase.

## TinyMUSH 2.0 (1991–1994)

In spring 1991, JT Traub (Moonchilde), Glenn Crocker, and Dave Peterson
(Evinar) began developing TinyMUSH 2.0, a major rewrite incorporating
features from PernMUSH, TinyCWRU, and TinyMUSH 1.0. In November 1991,
PernMUSH the game switched to the TinyMUSH 2.0 codebase. Evinar
released TinyMUSH 2.0.10 patchlevel 6 as the final version in April
1994.

TinyMUSH 2.0.10p6 is the direct ancestor of both TinyMUX and
TinyMUSH 3.

## TinyMUSH 2.2 (1994–1995)

Lydia Leong (Amberyl), Jean Marie Diaz (Ambar), and Deborah Hooker
(Ysabel) began TinyMUSH 2.2 in fall 1994. Released in April 1995,
it added significant features but did not merge the TinyMUX line.

## TinyMUX 1.x (1994–1998)

David Passmore (Lauren) forked TinyMUSH 2.0.10p6 to create TinyMUX 1.0.
The "MUX" name reflected its origins as a "multiplexed" variant focused
on performance and reliability. TinyMUX 1.x reached version 1.6 before
the 2.0 rewrite.

## TinyMUSH 3.0 (1999–2010)

TinyMUSH 3.0 merged the TinyMUSH 2.2 and TinyMUX 1.6 codebases. Led
by Lydia Leong (Amberyl) and David Passmore (Lauren), with Robby
Griffin (Alierak) joining in November 1999, TinyMUSH 3.0 entered beta
in September 1999 and was released in December 2000. TinyMUSH 3.1
introduced the first loadable plugin module system in the MUSH family.
Eddy Beaupre (Tyr) joined the team around 2004. Lydia Leong returned
as primary maintainer in May 2007 and released TinyMUSH 3.2 beta 3 in
June 2010 as her final release. Tyr assumed maintenance in June 2011
and released TinyMUSH 3.2 gamma.

## TinyMUX 2.0 (1998–present)

Stephen Dennis (Brazil) began TinyMUX 2.0 in September 1998 as a
Windows NT port of TinyMUX 1.6, initially named "Win32MUX." The name
TinyMUX 2.0 was adopted in March 1999. Unlike TinyMUSH 3 which merged
TinyMUSH 2.2 and TinyMUX 1.6, TinyMUX 2.0 continued the pure TinyMUX
line from TinyMUSH 2.0.10p6, adding Windows native support from the
start.

Key releases:

 - **2.0** (2001) — Bug fixes, performance, native Windows support.
 - **2.1** (2002) — Feature development begins.
 - **2.2** (2002) — Continued features and fixes.
 - **2.3** (2004) — Mixed features/fixes/performance.
 - **2.4** (August 2006) — Stability release.
 - **2.6** (April 2007) — Major feature release.
 - **2.7** (October 2008) — Unicode support, SSL, loadable modules.
 - **2.9** (May 2010) — Refinement release.
 - **2.10** (July 2012) — 256-color and 24-bit (truecolor) support.
 - **2.12** — PCRE2, autoconf/automake modernization.
 - **2.13** — SQLite storage backend, GANL networking, NFC
   normalization, Unicode 16.0, DUCET collation, grapheme clustering.
 - **2.14** (in development) — Three-layer architecture
   (libmux/engine/driver), AST-based expression evaluator, COM module
   system, comsys and @mail module extraction.

In late 2002, PennMUSH, TinyMUSH, and TinyMUX jointly adopted the
Clarified Artistic License, ensuring all three major MUSH codebases
were Free Software / Open Source.

## Other Notable Relatives

 - **RhostMUSH** — developed from TinyMUD via TinyMUSE, independently
   maintained with its own feature set.
 - **TinyMUSE** — derived from MicroMUSH, focused on educational and
   science-fiction environments. BattletechMUSE was a notable instance.
 - **BattletechMUX** — combined TinyMUX 1.x with BattletechMUSE
   mechanics for giant-robot combat simulation.
 - **TinyMARE** — a hybrid of TinyMUSH and TinyMUSE created by Byron
   Stanoszek, developed 1993–2003.
 - **CobraMUSH** — a PennMUSH derivative.

## Family Tree

```
MUD1 (1978, Trubshaw/Bartle, Essex)
 |
 +-- AberMUD (1987, Cox et al., Aberystwyth)
 |    |
 |    +-- TinyMUD (1989, Aspnes, CMU)
 |         |
 |         +-- TinyMUSH 1.0 (1990, Foard)
 |              |
 |              +-- TinyCWRU (Glenn Crocker)
 |              +-- MicroMUSH --> TinyMUSE
 |              |                  |
 |              |                  +-- MicroMUSE/PernMUSH (1991, Traub)
 |              |                  |    |
 |              |                  |    +-- PennMUSH (1992+, Leong/Javelin)
 |              |                  |
 |              |                  +-- BattletechMUSE
 |              |                  |
 |              |                  +-- RhostMUSH
 |              |
 |              +-- TinyMUSH 2.0 (1991, Traub/Crocker/Peterson)
 |                   |
 |                   +-- TinyMUSH 2.2 (1994, Leong/Diaz/Hooker)
 |                   |    |
 |                   |    +---+
 |                   |        |
 |                   |        +-- TinyMUSH 3.0 (1999, merged 2.2 + MUX 1.6)
 |                   |
 |                   +-- TinyMUX 1.x (1994, Passmore)
 |                        |
 |                        +---+  (MUX 1.6 also merged into TinyMUSH 3.0)
 |                        |
 |                        +-- TinyMUX 2.x (1998+, Dennis)
 |                             |
 |                             +-- TinyMUX 2.14 (current)
 |
 +-- LPMUD (1989, Pensjö) --> MudOS, FluffOS, etc.
 |
 +-- DikuMUD (1990, Hammer et al.) --> CircleMUD, ROM, Merc, etc.
```

## References

 - TinyMUX Wiki: https://wiki.tinymux.org/index.php/History
 - Richard Bartle, "Designing Virtual Worlds," New Riders, 2003.
 - Lauren A. Burka, "The MUDline," 1995.
 - Richard Bartle, "Early MUD History," 1990.
 - Michael Lawrie, "Escape from the Dungeon," 2003.
 - Richard Skrenta, "An Introduction to Monster," 1997.
 - James Aspnes, "Monster" (alt.mud post), July 4, 1990.
 - Bill Wisner, "A brief history of MUDs" (alt.mud), June 29, 1990.
 - Jessica Mulligan & Bridgette Patrovsky, "Developing Online Games:
   An Insider's Guide," New Riders, 2003.
