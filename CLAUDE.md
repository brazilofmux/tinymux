# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands
- Configure (one-time): `cd mux && ./configure --enable-realitylvls --enable-wodrealms`
  - For release builds add `--enable-stubslave`; omit for smoke testing
- **Build everything from repo root**: `make install`
  - Builds libmux.so, netmux, engine.so, all modules, creates game/bin symlinks
  - This is the standard workflow — always build from the repo root
- Clean: `make clean` or `make realclean`
- Run server: `cd mux/game && ./bin/netmux`
- **Do NOT build from mux/src/ directly** — that only builds netmux, not engine.so or modules

## Testing
- Run smoke tests: `make test` (from repo root — builds, installs, then tests)
- Or manually: `cd testcases && ./tools/Makesmoke && ./tools/Smoke`
- Test output in: `testcases/smoke.log`

## Release Process
- Update version numbers in:
  - `dounix.sh` and `dowin32.sh`: Update OLD_BUILD and NEW_BUILD
  - `mux/src/_build.h`: Update MUX_VERSION and MUX_RELEASE_DATE
- Building release packages:
  - Unix/Linux/FreeBSD: Run `./dounix.sh` from repository root
  - Windows: Run `./dowin32.sh` from repository root
- Release artifacts include:
  - Full distribution archives (.tar.gz, .tar.bz2)
  - Patch files for upgrading from previous version (.patch.gz)
  - SHA256 checksums for all distribution files
- Generated files:
  - `mux-2.14.0.x.unix.tar.gz` - Complete distribution
  - `mux-2.14.0.x.unix.tar.bz2` - Same, in bzip2 format
  - `mux-2.14.0.[x-1]-2.14.0.x.unix.patch.gz` - Patch from previous version
  - Each file has a corresponding .sha256 checksum file

## Generated Files — DO NOT EDIT
See [`docs/generated-files.md`](docs/generated-files.md) for the full map of generated files and their sources.
Ragel outputs (`art_scan.cpp`, `ast_scan.cpp`, `color_ops.c`, `muxescape.cpp`) are
made read-only on disk (`chmod a-w`) by their Makefile generation rules. A pre-commit
hook (`hooks/pre-commit`) blocks commits that include generated output without its source.
Edit the `.rl`/`.ac`/`.proto` source and regenerate — never hand-edit the output.

## Code Style Guidelines
- Indentation: 4 spaces, no tabs
- Bracing: Opening braces on same line: `if (condition) {`
- Constants/Macros: UPPERCASE_WITH_UNDERSCORES
- Classes: CamelCase with leading 'C' (e.g., `CHashTable`)
- Member variables: Use `m_` prefix (e.g., `m_pName`)
- Types: Use constant width types (UINT32, INT64)
- Strings: Use UTF8* for UTF-8 encoded strings
- Nullptr: Use `nullptr` instead of NULL
- Error handling: Return bool/codes for success/failure