# Makefile.am for TinyMUX 2.14 modules
#
# engine.so is built as a subdirectory; external modules are built here.

SUBDIRS = engine

# -Wformat, and only -Wformat (#1630).  comsys_mod and mail_mod declare
# __attribute__((format(printf,...))) on their log_storage_failure helpers, and
# without this flag that attribute checks nothing: -Wformat is off by default
# and these modules are compiled with neither -Wall nor -W.  #1634 landed the
# attribute and stated the format strings had been type-checked; they had not
# been, and could not have been.  A guard that never fires is worse than no
# guard, because it reads as coverage.
#
# Measured before adding: all five modules built here are already clean under
# -Wformat, so this costs no new warnings today.  Not -Wall -- that has not
# been audited on this tree and would land a wall of pre-existing noise in an
# unrelated change.
#
AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/ganl/include -I$(top_srcdir)/sqlite \
    -Wformat \
    $(WOD_REALMS) $(REALITY_LVLS) $(STUB_SLAVE) \
    $(SELFCHECK) $(INLINESQL) $(SQL_INCLUDE) $(SSL) $(OPENSSL_CFLAGS)

# External modules — each is a single .cpp → .so
all-local: sqlproxy.so sqlslave.so exp3.so comsys_mod.so mail_mod.so

# Each module compiles the COM interface declarations in
# include/modules.h, and an interface change there alters both the vtable
# layout and the IID a module queries with.  Without modules.h as a
# prerequisite, `make install` relinks the .so from a stale object that
# still carries the OLD IID: QueryInterface then fails and every function
# in that module reports "#-1 NO ... INTERFACE" (seen on #1194's IID bump,
# which broke all ten exp3 m* functions until exp3 was rebuilt by hand).
#
sqlproxy.so: sqlproxy/sqlproxy.cpp $(top_srcdir)/include/modules.h
	$(CXX) $(CXXFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS) $(LDFLAGS) $(DYNAMICLIB_CXXFLAGS) -o $@ $< $(PCRE2_LIBS)

exp3.so: exp3/exp3.cpp exp3/exp3.h $(top_srcdir)/include/modules.h
	$(CXX) $(CXXFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS) $(LDFLAGS) $(DYNAMICLIB_CXXFLAGS) -o $@ $< $(PCRE2_LIBS)

comsys_mod.so: comsys/comsys_mod.cpp comsys/comsys_mod.h $(top_srcdir)/include/modules.h
	$(CXX) $(CXXFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS) $(LDFLAGS) $(DYNAMICLIB_CXXFLAGS) -o $@ $< $(PCRE2_LIBS)

mail_mod.so: mail/mail_mod.cpp mail/mail_mod.h $(top_srcdir)/include/modules.h
	$(CXX) $(CXXFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS) $(LDFLAGS) $(DYNAMICLIB_CXXFLAGS) -o $@ $< $(PCRE2_LIBS)

# SQL slave needs special treatment for SQL libraries
sqlslave.so: sqlslave/sqlslave.cpp $(top_srcdir)/include/modules.h
	$(CXX) $(CXXFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS) $(LDFLAGS) $(SQL_INCLUDE) $(DYNAMICLIB_CXXFLAGS) -o $@ $< $(SQL_LIBPATH) $(SQL_LIBS) $(PCRE2_LIBS)

# Installation
install-exec-local:
	$(MKDIR_P) $(abs_top_srcdir)/game/bin
	cd $(abs_top_srcdir)/game/bin && rm -f sqlproxy.so && $(LN_S) $(abs_builddir)/sqlproxy.so .
	cd $(abs_top_srcdir)/game/bin && rm -f sqlslave.so && $(LN_S) $(abs_builddir)/sqlslave.so .
	cd $(abs_top_srcdir)/game/bin && rm -f exp3.so && $(LN_S) $(abs_builddir)/exp3.so .
	cd $(abs_top_srcdir)/game/bin && rm -f comsys_mod.so && $(LN_S) $(abs_builddir)/comsys_mod.so .
	cd $(abs_top_srcdir)/game/bin && rm -f mail_mod.so && $(LN_S) $(abs_builddir)/mail_mod.so .

CLEANFILES = *.so *.so~

EXTRA_DIST = sqlproxy/sqlproxy.cpp sqlslave/sqlslave.cpp \
    exp3/exp3.cpp exp3/exp3.h \
    comsys/comsys_mod.cpp comsys/comsys_mod.h \
    mail/mail_mod.cpp mail/mail_mod.h
