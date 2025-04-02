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
 - Updated regular expression engine from PCRE to PCRE2
   * Improved performance with Just-In-Time compilation
   * Better Unicode support
   * Modern API with improved memory management

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
 - Fixed several network and SSL-related issues: added defensive null
   checks, corrected iterator handling, improved SSL state transitions,
   and fixed a descriptor management bug that could remove the wrong
   descriptor when a player has multiple connections.
 - Fixed a string ownership bug in upated muxcli.cpp (introduced in
   2.13.0.5). Neither side persisted the value. Serious.

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
 - Improved safety of XOR operations in utf/strings.cpp by adding a
   helper function that validates input lengths and buffer sizes. This
   change does not affect the output.
 - Improved the emission of COPY and RUN phrases in utf/smutil.cpp to
   support multiple consecutive phrases when necessary. This change is
   developer-only and is a necessary precursor to handling certain data
   sets correctly; output remains unchanged.
 - Name conflicts and deprecated openssl interfaces forced a
   reorganization in SHA-1 and Digest.
 - Removed SAL annotations that were interfering with code clarity.
 - Refreshed muxcli.cpp and its header to modernize code style and clean
   up legacy constructs.
 - Updated the mux_alarm class to improve clarity and maintainability.
 - Removed the unnecessary deleter for mux_alarm on Unix, simplifying
   memory management.
 - Reordered operations to avoid a race condition, enhancing stability
   in concurrent scenarios.
 - Resolved a naming conflict involving bind() to prevent build
   ambiguities.
 - Refactored the time parser by replacing macro constants with
   C++-style constants for better type safety and clarity.
 - Updated to C++17 standard.
