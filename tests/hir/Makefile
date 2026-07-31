# tests/hir — HIR CFG capacity / sentinel regressions (#1863).
#
# Header-only hir_program; no libmux link required.

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -g -O1 -Wall -Wextra
CPPFLAGS += -I../../mux/include

# Match the repo's sanitizer flags when the main tree is instrumented
# (#1522 pattern): empty on an ordinary build.
MUX_SANITIZE := $(shell grep -oE '\-fsanitize=[a-z,]+|\-fno-omit-frame-pointer' \
                  ../../mux/config.status 2>/dev/null | sort -u | tr '\n' ' ')
CXXFLAGS += $(MUX_SANITIZE)

.PHONY: all test clean

all: test_blocks

test_blocks: test_blocks.cpp ../../mux/include/hir.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ test_blocks.cpp

test: test_blocks
	./test_blocks

clean:
	rm -f test_blocks
