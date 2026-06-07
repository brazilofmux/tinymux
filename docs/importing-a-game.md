# Importing an existing game / migrating to the SQLite backend

TinyMUX now stores the live game in a **SQLite database** rather than reading
the flatfile directly on every boot. This changes how you import an existing
game, and it is the source of a common "my old passwords don't work" symptom
when migrating from an older build. This page explains the model and the steps.

## The two files in `game/data/`

| File | What it is | Role |
|------|------------|------|
| `netmux.db` | The **flatfile** (human-readable text, starts with `+X...`) | Seed / export format only |
| `netmux.sqlite` | The **live database** the server actually runs from | Authoritative |

The SQLite filename is derived from your configured `input_database`
(in `netmux.conf`) by replacing the `.db` suffix with `.sqlite`. So
`input_database data/netmux.db` means the server reads and writes
`data/netmux.sqlite`.

## Boot precedence (read this twice)

> **If `data/netmux.sqlite` exists and contains a game, the server warm-starts
> from it and the `netmux.db` flatfile is never read.**

On a brand-new install, the first boot builds `netmux.sqlite` from the seed
flatfile `netmux.db`. After that, the flatfile is ignored. This is why simply
dropping your old game in as `netmux.db` does nothing once a `.sqlite` exists —
you keep logging into whatever is already in the SQLite database. The startup
log notes this with a "Warm-started from SQLite database; the flatfile ... was
not consulted" line.

## Importing your old flatfile

You have two equivalent options. Both assume the server is **stopped**.

### Option A — let the server rebuild from a flatfile

```sh
cd game
rm -f data/netmux.sqlite          # remove the stale/seed database
cp /path/to/your_old_game.flat data/netmux.db
./bin/netmux                      # first boot rebuilds netmux.sqlite from it
```

### Option B — load the flatfile explicitly with db_load

```sh
cd game/data
./db_load netmux /path/to/your_old_game.flat
cd ..
./bin/netmux
```

`db_load` always writes the `.sqlite` into the game's `data/` directory (next to
the script), regardless of which directory you run it from, so it can no longer
land somewhere the server doesn't read. It prints the exact file it loaded into.

If `data/netmux.sqlite` already exists, `db_load` refuses to overwrite it and
tells you the file path. Either remove that file first, or force the replacement:

```sh
./db_load -f netmux /path/to/your_old_game.flat
```

To bring across comsys and mail as well:

```sh
./db_load netmux your_old_game.flat -C comsys.db -m mail.db
```

## Exporting back to a flatfile

```sh
cd game/data
./db_unload netmux backup.flat            # writes backup.flat in this directory
./db_unload netmux backup.flat -C comsys.db -m mail.db
```

## Passwords

Password hashes (`$SHA1$...`, `$1$...`, etc.) are carried through import
unchanged, so old passwords continue to work once you are logging into the
correct database. If old passwords appear to fail, the cause is almost always
that the server is reading a different `netmux.sqlite` than the one your game
was imported into — see the boot precedence above.
