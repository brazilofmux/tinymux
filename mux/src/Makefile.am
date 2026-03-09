# Makefile.am for TinyMUX 2.14

SUBDIRS = tools ganl . modules

# Shared build flags
AM_CPPFLAGS = -I$(srcdir)/ganl/include $(WOD_REALMS) $(REALITY_LVLS) $(STUB_SLAVE) \
    $(SELFCHECK) $(INLINESQL) $(SQL_INCLUDE) $(SSL) $(OPENSSL_CFLAGS)

# Source files for libmux.so — core utilities with no game-state dependency.
# These files include core.h (not externs.h) and are shared between netmux,
# stubslave, and modules.
#
LIBMUX_SRC = libmux.cpp \
    sha1.cpp svdrand.cpp svdhash.cpp \
    timeutil.cpp timeabsolute.cpp timedelta.cpp timeparser.cpp timezone.cpp \
    utf8_collate.cpp utf8_grapheme.cpp utf8_normalize.cpp utf8tables.cpp \
    strtod.cpp alarm.cpp mathutil.cpp stringutil.cpp alloc.cpp

# Source files for netmux — game server with full externs.h dependency.
#
NETMUX_SRC = _build.cpp ast.cpp ast_scan.cpp attrcache.cpp boolexp.cpp \
    bsd.cpp command.cpp comsys.cpp conf.cpp cque.cpp create.cpp db.cpp \
    db_rw.cpp eval.cpp file_c.cpp flags.cpp funceval.cpp funceval2.cpp \
    functions.cpp funmath.cpp game.cpp ganl_adapter.cpp help.cpp htab.cpp \
    local.cpp log.cpp look.cpp mail.cpp match.cpp mguests.cpp \
    modules.cpp move.cpp muxcli.cpp netaddr.cpp net.cpp object.cpp \
    predicates.cpp player.cpp player_c.cpp plusemail.cpp powers.cpp \
    quota.cpp rob.cpp set.cpp signals.cpp sitemon.cpp \
    session.cpp speech.cpp telnet.cpp timer.cpp \
    unparse.cpp vattr.cpp \
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

# libmux shared library (modules resolve symbols from this at runtime)
all-local: libmux.so

LIBMUX_OBJS = $(LIBMUX_SRC:.cpp=.lo)

# Compile each libmux source with -fPIC
%.lo: %.cpp
	$(CXX) $(CXXFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS) $(DYNAMICLIB_CXXFLAGS) -c -o $@ $<

libmux.so: $(LIBMUX_OBJS)
	$(CXX) $(CXXFLAGS) $(DYNAMICLIB_CXXFLAGS) -o $@ $(LIBMUX_OBJS) $(LIBS) $(DL_LIB) $(SQL_LIBS)

# netmux main program
netmux_SOURCES = $(NETMUX_SRC) version.cpp
netmux_LDADD = libmux.so ganl/libganl.a $(LIBS) $(SQL_LIBS) $(PCRE2_LIBS) $(OPENSSL_LIBS)

# slave program
slave_SOURCES = slave.cpp
slave_LDADD = $(LIBS)

# stubslave program
stubslave_SOURCES = stubslave.cpp
stubslave_LDADD = libmux.so $(LIBS) $(STUBLIBS)

# Version number routines
# The DEBIAN_BUILD environment variable can be set to create deterministic builds
# Example: DEBIAN_BUILD=1 ./configure && make
version.o: version.cpp FORCE
	if [ "x$(DEBIAN_BUILD)" = "x1" ]; then \
		$(CXX) $(CXXFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS) -DMUX_BUILD_DATE="\"(Debian build)\"" -DMUX_BUILD_NUM="\"0\"" -c -o $@ $<; \
	else \
		$(CXX) $(CXXFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS) -DMUX_BUILD_DATE="\"`date`\"" -DMUX_BUILD_NUM="\"`sh ./buildnum.sh`\"" -c -o $@ $<; \
	fi

.PHONY: FORCE
FORCE:

# Installation
bindir = $(abs_top_srcdir)/../game/bin

# Custom install-exec-hook to create symlinks
install-exec-hook:
	cd $(DESTDIR)$(bindir) && rm -f dbconvert && ln -s netmux dbconvert
	cd $(DESTDIR)$(bindir) && rm -f libmux.so && $(LN_S) $(abs_builddir)/libmux.so .

# No need for manual dependency tracking - automake handles this now

# Ragel scanner generation
ast_scan.cpp: ast_scan.rl ast.h
	ragel -G2 -o $@ $<

BUILT_SOURCES = ast_scan.cpp

# Clean up additional files
CLEANFILES = libmux.so *.lo

# Additional distributions
EXTRA_DIST = buildnum.sh ast_scan.rl
