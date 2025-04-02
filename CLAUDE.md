# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands
- Configure: `./configure [options]` (options: --enable-memorybased, --enable-realitylvls, etc.)
- Build: `make depend && make`
- Clean: `make clean` or `make realclean`
- Run server: `cd mux/game && ./bin/netmux` (starts the MUD server)

## Testing
- Run smoke tests: `cd testcases/tools && ./Makesmoke && ./Smoke`
- Test output in: `testcases/smoke.log`

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