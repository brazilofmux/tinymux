# Makefile.am for TinyMUX 2.14 modules

# Shared build flags
AM_CPPFLAGS = -I$(top_srcdir)

# Need to build modules as shared objects
all-local: funcs.so sample.so sum.so sqlproxy.so sqlslave.so exp3.so

# Generic rule for building module shared objects
funcs.so: funcs.cpp funcs.h
	$(CXX) $(CXXFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS) $(DYNAMICLIB_CXXFLAGS) -o $@ $< $(PCRE2_LIBS)

sample.so: sample.cpp sample.h
	$(CXX) $(CXXFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS) $(DYNAMICLIB_CXXFLAGS) -o $@ $< $(PCRE2_LIBS)

sum.so: sum.cpp sum.h
	$(CXX) $(CXXFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS) $(DYNAMICLIB_CXXFLAGS) -o $@ $< $(PCRE2_LIBS)

sqlproxy.so: sqlproxy.cpp sql.h
	$(CXX) $(CXXFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS) $(DYNAMICLIB_CXXFLAGS) -o $@ $< $(PCRE2_LIBS)

exp3.so: exp3.cpp exp3.h
	$(CXX) $(CXXFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS) $(DYNAMICLIB_CXXFLAGS) -o $@ $< $(PCRE2_LIBS)

# SQL slave needs special treatment for SQL libraries
sqlslave.so: sqlslave.cpp sql.h
	$(CXX) $(CXXFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS) $(SQL_INCLUDE) $(DYNAMICLIB_CXXFLAGS) -o $@ $< $(SQL_LIBPATH) $(SQL_LIBS) $(PCRE2_LIBS)

# Installation
install-exec-local:
	$(MKDIR_P) $(abs_top_srcdir)/../game/bin
	cd $(abs_top_srcdir)/../game/bin && rm -f funcs.so && $(LN_S) $(abs_builddir)/funcs.so .
	cd $(abs_top_srcdir)/../game/bin && rm -f sample.so && $(LN_S) $(abs_builddir)/sample.so .
	cd $(abs_top_srcdir)/../game/bin && rm -f sum.so && $(LN_S) $(abs_builddir)/sum.so .
	cd $(abs_top_srcdir)/../game/bin && rm -f sqlproxy.so && $(LN_S) $(abs_builddir)/sqlproxy.so .
	cd $(abs_top_srcdir)/../game/bin && rm -f sqlslave.so && $(LN_S) $(abs_builddir)/sqlslave.so .
	cd $(abs_top_srcdir)/../game/bin && rm -f exp3.so && $(LN_S) $(abs_builddir)/exp3.so .

# Clean up additional files
CLEANFILES = *.so *.so~

# Source files to include in distribution
EXTRA_DIST = funcs.cpp funcs.h sample.cpp sample.h sum.cpp sum.h \
    sqlproxy.cpp sql.h sqlslave.cpp exp3.cpp exp3.h
