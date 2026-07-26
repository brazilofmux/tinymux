# Makefile — unit tests for the DBT block cache (dbt_cache_insert /
# dbt_cache_lookup, #1153).
#
# Links dbt.cpp against a driver that stubs the four dbt_backend_* symbols
# the translation unit references.  dbt.cpp is compiled here directly rather
# than taken from the engine build, so this test needs no prior
# `make install` and no --enable-jit.
#
# Build: make
# Run:   make test

CXX       = g++
CXXFLAGS  = -std=c++17 -g -O2
TESTFLAGS = -Wall -Wextra
INCDIR    = ../../mux/include
ENGINE    = ../../mux/modules/engine
DEFS      = -DHAVE_CONFIG_H -DTINYMUX_JIT

TARGET = test_dbt_cache
OBJS   = test_dbt_cache.o dbt_shared.o

all: $(TARGET)

test_dbt_cache.o: test_dbt_cache.cpp
	$(CXX) $(CXXFLAGS) $(TESTFLAGS) $(DEFS) -I$(INCDIR) -c -o $@ $<

# Code under test, built under the engine's own warning posture (see the
# same note in tests/dbt_chain/Makefile).
dbt_shared.o: $(ENGINE)/dbt.cpp
	$(CXX) $(CXXFLAGS) $(DEFS) -I$(INCDIR) -c -o $@ $<

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f *.o $(TARGET)

.PHONY: all test clean
