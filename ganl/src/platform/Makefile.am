if LINUX
SUBDIRS = epoll select factory
endif

if BSD
SUBDIRS = kqueue select factory
endif

noinst_LIBRARIES = libganlplatform.a

if LINUX
libganlplatform_a_SOURCES = \
  epoll/epoll_network_engine.cpp \
  select/select_network_engine.cpp \
  factory/network_engine_factory.cpp
endif

if BSD
libganlplatform_a_SOURCES = \
  kqueue/kqueue_network_engine.cpp \
  select/select_network_engine.cpp \
  factory/network_engine_factory.cpp
endif

libganlplatform_a_CPPFLAGS = -I$(top_srcdir)/include
