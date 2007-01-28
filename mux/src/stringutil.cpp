/*! \file stringutil.cpp
 * \brief String utility functions.
 *
 * $Id$
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "ansi.h"
#include "pcre.h"

const bool mux_isprint[256] =
{
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
//
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 1
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 2
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 3
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 4
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 5
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 6
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,  // 7

    0, 0, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 0,  // 8
    0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 0, 1, 0, 1, 1,  // 9
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // A
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // B
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // C
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // D
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // E
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1   // F
};

const bool mux_isdigit[256] =
{
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
//
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 1
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 2
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,  // 3
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 4
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 5
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 6
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 7

    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 8
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 9
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // A
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // B
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // C
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // D
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // E
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0   // F
};

const bool mux_isxdigit[256] =
{
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
//
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 1
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 2
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,  // 3
    0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 4
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 5
    0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 6
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 7

    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 8
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 9
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // A
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // B
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // C
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // D
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // E
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0   // F
};

const bool mux_isazAZ[256] =
{
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
//
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 1
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 2
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 3
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 4
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,  // 5
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 6
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,  // 7

    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 8
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 9
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // A
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // B
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // C
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // D
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // E
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0   // F
};

const bool mux_isalpha[256] =
{
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
//
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 1
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 2
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 3
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 4
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,  // 5
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 6
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,  // 7

    0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 0,  // 8
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 1,  // 9
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,  // A
    0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,  // B
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // C
    1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1,  // D
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // E
    1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0   // F
};

const bool mux_isalnum[256] =
{
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
//
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 1
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 2
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,  // 3
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 4
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,  // 5
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 6
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,  // 7

    0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 0,  // 8
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 1,  // 9
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,  // A
    0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,  // B
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // C
    1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1,  // D
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // E
    1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0   // F
};

const bool mux_isupper[256] =
{
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
//
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 1
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 2
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 3
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 4
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,  // 5
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 6
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 7

    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 0,  // 8
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,  // 9
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // A
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // B
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // C
    1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0,  // D
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // E
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0   // F
};

const bool mux_islower[256] =
{
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
//
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 1
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 2
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 3
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 4
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 5
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 6
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,  // 7

    0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 8
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 0,  // 9
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,  // A
    0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,  // B
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // C
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,  // D
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // E
    1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0   // F
};

const bool mux_isspace[256] =
{
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
//
    0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0,  // 0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 1
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 2
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 3
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 4
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 5
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 6
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 7

    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 8
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 9
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // A
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // B
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // C
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // D
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // E
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0   // F
};

// The first character of an attribute name must be either alphabetic,
// '_', '#', '.', or '~'. It's handled by the following table.
//
// Characters thereafter may be letters, numbers, and characters from
// the set {'?!`/-_.@#$^&~=+<>()}. Lower-case letters are turned into
// uppercase before being used, but lower-case letters are valid input.
//
bool mux_AttrNameInitialSet[256] =
{
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
//
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 1
    0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0,  // 2
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 3
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 4
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1,  // 5
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 6
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 0,  // 7

    0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 0,  // 8
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 1,  // 9
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,  // A
    0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,  // B
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // C
    1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1,  // D
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // E
    1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0   // F
};

bool mux_AttrNameSet[256] =
{
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
//
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 1
    0, 1, 0, 1, 1, 0, 1, 1, 1, 1, 0, 1, 0, 1, 1, 1,  // 2
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1,  // 3
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 4
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1,  // 5
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 6
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 0,  // 7

    0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 0,  // 8
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 1,  // 9
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,  // A
    0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,  // B
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // C
    1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1,  // D
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // E
    1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0   // F
};

// Valid characters for an object name are all printable
// characters except those from the set {=&|}.
//
const bool mux_ObjectNameSet[256] =
{
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
//
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 1
    1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 2
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1,  // 3
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 4
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 5
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 6
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0,  // 7

    0, 0, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 0,  // 8
    0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 0, 1, 0, 1, 1,  // 9
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // A
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // B
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // C
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // D
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // E
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0   // F
};

// Valid characters for a player name are all alphanumeric plus
// {`$_-.,'} plus SPACE depending on configuration.
//
bool mux_PlayerNameSet[256] =
{
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
//
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 1
    1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 0,  // 2
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,  // 3
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 4
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1,  // 5
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 6
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,  // 7

    0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 0,  // 8
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 1,  // 9
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,  // A
    0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,  // B
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // C
    1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1,  // D
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // E
    1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0   // F
};

// Characters which should be escaped for the secure()
// function: '%$\[](){},;'.
//
const bool mux_issecure[256] =
{
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
//
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 1
    0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0, 0,  // 2
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0,  // 3
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 4
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0,  // 5
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 6
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0,  // 7

    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 8
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 9
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // A
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // B
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // C
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // D
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // E
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0   // F
};

// Characters which should be escaped for the escape()
// function: '%\[]{};,()^$'.
//
const bool mux_isescape[256] =
{
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
//
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 1
    0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0, 0,  // 2
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0,  // 3
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 4
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0,  // 5
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 6
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0,  // 7

    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 8
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 9
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // A
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // B
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // C
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // D
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // E
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0   // F
};

const bool ANSI_TokenTerminatorTable[256] =
{
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
//
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 1
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 2
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 3
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 4
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,  // 5
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 6
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,  // 7

    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 8
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 9
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // A
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // B
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // C
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // D
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // E
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0   // F
};

const unsigned char mux_hex2dec[256] =
{
//  0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
//
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 0
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 1
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 2
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  0,  0,  0,  0,  0,  0,  // 3
    0, 10, 11, 12, 13, 14, 15,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 4
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 5
    0, 10, 11, 12, 13, 14, 15,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 6
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 7

    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 8
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // A
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // B
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // C
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // D
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // E
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0   // F
};

const unsigned char mux_toupper[256] =
{
//   0     1     2     3     4     5     6     7     8     9     A     B     C     D     E     F
//
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, // 0
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, // 1
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, // 2
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, // 3
    0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, // 4
    0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F, // 5
    0x60, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, // 6
    0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F, // 7

    0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F, // 8
    0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x8A, 0x9B, 0x8C, 0x9D, 0x8E, 0x9F, // 9
    0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, // A
    0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, // B
    0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF, // C
    0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, // D
    0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF, // E
    0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xF7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xFF  // F
};

const unsigned char mux_tolower[256] =
{
//   0     1     2     3     4     5     6     7     8     9     A     B     C     D     E     F
//
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, // 0
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, // 1
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, // 2
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, // 3
    0x40, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, // 4
    0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F, // 5
    0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, // 6
    0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F, // 7

    0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x9A, 0x8B, 0x9C, 0x8D, 0x9E, 0x8F, // 8
    0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0xFF, // 9
    0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, // A
    0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, // B
    0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF, // C
    0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xD7, 0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xDF, // D
    0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF, // E
    0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF  // F
};

const unsigned char mux_StripAccents[256] =
{
//   0     1     2     3     4     5     6     7     8     9     A     B     C     D     E     F
//
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, // 0
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, // 1
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, // 2
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, // 3
    0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, // 4
    0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F, // 5
    0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, // 6
    0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F, // 7

    0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F, // 8
    0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F, // 9
    0xA0, 0x21, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0x22, 0xAC, 0xAD, 0xAE, 0xAF, // A
    0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0x22, 0xBC, 0xBD, 0xBE, 0x3F, // B
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0xC6, 0x43, 0x45, 0x45, 0x45, 0x45, 0x49, 0x49, 0x49, 0x49, // C
    0x44, 0x4E, 0x4F, 0x4F, 0x4F, 0x4F, 0x4F, 0xD7, 0x4F, 0x55, 0x55, 0x55, 0x55, 0x59, 0x50, 0x42, // D
    0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0xE6, 0x63, 0x65, 0x65, 0x65, 0x65, 0x69, 0x69, 0x69, 0x69, // E
    0x6F, 0x6E, 0x6F, 0x6F, 0x6F, 0x6F, 0x6F, 0xF7, 0x6F, 0x75, 0x75, 0x75, 0x75, 0x79, 0x70, 0x79, // F
};

// This will help decode UTF-8 sequences.
//
// 0xxxxxxx ==> 00000000-01111111 ==> 00-7F 1 byte sequence.
// 10xxxxxx ==> 10000000-10111111 ==> 80-BF continue
// 110xxxxx ==> 11000000-11011111 ==> C0-DF 2 byte sequence.
// 1110xxxx ==> 11100000-11101111 ==> E0-EF 3 byte sequence.
// 11110xxx ==> 11110000-11110111 ==> F0-F7 4 byte sequence.
//              11111000-11111111 illegal
//
// Also, RFC 3629 specifies that 0xC0, 0xC1, and 0xF5-0xFF never
// appear in a valid sequence.
//
// The first byte gives the length of a sequence (UTF8_SIZE1 - UTF8_SIZE4).
// Bytes in the middle of a sequence map to UTF_CONTINUE.  Bytes which should
// not appear map to UTF_ILLEGAL.
//
#define UTF8_ILLEGAL   0
#define UTF8_SIZE1     1
#define UTF8_SIZE2     2
#define UTF8_SIZE3     3
#define UTF8_SIZE4     4
#define UTF8_CONTINUE  5

const unsigned char mux_utf8[256] =
{
//  0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
//
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 0
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 1
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 2
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 3
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 4
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 5
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 6
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 7

    5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  // 8
    5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  // 9
    5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  // A
    5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  // B
    0,  0,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  // C
    2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  // D
    3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  // E
    4,  4,  4,  4,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0   // F
};

// The following table maps existing 8-bit characters to UTF-16 which can
// then be encoded into UTF-8.
//
const UINT16 mux_ch2utf16[256] =
{
    0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007,
    0x0008, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E, 0x000F,
    0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015, 0x0016, 0x0017,
    0x0018, 0x0019, 0x001A, 0x001B, 0x001C, 0x001D, 0x001E, 0x001F,
    0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027,
    0x0028, 0x0029, 0x002A, 0x002B, 0x002C, 0x002D, 0x002E, 0x002F,
    0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
    0x0038, 0x0039, 0x003A, 0x003B, 0x003C, 0x003D, 0x003E, 0x003F,
    0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
    0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F,
    0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,
    0x0058, 0x0059, 0x005A, 0x005B, 0x005C, 0x005D, 0x005E, 0x005F,
    0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067,
    0x0068, 0x0069, 0x006A, 0x006B, 0x006C, 0x006D, 0x006E, 0x006F,
    0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077,
    0x0078, 0x0079, 0x007A, 0x007B, 0x007C, 0x007D, 0x007E, 0x007F,
    0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD,
    0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178,
    0x00A0, 0x00A1, 0x00A2, 0x00A3, 0x00A4, 0x00A5, 0x00A6, 0x00A7,
    0x00A8, 0x00A9, 0x00AA, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x00AF,
    0x00B0, 0x00B1, 0x00B2, 0x00B3, 0x00B4, 0x00B5, 0x00B6, 0x00B7,
    0x00B8, 0x00B9, 0x00BA, 0x00BB, 0x00BC, 0x00BD, 0x00BE, 0x00BF,
    0x00C0, 0x00C1, 0x00C2, 0x00C3, 0x00C4, 0x00C5, 0x00C6, 0x00C7,
    0x00C8, 0x00C9, 0x00CA, 0x00CB, 0x00CC, 0x00CD, 0x00CE, 0x00CF,
    0x00D0, 0x00D1, 0x00D2, 0x00D3, 0x00D4, 0x00D5, 0x00D6, 0x00D7,
    0x00D8, 0x00D9, 0x00DA, 0x00DB, 0x00DC, 0x00DD, 0x00DE, 0x00DF,
    0x00E0, 0x00E1, 0x00E2, 0x00E3, 0x00E4, 0x00E5, 0x00E6, 0x00E7,
    0x00E8, 0x00E9, 0x00EA, 0x00EB, 0x00EC, 0x00ED, 0x00EE, 0x00EF,
    0x00F0, 0x00F1, 0x00F2, 0x00F3, 0x00F4, 0x00F5, 0x00F6, 0x00F7,
    0x00F8, 0x00F9, 0x00FA, 0x00FB, 0x00FC, 0x00FD, 0x00FE, 0x00FF
};

// ANSI_lex - This function parses a string and returns two token types.
// The type identifies the token type of length nLengthToken0. nLengthToken1
// may also be present and is a token of the -other- type.
//
int ANSI_lex(size_t nString, const char *pString, size_t *nLengthToken0, size_t *nLengthToken1)
{
    *nLengthToken0 = 0;
    *nLengthToken1 = 0;

    const char *p = pString;

    for (;;)
    {
        // Look for an ESC_CHAR
        //
        p = strchr(p, ESC_CHAR);
        if (!p)
        {
            // This is the most common case by far.
            //
            *nLengthToken0 = nString;
            return TOKEN_TEXT_ANSI;
        }

        // We have an ESC_CHAR. Let's look at the next character.
        //
        if (p[1] != '[')
        {
            // Could be a '\0' or another non-'[' character.
            // Move the pointer to position ourselves over it.
            // And continue looking for an ESC_CHAR.
            //
            p = p + 1;
            continue;
        }

        // We found the beginning of an ANSI sequence.
        // Find the terminating character.
        //
        const char *q = p+2;
        while (ANSI_TokenTerminatorTable[(unsigned char)*q] == 0)
        {
            q++;
        }
        if (q[0] == '\0')
        {
            // There was no good terminator. Treat everything like text.
            // Also, we are at the end of the string, so just return.
            //
            *nLengthToken0 = q - pString;
            return TOKEN_TEXT_ANSI;
        }
        else
        {
            // We found an ANSI sequence.
            //
            if (p == pString)
            {
                // The ANSI sequence started it.
                //
                *nLengthToken0 = q - pString + 1;
                return TOKEN_ANSI;
            }
            else
            {
                // We have TEXT followed by an ANSI sequence.
                //
                *nLengthToken0 = p - pString;
                *nLengthToken1 = q - p + 1;
                return TOKEN_TEXT_ANSI;
            }
        }
    }
}

char *strip_ansi(const char *szString, size_t *pnString)
{
    static char Buffer[LBUF_SIZE];
    char *pBuffer = Buffer;

    const char *pString = szString;
    if (!pString)
    {
        if (pnString)
        {
            *pnString = 0;
        }
        *pBuffer = '\0';
        return Buffer;
    }
    size_t nString = strlen(szString);

    while (nString)
    {
        size_t nTokenLength0;
        size_t nTokenLength1;
        int iType = ANSI_lex(nString, pString, &nTokenLength0, &nTokenLength1);

        if (iType == TOKEN_TEXT_ANSI)
        {
            memcpy(pBuffer, pString, nTokenLength0);
            pBuffer += nTokenLength0;

            size_t nSkipLength = nTokenLength0 + nTokenLength1;
            nString -= nSkipLength;
            pString += nSkipLength;
        }
        else
        {
            // TOKEN_ANSI
            //
            nString -= nTokenLength0;
            pString += nTokenLength0;
        }
    }
    if (pnString)
    {
        *pnString = pBuffer - Buffer;
    }
    *pBuffer = '\0';
    return Buffer;
}

char *strip_accents(const char *szString, size_t *pnString)
{
    static char Buffer[LBUF_SIZE];
    char *pBuffer = Buffer;

    const char *pString = szString;
    if (pString)
    {
        while (*pString)
        {
            *pBuffer = mux_StripAccents(*pString);
            pBuffer++;
            pString++;
        }
    }
    if (pnString)
    {
        *pnString = pBuffer - Buffer;
    }
    *pBuffer = '\0';
    return Buffer;
}

#define ANSI_COLOR_INDEX_BLACK     0
#define ANSI_COLOR_INDEX_RED       1
#define ANSI_COLOR_INDEX_GREEN     2
#define ANSI_COLOR_INDEX_YELLOW    3
#define ANSI_COLOR_INDEX_BLUE      4
#define ANSI_COLOR_INDEX_MAGENTA   5
#define ANSI_COLOR_INDEX_CYAN      6
#define ANSI_COLOR_INDEX_WHITE     7
#define ANSI_COLOR_INDEX_DEFAULT   8

static const ANSI_ColorState acsRestingStates[3] =
{
    {true,  false, false, false, false, ANSI_COLOR_INDEX_DEFAULT, ANSI_COLOR_INDEX_DEFAULT},
    {false, false, false, false, false, ANSI_COLOR_INDEX_WHITE,   ANSI_COLOR_INDEX_DEFAULT},
    {true,  false, false, false, false, ANSI_COLOR_INDEX_DEFAULT, ANSI_COLOR_INDEX_DEFAULT}
};

static void ANSI_Parse_m(ANSI_ColorState *pacsCurrent, size_t nANSI, const char *pANSI)
{
    // If the last character isn't an 'm', then it's an ANSI sequence we
    // don't support, yet. TODO: There should be a ANSI_Parse() function
    // that calls into this one -only- if there's an 'm', but since 'm'
    // is the only command this game understands at the moment, it's easier
    // to put the test here.
    //
    if (pANSI[nANSI-1] != 'm')
    {
        return;
    }

    // Process entire string and update the current color state structure.
    //
    while (nANSI)
    {
        // Process the next attribute phrase (terminated by ';' or 'm'
        // typically).
        //
        const char *p = pANSI;
        while (mux_isdigit(*p))
        {
            p++;
        }
        size_t nLen = p - pANSI + 1;
        if (p[0] == 'm' || p[0] == ';')
        {
            // We have an attribute.
            //
            if (nLen == 2)
            {
                int iCode = pANSI[0] - '0';
                switch (iCode)
                {
                case 0:
                    // Normal.
                    //
                    *pacsCurrent = acsRestingStates[ANSI_ENDGOAL_NORMAL];
                    break;

                case 1:
                    // High Intensity.
                    //
                    pacsCurrent->bHighlite = true;
                    pacsCurrent->bNormal = false;
                    break;

                case 2:
                    // Low Intensity.
                    //
                    pacsCurrent->bHighlite = false;
                    pacsCurrent->bNormal = false;
                    break;

                case 4:
                    // Underline.
                    //
                    pacsCurrent->bUnder = true;
                    pacsCurrent->bNormal = false;
                    break;

                case 5:
                    // Blinking.
                    //
                    pacsCurrent->bBlink = true;
                    pacsCurrent->bNormal = false;
                    break;

                case 7:
                    // Reverse Video
                    //
                    pacsCurrent->bInverse = true;
                    pacsCurrent->bNormal = false;
                    break;
                }
            }
            else if (nLen == 3)
            {
                int iCode0 = pANSI[0] - '0';
                int iCode1 = pANSI[1] - '0';
                if (iCode0 == 3)
                {
                    // Foreground Color
                    //
                    if (iCode1 <= 7)
                    {
                        pacsCurrent->iForeground = iCode1;
                        pacsCurrent->bNormal = false;
                    }
                }
                else if (iCode0 == 4)
                {
                    // Background Color
                    //
                    if (iCode1 <= 7)
                    {
                        pacsCurrent->iBackground = iCode1;
                        pacsCurrent->bNormal = false;
                    }
                }
            }
        }
        pANSI += nLen;
        nANSI -= nLen;
    }
}

// The following is really 30 (E[0mE[1mE[4mE[5mE[7mE[33mE[43m) but we are
// being conservative.
//
#define ANSI_MAXIMUM_BINARY_TRANSITION_LENGTH 60

// Generate the minimal ANSI sequence that will transition from one color state
// to another.
//
static char *ANSI_TransitionColorBinary
(
    const ANSI_ColorState *acsCurrent,
    const ANSI_ColorState *pcsNext,
    size_t *nTransition,
    int  iEndGoal
)
{
    static char Buffer[ANSI_MAXIMUM_BINARY_TRANSITION_LENGTH+1];

    if (memcmp(acsCurrent, pcsNext, sizeof(ANSI_ColorState)) == 0)
    {
        *nTransition = 0;
        Buffer[0] = '\0';
        return Buffer;
    }
    ANSI_ColorState tmp = *acsCurrent;
    char *p = Buffer;

    if (pcsNext->bNormal)
    {
        // With NOBLEED, we can't stay in the normal mode. We must eventually
        // be on a white foreground.
        //
        pcsNext = &acsRestingStates[iEndGoal];
    }

    // Do we need to go through the normal state?
    //
    if (  tmp.bHighlite && !pcsNext->bHighlite
       || tmp.bUnder    && !pcsNext->bUnder
       || tmp.bBlink    && !pcsNext->bBlink
       || tmp.bInverse  && !pcsNext->bInverse
       || (  tmp.iBackground != ANSI_COLOR_INDEX_DEFAULT
          && pcsNext->iBackground == ANSI_COLOR_INDEX_DEFAULT)
       || (  tmp.iForeground != ANSI_COLOR_INDEX_DEFAULT
          && pcsNext->iForeground == ANSI_COLOR_INDEX_DEFAULT))
    {
        memcpy(p, ANSI_NORMAL, sizeof(ANSI_NORMAL)-1);
        p += sizeof(ANSI_NORMAL)-1;
        tmp = acsRestingStates[ANSI_ENDGOAL_NORMAL];
    }
    if (tmp.bHighlite != pcsNext->bHighlite)
    {
        memcpy(p, ANSI_HILITE, sizeof(ANSI_HILITE)-1);
        p += sizeof(ANSI_HILITE)-1;
    }
    if (tmp.bUnder != pcsNext->bUnder)
    {
        memcpy(p, ANSI_UNDER, sizeof(ANSI_UNDER)-1);
        p += sizeof(ANSI_UNDER)-1;
    }
    if (tmp.bBlink != pcsNext->bBlink)
    {
        memcpy(p, ANSI_BLINK, sizeof(ANSI_BLINK)-1);
        p += sizeof(ANSI_BLINK)-1;
    }
    if (tmp.bInverse != pcsNext->bInverse)
    {
        memcpy(p, ANSI_INVERSE, sizeof(ANSI_INVERSE)-1);
        p += sizeof(ANSI_INVERSE)-1;
    }
    if (tmp.iForeground != pcsNext->iForeground)
    {
        memcpy(p, ANSI_FOREGROUND, sizeof(ANSI_FOREGROUND)-1);
        p += sizeof(ANSI_FOREGROUND)-1;
        *p++ = static_cast<char>(pcsNext->iForeground + '0');
        *p++ = ANSI_ATTR_CMD;
    }
    if (tmp.iBackground != pcsNext->iBackground)
    {
        memcpy(p, ANSI_BACKGROUND, sizeof(ANSI_BACKGROUND)-1);
        p += sizeof(ANSI_BACKGROUND)-1;
        *p++ = static_cast<char>(pcsNext->iBackground + '0');
        *p++ = ANSI_ATTR_CMD;
    }
    *p = '\0';
    *nTransition = p - Buffer;
    return Buffer;
}

// The following is really 21 (%xn%xh%xu%xi%xf%xR%xr) but we are being conservative
//
#define ANSI_MAXIMUM_ESCAPE_TRANSITION_LENGTH 42

// Generate the minimal MU ANSI %-sequence that will transition from one color state
// to another.
//
static char *ANSI_TransitionColorEscape
(
    ANSI_ColorState *acsCurrent,
    ANSI_ColorState *acsNext,
    int *nTransition
)
{
    static char Buffer[ANSI_MAXIMUM_ESCAPE_TRANSITION_LENGTH+1];
    static const char cForegroundColors[9] = "xrgybmcw";
    static const char cBackgroundColors[9] = "XRGYBMCW";

    if (memcmp(acsCurrent, acsNext, sizeof(ANSI_ColorState)) == 0)
    {
        *nTransition = 0;
        Buffer[0] = '\0';
        return Buffer;
    }
    ANSI_ColorState tmp = *acsCurrent;
    int  i = 0;

    // Do we need to go through the normal state?
    //
    if (  tmp.bBlink    && !acsNext->bBlink
       || tmp.bHighlite && !acsNext->bHighlite
       || tmp.bInverse  && !acsNext->bInverse
       || (  tmp.iBackground != ANSI_COLOR_INDEX_DEFAULT
          && acsNext->iBackground == ANSI_COLOR_INDEX_DEFAULT)
       || (  tmp.iForeground != ANSI_COLOR_INDEX_DEFAULT
          && acsNext->iForeground == ANSI_COLOR_INDEX_DEFAULT))
    {
        Buffer[i  ] = '%';
        Buffer[i+1] = 'x';
        Buffer[i+2] = 'n';
        i = i + 3;
        tmp = acsRestingStates[ANSI_ENDGOAL_NORMAL];
    }
    if (tmp.bHighlite != acsNext->bHighlite)
    {
        Buffer[i  ] = '%';
        Buffer[i+1] = 'x';
        Buffer[i+2] = 'h';
        i = i + 3;
    }
    if (tmp.bUnder != acsNext->bUnder)
    {
        Buffer[i  ] = '%';
        Buffer[i+1] = 'x';
        Buffer[i+2] = 'u';
        i = i + 3;
    }
    if (tmp.bBlink != acsNext->bBlink)
    {
        Buffer[i  ] = '%';
        Buffer[i+1] = 'x';
        Buffer[i+2] = 'f';
        i = i + 3;
    }
    if (tmp.bInverse != acsNext->bInverse)
    {
        Buffer[i  ] = '%';
        Buffer[i+1] = 'x';
        Buffer[i+2] = 'i';
        i = i + 3;
    }
    if (  tmp.iForeground != acsNext->iForeground
       && acsNext->iForeground < ANSI_COLOR_INDEX_DEFAULT)
    {
        Buffer[i  ] = '%';
        Buffer[i+1] = 'x';
        Buffer[i+2] = cForegroundColors[acsNext->iForeground];
        i = i + 3;
    }
    if (  tmp.iBackground != acsNext->iBackground
       && acsNext->iBackground < ANSI_COLOR_INDEX_DEFAULT)
    {
        Buffer[i  ] = '%';
        Buffer[i+1] = 'x';
        Buffer[i+2] = cBackgroundColors[acsNext->iBackground];
        i = i + 3;
    }
    Buffer[i] = '\0';
    *nTransition = i;
    return Buffer;
}

void ANSI_String_In_Init
(
    struct ANSI_In_Context *pacIn,
    const char *szString,
    int        iEndGoal
)
{
    pacIn->m_cs = acsRestingStates[iEndGoal];
    pacIn->m_p  = szString;
    pacIn->m_n  = strlen(szString);
}

void ANSI_String_Out_Init
(
    struct ANSI_Out_Context *pacOut,
    char  *pField,
    size_t nField,
    size_t vwMax,
    int    iEndGoal
)
{
    pacOut->m_cs       = acsRestingStates[ANSI_ENDGOAL_NORMAL];
    pacOut->m_bDone    = false;
    pacOut->m_iEndGoal = iEndGoal;
    pacOut->m_n        = 0;
    pacOut->m_nMax     = nField;
    pacOut->m_p        = pField;
    pacOut->m_vw       = 0;
    pacOut->m_vwMax    = vwMax;
}

// TODO: Rework comment block.
//
// ANSI_String_Copy -- Copy characters into a buffer starting at
// pField0 with maximum size of nField. Truncate the string if it would
// overflow the buffer -or- if it would have a visual with of greater
// than maxVisualWidth. Returns the number of ANSI-encoded characters
// copied to. Also, the visual width produced by this is returned in
// *pnVisualWidth.
//
// There are three ANSI color states that we deal with in this routine:
//
// 1. csPrevious is the color state at the current end of the field.
//    It has already been encoded into the field.
//
// 2. csCurrent is the color state that the current TEXT will be shown
//    with. It hasn't been encoded into the field, yet, and if we don't
//    have enough room for at least one character of TEXT, then it may
//    never be encoded into the field.
//
// 3. csFinal is the required color state at the end. This is usually
//    the normal state or in the case of NOBLEED, it's a specific (and
//    somewhate arbitrary) foreground/background combination.
//
void ANSI_String_Copy
(
    struct ANSI_Out_Context *pacOut,
    struct ANSI_In_Context  *pacIn,
    size_t maxVisualWidth0
)
{
    // Check whether we have previous struck the session limits (given
    // by ANSI_String_Out_Init() for field size or visual width.
    //
    if (pacOut->m_bDone)
    {
        return;
    }

    // What is the working limit for visual width.
    //
    size_t vw = 0;
    size_t vwMax = pacOut->m_vwMax;
    if (maxVisualWidth0 < vwMax)
    {
        vwMax = maxVisualWidth0;
    }

    // What is the working limit for field size.
    //
    size_t nMax = pacOut->m_nMax;

    char *pField = pacOut->m_p;
    while (pacIn->m_n)
    {
        size_t nTokenLength0;
        size_t nTokenLength1;
        int iType = ANSI_lex(pacIn->m_n, pacIn->m_p, &nTokenLength0,
            &nTokenLength1);

        if (iType == TOKEN_TEXT_ANSI)
        {
            // We have a TEXT+[ANSI] phrase. The text length is given
            // by nTokenLength0, and the ANSI characters that follow
            // (if present) are of length nTokenLength1.
            //
            // Process TEXT part first.
            //
            // TODO: If there is a maximum size for the transitions,
            // and we have gobs of space, don't bother calculating
            // sizes so carefully. It might be faster

            // nFieldEffective is used to allocate and plan space for
            // the rest of the physical field (given by the current
            // nField length).
            //
            size_t nFieldAvailable = nMax - 1; // Leave room for '\0'.
            size_t nFieldNeeded = 0;

            size_t nTransitionFinal = 0;
            if (pacOut->m_iEndGoal <= ANSI_ENDGOAL_NOBLEED)
            {
                // If we lay down -any- of the TEXT part, we need to make
                // sure we always leave enough room to get back to the
                // required final ANSI color state.
                //
                if (memcmp( &(pacIn->m_cs),
                            &acsRestingStates[pacOut->m_iEndGoal],
                            sizeof(ANSI_ColorState)) != 0)
                {
                    // The color state of the TEXT isn't the final state,
                    // so how much room will the transition back to the
                    // final state take?
                    //
                    ANSI_TransitionColorBinary( &(pacIn->m_cs),
                                                &acsRestingStates[pacOut->m_iEndGoal],
                                                &nTransitionFinal,
                                                pacOut->m_iEndGoal);

                    nFieldNeeded += nTransitionFinal;
                }
            }

            // If we lay down -any- of the TEXT part, it needs to be
            // the right color.
            //
            size_t nTransition = 0;
            char *pTransition =
                ANSI_TransitionColorBinary( &(pacOut->m_cs),
                                            &(pacIn->m_cs),
                                            &nTransition,
                                            pacOut->m_iEndGoal);
            nFieldNeeded += nTransition;

            // If we find that there is no room for any of the TEXT,
            // then we're done.
            //
            // TODO: The visual width test can be done further up to save time.
            //
            if (  nFieldAvailable <= nTokenLength0 + nFieldNeeded
               || vwMax < vw + nTokenLength0)
            {
                // We have reached the limits of the field.
                //
                if (nFieldNeeded < nFieldAvailable)
                {
                    // There was enough physical room in the field, but
                    // we would have exceeded the maximum visual width
                    // if we used all the text.
                    //
                    if (nTransition)
                    {
                        // Encode the TEXT color.
                        //
                        memcpy(pField, pTransition, nTransition);
                        pField += nTransition;
                    }

                    // Place just enough of the TEXT in the field.
                    //
                    size_t nTextToAdd = vwMax - vw;
                    if (nFieldAvailable < nTextToAdd + nFieldNeeded)
                    {
                        nTextToAdd = nFieldAvailable - nFieldNeeded;
                    }
                    memcpy(pField, pacIn->m_p, nTextToAdd);
                    pField += nTextToAdd;
                    pacIn->m_p += nTextToAdd;
                    pacIn->m_n -= nTextToAdd;
                    vw += nTextToAdd;
                    pacOut->m_cs = pacIn->m_cs;

                    // Was this visual width limit related to the session or
                    // the call?
                    //
                    if (vwMax != maxVisualWidth0)
                    {
                        pacOut->m_bDone = true;
                    }
                }
                else
                {
                    // Was size limit related to the session or the call?
                    //
                    pacOut->m_bDone = true;
                }
                pacOut->m_n += pField - pacOut->m_p;
                pacOut->m_nMax -= pField - pacOut->m_p;
                pacOut->m_p  = pField;
                pacOut->m_vw += vw;
                return;
            }

            if (nTransition)
            {
                memcpy(pField, pTransition, nTransition);
                pField += nTransition;
                nMax   -= nTransition;
            }
            memcpy(pField, pacIn->m_p, nTokenLength0);
            pField  += nTokenLength0;
            nMax    -= nTokenLength0;
            pacIn->m_p += nTokenLength0;
            pacIn->m_n -= nTokenLength0;
            vw += nTokenLength0;
            pacOut->m_cs = pacIn->m_cs;

            if (nTokenLength1)
            {
                // Process ANSI
                //
                ANSI_Parse_m(&(pacIn->m_cs), nTokenLength1, pacIn->m_p);
                pacIn->m_p += nTokenLength1;
                pacIn->m_n -= nTokenLength1;
            }
        }
        else
        {
            // Process ANSI
            //
            ANSI_Parse_m(&(pacIn->m_cs), nTokenLength0, pacIn->m_p);
            pacIn->m_n -= nTokenLength0;
            pacIn->m_p += nTokenLength0;
        }
    }
    pacOut->m_n += pField - pacOut->m_p;
    pacOut->m_nMax -= pField - pacOut->m_p;
    pacOut->m_p  = pField;
    pacOut->m_vw += vw;
}

size_t ANSI_String_Finalize
(
    struct ANSI_Out_Context *pacOut,
    size_t *pnVisualWidth
)
{
    char *pField = pacOut->m_p;
    if (pacOut->m_iEndGoal <= ANSI_ENDGOAL_NOBLEED)
    {
        size_t nTransition = 0;
        char *pTransition =
            ANSI_TransitionColorBinary( &(pacOut->m_cs),
                                        &acsRestingStates[pacOut->m_iEndGoal],
                                        &nTransition, pacOut->m_iEndGoal);
        if (nTransition)
        {
            memcpy(pField, pTransition, nTransition);
            pField += nTransition;
        }
    }
    *pField = '\0';
    pacOut->m_n += pField - pacOut->m_p;
    pacOut->m_p  = pField;
    *pnVisualWidth = pacOut->m_vw;
    return pacOut->m_n;
}

// Take an ANSI string and fit as much of the information as possible
// into a field of size nField. Truncate text. Also make sure that no color
// leaks out of the field.
//
size_t ANSI_TruncateToField
(
    const char *szString,
    size_t nField,
    char *pField0,
    size_t maxVisualWidth,
    size_t *pnVisualWidth,
    int  iEndGoal
)
{
    if (!szString)
    {
        pField0[0] = '\0';
        return 0;
    }
    struct ANSI_In_Context aic;
    struct ANSI_Out_Context aoc;
    ANSI_String_In_Init(&aic, szString, iEndGoal);
    ANSI_String_Out_Init(&aoc, pField0, nField, maxVisualWidth, iEndGoal);
    ANSI_String_Copy(&aoc, &aic, maxVisualWidth);
    return ANSI_String_Finalize(&aoc, pnVisualWidth);
}

char *ANSI_TruncateAndPad_sbuf(const char *pString, size_t nMaxVisualWidth, char fill)
{
    char *pStringModified = alloc_sbuf("ANSI_TruncateAndPad_sbuf");
    size_t nAvailable = SBUF_SIZE - nMaxVisualWidth;
    size_t nVisualWidth;
    size_t nLen = ANSI_TruncateToField(pString, nAvailable,
        pStringModified, nMaxVisualWidth, &nVisualWidth, ANSI_ENDGOAL_NORMAL);
    for (size_t i = nMaxVisualWidth - nVisualWidth; i > 0; i--)
    {
        pStringModified[nLen] = fill;
        nLen++;
    }
    pStringModified[nLen] = '\0';
    return pStringModified;
}

char *normal_to_white(const char *szString)
{
    static char Buffer[LBUF_SIZE];
    size_t nVisualWidth;
    ANSI_TruncateToField( szString,
                          sizeof(Buffer),
                          Buffer,
                          sizeof(Buffer),
                          &nVisualWidth,
                          ANSI_ENDGOAL_NOBLEED
                        );
    return Buffer;
}

typedef struct
{
    int len;
    char *p;
} LITERAL_STRING_STRUCT;

#define NUM_MU_SUBS 14
static LITERAL_STRING_STRUCT MU_Substitutes[NUM_MU_SUBS] =
{
    { 1, " "  },  // 0
    { 1, " "  },  // 1
    { 2, "%t" },  // 2
    { 2, "%r" },  // 3
    { 0, NULL },  // 4
    { 2, "%b" },  // 5
    { 2, "%%" },  // 6
    { 2, "%(" },  // 7
    { 2, "%)" },  // 8
    { 2, "%[" },  // 9
    { 2, "%]" },  // 10
    { 2, "%{" },  // 11
    { 2, "%}" },  // 12
    { 2, "\\\\" } // 13
};

const unsigned char MU_EscapeConvert[256] =
{
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
//
    0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 3, 0, 0, 4, 0, 0,  // 0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 1
    1, 0, 0, 0, 0, 6, 0, 0, 7, 8, 0, 0, 0, 0, 0, 0,  // 2
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 3
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 4
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9,13,10, 0, 0,  // 5
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 6
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,11, 0,12, 0, 0,  // 7

    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 8
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 9
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // A
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // B
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // C
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // D
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // E
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0   // F
};

const unsigned char MU_EscapeNoConvert[256] =
{
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
//
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 4, 0, 0,  // 0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 1
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 2
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 3
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 4
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 5
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 6
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 7

    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 8
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 9
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // A
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // B
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // C
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // D
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // E
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0   // F
};

// Convert raw character sequences into MUX substitutions (type = 1)
// or strips them (type = 0).
//
char *translate_string(const char *szString, bool bConvert)
{
    static char szTranslatedString[LBUF_SIZE];
    char *pTranslatedString = szTranslatedString;

    const char *pString = szString;
    if (!szString)
    {
        *pTranslatedString = '\0';
        return szTranslatedString;
    }
    size_t nString = strlen(szString);

    ANSI_ColorState csCurrent;
    ANSI_ColorState csPrevious;
    csCurrent = acsRestingStates[ANSI_ENDGOAL_NOBLEED];
    csPrevious = csCurrent;
    const unsigned char *MU_EscapeChar = (bConvert)? MU_EscapeConvert : MU_EscapeNoConvert;
    while (nString)
    {
        size_t nTokenLength0;
        size_t nTokenLength1;
        int iType = ANSI_lex(nString, pString, &nTokenLength0, &nTokenLength1);

        if (iType == TOKEN_TEXT_ANSI)
        {
            // Process TEXT
            //
            int nTransition = 0;
            if (bConvert)
            {
                char *pTransition = ANSI_TransitionColorEscape(&csPrevious, &csCurrent, &nTransition);
                safe_str(pTransition, szTranslatedString, &pTranslatedString);
            }
            nString -= nTokenLength0;

            while (nTokenLength0--)
            {
                unsigned char ch = *pString++;
                unsigned char code = MU_EscapeChar[ch];
                if (  0 < code
                   && code < NUM_MU_SUBS)
                {
                    // The following can look one ahead off the end of the
                    // current token (and even at the '\0' at the end of the
                    // string, but this is acceptable. An extra look will
                    // always see either ESC from the next ANSI sequence,
                    // or the '\0' on the end of the string. No harm done.
                    //
                    if (ch == ' ' && pString[0] == ' ')
                    {
                        code = 5;
                    }
                    safe_copy_buf(MU_Substitutes[code].p,
                        MU_Substitutes[code].len, szTranslatedString,
                        &pTranslatedString);
                }
                else
                {
                    safe_chr(ch, szTranslatedString, &pTranslatedString);
                }
            }
            csPrevious = csCurrent;

            if (nTokenLength1)
            {
                // Process ANSI
                //
                ANSI_Parse_m(&csCurrent, nTokenLength1, pString);
                pString += nTokenLength1;
                nString -= nTokenLength1;
            }
        }
        else
        {
            // Process ANSI
            //
            ANSI_Parse_m(&csCurrent, nTokenLength0, pString);
            nString -= nTokenLength0;
            pString += nTokenLength0;
        }
    }
    *pTranslatedString = '\0';
    return szTranslatedString;
}

/* ---------------------------------------------------------------------------
 * munge_space: Compress multiple spaces to one space, also remove leading and
 * trailing spaces.
 */
char *munge_space(const char *string)
{
    char *buffer = alloc_lbuf("munge_space");
    const char *p = string;
    char *q = buffer;

    if (p)
    {
        // Remove initial spaces.
        //
        while (mux_isspace(*p))
            p++;

        while (*p)
        {
            while (*p && !mux_isspace(*p))
                *q++ = *p++;

            while (mux_isspace(*p))
            {
                p++;
            }

            if (*p)
                *q++ = ' ';
        }
    }

    // Remove terminal spaces and terminate string.
    //
    *q = '\0';
    return buffer;
}

/* ---------------------------------------------------------------------------
 * trim_spaces: Remove leading and trailing spaces.
 */
char *trim_spaces(char *string)
{
    char *buffer = alloc_lbuf("trim_spaces");
    char *p = string;
    char *q = buffer;

    if (p)
    {
        // Remove initial spaces.
        //
        while (mux_isspace(*p))
        {
            p++;
        }

        while (*p)
        {
            // Copy non-space characters.
            //
            while (*p && !mux_isspace(*p))
            {
                *q++ = *p++;
            }

            // Compress spaces.
            //
            while (mux_isspace(*p))
            {
                p++;
            }

            // Leave one space.
            //
            if (*p)
            {
                *q++ = ' ';
            }
        }
    }

    // Terminate string.
    //
    *q = '\0';
    return buffer;
}

/*
 * ---------------------------------------------------------------------------
 * * grabto: Return portion of a string up to the indicated character.  Also
 * * returns a modified pointer to the string ready for another call.
 */

char *grabto(char **str, char targ)
{
    char *savec, *cp;

    if (!str || !*str || !**str)
        return NULL;

    savec = cp = *str;
    while (*cp && *cp != targ)
        cp++;
    if (*cp)
        *cp++ = '\0';
    *str = cp;
    return savec;
}

int string_compare(const char *s1, const char *s2)
{
    if (  mudstate.bStandAlone
       || mudconf.space_compress)
    {
        while (mux_isspace(*s1))
        {
            s1++;
        }
        while (mux_isspace(*s2))
        {
            s2++;
        }

        while (  *s1 && *s2
              && (  (mux_tolower(*s1) == mux_tolower(*s2))
                 || (mux_isspace(*s1) && mux_isspace(*s2))))
        {
            if (mux_isspace(*s1) && mux_isspace(*s2))
            {
                // skip all other spaces.
                //
                do
                {
                    s1++;
                } while (mux_isspace(*s1));

                do
                {
                    s2++;
                } while (mux_isspace(*s2));
            }
            else
            {
                s1++;
                s2++;
            }
        }
        if (  *s1
           && *s2)
        {
            return 1;
        }

        if (mux_isspace(*s1))
        {
            while (mux_isspace(*s1))
            {
                s1++;
            }
            return *s1;
        }
        if (mux_isspace(*s2))
        {
            while (mux_isspace(*s2))
            {
                s2++;
            }
            return *s2;
        }
        if (  *s1
           || *s2)
        {
            return 1;
        }
        return 0;
    }
    else
    {
        return mux_stricmp(s1, s2);
    }
}

int string_prefix(const char *string, const char *prefix)
{
    int count = 0;

    while (*string && *prefix
          && (mux_tolower(*string) == mux_tolower(*prefix)))
    {
        string++, prefix++, count++;
    }
    if (*prefix == '\0')
    {
        // Matched all of prefix.
        //
        return count;
    }
    else
    {
        return 0;
    }
}

/*
 * accepts only nonempty matches starting at the beginning of a word
 */

const char *string_match(const char *src, const char *sub)
{
    if ((*sub != '\0') && (src))
    {
        while (*src)
        {
            if (string_prefix(src, sub))
            {
                return src;
            }

            // else scan to beginning of next word
            //
            while (mux_isalnum(*src))
            {
                src++;
            }
            while (*src && !mux_isalnum(*src))
            {
                src++;
            }
        }
    }
    return 0;
}

/*
 * ---------------------------------------------------------------------------
 * * replace_string: Returns an lbuf containing string STRING with all occurances
 * * of OLD replaced by NEW. OLD and NEW may be different lengths.
 * * (mitch 1 feb 91)
 */

char *replace_string(const char *old, const char *new0, const char *s)
{
    if (!s)
    {
        return NULL;
    }
    size_t olen = strlen(old);
    char *result = alloc_lbuf("replace_string");
    char *r = result;
    while (*s)
    {
        // Find next occurrence of the first character of OLD string.
        //
        const char *p = strchr(s, old[0]);
        if (  olen
           && p)
        {
            // Copy up to the next occurrence of the first char of OLD.
            //
            size_t n = p - s;
            if (n)
            {
                safe_copy_buf(s, n, result, &r);
                s += n;
            }

            // If we are really at an complete OLD, append NEW to the result
            // and bump the input string past the occurrence of OLD.
            // Otherwise, copy the character and try matching again.
            //
            if (!strncmp(old, s, olen))
            {
                safe_str(new0, result, &r);
                s += olen;
            }
            else
            {
                safe_chr(*s, result, &r);
                s++;
            }
        }
        else
        {
            // Finish copying source string. No matches. No further
            // work to perform.
            //
            safe_str(s, result, &r);
            break;
        }
    }
    *r = '\0';
    return result;
}

// ---------------------------------------------------------------------------
// replace_tokens: Performs ## and #@ substitution.
//
char *replace_tokens
(
    const char *s,
    const char *pBound,
    const char *pListPlace,
    const char *pSwitch
)
{
    if (!s)
    {
        return NULL;
    }
    char *result = alloc_lbuf("replace_tokens");
    char *r = result;

    while (*s)
    {
        // Find next '#'.
        //
        const char *p = strchr(s, '#');
        if (p)
        {
            // Copy up to the next occurrence of the first character.
            //
            size_t n = p - s;
            if (n)
            {
                safe_copy_buf(s, n, result, &r);
                s += n;
            }

            if (  s[1] == '#'
               && pBound)
            {
                // BOUND_VAR
                //
                safe_str(pBound, result, &r);
                s += 2;
            }
            else if (  s[1] == '@'
                    && pListPlace)
            {
                // LISTPLACE_VAR
                //
                safe_str(pListPlace, result, &r);
                s += 2;
            }
            else if (  s[1] == '$'
                    && pSwitch)
            {
                // SWITCH_VAR
                //
                safe_str(pSwitch, result, &r);
                s += 2;
            }
            else
            {
                safe_chr(*s, result, &r);
                s++;
            }
        }
        else
        {
            // Finish copying source string. No matches. No further
            // work to perform.
            //
            safe_str(s, result, &r);
            break;
        }
    }
    *r = '\0';
    return result;
}

#if 0
// Returns the number of identical characters in the two strings.
//
int prefix_match(const char *s1, const char *s2)
{
    int count = 0;

    while (*s1 && *s2
          && (mux_tolower(*s1) == mux_tolower(*s2)))
    {
        s1++, s2++, count++;
    }

    // If the whole string matched, count the null.  (Yes really.)
    //
    if (!*s1 && !*s2)
    {
        count++;
    }
    return count;
}
#endif // 0

bool minmatch(char *str, char *target, int min)
{
    while (*str && *target
          && (mux_tolower(*str) == mux_tolower(*target)))
    {
        str++;
        target++;
        min--;
    }
    if (*str)
    {
        return false;
    }
    if (!*target)
    {
        return true;
    }
    return (min <= 0);
}

// --------------------------------------------------------------------------
// StringCloneLen: allocate memory and copy string
//
char *StringCloneLen(const char *str, size_t nStr)
{
    char *buff = (char *)MEMALLOC(nStr+1);
    if (buff)
    {
        memcpy(buff, str, nStr);
        buff[nStr] = '\0';
    }
    else
    {
        ISOUTOFMEMORY(buff);
    }
    return buff;
}

// --------------------------------------------------------------------------
// StringClone: allocate memory and copy string
//
char *StringClone(const char *str)
{
    return StringCloneLen(str, strlen(str));
}

#if 0
// --------------------------------------------------------------------------
// BufferCloneLen: allocate memory and copy buffer
//
char *BufferCloneLen(const char *pBuffer, unsigned int nBuffer)
{
    char *buff = (char *)MEMALLOC(nBuffer);
    ISOUTOFMEMORY(buff);
    memcpy(buff, pBuffer, nBuffer);
    return buff;
}
#endif // 0

/* ---------------------------------------------------------------------------
 * safe_copy_str, safe_copy_chr - Copy buffers, watching for overflows.
 */

void safe_copy_str(const char *src, char *buff, char **bufp, size_t nSizeOfBuffer)
{
    if (src == NULL) return;

    char *tp = *bufp;
    char *maxtp = buff + nSizeOfBuffer;
    while (tp < maxtp && *src)
    {
        *tp++ = *src++;
    }
    *bufp = tp;
}

void safe_copy_str_lbuf(const char *src, char *buff, char **bufp)
{
    if (src == NULL)
    {
        return;
    }

    char *tp = *bufp;
    char *maxtp = buff + LBUF_SIZE - 1;
    while (tp < maxtp && *src)
    {
        *tp++ = *src++;
    }
    *bufp = tp;
}

size_t safe_copy_buf(const char *src, size_t nLen, char *buff, char **bufc)
{
    size_t left = LBUF_SIZE - (*bufc - buff) - 1;
    if (left < nLen)
    {
        nLen = left;
    }
    memcpy(*bufc, src, nLen);
    *bufc += nLen;
    return nLen;
}

size_t safe_fill(char *buff, char **bufc, char chFill, size_t nSpaces)
{
    // Check for buffer limits.
    //
    size_t nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
    if (nSpaces > nBufferAvailable)
    {
        nSpaces = nBufferAvailable;
    }

    // Fill with spaces.
    //
    memset(*bufc, chFill, nSpaces);
    *bufc += nSpaces;
    return nSpaces;
}

// mux_strncpy: Copies up to specified number of chars from source.
// Note: unlike strncpy(), this null-terminates after copying.
//
void mux_strncpy(char *dest, const char *src, size_t nSizeOfBuffer)
{
    if (src == NULL) return;

    char *tp = dest;
    char *maxtp = dest + nSizeOfBuffer;
    while (tp < maxtp && *src)
    {
        *tp++ = *src++;
    }
    *tp = '\0';
}

bool matches_exit_from_list(char *str, const char *pattern)
{
    char *s;

    while (*pattern)
    {
        for (s = str;   // check out this one
             ( *s
             && (mux_tolower(*s) == mux_tolower(*pattern))
             && *pattern
             && (*pattern != EXIT_DELIMITER));
             s++, pattern++) ;

        // Did we match it all?
        //
        if (*s == '\0')
        {
            // Make sure nothing afterwards
            //
            while (mux_isspace(*pattern))
            {
                pattern++;
            }

            // Did we get it?
            //
            if (  !*pattern
               || (*pattern == EXIT_DELIMITER))
            {
                return true;
            }
        }
        // We didn't get it, find next string to test
        //
        while (  *pattern
              && *pattern++ != EXIT_DELIMITER)
        {
            ; // Nothing.
        }
        while (mux_isspace(*pattern))
        {
            pattern++;
        }
    }
    return false;
}

const char Digits100[201] =
"001020304050607080900111213141516171819102122232425262728292\
031323334353637383930414243444546474849405152535455565758595\
061626364656667686960717273747576777879708182838485868788898\
09192939495969798999";

size_t mux_ltoa(long val, char *buf)
{
    char *p = buf;

    if (val < 0)
    {
        *p++ = '-';
        val = -val;
    }
    unsigned long uval = (unsigned long)val;

    char *q = p;

    const char *z;
    while (uval > 99)
    {
        z = Digits100 + ((uval % 100) << 1);
        uval /= 100;
        *p++ = *z;
        *p++ = *(z+1);
    }
    z = Digits100 + (uval << 1);
    *p++ = *z;
    if (uval > 9)
    {
        *p++ = *(z+1);
    }

    size_t nLength = p - buf;
    *p-- = '\0';

    // The digits are in reverse order with a possible leading '-'
    // if the value was negative. q points to the first digit,
    // and p points to the last digit.
    //
    while (q < p)
    {
        // Swap characters are *p and *q
        //
        char temp = *p;
        *p = *q;
        *q = temp;

        // Move p and first digit towards the middle.
        //
        --p;
        ++q;

        // Stop when we reach or pass the middle.
        //
    }
    return nLength;
}

char *mux_ltoa_t(long val)
{
    static char buff[I32BUF_SIZE];
    mux_ltoa(val, buff);
    return buff;
}

void safe_ltoa(long val, char *buff, char **bufc)
{
    static char temp[I32BUF_SIZE];
    size_t n = mux_ltoa(val, temp);
    safe_copy_buf(temp, n, buff, bufc);
}

size_t mux_i64toa(INT64 val, char *buf)
{
    char *p = buf;

    if (val < 0)
    {
        *p++ = '-';
        val = -val;
    }
    UINT64 uval = (UINT64)val;

    char *q = p;

    const char *z;
    while (uval > 99)
    {
        z = Digits100 + ((uval % 100) << 1);
        uval /= 100;
        *p++ = *z;
        *p++ = *(z+1);
    }
    z = Digits100 + (uval << 1);
    *p++ = *z;
    if (uval > 9)
    {
        *p++ = *(z+1);
    }

    size_t nLength = p - buf;
    *p-- = '\0';

    // The digits are in reverse order with a possible leading '-'
    // if the value was negative. q points to the first digit,
    // and p points to the last digit.
    //
    while (q < p)
    {
        // Swap characters are *p and *q
        //
        char temp = *p;
        *p = *q;
        *q = temp;

        // Move p and first digit towards the middle.
        //
        --p;
        ++q;

        // Stop when we reach or pass the middle.
        //
    }
    return nLength;
}

char *mux_i64toa_t(INT64 val)
{
    static char buff[I64BUF_SIZE];
    mux_i64toa(val, buff);
    return buff;
}

void safe_i64toa(INT64 val, char *buff, char **bufc)
{
    static char temp[I64BUF_SIZE];
    size_t n = mux_i64toa(val, temp);
    safe_copy_buf(temp, n, buff, bufc);
}

const char TableATOI[16][10] =
{
    {  0,  1,  2,  3,  4,  5,  6,  7,  8,  9},
    { 10, 11, 12, 13, 14, 15, 16, 17, 18, 19},
    { 20, 21, 22, 23, 24, 25, 26, 27, 28, 29},
    { 30, 31, 32, 33, 34, 35, 36, 37, 38, 39},
    { 40, 41, 42, 43, 44, 45, 46, 47, 48, 49},
    { 50, 51, 52, 53, 54, 55, 56, 57, 58, 59},
    { 60, 61, 62, 63, 64, 65, 66, 67, 68, 69},
    { 70, 71, 72, 73, 74, 75, 76, 77, 78, 79},
    { 80, 81, 82, 83, 84, 85, 86, 87, 88, 89},
    { 90, 91, 92, 93, 94, 95, 96, 97, 98, 99}
};

long mux_atol(const char *pString)
{
    long sum = 0;
    int LeadingCharacter = 0;

    // Convert ASCII digits
    //
    unsigned int c1;
    unsigned int c0 = pString[0];
    if (!mux_isdigit(c0))
    {
        while (mux_isspace(pString[0]))
        {
            pString++;
        }
        LeadingCharacter = pString[0];
        if (  LeadingCharacter == '-'
           || LeadingCharacter == '+')
        {
            pString++;
        }
        c0 = pString[0];
        if (!mux_isdigit(c0))
        {
            return 0;
        }
    }

    do
    {
        c1 = pString[1];
        if (mux_isdigit(c1))
        {
            sum = 100 * sum + TableATOI[c0-'0'][c1-'0'];
            pString += 2;
        }
        else
        {
            sum = 10 * sum + (c0-'0');
            break;
        }
    } while (mux_isdigit(c0 = pString[0]));

    // Interpret sign
    //
    if (LeadingCharacter == '-')
    {
        sum = -sum;
    }
    return sum;
}

INT64 mux_atoi64(const char *pString)
{
    INT64 sum = 0;
    int LeadingCharacter = 0;

    // Convert ASCII digits
    //
    unsigned int c1;
    unsigned int c0 = pString[0];
    if (!mux_isdigit(c0))
    {
        while (mux_isspace(pString[0]))
        {
            pString++;
        }
        LeadingCharacter = pString[0];
        if (  LeadingCharacter == '-'
           || LeadingCharacter == '+')
        {
            pString++;
        }
        c0 = pString[0];
        if (!mux_isdigit(c0))
        {
            return 0;
        }
    }

    do
    {
        c1 = pString[1];
        if (mux_isdigit(c1))
        {
            sum = 100 * sum + TableATOI[c0-'0'][c1-'0'];
            pString += 2;
        }
        else
        {
            sum = 10 * sum + (c0-'0');
            break;
        }
    } while (mux_isdigit(c0 = pString[0]));

    // Interpret sign
    //
    if (LeadingCharacter == '-')
    {
        sum = -sum;
    }
    return sum;
}

// Floating-point strings match one of the following patterns:
//
// [+-]?[0-9]?(.[0-9]+)([eE][+-]?[0-9]{1,3})?
// [+-]?[0-9]+(.[0-9]?)([eE][+-]?[0-9]{1,3})?
// +Inf
// -Inf
// Ind
// NaN
//
bool ParseFloat(PARSE_FLOAT_RESULT *pfr, const char *str, bool bStrict)
{
    memset(pfr, 0, sizeof(PARSE_FLOAT_RESULT));

    // Parse Input
    //
    unsigned char ch;
    pfr->pMeat = str;
    if (  !mux_isdigit(*str)
       && *str != '.')
    {
        while (mux_isspace(*str))
        {
            str++;
        }

        pfr->pMeat = str;
        if (*str == '-')
        {
            pfr->iLeadingSign = '-';
            str++;
        }
        else if (*str == '+')
        {
            pfr->iLeadingSign = '+';
            str++;
        }

        if (  !mux_isdigit(*str)
           && *str != '.')
        {
            // Look for three magic strings.
            //
            ch = mux_toupper(str[0]);
            if (ch == 'I')
            {
                // Could be 'Inf' or 'Ind'
                //
                ch = mux_toupper(str[1]);
                if (ch == 'N')
                {
                    ch = mux_toupper(str[2]);
                    if (ch == 'F')
                    {
                        // Inf
                        //
                        if (pfr->iLeadingSign == '-')
                        {
                            pfr->iString = IEEE_MAKE_NINF;
                        }
                        else
                        {
                            pfr->iString = IEEE_MAKE_PINF;
                        }
                        str += 3;
                        goto LastSpaces;
                    }
                    else if (ch == 'D')
                    {
                        // Ind
                        //
                        pfr->iString = IEEE_MAKE_IND;
                        str += 3;
                        goto LastSpaces;
                    }
                }
            }
            else if (ch == 'N')
            {
                // Could be 'Nan'
                //
                ch = mux_toupper(str[1]);
                if (ch == 'A')
                {
                    ch = mux_toupper(str[2]);
                    if (ch == 'N')
                    {
                        // Nan
                        //
                        pfr->iString = IEEE_MAKE_NAN;
                        str += 3;
                        goto LastSpaces;
                    }
                }
            }
            return false;
        }
    }

    // At this point, we have processed the leading sign, handled all
    // the magic strings, skipped the leading spaces, and best of all
    // we either have a digit or a decimal point.
    //
    pfr->pDigitsA = str;
    while (mux_isdigit(*str))
    {
        pfr->nDigitsA++;
        str++;
    }

    if (*str == '.')
    {
        str++;
    }

    pfr->pDigitsB = str;
    while (mux_isdigit(*str))
    {
        pfr->nDigitsB++;
        str++;
    }

    if (  pfr->nDigitsA == 0
       && pfr->nDigitsB == 0)
    {
        return false;
    }

    ch = mux_toupper(*str);
    if (ch == 'E')
    {
        // There is an exponent portion.
        //
        str++;
        if (*str == '-')
        {
            pfr->iExponentSign = '-';
            str++;
        }
        else if (*str == '+')
        {
            pfr->iExponentSign = '+';
            str++;
        }
        pfr->pDigitsC = str;
        while (mux_isdigit(*str))
        {
            pfr->nDigitsC++;
            str++;
        }

        if (  pfr->nDigitsC < 1
           || 4 < pfr->nDigitsC)
        {
            return false;
        }
    }

LastSpaces:

    pfr->nMeat = str - pfr->pMeat;

    // Trailing spaces.
    //
    while (mux_isspace(*str))
    {
        str++;
    }

    if (bStrict)
    {
        return (!*str);
    }
    else
    {
        return true;
    }
}

#define ATOF_LIMIT 100
static const double powerstab[10] =
{
            1.0,
           10.0,
          100.0,
         1000.0,
        10000.0,
       100000.0,
      1000000.0,
     10000000.0,
    100000000.0,
   1000000000.0
};

double mux_atof(const char *szString, bool bStrict)
{
    PARSE_FLOAT_RESULT pfr;
    if (!ParseFloat(&pfr, szString, bStrict))
    {
        return 0.0;
    }

    if (pfr.iString)
    {
        // Return the double value which corresponds to the
        // string when HAVE_IEEE_FORMAT.
        //
#ifdef HAVE_IEEE_FP_FORMAT
        return MakeSpecialFloat(pfr.iString);
#else // HAVE_IEEE_FP_FORMAT
        return 0.0;
#endif // HAVE_IEEE_FP_FORMAT
    }

    // See if we can shortcut the decoding process.
    //
    double ret;
    if (  pfr.nDigitsA <= 9
       && pfr.nDigitsC == 0)
    {
        if (pfr.nDigitsB <= 9)
        {
            if (pfr.nDigitsB == 0)
            {
                // This 'floating-point' number is just an integer.
                //
                ret = (double)mux_atol(pfr.pDigitsA);
            }
            else
            {
                // This 'floating-point' number is fixed-point.
                //
                double rA = (double)mux_atol(pfr.pDigitsA);
                double rB = (double)mux_atol(pfr.pDigitsB);
                double rScale = powerstab[pfr.nDigitsB];
                ret = rA + rB/rScale;

                // As it is, ret is within a single bit of what a
                // a call to atof would return. However, we can
                // achieve that last lowest bit of precision by
                // computing a residual.
                //
                double residual = (ret - rA)*rScale;
                ret += (rB - residual)/rScale;
            }
            if (pfr.iLeadingSign == '-')
            {
                ret = -ret;
            }
            return ret;
        }
    }

    const char *p = pfr.pMeat;
    size_t n = pfr.nMeat;

    // We need to protect certain libraries from going nuts from being
    // force fed lots of ASCII.
    //
    char *pTmp = NULL;
    if (n > ATOF_LIMIT)
    {
        pTmp = alloc_lbuf("mux_atof");
        memcpy(pTmp, p, ATOF_LIMIT);
        pTmp[ATOF_LIMIT] = '\0';
        p = pTmp;
    }

    ret = mux_strtod(p, NULL);

    if (pTmp)
    {
        free_lbuf(pTmp);
    }

    return ret;
}

char *mux_ftoa(double r, bool bRounded, int frac)
{
    static char buffer[100];
    char *q = buffer;
    char *rve = NULL;
    int iDecimalPoint = 0;
    int bNegative = 0;
    int mode = 0;
    int nRequestMaximum = 50;
    const int nRequestMinimum = -20;
    int nRequest = nRequestMaximum;

    // If float_precision is enabled, let it override nRequestMaximum.
    //
    if (0 <= mudconf.float_precision)
    {
        mode = 3;
        if (mudconf.float_precision < nRequestMaximum)
        {
            nRequestMaximum = mudconf.float_precision;
            nRequest        = mudconf.float_precision;
        }
    }

    if (bRounded)
    {
        mode = 3;
        nRequest = frac;
        if (nRequestMaximum < nRequest)
        {
            nRequest = nRequestMaximum;
        }
        else if (nRequest < nRequestMinimum)
        {
            nRequest = nRequestMinimum;
        }
    }

    char *p = mux_dtoa(r, mode, nRequest, &iDecimalPoint, &bNegative, &rve);
    size_t nSize = rve - p;
    if (nSize > 50)
    {
        nSize = 50;
    }

    if (bNegative)
    {
        *q++ = '-';
    }

    if (iDecimalPoint == 9999)
    {
        // Inf or NaN
        //
        memcpy(q, p, nSize);
        q += nSize;
    }
    else if (nSize <= 0)
    {
        // Zero
        //
        if (bNegative)
        {
            // If we laid down a minus sign, we should remove it.
            //
            q--;
        }
        *q++ = '0';
        if (  bRounded
           && 0 < nRequest)
        {
            *q++ = '.';
            memset(q, '0', nRequest);
            q += nRequest;
        }
    }
    else if (  iDecimalPoint <= -6
            || 18 <= iDecimalPoint)
    {
        *q++ = *p++;
        if (1 < nSize)
        {
            *q++ = '.';
            memcpy(q, p, nSize-1);
            q += nSize-1;
        }
        *q++ = 'E';
        q += mux_ltoa(iDecimalPoint-1, q);
    }
    else if (iDecimalPoint <= 0)
    {
        // iDecimalPoint = -5 to 0
        //
        *q++ = '0';
        *q++ = '.';
        memset(q, '0', -iDecimalPoint);
        q += -iDecimalPoint;
        memcpy(q, p, nSize);
        q += nSize;
        if (bRounded)
        {
            size_t nPad = nRequest - (nSize - iDecimalPoint);
            if (0 < nPad)
            {
                memset(q, '0', nPad);
                q += nPad;
            }
        }
    }
    else
    {
        // iDecimalPoint = 1 to 17
        //
        if (nSize <= static_cast<size_t>(iDecimalPoint))
        {
            memcpy(q, p, nSize);
            q += nSize;
            memset(q, '0', iDecimalPoint - nSize);
            q += iDecimalPoint - nSize;
            if (  bRounded
               && 0 < nRequest)
            {
                *q++ = '.';
                memset(q, '0', nRequest);
                q += nRequest;
            }
        }
        else
        {
            memcpy(q, p, iDecimalPoint);
            q += iDecimalPoint;
            p += iDecimalPoint;
            *q++ = '.';
            memcpy(q, p, nSize - iDecimalPoint);
            q += nSize - iDecimalPoint;
            if (bRounded)
            {
                size_t nPad = nRequest - (nSize - iDecimalPoint);
                if (0 < nPad)
                {
                    memset(q, '0', nPad);
                    q += nPad;
                }
            }
        }
    }
    *q = '\0';
    return buffer;
}

bool is_integer(const char *str, int *pDigits)
{
    LBUF_OFFSET i = 0;
    int nDigits = 0;
    if (pDigits)
    {
        *pDigits = 0;
    }

    // Leading spaces.
    //
    while (mux_isspace(str[i]))
    {
        i++;
    }

    // Leading minus or plus
    //
    if (str[i] == '-' || str[i] == '+')
    {
        i++;

        // Just a sign by itself isn't an integer.
        //
        if (!str[i])
        {
            return false;
        }
    }

    // Need at least 1 integer
    //
    if (!mux_isdigit(str[i]))
    {
        return false;
    }

    // The number (int)
    //
    do
    {
        i++;
        nDigits++;
    } while (mux_isdigit(str[i]));

    if (pDigits)
    {
        *pDigits = nDigits;
    }

    // Trailing Spaces.
    //
    while (mux_isspace(str[i]))
    {
        i++;
    }

    return (!str[i]);
}

bool is_rational(const char *str)
{
    LBUF_OFFSET i = 0;

    // Leading spaces.
    //
    while (mux_isspace(str[i]))
    {
        i++;
    }

    // Leading minus or plus sign.
    //
    if (str[i] == '-' || str[i] == '+')
    {
        i++;

        // But not if just a sign.
        //
        if (!str[i])
        {
            return false;
        }
    }

    // Need at least one digit.
    //
    bool got_one = false;
    if (mux_isdigit(str[i]))
    {
        got_one = true;
    }

    // The number (int)
    //
    while (mux_isdigit(str[i]))
    {
        i++;
    }

    // Decimal point.
    //
    if (str[i] == '.')
    {
        i++;
    }

    // Need at least one digit
    //
    if (mux_isdigit(str[i]))
    {
        got_one = true;
    }

    if (!got_one)
    {
        return false;
    }

    // The number (fract)
    //
    while (mux_isdigit(str[i]))
    {
        i++;
    }

    // Trailing spaces.
    //
    while (mux_isspace(str[i]))
    {
        i++;
    }

    // There must be nothing else after the trailing spaces.
    //
    return (!str[i]);
}

bool is_real(const char *str)
{
    PARSE_FLOAT_RESULT pfr;
    return ParseFloat(&pfr, str);
}

// mux_strtok_src, mux_strtok_ctl, mux_strtok_parse.
//
// These three functions work together to replace the functionality of the
// strtok() C runtime library function. Call mux_strtok_src() first with
// the string to parse, then mux_strtok_ctl() with the control
// characters, and finally mux_strtok_parse() to parse out the tokens.
//
// You may call mux_strtok_ctl() to change the set of control characters
// between mux_strtok_parse() calls, however keep in mind that the parsing
// may not occur how you intend it to as mux_strtok_parse() does not
// consume -all- of the controlling delimiters that separate two tokens.
// It consumes only the first one.
//
void mux_strtok_src(MUX_STRTOK_STATE *tts, char *arg_pString)
{
    if (!tts || !arg_pString) return;

    // Remember the string to parse.
    //
    tts->pString = arg_pString;
}

void mux_strtok_ctl(MUX_STRTOK_STATE *tts, char *pControl)
{
    if (!tts || !pControl) return;

    // No character is a control character.
    //
    memset(tts->aControl, 0, sizeof(tts->aControl));

    // The NULL character is always a control character.
    //
    tts->aControl[0] = 1;

    // Record the user-specified control characters.
    //
    while (*pControl)
    {
        tts->aControl[(unsigned char)*pControl] = 1;
        pControl++;
    }
}

char *mux_strtok_parseLEN(MUX_STRTOK_STATE *tts, size_t *pnLen)
{
    *pnLen = 0;
    if (!tts)
    {
        return NULL;
    }
    char *p = tts->pString;
    if (!p)
    {
        return NULL;
    }

    // Skip over leading control characters except for the NUL character.
    //
    while (tts->aControl[(unsigned char)*p] && *p)
    {
        p++;
    }

    char *pReturn = p;

    // Skip over non-control characters.
    //
    while (tts->aControl[(unsigned char)*p] == 0)
    {
        p++;
    }

    // What is the length of this token?
    //
    *pnLen = p - pReturn;

    // Terminate the token with a NUL.
    //
    if (p[0])
    {
        // We found a non-NUL delimiter, so the next call will begin parsing
        // on the character after this one.
        //
        tts->pString = p+1;
    }
    else
    {
        // We hit the end of the string, so the end of the string is where
        // the next call will begin.
        //
        tts->pString = p;
    }

    // Did we find a token?
    //
    if (*pnLen > 0)
    {
        return pReturn;
    }
    else
    {
        return NULL;
    }
}

char *mux_strtok_parse(MUX_STRTOK_STATE *tts)
{
    size_t nLen;
    char *p = mux_strtok_parseLEN(tts, &nLen);
    if (p)
    {
        p[nLen] = '\0';
    }
    return p;
}

// This function will filter out any characters in the the set from
// the string.
//
char *RemoveSetOfCharacters(char *pString, char *pSetToRemove)
{
    static char Buffer[LBUF_SIZE];
    char *pBuffer = Buffer;

    size_t nLen;
    size_t nLeft = sizeof(Buffer) - 1;
    char *p;
    MUX_STRTOK_STATE tts;
    mux_strtok_src(&tts, pString);
    mux_strtok_ctl(&tts, pSetToRemove);
    for ( p = mux_strtok_parseLEN(&tts, &nLen);
          p && nLeft;
          p = mux_strtok_parseLEN(&tts, &nLen))
    {
        if (nLeft < nLen)
        {
            nLen = nLeft;
        }
        memcpy(pBuffer, p, nLen);
        pBuffer += nLen;
        nLeft -= nLen;
    }
    *pBuffer = '\0';
    return Buffer;
}

void ItemToList_Init(ITL *p, char *arg_buff, char **arg_bufc,
    char arg_chPrefix, char arg_chSep)
{
    p->bFirst = true;
    p->chPrefix = arg_chPrefix;
    p->chSep = arg_chSep;
    p->buff = arg_buff;
    p->bufc = arg_bufc;
    p->nBufferAvailable = LBUF_SIZE - (*arg_bufc - arg_buff) - 1;
}

bool ItemToList_AddInteger(ITL *pContext, int i)
{
    char smbuf[SBUF_SIZE];
    char *p = smbuf;
    if (  !pContext->bFirst
       && pContext->chSep)
    {
        *p++ = pContext->chSep;
    }
    if (pContext->chPrefix)
    {
        *p++ = pContext->chPrefix;
    }
    p += mux_ltoa(i, p);
    size_t nLen = p - smbuf;
    if (nLen > pContext->nBufferAvailable)
    {
        // Out of room.
        //
        return false;
    }
    if (pContext->bFirst)
    {
        pContext->bFirst = false;
    }
    memcpy(*(pContext->bufc), smbuf, nLen);
    *(pContext->bufc) += nLen;
    pContext->nBufferAvailable -= nLen;
    return true;
}

bool ItemToList_AddInteger64(ITL *pContext, INT64 i64)
{
    char smbuf[SBUF_SIZE];
    char *p = smbuf;
    if (  !pContext->bFirst
       && pContext->chSep)
    {
        *p++ = pContext->chSep;
    }
    if (pContext->chPrefix)
    {
        *p++ = pContext->chPrefix;
    }
    p += mux_i64toa(i64, p);
    size_t nLen = p - smbuf;
    if (nLen > pContext->nBufferAvailable)
    {
        // Out of room.
        //
        return false;
    }
    if (pContext->bFirst)
    {
        pContext->bFirst = false;
    }
    memcpy(*(pContext->bufc), smbuf, nLen);
    *(pContext->bufc) += nLen;
    pContext->nBufferAvailable -= nLen;
    return true;
}

bool ItemToList_AddStringLEN(ITL *pContext, size_t nStr, char *pStr)
{
    size_t nLen = nStr;
    if (  !pContext->bFirst
       && pContext->chSep)
    {
        nLen++;
    }
    if (pContext->chPrefix)
    {
        nLen++;
    }
    if (nLen > pContext->nBufferAvailable)
    {
        // Out of room.
        //
        return false;
    }
    char *p = *(pContext->bufc);
    if (pContext->bFirst)
    {
        pContext->bFirst = false;
    }
    else if (pContext->chSep)
    {
        *p++ = pContext->chSep;
    }
    if (pContext->chPrefix)
    {
        *p++ = pContext->chPrefix;
    }
    memcpy(p, pStr, nStr);
    *(pContext->bufc) += nLen;
    pContext->nBufferAvailable -= nLen;
    return true;
}

bool ItemToList_AddString(ITL *pContext, char *pStr)
{
    size_t nStr = strlen(pStr);
    return ItemToList_AddStringLEN(pContext, nStr, pStr);
}

void ItemToList_Final(ITL *pContext)
{
    **(pContext->bufc) = '\0';
}

// mux_stricmp - Compare two strings ignoring case.
//
int mux_stricmp(const char *a, const char *b)
{
    while (  *a
          && *b
          && mux_tolower(*a) == mux_tolower(*b))
    {
        a++;
        b++;
    }
    int c1 = mux_tolower(*a);
    int c2 = mux_tolower(*b);
    if (c1 < c2)
    {
        return -1;
    }
    else if (c1 > c2)
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

// mux_memicmp - Compare two buffers ignoring case.
//
int mux_memicmp(const void *p1_arg, const void *p2_arg, size_t n)
{
    unsigned char *p1 = (unsigned char *)p1_arg;
    unsigned char *p2 = (unsigned char *)p2_arg;
    while (  n
          && mux_tolower(*p1) == mux_tolower(*p2))
    {
        n--;
        p1++;
        p2++;
    }
    if (n)
    {
        int c1 = mux_tolower(*p1);
        int c2 = mux_tolower(*p2);
        if (c1 < c2)
        {
            return -1;
        }
        else if (c1 > c2)
        {
            return 1;
        }
    }
    return 0;
}

// mux_strlwr - Convert string to all lower case.
//
void mux_strlwr(char *a)
{
    while (*a)
    {
        *a = mux_tolower(*a);
        a++;
    }
}

// mux_strupr - Convert string to all upper case.
//
void mux_strupr(char *a)
{
    while (*a)
    {
        *a = mux_toupper(*a);
        a++;
    }
}


// mux_vsnprintf - Is an sprintf-like function that will not overflow
// a buffer of specific size. The size is give by count, and count
// should be chosen to include the '\0' termination.
//
// Returns: A number from 0 to count-1 that is the string length of
// the returned (possibly truncated) buffer.
//
size_t DCL_CDECL mux_vsnprintf(char *buff, size_t count, const char *fmt, va_list va)
{
    // From the manuals:
    //
    // vsnprintf returns the number of characters written, not
    // including the terminating '\0' character.
    //
    // It returns a -1 if an output error occurs.
    //
    // It can return a number larger than the size of the buffer
    // on some systems to indicate how much space it -would- have taken
    // if not limited by the request.
    //
    // On Win32, it can fill the buffer completely without a
    // null-termination and return -1.

    // To favor the Unix case, if there is an output error, but
    // vsnprint doesn't touch the buffer, we avoid undefined trash by
    // null-terminating the buffer to zero-length before the call.
    // Not sure that this happens, but it's a cheap precaution.
    //
    buff[0] = '\0';

    // If Unix version does start touching the buffer, null-terminates,
    // and returns -1, we are still safe. However, if Unix version
    // touches the buffer writes garbage, and then returns -1, we may
    // pass garbage, but this possibility seems very unlikely.
    //
    size_t len;
#if defined(WIN32)
#if !defined(__INTEL_COMPILER) && (_MSC_VER >= 1400)
    int cc = vsnprintf_s(buff, count, _TRUNCATE, fmt, va);
#else // _MSC_VER
    int cc = _vsnprintf(buff, count, fmt, va);
#endif // _MSC_VER
#else // WIN32
#ifdef NEED_VSPRINTF_DCL
    extern char *vsprintf(char *, char *, va_list);
#endif // NEED_VSPRINTF_DCL

    int cc = vsnprintf(buff, count, fmt, va);
#endif // WIN32
    if (0 <= cc && static_cast<size_t>(cc) <= count-1)
    {
        len = cc;
    }
    else
    {
        if (buff[0] == '\0')
        {
            // vsnprintf did not touch the buffer.
            //
            len = 0;
        }
        else
        {
            len = count-1;
        }
    }
    buff[len] = '\0';
    return len;
}

void DCL_CDECL mux_sprintf(char *buff, size_t count, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    (void)mux_vsnprintf(buff, count, fmt, ap);
    va_end(ap);
}

// This function acts like fgets except that any data on the end of the
// line past the buffer size is truncated instead of being returned on
// the next call.
//
size_t GetLineTrunc(char *Buffer, size_t nBuffer, FILE *fp)
{
    size_t lenBuffer = 0;
    if (fgets(Buffer, static_cast<int>(nBuffer), fp))
    {
        lenBuffer = strlen(Buffer);
    }
    if (lenBuffer <= 0)
    {
        memcpy(Buffer, "\n", 2);
        return 1;
    }
    if (Buffer[lenBuffer-1] != '\n')
    {
        // The line was too long for the buffer. Continue reading until the
        // end of the line.
        //
        char TruncBuffer[SBUF_SIZE];
        size_t lenTruncBuffer;
        do
        {
            if (!fgets(TruncBuffer, sizeof(TruncBuffer), fp))
            {
                break;
            }
            lenTruncBuffer = strlen(TruncBuffer);
        }
        while (TruncBuffer[lenTruncBuffer-1] != '\n');
    }
    return lenBuffer;
}

// Method: Boyer-Moore-Horspool
//
// This method is a simplification of the Boyer-Moore String Searching
// Algorithm, but a useful one. It does not require as much temporary
// storage, and the setup costs are not as high as the full Boyer-Moore.
//
// If we were searching megabytes of data instead of 8KB at most, then
// the full Boyer-Moore would make more sense.
//
#define BMH_LARGE 32767
void BMH_Prepare(BMH_State *bmhs, size_t nPat, const char *pPat)
{
    if (nPat <= 0)
    {
        return;
    }
    size_t k;
    for (k = 0; k < 256; k++)
    {
        bmhs->m_d[k] = nPat;
    }

    char chLastPat = pPat[nPat-1];
    bmhs->m_skip2 = nPat;
    for (k = 0; k < nPat - 1; k++)
    {
        bmhs->m_d[(unsigned char)pPat[k]] = nPat - k - 1;
        if (pPat[k] == chLastPat)
        {
            bmhs->m_skip2 = nPat - k - 1;
        }
    }
    bmhs->m_d[(unsigned char)chLastPat] = BMH_LARGE;
}

bool BMH_Execute(BMH_State *bmhs, size_t *pnMatched, size_t nPat, const char *pPat, size_t nSrc, const char *pSrc)
{
    if (nPat <= 0)
    {
        return false;
    }
    for (size_t i = nPat-1; i < nSrc; i += bmhs->m_skip2)
    {
        while ((i += bmhs->m_d[(unsigned char)(pSrc[i])]) < nSrc)
        {
            ; // Nothing.
        }
        if (i < BMH_LARGE)
        {
            break;
        }
        i -= BMH_LARGE;
        int j = static_cast<int>(nPat - 1);
        const char *s = pSrc + (i - j);
        while (--j >= 0 && s[j] == pPat[j])
        {
            ; // Nothing.
        }
        if (j < 0)
        {
            *pnMatched = s-pSrc;
            return true;
        }
    }
    return false;
}

bool BMH_StringSearch(size_t *pnMatched, size_t nPat, const char *pPat, size_t nSrc, const char *pSrc)
{
    BMH_State bmhs;
    BMH_Prepare(&bmhs, nPat, pPat);
    return BMH_Execute(&bmhs, pnMatched, nPat, pPat, nSrc, pSrc);
}

void BMH_PrepareI(BMH_State *bmhs, size_t nPat, const char *pPat)
{
    if (nPat <= 0)
    {
        return;
    }
    size_t k;
    for (k = 0; k < 256; k++)
    {
        bmhs->m_d[k] = nPat;
    }

    char chLastPat = pPat[nPat-1];
    bmhs->m_skip2 = nPat;
    for (k = 0; k < nPat - 1; k++)
    {
        bmhs->m_d[mux_toupper(pPat[k])] = nPat - k - 1;
        bmhs->m_d[mux_tolower(pPat[k])] = nPat - k - 1;
        if (pPat[k] == chLastPat)
        {
            bmhs->m_skip2 = nPat - k - 1;
        }
    }
    bmhs->m_d[mux_toupper(chLastPat)] = BMH_LARGE;
    bmhs->m_d[mux_tolower(chLastPat)] = BMH_LARGE;
}

bool BMH_ExecuteI(BMH_State *bmhs, size_t *pnMatched, size_t nPat, const char *pPat, size_t nSrc, const char *pSrc)
{
    if (nPat <= 0)
    {
        return false;
    }
    for (size_t i = nPat-1; i < nSrc; i += bmhs->m_skip2)
    {
        while ((i += bmhs->m_d[(unsigned char)(pSrc[i])]) < nSrc)
        {
            ; // Nothing.
        }
        if (i < BMH_LARGE)
        {
            break;
        }
        i -= BMH_LARGE;
        int j = static_cast<int>(nPat - 1);
        const char *s = pSrc + (i - j);
        while (  --j >= 0
              && mux_toupper(s[j]) == mux_toupper(pPat[j]))
        {
            ; // Nothing.
        }
        if (j < 0)
        {
            *pnMatched = s-pSrc;
            return true;
        }
    }
    return false;
}

bool BMH_StringSearchI(size_t *pnMatched, size_t nPat, const char *pPat, size_t nSrc, const char *pSrc)
{
    BMH_State bmhs;
    BMH_PrepareI(&bmhs, nPat, pPat);
    return BMH_ExecuteI(&bmhs, pnMatched, nPat, pPat, nSrc, pSrc);
}

// ---------------------------------------------------------------------------
// cf_art_except:
//
// Add an article rule to the ruleset.
//

CF_HAND(cf_art_rule)
{
    UNUSED_PARAMETER(pExtra);
    UNUSED_PARAMETER(nExtra);

    char* pCurrent = str;

    while (mux_isspace(*pCurrent))
    {
        pCurrent++;
    }
    char* pArticle = pCurrent;
    while (  !mux_isspace(*pCurrent)
          && *pCurrent != '\0')
    {
        pCurrent++;
    }
    if (*pCurrent == '\0')
    {
        cf_log_syntax(player, cmd, "No article or regexp specified.");
        return -1;
    }

    bool bUseAn = false;
    bool bOkay = false;

    if (pCurrent - pArticle <= 2)
    {
        if (mux_tolower(pArticle[0]) == 'a')
        {
            if (mux_tolower(pArticle[1]) == 'n')
            {
                bUseAn = true;
                bOkay = true;
            }

            if (mux_isspace(pArticle[1]))
            {
                bOkay = true;
            }
        }
    }

    if (!bOkay)
    {
        *pCurrent = '\0';
        cf_log_syntax(player, cmd, "Invalid article '%s'.", pArticle);
        return -1;
    }

    while (mux_isspace(*pCurrent))
    {
        pCurrent++;
    }

    if (*pCurrent == '\0')
    {
        cf_log_syntax(player, cmd, "No regexp specified.");
        return -1;
    }

    const char *errptr;
    int erroffset;
    pcre* reNewRegexp = pcre_compile(pCurrent, 0, &errptr, &erroffset, NULL);
    if (!reNewRegexp)
    {
        cf_log_syntax(player, cmd, "Error processing regexp '%s':.",
              pCurrent, errptr);
        return -1;
    }

    pcre_extra *study = pcre_study(reNewRegexp, 0, &errptr);

    ArtRuleset** arRules = (ArtRuleset **) vp;

    ArtRuleset* arNewRule = NULL;
    try
    {
        arNewRule = new ArtRuleset;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (NULL != arNewRule)
    {
        // Push new rule at head of list.
        //
        arNewRule->m_pNextRule = *arRules;
        arNewRule->m_bUseAn = bUseAn;
        arNewRule->m_pRegexp = reNewRegexp;
        arNewRule->m_pRegexpStudy = study;
        *arRules = arNewRule;
    }
    else
    {
        MEMFREE(reNewRegexp);
        if (study)
        {
            MEMFREE(study);
        }
        cf_log_syntax(player, cmd, "Out of memory.");
        return -1;
    }

    return 0;
}

#if defined(FIRANMUX)

char *linewrap_general(char *strret, int field, char *left, char *right)
{
    int tabsets[] =
    {
        1, 9, 17, 25, 33, 41, 49, 57, 65, 73, 81
    };

    int leftmargin = 1;
    int rightmargin = 1+field;

    int position = 1;
    int index1 = 0;
    int spacesleft;
    bool space_eaten = true;
    bool line_indented = false;
    bool skip_out = false;

    char *str = alloc_lbuf("linewrap_desc");
    char *ostr = str;

    const char *original = strret;

    for (;;)
    {
        if (!original[index1])
        {
            break;
        }

        if (position == rightmargin)
        {
            line_indented = false;
            space_eaten = false;
            position = leftmargin;

            safe_str(right, str, &ostr);
            safe_str("\r\n", str, &ostr);
            continue;
        }

        if (position == leftmargin)
        {
            if (!line_indented)
            {
                safe_str(left, str, &ostr);
                line_indented = true;
            }

            if (!space_eaten)
            {
                if (' ' == original[index1])
                {
                    index1++;
                    space_eaten = true;
                    continue;
                }
            }
        }

        spacesleft = rightmargin - position;
        int index3 = index1;
        while (original[index3])
        {
            if (ESC_CHAR == original[index3])
            {
                while (  original[index3]
                      && original[index3++] != 'm')
                {
                    ; // Nothing.
                }
                continue;
            }

            if (mux_isspace(original[index3]))
            {
                break;
            }
            spacesleft--;
            index3++;
        }

        if ((index3 - index1) > field)
        {
            skip_out = true;
        }

        if (mux_isspace(original[index1]))
        {
            skip_out = false;
        }

        if (!skip_out)
        {
            if (spacesleft < 0)
            {
                int loop;
                for (loop = rightmargin - position; loop; loop--)
                {
                    safe_chr(' ', str, &ostr);
                }
                position = rightmargin;
                continue;
            }
        }

        switch (original[index1])
        {
        case ESC_CHAR:
            do {
                safe_chr(original[index1], str, &ostr);
            } while (  original[index1++] != 'm'
                    && original[index1]);
            break;

        case '\r':
            {
                int loop;
                for (loop = rightmargin-position; loop; loop--)
                {
                    safe_chr(' ', str, &ostr);
                }
            }
            position = rightmargin;
            index1 = index1 + 2;
            break;

        case '\t':
            {
                int index3 = 0;
                int difference = 0;

                index1++;

                for (;;)
                {
                    if (position < tabsets[index3])
                    {
                        break;
                    }
                    index3++;
                }

                difference = (rightmargin < tabsets[index3]) ?
                    rightmargin - position : tabsets[index3] - position;

                position = (rightmargin < tabsets[index3]) ?
                    rightmargin : tabsets[index3];

                for (; difference; difference--)
                {
                    safe_chr(' ', str, &ostr);
                }

                if (position == rightmargin)
                {
                    continue;
                }
                break;
            }
        default:
            safe_chr(original[index1], str, &ostr);
            index1++;
            position++;
            break;
        }
    }

    int loop;
    for (loop = rightmargin - position; loop; loop--)
    {
        safe_chr(' ', str, &ostr);
    }

    safe_str(right, str, &ostr);
    *ostr = '\0';

    char *bp = strret;
    safe_str(str, strret, &bp);
    *bp = '\0';

    free_lbuf(str);
    return strret;
}

char *linewrap_desc(char *str)
{
    return linewrap_general(str, 70, "     ", "");
}

#endif // FIRANMUX

/*! \brief Constructs mux_string object.
 *
 * This constructor puts the mux_string object into an initial, reasonable,
 * and empty state.
 *
 * \return         None.
 */

mux_string::mux_string(void)
{
    m_n = 0;
    m_ach[0] = '\0';
}

/*! \brief Constructs mux_string object.
 *
 * This is a deep copy constructor.
 *
 * \param sStr     mux_string to be copied.
 * \return         None.
 */

mux_string::mux_string(const mux_string &sStr)
{
    import(sStr);
}

/*! \brief Constructs mux_string object from an ANSI string.
 *
 * Parses the given ANSI string into a form which can be more-easily
 * navigated.
 *
 * \param pStr     ANSI string to be parsed.
 * \return         None.
 */

mux_string::mux_string(const char *pStr)
{
    import(pStr);
}

void mux_string::append(const char cChar)
{
    if (m_n < LBUF_SIZE-1)
    {
        m_ach[m_n] = cChar;
        m_acs[m_n] = acsRestingStates[ANSI_ENDGOAL_NORMAL];
        m_n++;
        m_ach[m_n] = '\0';
    }
}

void mux_string::append(dbref num)
{
    append('#');
    append_TextPlain(mux_ltoa_t(num));
}

void mux_string::append(INT64 iInt)
{
    append_TextPlain(mux_i64toa_t(iInt));
}

void mux_string::append(long lLong)
{
    append_TextPlain(mux_ltoa_t(lLong));
}

/*! \brief Extract and append a range of characters.
 *
 * \param sStr     mux_string from which to extract characters.
 * \param nStart   Beginning of range to extract and apend.
 * \param nLen     Length of range to extract and append.
 * \return         None.
 */

void mux_string::append(const mux_string &sStr, size_t nStart, size_t nLen)
{
    if (  sStr.m_n <= nStart
       || 0 == nLen
       || LBUF_SIZE-1 == m_n)
    {
        // The selection range is empty, or no buffer space is left.
        //
        return;
    }

    if (sStr.m_n - nStart < nLen)
    {
        nLen = sStr.m_n - nStart;
    }

    if ((LBUF_SIZE-1)-m_n < nLen)
    {
        nLen = (LBUF_SIZE-1)-m_n;
    }

    memcpy(m_ach + m_n, sStr.m_ach + nStart, nLen * sizeof(m_ach[0]));
    memcpy(m_acs + m_n, sStr.m_acs + nStart, nLen * sizeof(m_acs[0]));

    m_n += nLen;
    m_ach[m_n] = '\0';
}

void mux_string::append(const char *pStr)
{
    if (  NULL == pStr
       || '\0' == *pStr)
    {
        return;
    }

    size_t nAvail = (LBUF_SIZE-1) - m_n;
    if (0 == nAvail)
    {
        // No room.
        //
        return;
    }

    size_t nLen = strlen(pStr);
    if (nAvail < nLen)
    {
        nLen = nAvail;
    }

    mux_string *sNew = new mux_string;
    
    sNew->import(pStr, nLen);

    append(*sNew);
    delete sNew;
}

void mux_string::append(const char *pStr, size_t nLen)
{
    if (  NULL == pStr
       || '\0' == *pStr)
    {
        return;
    }

    size_t nAvail = (LBUF_SIZE-1) - m_n;
    if (0 == nAvail)
    {
        // No room.
        //
        return;
    }
    if (nAvail < nLen)
    {
        nLen = nAvail;
    }

    mux_string *sNew = new mux_string;

    sNew->import(pStr, nLen);
    append(*sNew);
    delete sNew;
}

void mux_string::append_TextPlain(const char *pStr)
{
    if (  '\0' == *pStr
       || LBUF_SIZE-1 <= m_n)
    {
        // The selection range is empty, or no buffer space is left.
        //
        return;
    }

    size_t nLen = strlen(pStr);

    if ((LBUF_SIZE-1)-m_n < nLen)
    {
        nLen = (LBUF_SIZE-1)-m_n;
    }

    memcpy(m_ach + m_n, pStr, nLen * sizeof(m_ach[0]));

    for (size_t i = 0; i < nLen; i++)
    {
        m_acs[m_n+i] = acsRestingStates[ANSI_ENDGOAL_NORMAL];
    }

    m_n += nLen;
    m_ach[m_n] = '\0';
}

void mux_string::append_TextPlain(const char *pStr, size_t nLen)
{
    if (  '\0' == *pStr
       || 0 == nLen
       || LBUF_SIZE-1 == m_n)
    {
        // The selection range is empty, or no buffer space is left.
        //
        return;
    }

    if ((LBUF_SIZE-1)-m_n < nLen)
    {
        nLen = (LBUF_SIZE-1)-m_n;
    }

    memcpy(m_ach + m_n, pStr, nLen * sizeof(m_ach[0]));

    for (size_t i = 0; i < nLen; i++)
    {
        m_acs[m_n+i] = acsRestingStates[ANSI_ENDGOAL_NORMAL];
    }

    m_n += nLen;
    m_ach[m_n] = '\0';
}

void mux_string::compress(const char ch)
{
    for (size_t i = 0, nTarget = 0; i < m_n; i++)
    {
        if (m_ach[i] == ch)
        {
            for (nTarget = 1; i + nTarget < m_n && m_ach[i + nTarget] == ch; nTarget++)
            {
                ; // Nothing.
            }
            if (1 < nTarget)
            {
                delete_Chars(i, nTarget-1);
            }
        }
    }
}

void mux_string::compress_Spaces(void)
{
    for (size_t i = 0, nSpaces = 0; i < m_n; i++)
    {
        if (mux_isspace(m_ach[i]))
        {
            for (nSpaces = 1; i + nSpaces < m_n && mux_isspace(m_ach[i + nSpaces]); nSpaces++)
            {
                ; // Nothing.
            }
            if (1 < nSpaces)
            {
                delete_Chars(i, nSpaces-1);
            }
        }
    }
}

/*! \brief Delete a range of characters.
 *
 * \param nStart   Beginning of range to delete.
 * \param nLen     Length of range.
 * \return         None.
 */

void mux_string::delete_Chars(size_t nStart, size_t nLen)
{
    if (  m_n <= nStart
       || 0 == nLen)
    {
        // The range does not select any characters.
        //
        return;
    }

    size_t nEnd = nStart + nLen;
    if (m_n <= nEnd)
    {
        // The range extends beyond the end, so we can simply truncate.
        //
        m_n = nStart;
        m_ach[m_n] = '\0';
        return;
    }

    size_t nMove = m_n - nEnd;
    memmove(m_ach+nStart, m_ach+nEnd, nMove * sizeof(m_ach[0]));
    memmove(m_acs+nStart, m_acs+nEnd, nMove * sizeof(m_acs[0]));
    m_n -= nLen;
    m_ach[m_n] = '\0';
}

void mux_string::edit(mux_string &sFrom, const mux_string &sTo)
{
    // Do the substitution.  Idea for prefix/suffix from R'nice@TinyTIM.
    //
    const char chFrom0 = sFrom.export_Char(0);
    size_t nFrom = sFrom.length();
    if (  1 == nFrom
       && '^' == chFrom0)
    {
        // Prepend 'to' to string.
        //
        prepend(sTo);
    }
    else if (  1 == nFrom
            && '$' == chFrom0)
    {
        // Append 'to' to string.
        //
        append(sTo);
    }
    else
    {
        const char chFrom1 = sFrom.export_Char(1);
        // Replace all occurances of 'from' with 'to'. Handle the special
        // cases of from = \$ and \^.
        //
        if (  (  '\\' == chFrom0
              || '%' == chFrom0)
           && (  '$' == chFrom1
              || '^' == chFrom1)
           && 2 == nFrom)
        {
            sFrom.delete_Chars(0,1);
            nFrom--;
        }

        size_t nStart = 0;
        size_t nFound = 0;
        size_t nTo = sTo.m_n;
        bool bSucceeded = search(sFrom, &nFound);
        while (bSucceeded)
        {
            nStart += nFound;
            replace_Chars(sTo, nStart, nFrom);
            nStart += nTo;

            if (nStart < LBUF_SIZE-1)
            {
                bSucceeded = search(sFrom, &nFound, nStart);
            }
            else
            {
                bSucceeded = false;
            }
        }
    }
}

char mux_string::export_Char(size_t n) const
{
    if (m_n <= n)
    {
        return '\0';
    }
    return m_ach[n];
}

ANSI_ColorState mux_string::export_Color(size_t n) const
{
    if (m_n <= n)
    {
        return acsRestingStates[ANSI_ENDGOAL_NORMAL];
    }
    return m_acs[n];
}

double mux_string::export_Float(bool bStrict) const
{
    return mux_atof(m_ach, bStrict);
}

INT64 mux_string::export_I64(void) const
{
    return mux_atoi64(m_ach);
}

long mux_string::export_Long(void) const
{
    return mux_atol(m_ach);
}

/*! \brief Generates ANSI string from internal form.
 *
 * \param buff     Pointer to beginning of lbuf.
 * \param bufc     Pointer to current position. Defaults to NULL.
 * \param nStart   String position to begin copying from. Defaults to 0.
 * \param nLen     Number of chars to copy. Defaults to LBUF_SIZE.
 * \param nBuffer  Size of buffer we're outputting into. Defaults to LBUF_SIZE-1.
 * \param iEndGoal Which output mode to use: normal or nobleed. Defaults to ANSI_ENDGOAL_NORMAL.
 * \return         None.
 */

void mux_string::export_TextAnsi(char *buff, char **bufc, size_t nStart, size_t nLen, size_t nBuffer, int iEndGoal) const
{
    // Sanity check our arguments and find out how much room we have.
    // We assume we're outputting into an LBUF unless given a smaller nBuffer.
    //
    if (NULL == bufc)
    {
        bufc = &buff;
    }
    if (  !buff
       || !*bufc)
    {
        return;
    }
    size_t nAvail = buff + nBuffer - *bufc;
    if (  nAvail < 1
       || nBuffer < nAvail)
    {
        return;
    }
    if (  m_n <= nStart
       || 0 == nLen)
    {
        return;
    }
    size_t  nLeft   = (m_n - nStart);
    if (nLeft < nLen)
    {
        nLen = nLeft;
    }
    if (nAvail < nLen)
    {
        nLen = nAvail;
    }

    // nStart is the position in the source string where we will start copying,
    //  and has a value in the range [0, m_n).
    // nAvail is the room left in the destination buffer,
    //  and has a value in the range (0, nBuffer).
    // nLeft is the length of the portion of the source string we'd like to copy,
    //  and has a value in the range (0, m_n].
    // nLen is the length of the portion of the source string we will try to copy,
    //  and has a value in the ranges (0, nLeft] and (0, nAvail].
    //
    size_t nPos = nStart;
    bool bPlentyOfRoom = nAvail > (nLen + 1) * (ANSI_MAXIMUM_BINARY_TRANSITION_LENGTH + 1);
    ANSI_ColorState csEndGoal = acsRestingStates[iEndGoal];
    size_t nCopied = 0;

    if (bPlentyOfRoom)
    {
        ANSI_ColorState csPrev = csEndGoal;
        while (nPos < nStart + nLen)
        {
            if (0 != memcmp(&csPrev, &m_acs[nPos], sizeof(ANSI_ColorState)))
            {
                safe_copy_str(ANSI_TransitionColorBinary(&csPrev, &(m_acs[nPos]),
                                                &nCopied, iEndGoal), buff, bufc, nBuffer);
                csPrev = m_acs[nPos];
            }
            safe_copy_chr(m_ach[nPos], buff, bufc, nBuffer);
            nPos++;
        }
        if (0 != memcmp(&csPrev, &csEndGoal, sizeof(ANSI_ColorState)))
        {
            safe_copy_str(ANSI_TransitionColorBinary(&csPrev, &csEndGoal, &nCopied, iEndGoal), buff, bufc, nBuffer);
        }
        **bufc = '\0';
        return;
    }

    // There's a chance we might hit the end of the buffer. Do it the hard way.
    size_t nNeededBefore = 0, nNeededAfter = 0;
    ANSI_ColorState csPrev = csEndGoal;
    while (nPos < nStart + nLen)
    {
        if (0 != memcmp(&csPrev, &m_acs[nPos], sizeof(ANSI_ColorState)))
        {
            if (0 != memcmp(&csEndGoal, &m_acs[nPos], sizeof(ANSI_ColorState)))
            {
                nNeededBefore = nNeededAfter;
                ANSI_TransitionColorBinary(&(m_acs[nPos]), &csEndGoal, &nCopied, iEndGoal);
                nNeededAfter = nCopied;
                char *pTransition = ANSI_TransitionColorBinary(&csPrev, &(m_acs[nPos]), &nCopied, iEndGoal);
                if (nBuffer < (*bufc-buff) + nCopied + 1 + nNeededAfter)
                {
                    // There isn't enough room to add the color sequence,
                    // its character, and still get back to normal. Stop here.
                    //
                    nNeededAfter = nNeededBefore;
                    break;
                }
                safe_copy_str(pTransition, buff, bufc, nBuffer);
            }
            else
            {
                safe_copy_str(ANSI_TransitionColorBinary(&csPrev, &(m_acs[nPos]),
                                            &nCopied, iEndGoal), buff, bufc, nBuffer);
                nNeededAfter = 0;
            }
            csPrev = m_acs[nPos];
        }
        if (nBuffer < (*bufc-buff) + 1 + nNeededAfter)
        {
            break;
        }
        safe_copy_chr(m_ach[nPos], buff, bufc, nBuffer);
        nPos++;
    }
    if (nNeededAfter)
    {
       safe_copy_str(ANSI_TransitionColorBinary(&csPrev, &csEndGoal, &nCopied, iEndGoal), buff, bufc, nBuffer);
    }
    **bufc = '\0';
    return;
}

/*! \brief Outputs ANSI-stripped string from internal form.
 *
 * \param buff     Pointer to beginning of lbuf.
 * \param bufc     Pointer to current position. Defaults to NULL.
 * \param nStart   String position to begin copying from. Defaults to 0.
 * \param nLen     Number of chars to copy. Defaults to LBUF_SIZE.
 * \param nBuffer  Size of buffer we're outputting into. Defaults to LBUF_SIZE-1.
 * \return         None.
 */

void mux_string::export_TextPlain(char *buff, char **bufc, size_t nStart, size_t nLen, size_t nBuffer) const
{
    // Sanity check our arguments and find out how much room we have.
    // We assume we're outputting into an LBUF unless given a smaller nBuffer.
    //
    if (NULL == bufc)
    {
        bufc = &buff;
    }
    if (  !buff
       || !*bufc)
    {
        return;
    }
    size_t nAvail = buff + nBuffer - *bufc;
    if (  nAvail < 1
       || nBuffer < nAvail)
    {
        return;
    }
    if (  m_n <= nStart
       || 0 == nLen)
    {
        return;
    }
    size_t  nLeft   = (m_n - nStart);
    if (nLeft < nLen)
    {
        nLen = nLeft;
    }
    if (nAvail < nLen)
    {
        nLen = nAvail;
    }

    // nStart is the position in the source string where we will start copying,
    //  and has a value in the range [0, m_n).
    // nAvail is the room left in the destination buffer,
    //  and has a value in the range (0, nBuffer).
    // nLeft is the length of the portion of the source string we'd like to copy,
    //  and has a value in the range (0, m_n].
    // nLen is the length of the portion of the source string we will copy,
    //  and has a value in the ranges (0, nLeft] and (0, nAvail].
    //
    safe_copy_str(m_ach+nStart, buff, bufc, *bufc-buff+nLen);
    **bufc = '\0';
}

/*! \brief Imports a single normal-colored character.
 *
 * \param chIn     Normal character.
 * \return         None.
 */

void mux_string::import(const char chIn)
{
    if (  ESC_CHAR != chIn
       && '\0' != chIn)
    {
        m_ach[0] = chIn;
        m_acs[0] = acsRestingStates[ANSI_ENDGOAL_NORMAL];
        m_n = 1;
    }
    else
    {
        m_n = 0;
    }
    m_ach[m_n] = '\0';
}

/*! \brief Converts and Imports a dbref.
 *
 * \param num      dbref to convert and import.
 * \return         None.
 */

void mux_string::import(dbref num)
{
    m_ach[0] = '#';
    m_n = 1;

    // mux_ltoa() sets the '\0'.
    //
    m_n += mux_ltoa(num, m_ach + 1);
    for (size_t i = 0; i < m_n; i++)
    {
        m_acs[i] = acsRestingStates[ANSI_ENDGOAL_NORMAL];
    }
}

/*! \brief Converts and Imports an INT64.
 *
 * \param iInt     INT64 to convert and import.
 * \return         None.
 */

void mux_string::import(INT64 iInt)
{
    // mux_i64toa() sets the '\0'.
    //
    m_n = mux_i64toa(iInt, m_ach);
    for (size_t i = 0; i < m_n; i++)
    {
        m_acs[i] = acsRestingStates[ANSI_ENDGOAL_NORMAL];
    }
}

/*! \brief Converts and Imports an long integer.
 *
 * \param lLong     long integer to convert and import.
 * \return         None.
 */

void mux_string::import(long lLong)
{
    // mux_ltoa() sets the '\0'.
    //
    m_n = mux_ltoa(lLong, m_ach);
    for (size_t i = 0; i < m_n; i++)
    {
        m_acs[i] = acsRestingStates[ANSI_ENDGOAL_NORMAL];
    }
}

/*! \brief Import a portion of another mux_string.
 *
 * \param sStr     mux_string to import.
 * \param nStart   Where to begin importing.
 * \return         None.
 */

void mux_string::import(const mux_string &sStr, size_t nStart)
{
    if (sStr.m_n <= nStart)
    {
        m_n = 0;
    }
    else
    {
        m_n = sStr.m_n - nStart;
        memcpy(m_ach, sStr.m_ach + nStart, m_n*sizeof(m_ach[0]));
        memcpy(m_acs, sStr.m_acs + nStart, m_n*sizeof(m_acs[0]));
    }
    m_ach[m_n] = '\0';
}

/*! \brief Import ANSI string.
 *
 * Parses the given ANSI string into a form which can be more-easily
 * navigated.
 *
 * \param pStr     ANSI-color encoded string to import.
 * \return         None.
 */

void mux_string::import(const char *pStr)
{
    m_n = 0;
    if (  NULL == pStr
       || '\0' == *pStr)
    {
        m_ach[m_n] = '\0';
        return;
    }

    size_t nLen = strlen(pStr);
    import(pStr, nLen);
}

/*! \brief Import ANSI string.
 *
 * Parses the given ANSI string into a form which can be more-easily
 * navigated.
 *
 * \param pStr     ANSI-color encoded string to import.
 * \param nLen     Length of portion of string, str, to import.
 * \return         None.
 */

void mux_string::import(const char *pStr, size_t nLen)
{
    m_n = 0;
    if (  NULL == pStr
       || '\0' == *pStr
       || 0 == nLen)
    {
        m_ach[m_n] = '\0';
        return;
    }

    if (LBUF_SIZE-1 < nLen)
    {
        nLen = LBUF_SIZE-1;
    }

    size_t nPos = 0;
    ANSI_ColorState cs = acsRestingStates[ANSI_ENDGOAL_NORMAL];
    size_t nAnsiLen = 0;

    while (nPos < nLen)
    {
        size_t nTokenLength0;
        size_t nTokenLength1;
        int iType = ANSI_lex(nLen - nPos, pStr + nPos,
                             &nTokenLength0, &nTokenLength1);

        if (iType == TOKEN_TEXT_ANSI)
        {
            // We always have room for the token since nLen (the total
            // amount of input to parse) is limited to LBUF_SIZE-1 and we
            // started this import with m_n = 0.
            //
            memcpy(m_ach + m_n, pStr + nPos, nTokenLength0 * sizeof(m_ach[0]));
            for (size_t i = m_n; i < m_n + nTokenLength0; i++)
            {
                m_acs[i] = cs;
            }

            m_n += nTokenLength0;
            nPos += nTokenLength0;

            nAnsiLen = nTokenLength1;
        }
        else
        {
            // TOKEN_ANSI
            //
            nAnsiLen = nTokenLength0;
        }
        ANSI_Parse_m(&cs, nAnsiLen, pStr+nPos);
        nPos += nAnsiLen;
    }
    m_ach[m_n] = '\0';
}

size_t mux_string::length(void) const
{
    return m_n;
}

void mux_string::prepend(const char cChar)
{
    size_t nMove = (m_n < LBUF_SIZE-1) ? m_n : LBUF_SIZE-2;

    memmove(m_ach + 1, m_ach, nMove * sizeof(m_ach[0]));
    memmove(m_acs + 1, m_acs, nMove * sizeof(m_acs[0]));

    m_ach[0] = cChar;
    m_acs[0] = acsRestingStates[ANSI_ENDGOAL_NORMAL];

    if (m_n < LBUF_SIZE-1)
    {
        m_n++;
    }
    m_ach[m_n] = '\0';
}

void mux_string::prepend(dbref num)
{
    mux_string *sStore = new mux_string(*this);

    import(num);
    append(*sStore);
    delete sStore;
}

void mux_string::prepend(long lLong)
{
    mux_string *sStore = new mux_string(*this);

    import(lLong);
    append(*sStore);
    delete sStore;
}

void mux_string::prepend(INT64 iInt)
{
    mux_string *sStore = new mux_string(*this);

    import(iInt);
    append(*sStore);
    delete sStore;
}

void mux_string::prepend(const mux_string &sStr)
{
    mux_string *sStore = new mux_string(*this);

    import(sStr);
    append(*sStore);
    delete sStore;
}

void mux_string::prepend(const char *pStr)
{
    mux_string *sStore = new mux_string(*this);

    import(pStr);
    append(*sStore);
    delete sStore;
}

void mux_string::prepend(const char *pStr, size_t n)
{
    mux_string *sStore = new mux_string(*this);

    import(pStr, n);
    append(*sStore);
    delete sStore;
}

void mux_string::replace_Chars(const mux_string &sTo, size_t nStart, size_t nLen)
{
    size_t nTo = sTo.m_n;
    size_t nMove = 0;
    size_t nCopy = nTo;
    if (nLen != nTo)
    {
        nMove = m_n-(nStart+nLen);
        if (LBUF_SIZE-1 < m_n + nTo - nLen)
        {
            if (LBUF_SIZE-1 < nStart + nTo)
            {
                nCopy = (LBUF_SIZE-1)-nStart;
                nMove = 0;
            }
            else
            {
                nMove = (LBUF_SIZE-1)-(nStart+nTo);
            }
        }
        if (nMove)
        {
            memmove(m_ach+nStart+nTo, m_ach+nStart + nLen, nMove * sizeof(m_ach[0]));
            memmove(m_acs+nStart+nTo, m_acs+nStart + nLen, nMove * sizeof(m_acs[0]));
        }
        m_n = nStart+nCopy+nMove;
    }
    memcpy(m_ach+nStart, sTo.m_ach, nCopy * sizeof(m_ach[0]));
    memcpy(m_acs+nStart, sTo.m_acs, nCopy * sizeof(m_acs[0]));
    m_ach[m_n] = '\0';
}

/*! \brief Reverses the string.
 *
 * \return         None.
 */

void mux_string::reverse(void)
{
    for (size_t i = 0, j = m_n-1; i < j; i++, j--)
    {
        char ch = m_ach[j];
        m_ach[j] = m_ach[i];
        m_ach[i] = ch;

        ANSI_ColorState cs = m_acs[j];
        m_acs[j] = m_acs[i];
        m_acs[i] = cs;
    }
}

/*! \brief Searches text for a specified pattern.
 *
 * \param pPattern Pointer to pattern to search for.
 * \param nPos     Pointer to value of position in string where pattern is found.
 * \param nStart   Position in string to begin looking at. Defaults to 0.
 * \return         True if found, false if not.
 */

bool mux_string::search
(
    const char *pPattern,
    size_t *nPos,
    size_t nStart
) const
{
    // Strip ANSI from pattern.
    //
    size_t nPat = 0;
    char *pPatBuf = strip_ansi(pPattern, &nPat);
    const char *pTarget = m_ach + nStart;

    size_t i = 0;
    bool bSucceeded = false;
    if (nPat == 1)
    {
        // We can optimize the single-character case.
        //
        const char *p = strchr(pTarget, pPatBuf[0]);
        if (p)
        {
            i = p - pTarget;
            bSucceeded = true;
        }
    }
    else if (nPat > 1)
    {
        // We have a multi-byte pattern.
        //
        bSucceeded = BMH_StringSearch(&i, nPat, pPatBuf, m_n - nStart, pTarget);
    }

    if (nPos)
    {
        *nPos = i;
    }
    return bSucceeded;
}

/*! \brief Searches text for a specified pattern.
 *
 * \param sPattern Reference to string to search for.
 * \param nPos     Pointer to value of position in string where pattern is found.
 * \param nStart   Position in string to begin looking at. Defaults to 0.
 * \return         True if found, false if not.
 */

bool mux_string::search
(
    const mux_string &sPattern,
    size_t *nPos,
    size_t nStart
) const
{
    // Strip ANSI from pattern.
    //
    const char *pTarget = m_ach + nStart;

    size_t i = 0;
    bool bSucceeded = false;
    if (1 == sPattern.m_n)
    {
        // We can optimize the single-character case.
        //
        const char *p = strchr(pTarget, sPattern.m_ach[0]);
        if (p)
        {
            i = p - pTarget;
            bSucceeded = true;
        }
    }
    else
    {
        // We have a multi-byte pattern.
        //
        bSucceeded = BMH_StringSearch(&i, sPattern.m_n, sPattern.m_ach, m_n - nStart, pTarget);
    }

    if (nPos)
    {
        *nPos = i;
    }
    return bSucceeded;
}

void mux_string::set_Char(size_t n, const char cChar)
{
    if (m_n <= n)
    {
        return;
    }
    m_ach[n] = cChar;
}

void mux_string::set_Color(size_t n, ANSI_ColorState csColor)
{
    if (m_n <= n)
    {
        return;
    }
    m_acs[n] = csColor;
}

/*! \brief Removes a specified set of characters from string.
 *
 * \param pStripSet Pointer to string of characters to remove.
 * \param nStart    Position in string to begin checking. Defaults to 0.
 * \param nLen      Number of characters in string to check. Defaults to LBUF_SIZE-1.
 * \return          None.
 */

void mux_string::strip(const char *pStripSet, size_t nStart, size_t nLen)
{
    static bool strip_table[UCHAR_MAX+1];

    if (  NULL == pStripSet
       || '\0' == pStripSet[0]
       || m_n <= nStart
       || 0 == nLen)
    {
        // Nothing to do.
        //
        return;
    }

    if (m_n-nStart < nLen)
    {
        nLen = m_n-nStart;
    }

    // Load set of characters to strip.
    //
    memset(strip_table, false, sizeof(strip_table));
    while (*pStripSet)
    {
        strip_table[(unsigned char)*pStripSet] = true;
        pStripSet++;
    }
    stripWithTable(strip_table, nStart, nLen);
}

void mux_string::stripWithTable(const bool strip_table[UCHAR_MAX+1], size_t nStart, size_t nLen)
{
    if (  m_n <= nStart
       || 0 == nLen)
    {
        // Nothing to do.
        //
        return;
    }

    if (m_n-nStart < nLen)
    {
        nLen = m_n-nStart;
    }

    bool bInStrip = false;
    size_t nStripStart = nStart;
    for (size_t i = nStart; i < nStart + nLen; i++)
    {
        if (  !bInStrip
           && strip_table[(unsigned char)m_ach[i]])
        {
            bInStrip = true;
            nStripStart = i;
        }
        else if (  bInStrip
                && !strip_table[(unsigned char)m_ach[i]])
        {
            // We've hit the end of a string to be stripped.
            //
            size_t nStrip = i - nStripStart;
            delete_Chars(nStripStart, nStrip);
            i -= nStrip;
            bInStrip = false;
        }
    }

    if (bInStrip)
    {
        if (m_n == nStart+nLen)
        {
            // We found chars to strip at the end of the string.
            // We can just truncate.
            //
            m_ach[nStripStart] = '\0';
            m_n = nStripStart;
        }
        else
        {
            size_t nStrip = nStart + nLen - nStripStart;
            delete_Chars(nStripStart, nStrip);
        }
    }
}

void mux_string::transform(mux_string &sFromSet, mux_string &sToSet, size_t nStart, size_t nLen)
{
    static unsigned char xfrmTable[UCHAR_MAX+1];

    if (m_n <= nStart)
    {
        return;
    }
    else if (m_n - nStart < nLen)
    {
        nLen = m_n - nStart;
    }

    // Set up table.
    //
    for (unsigned int c = 0; c <= UCHAR_MAX; c++)
    {
        xfrmTable[c] = (unsigned char)c;
    }

    unsigned char cFrom, cTo;
    size_t nSet = sFromSet.m_n;
    if (sToSet.m_n < nSet)
    {
        nSet = sToSet.m_n;
    }
    for (size_t i = 0; i < nSet; i++)
    {
        cFrom = (unsigned char)sFromSet.m_ach[i];
        cTo = (unsigned char)sToSet.m_ach[i];
        xfrmTable[cFrom] = cTo;
    }

    transformWithTable(xfrmTable, nStart, nLen);
}

void mux_string::transformWithTable(const unsigned char xfrmTable[256], size_t nStart, size_t nLen)
{
    if (m_n <= nStart)
    {
        return;
    }
    else if (m_n - nStart < nLen)
    {
        nLen = m_n - nStart;
    }

    for (size_t i = nStart; i < nStart + nLen; i++)
    {
        m_ach[i] = xfrmTable[(unsigned char)m_ach[i]];
    }
}

void mux_string::trim(const char ch, bool bLeft, bool bRight)
{
    if (  0 == m_n
       || (  !bLeft
          && !bRight ))
    {
        return;
    }

    if (bRight)
    {
        size_t iPos = m_n - 1;
        while (  ch == m_ach[iPos]
              && 0 < iPos)
        {
            iPos--;
        }

        if (iPos < m_n - 1)
        {
            m_n = iPos + 1;
            m_ach[m_n] = '\0';
        }
    }

    if (bLeft)
    {
        size_t iPos = 0;
        while (  ch == m_ach[iPos]
              && iPos < m_n)
        {
            iPos++;
        }

        if (0 < iPos)
        {
            delete_Chars(0, iPos);
        }
    }
}

void mux_string::trim(const char *p, bool bLeft, bool bRight)
{
    if (  0 == m_n
       || NULL == p
       || '\0' == p[0]
       || (  !bLeft
          && !bRight ))
    {
        return;
    }

    size_t n = strlen(p);

    if (1 == n)
    {
        trim(p[0], bLeft, bRight);
        return;
    }
    else
    {
        trim(p, n, bLeft, bRight);
    }
}

void mux_string::trim(const char *p, size_t n, bool bLeft, bool bRight)
{
    if (  0 == m_n
       || NULL == p
       || 0 == n
       || m_n < n
       || (  !bLeft
          && !bRight ))
    {
        return;
    }

    if (bRight)
    {
        size_t iPos = m_n - 1;
        size_t iDist = n - 1;
        while (  p[iDist] == m_ach[iPos]
              && 0 < iPos)
        {
            iPos--;
            iDist = (0 < iDist) ? iDist - 1 : n - 1;
        }

        if (iPos < m_n - 1)
        {
            m_n = iPos + 1;
            m_ach[m_n] = '\0';
        }
    }

    if (bLeft)
    {
        size_t iPos = 0;
        while (  p[iPos % n] == m_ach[iPos]
              && iPos < m_n)
        {
            iPos++;
        }

        if (0 < iPos)
        {
            delete_Chars(0, iPos);
        }
    }
}

void mux_string::truncate(size_t nLen)
{
    if (m_n <= nLen)
    {
        return;
    }
    m_n = nLen;
    m_ach[m_n] = '\0';
}

mux_words::mux_words(const mux_string &sStr) : m_s(&sStr)
{
    m_aiWordBegins[0] = 0;
    m_aiWordEnds[0] = 0;
    m_nWords = 0;
}

void mux_words::export_WordAnsi(LBUF_OFFSET n, char *buff, char **bufc)
{
    if (m_nWords < n)
    {
        return;
    }

    size_t iStart = m_aiWordBegins[n];
    size_t nLen = m_aiWordEnds[n] - iStart;
    m_s->export_TextAnsi(buff, bufc, iStart, nLen);
}

LBUF_OFFSET mux_words::find_Words(void)
{
    LBUF_OFFSET n = static_cast<LBUF_OFFSET>(m_s->m_n);
    LBUF_OFFSET nWords = 0;
    bool bPrev = true;

    for (LBUF_OFFSET i = 0; i < n; i++)
    {
        if (  !bPrev
           && m_aControl[(unsigned char)(m_s->m_ach[i])])
        {
            bPrev = true;
            m_aiWordEnds[nWords] = i;
            nWords++;
        }
        else if (bPrev)
        {
            bPrev = false;
            m_aiWordBegins[nWords] = i;
        }
    }
    if (!bPrev)
    {
        m_aiWordEnds[nWords] = n;
        nWords++;
    }
    m_nWords = nWords;
    return m_nWords;
}

LBUF_OFFSET mux_words::find_Words(const char *pDelim)
{
    size_t nDelim = 0;
    pDelim = strip_ansi(pDelim, &nDelim);

    size_t iPos = 0;
    LBUF_OFFSET iStart = 0;
    LBUF_OFFSET nWords = 0;
    bool bSucceeded = m_s->search(pDelim, &iPos, iStart);

    while (  bSucceeded
          && nWords + 1 < MAX_WORDS)
    {
        m_aiWordBegins[nWords] = iStart;
        m_aiWordEnds[nWords] = static_cast<LBUF_OFFSET>(iStart + iPos);
        nWords++;
        iStart = static_cast<LBUF_OFFSET>(iStart + iPos + nDelim);
        bSucceeded = m_s->search(pDelim, &iPos, iStart);
    }
    m_aiWordBegins[nWords] = iStart;
    m_aiWordEnds[nWords] = static_cast<LBUF_OFFSET>(m_s->m_n);
    nWords++;
    m_nWords = nWords;
    return nWords;
}

void mux_words::set_Control(const char *pControlSet)
{
    if (  NULL == pControlSet
       || '\0' == pControlSet[0])
    {
        // Nothing to do.
        //
        return;
    }

    // Load set of characters.
    //
    memset(m_aControl, false, sizeof(m_aControl));
    while (*pControlSet)
    {
        m_aControl[(unsigned char)*pControlSet] = true;
        pControlSet++;
    }
}

void mux_words::set_Control(const bool table[UCHAR_MAX+1])
{
    memcpy(m_aControl, table, sizeof(table));
}
