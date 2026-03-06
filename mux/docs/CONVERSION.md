---
author: Brazil
date: March 2026
title: CONVERSION
---

# Conversions from Other Database Formats

TinyMUX 2.13 has been proven to read the following database formats:

> TinyMUX 2.0, 2.1, 2.2, 2.3, 2.4, 2.6, 2.7, 2.9, 2.10

# Upgrading An Existing TinyMUX

 - `@shutdown` the game.

 - If upgrading from a flatfile-based version, export a flatfile using your
   old version's `db_unload` and move it to a safe location.

 - Compile TinyMUX 2.13 per `INSTALL.md`.  Note that TinyMUX 2.13 untars
   into a separate directory.

 - Copy `.txt` files to `text/`

 - Copy existing `mux.config`

 - **Important:** Your old `GAMENAME.conf` file will not work under
   TinyMUX 2.13 if you are converting from a version prior to TinyMUX 2.0.

   To make a proper file, you must edit the `GAMENAME.conf` in the
   TinyMUX 2.13 `game/` and add in the config parameters.

 - You can save a little time by `cat oldfile >> newfile` and using
   the editor to remove unneeded lines.

 - If upgrading from a flatfile-based version, use `db_load` in the
   `game/data` directory to import your flatfile into SQLite:

       cd game/data
       ./db_load netmux netmux.flat -C comsys.db -m mail.db

   The `-C` and `-m` flags are optional and handle comsys and mail
   flatfiles.

 - Restart the game.

# Database Tools

TinyMUX 2.13 uses SQLite for all game data storage.  The `dbconvert` tool
imports flatfiles into the SQLite database and exports them back out.
The `db_load` and `db_unload` scripts simplify the process.

The syntax of the scripts is:

- `./db_load netmux netmux.flat`

  This imports a flatfile into the SQLite database.

- `./db_unload netmux netmux.flat`

  This exports the SQLite database to a flatfile.

- Both scripts accept optional `-C comsys.db` and `-m mail.db` flags
  to handle comsys and mail flatfiles respectively.
