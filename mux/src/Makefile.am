# Makefile.am for TinyMUX 2.13

SUBDIRS = tools ganl

# Build optimizations
AM_CXXFLAGS = -g -O

# Shared build flags
AM_CPPFLAGS = -I$(srcdir)/ganl/include $(MEMORY_BASED) $(WOD_REALMS) $(REALITY_LVLS) $(STUB_SLAVE) $(DEPRECATED) \
    $(SELFCHECK) $(INLINESQL) $(SQL_INCLUDE) $(SSL) $(USE_GANL)

# Base source files for building netmux
NETMUX_SRC = _build.cpp alarm.cpp alloc.cpp attrcache.cpp boolexp.cpp \
    bsd.cpp command.cpp comsys.cpp conf.cpp cque.cpp create.cpp db.cpp \
    db_rw.cpp eval.cpp file_c.cpp flags.cpp funceval.cpp funceval2.cpp \
    functions.cpp funmath.cpp game.cpp help.cpp htab.cpp local.cpp log.cpp \
    look.cpp mail.cpp match.cpp mathutil.cpp mguests.cpp modules.cpp move.cpp \
    muxcli.cpp netcommon.cpp object.cpp predicates.cpp player.cpp player_c.cpp \
    plusemail.cpp powers.cpp quota.cpp rob.cpp set.cpp sha1.cpp \
    speech.cpp stringutil.cpp strtod.cpp svdrand.cpp svdhash.cpp timer.cpp \
    timeabsolute.cpp timedelta.cpp timeparser.cpp timeutil.cpp timezone.cpp \
    unparse.cpp utf8tables.cpp vattr.cpp walkdb.cpp wild.cpp wiz.cpp

# Add reality levels if enabled
if REALITY_ENABLED
NETMUX_SRC += levels.cpp
endif

# Build executables
bin_PROGRAMS = netmux slave
noinst_LIBRARIES = libmux.a

if STUBSLAVE_ENABLED
bin_PROGRAMS += stubslave
endif

# libmux library
libmux_a_SOURCES = libmux.cpp

# netmux main program
netmux_SOURCES = $(NETMUX_SRC) version.cpp
netmux_LDADD = libmux.a ganl/libganl.a $(LIBS) $(SQL_LIBS) $(PCRE2_LIBS)

# slave program
slave_SOURCES = slave.cpp
slave_LDADD = $(LIBS)

# stubslave program
stubslave_SOURCES = stubslave.cpp
stubslave_LDADD = libmux.a $(LIBS) $(STUBLIBS) 

# No unsplit utility anymore

# Dynamic library (for modules)
all-local: libmux.so modules-local

libmux.so: libmux.cpp
	$(CXX) $(AM_CXXFLAGS) $(CXXFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS) $(DYNAMICLIB_CXXFLAGS) -o $@ $< $(LIBS) $(DL_LIB) $(SQL_LIBS)

# Build modules after libmux.so is available
modules-local:
	$(MAKE) -C modules

# Version number routines
# The DEBIAN_BUILD environment variable can be set to create deterministic builds
# Example: DEBIAN_BUILD=1 ./configure && make
version.o: version.cpp
	if [ "x$(DEBIAN_BUILD)" = "x1" ]; then \
		$(CXX) $(AM_CXXFLAGS) $(CXXFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS) -DMUX_BUILD_DATE="\"(Debian build)\"" -DMUX_BUILD_NUM="\"0\"" -c -o $@ $<; \
	else \
		$(CXX) $(AM_CXXFLAGS) $(CXXFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS) -DMUX_BUILD_DATE="\"`date`\"" -DMUX_BUILD_NUM="\"`sh ./buildnum.sh`\"" -c -o $@ $<; \
	fi

# Installation
bindir = $(abs_top_srcdir)/../game/bin

# Custom install-exec-hook to create symlinks
install-exec-hook:
	cd $(DESTDIR)$(bindir) && rm -f dbconvert && ln -s netmux dbconvert
	cd $(DESTDIR)$(bindir) && rm -f libmux.so && ln -s $(abs_builddir)/libmux.so .

# No need for manual dependency tracking - automake handles this now

# Clean up additional files
CLEANFILES = libmux.so

# Clean modules
clean-local:
	$(MAKE) -C modules clean

# Additional distributions
EXTRA_DIST = buildnum.sh
