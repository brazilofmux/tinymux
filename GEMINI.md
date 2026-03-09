# TinyMUX 2.14 - Project Context

TinyMUX 2.14 is a high-performance MUSH/MUD server written in C++17. It is a continuation of the TinyMUX 1.x line, modernized with SQLite storage, SSL support (via OpenSSL), and advanced networking (GANL with epoll/kqueue).

## Project Overview

- **Language:** C++17 (with some C for bundled libraries like SQLite).
- **Architecture:** Client-server MUD engine.
- **Storage:** SQLite (bundled). Flatfiles can be imported/exported via `dbconvert`.
- **Networking:** GANL (Global Adaptive Network Layer) supporting IPv4/IPv6, SSL, and high-concurrency via `epoll` (Linux) or `kqueue` (BSD/macOS).
- **Platforms:** Linux (Debian, Fedora, Rocky), FreeBSD, macOS, and Windows.

## Key Directories

- `mux/src/`: Core server source code and build configuration (`configure.ac`, `Makefile.am`).
- `mux/game/`: Runtime environment. Contains `bin/` (executables), `data/` (databases), `text/` (help/connect files), and `logs/`.
- `testcases/`: Smoke test suite (`.mux` scripts) and automation tools in `testcases/tools/`.
- `parser/`: Research tools for tokenizing, parsing, and evaluating MUSH code.
- `db/`: Unit tests for the SQLite backend and storage layers.
- `docs/`: Extensive design notes, manuals, and configuration guides.

## Building and Running

### Build Prerequisites
Install development libraries for OpenSSL and PCRE2.
- **Debian/Ubuntu:** `sudo apt install build-essential libssl-dev libpcre2-dev pkg-config sqlite3`
- **RHEL/Fedora:** `sudo dnf install gcc-c++ openssl-devel pcre2-devel pkgconf-pkg-config make sqlite`

### Build Commands
The build system uses autoconf/automake. The entry point is `mux/src/`.

```bash
cd mux/src
./configure --enable-realitylvls --enable-wodrealms --enable-stubslave
make -j$(nproc)
make install  # Required to create symlinks in ../game/bin
```

### Running the Server
```bash
cd mux/game
./Startmux
```
To stop the server, use `@shutdown` from within the game or `kill` the process ID found in `netmux.pid`.

## Testing

### Smoke Tests
End-to-end behavioral tests using `.mux` scripts.
```bash
cd testcases/tools
./Makesmoke
./Smoke
```
Results are logged to `testcases/smoke.log`.

### Backend Tests
```bash
cd db
make test
```

### Parser Research Tools
```bash
cd parser
make
./eval --ast "1+2"
```

## Development Conventions

- **Code Style:** 4 spaces, no tabs.
- **Naming:** 
    - Classes: `CCamelCase` (e.g., `CSQLiteDB`).
    - Member variables: `m_` prefix (e.g., `m_db`).
    - Macros/Constants: `UPPER_CASE_WITH_UNDERSCORES`.
- **Types:** Use `nullptr` instead of `NULL`. Use constant-width types (`UINT32`, `INT64`) where appropriate.
- **Strings:** Prefer UTF-8 (using `UTF8*` types where defined).
- **Bracing:** While historical code may vary, preferred style for new code is opening braces on the same line: `if (condition) {`.
- **Self-Checking:** For intensive debugging, use `./configure --enable-selfcheck`.

## Database Management
- `dbconvert` (symlinked to `netmux`) is the primary tool for data migration.
- Use `game/data/db_load` to import flatfiles into SQLite.
- Use `game/data/db_unload` to export SQLite to flatfiles.
