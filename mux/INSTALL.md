---
author: Brazil
date: March 2026
title: INSTALL
---

Please note that there are two sets of instructions included in this
file. Please skip to _Instructions for Existing Games_ for how to
upgrade your server or to compile in preparation for moving an existing
game.

# Prerequisites:

TinyMUX requires a C++17 compiler and the following development
libraries. Install them before running `./configure`.

**Debian / Ubuntu:**
```
sudo apt install build-essential libssl-dev libpcre2-dev pkg-config sqlite3
```

**Fedora / RHEL / Rocky:**
```
sudo dnf install gcc-c++ openssl-devel pcre2-devel pkgconf-pkg-config make sqlite
```

**FreeBSD:**
```
pkg install gmake pcre2 openssl pkgconf sqlite3
```

**macOS (Homebrew):**
```
brew install openssl pcre2 pkg-config sqlite3
```

The `sqlite3` CLI is used by the `Backup` script and is useful for
inspecting the database. The SQLite library itself is bundled with
TinyMUX and does not need to be installed separately.

If `./configure` fails, the error message will usually indicate which
library or tool is missing.

# Instructions for New Installations:

1. `cd src/` to the source directory. Run `./configure`.

    This will customize `autoconf.h` and `Makefile` for your system.

    SSL support is always enabled; OpenSSL is a required dependency.
    GANL is the networking implementation.

    Optional packages are documented separately and enabled with the
    following configuration options:

      |                        |                                                     |
      |------------------------|-----------------------------------------------------|
      | `--enable-realitylvls` | See `REALITY.md` and `REALITY.SETUP.md`.            |
      | `--enable-stubslave`   | See `MODULES.md`.                                   |
      | `--enable-wodrealms`   | See `docs/REALMS.md`.                               |
      | `--enable-inlinesql`   | Enables in-line MySQL support.                      |
      | `--enable-deprecated`  | Enables deprecated features.                        |

2. Run `make`. This will produce `netmux`, `slave`, and other executables.

3. Run `make install` to create the necessary symlinks in the `game/bin`
    directory. This step is required. After installation, the `dbconvert`
    command will be a symlink to `netmux`.

4. When starting from a TinyMUX from scratch, do the following:

      - cd to the game directory. `cd ../game`
      - Make your configuration file, as described in `docs/CONFIGURATION.md`
      - Type `./Startmux`. TinyMUX 2.13 automatically creates a minimal DB
        if one does not exist in the `game/data` directory.
      - Log into the game as player wizard `connect wizard potrzebie` and
        shut it down again.

5. Edit the .txt files in `game/text` to your liking. In particular,
    `connect.txt` and `motd.txt`.

6. Start TinyMUX 2.13 by running `./Startmux` again.

7. `@ccreate` a channel named `Public`, and a channel named `Guests`
    from within the TinyMUX. Created players will automatically be
    joined to `Public` with alias `pub`, guests will automatically join
    `Guests` with alias `g`.

# Database Tools:

 - `dbconvert` imports flatfiles into the SQLite database and exports
   them back out. The `db_load` and `db_unload` scripts in `game/data`
   simplify the process.

 - To import a flatfile into SQLite:

```
       ./db_load netmux netmux.flat
       ./db_load netmux netmux.flat -C comsys.db -m mail.db
```

 - To export a flatfile from SQLite:

```
       ./db_unload netmux netmux.flat
       ./db_unload netmux netmux.flat -C comsys.db -m mail.db
```

 - The `-C` and `-m` flags are optional and handle comsys and mail
   flatfiles respectively. If omitted, comsys and mail data remain
   in SQLite only.

# Instructions for Existing Games:

NOTE: It is HIGHLY recommended that you preserve an earlier setup if you
can, to make conversion a bit less painful. If you had one while
converting, make sure the conversion process has completed successfully
before you delete your old distribution. We cannot stress enough to you
the importance of protecting your data throughout any conversion or
upgrade.

1. `cd src/` to the source directory. Run `./configure`.

    This will customize `autoconf.h` and `Makefile` for your system.
    See the new installation instructions above for configure options.

2. Run `make`. This will produce `netmux`, `slave`, and other executables.

3. Run `make install` to create the necessary symlinks in the `game/bin`
    directory. This step is required.

4. Place/change your files.

    - Put text files in `game/text`.

    - If upgrading from a flatfile-based version, use `db_load` in the
      `game/data` directory to import your flatfile into SQLite:

          cd game/data
          ./db_load netmux netmux.flat -C comsys.db -m mail.db

    - If you changed the `GAMENAME` in `mux.config`, be sure to change the
      filenames in `GAMENAME.conf` as well.

    - If you had a mail database previously, adjust `mail_expiration`
      accordingly, BEFORE you restart the game, or else ALL `@mail` older
      than the default value of 14 days will be deleted.

5. Start TinyMUX 2.13 by running `./Startmux`.

