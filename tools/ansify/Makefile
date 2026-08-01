# ansify — TinyMUX percent-color → ANSI escape converter
#
# Source of truth: ansify.rl  (Ragel -G2, same style as muxescape / color_ops)
# Generated:       ansify.c   (checked in — do not hand-edit)
#
#   make          build ./ansify from the checked-in ansify.c (no Ragel)
#   make regen    regenerate ansify.c from ansify.rl (requires Ragel)
#   make test     build + smoke tests
#   make clean    remove binary (keeps generated .c)

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -Wno-implicit-fallthrough -std=c11
LDFLAGS ?=
RAGEL   ?= ragel
RFLAGS  ?= -G2 -C

.PHONY: all clean test regen

all: ansify

# Default build uses the checked-in generated C so contributors do not need
# Ragel installed.  Edit ansify.rl, then `make regen`, then commit both.
ansify: ansify.c
	$(CC) $(CFLAGS) -o $@ ansify.c $(LDFLAGS)

regen: ansify.rl
	chmod u+w ansify.c 2>/dev/null || true
	$(RAGEL) $(RFLAGS) -o ansify.c ansify.rl
	chmod a-w ansify.c 2>/dev/null || true

clean:
	rm -f ansify ansify.o

test: ansify
	@sh tests/smoke.sh
