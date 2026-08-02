# tests/stubslave — #1939 muxscript stubslave-teardown regression.
#
# Needs a built muxscript + stubslave (make install from the repo root with
# configure --enable-stubslave).  Skips green on builds without stubslave.
# Opt-in from the root Makefile as `make test-stubslave-teardown` and part
# of `make test`.

.PHONY: all test clean

all:
	@true

test:
	@bash ./run_teardown.sh

clean:
	@rm -rf work
