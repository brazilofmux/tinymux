# Makefile.am for Hydra persistent connection proxy
#
# Standalone binary.  Links against libmux.so + libganl.a.
# Does NOT link against engine.so.

AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/ganl/include -I$(top_srcdir)/sqlite \
    $(SSL) $(OPENSSL_CFLAGS) $(GRPC_ENABLED)

bin_PROGRAMS = hydra

hydra_SOURCES = \
    hydra_main.cpp \
    config.cpp \
    hydra_log.cpp \
    session_manager.cpp \
    account_manager.cpp \
    telnet_bridge.cpp \
    scrollback.cpp \
    crypto.cpp \
    process_manager.cpp \
    websocket.cpp

# gRPC support (optional, requires --enable-grpc at configure time)
if GRPC_ENABLED
hydra_SOURCES += grpc_server.cpp grpc_web.cpp

BUILT_SOURCES = hydra.grpc.pb.cc hydra.grpc.pb.h hydra.pb.cc hydra.pb.h

hydra.pb.cc hydra.pb.h: hydra.proto
	$(PROTOC) --cpp_out=. $<

hydra.grpc.pb.cc hydra.grpc.pb.h: hydra.proto
	$(PROTOC) --grpc_out=. --plugin=protoc-gen-grpc=$(GRPC_CPP_PLUGIN) $<

CLEANFILES = hydra.pb.cc hydra.pb.h hydra.grpc.pb.cc hydra.grpc.pb.h

AM_CPPFLAGS += $(GRPC_CFLAGS)
endif

hydra_CXXFLAGS = $(AM_CXXFLAGS) -fPIC -std=c++17 \
    -fstack-protector-strong -Wformat -Wformat-security

hydra_CPPFLAGS = $(AM_CPPFLAGS) -D_FORTIFY_SOURCE=2

hydra_LDFLAGS = -L$(top_builddir)/lib \
    -Wl,-rpath,'$$ORIGIN' \
    -Wl,-rpath,$(abs_top_builddir)/lib \
    -pie \
    -Wl,-z,relro,-z,now

hydra_LDADD = -lmux $(top_builddir)/ganl/libganl.a \
    $(top_builddir)/sqlite/libsqlite3.a \
    $(LIBS) $(OPENSSL_LIBS) -lpthread

if GRPC_ENABLED
hydra_LDADD += $(GRPC_LIBS)
endif

# Installation — same bin directory as netmux
bindir = $(abs_top_srcdir)/game/bin

install-exec-hook:
	cd $(DESTDIR)$(bindir) && rm -f libmux.so && $(LN_S) $(abs_top_builddir)/lib/libmux.so .
