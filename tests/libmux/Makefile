# Makefile — Standalone unit tests for libmux.so
#
# Links against the built libmux.so to test stringutil, mathutil,
# alloc, and SHA1 functions in isolation.
#
# Build: make
# Run:   make test

CXX      = g++
CXXFLAGS = -std=c++17 -g -O2 -Wall -Wextra -fPIC
INCDIR   = ../../mux/include
LIBDIR   = ../../mux/lib

TARGET   = test_libmux

all: $(TARGET)

test_libmux.o: test_libmux.cpp
	$(CXX) $(CXXFLAGS) -I$(INCDIR) -c -o $@ $<

$(TARGET): test_libmux.o
	$(CXX) $(CXXFLAGS) -o $@ $^ -L$(LIBDIR) -lmux -Wl,-rpath,$(LIBDIR)

# Prepend LIBDIR so we load the freshly-built libmux.so here and not a stale
# copy that an ambient LD_LIBRARY_PATH (e.g. ~/lib) might otherwise shadow:
# DT_RUNPATH from -rpath loses to LD_LIBRARY_PATH, so make ours win.
test: $(TARGET)
	LD_LIBRARY_PATH=$(LIBDIR):$$LD_LIBRARY_PATH ./$(TARGET)

clean:
	rm -f *.o $(TARGET)

.PHONY: all test clean
