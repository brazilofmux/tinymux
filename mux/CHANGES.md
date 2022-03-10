---
title: TinyMUX 2.13 CHANGES
date: March 2022
author:
 - Brazil
---

# Major changes that may affect performance and require softcode tweaks:

 - Reworking networking to support non-blocking SSL sockets.
 - Require and use the platforms PCRE library instead of a static, private
   version.

# Feature Additions:

 - Update to Unicode 10.0.

# Bug Fixes:

 - @npemit now refers to @pemit/noeval.
 - pose now documents /noeval switch.
 - Don't notify permission denied with @edit when QUIET.

# Performance Enhancements:

 - None.

# Cosmetic Changes:

 - None.

# Miscellaneous:

 - Remove FiranMUX build.
