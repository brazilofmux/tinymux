# GANL engine + ConnectionBase regression harness (POSIX side).
#
# Linux:      epoll + select engines
# macOS/BSD:  kqueue + select engines
# Windows:    no make — run mux/ganl/tests/run-msvc.bat (builds ganl_tests +
#             ganl_connection_tests via MSBuild; covers wselect/iocp + fakes).
#
# Invoked as `make test-ganl` (part of `make test`) from the repo root,
# or directly: `make -C mux/ganl/tests check`.
#
# Engines print debug logging to stderr in non-NDEBUG builds; `check`
# redirects stderr to ganl_tests.err / ganl_connection_tests.err.

UNAME_S := $(shell uname -s)

CXX ?= g++
CXXFLAGS ?= -std=c++17 -g -O1
CPPFLAGS += -I../include

ENGINE_SRCS = ganl_engine_tests.cpp \
       ../src/select_network_engine.cpp \
       ../src/io_buffer.cpp \
       ../src/network_address.cpp \
       ../src/slave_spawn_posix.cpp

CONN_SRCS = ganl_connection_tests.cpp \
       ../src/connection.cpp \
       ../src/select_network_engine.cpp \
       ../src/io_buffer.cpp \
       ../src/network_address.cpp \
       ../src/slave_spawn_posix.cpp

ifeq ($(UNAME_S),Linux)
ENGINE_SRCS += ../src/epoll_network_engine.cpp
else
ENGINE_SRCS += ../src/kqueue_network_engine.cpp
endif

HDRS = $(wildcard ../include/*.h)

.PHONY: all check clean

all: ganl_engine_tests ganl_connection_tests

ganl_engine_tests: $(ENGINE_SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(ENGINE_SRCS) -o $@

ganl_connection_tests: $(CONN_SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(CONN_SRCS) -o $@

check: ganl_engine_tests ganl_connection_tests
	./ganl_engine_tests 2> ganl_tests.err
	./ganl_connection_tests 2> ganl_connection_tests.err

clean:
	rm -rf ganl_engine_tests ganl_connection_tests \
	       ganl_engine_tests.dSYM ganl_connection_tests.dSYM \
	       ganl_tests.err ganl_connection_tests.err
