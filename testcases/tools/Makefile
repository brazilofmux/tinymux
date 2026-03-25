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

MUX_LIB  = ../../mux/lib
MUX_INC  = ../../mux/include
ICU_LIBS = $(shell pkg-config --libs icu-uc icu-i18n 2>/dev/null || echo "-licui18n -licuuc -licudata")

test_unicode_icu: test_unicode_icu.cpp
	g++ -std=c++17 -O2 -I$(MUX_INC) -o $@ $< -L$(MUX_LIB) -Wl,-rpath,$(MUX_LIB) -lmux $(ICU_LIBS) -lm

.PHONY: test_icu
test_icu: test_unicode_icu
	./test_unicode_icu

clean:
	rm -f $(TOOLS) test_unicode_icu
