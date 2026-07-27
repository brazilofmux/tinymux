# Top-level convenience Makefile.
# Delegates to the autotools build system in mux/.
#
# Usage:
#   make              — build everything (libmux, netmux, engine, modules)
#   make install      — build + create symlinks in mux/game/bin
#   make clean        — clean all build artifacts
#   make test         — run smoke tests (build + install first)
#   make hooks        — install git hooks (done automatically on first build)

# Keep test-lua-jit (added on master after this branch was cut) alongside
# the new dual-route smoke targets.
.PHONY: all install clean realclean test test-ios test-ganl test-netaddr test-format test-dbt test-alarm test-smoke test-smoke-ast test-scenario test-parity213 test-stress test-jit-qreg test-jit-ifelse test-lua-jit test-lua-ecall test-vacuous test-narrowing test-config test-nls test-nls-runtime test-asan hooks

# Install git hooks on first build so all developers get protection
# against accidentally editing generated files.
hooks:
	@if git rev-parse --git-dir >/dev/null 2>&1 && [ -d hooks ]; then \
	    git config core.hooksPath hooks; \
	    echo "Git hooks installed (core.hooksPath = hooks)"; \
	fi

all: hooks
	$(MAKE) -C mux

install: all
	$(MAKE) -C mux install

clean:
	$(MAKE) -C mux clean
	$(MAKE) -C testcases/tools clean
	$(MAKE) -C mux/ganl/tests clean
	$(MAKE) -C tests/dbt clean

realclean:
	$(MAKE) -C mux distclean

test: install test-ganl test-netaddr test-format test-nls test-nls-runtime test-vacuous test-narrowing test-config test-dbt test-alarm test-jit-qreg test-jit-ifelse test-lua-ecall test-ios test-smoke test-smoke-ast

# Smoke on the compiled route (jit_eval_brackets defaults on).
test-smoke:
	$(MAKE) -C testcases/tools
	@echo "==> Smoke: compiled route (jit_eval_brackets default)"
	cd testcases && ./tools/Makesmoke && ./tools/Smoke

# Smoke again on the interpreted route (#1243).
#
# The default run cannot fail on an AST-route-only defect: with
# jit_eval_brackets on, bracketed expressions are compiled, so a bug that
# exists only in the interpreter never executes.  Three fixes in one day
# (#1214, #1238, #1246) passed `make test` with the fix reverted and were
# caught only by this second pass -- testcases/disarm_fn.mux carries a
# ROUTE: header saying so, because the file cannot defend itself under the
# default configuration.
#
# The interpreted route is not legacy: it handles every expression the JIT
# declines, so it runs in production on every server.
#
# Whole corpus rather than a route-sensitive subset -- a second pass costs
# ~30s against a ~30s first pass, which is not worth the bookkeeping of
# deciding which files are route-sensitive (and getting that wrong
# silently loses coverage).
#
# Depends on test-smoke so smoke.flat is already built and the two passes
# report in a fixed order.
test-smoke-ast: test-smoke
	@echo "==> Smoke: interpreted route (jit_eval_brackets 0)"
	cd testcases && SMOKE_EXTRA_CONF="jit_eval_brackets 0" ./tools/Smoke

# Static guard: no smoke case may be incapable of failing (#1434 family).
#
# A tr.tc* label with a Succeeded branch and no non-success branch reports the
# same verdict every run -- it counts toward the total and cannot go red.  Eight
# separate findings in one day were that exact shape (#1413, #1426, #1434,
# #1438, #1460/#1498, #1495), each found by a person noticing rather than by a
# check.  Same move as the format guard: turn a sweep someone remembers to run
# into something the build does.
#
# Source-only, so it needs no build and runs before the suites.
test-vacuous:
	@echo "==> Checking for smoke cases that cannot fail"
	cd testcases && python3 tools/check_vacuous.py

# Runtime oracle for the xx pseudo-locale (#1523).
#
# test-nls above is the static half -- markings, catalogue coverage, .pot
# freshness -- and that is where the risk that has actually bitten lives.  This
# is the half that shows translation happens at all: nothing else in the suite
# observes notify() prose, so the whole translatable surface is otherwise
# invisible to it.
#
# The point is the pairing, not the LANGUAGE=xx run on its own.  A smoke run
# with LANGUAGE=xx is green whether the catalogue is correct, corrupt or absent
# -- measured on #1523 with the catalogue in a directory the server never opens
# -- so it is only evidence if something also asserts that removing the
# catalogue takes the translations away.  Both directions are checked.
#
# Skips cleanly without --enable-nls or without msgfmt.
test-nls-runtime:
	@echo "==> Running NLS runtime oracle (xx pseudo-locale)"
	bash tests/nls/run.sh

# 64-bit parse into a narrower destination (#1402).
#
# mux_atoi64() returns int64_t; storing that somewhere narrower truncates
# BEFORE anything can judge the value, so a range check placed after it passes
# on input it was written to reject.  #1404 fixed three instances (justify
# width, printf %d, fun_shl's count); these cover @poor and hasquota().
#
# Not smoke cases, for two different reasons: @poor is CA_GOD and walks the
# whole database, so a tr.tc* case would be refused and would also rewrite
# every other test's money; hasquota() needs `quotas yes`, which smoke
# deliberately runs without (powersee_fn.mux TC005 asserts the disabled path).
# Each case therefore gets a throwaway game, as tests/luajit does.
#
# Worth knowing where these can fail: `int` is 32-bit everywhere, so these run
# meaningfully on every host.  The rest of #1402 is `long`, which is 64-bit on
# LP64 -- that half cannot fail on Linux or macOS at all, so its coverage only
# means something on Windows.
test-narrowing:
	@echo "==> Running narrowing-destination tests"
	bash tests/narrowing/run.sh

# An unreadable configuration file must be fatal (#1601).  cf_read()'s return
# was discarded in LoadGame, so netmux came up on compiled-in defaults --
# listening, on the wrong database -- and muxscript reported success, which
# made every harness probe indistinguishable from the thing under test.
#
# Pins the two deliberately non-fatal cases too (unknown directive, empty
# file); those are the ones a later "make config errors fatal" change would
# break.  Needs only muxscript, so it runs on any built tree.
test-config:
	@echo "==> Running configuration-error tests"
	bash tests/config/run.sh

# JIT q-register scope oracle (docs/plan-jit-evalbracket-lift.md).
# Compares forced-JIT vs AST results for the scope/ordering shapes fixed
# in plan Phases 2-3.  Skips cleanly on builds without --enable-jit
# (the script exits 2 when jitstats()/the JIT blob is unavailable).
test-jit-qreg:
	@echo "==> Running JIT q-register scope oracle"
	@sh testcases/tools/jit_qreg/oracle.sh; rc=$$?; \
	if [ $$rc -eq 2 ]; then \
	    echo "==> Skipping (build has no JIT)"; \
	else \
	    exit $$rc; \
	fi

# JIT ifelse()/if() condition-truth oracle (#1157).
# Compares JIT vs AST for the condition shapes where xlate() disagrees
# with "atol() != 0" or with an integer truncation: bare %0-%9 cargs,
# non-numeric and dbref literals, and fractional floats.
# Skips cleanly on builds without --enable-jit (the script exits 2).
test-jit-ifelse:
	@echo "==> Running JIT ifelse() condition oracle"
	@sh testcases/tools/jit_ifelse/oracle.sh; rc=$$?; \
	if [ $$rc -eq 2 ]; then \
	    echo "==> Skipping (build has no JIT)"; \
	else \
	    exit $$rc; \
	fi

# Full smoke with mudconf.lua_jit forced on (#1309).  Default `make test`
# keeps lua_jit off so production configs stay safe until Phase 4 default-on.
# Requires --enable-jit (same as the rest of the Lua JIT path).
# Opt-in: not part of `make test`.
#   make test-lua-jit
#   # or:  cd testcases && SMOKE_EXTRA_CONF='lua_jit 1' ./tools/Smoke
test-lua-jit: install
	@echo "==> Running smoke with lua_jit 1 (Lua bytecode→HIR→DBT path)"
	$(MAKE) -C testcases/tools
	cd testcases && ./tools/Makesmoke && SMOKE_EXTRA_CONF='lua_jit 1' ./tools/Smoke

# Lua JIT differential harness (#1423 / #1426 / #1512): SURVIVE / AGREE / EXEC.
# Part of `make test`.  Cheap, and the only place that requires lua_run_ok to
# advance so a decline cannot masquerade as a pass (#1426).
#
# Unlike test-lua-jit above, this sets jit_eval_brackets 0 as well.  That
# matters: with eval brackets compiled, fun_lua is ECALLed from inside a DBT
# program, run_cached_program refuses the nested run (#1326), and the Lua JIT
# executes nothing at all -- so `lua_jit 1` on its own cannot reach any of
# this code.
test-lua-ecall: install
	@echo "==> Running Lua JIT differential harness (survive/agree/exec)"
	bash tests/luajit/run.sh

# GANL engine regression harness (epoll/select on Linux, kqueue/select on
# macOS/BSD).  Scripted engine scenarios locking in the 2026-07 fixes.
test-ganl:
	@echo "==> Running GANL engine tests"
	$(MAKE) -C mux/ganl/tests check

# netaddr unit tests: mux_subnet::compare_to (subnet/address, #799/#800) and
# parse_subnet rejection/normalization paths.  Links the netmux-side
# netmux-netaddr.o (from install) against libmux.
test-netaddr:
	@echo "==> Running netaddr subnet tests"
	$(MAKE) -C tests/netaddr test

# Run the high-coverage suites against a sanitizer build (#1440).
#
# Deliberately does NOT reconfigure.  Silently replacing the tree's build
# settings would be rude, and a sanitizer build is not what anyone wants left
# behind.  Configure one yourself first:
#
#   cd mux && ./configure <your usual flags> --enable-sanitizers
#
# or, to pick the set:
#
#   cd mux && ./configure <your usual flags> --enable-sanitizers=address
#
# then `make clean && make install` from the repo root.  The clean matters:
# objects left from a non-sanitizer build link fine but are not instrumented,
# which reads as a clean run.
#
# The value is concentrated in the suites that execute the most engine code.
# ASan reports a bad access only when something reaches it, so this multiplies
# the coverage already there rather than substituting for it.
#
# Everything below was verified to run clean under -fsanitize=address,undefined
# before being added, including test-ganl and test-scenario -- the only legs
# that exercise the live network path, which muxscript cannot reach at all.
#
# rvbench_fn is excluded from the smoke legs.  It issues 55 rvbench() calls at
# 10000 iterations each; instrumented, that is ~25ms per iteration, so the file
# alone runs for hours and the harness reports an idle-hang at ~260 of 315
# files -- which is how test-asan came to report FAILED for instrumentation
# cost rather than for a defect.  The timeouts are raised as well, since an
# instrumented smoke run legitimately takes several times longer.
ASAN_SMOKE_EXCLUDE = rvbench_fn

# LeakSanitizer is fatal to the harness, not merely noisy.  A long-lived
# server legitimately does not free everything at exit, and LSan's nonzero
# exit status at shutdown makes Makesmoke report "ERROR: muxscript failed"
# before a single test runs.  Measured on a sanitizer build of this tree:
#
#   default ASAN_OPTIONS           muxscript exit=1, "detected memory leaks"
#   ASAN_OPTIONS=detect_leaks=0    exit=0
#
# Leak hunting stays available and opt-in -- there are ~500 KB in ~460
# allocations to look at when someone wants them:
#
#   ASAN_OPTIONS=detect_leaks=1 make test-asan
#
# Both honour a value already in the environment, so either can be overridden.
ASAN_ENV = ASAN_OPTIONS=$${ASAN_OPTIONS:-detect_leaks=0} \
           UBSAN_OPTIONS=$${UBSAN_OPTIONS:-print_stacktrace=1}

# The tests/ islands build their own binaries from their own Makefiles, so
# they are NOT sanitizer-instrumented even when libmux is, and ASan refuses
# to start:
#
#   ASan runtime does not come first in initial library list; you should
#   either link runtime to your application or manually preload it
#
# test-format and test-dbt both died on this immediately.  Preloading the
# runtime is the documented remedy and keeps the islands out of the configure
# plumbing.  Resolved from the compiler rather than hardcoded, and empty when
# unavailable so an unusual toolchain degrades to the original error rather
# than a confusing LD_PRELOAD failure.
ASAN_PRELOAD = $(shell $(CC) -print-file-name=libasan.so 2>/dev/null | grep / || true)
ASAN_ISLAND_ENV = $(if $(ASAN_PRELOAD),LD_PRELOAD=$(ASAN_PRELOAD),) $(ASAN_ENV)
test-asan:
	@if ! grep -q 'fsanitize' mux/config.status 2>/dev/null; then \
	    echo "==> test-asan: this tree is not configured with sanitizers."; \
	    echo "    See the recipe above this target in the Makefile."; \
	    exit 1; \
	fi
	@echo "==> Running the suites under sanitizers"
	$(ASAN_ISLAND_ENV) $(MAKE) test-format
	$(ASAN_ISLAND_ENV) $(MAKE) test-netaddr
	$(ASAN_ISLAND_ENV) $(MAKE) test-alarm
	$(ASAN_ISLAND_ENV) $(MAKE) test-dbt
	$(ASAN_ISLAND_ENV) $(MAKE) test-ganl
	$(ASAN_ISLAND_ENV) $(MAKE) test-jit-qreg
	$(ASAN_ISLAND_ENV) $(MAKE) test-jit-ifelse
	$(ASAN_ISLAND_ENV) $(MAKE) test-lua-ecall
	$(ASAN_ISLAND_ENV) $(MAKE) test-scenario
	cd testcases && $(ASAN_ENV) SMOKE_EXCLUDE="$(ASAN_SMOKE_EXCLUDE)" ./tools/Makesmoke \
	    && $(ASAN_ENV) SMOKE_EXCLUDE="$(ASAN_SMOKE_EXCLUDE)" ./tools/Smoke \
	        --activity-timeout 300 --wallclock-timeout 3600
	cd testcases && $(ASAN_ENV) SMOKE_EXCLUDE="$(ASAN_SMOKE_EXCLUDE)" \
	    SMOKE_EXTRA_CONF="jit_eval_brackets 0" ./tools/Smoke \
	        --activity-timeout 300 --wallclock-timeout 3600

# mux_vsnprintf differential tests: %i, %o and the floating-point conversions
# against the platform snprintf as an oracle.  These conversions used to fall
# through to mux_assert(0) and abort the process (#1382, and the same shape in
# @list), so they are implemented rather than forbidden -- and the float path
# assembles mux_dtoa digits by hand, which is precisely the code that needs an
# oracle rather than a few spot checks.
test-format:
	@echo "==> Running mux_vsnprintf format tests"
	$(MAKE) -C tests/format test

# NLS marking and catalogue guard (tests/nls): softcode ABI tokens and printf
# conversions must never become translatable, a literal must not be M_() in one
# place and T() in another within one file, every catalogue must cover the .pot
# with no fuzzy entries (msgfmt drops those silently), and the .pot must match
# what the sources actually mark.  Static -- no build, no server, no catalogue
# needs installing (#1505).
#
# Runs whether or not the tree was configured --enable-nls: the marking is in
# the sources either way, and a slice that breaks it should not be able to hide
# behind an English-only build.
test-nls:
	@echo "==> Running NLS marking/catalogue guard"
	python3 tests/nls/check_nls.py

# DBT and RV64 tests (tests/dbt): chain patch encode/decode across all three
# backends (#1152), block cache dedupe and eviction (#1153), and the RV64
# execution harness -- interpreter plus DBT, host backend only since it runs
# what it translates.  Three binaries because each needs a different link.
# All compile engine sources directly: no `install`, no --enable-jit, and no
# skip path except `exec` on a host with no backend.
test-dbt:
	@echo "==> Running DBT and RV64 tests"
	$(MAKE) -C tests/dbt test

# mux_alarm unit tests: the per-command wall-clock abort.  Guards the lazy
# worker-thread start — alarm_clock is a libmux global whose constructor used
# to spawn a thread during static init, deadlocking before main in ~14% of
# runs (which is what made `make test` hang here intermittently).
test-alarm:
	@echo "==> Running mux_alarm tests"
	$(MAKE) -C tests/alarm test

# Live scenario test: the wildcard capture path ($-command %0..%9), which
# muxscript cannot drive.  Opt-in (NOT part of `make test`) because it spins a
# throwaway netmux and drives it over a socket — timing-sensitive by nature.
test-scenario: install
	@echo "==> Running wildcard-capture scenario test"
	bash tests/scenario/run.sh

# 2.13 <-> 2.14 parser parity jig.  MUSH function-call recognition is
# context sensitive in ways a tokenizer cannot decide (`add(` is a call,
# `foo(` is not — only a function-table lookup separates them), so the
# grammar is not well-formed and there is no rule to validate against.
# The specification is what 2.13 actually does, and this measures it.
#
# Compares 2.14's JIT and AST routes against each other (always), and
# against a built 2.13 tree when one is available (MUX213_ROOT, or a
# conventional location).  All three are driven identically through a
# live netmux over a socket — 2.13 has no muxscript, so the 2.14 legs
# use the same path rather than the convenient one.
#
# Opt-in, NOT part of `make test`: divergences currently exist (#1214),
# so this is a measurement tool rather than a pass/fail gate.
test-parity213: install
	@echo "==> Checking the adjudicator itself (#1368)"
	bash tests/parity213/selftest_adjudicate.sh
	@echo "==> Running 2.13/2.14 parser parity jig"
	sh tests/parity213/run.sh

# Live network + queue stress harness: concurrent connections, bulk queue
# fan-outs, and an over-cap burst that provokes the runaway shed.  Defensive
# — pushes the accept/read/write path and the scheduler to catch problems
# before a live game does.  Opt-in (NOT part of `make test`): live-socket,
# timing-sensitive, and multi-second by design.
test-stress: install
	@echo "==> Running network+queue stress harness"
	bash tests/stress/run.sh

# Headless iOS Titan parser/model tests via SPM. Skipped off Darwin
# or when swift is unavailable.
test-ios:
	@if [ "$$(uname -s)" = "Darwin" ] && command -v swift >/dev/null 2>&1; then \
	    echo "==> Running iOS Titan parser/model tests"; \
	    cd client/ios && swift test; \
	else \
	    echo "==> Skipping iOS tests (not on Darwin or no swift available)"; \
	fi
