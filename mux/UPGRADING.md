---
author: Brazil
date: March 2026
title: UPGRADING
---

# Upgrading to TinyMUX 2.13

TinyMUX 2.13 replaces the flatfile and CHashFile (`.dir`/`.pag`) storage
with a single SQLite database.  This guide walks through upgrading from
TinyMUX 2.12 or earlier.  For conversions from other server families
(PennMUSH, RhostMUSH, TinyMUSH), see `docs/CONVERSION.md`.

## Before You Start

You will need three files from your old installation:

| File | Description |
|------|-------------|
| `netmux.db.flat` | Main database flatfile (or your custom `GAMENAME`) |
| `comsys.db` | Channel system data |
| `mail.db` | @mail data |

If your game uses a custom `GAMENAME` (e.g., `mylittleponymux`), substitute
that name wherever `netmux` appears below.

## Step 1 -- Back Up Your Old Game

While the old game is still running:

```
@dump/flatfile
@shutdown
```

Then, from the old `game` directory:

```
./Backup
```

This ensures you have a clean, complete snapshot before proceeding.

## Step 2 -- Export Flatfiles from the Old Version

Use your **old** version's tools to export.  This is critical -- always
export with the same version that wrote the data.

```
cd game/data
./db_unload netmux netmux.db.flat
```

Verify the flatfile is complete:

- The first line should begin with `+X` followed by a version number.
- The last line should be `***END OF DUMP***`.

If the end marker is missing, the file was truncated and should not be used.
Go back and re-export.

Keep `comsys.db` and `mail.db` from the old `game/data` directory as-is.

## Step 3 -- Build TinyMUX 2.13

TinyMUX 2.13 untars into its own directory, so your old installation is
not disturbed.

```
tar xzf mux-2.13.0.x.unix.tar.gz
cd mux/src
./configure
make
make install
```

The `make install` step is new in 2.13 -- it creates symlinks in `game/bin/`
that the startup scripts expect.

Optional `configure` flags:

- `--enable-realitylvls` -- Reality levels (WoD realms, etc.)
- `--enable-wodrealms` -- WoD-specific realm extensions

## Step 4 -- Prepare the New Game Directory

Copy your customized files into the new tree:

```
cp /old/game/text/*.txt  game/text/
cp /old/game/mux.config  game/
```

### Update `netmux.conf` (or `GAMENAME.conf`)

Review your configuration file against the new `game/netmux.conf`.  The
following parameters have been removed and should be deleted if present:

- `have_comsys`
- `have_mailer`

The following parameters still work but have changed behavior:

| Parameter | What Changed |
|-----------|--------------|
| `input_database` | Now also determines the SQLite filename (`netmux.db` becomes `netmux.sqlite`) |
| `output_database` | Used only for `@dump/flatfile` exports |
| `crash_database` | Used only for panic dumps |
| `fork_dump` | Affects `@dump/flatfile` only; periodic saves (WAL checkpoints) never fork |
| `comsys_database` | Used during dbconvert import/export; normal operation stores data in SQLite |
| `mail_database` | Used during dbconvert import/export; normal operation stores data in SQLite |

If your old conf set `input_database` to a custom name, the same name will
carry over -- SQLite simply derives `GAMENAME.sqlite` from it.

### Update `mux.config` (optional)

If you customized `mux.config` (e.g., changed `GAMENAME`), copy it over and
review.  No parameters have been removed from this file.

## Step 5 -- Import into SQLite

Place the three old files in the new `game/data` directory, then import:

```
cd game/data
./db_load netmux netmux.db.flat -C comsys.db -m mail.db
```

This creates `netmux.sqlite` containing all game objects, attributes,
channels, and @mail.

The `-C` and `-m` flags handle comsys and mail respectively.  If you only
have the main flatfile, you can omit them:

```
./db_load netmux netmux.db.flat
```

Channel and mail data will then need to be loaded from the running game's
flatfiles on first startup (the server falls back to reading `comsys.db`
and `mail.db` if they exist in `data/`).

## Step 6 -- Verify the Import

Spot-check that data landed correctly:

```
sqlite3 netmux.sqlite "SELECT count(*) FROM objects;"
sqlite3 netmux.sqlite "SELECT count(*) FROM channels;"
sqlite3 netmux.sqlite "SELECT count(*) FROM mail_headers;"
```

Compare the object count against your old `db_check` output or the `+X`
header in the flatfile (the number after the version is `db_top`).

You can also export back to a flatfile and diff:

```
./db_unload netmux netmux_verify.flat -C comsys_verify.db -m mail_verify.db
```

## Step 7 -- Start the Game

```
cd game
./Startmux
```

Connect as `Wizard` and verify:

- `@list channels` -- channels are present
- `@mail` -- mail is intact
- Walk around and spot-check objects, attributes, and exits

## Ongoing Differences

Once running on 2.13, the day-to-day experience is mostly the same with
a few changes:

- **`@dump`** performs a WAL checkpoint (fast, no fork).  It does not write
  a flatfile.
- **`@dump/flatfile`** writes a traditional flatfile export.  This is the
  equivalent of the old `@dump` behavior.
- **`./Backup`** uses SQLite's `.backup` command to create a compressed,
  timestamped copy of the database.
- **`.dir`/`.pag` files** are no longer created or used.
- **`make install`** must be run after rebuilding to update the symlinks.

## Troubleshooting

**dbconvert fails with "cannot open input file"**
:   Check that `netmux.db.flat` is in the `data/` directory and the filename
    matches exactly.  The `-i` flag expects a path relative to `data/`.

**Object count is zero after import**
:   Verify the flatfile has the `***END OF DUMP***` marker.  A truncated
    file imports zero objects silently.

**Channels or mail missing after startup**
:   If you omitted `-C` or `-m` during import, ensure `comsys.db` and
    `mail.db` are present in `data/` -- the server will load them as a
    fallback on first startup.

**"make install" was skipped**
:   The game will fail to start because `game/bin/netmux` won't exist.
    Run `make install` from `src/`.

## See Also

- `INSTALL.md` -- Building from source
- `NOTES.md` -- Database tools and backup procedures
- `docs/CONFIGURATION.md` -- Configuration parameter reference
- `docs/CONVERSION.md` -- Converting from other server families
- `docs/BACKUPS.md` -- Backup procedures
