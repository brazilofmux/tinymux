# Static build: netmux and muxscript with the engine linked into them,
# for a platform without dlopen (and to prove that build on one with it).
#
# Run after the normal build (./configure in mux/, make install from the
# repository root): this reuses its objects and its compiler flags, and
# recompiles only the three files that differ with MUX_STATIC_MODULES --
# libmux.cpp (modules come from a table, not from dlopen), mux_main.cpp
# (no engine.so to check for) and src/static_modules.cpp (the table).
#
# The engine goes in as one object, partially linked from its .eo files
# and the SQLite and Lua it bundles.  The partial link makes every hidden
# symbol local (the engine is compiled -fvisibility=hidden), so the
# engine's own copies of names the driver also defines -- conn_bridge.cpp's
# forwarders for net.cpp's functions, and the rest -- stay as private to it
# as they are inside engine.so, and both sides keep calling their own.  Its
# four entry points are then renamed engine_mux_Register and so on, which
# is what static_modules.cpp's table names, so that the optional modules
# (comsys_mod, mail_mod, exp3, built the same way) share the link.
#
#   cd mux/static && make          # -> netmux, muxscript here
#   make install                   # copies both over game/bin's
#
top := ..
grab = $(shell sed -n 's/^$(1) = //p' $(top)/src/Makefile)

CXX      := $(call grab,CXX)
CXXFLAGS := $(call grab,CXXFLAGS)
CPPFLAGS := $(call grab,CPPFLAGS)
DEFS     := $(call grab,SSL)
LIBS     := $(call grab,LIBS) $(call grab,PCRE2_LIBS) $(call grab,OPENSSL_LIBS)
LDFLAGS  := $(call grab,LDFLAGS)
OBJCOPY  ?= llvm-objcopy

INC := -I$(top)/include -I$(top)/modules -I$(top)/announce -I$(top)/ganl/include \
       -I$(top)/sqlite -I$(top)/src
STATIC := -DMUX_STATIC_MODULES

# libmux.cpp is built as lib/Makefile builds it (no -DHAVE_CONFIG_H, hidden
# visibility); the other two as src/ and script/ build theirs.
#
libmux.o: $(top)/lib/libmux.cpp
	$(CXX) $(CXXFLAGS) $(INC) $(CPPFLAGS) $(DEFS) $(STATIC) -fPIC -fvisibility=hidden -c -o $@ $<
mux_main.o: $(top)/script/mux_main.cpp
	$(CXX) $(CXXFLAGS) -DHAVE_CONFIG_H $(INC) $(CPPFLAGS) $(DEFS) $(STATIC) -fPIC -c -o $@ $<
static_modules.o: $(top)/src/static_modules.cpp
	$(CXX) $(CXXFLAGS) -DHAVE_CONFIG_H $(INC) $(CPPFLAGS) $(DEFS) $(STATIC) -fPIC -c -o $@ $<

LIBMUX_OBJS := libmux.o $(filter-out $(top)/lib/libmux.lo,$(wildcard $(top)/lib/*.lo))
DRIVER_OBJS := $(wildcard $(top)/src/netmux-*.o)
ENGINE_OBJS := $(wildcard $(top)/modules/engine/*.eo)
ENTRIES := mux_GetClassObject mux_CanUnloadNow mux_Register mux_Unregister

# ld64 -r turns private externs (hidden) into local symbols by default; GNU
# and LLVM ld leave them global-hidden, and objcopy --localize-hidden does
# the rest.  Mach-O symbols carry a leading underscore.
#
ifeq ($(shell uname -s),Darwin)
LOCALIZE :=
U := _
else
LOCALIZE := --localize-hidden
U :=
endif

# partial <module> <objects...>: one object with only the renamed entry
# points global
partial = ld -r -o $(1).partial.o $(2) && \
	$(OBJCOPY) $(LOCALIZE) $(foreach e,$(ENTRIES),--redefine-sym $(U)$(e)=$(U)$(1)_$(e)) $(1).partial.o $(1).o && \
	rm -f $(1).partial.o

engine.o: $(ENGINE_OBJS) $(top)/sqlite/libsqlite3.a $(top)/lua54/liblua54.a
	rm -rf engine.ar && mkdir -p engine.ar/sqlite engine.ar/lua
	cd engine.ar/sqlite && ar x ../../$(top)/sqlite/libsqlite3.a
	cd engine.ar/lua && ar x ../../$(top)/lua54/liblua54.a
	$(call partial,engine,$(ENGINE_OBJS) engine.ar/sqlite/*.o engine.ar/lua/*.o)
	rm -rf engine.ar

# The optional modules are one source file each, built straight into a .so
# with default visibility.  Here they are compiled -fvisibility=hidden, so
# that the partial link can keep their internals to themselves; their entry
# points are DCL_EXPORT, visibility("default"), and survive it.
#
MODULE_CPPFLAGS = $(CXXFLAGS) $(INC) $(CPPFLAGS) $(DEFS) $(STATIC) -Wformat -fPIC -fvisibility=hidden
comsys_mod.o: $(top)/modules/comsys/comsys_mod.cpp
	$(CXX) $(MODULE_CPPFLAGS) -c -o comsys_mod.hidden.o $<
	$(call partial,comsys_mod,comsys_mod.hidden.o) && rm -f comsys_mod.hidden.o
mail_mod.o: $(top)/modules/mail/mail_mod.cpp
	$(CXX) $(MODULE_CPPFLAGS) -c -o mail_mod.hidden.o $<
	$(call partial,mail_mod,mail_mod.hidden.o) && rm -f mail_mod.hidden.o
exp3.o: $(top)/modules/exp3/exp3.cpp
	$(CXX) $(MODULE_CPPFLAGS) -c -o exp3.hidden.o $<
	$(call partial,exp3,exp3.hidden.o) && rm -f exp3.hidden.o

MODULE_OBJS := engine.o comsys_mod.o mail_mod.o exp3.o

netmux: $(DRIVER_OBJS) static_modules.o $(MODULE_OBJS) $(LIBMUX_OBJS) $(top)/ganl/libganl.a
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(DRIVER_OBJS) static_modules.o $(MODULE_OBJS) \
	    $(LIBMUX_OBJS) $(top)/ganl/libganl.a $(LIBS)

muxscript: mux_main.o static_modules.o $(MODULE_OBJS) $(LIBMUX_OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ mux_main.o static_modules.o $(MODULE_OBJS) \
	    $(LIBMUX_OBJS) $(LIBS)

all: netmux muxscript
.DEFAULT_GOAL := all

install: netmux muxscript
	rm -f $(top)/game/bin/netmux $(top)/game/bin/muxscript
	cp netmux muxscript $(top)/game/bin/

clean:
	rm -rf netmux muxscript *.o engine.ar

.PHONY: all install clean
