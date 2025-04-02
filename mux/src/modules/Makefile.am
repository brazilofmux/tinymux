# Makefile.am for TinyMUX 2.13 modules

# Shared build flags
AM_CPPFLAGS = -I$(top_srcdir)

# Module source files
MODULES_SRC = funcs.cpp sample.cpp sum.cpp sqlproxy.cpp sqlslave.cpp

# Need to build modules as shared objects
all-local: funcs.so sample.so sum.so sqlproxy.so sqlslave.so

# Generic rule for building module shared objects
funcs.so: funcs.cpp funcs.h
	$(CXX) $(AM_CXXFLAGS) $(CXXFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS) @DYNAMICLIB_CXXFLAGS@ -o $@ $< $(PCRE2_LIBS)

sample.so: sample.cpp sample.h
	$(CXX) $(AM_CXXFLAGS) $(CXXFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS) @DYNAMICLIB_CXXFLAGS@ -o $@ $< $(PCRE2_LIBS)

sum.so: sum.cpp sum.h
	$(CXX) $(AM_CXXFLAGS) $(CXXFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS) @DYNAMICLIB_CXXFLAGS@ -o $@ $< $(PCRE2_LIBS)

sqlproxy.so: sqlproxy.cpp sql.h
	$(CXX) $(AM_CXXFLAGS) $(CXXFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS) @DYNAMICLIB_CXXFLAGS@ -o $@ $< $(PCRE2_LIBS)

# SQL slave needs special treatment for SQL libraries
sqlslave.so: sqlslave.cpp sql.h
	$(CXX) $(AM_CXXFLAGS) $(CXXFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS) @SQL_INCLUDE@ @DYNAMICLIB_CXXFLAGS@ -o $@ $< @SQL_LIBPATH@ @SQL_LIBS@ $(PCRE2_LIBS)

# Installation
install-exec-local:
	$(MKDIR_P) $(abs_top_srcdir)/../game/bin
	cd $(abs_top_srcdir)/../game/bin && rm -f funcs.so && $(LN_S) $(abs_builddir)/funcs.so .
	cd $(abs_top_srcdir)/../game/bin && rm -f sample.so && $(LN_S) $(abs_builddir)/sample.so .
	cd $(abs_top_srcdir)/../game/bin && rm -f sum.so && $(LN_S) $(abs_builddir)/sum.so .
	cd $(abs_top_srcdir)/../game/bin && rm -f sqlproxy.so && $(LN_S) $(abs_builddir)/sqlproxy.so .
	cd $(abs_top_srcdir)/../game/bin && rm -f sqlslave.so && $(LN_S) $(abs_builddir)/sqlslave.so .

# No need for manual dependency tracking - automake handles this now

# Clean up additional files
CLEANFILES = *.so *.so~