# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands
- Configure (one-time): `cd mux && ./configure --enable-jit --enable-realitylvls --enable-wodrealms`
  - `--enable-jit` defaults to NO. Without it the JIT is never built, and the
    `jit_parity`/`jit_diff` tests silently exercise only the interpreter.
  - For release builds add `--enable-stubslave`; omit for smoke testing
  - **No `CPPFLAGS`/`LDFLAGS` needed on any platform** for this line. PCRE2 and
    OpenSSL are found via `pkg-config`. The one exception is `--enable-nls` on
    macOS — gettext ships no `.pc` file, so add
    `CPPFLAGS="-I$(brew --prefix)/include" LDFLAGS="-L$(brew --prefix)/lib"`.
- **Build everything from repo root**: `make install`
  - Builds libmux.so, netmux, engine.so, all modules, creates game/bin symlinks
  - This is the standard workflow — always build from the repo root
- Clean: `make clean` or `make realclean`
- Run server: `cd mux/game && ./bin/netmux`
- **Do NOT build from mux/src/ directly** — that only builds netmux, not engine.so or modules
- **Always `make clean` after changing configure flags.** A partially-built
  tree (stale `muxscript`, fresh `engine.so`) produces failures that describe
  nothing real — ASan's "Interceptors are not working" is the usual tell.
- **Header edits can under-rebuild the same way (#2118).** Engine/libmux use
  `-MMD` side-car `.d` files for header deps. An object without its `.d` is
  invisible to make: a layout or struct-size header change then rebuilds only
  some TUs and the binary lies coherently (that was the whole of #2107). The
  Makefiles drop orphan objects automatically; if another host is green on the
  same commit, treat that as a build-hygiene signal first.

Platform prerequisites (Debian/Ubuntu and Homebrew package lists), autotools
version constraints for regenerating `configure`, and a symptom→cause table
live in [`docs/building.md`](docs/building.md). Read it before running
`autoconf`/`automake`: `configure` comes from autoconf **2.73** and the
`Makefile.in` files from automake **1.16.5**, and regenerating with a different
version churns whole files (#1477). Prefer expressing build changes in
`configure.ac` over `Makefile.am` for exactly that reason.

## Testing
- Run smoke tests: `make test` (from repo root — builds, installs, then tests)
- Or manually: `cd testcases && ./tools/Makesmoke && ./tools/Smoke`
- Test output in: `testcases/smoke.log`
- **A green run only covers what the tree was configured with** (#1946).
  `make test` prints a build-configuration banner first and repeats it in the
  summary, because most optional features default to NO and their tests skip
  cleanly when absent — so "29 passed" means different things on different
  boxes. `make test-buildconfig` prints it alone in about a second.
  - The banner aborts the run if the tree was reconfigured without a rebuild:
    the binaries would not be the configuration the banner names.
  - `make test EXPECT_CONFIG="jit=yes"` makes a box assert the job it exists
    to do, so a machine meant to cover the JIT fails loudly on a non-JIT
    build instead of skipping politely. Known keys: `jit stubslave nls
    realitylvls wodrealms`.
- GANL engine regression harness: `make test-ganl` (also part of `make test`);
  scripted engine scenarios in `mux/ganl/tests/`, TAP output
  - On Windows (no make): build `mux/ganl/tests/ganl_tests.vcxproj` with MSBuild
    and run `mux/bin_release/ganl_tests.exe` — covers the wselect + iocp engines
    via the accept path (nonzero exit on failure)
- netaddr subnet unit tests: `make test-netaddr` (also part of `make test`);
  `mux_subnet::compare_to` + `parse_subnet` in `tests/netaddr/` (#799/#800)
- SQLite storage backend: `make test-db` (also part of `make test`);
  `tests/db/` drives `sqlitedb.cpp` and `sqlite_backend.cpp` through
  `storage_backend.h` — the persistence layer's only coverage outside smoke.
  **`test-db` is not `test-dbt`.** The latter is the RV64 DBT suite and
  contains the former's name as a substring, which is why `tests/db` sat
  unwired long enough to need its own issue (#1953). Cold build ~23s
  (it compiles `sqlite3.c` itself); warm ~1s.
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
- Algorithmic growth: `make test-growth` (opt-in, NOT in `make test`);
  `tests/growth/` asserts the **complexity class** of evaluation, not its
  speed. Doubling N costs 2.0x if an implementation is linear and 4.0x if it is
  quadratic **on every machine** — the hardware cancels out of the ratio — so
  unlike `test-perf` this needs no per-machine baseline and no calibrated
  tolerance. It cannot see a 10% regression; it can see O(n) become O(n²).
  - The verdict is a least-squares exponent over all points, not consecutive
    ratios: one cold-cache measurement must not decide it.
  - Known defects are `xfail`'d against an issue number in `driver.py`'s
    `CASES`. An xfail that starts **passing** fails the run — a stale xfail
    list is how a fixed bug gets un-fixed later.
  - **Two benchmark fields lie and are deliberately unused.** `astbench`'s
    `jit=` times `jit_eval` even when it bails instantly for want of a lowering
    (`citer()` reads a flat 2.7us at every N — that is absence, not speed), and
    its `result=` comes from a third AST call so it never notices. `rvbench`'s
    `native=` calls `mux_exec`, which *dispatches to the JIT* for anything
    JIT-eligible — reading it as "the interpreter" is what made #2052 look like
    a defect shared by both routes when only one route has it. Use `astbench`'s
    `ast=` for the interpreter and `rvbench`'s `cached=` for the JIT.

## Release Process

**What a 2.14.0.x build is.** 2.14 is ALPHA and stays ALPHA. The released
version — what live games and players actually run — is **2.13**, out March
2026. A 2.14.0.x tag is a checkpoint in a long alpha, not something anyone
is asked to deploy. **There is no planned Beta stage**, so do not propose
clearing `ALPHA` in `_build.h`, and do not write CHANGES entries or issue
comments that address a 2.14 site operator as if one exists.

Historically the minor number encoded the year the work finished: 2.0 in
2000, 2.1 in 2001, and so on through roughly 2.9, after which the scheme
lapsed. 2.14 is loosely aimed at finishing around March 2027, or possibly
December 2026 if it is worth re-syncing to a year boundary — flexible, and
not a commitment. Soak time, not feature count, is the gating factor.

- **Before tagging, `make test` must pass under more than one configuration**
  (#1946). The default test build omits `--enable-stubslave`, which is a
  *release* build flag — so the shipped stubslave path is the one the test
  build never exercises, and #1939's crash lived exactly there. At minimum:
  - `--enable-jit ... --enable-stubslave` → `make test EXPECT_CONFIG="jit=yes stubslave=yes"`
  - a build with neither, to confirm the guards skip rather than fail
  - `make clean` between them; configure flags do not take effect otherwise
- Update version numbers in:
  - `dounix.sh` and `dowin32.sh`: Update OLD_BUILD and NEW_BUILD
  - `mux/include/_build.h`: Update MUX_VERSION and MUX_RELEASE_DATE.
    Leave `ALPHA` defined (see above); it is what `netmux -v` prints.
- **`git status` must be clean before packaging.** `dounix.sh` copies from
  the working tree (`cp "$CHANGES_DIR/$file"`), not from a git export, so
  anything dirty ships — into the tarball *and* into the generated
  `.patch.gz`. The Ragel outputs are the ones to watch: regenerating any of
  them rewrites `#line` directives in a toolchain-dependent way (#1950), so
  a diff of ~81 `#line`-only lines in `mux/lib/color_ops.c` is churn, not
  content. Restore before packaging:
  `chmod u+w <file> && git checkout -- <file> && chmod a-w <file>`.
  `make clean` no longer triggers this — #1959 took `color_ops.c` out of
  `CLEANFILES`, so it is no longer deleted and regenerated on every rebuild.
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

## String Encoding — Validate at the Boundary, Not Internally
Incoming bytes are validated at the **network boundary** for their encoding
(ascii, latin-1, latin-2, cp437, utf-8 Unicode 16), and Unicode is additionally
**required to be NFC** — `save_command` (`mux/src/net.cpp`) checks `utf8_is_nfc`
and normalises before queueing. From there the server maintains NFC through the
database and back out to the network.

**So internal code must not re-validate.** A string reaching a function inside
the server is already well-formed NFC. The exceptions are local and temporary —
a function that decomposes and recomposes must mind its own intermediate state.
If the invariant is worth testing, test it in a sanitising/debug build, not on
release code paths.

Two consequences that are easy to get backwards:
- **"Preserves existing behaviour" is not automatically safer.** Behaviour
  preserved on input the boundary excludes is cost, not safety. A refactor whose
  results change *only* for malformed input is not a regression.
- **Do not write tests that pin out-of-contract behaviour.** A fuzz battery
  emitting bare PUA lead bytes (`0xEF`/`0xF3` without their continuations) is
  generating input that cannot arrive, and freezing whatever the code happens to
  do with it — which makes legitimate refactors read as regressions. Generate
  complete sequences. This is not hypothetical: it cost a full cycle in #2002.

Worth stating because the failure mode is contagious. Defensive validation and
behaviour-pinning tests both *look* like diligence, so the next session copies
them, and the pattern spreads without anyone deciding to adopt it.

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