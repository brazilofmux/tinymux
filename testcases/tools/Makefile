# testcases/tools/ — Ragel -G2 MUX softcode utilities
#
# Targets:
#   all       — build unformat and reformat
#   clean     — remove binaries (not .c files)
#
# The .c files are checked in so platforms without ragel can still build.
# If ragel is available, editing a .rl regenerates the corresponding .c.

CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -Wno-implicit-fallthrough -std=c11 -D_POSIX_C_SOURCE=200809L -g
RAGEL   = ragel
RFLAGS  = -G2 -C

TOOLS   = unformat reformat

.PHONY: all clean

all: $(TOOLS)

# Ragel .rl -> .c (only if ragel is installed)
%.c: %.rl
	@if command -v $(RAGEL) >/dev/null 2>&1; then \
	    echo "$(RAGEL) $(RFLAGS) -o $@ $<"; \
	    $(RAGEL) $(RFLAGS) -o $@ $<; \
	else \
	    echo "ragel not found, using checked-in $@"; \
	fi

# .c -> binary
%: %.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TOOLS)
