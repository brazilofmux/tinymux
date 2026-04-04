# Makefile.am for libmdbx amalgamation
#
# Builds mdbx.o as a convenience library for engine.so.

noinst_LIBRARIES = libmdbx.a
libmdbx_a_SOURCES = mdbx.c
libmdbx_a_CFLAGS = $(AM_CFLAGS) -fPIC -fvisibility=hidden -std=gnu11
