---
author: Brazil
date: March 2026
title: NOTES
---

This is a list of information that never seems to be handy enough when you
need it.

# Handy Stuff:

 - The default password for `Wizard(#1)` is `potrzebie`.

 - For new games, change the password for `#1` first thing and *write it down*
   in a safe place offline.

 - The default port has been changed to `2860`. Change this in the
   `netmux.conf` or `GAMENAME.conf` file.

 - Always be sure to ftp files in binary mode. Otherwise, you can expect
   your data to be unusable.

 - Default `GAMENAME` will be `netmux`. If you changed the `GAMENAME` in
   `mux.config`, be sure to change the filenames in `GAMENAME.conf` as well,
   otherwise, TinyMUX and (perhaps more importantly), `./Backup`, won't be
   able to find your DB.

 - Read `wizhelp config parameters` when you get the TinyMUX started. If you
   are porting from PennMUSH, for example, where the master room is set
   as `#2`, you would have to place the config parameter `master_room #2` in
   your configuration file.

 - If you had a mail database previously, remember to adjust
   `mail_expiration` accordingly, or else all `@mail` older than the default
   value of 14 days will be deleted.

# Database Tools:

`dbconvert` imports flatfiles into the SQLite database and exports them
back out. The `db_load` and `db_unload` scripts in `game/data` simplify
the process.

To import a flatfile into SQLite:
```
    ./db_load netmux netmux.flat
    ./db_load netmux netmux.flat -C comsys.db -m mail.db
```

To export a flatfile from SQLite:
```
    ./db_unload netmux netmux.flat
    ./db_unload netmux netmux.flat -C comsys.db -m mail.db
```

The `-C` and `-m` flags are optional and handle comsys and mail flatfiles
respectively. If omitted, comsys and mail data remain in SQLite only.

# On Flatfiles:

Flatfiles are a portable text format for moving game data between servers
or versions. When migrating to a new machine or upgrading, use `db_unload`
to export a flatfile and `db_load` to import it on the other end.

 - Always use your original `db_unload` or `dbconvert` to export. *We cannot
   stress this enough.* Data loss is possible, especially since `dbconvert`
   sometimes changes between releases.

 - You can verify a flatfile by checking for `***END OF DUMP***` at the end
   and attribute values throughout. If the end marker is missing, the file
   was truncated.

# On Making Backups:

TinyMUX 2.14 includes a `Backup` script in the `game` directory. It uses
the SQLite `.backup` command to create a timestamped, compressed copy of
the database:

 - `@shutdown` the game.

 - `cd` to the `game` directory.

 - Type `./Backup`

 - Restart the game using `./Startmux`

 - Move the backup to onsite and offsite storage locations.

# On Tools:

 - `announce.c` sits and listens on a specified port. Whenever anyone
   connects, it announces a message and disconnects them.

 - You should create a message and call the file `message_file`. This will
   serve as your announcement to people trying to connect.

 - Compile `announce.c` by typing `gcc -o announce announce.c`. If you are
   not using the GNU compiler, substitute the C compiler in use on your
   system.

 - Type `announce port < message_file` and the port announcer will take
   over from there.
