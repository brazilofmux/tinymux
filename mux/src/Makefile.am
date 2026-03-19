# Makefile.am for TinyMUX 2.13

SUBDIRS = tools ganl . modules

# Shared build flags
AM_CPPFLAGS = -I$(srcdir)/ganl/include $(WOD_REALMS) $(REALITY_LVLS) $(STUB_SLAVE) \
    $(SELFCHECK) $(INLINESQL) $(SQL_INCLUDE) $(SSL) $(OPENSSL_CFLAGS)

# Base source files for building netmux
NETMUX_SRC = _build.cpp alarm.cpp alloc.cpp attrcache.cpp boolexp.cpp \
    bsd.cpp command.cpp comsys.cpp conf.cpp cque.cpp create.cpp db.cpp \
    db_rw.cpp eval.cpp file_c.cpp flags.cpp funceval.cpp funceval2.cpp \
    functions.cpp funmath.cpp game.cpp ganl_adapter.cpp help.cpp htab.cpp \
    local.cpp log.cpp look.cpp mail.cpp match.cpp mathutil.cpp mguests.cpp \
    modules.cpp move.cpp muxcli.cpp netaddr.cpp netcommon.cpp object.cpp \
    predicates.cpp player.cpp player_c.cpp plusemail.cpp powers.cpp \
    quota.cpp rob.cpp set.cpp sha1.cpp signals.cpp sitemon.cpp \
    speech.cpp stringutil.cpp strtod.cpp svdrand.cpp svdhash.cpp \
    telnet.cpp timer.cpp timeabsolute.cpp timedelta.cpp timeparser.cpp \
    timeutil.cpp timezone.cpp unparse.cpp utf8_collate.cpp utf8_grapheme.cpp utf8_normalize.cpp utf8tables.cpp vattr.cpp \
    walkdb.cpp wild.cpp wiz.cpp

# Add reality levels if enabled
if REALITY_ENABLED
NETMUX_SRC += levels.cpp
endif

# SQLite storage backend (always enabled)
NETMUX_SRC += sqlitedb.cpp sqlite_backend.cpp sqlite/sqlite3.c

# Build executables
bin_PROGRAMS = netmux slave

if STUBSLAVE_ENABLED
bin_PROGRAMS += stubslave
endif

# Dynamic library (for modules) — must be built before netmux
all-local: libmux.so

libmux.so: libmux.cpp
	$(CXX) $(CXXFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS) $(DYNAMICLIB_CXXFLAGS) -o $@ $< $(LIBS) $(DL_LIB) $(SQL_LIBS)

# netmux main program
netmux_SOURCES = $(NETMUX_SRC) version.cpp
netmux_LDADD = libmux.so ganl/libganl.a $(LIBS) $(SQL_LIBS) $(PCRE2_LIBS) $(OPENSSL_LIBS)

# slave program
slave_SOURCES = slave.cpp
slave_LDADD = $(LIBS)

# stubslave program
stubslave_SOURCES = stubslave.cpp
stubslave_LDADD = libmux.so $(LIBS) $(STUBLIBS)

# Installation
bindir = $(abs_top_srcdir)/../game/bin

# Custom install-exec-hook to create symlinks
install-exec-hook:
	cd $(DESTDIR)$(bindir) && rm -f dbconvert && ln -s netmux dbconvert
	cd $(DESTDIR)$(bindir) && rm -f libmux.so && $(LN_S) $(abs_builddir)/libmux.so .

# Clean up additional files
CLEANFILES = libmux.so

EXTRA_DIST =
