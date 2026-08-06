# Makefile — DBT and RV64 tests.
#
# Five binaries rather than one, because each needs a genuinely different
# link and they cannot coexist in a single image:
#
#   chain  all three backends at once, with their colliding strong symbols
#          renamed via -D.  It only inspects emitted bytes, so it does not
#          need a host that can run them — which is the point: #1152 was a
#          decode bug in a backend nobody compiled.  No dbt.cpp.
#   cache  dbt.cpp against backend stubs that abort if called.  Cannot be
#          merged with `chain`, whose backends are renamed away, or with
#          `exec`, which links the real ones.
#   interp guest-memory bounds on the interpreter route.  #includes
#          dbt_interp.cpp to reach file-static mem_check, so it cannot
#          share a link with `exec`, which compiles that TU normally.
#   exec   the RV64 execution harness: interpreter and DBT, host backend
#          only, because this one actually runs the code it translates.
#   fuzz   instruction-level differential fuzzer: random RV64 sequences
#          through the interpreter and through the DBT, comparing all 32
#          integer and 32 FP registers.  Same link as exec, minus the ELF
#          loader and the hand-assembled cases.
#
# All of them compile engine sources directly, so none needs a prior
# `make install` or --enable-jit, and none has a skip path that could
# quietly degrade into testing nothing.  `exec` and `fuzz` are the
# exceptions on host support: they skip loudly off x86_64/aarch64 because
# they must execute what they translate.
#
# Build: make          Run: make test
# Individually:        make chain | cache | interp | exec | fuzz

CXX       = g++
# -MMD -MP: emit header prerequisites next to each object (#1952), so the
# ENGINE_HDRS list below cannot silently go stale.  That list is correct
# today and the comment on it explains why it exists; what it cannot do is
# notice a new #include nobody adds to it.
CXXFLAGS  = -std=c++17 -g -O2 -MMD -MP
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

# Engine headers every target here compiles against.  Listed explicitly as
# prerequisites because the code under test lives outside this directory:
# without them, editing dbt_internal.h (which holds rc_loop_overcommits and
# the register-cache sizing) rebuilds nothing and the suite reports a pass
# from a stale binary.
ENGINE_HDRS = $(INCDIR)/dbt.h $(INCDIR)/dbt_internal.h \
              $(INCDIR)/dbt_interp.h $(INCDIR)/dbt_decoder.h \
              $(INCDIR)/dbt_host.h $(INCDIR)/dbt_elf64.h \
              $(INCDIR)/dbt_emit_a64.h $(INCDIR)/dbt_emit_x64.h \
              $(INCDIR)/dbt_jit_mem.h

all: test_chain test_cache test_interp dbt_test fuzz_diff test_alarm test_reloc test_emit_bounds

# --- chain: all three backends, byte inspection only -----------------
test_chain.o: test_chain.cpp $(ENGINE_HDRS)
	$(DRIVER) -c -o $@ $<
be_a64.o: $(ENGINE)/dbt_a64_sysv.cpp $(ENGINE_HDRS)
	$(COMPILE) $(call rename,a64) -c -o $@ $<
be_x64sysv.o: $(ENGINE)/dbt_x64_sysv.cpp $(ENGINE_HDRS)
	$(COMPILE) $(call rename,x64sysv) -c -o $@ $<
be_win64.o: $(ENGINE)/dbt_x64_win64.cpp $(ENGINE_HDRS)
	$(COMPILE) $(call rename,win64) -c -o $@ $<
test_chain: test_chain.o be_a64.o be_x64sysv.o be_win64.o
	$(CXX) $(CXXFLAGS) -o $@ $^

# --- reloc: code-slot JAL scan/re-aim (#2129) ------------------------
# dbt_reloc.h is self-contained, so this is a driver-only link that runs
# on any host — the golden words come from riscv64-unknown-elf-as, so a
# codec bug fails here without needing to execute anything.
test_reloc.o: test_reloc.cpp $(INCDIR)/dbt_reloc.h
	$(DRIVER) -c -o $@ $<
test_reloc: test_reloc.o
	$(CXX) $(CXXFLAGS) -o $@ $^

# --- emit_bounds: raw backpatches vs a full code buffer --------------
# The x64 emitter only writes bytes into a caller buffer, so this runs
# on any host — an arm64 box compiling it still checks the x64 emitter
# it cannot execute (which is how the unguarded backpatch survived: the
# only guarded-emitter host was the one that could not reach the bug).
test_emit_bounds.o: test_emit_bounds.cpp $(INCDIR)/dbt_emit_x64.h $(INCDIR)/dbt.h
	$(DRIVER) -c -o $@ $<
test_emit_bounds: test_emit_bounds.o
	$(CXX) $(CXXFLAGS) -o $@ $^

# --- cache: dbt.cpp against aborting backend stubs -------------------
test_cache.o: test_cache.cpp $(ENGINE_HDRS)
	$(DRIVER) -c -o $@ $<
dbt_shared.o: $(ENGINE)/dbt.cpp $(ENGINE_HDRS)
	$(COMPILE) -c -o $@ $<
test_cache: test_cache.o dbt_shared.o
	$(CXX) $(CXXFLAGS) -o $@ $^

# --- interp: guest-memory bounds on the interpreter route ------------
# mem_check and the mem accessors are file-static, so the driver
# #includes dbt_interp.cpp rather than linking it.  That also means no
# stubs: the TU needs only dbt_interp.h, dbt_decoder.h and the standard
# library.  It cannot share a link with `exec`, which compiles the same
# TU normally, or with `cache`/`chain`, whose backends are stubbed or
# renamed (#1292).
test_interp.o: test_interp.cpp $(ENGINE)/dbt_interp.cpp $(ENGINE_HDRS)
	$(COMPILE) -I$(ENGINE) -c -o $@ $<
test_interp: test_interp.o
	$(CXX) $(CXXFLAGS) -o $@ $^

# --- exec: real execution, host backend only -------------------------
#
# dbt_test compiles its sources in one step rather than via .o files, so it
# needs those sources named as prerequisites.  A recipe with no
# prerequisites is only ever run once: make then reports the binary up to
# date forever, and every later engine change is "tested" by whichever
# binary happened to be built first.
EXEC_SRCS = $(ENGINE)/dbt_test.cpp $(ENGINE)/dbt_interp.cpp \
            $(ENGINE)/dbt_elf64.cpp $(ENGINE)/dbt.cpp \
            $(ENGINE)/$(HOST_BACKEND).cpp

# --- fuzz: instruction-level differential fuzzer ---------------------
# Same link as exec minus the ELF loader and the hand-assembled cases.
FUZZ_SRCS = fuzz_diff.cpp $(ENGINE)/dbt_interp.cpp $(ENGINE)/dbt.cpp \
            $(ENGINE)/$(HOST_BACKEND).cpp

# --- alarm: guest loops must stay inside dbt_run's safety nets (#1571) ---
# Same link as fuzz: dbt.cpp plus the host backend, no ELF loader.
ALARM_SRCS = test_alarm.cpp $(ENGINE)/dbt_interp.cpp $(ENGINE)/dbt.cpp \
             $(ENGINE)/$(HOST_BACKEND).cpp

ifeq ($(HOST_BACKEND),)
dbt_test:
	@echo "SKIP: no DBT backend for host '$(ARCH)' (configure.ac supports"
	@echo "      x86_64 and aarch64).  Execution tests cannot run here."
fuzz_diff:
	@echo "SKIP: no DBT backend for host '$(ARCH)'."
test_alarm:
	@echo "SKIP: no DBT backend for host '$(ARCH)'."
else
dbt_test: $(EXEC_SRCS) $(ENGINE_HDRS)
	$(COMPILE) -o $@ $(EXEC_SRCS)
fuzz_diff: $(FUZZ_SRCS) $(ENGINE_HDRS)
	$(COMPILE) -o $@ $(FUZZ_SRCS)
test_alarm: $(ALARM_SRCS) $(ENGINE_HDRS)
	$(COMPILE) -o $@ $(ALARM_SRCS) -lpthread
endif

chain: test_chain
	./test_chain

reloc: test_reloc
	./test_reloc

emit_bounds: test_emit_bounds
	./test_emit_bounds

cache: test_cache
	./test_cache

interp: test_interp
	./test_interp

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

# A small default keeps `make test` quick, and the corpus is deterministic,
# so a CI failure reproduces exactly.  Soak with more:
#   DBT_FUZZ_ITERS=20000 DBT_FUZZ_SEED=7 make -C tests/dbt fuzz
alarm: test_alarm
	./test_alarm

fuzz: fuzz_diff
ifeq ($(HOST_BACKEND),)
	@echo "==> Skipping DBT fuzz (unsupported host '$(ARCH)')"
else
	./fuzz_diff
endif

test: emit_bounds reloc chain cache interp exec fuzz alarm

clean:
	rm -f *.o *.d test_chain test_cache test_interp dbt_test fuzz_diff \
	      test_reloc test_emit_bounds

# Generated by -MMD; absent on the first build, hence the leading '-'.
-include $(wildcard *.d)

.PHONY: all test chain cache interp exec fuzz reloc emit_bounds clean
