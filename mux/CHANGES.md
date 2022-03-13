---
title: TinyMUX 2.13 CHANGES
date: March 2022
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
 - Replaced attrcache.cpp data structures with STL.
