# Makefile — unit tests for mux_table_* (#1667 Phase 3).
#
# Needs a built tree: make install from repo root (libmux.so in mux/lib).

CXX      = g++
CXXFLAGS = -std=c++17 -g -O2 -Wall -Wextra -fPIC
INCDIR   = ../../mux/include
LIBDIR   = ../../mux/lib

TARGET   = test_table

all: $(TARGET)

test_table.o: test_table.cpp
	$(CXX) $(CXXFLAGS) -DHAVE_CONFIG_H -I$(INCDIR) -c -o $@ $<

$(TARGET): test_table.o
	$(CXX) $(CXXFLAGS) -o $@ $^ -L$(LIBDIR) -lmux -Wl,-rpath,$(LIBDIR)

test: $(TARGET)
	LD_LIBRARY_PATH=$(LIBDIR):$$LD_LIBRARY_PATH ./$(TARGET)

clean:
	rm -f $(TARGET) test_table.o

.PHONY: all test clean
