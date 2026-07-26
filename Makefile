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
.PHONY: all install clean realclean test test-ios test-ganl test-netaddr test-format test-dbt test-alarm test-smoke test-smoke-ast test-scenario test-parity213 test-stress test-jit-qreg test-jit-ifelse test-lua-jit test-lua-ecall test-asan hooks

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

test: install test-ganl test-netaddr test-format test-dbt test-alarm test-jit-qreg test-jit-ifelse test-lua-ecall test-ios test-smoke test-smoke-ast

# Extra flags passed through to tools/Smoke.  Empty for a normal build; set by
# test-asan, where an instrumented binary is slow enough to trip Smoke's
# hang detector (#1440 follow-up).
SMOKE_FLAGS ?=

# Smoke on the compiled route (jit_eval_brackets defaults on).
test-smoke:
	$(MAKE) -C testcases/tools
	@echo "==> Smoke: compiled route (jit_eval_brackets default)"
	cd testcases && ./tools/Makesmoke && ./tools/Smoke $(SMOKE_FLAGS)

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
	cd testcases && SMOKE_EXTRA_CONF="jit_eval_brackets 0" ./tools/Smoke $(SMOKE_FLAGS)

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

# Fault containment for the Lua JIT's ECALL table ops (#1423): a Lua chunk
# must not be able to abort the process.  Part of `make test` -- it is a crash
# regression, and it is cheap.
#
# Unlike test-lua-jit above, this sets jit_eval_brackets 0 as well.  That
# matters: with eval brackets compiled, fun_lua is ECALLed from inside a DBT
# program, run_cached_program refuses the nested run (#1326), and the Lua JIT
# executes nothing at all -- so `lua_jit 1` on its own cannot reach any of
# this code.  See #1426.
test-lua-ecall: install
	@echo "==> Running Lua JIT ECALL fault-containment tests"
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
# Smoke's hang detector has to be relaxed for an instrumented binary.  Its
# defaults (30s without log growth, 300s wall clock) are sized for a normal
# build, and an instrumented run trips them:
#
#     TIMEOUT: idle-hang (no log activity for 30s >= 30s)
#
# at about 1050 of 1497 cases.  Raising it to 300s moved the report to
# "no log activity for 301s" at the SAME point, which is the useful
# measurement: this is not uniform slowness, it is one silent stretch.  The
# last line before it is
#
#     Beginning rvbench() benchmarks.
#
# rvbench_fn.mux runs ~37 benchmarks of 10000 iterations each and logs nothing
# until they finish, so under instrumentation it goes quiet for many minutes.
# astbench_fn, benchmark_fn and cachestats_fn are the same shape.
#
# The numbers below are generous rather than sufficient -- a full instrumented
# pass has NOT been demonstrated to complete here, and the benchmarks are the
# reason.  Measuring performance under -fsanitize is meaningless anyway, so the
# real fix is to exclude those four files from this target; the generator takes
# an inclusion list and has no skip mechanism today.  See the issue linked from
# the commit for that.
ASAN_SMOKE_FLAGS ?= --activity-timeout 900 --wallclock-timeout 7200

# tests/format and tests/dbt build their own binaries with their own hardcoded
# CXXFLAGS, so they are NOT instrumented even on a sanitizer tree.  A plain
# binary that then loads an instrumented libmux.so is refused outright:
#
#     ASan runtime does not come first in initial library list; you should
#     either link runtime to your application or manually preload it with
#     LD_PRELOAD.
#
# which made test-asan fail at its first suite and never reach smoke at all.
# Preloading the runtime is ASan's own remedy for that message.  Resolved
# rather than hardcoded because the path is compiler- and
# version-specific, and left empty when it cannot be found so a tree without
# it (or a platform where the mechanism differs, e.g. macOS
# DYLD_INSERT_LIBRARIES) degrades to the previous behaviour rather than
# failing on a bogus LD_PRELOAD.
ASAN_RUNTIME := $(shell p=$$(g++ -print-file-name=libasan.so 2>/dev/null); \
                        [ -n "$$p" ] && [ -e "$$p" ] && echo "$$p")
ASAN_PRELOAD := $(if $(ASAN_RUNTIME),LD_PRELOAD=$(ASAN_RUNTIME),)

test-asan:
	@if ! grep -q 'fsanitize' mux/config.status 2>/dev/null; then \
	    echo "==> test-asan: this tree is not configured with sanitizers."; \
	    echo "    See the recipe above this target in the Makefile."; \
	    exit 1; \
	fi
	@echo "==> Running the high-coverage suites under sanitizers"
	@if [ -z "$(ASAN_RUNTIME)" ]; then \
	    echo "    note: libasan.so not located; tests/format and tests/dbt"; \
	    echo "    may refuse to run against an instrumented libmux."; \
	fi
	$(ASAN_PRELOAD) $(MAKE) test-format
	$(ASAN_PRELOAD) $(MAKE) test-dbt
	$(MAKE) SMOKE_FLAGS="$(ASAN_SMOKE_FLAGS)" test-smoke
	$(MAKE) SMOKE_FLAGS="$(ASAN_SMOKE_FLAGS)" test-smoke-ast

# mux_vsnprintf differential tests: %i, %o and the floating-point conversions
# against the platform snprintf as an oracle.  These conversions used to fall
# through to mux_assert(0) and abort the process (#1382, and the same shape in
# @list), so they are implemented rather than forbidden -- and the float path
# assembles mux_dtoa digits by hand, which is precisely the code that needs an
# oracle rather than a few spot checks.
test-format:
	@echo "==> Running mux_vsnprintf format tests"
	$(MAKE) -C tests/format test

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
