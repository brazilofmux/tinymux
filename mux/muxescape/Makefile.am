# Makefile.am for muxescape utility
#
# Standalone text escaping tool — no TinyMUX library dependencies.

bin_PROGRAMS = muxescape

muxescape_SOURCES = muxescape.cpp

# Ragel .rl -> .cpp — output is read-only to prevent accidental edits.
# #line directives are stripped (#1950); see mux/lib/Makefile.am for why.
muxescape.cpp: muxescape.rl
	chmod u+w $@ 2>/dev/null || true
	ragel -G2 -o $@ $<
	sed '/^#line /d' $@ > $@.tmp && mv -f $@.tmp $@
	chmod a-w $@

# Installation
bindir = $(abs_top_srcdir)/game/bin
