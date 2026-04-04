# Makefile.am for engine.so
#
# Game logic shared library.  These files have no #include "interface.h"
# and no direct DESC access.  They call driver functions through
# conn_bridge.cpp (COM delegation).
#
# -fvisibility=hidden: all symbols hidden unless marked DCL_EXPORT.
# Only COM front-door functions are exported (mux_Register, etc.).

AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/ganl/include -I$(top_srcdir)/sqlite \
    -I$(top_srcdir)/libmdbx -I$(top_srcdir)/lua54 \
    $(WOD_REALMS) $(REALITY_LVLS) $(STUB_SLAVE) $(TINYMUX_JIT) \
    $(SELFCHECK) $(INLINESQL) $(SQL_INCLUDE) $(SSL) $(OPENSSL_CFLAGS)

ENGINE_CXX_SRC = art_scan.cpp ast.cpp ast_scan.cpp attrcache.cpp boolexp.cpp \
    command.cpp comsys.cpp conf.cpp conn_bridge.cpp cque.cpp create.cpp cron.cpp \
    db.cpp db_rw.cpp engine.cpp engine_com.cpp eval.cpp file_c.cpp flags.cpp \
    funceval.cpp funceval2.cpp functions.cpp funmath.cpp funcweb.cpp help.cpp htab.cpp \
    local.cpp log.cpp look.cpp mail.cpp match.cpp mguests.cpp \
    move.cpp object.cpp predicates.cpp player.cpp player_c.cpp routing.cpp \
    plusemail.cpp powers.cpp quota.cpp rob.cpp set.cpp \
    dbt.cpp @DBT_BACKEND@.cpp hir_lower.cpp hir_codegen.cpp jit_compiler.cpp dbt_interp.cpp dbt_elf64.cpp \
    hir_ssa.cpp hir_opt.cpp lua_bytecode.cpp hir_lower_lua.cpp jit_lua.cpp \
    lua_mod.cpp session.cpp speech.cpp timer.cpp walk.cpp \
    unparse.cpp vattr.cpp walkdb.cpp wild.cpp wiz.cpp

# Add reality levels if enabled
if REALITY_ENABLED
ENGINE_CXX_SRC += levels.cpp
endif

# Storage backends (SQLite always enabled; libmdbx for tiered storage)
ENGINE_CXX_SRC += sqlitedb.cpp sqlite_backend.cpp mdbx_backend.cpp

ENGINE_CXX_OBJS = $(ENGINE_CXX_SRC:.cpp=.eo)

all-local: engine.so

# Engine uses .eo suffix to distinguish from libmux's .lo objects.
# -MMD -MF generates per-object dependency files for header tracking.
%.eo: %.cpp
	$(CXX) $(CXXFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS) $(DYNAMICLIB_CXXFLAGS) -fvisibility=hidden -MMD -MF $(@:.eo=.d) -c -o $@ $<

-include $(ENGINE_CXX_OBJS:.eo=.d)

engine.so: $(ENGINE_CXX_OBJS) $(top_builddir)/sqlite/libsqlite3.a $(top_builddir)/libmdbx/libmdbx.a $(top_builddir)/lua54/liblua54.a $(top_builddir)/lib/libmux.so
	$(CXX) $(CXXFLAGS) $(DYNAMICLIB_CXXFLAGS) -Wl,--no-undefined \
	    -Wl,-soname,engine.so \
	    -Wl,-rpath,'$$ORIGIN' -Wl,-rpath,$(abs_top_builddir)/lib \
	    -o $@ $(ENGINE_CXX_OBJS) \
	    $(top_builddir)/sqlite/libsqlite3.a \
	    $(top_builddir)/libmdbx/libmdbx.a \
	    $(top_builddir)/lua54/liblua54.a \
	    -L$(top_builddir)/lib -lmux \
	    $(LIBS) $(PCRE2_LIBS) $(OPENSSL_LIBS)

# Ragel scanner generation — output is read-only to prevent accidental edits.
art_scan.cpp: art_scan.rl art_scan.h
	chmod u+w $@ 2>/dev/null || true
	ragel -G2 -o $@ $<
	chmod a-w $@

ast_scan.cpp: ast_scan.rl $(top_srcdir)/include/ast.h
	chmod u+w $@ 2>/dev/null || true
	ragel -G2 -o $@ $<
	chmod a-w $@

BUILT_SOURCES = art_scan.cpp ast_scan.cpp

CLEANFILES = engine.so *.eo *.d

EXTRA_DIST = art_scan.rl ast_scan.rl $(ENGINE_CXX_SRC)
