# Top-level convenience Makefile.
# Delegates to the autotools build system in mux/.
#
# Usage:
#   make              — build everything (libmux, netmux, engine, modules)
#   make install      — build + create symlinks in mux/game/bin
#   make clean        — clean all build artifacts
#   make test         — run smoke tests (build + install first)
#   make hooks        — install git hooks (done automatically on first build)

.PHONY: all install clean realclean test test-ios test-ganl test-netaddr test-scenario test-stress test-jit-qreg hooks

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

realclean:
	$(MAKE) -C mux distclean

test: install test-ganl test-netaddr test-jit-qreg test-ios
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

# Live scenario test: the wildcard capture path ($-command %0..%9), which
# muxscript cannot drive.  Opt-in (NOT part of `make test`) because it spins a
# throwaway netmux and drives it over a socket — timing-sensitive by nature.
test-scenario: install
	@echo "==> Running wildcard-capture scenario test"
	bash tests/scenario/run.sh

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
