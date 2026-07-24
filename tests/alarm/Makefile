# Makefile — unit tests for mux_alarm (the per-command wall-clock abort).
#
# Guards the lazy worker-thread start: alarm_clock is a libmux global whose
# constructor used to spawn a thread during static initialization, which
# deadlocked before main in ~14% of runs.  See test_alarm.cpp.
#
# Links only against libmux — mux_alarm lives entirely there, so no
# driver-side objects or stubs are needed.
#
# Build: make
# Run:   make test

CXX      = g++
CXXFLAGS = -std=c++17 -g -O2 -Wall -Wextra -fPIC
INCDIR   = ../../mux/include
LIBDIR   = ../../mux/lib

TARGET   = test_alarm

all: $(TARGET)

test_alarm.o: test_alarm.cpp
	$(CXX) $(CXXFLAGS) -DHAVE_CONFIG_H -I$(INCDIR) -c -o $@ $<

$(TARGET): test_alarm.o
	$(CXX) $(CXXFLAGS) -o $@ $^ -L$(LIBDIR) -lmux -Wl,-rpath,$(LIBDIR)

# Prepend LIBDIR so the freshly-built libmux.so wins over any ambient
# LD_LIBRARY_PATH shadow (same rationale as tests/netaddr).
test: $(TARGET)
	LD_LIBRARY_PATH=$(LIBDIR):$$LD_LIBRARY_PATH ./$(TARGET)

clean:
	rm -f *.o $(TARGET)

.PHONY: all test clean
