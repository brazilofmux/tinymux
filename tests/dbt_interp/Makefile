# Makefile — guest memory bounds checks for the RV64 interpreter (#1292).
#
# mem_check() and the mem_read*/mem_write* accessors are file-static, so the
# test #includes dbt_interp.cpp rather than linking against it.  That means a
# single translation unit and no stubs: dbt_interp.cpp depends only on
# dbt_interp.h, dbt_decoder.h and the standard library.
#
# Compiled here directly rather than taken from the engine build, so this
# needs no prior `make install` and no --enable-jit, and has no skip path.
#
# Build: make
# Run:   make test

CXX       = g++
CXXFLAGS  = -std=c++17 -g -O2
INCDIR    = ../../mux/include
ENGINE    = ../../mux/modules/engine
DEFS      = -DHAVE_CONFIG_H -DTINYMUX_JIT

TARGET = test_dbt_interp
OBJS   = test_dbt_interp.o

all: $(TARGET)

# The engine's own warning posture applies here: the test TU includes the
# code under test, so -Wall -Wextra would report on engine source rather
# than on the test (same reasoning as tests/dbt_cache's dbt_shared.o).
test_dbt_interp.o: test_dbt_interp.cpp $(ENGINE)/dbt_interp.cpp
	$(CXX) $(CXXFLAGS) $(DEFS) -I$(INCDIR) -I$(ENGINE) -c -o $@ $<

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f *.o $(TARGET)

.PHONY: all test clean
