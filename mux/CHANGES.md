---
title: TinyMUX 2.13 CHANGES
date: March 2025
author:
 - Brazil
---

# Major changes that may affect performance and require softcode tweaks:

 - Reworked networking to support non-blocking SSL sockets.
 - Require PCRE to be installed instead of using a static, private
   version.

# Feature Additions:

 - Update to Unicode 10.0.

# Bug Fixes:

 - @npemit now refers to @pemit/noeval.
 - pose now documents /noeval switch.
 - Don't notify permission denied with @edit when QUIET.
 - Fixed strncpy() corner case in @hook.
 - Fixed fun_cwho(,all) command to return all objects and players
   associated with the channel, ensuring it behaves as documented.
 - Modified home command behavior to disregard the command if the player
   is already at home, and to suppress public announcements in BLIND
   locations.

# Performance Enhancements:

 - None.

# Cosmetic Changes:

 - Increased trimmed name field length for WHO, DOING, and SESSION
   displays (from 16 to 31) and adjusted field widths to maintain proper
   alignment.

# Miscellaneous:

 - Removed FiranMUX build.
 - Updated SAL annotations.
 - Fix ReSharper warnings and accept recommendations.
 - Updated to C++14 standard.
 - Replaced acache_htab, attr_name_htab, channel_htab, desc_htab, and
   descriptor_list with STL equivalent.
 - Re-work general case mux_string::tranform case to use STL map
   instead of scratch_htab.
 - Re-work the general case mux_string::tranform case to use STL map
   instead of scratch_htab.
 - Update ax_cxx_compile_stdcxx.m4.
 - Update configure.in and configure.
 - Concentrate #include files into externs.h.
 - Replaced flags_htab, func_htab, and fwdlist_htab with STL equivalents
   (unordered_map).
 - Updated to autoconf v2.71 and improved build dependencies to prevent
   race conditions.
 - Applied const-correctness improvements to functions handling name
   formatting in the network user display.
 - Updated +help index in plushelp.txt to include +selfboot and mp.
 - Re-enabled timezone caching in timezone.cpp (a change in 2008 had
   inadvertently disabled caching, affecting timezone-related
   performance).
 - Improved safety of XOR operations in utf/strings.cpp by adding a helper
   function that validates input lengths and buffer sizes. This change does
   not affect the output.
