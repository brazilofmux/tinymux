# Makefile — DBT and RV64 tests.
#
# Three binaries rather than one, because each needs a genuinely different
# link and they cannot coexist in a single image:
#
#   chain  all three backends at once, with their colliding strong symbols
#          renamed via -D.  It only inspects emitted bytes, so it does not
#          need a host that can run them — which is the point: #1152 was a
#          decode bug in a backend nobody compiled.  No dbt.cpp.
#   cache  dbt.cpp against backend stubs that abort if called.  Cannot be
#          merged with `chain`, whose backends are renamed away, or with
#          `exec`, which links the real ones.
#   exec   the RV64 execution harness: interpreter and DBT, host backend
#          only, because this one actually runs the code it translates.
#   fuzz   instruction-level differential fuzzer: random RV64 sequences
#          through both routes, comparing all 32 registers.  Same link as
#          exec minus the ELF loader and the hand-assembled cases.
#
# All three compile engine sources directly, so none needs a prior
# `make install` or --enable-jit, and none has a skip path that could
# quietly degrade into testing nothing.  `exec` is the exception on host
# support: it skips loudly off x86_64/aarch64 because it must execute.
#
# Build: make          Run: make test
# Individually:        make chain | make cache | make exec

CXX       = g++
CXXFLAGS  = -std=c++17 -g -O2
TESTFLAGS = -Wall -Wextra
INCDIR    = ../../mux/include
ENGINE    = ../../mux/modules/engine
DEFS      = -DHAVE_CONFIG_H -DTINYMUX_JIT
COMPILE   = $(CXX) $(CXXFLAGS) $(DEFS) -I$(INCDIR)

# Engine sources are code under test, not code being written here; the
# engine build already compiles them under its own warning flags, so
# -Wall -Wextra applies to the drivers alone to keep real failures visible.
DRIVER    = $(CXX) $(CXXFLAGS) $(TESTFLAGS) $(DEFS) -I$(INCDIR)

# Host backend for `exec`, mirroring configure.ac's @DBT_BACKEND@ mapping.
ARCH := $(shell uname -m)
ifneq (,$(filter $(ARCH),aarch64 arm64))
HOST_BACKEND = dbt_a64_sysv
else ifneq (,$(filter $(ARCH),x86_64 amd64))
HOST_BACKEND = dbt_x64_sysv
else
HOST_BACKEND =
endif

# The five strong symbols every backend defines; $(1) is the per-backend
# prefix test_chain.cpp declares.
rename = -Ddbt_backend_backpatch_jmp=$(1)_backpatch_jmp \
         -Ddbt_backend_decode_jmp_target=$(1)_decode_jmp_target \
         -Ddbt_backend_emit_trampoline=$(1)_emit_trampoline \
         -Ddbt_backend_translate_block=$(1)_translate_block \
         -Ddbt_register_intrinsic=$(1)_register_intrinsic

ELF = $(ENGINE)/dbt_rt/test_rv64.elf

all: test_chain test_cache dbt_test fuzz_diff

# --- chain: all three backends, byte inspection only -----------------
test_chain.o: test_chain.cpp
	$(DRIVER) -c -o $@ $<
be_a64.o: $(ENGINE)/dbt_a64_sysv.cpp
	$(COMPILE) $(call rename,a64) -c -o $@ $<
be_x64sysv.o: $(ENGINE)/dbt_x64_sysv.cpp
	$(COMPILE) $(call rename,x64sysv) -c -o $@ $<
be_win64.o: $(ENGINE)/dbt_x64_win64.cpp
	$(COMPILE) $(call rename,win64) -c -o $@ $<
test_chain: test_chain.o be_a64.o be_x64sysv.o be_win64.o
	$(CXX) $(CXXFLAGS) -o $@ $^

# --- cache: dbt.cpp against aborting backend stubs -------------------
test_cache.o: test_cache.cpp
	$(DRIVER) -c -o $@ $<
dbt_shared.o: $(ENGINE)/dbt.cpp
	$(COMPILE) -c -o $@ $<
test_cache: test_cache.o dbt_shared.o
	$(CXX) $(CXXFLAGS) -o $@ $^

# --- exec: real execution, host backend only -------------------------
EXEC_SRCS = $(ENGINE)/dbt_test.cpp $(ENGINE)/dbt_interp.cpp \
            $(ENGINE)/dbt_elf64.cpp $(ENGINE)/dbt.cpp \
            $(ENGINE)/$(HOST_BACKEND).cpp
dbt_test:
ifeq ($(HOST_BACKEND),)
	@echo "SKIP: no DBT backend for host '$(ARCH)' (configure.ac supports"
	@echo "      x86_64 and aarch64).  Execution tests cannot run here."
else
	$(COMPILE) -o $@ $(EXEC_SRCS)
endif

FUZZ_SRCS = fuzz_diff.cpp $(ENGINE)/dbt_interp.cpp $(ENGINE)/dbt.cpp \
            $(ENGINE)/$(HOST_BACKEND).cpp
fuzz_diff:
ifeq ($(HOST_BACKEND),)
	@echo "SKIP: no DBT backend for host '$(ARCH)'."
else
	$(COMPILE) -o $@ $(FUZZ_SRCS)
endif

chain: test_chain
	./test_chain

cache: test_cache
	./test_cache

exec: dbt_test
ifeq ($(HOST_BACKEND),)
	@echo "==> Skipping DBT execution tests (unsupported host '$(ARCH)')"
else
	@if [ -f "$(ELF)" ]; then \
	    ./dbt_test "$(ELF)"; \
	else \
	    echo "NOTE: $(ELF) missing — running hand-assembled cases only,"; \
	    echo "      which drops the ELF leg's DBT block-translation coverage."; \
	    ./dbt_test; \
	fi
endif

# A small default keeps `make test` quick; the corpus is deterministic, so
# a CI failure reproduces exactly.  Soak with more:
#   DBT_FUZZ_ITERS=20000 DBT_FUZZ_SEED=7 make -C tests/dbt fuzz
fuzz: fuzz_diff
ifeq ($(HOST_BACKEND),)
	@echo "==> Skipping DBT fuzz (unsupported host '$(ARCH)')"
else
	./fuzz_diff
endif

test: chain cache exec fuzz

clean:
	rm -f *.o test_chain test_cache dbt_test fuzz_diff

.PHONY: all test chain cache exec fuzz clean
