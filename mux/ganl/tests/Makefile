# GANL engine regression harness (POSIX side).
#
# Linux:      epoll + select engines
# macOS/BSD:  kqueue + select engines
# Windows:    no make — build ganl_tests.vcxproj with MSBuild instead
#             (covers the wselect + iocp engines via the accept path).
#
# Invoked as `make test-ganl` (part of `make test`) from the repo root,
# or directly: `make -C mux/ganl/tests check`.
#
# Engines print debug logging to stderr in non-NDEBUG builds; `check`
# redirects stderr to ganl_tests.err and shows the TAP stdout only.

UNAME_S := $(shell uname -s)

CXX ?= g++
CXXFLAGS ?= -std=c++17 -g -O1
CPPFLAGS += -I../include

SRCS = ganl_engine_tests.cpp \
       ../src/select_network_engine.cpp \
       ../src/io_buffer.cpp \
       ../src/network_address.cpp \
       ../src/slave_spawn_posix.cpp

ifeq ($(UNAME_S),Linux)
SRCS += ../src/epoll_network_engine.cpp
else
SRCS += ../src/kqueue_network_engine.cpp
endif

HDRS = $(wildcard ../include/*.h)

.PHONY: all check clean

all: ganl_engine_tests

ganl_engine_tests: $(SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(SRCS) -o $@

check: ganl_engine_tests
	./ganl_engine_tests 2> ganl_tests.err

clean:
	rm -rf ganl_engine_tests ganl_engine_tests.dSYM ganl_tests.err
