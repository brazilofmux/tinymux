# Standalone Makefile for Hydra persistent connection proxy
#
# Usage:
#   cd mux/proxy && make                  # build hydra with gRPC
#   cd mux/proxy && make GRPC=0           # build hydra without gRPC
#   cd mux/proxy && make clean            # remove objects
#   cd mux/proxy && make install          # copy hydra + libmux.so to game/bin
#
# Prerequisites:
#   - mux must be configured and built first (libmux.so, libganl.a, libsqlite3.a)
#   - System packages: libssl-dev, nlohmann-json3-dev, libsqlite3-dev
#   - For gRPC: libgrpc++-dev, protobuf-compiler-grpc, libprotobuf-dev

# Default target (must be first to avoid proto rules taking precedence)
.DEFAULT_GOAL := all

# Paths relative to this directory (mux/proxy/)
MUX_TOP     = ..
INCLUDE_DIR = $(MUX_TOP)/include
GANL_INC    = $(MUX_TOP)/ganl/include
SQLITE_DIR  = $(MUX_TOP)/sqlite
LIB_DIR     = $(MUX_TOP)/lib
GANL_LIB    = $(MUX_TOP)/ganl/libganl.a
SQLITE_LIB  = $(SQLITE_DIR)/libsqlite3.a
GAME_BIN    = $(MUX_TOP)/game/bin

CXX      = g++
CXXSTD   = -std=c++17

# gRPC support (default: on)
GRPC ?= 1

CPPFLAGS = -I$(INCLUDE_DIR) -I$(GANL_INC) -I$(SQLITE_DIR) \
           -DSSL_ENABLED -D_FORTIFY_SOURCE=2

CXXFLAGS = $(CXXSTD) -g -O2 -fPIC \
           -fstack-protector-strong -Wformat -Wformat-security

LDFLAGS  = -L$(LIB_DIR) \
           -Wl,-rpath,'$$ORIGIN' \
           -Wl,-rpath,$(abspath $(LIB_DIR)) \
           -pie \
           -Wl,-z,relro,-z,now

LDLIBS   = -lmux $(GANL_LIB) $(SQLITE_LIB) \
           -lssl -lcrypto -lcrypt -lpthread

# Source files (core)
SRCS = \
    hydra_main.cpp \
    config.cpp \
    hydra_log.cpp \
    base64.cpp \
    session_manager.cpp \
    account_manager.cpp \
    telnet_bridge.cpp \
    scrollback.cpp \
    crypto.cpp \
    process_manager.cpp \
    websocket.cpp

# gRPC support
ifeq ($(GRPC),1)
PROTOC          = protoc
GRPC_CPP_PLUGIN = /usr/bin/grpc_cpp_plugin

CPPFLAGS += -DGRPC_ENABLED $(shell pkg-config --cflags grpc++ protobuf)
LDLIBS   += $(shell pkg-config --libs grpc++ protobuf)

SRCS += grpc_server.cpp grpc_web.cpp

# Protobuf-generated sources
PROTO_SRCS = hydra.pb.cpp hydra.grpc.pb.cpp
PROTO_HDRS = hydra.pb.h hydra.grpc.pb.h

SRCS += $(PROTO_SRCS)

hydra.pb.cpp hydra.pb.h: hydra.proto
	$(PROTOC) --cpp_out=. $<
	mv hydra.pb.cc hydra.pb.cpp

hydra.grpc.pb.cpp hydra.grpc.pb.h: hydra.proto
	$(PROTOC) --grpc_out=. --plugin=protoc-gen-grpc=$(GRPC_CPP_PLUGIN) $<
	mv hydra.grpc.pb.cc hydra.grpc.pb.cpp

# Files that #include proto headers must wait for codegen
session_manager.o grpc_server.o grpc_web.o: $(PROTO_HDRS)
endif

OBJS = $(SRCS:.cpp=.o)
TARGET = hydra

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)
ifeq ($(GRPC),1)
	rm -f $(PROTO_SRCS) $(PROTO_HDRS)
endif

install: $(TARGET)
	cp $(TARGET) $(GAME_BIN)/
	cd $(GAME_BIN) && rm -f libmux.so && ln -s $(abspath $(LIB_DIR))/libmux.so .
