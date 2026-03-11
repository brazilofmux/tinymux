# Makefile.am for engine.so
#
# Game logic shared library.  These files have no #include "interface.h"
# and no direct DESC access.  They call driver functions through
# conn_bridge.cpp (COM delegation).
#
# -fvisibility=hidden: all symbols hidden unless marked DCL_EXPORT.
# Only COM front-door functions are exported (mux_Register, etc.).

AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/ganl/include -I$(top_srcdir)/sqlite \
    $(WOD_REALMS) $(REALITY_LVLS) $(STUB_SLAVE) \
    $(SELFCHECK) $(INLINESQL) $(SQL_INCLUDE) $(SSL) $(OPENSSL_CFLAGS)

ENGINE_CXX_SRC = ast.cpp ast_scan.cpp attrcache.cpp boolexp.cpp \
    command.cpp comsys.cpp conf.cpp conn_bridge.cpp cque.cpp create.cpp cron.cpp \
    db.cpp db_rw.cpp engine.cpp engine_com.cpp eval.cpp file_c.cpp flags.cpp \
    funceval.cpp funceval2.cpp functions.cpp funmath.cpp funcweb.cpp help.cpp htab.cpp \
    local.cpp log.cpp look.cpp mail.cpp match.cpp mguests.cpp \
    move.cpp object.cpp predicates.cpp player.cpp player_c.cpp \
    plusemail.cpp powers.cpp quota.cpp rob.cpp set.cpp \
    dbt.cpp dbt_interp.cpp dbt_elf64.cpp session.cpp speech.cpp timer.cpp \
    unparse.cpp vattr.cpp walkdb.cpp wild.cpp wiz.cpp

# Add reality levels if enabled
if REALITY_ENABLED
ENGINE_CXX_SRC += levels.cpp
endif

# SQLite storage backend (always enabled)
ENGINE_CXX_SRC += sqlitedb.cpp sqlite_backend.cpp

ENGINE_CXX_OBJS = $(ENGINE_CXX_SRC:.cpp=.eo)

all-local: engine.so

# Engine uses .eo suffix to distinguish from libmux's .lo objects.
%.eo: %.cpp
	$(CXX) $(CXXFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS) $(DYNAMICLIB_CXXFLAGS) -fvisibility=hidden -c -o $@ $<

engine.so: $(ENGINE_CXX_OBJS) $(top_builddir)/sqlite/libsqlite3.a $(top_builddir)/lib/libmux.so
	$(CXX) $(CXXFLAGS) $(DYNAMICLIB_CXXFLAGS) -Wl,--no-undefined \
	    -Wl,-soname,engine.so \
	    -Wl,-rpath,'$$ORIGIN' -Wl,-rpath,$(abs_top_builddir)/lib \
	    -o $@ $(ENGINE_CXX_OBJS) \
	    $(top_builddir)/sqlite/libsqlite3.a \
	    -L$(top_builddir)/lib -lmux \
	    $(LIBS) $(PCRE2_LIBS) $(OPENSSL_LIBS)

# Ragel scanner generation
ast_scan.cpp: ast_scan.rl $(top_srcdir)/include/ast.h
	ragel -G2 -o $@ $<

BUILT_SOURCES = ast_scan.cpp

CLEANFILES = engine.so *.eo

EXTRA_DIST = ast_scan.rl $(ENGINE_CXX_SRC)
