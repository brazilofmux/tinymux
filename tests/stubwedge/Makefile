# tests/stubwedge — #2238 stubslave-wedge regression, and the #2244
# protocol-fault regression that its core forensics led to.
#
# Needs a built muxscript + stubslave (make install from the repo root with
# configure --enable-stubslave).  Skips green on builds without stubslave.

.PHONY: all test clean

all:
	@true

test:
	@bash ./run_wedge.sh
	@bash ./run_fault.sh

clean:
	@rm -rf work
