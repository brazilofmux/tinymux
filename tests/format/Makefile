# Makefile — differential tests for mux_vsnprintf against the platform's
# snprintf (#1382 follow-up).
#
# mux_vsnprintf implements printf conversions by hand and, until recently,
# aborted the process via mux_assert(0) on any conversion it did not know --
# which is how %f took the server down twice.  %i, %o and the floating-point
# conversions are now implemented, with digits from mux_dtoa rather than the
# host libc.  Hand-assembled float formatting needs an oracle, so these tests
# compare against snprintf over a wide corpus.
#
# Links against libmux, which supplies mux_vsnprintf, mux_dtoa and mux_utoo.
# Needs a built tree:  make install   (from the repo root)
#
# Build: make
# Run:   make test

CXX      = g++
CXXFLAGS = -std=c++17 -g -O2 -Wall -Wextra -fPIC
INCDIR   = ../../mux/include
LIBDIR   = ../../mux/lib

TARGET   = test_format

all: $(TARGET)

test_format.o: test_format.cpp
	$(CXX) $(CXXFLAGS) -DHAVE_CONFIG_H -I$(INCDIR) -c -o $@ $<

$(TARGET): test_format.o
	$(CXX) $(CXXFLAGS) -o $@ $^ -L$(LIBDIR) -lmux -Wl,-rpath,$(LIBDIR)

# The source guard runs first and is independent of the built binary: it is
# the half that catches a BAD FORMAT BEING ADDED, which the runtime tests
# cannot see.  Since #1429 an unsupported spec is echoed and the rest of the
# message dropped rather than aborting -- quieter in production, and quieter
# is exactly why the noise has to happen here.
test: $(TARGET)
	python3 ./check_formats.py
	LD_LIBRARY_PATH=$(LIBDIR):$$LD_LIBRARY_PATH ./$(TARGET)

clean:
	rm -f $(TARGET) test_format.o

.PHONY: all test clean
