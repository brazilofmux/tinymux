---
author: Brazil
date: March 2026
title: LIMITS
---

While some limits of the server depend on the host environment (CPU
architecture, compiler, and operating system), there are certain universal
limits, and we can also describe the typical limits on typical platforms.

# The 8000 Character Limit

Attribute values, global registers, function arguments, internal
intermediate buffers, and `@mail` bodies are typically limited to
approximately 8000 characters.  However, you aren't always able to use the
full 8000 characters because a null (`\0`) character is needed at the end,
ANSI color codes require their own space, and for attributes, the owner and
attribute flags are encoded at the beginning of the buffer.  This
effectively reduces its usable size.

# Object Limit

The number of objects is limited to the maximum value of an `int`.

# Attribute Name Limit

The number of unique attribute names is limited to the maximum value of an
`int`.

# Attributes per Object

TinyMUX 2.14 stores attributes in SQLite.  The number of attributes per
object is not subject to the legacy 8000-byte A_LIST encoding limit.
The practical limit is determined by available disk space and memory.

# Range and Precision of Dates

MUX supports a proleptic Gregorian calendar with dates in the range of
`Sat Jan 01 00:00:00 -27256 UTC` through `Thu Dec 31 23:59:59 30826 UTC`
which corresponds to -922283539200 through 910638979199.  The precision is
100 nanoseconds, and 0 (the epoch) corresponds to
`Sat Jan 01 00:00:00 1970 UTC`.

MUX does not account for leap seconds.  However, it does take into account
what is called "Summer Time" or "Daylight Savings Time."  Given that the
operating system information about Daylight Savings Time is limited, TinyMUX
does not apply DST for times that occur earlier the earliest information
provided by the operating system.  However, for times that occur after the
latest information provided by the operating system, TinyMUX maps like years
to like years and borrows DST information from years that are within the
range supported by the operating system.

# Typical Platform Limits

An `int` is typically 32-bits and can represent values from
-2147483648 through 2147483647.

Many platforms employ the IEEE floating-point formats.  The range of
floating-point numbers which may be represented is approximately 1e-323
through 1e+308.

A `long long` is typically 64-bits and can represent values from
-9223372036854775808 to 9223372036854775807.  This is the internal
data type used for `mod()`, `remainder()`, `floordiv()`, `pack()`,
`unpack()`, `iadd()`, `isub()`, `imul()`, and `idiv()`.
