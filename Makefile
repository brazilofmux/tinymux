# Top-level convenience Makefile.
# Delegates to the autotools build system in mux/.
#
# Usage:
#   make              — build everything (libmux, netmux, engine, modules)
#   make install      — build + create symlinks in mux/game/bin
#   make clean        — clean all build artifacts
#   make test         — run smoke tests (build + install first)
#   make hooks        — install git hooks (done automatically on first build)

.PHONY: all install clean realclean test test-ios test-ganl test-netaddr test-dbt-chain test-alarm test-scenario test-parity213 test-stress test-jit-qreg test-jit-ifelse hooks

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
	$(MAKE) -C tests/dbt_chain clean

realclean:
	$(MAKE) -C mux distclean

test: install test-ganl test-netaddr test-dbt-chain test-alarm test-jit-qreg test-jit-ifelse test-ios
	$(MAKE) -C testcases/tools
	cd testcases && ./tools/Makesmoke && ./tools/Smoke

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

# DBT block-chaining patch encode/decode tests (#1152).  Asserts that
# dbt_backend_decode_jmp_target is the exact inverse of
# dbt_backend_backpatch_jmp, which is what dbt_resolve_chains relies on to
# tell an unresolved site from a live one.  Builds all three backends
# (a64_sysv, x64_sysv, x64_win64) into one binary on every host — the #1152
# bug survived precisely because nothing exercised the affected backend.
# Compiles the backend sources directly, so it needs neither `install` nor
# --enable-jit and cannot degrade into testing nothing.
test-dbt-chain:
	@echo "==> Running DBT chain patch encode/decode tests"
	$(MAKE) -C tests/dbt_chain test

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
