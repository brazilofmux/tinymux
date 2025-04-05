# Makefile.am for GANL (Game/Graphics Abstraction Network Library)

# Include paths
AM_CPPFLAGS = -I$(top_srcdir) -I$(srcdir)/include -I$(srcdir)

# Library definition
noinst_LIBRARIES = libganl.a

# Common source files for libganl.a
libganl_a_SOURCES = \
    src/connection.cpp \
    src/io_buffer.cpp \
    src/network_address.cpp \
    src/network_engine_factory.cpp \
    src/openssl_transport.cpp \
    src/secure_transport_factory.cpp \
    src/select_network_engine.cpp \
    src/telnet_protocol_handler.cpp

# Platform-specific source files
if HAVE_EPOLL
libganl_a_SOURCES += src/epoll_network_engine.cpp
endif

if HAVE_KQUEUE
libganl_a_SOURCES += src/kqueue_network_engine.cpp
endif

# Header files (for distribution)
noinst_HEADERS = \
    include/connection.h \
    include/epoll_network_engine.h \
    include/io_buffer.h \
    include/kqueue_network_engine.h \
    include/network_engine_factory.h \
    include/network_engine.h \
    include/network_types.h \
    include/openssl_transport.h \
    include/protocol_handler.h \
    include/secure_transport_factory.h \
    include/secure_transport.h \
    include/select_network_engine.h \
    include/session_manager.h \
    include/telnet_protocol_handler.h
