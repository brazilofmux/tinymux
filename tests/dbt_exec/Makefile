# Makefile — RV64 execution tests for the interpreter and the DBT.
#
# mux/modules/engine/dbt_test.cpp has existed for a long time and has never
# been in any build: no Makefile.am entry, no CI, no `make test`.  It only
# ran if a developer typed the compile line out of its header comment by
# hand.  That left the project with no automated RV64 *execution* coverage
# at all, which is the same "nothing exercises it" gap that produced #1152
# (an x86 decode applied to AArch64), #1153 and #1151 — all three found by
# reading rather than by running.
#
# Unlike tests/dbt_chain, which compiles all three backends because it only
# inspects emitted bytes, this harness EXECUTES translated code and so must
# build the backend matching this host.  The selection mirrors configure.ac;
# an unrecognised host skips loudly rather than building a wrong backend.
#
# Build: make
# Run:   make test

CXX       = g++
CXXFLAGS  = -std=c++17 -g -O2
INCDIR    = ../../mux/include
ENGINE    = ../../mux/modules/engine
DEFS      = -DHAVE_CONFIG_H -DTINYMUX_JIT

# Host backend, same mapping configure.ac uses for @DBT_BACKEND@.
ARCH := $(shell uname -m)
ifneq (,$(filter $(ARCH),aarch64 arm64))
BACKEND = dbt_a64_sysv
else ifneq (,$(filter $(ARCH),x86_64 amd64))
BACKEND = dbt_x64_sysv
else
BACKEND =
endif

TARGET = dbt_test
SRCS   = $(ENGINE)/dbt_test.cpp $(ENGINE)/dbt_interp.cpp \
         $(ENGINE)/dbt_elf64.cpp $(ENGINE)/dbt.cpp \
         $(ENGINE)/$(BACKEND).cpp

# The cross-compiled RV64 ELF carries most of the DBT block-translation
# coverage: the hand-assembled cases are mostly interpreter-only, while the
# ELF leg runs through both routes and translates ~54 blocks.
ELF = $(ENGINE)/dbt_rt/test_rv64.elf

all: $(TARGET)

$(TARGET):
ifeq ($(BACKEND),)
	@echo "SKIP: no DBT backend for host '$(ARCH)' (configure.ac supports"
	@echo "      x86_64 and aarch64).  Execution tests cannot run here."
else
	$(CXX) $(CXXFLAGS) $(DEFS) -I$(INCDIR) -o $@ $(SRCS)
endif

test: $(TARGET)
ifeq ($(BACKEND),)
	@echo "==> Skipping DBT execution tests (unsupported host '$(ARCH)')"
else
	@if [ -f "$(ELF)" ]; then \
	    ./$(TARGET) "$(ELF)"; \
	else \
	    echo "NOTE: $(ELF) missing — running hand-assembled cases only,"; \
	    echo "      which drops the ELF leg's DBT block-translation coverage."; \
	    ./$(TARGET); \
	fi
endif

clean:
	rm -f *.o $(TARGET)

.PHONY: all test clean
