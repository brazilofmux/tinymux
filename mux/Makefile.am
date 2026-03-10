# Makefile.am for TinyMUX 2.14
#
# Build root.  Subdirectories are built in dependency order:
#   ganl     — networking library (static, no deps)
#   sqlite   — SQLite amalgamation (static, no deps)
#   lib      — libmux.so (core utilities)
#   src      — netmux driver (links libmux.so + libganl.a)
#   modules  — engine.so + external modules (link libmux.so)
#   announce — standalone announce tool

SUBDIRS = ganl sqlite lib src modules announce

# Shared build flags available to all subdirectories.
AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/ganl/include -I$(top_srcdir)/sqlite \
    $(WOD_REALMS) $(REALITY_LVLS) $(STUB_SLAVE) \
    $(SELFCHECK) $(INLINESQL) $(SQL_INCLUDE) $(SSL) $(OPENSSL_CFLAGS)

# Version number routines — buildnum.sh lives here at the build root.
EXTRA_DIST = buildnum.sh
