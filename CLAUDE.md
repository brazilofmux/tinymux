# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands
- Configure: `./configure [options]` (options: --enable-memorybased, --enable-realitylvls, etc.)
- Standard configuration: `./configure --enable-realitylvls --enable-wodrealms --enable-stubslave --enable-ssl`
- Generate dependencies: `make depend` (important after code changes)
- Build: `make`
- Clean: `make clean` or `make realclean`
- Run server: `cd mux/game && ./bin/netmux` (starts the MUD server)

## Updating Dependencies
- When making significant code changes, update dependencies:
  1. Run `./configure` with appropriate options
  2. Run `make depend` to regenerate .depend
  3. Run `make` to verify build succeeds
  4. Commit updated .depend file

## Testing
- Run smoke tests: `cd testcases/tools && ./Makesmoke && ./Smoke`
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
  - `mux-2.13.0.x.unix.tar.gz` - Complete distribution
  - `mux-2.13.0.x.unix.tar.bz2` - Same, in bzip2 format
  - `mux-2.13.0.[x-1]-2.13.0.x.unix.patch.gz` - Patch from previous version
  - Each file has a corresponding .sha256 checksum file

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