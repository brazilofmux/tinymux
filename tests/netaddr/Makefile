# Makefile — unit tests for mux_subnet::compare_to (the access-control
# subnet comparator; #799).
#
# Links the netmux-side netaddr object against libmux with driver-global
# stubs (in test_netaddr.cpp).  Needs a built netmux:
#   make install   (from the repo root) produces mux/src/netmux-netaddr.o
#
# Build: make
# Run:   make test

CXX      = g++
CXXFLAGS = -std=c++17 -g -O2 -Wall -Wextra -fPIC
INCDIR   = ../../mux/include
LIBDIR   = ../../mux/lib
NETADDR_O = ../../mux/src/netmux-netaddr.o

TARGET   = test_netaddr

all: $(TARGET)

test_netaddr.o: test_netaddr.cpp
	$(CXX) $(CXXFLAGS) -DHAVE_CONFIG_H -I$(INCDIR) -c -o $@ $<

$(TARGET): test_netaddr.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(NETADDR_O) -L$(LIBDIR) -lmux -Wl,-rpath,$(LIBDIR)

# Prepend LIBDIR so the freshly-built libmux.so wins over any ambient
# LD_LIBRARY_PATH shadow (same rationale as tests/libmux).
test: $(TARGET)
	LD_LIBRARY_PATH=$(LIBDIR):$$LD_LIBRARY_PATH ./$(TARGET)

clean:
	rm -f *.o $(TARGET)

.PHONY: all test clean
