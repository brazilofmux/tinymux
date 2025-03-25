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

# Performance Enhancements:

 - None.

# Cosmetic Changes:

 - None.

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
