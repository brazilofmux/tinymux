SUBDIRS = openssl factory

noinst_LIBRARIES = libganlssl.a

libganlssl_a_SOURCES = \
  openssl/openssl_transport.cpp \
  factory/secure_transport_factory.cpp

libganlssl_a_CPPFLAGS = -I$(top_srcdir)/include
