# Makefile.am for bundled Lua 5.4.7
#
# Builds liblua54.a as a convenience library for lua_mod.so.

noinst_LIBRARIES = liblua54.a

liblua54_a_SOURCES = \
    lapi.c lauxlib.c lbaselib.c lcode.c lcorolib.c lctype.c \
    ldblib.c ldebug.c ldo.c ldump.c lfunc.c lgc.c linit.c \
    liolib.c llex.c lmathlib.c lmem.c loadlib.c lobject.c \
    lopcodes.c loslib.c lparser.c lstate.c lstring.c lstrlib.c \
    ltable.c ltablib.c ltm.c lundump.c lutf8lib.c lvm.c lzio.c

liblua54_a_CFLAGS = $(AM_CFLAGS) -fPIC -fvisibility=hidden
