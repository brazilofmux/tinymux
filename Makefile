# Top-level convenience Makefile.
# Delegates to the autotools build system in mux/.
#
# Usage:
#   make              — build everything (libmux, netmux, engine, modules)
#   make install      — build + create symlinks in mux/game/bin
#   make clean        — clean all build artifacts
#   make test         — run smoke tests (build + install first)

.PHONY: all install clean realclean test

all:
	$(MAKE) -C mux

install: all
	$(MAKE) -C mux install
	$(MAKE) -C mux/script install

clean:
	$(MAKE) -C mux clean
	$(MAKE) -C mux/script clean
	$(MAKE) -C testcases/tools clean

realclean:
	$(MAKE) -C mux distclean

test: install
	$(MAKE) -C testcases/tools
	cd testcases && ./tools/Makesmoke && ./tools/Smoke
