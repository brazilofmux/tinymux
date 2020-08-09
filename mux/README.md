---
author: Brazil
date: August 2020
title: README (Unix)
---

Congratulations and welcome to the `README.md` file for TinyMUX 2.13. That
you have chosen to actually read the file is a sign of bravery equal to
that of Beowulf facing Grendel or anyone who has ever faced the garden
variety lumbering behemoth.

We know that `README` files are often turgid and vague, filled with
technobabble that ties the reader up needlessly in corundums of little
purpose. Too often, the story ends with someone spending a lot of time
with someone else in countless hours of consultation, all to type a few
lines that were there the whole time, buried beyond view.

In the past, this has resulted in `README` being tossed aside, unread,
unwanted, unloved, and probably bitter from the experience. However,
because we want you to succeed, we are happy to provide a file that
will, in our sincerest hopes, provide you with enough information to
make that possible. If not, it should be at least an entertaining read.

The TinyMUX 2.13 `README` and supplemental documentation has been divided
into multiple files. For discussion and instructions on how to do
various things associated with the care and feeding of your TinyMUX,
please refer to the following files:

  -------------------- --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
  `README.md`          General information on the distribution, platforms it has been shown to run under out of the box, and how to report bugs.
  `CHANGES.md`         New features, commands, and bug fixes.
  `INSTALL.md`         How to compile your game, step-by-step.
  `NOTES.md`           All kinds of tidbits made much easier to find. Known compiling issues and known fixes are found here. Known OSes that TinyMUX 2.13 will run on and compiling issues. Has basic conversion information.
  `docs/BACKUP`        A file about the TinyMUX 2.13 Backup script.
  `docs/CONFIGURATION` How to set up your TinyMUX, including an explanation of the common configuration items, as well as how to set up your game so that your database files so that they are customized for your `GAMENAME`.
  `docs/CONVERSION`    How to convert a game from another type of server to TinyMUX.
  `docs/CREDITS`       The people who helped make this server possible.
  `docs/GUESTS`        How the current guest implementation works and how to set it up.
  `docs/MEMORY`        Explanations about how to set up disk and memory-based operation plus tips on saving memory.
  `docs/PATCHES`       A basic introduction to applying patches to the server.
  -------------------- --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

# General Information:

TinyMUX 2.13 is written in a mix of C and C++ and is currently being
developed on Windows, Debian, and FreeBSD. It is a continuation of the
TinyMUX 1.x flavor of MUSH servers.

The latest version of this code is found `brazilmux.tinymux.org 2860`.

Refer to the file `CHANGES.md` for information about new features,
commands, and bug fixes. `NOTES.md` now has a lot of small bits of info
related to running the server, as well as reminders about how to
flatfile the database for conversions and moving from site to site.

# Reporting Bugs:

Bugs happen in any large and complex piece of code.

As for the cases where you do find bugs, we would like you to take the
time to send an e-mail with as much data as possible about the bug. Each
bug report we receive is rated both by its severity (how much pain it
causes) and prioritized according to how beneficial it would be to fix
it relative to the other bugs on the list. Sometimes, this process is
formalized. Sometimes, due to lack of available time, it is ad-hoc.

-   Please double check to be sure it really is a bug and not a new
    feature or design decision. This is important to the process, as it
    may mean that something was missed in the documentation and needs to
    be made clearer. This simple test is that if the server crashes, it
    is a bug and if you cannot tell, consider it one.

-   Try to reproduce and document the sequence of events that will show
    the bug. This will help the dev team find the problem sooner. The
    `log` configuration parameter may be helpful in this process. It is
    more important for the bug reporter to develop a reproducible case
    than it is for the bug reporter to isolate where the bug might be
    located or to develop a workaround or fix for the bug.

-   Send mail about it to: `brazilofmux@gmail.com` If the bug crashes the
    TinyMUX, try to include the following information from running `dbx`
    (or `gdb`) on the resulting core file:

    -   Output of the `where` command.

    -   Output of the `dump` command for each procedure level.

-   To use `gdb` or `dbx`, make sure you are in the `game` directory, and
    type:

    -   `gdb bin/netmux core`

    -   `dbx bin/netmux core`

-   From there, simply type 'help' for help, or 'quit' to exit.

# Environment:

TinyMUX 2.13 should run on most Unixes with BSD-style sockets and a C++
compiler that groks function prototypes. It is 64-bit clean code
supporting both IPv4 and IPv6.
