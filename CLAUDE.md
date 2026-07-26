# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands
- Configure (one-time): `cd mux && ./configure --enable-jit --enable-realitylvls --enable-wodrealms`
  - `--enable-jit` defaults to NO. Without it the JIT is never built, and the
    `jit_parity`/`jit_diff` tests silently exercise only the interpreter.
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
- GANL engine regression harness: `make test-ganl` (also part of `make test`);
  scripted engine scenarios in `mux/ganl/tests/`, TAP output
  - On Windows (no make): build `mux/ganl/tests/ganl_tests.vcxproj` with MSBuild
    and run `mux/bin_release/ganl_tests.exe` — covers the wselect + iocp engines
    via the accept path (nonzero exit on failure)
- netaddr subnet unit tests: `make test-netaddr` (also part of `make test`);
  `mux_subnet::compare_to` + `parse_subnet` in `tests/netaddr/` (#799/#800)
- DBT and RV64 tests: `make test-dbt` (also part of `make test`); `tests/dbt/`
  builds five binaries, each needing a different link:
  - **chain** — `dbt_backend_decode_jmp_target` must invert
    `dbt_backend_backpatch_jmp` (#1152). Compiles **all three** backends into
    one binary via `-D` symbol renames, on every host: #1152 survived because
    nothing exercised the affected backend, so host-only coverage is not enough.
  - **cache** — `dbt_cache_insert`/`dbt_cache_lookup` dedupe and FIFO eviction (#1153).
  - **interp** — guest-memory bounds on the interpreter route (#1292), plus a
    qemu-derived FCVT conformance table (#1319). `#include`s `dbt_interp.cpp`
    to reach file-static `mem_check`, so it cannot share a link with `exec`.
  - **exec** — `mux/modules/engine/dbt_test.cpp`, which had never been wired into
    any build. Hand-assembled RV64 through the interpreter and (for ~6 of 39
    test functions) the DBT, plus a cross-compiled ELF through both routes —
    the ELF leg carries most of the block-translation coverage. Builds the
    **host** backend only, since it executes; skips loudly off x86_64/aarch64.
  - **fuzz** — instruction-level differential fuzzer: random RV64 sequences
    through the interpreter and the DBT, comparing all 32 integer and 32 FP
    registers, with delta-debug shrinking of any mismatch. `jit_diff` fuzzes
    the softcode layer; this one fuzzes the layer below it, where #1147,
    #1148, #1151, #1152, #1153, #1311, #1313 and #1320 all lived.
    Deterministic (`DBT_FUZZ_SEED`, `DBT_FUZZ_ITERS`); soak with
    `DBT_FUZZ_ITERS=20000 make -C tests/dbt fuzz`.
- **A differential test cannot see a bug both routes share.** #1319 and #1320
  were exactly that — interpreter and DBT both truncated FCVT, so the fuzzer
  called it clean. Anything suspected of being a shared misreading of the
  spec needs an external oracle; this box has `riscv64-unknown-elf-gcc` and
  `qemu-riscv64-static`, and `tests/dbt/test_interp.cpp` carries golden values
  taken that way rather than from either implementation here.
- Wildcard-capture scenario: `make test-scenario` (opt-in, NOT in `make test`);
  spins a throwaway netmux and drives `$`-command `%0..%9` captures over a
  socket (`tests/scenario/`) — the path muxscript can't reach

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