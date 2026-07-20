# Top-level convenience Makefile.
# Delegates to the autotools build system in mux/.
#
# Usage:
#   make              — build everything (libmux, netmux, engine, modules)
#   make install      — build + create symlinks in mux/game/bin
#   make clean        — clean all build artifacts
#   make test         — run smoke tests (build + install first)
#   make hooks        — install git hooks (done automatically on first build)

.PHONY: all install clean realclean test test-ios test-ganl test-netaddr hooks

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

test: install test-ganl test-netaddr test-ios
	$(MAKE) -C testcases/tools
	cd testcases && ./tools/Makesmoke && ./tools/Smoke

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

# Headless iOS Titan parser/model tests via SPM. Skipped off Darwin
# or when swift is unavailable.
test-ios:
	@if [ "$$(uname -s)" = "Darwin" ] && command -v swift >/dev/null 2>&1; then \
	    echo "==> Running iOS Titan parser/model tests"; \
	    cd client/ios && swift test; \
	else \
	    echo "==> Skipping iOS tests (not on Darwin or no swift available)"; \
	fi
