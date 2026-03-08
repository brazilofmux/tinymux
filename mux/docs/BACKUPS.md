---
author: Brazil
date: March 2026
title: BACKUPS
---

# The Importance of Good Backups

Experienced TinyMUX users will tell you that having a good, recent backup of
your database is the best defense against data loss.  In a perfect world, we
wouldn't have to deal with random accidents or malicious attacks that wipe
out data, but there are an unfortunate number of cases where these events
occur and entire games have disappeared forever because of it.

# Making a Backup

TinyMUX 2.14 stores all game data (objects, attributes, comsys, and mail)
in a single SQLite database file.  The included `Backup` script, located in
the `game/` directory, uses the SQLite `.backup` command to create a
timestamped, compressed copy of the database.

To use the Backup script:

1. `@shutdown` the game.
2. `cd` to the `game` directory.
3. Run `./Backup`
4. Restart the game using `./Startmux`

The script produces a file named `GAMENAME.MMDD-HHMM.sqlite.gz` in the
`game/data` directory.

For portability or migration to another version, you can also export a
flatfile using `db_unload` in the `game/data` directory.  See `NOTES.md` for
details on `db_load` and `db_unload`.

# Storing Your Backups

Depending on how much disk space your provider allows you, we recommend
that you keep a number of the most recent backups on the machine for ready
access.  Having to wait while someone ftps the files back to the machine
is a tedious process for the person who feels any pressure to restore the
game.  Be kind to your site admin---give them something to work with.

No matter how reliable or redundantly backups are performed on the machine
that your game runs on, it is critical that you also store copies of your
most recent backups at an off-site location.  Again, we cannot stress this
enough.
