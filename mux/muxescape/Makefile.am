# Makefile.am for muxescape utility
#
# Standalone text escaping tool — no TinyMUX library dependencies.

bin_PROGRAMS = muxescape

muxescape_SOURCES = muxescape.cpp

# Ragel .rl -> .cpp — output is read-only to prevent accidental edits.
muxescape.cpp: muxescape.rl
	chmod u+w $@ 2>/dev/null || true
	ragel -G2 -o $@ $<
	chmod a-w $@

# Installation
bindir = $(abs_top_srcdir)/game/bin
