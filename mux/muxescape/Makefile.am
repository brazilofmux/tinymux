# Makefile.am for muxescape utility
#
# Standalone text escaping tool — no TinyMUX library dependencies.

bin_PROGRAMS = muxescape

muxescape_SOURCES = muxescape.cpp

# Ragel .rl -> .cpp
muxescape.cpp: muxescape.rl
	ragel -G2 -o $@ $<

# Installation
bindir = $(abs_top_srcdir)/game/bin
