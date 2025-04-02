bin_PROGRAMS = echo_server

echo_server_SOURCES = main.cpp

echo_server_CPPFLAGS = -I$(top_srcdir)/include
echo_server_LDADD = \
  $(top_builddir)/src/common/libganlcommon.a \
  $(top_builddir)/src/platform/libganlplatform.a \
  $(top_builddir)/src/ssl/libganlssl.a \
  $(top_builddir)/src/protocol/libganlprotocol.a
