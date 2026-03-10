# Makefile.am for SQLite amalgamation
#
# Builds sqlite3.o as a convenience library for engine.so.

noinst_LIBRARIES = libsqlite3.a
libsqlite3_a_SOURCES = sqlite3.c
libsqlite3_a_CFLAGS = $(AM_CFLAGS) -fPIC -fvisibility=hidden
