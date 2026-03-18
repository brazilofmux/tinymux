# Makefile.am for TinyMUX 2.14 modules
#
# engine.so is built as a subdirectory; external modules are built here.

SUBDIRS = engine

AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/ganl/include -I$(top_srcdir)/sqlite \
    $(WOD_REALMS) $(REALITY_LVLS) $(STUB_SLAVE) \
    $(SELFCHECK) $(INLINESQL) $(SQL_INCLUDE) $(SSL) $(OPENSSL_CFLAGS)

# External modules — each is a single .cpp → .so
all-local: sqlproxy.so sqlslave.so exp3.so comsys_mod.so mail_mod.so lua_mod.so

sqlproxy.so: sqlproxy/sqlproxy.cpp
	$(CXX) $(CXXFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS) $(DYNAMICLIB_CXXFLAGS) -o $@ $< $(PCRE2_LIBS)

exp3.so: exp3/exp3.cpp exp3/exp3.h
	$(CXX) $(CXXFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS) $(DYNAMICLIB_CXXFLAGS) -o $@ $< $(PCRE2_LIBS)

comsys_mod.so: comsys/comsys_mod.cpp comsys/comsys_mod.h
	$(CXX) $(CXXFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS) $(DYNAMICLIB_CXXFLAGS) -o $@ $< $(PCRE2_LIBS)

mail_mod.so: mail/mail_mod.cpp mail/mail_mod.h
	$(CXX) $(CXXFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS) $(DYNAMICLIB_CXXFLAGS) -o $@ $< $(PCRE2_LIBS)

lua_mod.so: lua/lua_mod.cpp lua/lua_mod.h
	$(CXX) $(CXXFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS) $(DYNAMICLIB_CXXFLAGS) `pkg-config --cflags lua5.4 2>/dev/null || echo -I/usr/include/lua5.4` -o $@ $< `pkg-config --libs lua5.4 2>/dev/null || echo -llua5.4` $(PCRE2_LIBS)

# SQL slave needs special treatment for SQL libraries
sqlslave.so: sqlslave/sqlslave.cpp
	$(CXX) $(CXXFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS) $(SQL_INCLUDE) $(DYNAMICLIB_CXXFLAGS) -o $@ $< $(SQL_LIBPATH) $(SQL_LIBS) $(PCRE2_LIBS)

# Installation
install-exec-local:
	$(MKDIR_P) $(abs_top_srcdir)/game/bin
	cd $(abs_top_srcdir)/game/bin && rm -f sqlproxy.so && $(LN_S) $(abs_builddir)/sqlproxy.so .
	cd $(abs_top_srcdir)/game/bin && rm -f sqlslave.so && $(LN_S) $(abs_builddir)/sqlslave.so .
	cd $(abs_top_srcdir)/game/bin && rm -f exp3.so && $(LN_S) $(abs_builddir)/exp3.so .
	cd $(abs_top_srcdir)/game/bin && rm -f comsys_mod.so && $(LN_S) $(abs_builddir)/comsys_mod.so .
	cd $(abs_top_srcdir)/game/bin && rm -f mail_mod.so && $(LN_S) $(abs_builddir)/mail_mod.so .
	cd $(abs_top_srcdir)/game/bin && rm -f lua_mod.so && $(LN_S) $(abs_builddir)/lua_mod.so .

CLEANFILES = *.so *.so~

EXTRA_DIST = sqlproxy/sqlproxy.cpp sqlslave/sqlslave.cpp \
    exp3/exp3.cpp exp3/exp3.h \
    comsys/comsys_mod.cpp comsys/comsys_mod.h \
    mail/mail_mod.cpp mail/mail_mod.h \
    lua/lua_mod.cpp lua/lua_mod.h
