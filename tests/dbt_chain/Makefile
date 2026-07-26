# Makefile — unit tests for the DBT block-chaining patch encode/decode pair
# (dbt_backend_backpatch_jmp / dbt_backend_decode_jmp_target, #1152).
#
# All three backends are built into one test binary, on every host, so that
# no backend's encoding can go untested the way x64_win64 otherwise would
# outside Windows.  Each backend .cpp defines the same five strong symbols,
# so each is compiled with those five renamed on the command line; the
# rename rewrites both the declaration (dbt_internal.h) and the definition
# within that translation unit, and needs no change to the shipping source.
#
# The backend sources are compiled here directly rather than linked from the
# engine build, so this test needs no prior `make install` and no
# --enable-jit: it can never degrade into testing nothing.
#
# Build: make
# Run:   make test

CXX      = g++
CXXFLAGS = -std=c++17 -g -O2
TESTFLAGS = -Wall -Wextra
INCDIR   = ../../mux/include
ENGINE   = ../../mux/modules/engine
DEFS     = -DHAVE_CONFIG_H -DTINYMUX_JIT

TARGET   = test_dbt_chain

# The five strong symbols every backend defines.  $(1) is the per-backend
# prefix the test declares in test_dbt_chain.cpp.
rename = -Ddbt_backend_backpatch_jmp=$(1)_backpatch_jmp \
         -Ddbt_backend_decode_jmp_target=$(1)_decode_jmp_target \
         -Ddbt_backend_emit_trampoline=$(1)_emit_trampoline \
         -Ddbt_backend_translate_block=$(1)_translate_block \
         -Ddbt_register_intrinsic=$(1)_register_intrinsic

OBJS = test_dbt_chain.o be_a64.o be_x64sysv.o be_win64.o

all: $(TARGET)

test_dbt_chain.o: test_dbt_chain.cpp
	$(CXX) $(CXXFLAGS) $(TESTFLAGS) $(DEFS) -I$(INCDIR) -c -o $@ $<

# The backend sources are code under test, not code being written here, and
# the engine build already compiles them under its own warning flags.  Adding
# a different posture here would only emit noise unrelated to what this test
# checks, so -Wall -Wextra is applied to the driver alone.
be_a64.o: $(ENGINE)/dbt_a64_sysv.cpp
	$(CXX) $(CXXFLAGS) $(DEFS) -I$(INCDIR) $(call rename,a64) -c -o $@ $<

be_x64sysv.o: $(ENGINE)/dbt_x64_sysv.cpp
	$(CXX) $(CXXFLAGS) $(DEFS) -I$(INCDIR) $(call rename,x64sysv) -c -o $@ $<

be_win64.o: $(ENGINE)/dbt_x64_win64.cpp
	$(CXX) $(CXXFLAGS) $(DEFS) -I$(INCDIR) $(call rename,win64) -c -o $@ $<

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f *.o $(TARGET)

.PHONY: all test clean
