---
title: TinyMUX 2.12 CHANGES
date: March 2020
author:
 - Brazil
---

# Major changes that may affect performance and require softcode tweaks:

 - Removed allow_guest_from_registered_site configuration option.  The
   guest_site/noguest_site and register_site/permit_site site
   restrictions can be combined instead to achieve this effect.

# Feature Additions:

 - Add /inline and /queued switches to @break and @assert.
 - Remove allow_guest_from_registered_site configuration option.
 - Support longer object names to allow COLOR256 gradients when
   FIRANMUX is defined.
 - Add Hangul set of characters for allowance in object names.
 - Add Hiragama, Katakana, and Kanji set of characters for allowance
   in object names.
 - Finish CStandardMarshaler, CLog, and funcs module.
 - Improve automated mapping of UTF-8 to ASCII, CP437, Latin-1, and
   Latin-2.
 - Update to PCRE 7.1.
 - Added LMAX(), LMIN(), TRACE().
 - Include Omega in distribution.
 - Remove support for Windows x86 build with and without Intel
   compiler.
 - Add environment for building Docker images from TinyMUX sources.
 - Add funcs module to the distribution.
 - Update to Unicode 8.0.

# Bug Fixes:

 - Fix data type conversion warning in Windows build which is a bug
   for Unix as well.
 - Build scripts should be for 2.12 instead of 2.11.
 - Fix compiler warnings for freeing (const char*)
 - Stop building jar32 archives of source and binaries. The jar32
   utility is not supported on 64-bit platforms.
 - Separated KeepAlive out from Idle-checking.
 - Update link to Pueblo Enhancer's Guide.
 - Fix for crash if STARTTLS is called without a valid certificate.
 - Fix LMIN topic.
 - Fix build break for --enable-firanmux.
 - Dark wizard in same room should not be locatable.
 - Fix typo in site list management which would prevent lots of
   combinations.
 - Fix GCC7 errors related to comparison between pointer and integer.
 - Fix 2007 bug in "@set <object>=<attribute>:_<fromobj>/<fromattr>".
 - Fix memory leak from trim_spaces() in MakeCanonicalExitName().
 - Need -lcrypto.
 - unsplit should accept '+' in directory names.
 - HAVE_MYSQL_H test was missing.
 - Announce should have used $(CC) intead of hardcoding 'gcc'.
 - Omega fixed for GCC7 and LLVM. Constant narrowing.
 - Fixed Omega handling of TinyMUSH's V_TIMESTAMPS and V_CREATETIME.
 - Include tools/Makefile.in in distribution.
 - Updating to Unicode 7.1 required that the state variable for the
   mux_isprint() state machine become an unsigned short.
 - Gloss over differences between openssl 1.0 and openssl 1.1
 - Fixed missing backslash in regexp help topic.

# Performance Enhancements:

 - Rewrite unsplit to avoid using sed.

# Cosmetic Changes:

 - None.

# Miscellaneous:

 - Consume v5.2 of EastAsianWidth.txt to develop ConsoleWidth() helper
   function.
 - Fixed -Wall compiler warnings that may eventually become build
   errors.
 - Move to C++11 and start using STL.
 - Include ax_cxx_compile_stdcxx.m4 for autoconf.
 - Replace NULL with nullptr.
 - Update Debian package.
 - Update to autoconf 2.69.
 - Use Visual Studio 2017.
 - Remove Doxygen.
 - Remove obsolete -cposix check.
 - Removed NEED_SPRINTF and NEED_VSPRINTF as we don't use them.
 - Use a single top-level configure.ac. Build recursively.
 - Use <csignal> instead of deprecated C++ header <signal.h>.
 - Add clean/realclean to tools sub-directory.
 - Bump C++ standard to C++14.
 - Update soution and projects to Visual Studio 2019, and set language
   standard to C++14.
 - Refreshen autoconf and related files.
 - Don't include sys/fcntl.h unless fcntl.h doesn't exist.
