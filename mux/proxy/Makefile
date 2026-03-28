# Standalone Makefile for Hydra persistent connection proxy
#
# Usage:
#   cd mux/proxy && make          # build hydra
#   cd mux/proxy && make clean    # remove objects
#   cd mux/proxy && make install  # copy hydra + libmux.so to game/bin
#
# Prerequisites:
#   - mux must be configured and built first (libmux.so, libganl.a, libsqlite3.a)
#   - System packages: libssl-dev, nlohmann-json3-dev, libsqlite3-dev

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

# Source files (non-gRPC)
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

install: $(TARGET)
	cp $(TARGET) $(GAME_BIN)/
	cd $(GAME_BIN) && rm -f libmux.so && ln -s $(abspath $(LIB_DIR))/libmux.so .
