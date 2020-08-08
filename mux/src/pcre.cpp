/*! \file pcre.cpp
 * \brief Perl-Compatible Regular Expressions
 *
 * This is a library of functions to support regular expressions whose syntax
 * and semantics are as close as possible to those of the Perl 5 language. See
 * the file doc/Tech.Notes for some information on the internals.
 *
 * \author Philip Hazel <ph10@cam.ac.uk>
 *
 * \verbatim
           Copyright (c) 1997-2006 University of Cambridge

-----------------------------------------------------------------------------
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.

    * Neither the name of the University of Cambridge nor the names of its
      contributors may be used to endorse or promote products derived from
      this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
-----------------------------------------------------------------------------
\endverbatim
 *
 * Modified by Shawn Wagner for TinyMUX to fit in one file and exclude unused
 * things. If you want the full thing, see http://www.pcre.org.
 *
 * Updated with 7.1 sources.
 */

#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include <limits.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#ifdef HAVE_STDDEF_H
#include <stddef.h>
#endif // HAVE_STDDEF_H

/* Begin pcre_internal.h */
/* We need to have types that specify unsigned 16-bit and 32-bit integers. We
cannot determine these outside the compilation (e.g. by running a program as
part of "configure") because PCRE is often cross-compiled for use on other
systems. Instead we make use of the maximum sizes that are available at
preprocessor time in standard C environments. */

typedef UINT16 pcre_uint16;
typedef UINT32 pcre_uint32;

/* All character handling must be done as unsigned characters. Otherwise there
are problems with top-bit-set characters and functions such as isspace().
However, we leave the interface to the outside world as char *, because that
should make things easier for callers. We define a short type for unsigned char
to save lots of typing. I tried "uchar", but it causes problems on Digital
Unix, where it is defined in sys/types, so use "uschar" instead. */

typedef unsigned char uschar;

/* This is an unsigned int value that no character can ever have. UTF-8
characters only go up to 0x7fffffff (though Unicode doesn't go beyond
0x0010ffff). */

#define NOTACHAR 0xffffffff

/* PCRE is able to support several different kinds of newline (CR, LF, CRLF,
"any" and "anycrlf" at present). The following macros are used to package up
testing for newlines. NLBLOCK, PSSTART, and PSEND are defined in the various
modules to indicate in which datablock the parameters exist, and what the
start/end of string field names are. */

#define NLTYPE_FIXED    0     /* Newline is a fixed length string */
#define NLTYPE_ANY      1     /* Newline is any Unicode line ending */
#define NLTYPE_ANYCRLF  2     /* Newline is CR, LF, or CRLF */

/* This macro checks for a newline at the given position */

#define IS_NEWLINE(p) \
  ((NLBLOCK->nltype != NLTYPE_FIXED)? \
    ((p) < NLBLOCK->PSEND && \
     _pcre_is_newline((p), NLBLOCK->nltype, NLBLOCK->PSEND, &(NLBLOCK->nllen),\
       utf8)) \
    : \
    ((p) <= NLBLOCK->PSEND - NLBLOCK->nllen && \
     (p)[0] == NLBLOCK->nl[0] && \
     (NLBLOCK->nllen == 1 || (p)[1] == NLBLOCK->nl[1]) \
    ) \
  )

/* This macro checks for a newline immediately preceding the given position */

#define WAS_NEWLINE(p) \
  ((NLBLOCK->nltype != NLTYPE_FIXED)? \
    ((p) > NLBLOCK->PSSTART && \
     _pcre_was_newline((p), NLBLOCK->nltype, NLBLOCK->PSSTART, \
       &(NLBLOCK->nllen), utf8)) \
    : \
    ((p) >= NLBLOCK->PSSTART + NLBLOCK->nllen && \
     (p)[-NLBLOCK->nllen] == NLBLOCK->nl[0] && \
     (NLBLOCK->nllen == 1 || (p)[-NLBLOCK->nllen+1] == NLBLOCK->nl[1]) \
    ) \
  )

/* When PCRE is compiled as a C++ library, the subject pointer can be replaced
with a custom type. This makes it possible, for example, to allow pcre_exec()
to process subject strings that are discontinuous by using a smart pointer
class. It must always be possible to inspect all of the subject string in
pcre_exec() because of the way it backtracks. Two macros are required in the
normal case, for sign-unspecified and unsigned char pointers. The former is
used for the external interface and appears in pcre.h, which is why its name
must begin with PCRE_. */

#define USPTR const unsigned char *

/* Include the public PCRE header and the definitions of UCP character property
values. */

#include "pcre.h"

/* End pcre_internal.h */

/* Begin config.h */
#define LINK_SIZE 2
#define MATCH_LIMIT 100000
#define MATCH_LIMIT_RECURSION MATCH_LIMIT
#define MAX_NAME_SIZE 32
#define MAX_NAME_COUNT 10000
#define MAX_DUPLENGTH 30000
#define NEWLINE '\n'
/* End config.h */

/* Begin pcre_internal.h */
/* This header contains definitions that are shared between the different
modules, but which are not relevant to the exported API. This includes some
functions whose names all begin with "_pcre_". */

/* PCRE keeps offsets in its compiled code as 2-byte quantities (always stored
in big-endian order) by default. These are used, for example, to link from the
start of a subpattern to its alternatives and its end. The use of 2 bytes per
offset limits the size of the compiled regex to around 64K, which is big enough
for almost everybody. However, I received a request for an even bigger limit.
For this reason, and also to make the code easier to maintain, the storing and
loading of offsets from the byte string is now handled by the macros that are
defined here.

The macros are controlled by the value of LINK_SIZE. This defaults to 2 in
the config.h file, but can be overridden by using -D on the command line. This
is automated on Unix systems via the "configure" command. */

#define PUT(a,n,d)   \
  (a[n] = static_cast<uschar>((d) >> 8)), \
  (a[(n)+1] = static_cast<uschar>((d) & 255))

#define GET(a,n) \
  (((a)[n] << 8) | (a)[(n)+1])

#define MAX_PATTERN_SIZE (1 << 16)


/* Convenience macro defined in terms of the others */

#define PUTINC(a,n,d)   PUT(a,n,d), a += LINK_SIZE


/* PCRE uses some other 2-byte quantities that do not change when the size of
offsets changes. There are used for repeat counts and for other things such as
capturing parenthesis numbers in back references. */

#define PUT2(a,n,d)   \
  a[n] = static_cast<uschar>((d) >> 8); \
  a[(n)+1] = static_cast<uschar>((d) & 255)

#define GET2(a,n) \
  (((a)[n] << 8) | (a)[(n)+1])

#define PUT2INC(a,n,d)  PUT2(a,n,d), a += 2


/* These are the public options that can change during matching. */

#define PCRE_IMS (PCRE_CASELESS|PCRE_MULTILINE|PCRE_DOTALL)

/* Private options flags start at the most significant end of the four bytes.
The public options defined in pcre.h start at the least significant end. Make
sure they don't overlap! The bits are getting a bit scarce now -- when we run
out, there is a dummy word in the structure that could be used for the private
bits. */

#define PCRE_NOPARTIAL     0x80000000  /* can't use partial with this regex */
#define PCRE_FIRSTSET      0x40000000  /* first_byte is set */
#define PCRE_REQCHSET      0x20000000  /* req_byte is set */
#define PCRE_STARTLINE     0x10000000  /* start after \n for multiline */
#define PCRE_JCHANGED      0x08000000  /* j option changes within regex */

/* Options for the "extra" block produced by pcre_study(). */

#define PCRE_STUDY_MAPPED   0x01     /* a map of starting chars exists */

/* Masks for identifying the public options that are permitted at compile
time, run time, or study time, respectively. */

#define PCRE_NEWLINE_BITS (PCRE_NEWLINE_CR|PCRE_NEWLINE_LF|PCRE_NEWLINE_ANY)

#define PUBLIC_OPTIONS \
  (PCRE_CASELESS|PCRE_EXTENDED|PCRE_ANCHORED|PCRE_MULTILINE| \
   PCRE_DOTALL|PCRE_DOLLAR_ENDONLY|PCRE_EXTRA|PCRE_UNGREEDY|PCRE_UTF8| \
   PCRE_NO_AUTO_CAPTURE|PCRE_NO_UTF8_CHECK|PCRE_AUTO_CALLOUT|PCRE_FIRSTLINE| \
   PCRE_DUPNAMES|PCRE_NEWLINE_BITS)

#define PUBLIC_EXEC_OPTIONS \
  (PCRE_ANCHORED|PCRE_NOTBOL|PCRE_NOTEOL|PCRE_NOTEMPTY|PCRE_NO_UTF8_CHECK| \
   PCRE_PARTIAL|PCRE_NEWLINE_BITS)

#define PUBLIC_STUDY_OPTIONS 0   /* None defined */

/* Magic number to provide a small check against being handed junk. Also used
to detect whether a pattern was compiled on a host of different endianness. */

#define MAGIC_NUMBER  0x50435245UL   /* 'PCRE' */

/* Negative values for the firstchar and reqchar variables */

#define REQ_UNSET (-2)
#define REQ_NONE  (-1)

/* Flags added to firstbyte or reqbyte; a "non-literal" item is either a
variable-length repeat, or a anything other than literal characters. */

#define REQ_CASELESS 0x0100    /* indicates caselessness */
#define REQ_VARY     0x0200    /* reqbyte followed non-literal item */

/* Miscellaneous definitions */

/* Escape items that are just an encoding of a particular data value. */

#ifndef ESC_e
#define ESC_e 27
#endif

#ifndef ESC_f
#define ESC_f '\f'
#endif

#ifndef ESC_n
#define ESC_n '\n'
#endif

#ifndef ESC_r
#define ESC_r '\r'
#endif

/* We can't officially use ESC_t because it is a POSIX reserved identifier
(presumably because of all the others like size_t). */

#ifndef ESC_tee
#define ESC_tee '\t'
#endif

/* Flag bits and data types for the extended class (OP_XCLASS) for classes that
contain UTF-8 characters with values greater than 255. */

#define XCL_NOT    0x01    /* Flag: this is a negative class */
#define XCL_MAP    0x02    /* Flag: a 32-byte map is present */

#define XCL_END       0    /* Marks end of individual items */
#define XCL_SINGLE    1    /* Single item (one multibyte char) follows */
#define XCL_RANGE     2    /* A range (two multibyte chars) follows */
#define XCL_PROP      3    /* Unicode property (2-byte property code follows) */
#define XCL_NOTPROP   4    /* Unicode inverted property (ditto) */

/* These are escaped items that aren't just an encoding of a particular data
value such as \n. They must have non-zero values, as check_escape() returns
their negation. Also, they must appear in the same order as in the opcode
definitions below, up to ESC_z. There's a dummy for OP_ANY because it
corresponds to "." rather than an escape sequence. The final one must be
ESC_REF as subsequent values are used for backreferences (\1, \2, \3, etc).
There are two tests in the code for an escape greater than ESC_b and less than
ESC_Z to detect the types that may be repeated. These are the types that
consume characters. If any new escapes are put in between that don't consume a
character, that code will have to change. */

enum { ESC_A = 1, ESC_G, ESC_B, ESC_b, ESC_D, ESC_d, ESC_S, ESC_s, ESC_W,
       ESC_w, ESC_dum1, ESC_C, ESC_P, ESC_p, ESC_R, ESC_X, ESC_Z, ESC_z,
       ESC_E, ESC_Q, ESC_k, ESC_REF };


/* Opcode table: OP_BRA must be last, as all values >= it are used for brackets
that extract substrings. Starting from 1 (i.e. after OP_END), the values up to
OP_EOD must correspond in order to the list of escapes immediately above.

To keep stored, compiled patterns compatible, new opcodes should be added
immediately before OP_BRA, where (since release 7.0) a gap is left for this
purpose.

*** NOTE NOTE NOTE *** Whenever this list is updated, the two macro definitions
that follow must also be updated to match. There is also a table called
"coptable" in pcre_dfa_exec.c that must be updated. */

enum {
  OP_END,            /* 0 End of pattern */

  /* Values corresponding to backslashed metacharacters */

  OP_SOD,            /* 1 Start of data: \A */
  OP_SOM,            /* 2 Start of match (subject + offset): \G */
  OP_NOT_WORD_BOUNDARY,  /*  3 \B */
  OP_WORD_BOUNDARY,      /*  4 \b */
  OP_NOT_DIGIT,          /*  5 \D */
  OP_DIGIT,              /*  6 \d */
  OP_NOT_WHITESPACE,     /*  7 \S */
  OP_WHITESPACE,         /*  8 \s */
  OP_NOT_WORDCHAR,       /*  9 \W */
  OP_WORDCHAR,           /* 10 \w */
  OP_ANY,            /* 11 Match any character */
  OP_ANYBYTE,        /* 12 Match any byte (\C); different to OP_ANY for UTF-8 */
  OP_NOTPROP,        /* 13 \P (not Unicode property) */
  OP_PROP,           /* 14 \p (Unicode property) */
  OP_ANYNL,          /* 15 \R (any newline sequence) */
  OP_EXTUNI,         /* 16 \X (extended Unicode sequence */
  OP_EODN,           /* 17 End of data or \n at end of data: \Z. */
  OP_EOD,            /* 18 End of data: \z */

  OP_OPT,            /* 19 Set runtime options */
  OP_CIRC,           /* 20 Start of line - varies with multiline switch */
  OP_DOLL,           /* 21 End of line - varies with multiline switch */
  OP_CHAR,           /* 22 Match one character, casefully */
  OP_CHARNC,         /* 23 Match one character, caselessly */
  OP_NOT,            /* 24 Match one character, not the following one */

  OP_STAR,           /* 25 The maximizing and minimizing versions of */
  OP_MINSTAR,        /* 26 these six opcodes must come in pairs, with */
  OP_PLUS,           /* 27 the minimizing one second. */
  OP_MINPLUS,        /* 28 This first set applies to single characters.*/
  OP_QUERY,          /* 29 */
  OP_MINQUERY,       /* 30 */

  OP_UPTO,           /* 31 From 0 to n matches */
  OP_MINUPTO,        /* 32 */
  OP_EXACT,          /* 33 Exactly n matches */

  OP_POSSTAR,        /* 34 Possessified star */
  OP_POSPLUS,        /* 35 Possessified plus */
  OP_POSQUERY,       /* 36 Posesssified query */
  OP_POSUPTO,        /* 37 Possessified upto */

  OP_NOTSTAR,        /* 38 The maximizing and minimizing versions of */
  OP_NOTMINSTAR,     /* 39 these six opcodes must come in pairs, with */
  OP_NOTPLUS,        /* 40 the minimizing one second. They must be in */
  OP_NOTMINPLUS,     /* 41 exactly the same order as those above. */
  OP_NOTQUERY,       /* 42 This set applies to "not" single characters. */
  OP_NOTMINQUERY,    /* 43 */

  OP_NOTUPTO,        /* 44 From 0 to n matches */
  OP_NOTMINUPTO,     /* 45 */
  OP_NOTEXACT,       /* 46 Exactly n matches */

  OP_NOTPOSSTAR,     /* 47 Possessified versions */
  OP_NOTPOSPLUS,     /* 48 */
  OP_NOTPOSQUERY,    /* 49 */
  OP_NOTPOSUPTO,     /* 50 */

  OP_TYPESTAR,       /* 51 The maximizing and minimizing versions of */
  OP_TYPEMINSTAR,    /* 52 these six opcodes must come in pairs, with */
  OP_TYPEPLUS,       /* 53 the minimizing one second. These codes must */
  OP_TYPEMINPLUS,    /* 54 be in exactly the same order as those above. */
  OP_TYPEQUERY,      /* 55 This set applies to character types such as \d */
  OP_TYPEMINQUERY,   /* 56 */

  OP_TYPEUPTO,       /* 57 From 0 to n matches */
  OP_TYPEMINUPTO,    /* 58 */
  OP_TYPEEXACT,      /* 59 Exactly n matches */

  OP_TYPEPOSSTAR,    /* 60 Possessified versions */
  OP_TYPEPOSPLUS,    /* 61 */
  OP_TYPEPOSQUERY,   /* 62 */
  OP_TYPEPOSUPTO,    /* 63 */

  OP_CRSTAR,         /* 64 The maximizing and minimizing versions of */
  OP_CRMINSTAR,      /* 65 all these opcodes must come in pairs, with */
  OP_CRPLUS,         /* 66 the minimizing one second. These codes must */
  OP_CRMINPLUS,      /* 67 be in exactly the same order as those above. */
  OP_CRQUERY,        /* 68 These are for character classes and back refs */
  OP_CRMINQUERY,     /* 69 */
  OP_CRRANGE,        /* 70 These are different to the three sets above. */
  OP_CRMINRANGE,     /* 71 */

  OP_CLASS,          /* 72 Match a character class, chars < 256 only */
  OP_NCLASS,         /* 73 Same, but the bitmap was created from a negative
                           class - the difference is relevant only when a UTF-8
                           character > 255 is encountered. */

  OP_XCLASS,         /* 74 Extended class for handling UTF-8 chars within the
                           class. This does both positive and negative. */

  OP_REF,            /* 75 Match a back reference */
  OP_RECURSE,        /* 76 Match a numbered subpattern (possibly recursive) */
  OP_CALLOUT,        /* 77 Call out to external function if provided */

  OP_ALT,            /* 78 Start of alternation */
  OP_KET,            /* 79 End of group that doesn't have an unbounded repeat */
  OP_KETRMAX,        /* 80 These two must remain together and in this */
  OP_KETRMIN,        /* 81 order. They are for groups the repeat for ever. */

  /* The assertions must come before BRA, CBRA, ONCE, and COND.*/

  OP_ASSERT,         /* 82 Positive lookahead */
  OP_ASSERT_NOT,     /* 83 Negative lookahead */
  OP_ASSERTBACK,     /* 84 Positive lookbehind */
  OP_ASSERTBACK_NOT, /* 85 Negative lookbehind */
  OP_REVERSE,        /* 86 Move pointer back - used in lookbehind assertions */

  /* ONCE, BRA, CBRA, and COND must come after the assertions, with ONCE first,
  as there's a test for >= ONCE for a subpattern that isn't an assertion. */

  OP_ONCE,           /* 87 Atomic group */
  OP_BRA,            /* 88 Start of non-capturing bracket */
  OP_CBRA,           /* 89 Start of capturing bracket */
  OP_COND,           /* 90 Conditional group */

  /* These three must follow the previous three, in the same order. There's a
  check for >= SBRA to distinguish the two sets. */

  OP_SBRA,           /* 91 Start of non-capturing bracket, check empty  */
  OP_SCBRA,          /* 92 Start of capturing bracket, check empty */
  OP_SCOND,          /* 93 Conditional group, check empty */

  OP_CREF,           /* 94 Used to hold a capture number as condition */
  OP_RREF,           /* 95 Used to hold a recursion number as condition */
  OP_DEF,            /* 96 The DEFINE condition */

  OP_BRAZERO,        /* 97 These two must remain together and in this */
  OP_BRAMINZERO      /* 98 order. */
};


/* This macro defines textual names for all the opcodes. These are used only
for debugging. The macro is referenced only in pcre_printint.c. */

#define OP_NAME_LIST \
  "End", "\\A", "\\G", "\\B", "\\b", "\\D", "\\d",                \
  "\\S", "\\s", "\\W", "\\w", "Any", "Anybyte",                   \
  "notprop", "prop", "anynl", "extuni",                           \
  "\\Z", "\\z",                                                   \
  "Opt", "^", "$", "char", "charnc", "not",                       \
  "*", "*?", "+", "+?", "?", "??", "{", "{", "{",                 \
  "*+","++", "?+", "{",                                           \
  "*", "*?", "+", "+?", "?", "??", "{", "{", "{",                 \
  "*+","++", "?+", "{",                                           \
  "*", "*?", "+", "+?", "?", "??", "{", "{", "{",                 \
  "*+","++", "?+", "{",                                           \
  "*", "*?", "+", "+?", "?", "??", "{", "{",                      \
  "class", "nclass", "xclass", "Ref", "Recurse", "Callout",       \
  "Alt", "Ket", "KetRmax", "KetRmin", "Assert", "Assert not",     \
  "AssertB", "AssertB not", "Reverse",                            \
  "Once", "Bra 0", "Bra", "Cond", "SBra 0", "SBra", "SCond",      \
  "Cond ref", "Cond rec", "Cond def", "Brazero", "Braminzero"


/* This macro defines the length of fixed length operations in the compiled
regex. The lengths are used when searching for specific things, and also in the
debugging printing of a compiled regex. We use a macro so that it can be
defined close to the definitions of the opcodes themselves.

As things have been extended, some of these are no longer fixed lenths, but are
minima instead. For example, the length of a single-character repeat may vary
in UTF-8 mode. The code that uses this table must know about such things. */

#define OP_LENGTHS \
  1,                             /* End                                    */ \
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  /* \A, \G, \B, \B, \D, \d, \S, \s, \W, \w */ \
  1, 1,                          /* Any, Anybyte                           */ \
  3, 3, 1, 1,                    /* NOTPROP, PROP, EXTUNI, ANYNL           */ \
  1, 1, 2, 1, 1,                 /* \Z, \z, Opt, ^, $                      */ \
  2,                             /* Char  - the minimum length             */ \
  2,                             /* Charnc  - the minimum length           */ \
  2,                             /* not                                    */ \
  /* Positive single-char repeats                            ** These are  */ \
  2, 2, 2, 2, 2, 2,              /* *, *?, +, +?, ?, ??      ** minima in  */ \
  4, 4, 4,                       /* upto, minupto, exact     ** UTF-8 mode */ \
  2, 2, 2, 4,                    /* *+, ++, ?+, upto+                      */ \
  /* Negative single-char repeats - only for chars < 256                   */ \
  2, 2, 2, 2, 2, 2,              /* NOT *, *?, +, +?, ?, ??                */ \
  4, 4, 4,                       /* NOT upto, minupto, exact               */ \
  2, 2, 2, 4,                    /* Possessive *, +, ?, upto               */ \
  /* Positive type repeats                                                 */ \
  2, 2, 2, 2, 2, 2,              /* Type *, *?, +, +?, ?, ??               */ \
  4, 4, 4,                       /* Type upto, minupto, exact              */ \
  2, 2, 2, 4,                    /* Possessive *+, ++, ?+, upto+           */ \
  /* Character class & ref repeats                                         */ \
  1, 1, 1, 1, 1, 1,              /* *, *?, +, +?, ?, ??                    */ \
  5, 5,                          /* CRRANGE, CRMINRANGE                    */ \
 33,                             /* CLASS                                  */ \
 33,                             /* NCLASS                                 */ \
  0,                             /* XCLASS - variable length               */ \
  3,                             /* REF                                    */ \
  1+LINK_SIZE,                   /* RECURSE                                */ \
  2+2*LINK_SIZE,                 /* CALLOUT                                */ \
  1+LINK_SIZE,                   /* Alt                                    */ \
  1+LINK_SIZE,                   /* Ket                                    */ \
  1+LINK_SIZE,                   /* KetRmax                                */ \
  1+LINK_SIZE,                   /* KetRmin                                */ \
  1+LINK_SIZE,                   /* Assert                                 */ \
  1+LINK_SIZE,                   /* Assert not                             */ \
  1+LINK_SIZE,                   /* Assert behind                          */ \
  1+LINK_SIZE,                   /* Assert behind not                      */ \
  1+LINK_SIZE,                   /* Reverse                                */ \
  1+LINK_SIZE,                   /* ONCE                                   */ \
  1+LINK_SIZE,                   /* BRA                                    */ \
  3+LINK_SIZE,                   /* CBRA                                   */ \
  1+LINK_SIZE,                   /* COND                                   */ \
  1+LINK_SIZE,                   /* SBRA                                   */ \
  3+LINK_SIZE,                   /* SCBRA                                  */ \
  1+LINK_SIZE,                   /* SCOND                                  */ \
  3,                             /* CREF                                   */ \
  3,                             /* RREF                                   */ \
  1,                             /* DEF                                    */ \
  1, 1,                          /* BRAZERO, BRAMINZERO                    */ \


/* A magic value for OP_RREF to indicate the "any recursion" condition. */

#define RREF_ANY  0xffff

/* Error code numbers. They are given names so that they can more easily be
tracked. */

enum { ERR0,  ERR1,  ERR2,  ERR3,  ERR4,  ERR5,  ERR6,  ERR7,  ERR8,  ERR9,
       ERR10, ERR11, ERR12, ERR13, ERR14, ERR15, ERR16, ERR17, ERR18, ERR19,
       ERR20, ERR21, ERR22, ERR23, ERR24, ERR25, ERR26, ERR27, ERR28, ERR29,
       ERR30, ERR31, ERR32, ERR33, ERR34, ERR35, ERR36, ERR37, ERR38, ERR39,
       ERR40, ERR41, ERR42, ERR43, ERR44, ERR45, ERR46, ERR47, ERR48, ERR49,
       ERR50, ERR51, ERR52, ERR53, ERR54, ERR55, ERR56, ERR57 };

/* The real format of the start of the pcre block; the index of names and the
code vector run on as long as necessary after the end. We store an explicit
offset to the name table so that if a regex is compiled on one host, saved, and
then run on another where the size of pointers is different, all might still
be well. For the case of compiled-on-4 and run-on-8, we include an extra
pointer that is always NULL. For future-proofing, a few dummy fields were
originally included - even though you can never get this planning right - but
there is only one left now.

NOTE NOTE NOTE:
Because people can now save and re-use compiled patterns, any additions to this
structure should be made at the end, and something earlier (e.g. a new
flag in the options or one of the dummy fields) should indicate that the new
fields are present. Currently PCRE always sets the dummy fields to zero.
NOTE NOTE NOTE:
*/

typedef struct real_pcre {
  pcre_uint32 magic_number;
  pcre_uint32 size;               /* Total that was malloced */
  pcre_uint32 options;
  pcre_uint32 dummy1;             /* For future use, maybe */

  pcre_uint16 top_bracket;
  pcre_uint16 top_backref;
  pcre_uint16 first_byte;
  pcre_uint16 req_byte;
  pcre_uint16 name_table_offset;  /* Offset to name table that follows */
  pcre_uint16 name_entry_size;    /* Size of any name items */
  pcre_uint16 name_count;         /* Number of name items */
  pcre_uint16 ref_count;          /* Reference count */

  const unsigned char *tables;    /* Pointer to tables or NULL for std */
  const unsigned char *nullpad;   /* NULL padding */
} real_pcre;

/* The format of the block used to store data from pcre_study(). The same
remark (see NOTE above) about extending this structure applies. */

typedef struct pcre_study_data {
  pcre_uint32 size;               /* Total that was malloced */
  pcre_uint32 options;
  uschar start_bits[32];
} pcre_study_data;

/* Structure for passing "static" information around between the functions
doing the compiling, so that they are thread-safe. */

typedef struct compile_data {
  const uschar *lcc;            /* Points to lower casing table */
  const uschar *fcc;            /* Points to case-flipping table */
  const uschar *cbits;          /* Points to character type table */
  const uschar *ctypes;         /* Points to table of type maps */
  const uschar *start_workspace;/* The start of working space */
  const uschar *start_code;     /* The start of the compiled code */
  const uschar *start_pattern;  /* The start of the pattern */
  const uschar *end_pattern;    /* The end of the pattern */
  uschar *hwm;                  /* High watermark of workspace */
  uschar *name_table;           /* The name/number table */
  int  names_found;             /* Number of entries so far */
  int  name_entry_size;         /* Size of each entry */
  int  bracount;                /* Count of capturing parens */
  int  top_backref;             /* Maximum back reference */
  unsigned int backref_map;     /* Bitmap of low back refs */
  int  external_options;        /* External (initial) options */
  int  req_varyopt;             /* "After variable item" flag for reqbyte */
  bool nopartial;               /* Set true if partial won't work */
  int  nltype;                  /* Newline type */
  int  nllen;                   /* Newline string length */
  uschar nl[4];                 /* Newline string when fixed length */
} compile_data;

/* Structure for maintaining a chain of pointers to the currently incomplete
branches, for testing for left recursion. */

typedef struct branch_chain {
  struct branch_chain *outer;
  uschar *current;
} branch_chain;

/* Structure for items in a linked list that represents an explicit recursive
call within the pattern. */

typedef struct recursion_info {
  struct recursion_info *prevrec; /* Previous recursion record (or NULL) */
  int group_num;                /* Number of group that was called */
  const uschar *after_call;     /* "Return value": points after the call in the expr */
  USPTR save_start;             /* Old value of md->start_match */
  int *offset_save;             /* Pointer to start of saved offsets */
  int saved_max;                /* Number of saved offsets */
} recursion_info;

/* When compiling in a mode that doesn't use recursive calls to match(),
a structure is used to remember local variables on the heap. It is defined in
pcre_exec.c, close to the match() function, so that it is easy to keep it in
step with any changes of local variable. However, the pointer to the current
frame must be saved in some "static" place over a longjmp(). We declare the
structure here so that we can put a pointer in the match_data structure. NOTE:
This isn't used for a "normal" compilation of pcre. */

/* Structure for building a chain of data for holding the values of the subject
pointer at the start of each subpattern, so as to detect when an empty string
has been matched by a subpattern - to break infinite loops. */

typedef struct eptrblock {
  struct eptrblock *epb_prev;
  USPTR epb_saved_eptr;
} eptrblock;


/* Structure for passing "static" information around between the functions
doing traditional NFA matching, so that they are thread-safe. */

typedef struct match_data {
  unsigned long int match_call_count;      /* As it says */
  unsigned long int match_limit;           /* As it says */
  unsigned long int match_limit_recursion; /* As it says */
  int   *offset_vector;         /* Offset vector */
  int    offset_end;            /* One past the end */
  int    offset_max;            /* The maximum usable for return data */
  int    nltype;                /* Newline type */
  int    nllen;                 /* Newline string length */
  uschar nl[4];                 /* Newline string when fixed */
  const uschar *lcc;            /* Points to lower casing table */
  const uschar *ctypes;         /* Points to table of type maps */
  bool   offset_overflow;       /* Set if too many extractions */
  bool   notbol;                /* NOTBOL flag */
  bool   noteol;                /* NOTEOL flag */
  bool   utf8;                  /* UTF8 flag */
  bool   endonly;               /* Dollar not before final \n */
  bool   notempty;              /* Empty string match not wanted */
  bool   partial;               /* PARTIAL flag */
  bool   hitend;                /* Hit the end of the subject at some point */
  const uschar *start_code;     /* For use when recursing */
  USPTR  start_subject;         /* Start of the subject string */
  USPTR  end_subject;           /* End of the subject string */
  USPTR  start_match;           /* Start of this match attempt */
  USPTR  end_match_ptr;         /* Subject position at end match */
  int    end_offset_top;        /* Highwater mark at end of match */
  int    capture_last;          /* Most recent capture number */
  int    start_offset;          /* The start offset value */
  eptrblock *eptrchain;         /* Chain of eptrblocks for tail recursions */
  int    eptrn;                 /* Next free eptrblock */
  recursion_info *recursive;    /* Linked list of recursion data */
  void  *callout_data;          /* To pass back to callouts */
} match_data;

/* Bit definitions for entries in the pcre_ctypes table. */

#define ctype_space   0x01
#define ctype_letter  0x02
#define ctype_digit   0x04
#define ctype_xdigit  0x08
#define ctype_word    0x10   /* alphameric or '_' */
#define ctype_meta    0x80   /* regexp meta char or zero (end pattern) */

/* Offsets for the bitmap tables in pcre_cbits. Each table contains a set
of bits for a class map. Some classes are built by combining these tables. */

#define cbit_space     0      /* [:space:] or \s */
#define cbit_xdigit   32      /* [:xdigit:] */
#define cbit_digit    64      /* [:digit:] or \d */
#define cbit_upper    96      /* [:upper:] */
#define cbit_lower   128      /* [:lower:] */
#define cbit_word    160      /* [:word:] or \w */
#define cbit_graph   192      /* [:graph:] */
#define cbit_print   224      /* [:print:] */
#define cbit_punct   256      /* [:punct:] */
#define cbit_cntrl   288      /* [:cntrl:] */
#define cbit_length  320      /* Length of the cbits table */

/* Offsets of the various tables from the base tables pointer, and
total length. */

#define lcc_offset      0
#define fcc_offset    256
#define cbits_offset  512
#define ctypes_offset (cbits_offset + cbit_length)
#define tables_length (ctypes_offset + 256)

/* Layout of the UCP type table that translates property names into types and
codes. */

typedef struct {
  const char *name;
  pcre_uint16 type;
  pcre_uint16 value;
} ucp_type_table;

/* End pcre_internal.h */
/* Begin pcre_chartables.c */
/*************************************************
*      Perl-Compatible Regular Expressions       *
*************************************************/

/* This file contains character tables that are used when no external tables
are passed to PCRE by the application that calls it. The tables are used only
for characters whose code values are less than 256.

This is a default version of the tables that assumes ASCII encoding. A program
called dftables (which is distributed with PCRE) can be used to build
alternative versions of this file. This is necessary if you are running in an
EBCDIC environment, or if you want to default to a different encoding, for
example ISO-8859-1. When dftables is run, it creates these tables in the
current locale. If PCRE is configured with --enable-rebuild-chartables, this
happens automatically.

The following #include is present because without it gcc 4.x may remove the
array definition from the final binary if PCRE is built into a static library
and dead code stripping is activated. This leads to link errors. Pulling in the
header ensures that the array gets flagged as "someone outside this compilation
unit might reference this" and so it will always be supplied to the linker. */

const unsigned char _pcre_default_tables[] = {

/* This table is a lower casing table. */

    0,  1,  2,  3,  4,  5,  6,  7,
    8,  9, 10, 11, 12, 13, 14, 15,
   16, 17, 18, 19, 20, 21, 22, 23,
   24, 25, 26, 27, 28, 29, 30, 31,
   32, 33, 34, 35, 36, 37, 38, 39,
   40, 41, 42, 43, 44, 45, 46, 47,
   48, 49, 50, 51, 52, 53, 54, 55,
   56, 57, 58, 59, 60, 61, 62, 63,
   64, 97, 98, 99,100,101,102,103,
  104,105,106,107,108,109,110,111,
  112,113,114,115,116,117,118,119,
  120,121,122, 91, 92, 93, 94, 95,
   96, 97, 98, 99,100,101,102,103,
  104,105,106,107,108,109,110,111,
  112,113,114,115,116,117,118,119,
  120,121,122,123,124,125,126,127,
  128,129,130,131,132,133,134,135,
  136,137,138,139,140,141,142,143,
  144,145,146,147,148,149,150,151,
  152,153,154,155,156,157,158,159,
  160,161,162,163,164,165,166,167,
  168,169,170,171,172,173,174,175,
  176,177,178,179,180,181,182,183,
  184,185,186,187,188,189,190,191,
  192,193,194,195,196,197,198,199,
  200,201,202,203,204,205,206,207,
  208,209,210,211,212,213,214,215,
  216,217,218,219,220,221,222,223,
  224,225,226,227,228,229,230,231,
  232,233,234,235,236,237,238,239,
  240,241,242,243,244,245,246,247,
  248,249,250,251,252,253,254,255,

/* This table is a case flipping table. */

    0,  1,  2,  3,  4,  5,  6,  7,
    8,  9, 10, 11, 12, 13, 14, 15,
   16, 17, 18, 19, 20, 21, 22, 23,
   24, 25, 26, 27, 28, 29, 30, 31,
   32, 33, 34, 35, 36, 37, 38, 39,
   40, 41, 42, 43, 44, 45, 46, 47,
   48, 49, 50, 51, 52, 53, 54, 55,
   56, 57, 58, 59, 60, 61, 62, 63,
   64, 97, 98, 99,100,101,102,103,
  104,105,106,107,108,109,110,111,
  112,113,114,115,116,117,118,119,
  120,121,122, 91, 92, 93, 94, 95,
   96, 65, 66, 67, 68, 69, 70, 71,
   72, 73, 74, 75, 76, 77, 78, 79,
   80, 81, 82, 83, 84, 85, 86, 87,
   88, 89, 90,123,124,125,126,127,
  128,129,130,131,132,133,134,135,
  136,137,138,139,140,141,142,143,
  144,145,146,147,148,149,150,151,
  152,153,154,155,156,157,158,159,
  160,161,162,163,164,165,166,167,
  168,169,170,171,172,173,174,175,
  176,177,178,179,180,181,182,183,
  184,185,186,187,188,189,190,191,
  192,193,194,195,196,197,198,199,
  200,201,202,203,204,205,206,207,
  208,209,210,211,212,213,214,215,
  216,217,218,219,220,221,222,223,
  224,225,226,227,228,229,230,231,
  232,233,234,235,236,237,238,239,
  240,241,242,243,244,245,246,247,
  248,249,250,251,252,253,254,255,

/* This table contains bit maps for various character classes. Each map is 32
bytes long and the bits run from the least significant end of each byte. The
classes that have their own maps are: space, xdigit, digit, upper, lower, word,
graph, print, punct, and cntrl. Other classes are built from combinations. */

  0x00,0x3e,0x00,0x00,0x01,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,

  0x00,0x00,0x00,0x00,0x00,0x00,0xff,0x03,
  0x7e,0x00,0x00,0x00,0x7e,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,

  0x00,0x00,0x00,0x00,0x00,0x00,0xff,0x03,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,

  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0xfe,0xff,0xff,0x07,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,

  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0xfe,0xff,0xff,0x07,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,

  0x00,0x00,0x00,0x00,0x00,0x00,0xff,0x03,
  0xfe,0xff,0xff,0x87,0xfe,0xff,0xff,0x07,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,

  0x00,0x00,0x00,0x00,0xfe,0xff,0xff,0xff,
  0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x7f,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,

  0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,
  0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x7f,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,

  0x00,0x00,0x00,0x00,0xfe,0xff,0x00,0xfc,
  0x01,0x00,0x00,0xf8,0x01,0x00,0x00,0x78,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,

  0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,

/* This table identifies various classes of character by individual bits:
  0x01   white space character
  0x02   letter
  0x04   decimal digit
  0x08   hexadecimal digit
  0x10   alphanumeric or '_'
  0x80   regular expression metacharacter or binary zero
*/

  0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /*   0-  7 */
  0x00,0x01,0x01,0x00,0x01,0x01,0x00,0x00, /*   8- 15 */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /*  16- 23 */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /*  24- 31 */
  0x01,0x00,0x00,0x00,0x80,0x00,0x00,0x00, /*    - '  */
  0x80,0x80,0x80,0x80,0x00,0x00,0x80,0x00, /*  ( - /  */
  0x1c,0x1c,0x1c,0x1c,0x1c,0x1c,0x1c,0x1c, /*  0 - 7  */
  0x1c,0x1c,0x00,0x00,0x00,0x00,0x00,0x80, /*  8 - ?  */
  0x00,0x1a,0x1a,0x1a,0x1a,0x1a,0x1a,0x12, /*  @ - G  */
  0x12,0x12,0x12,0x12,0x12,0x12,0x12,0x12, /*  H - O  */
  0x12,0x12,0x12,0x12,0x12,0x12,0x12,0x12, /*  P - W  */
  0x12,0x12,0x12,0x80,0x80,0x00,0x80,0x10, /*  X - _  */
  0x00,0x1a,0x1a,0x1a,0x1a,0x1a,0x1a,0x12, /*  ` - g  */
  0x12,0x12,0x12,0x12,0x12,0x12,0x12,0x12, /*  h - o  */
  0x12,0x12,0x12,0x12,0x12,0x12,0x12,0x12, /*  p - w  */
  0x12,0x12,0x12,0x80,0x80,0x00,0x00,0x00, /*  x -127 */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /* 128-135 */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /* 136-143 */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /* 144-151 */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /* 152-159 */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /* 160-167 */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /* 168-175 */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /* 176-183 */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /* 184-191 */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /* 192-199 */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /* 200-207 */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /* 208-215 */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /* 216-223 */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /* 224-231 */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /* 232-239 */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /* 240-247 */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};/* 248-255 */


/* End pcre_chartables.c */
/* Begin pcre_get.c */
/*************************************************
*      Copy captured string to given buffer      *
*************************************************/

/* This function copies a single captured substring into a given buffer.
Note that we use memcpy() rather than strncpy() in case there are binary zeros
in the string.

Arguments:
  subject        the subject string that was matched
  ovector        pointer to the offsets table
  stringcount    the number of substrings that were captured
                   (i.e. the yield of the pcre_exec call, unless
                   that was zero, in which case it should be 1/3
                   of the offset table size)
  stringnumber   the number of the required substring
  buffer         where to put the substring
  size           the size of the buffer

Returns:         if successful:
                   the length of the copied string, not including the zero
                   that is put on the end; can be zero
                 if not successful:
                   PCRE_ERROR_NOMEMORY (-6) buffer too small
                   PCRE_ERROR_NOSUBSTRING (-7) no such captured substring
*/

int
pcre_copy_substring(const char *subject, int *ovector, int stringcount,
  int stringnumber, char *buffer, int size)
{
int yield;
if (stringnumber < 0 || stringnumber >= stringcount)
  return PCRE_ERROR_NOSUBSTRING;
stringnumber *= 2;
yield = ovector[stringnumber+1] - ovector[stringnumber];
if (size < yield + 1) return PCRE_ERROR_NOMEMORY;
memcpy(buffer, subject + ovector[stringnumber], yield);
buffer[yield] = 0;
return yield;
}

/* End pcre_get.c */
/* Begin pcre_maketables.c */
/*************************************************
*           Create PCRE character tables         *
*************************************************/

/* This function builds a set of character tables for use by PCRE and returns
a pointer to them. They are build using the ctype functions, and consequently
their contents will depend upon the current locale setting. When compiled as
part of the library, the store is obtained via pcre_malloc(), but when compiled
inside dftables, use malloc().

Arguments:   none
Returns:     pointer to the contiguous block of data
*/

const unsigned char *
pcre_maketables(void)
{
    unsigned char *yield, *p;
    int i;

    yield = static_cast<unsigned char*>(malloc(tables_length));

    if (yield == NULL) return NULL;
    p = yield;

    /* First comes the lower casing table */

    for (i = 0; i < 256; i++)
    {
        *p++ = mux_tolower_ascii(i);
    }

    /* Next the case-flipping table */

    for (i = 0; i < 256; i++)
    {
        *p++ = mux_islower_ascii(i)? mux_toupper_ascii(i) : mux_tolower_ascii(i);
    }

    /* Then the character class tables. Don't try to be clever and save effort on
    exclusive ones - in some locales things may be different. Note that the table
    for "space" includes everything "isspace" gives, including VT in the default
    locale. This makes it work for the POSIX class [:space:]. Note also that it is
    possible for a character to be alnum or alpha without being lower or upper,
    such as "male and female ordinals" (\xAA and \xBA) in the fr_FR locale (at
    least under Debian Linux's locales as of 12/2005). So we must test for alnum
    specially. */

    memset(p, 0, cbit_length);
    for (i = 0; i < 256; i++)
    {
        if (mux_isdigit(i)) p[cbit_digit  + i/8] |= 1 << (i&7);
        if (mux_isupper_ascii(i)) p[cbit_upper  + i/8] |= 1 << (i&7);
        if (mux_islower_ascii(i)) p[cbit_lower  + i/8] |= 1 << (i&7);
        if (mux_isalnum(i)) p[cbit_word   + i/8] |= 1 << (i&7);
        if (i == '_')       p[cbit_word   + i/8] |= 1 << (i&7);
        if (mux_isspace(i)) p[cbit_space  + i/8] |= 1 << (i&7);
        if (mux_isxdigit(i))p[cbit_xdigit + i/8] |= 1 << (i&7);
        if (isgraph(i))     p[cbit_graph  + i/8] |= 1 << (i&7);
        if (isprint(i))     p[cbit_print  + i/8] |= 1 << (i&7);
        if (ispunct(i))     p[cbit_punct  + i/8] |= 1 << (i&7);
        if (iscntrl(i))     p[cbit_cntrl  + i/8] |= 1 << (i&7);
    }
    p += cbit_length;

    /* Finally, the character type table. In this, we exclude VT from the white
    space chars, because Perl doesn't recognize it as such for \s and for comments
    within regexes. */

    for (i = 0; i < 256; i++)
    {
        unsigned char x = 0;
        if (i != 0x0b && mux_isspace(i)) x += ctype_space;
        if (mux_isalpha(i)) x += ctype_letter;
        if (mux_isdigit(i)) x += ctype_digit;
        if (mux_isxdigit(i)) x += ctype_xdigit;
        if (mux_isalnum(i) || i == '_') x += ctype_word;

        /* Note: strchr includes the terminating zero in the characters it considers.
        In this instance, that is ok because we want binary zero to be flagged as a
        meta-character, which in this sense is any character that terminates a run
        of data characters. */

        if (strchr("\\*+?{^.$|()[", i) != 0) x += ctype_meta;
        *p++ = x;
    }

    return yield;
}

/* End pcre_maketables.c */
/* Begin pcre_tables.c */
/*************************************************
*           Tables for UTF-8 support             *
*************************************************/

/* These are the breakpoints for different numbers of bytes in a UTF-8
character. */

#ifdef SUPPORT_UTF8

static const int _pcre_utf8_table1[] =
  { 0x7f, 0x7ff, 0xffff, 0x1fffff, 0x3ffffff, 0x7fffffff};

const int _pcre_utf8_table1_size = sizeof(_pcre_utf8_table1)/sizeof(int);

/* These are the indicator bits and the mask for the data bits to set in the
first byte of a character, indexed by the number of additional bytes. */

static const int _pcre_utf8_table2[] = { 0,    0xc0, 0xe0, 0xf0, 0xf8, 0xfc};
static const int _pcre_utf8_table3[] = { 0xff, 0x1f, 0x0f, 0x07, 0x03, 0x01};

/* Table of the number of extra bytes, indexed by the first byte masked with
0x3f. The highest number for a valid UTF-8 first byte is in fact 0x3d. */

static const uschar _pcre_utf8_table4[] = {
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  3,3,3,3,3,3,3,3,4,4,4,4,5,5,5,5 };

#endif  /* SUPPORT_UTF8 */

/* End pcre_tables.c */
/* Begin pcre_study.c */

/* Returns from set_start_bits() */

enum { SSB_FAIL, SSB_DONE, SSB_CONTINUE };


/*************************************************
*      Set a bit and maybe its alternate case    *
*************************************************/

/* Given a character, set its bit in the table, and also the bit for the other
version of a letter if we are caseless.

Arguments:
  start_bits    points to the bit map
  c             is the character
  caseless      the caseless flag
  cd            the block with char table pointers

Returns:        nothing
*/

static void
set_bit(uschar *start_bits, unsigned int c, bool caseless, compile_data *cd)
{
start_bits[c/8] |= (1 << (c&7));
if (caseless && (cd->ctypes[c] & ctype_letter) != 0)
  start_bits[cd->fcc[c]/8] |= (1 << (cd->fcc[c]&7));
}



/*************************************************
*          Create bitmap of starting bytes       *
*************************************************/

/* This function scans a compiled unanchored expression recursively and
attempts to build a bitmap of the set of possible starting bytes. As time goes
by, we may be able to get more clever at doing this. The SSB_CONTINUE return is
useful for parenthesized groups in patterns such as (a*)b where the group
provides some optional starting bytes but scanning must continue at the outer
level to find at least one mandatory byte. At the outermost level, this
function fails unless the result is SSB_DONE.

Arguments:
  code         points to an expression
  start_bits   points to a 32-byte table, initialized to 0
  caseless     the current state of the caseless flag
  utf8         true if in UTF-8 mode
  cd           the block with char table pointers

Returns:       SSB_FAIL     => Failed to find any starting bytes
               SSB_DONE     => Found mandatory starting bytes
               SSB_CONTINUE => Found optional starting bytes
*/

static int
set_start_bits(const uschar *code, uschar *start_bits, bool caseless,
  bool utf8, compile_data *cd)
{
register int c;
int yield = SSB_DONE;

#if 0
/* ========================================================================= */
/* The following comment and code was inserted in January 1999. In May 2006,
when it was observed to cause compiler warnings about unused values, I took it
out again. If anybody is still using OS/2, they will have to put it back
manually. */

/* This next statement and the later reference to dummy are here in order to
trick the optimizer of the IBM C compiler for OS/2 into generating correct
code. Apparently IBM isn't going to fix the problem, and we would rather not
disable optimization (in this module it actually makes a big difference, and
the pcre module can use all the optimization it can get). */

volatile int dummy;
/* ========================================================================= */
#endif

do
  {
  const uschar *tcode = code + (((int)*code == OP_CBRA)? 3:1) + LINK_SIZE;
  bool try_next = true;

  while (try_next)    /* Loop for items in this branch */
    {
    int rc;
    switch(*tcode)
      {
      /* Fail if we reach something we don't understand */

      default:
      return SSB_FAIL;

      /* If we hit a bracket or a positive lookahead assertion, recurse to set
      bits from within the subpattern. If it can't find anything, we have to
      give up. If it finds some mandatory character(s), we are done for this
      branch. Otherwise, carry on scanning after the subpattern. */

      case OP_BRA:
      case OP_SBRA:
      case OP_CBRA:
      case OP_SCBRA:
      case OP_ONCE:
      case OP_ASSERT:
      rc = set_start_bits(tcode, start_bits, caseless, utf8, cd);
      if (rc == SSB_FAIL) return SSB_FAIL;
      if (rc == SSB_DONE) try_next = false; else
        {
        do tcode += GET(tcode, 1); while (*tcode == OP_ALT);
        tcode += 1 + LINK_SIZE;
        }
      break;

      /* If we hit ALT or KET, it means we haven't found anything mandatory in
      this branch, though we might have found something optional. For ALT, we
      continue with the next alternative, but we have to arrange that the final
      result from subpattern is SSB_CONTINUE rather than SSB_DONE. For KET,
      return SSB_CONTINUE: if this is the top level, that indicates failure,
      but after a nested subpattern, it causes scanning to continue. */

      case OP_ALT:
      yield = SSB_CONTINUE;
      try_next = false;
      break;

      case OP_KET:
      case OP_KETRMAX:
      case OP_KETRMIN:
      return SSB_CONTINUE;

      /* Skip over callout */

      case OP_CALLOUT:
      tcode += 2 + 2*LINK_SIZE;
      break;

      /* Skip over lookbehind and negative lookahead assertions */

      case OP_ASSERT_NOT:
      case OP_ASSERTBACK:
      case OP_ASSERTBACK_NOT:
      do tcode += GET(tcode, 1); while (*tcode == OP_ALT);
      tcode += 1 + LINK_SIZE;
      break;

      /* Skip over an option setting, changing the caseless flag */

      case OP_OPT:
      caseless = (tcode[1] & PCRE_CASELESS) != 0;
      tcode += 2;
      break;

      /* BRAZERO does the bracket, but carries on. */

      case OP_BRAZERO:
      case OP_BRAMINZERO:
      if (set_start_bits(++tcode, start_bits, caseless, utf8, cd) == SSB_FAIL)
        return SSB_FAIL;
/* =========================================================================
      See the comment at the head of this function concerning the next line,
      which was an old fudge for the benefit of OS/2.
      dummy = 1;
  ========================================================================= */
      do tcode += GET(tcode,1); while (*tcode == OP_ALT);
      tcode += 1 + LINK_SIZE;
      break;

      /* Single-char * or ? sets the bit and tries the next item */

      case OP_STAR:
      case OP_MINSTAR:
      case OP_POSSTAR:
      case OP_QUERY:
      case OP_MINQUERY:
      case OP_POSQUERY:
      set_bit(start_bits, tcode[1], caseless, cd);
      tcode += 2;
#ifdef SUPPORT_UTF8
      if (utf8 && tcode[-1] >= 0xc0)
        tcode += _pcre_utf8_table4[tcode[-1] & 0x3f];
#endif
      break;

      /* Single-char upto sets the bit and tries the next */

      case OP_UPTO:
      case OP_MINUPTO:
      case OP_POSUPTO:
      set_bit(start_bits, tcode[3], caseless, cd);
      tcode += 4;
#ifdef SUPPORT_UTF8
      if (utf8 && tcode[-1] >= 0xc0)
        tcode += _pcre_utf8_table4[tcode[-1] & 0x3f];
#endif
      break;

      /* At least one single char sets the bit and stops */

      case OP_EXACT:       /* Fall through */
      tcode += 2;

      case OP_CHAR:
      case OP_CHARNC:
      case OP_PLUS:
      case OP_MINPLUS:
      case OP_POSPLUS:
      set_bit(start_bits, tcode[1], caseless, cd);
      try_next = false;
      break;

      /* Single character type sets the bits and stops */

      case OP_NOT_DIGIT:
      for (c = 0; c < 32; c++)
        start_bits[c] |= ~cd->cbits[c+cbit_digit];
      try_next = false;
      break;

      case OP_DIGIT:
      for (c = 0; c < 32; c++)
        start_bits[c] |= cd->cbits[c+cbit_digit];
      try_next = false;
      break;

      /* The cbit_space table has vertical tab as whitespace; we have to
      discard it. */

      case OP_NOT_WHITESPACE:
      for (c = 0; c < 32; c++)
        {
        int d = cd->cbits[c+cbit_space];
        if (c == 1) d &= ~0x08;
        start_bits[c] |= ~d;
        }
      try_next = false;
      break;

      /* The cbit_space table has vertical tab as whitespace; we have to
      discard it. */

      case OP_WHITESPACE:
      for (c = 0; c < 32; c++)
        {
        int d = cd->cbits[c+cbit_space];
        if (c == 1) d &= ~0x08;
        start_bits[c] |= d;
        }
      try_next = false;
      break;

      case OP_NOT_WORDCHAR:
      for (c = 0; c < 32; c++)
        start_bits[c] |= ~cd->cbits[c+cbit_word];
      try_next = false;
      break;

      case OP_WORDCHAR:
      for (c = 0; c < 32; c++)
        start_bits[c] |= cd->cbits[c+cbit_word];
      try_next = false;
      break;

      /* One or more character type fudges the pointer and restarts, knowing
      it will hit a single character type and stop there. */

      case OP_TYPEPLUS:
      case OP_TYPEMINPLUS:
      tcode++;
      break;

      case OP_TYPEEXACT:
      tcode += 3;
      break;

      /* Zero or more repeats of character types set the bits and then
      try again. */

      case OP_TYPEUPTO:
      case OP_TYPEMINUPTO:
      case OP_TYPEPOSUPTO:
      tcode += 2;               /* Fall through */

      case OP_TYPESTAR:
      case OP_TYPEMINSTAR:
      case OP_TYPEPOSSTAR:
      case OP_TYPEQUERY:
      case OP_TYPEMINQUERY:
      case OP_TYPEPOSQUERY:
      switch(tcode[1])
        {
        case OP_ANY:
        return SSB_FAIL;

        case OP_NOT_DIGIT:
        for (c = 0; c < 32; c++)
          start_bits[c] |= ~cd->cbits[c+cbit_digit];
        break;

        case OP_DIGIT:
        for (c = 0; c < 32; c++)
          start_bits[c] |= cd->cbits[c+cbit_digit];
        break;

        /* The cbit_space table has vertical tab as whitespace; we have to
        discard it. */

        case OP_NOT_WHITESPACE:
        for (c = 0; c < 32; c++)
          {
          int d = cd->cbits[c+cbit_space];
          if (c == 1) d &= ~0x08;
          start_bits[c] |= ~d;
          }
        break;

        /* The cbit_space table has vertical tab as whitespace; we have to
        discard it. */

        case OP_WHITESPACE:
        for (c = 0; c < 32; c++)
          {
          int d = cd->cbits[c+cbit_space];
          if (c == 1) d &= ~0x08;
          start_bits[c] |= d;
          }
        break;

        case OP_NOT_WORDCHAR:
        for (c = 0; c < 32; c++)
          start_bits[c] |= ~cd->cbits[c+cbit_word];
        break;

        case OP_WORDCHAR:
        for (c = 0; c < 32; c++)
          start_bits[c] |= cd->cbits[c+cbit_word];
        break;
        }

      tcode += 2;
      break;

      /* Character class where all the information is in a bit map: set the
      bits and either carry on or not, according to the repeat count. If it was
      a negative class, and we are operating with UTF-8 characters, any byte
      with a value >= 0xc4 is a potentially valid starter because it starts a
      character with a value > 255. */

      case OP_NCLASS:
#ifdef SUPPORT_UTF8
      if (utf8)
        {
        start_bits[24] |= 0xf0;              /* Bits for 0xc4 - 0xc8 */
        memset(start_bits+25, 0xff, 7);      /* Bits for 0xc9 - 0xff */
        }
#endif
      /* Fall through */

      case OP_CLASS:
        {
        tcode++;

        /* In UTF-8 mode, the bits in a bit map correspond to character
        values, not to byte values. However, the bit map we are constructing is
        for byte values. So we have to do a conversion for characters whose
        value is > 127. In fact, there are only two possible starting bytes for
        characters in the range 128 - 255. */

#ifdef SUPPORT_UTF8
        if (utf8)
          {
          for (c = 0; c < 16; c++) start_bits[c] |= tcode[c];
          for (c = 128; c < 256; c++)
            {
            if ((tcode[c/8] && (1 << (c&7))) != 0)
              {
              int d = (c >> 6) | 0xc0;            /* Set bit for this starter */
              start_bits[d/8] |= (1 << (d&7));    /* and then skip on to the */
              c = (c & 0xc0) + 0x40 - 1;          /* next relevant character. */
              }
            }
          }

        /* In non-UTF-8 mode, the two bit maps are completely compatible. */

        else
#endif
          {
          for (c = 0; c < 32; c++) start_bits[c] |= tcode[c];
          }

        /* Advance past the bit map, and act on what follows */

        tcode += 32;
        switch (*tcode)
          {
          case OP_CRSTAR:
          case OP_CRMINSTAR:
          case OP_CRQUERY:
          case OP_CRMINQUERY:
          tcode++;
          break;

          case OP_CRRANGE:
          case OP_CRMINRANGE:
          if (((tcode[1] << 8) + tcode[2]) == 0) tcode += 5;
            else try_next = false;
          break;

          default:
          try_next = false;
          break;
          }
        }
      break; /* End of bitmap class handling */

      }      /* End of switch */
    }        /* End of try_next loop */

  code += GET(code, 1);   /* Advance to next branch */
  }
while (*code == OP_ALT);
return yield;
}



/*************************************************
*          Study a compiled expression           *
*************************************************/

/* This function is handed a compiled expression that it must study to produce
information that will speed up the matching. It returns a pcre_extra block
which then gets handed back to pcre_exec().

Arguments:
  re        points to the compiled expression
  options   contains option bits
  errorptr  points to where to place error messages;
            set NULL unless error

Returns:    pointer to a pcre_extra block, with study_data filled in and the
              appropriate flag set;
            NULL on error or if no optimization possible
*/

pcre_extra *
pcre_study(const pcre *external_re, int options, const char **errorptr)
{
uschar start_bits[32];
pcre_extra *extra;
pcre_study_data *study;
const uschar *tables;
uschar *code;
compile_data compile_block;
const real_pcre *re = (const real_pcre *)external_re;

*errorptr = NULL;

if (re == NULL || re->magic_number != MAGIC_NUMBER)
  {
  *errorptr = "argument is not a compiled regular expression";
  return NULL;
  }

if ((options & ~PUBLIC_STUDY_OPTIONS) != 0)
  {
  *errorptr = "unknown or incorrect option bit(s) set";
  return NULL;
  }

code = (uschar *)re + re->name_table_offset +
  (re->name_count * re->name_entry_size);

/* For an anchored pattern, or an unanchored pattern that has a first char, or
a multiline pattern that matches only at "line starts", no further processing
at present. */

if ((re->options & (PCRE_ANCHORED|PCRE_FIRSTSET|PCRE_STARTLINE)) != 0)
  return NULL;

/* Set the character tables in the block that is passed around */

tables = re->tables;
if (tables == NULL)
  (void)pcre_fullinfo(external_re, NULL, PCRE_INFO_DEFAULT_TABLES, &tables);

compile_block.lcc = tables + lcc_offset;
compile_block.fcc = tables + fcc_offset;
compile_block.cbits = tables + cbits_offset;
compile_block.ctypes = tables + ctypes_offset;

/* See if we can find a fixed set of initial characters for the pattern. */

memset(start_bits, 0, 32 * sizeof(uschar));
if (set_start_bits(code, start_bits, (re->options & PCRE_CASELESS) != 0,
  (re->options & PCRE_UTF8) != 0, &compile_block) != SSB_DONE) return NULL;

/* Get a pcre_extra block and a pcre_study_data block. The study data is put in
the latter, which is pointed to by the former, which may also get additional
data set later by the calling program. At the moment, the size of
pcre_study_data is fixed. We nevertheless save it in a field for returning via
the pcre_fullinfo() function so that if it becomes variable in the future, we
don't have to change that code. */

extra = static_cast<pcre_extra *>(malloc(sizeof(pcre_extra) + sizeof(pcre_study_data)));

if (extra == NULL)
  {
  *errorptr = "failed to get memory";
  return NULL;
  }

study = reinterpret_cast<pcre_study_data *>(reinterpret_cast<char*>(extra) + sizeof(pcre_extra));
extra->flags = PCRE_EXTRA_STUDY_DATA;
extra->study_data = study;

study->size = sizeof(pcre_study_data);
study->options = PCRE_STUDY_MAPPED;
memcpy(study->start_bits, start_bits, sizeof(start_bits));

return extra;
}

/* End pcre_study.c */
/* Begin pcre_internal.h */
#define DPRINTF(p) /*nothing*/
/* End pcre_internal.h */

#ifdef SUPPORT_UCP
/* Begin pcre_ucp_searchfuncs.c */
#include "ucp.h"               /* Exported interface */
/* End pcre_ucp_searchfuncs.c */

/* Begin ucpinternal.h */
/*************************************************
*           Unicode Property Table handler       *
*************************************************/

/* Internal header file defining the layout of the bits in each pair of 32-bit
words that form a data item in the table. */

typedef struct cnode {
  pcre_uint32 f0;
  pcre_uint32 f1;
} cnode;

/* Things for the f0 field */

#define f0_scriptmask   0xff000000  /* Mask for script field */
#define f0_scriptshift          24  /* Shift for script value */
#define f0_rangeflag    0x00f00000  /* Flag for a range item */
#define f0_charmask     0x001fffff  /* Mask for code point value */

/* Things for the f1 field */

#define f1_typemask     0xfc000000  /* Mask for char type field */
#define f1_typeshift            26  /* Shift for the type field */
#define f1_rangemask    0x0000ffff  /* Mask for a range offset */
#define f1_casemask     0x0000ffff  /* Mask for a case offset */
#define f1_caseneg      0xffff8000  /* Bits for negation */

/* The data consists of a vector of structures of type cnode. The two unsigned
32-bit integers are used as follows:

(f0) (1) The most significant byte holds the script number. The numbers are
         defined by the enum in ucp.h.

     (2) The 0x00800000 bit is set if this entry defines a range of characters.
         It is not set if this entry defines a single character

     (3) The 0x00600000 bits are spare.

     (4) The 0x001fffff bits contain the code point. No Unicode code point will
         ever be greater than 0x0010ffff, so this should be OK for ever.

(f1) (1) The 0xfc000000 bits contain the character type number. The numbers are
         defined by an enum in ucp.h.

     (2) The 0x03ff0000 bits are spare.

     (3) The 0x0000ffff bits contain EITHER the unsigned offset to the top of
         range if this entry defines a range, OR the *signed* offset to the
         character's "other case" partner if this entry defines a single
         character. There is no partner if the value is zero.

-------------------------------------------------------------------------------
| script (8) |.|.|.| codepoint (21) || type (6) |.|.| spare (8) | offset (16) |
-------------------------------------------------------------------------------
              | | |                              | |
              | | |-> spare                      | |-> spare
              | |                                |
              | |-> spare                        |-> spare
              |
              |-> range flag

The upper/lower casing information is set only for characters that come in
pairs. The non-one-to-one mappings in the Unicode data are ignored.

When searching the data, proceed as follows:

(1) Set up for a binary chop search.

(2) If the top is not greater than the bottom, the character is not in the
    table. Its type must therefore be "Cn" ("Undefined").

(3) Find the middle vector element.

(4) Extract the code point and compare. If equal, we are done.

(5) If the test character is smaller, set the top to the current point, and
    goto (2).

(6) If the current entry defines a range, compute the last character by adding
    the offset, and see if the test character is within the range. If it is,
    we are done.

(7) Otherwise, set the bottom to one element past the current point and goto
    (2).
*/


/* End of ucpinternal.h */
/* Begin pcre_ucp_searchfuncs.c */
#include "ucptable.cpp"          /* The table itself */


/* Table to translate from particular type value to the general value. */

static int ucp_gentype[] = {
  ucp_C, ucp_C, ucp_C, ucp_C, ucp_C,  /* Cc, Cf, Cn, Co, Cs */
  ucp_L, ucp_L, ucp_L, ucp_L, ucp_L,  /* Ll, Lu, Lm, Lo, Lt */
  ucp_M, ucp_M, ucp_M,                /* Mc, Me, Mn */
  ucp_N, ucp_N, ucp_N,                /* Nd, Nl, No */
  ucp_P, ucp_P, ucp_P, ucp_P, ucp_P,  /* Pc, Pd, Pe, Pf, Pi */
  ucp_P, ucp_P,                       /* Ps, Po */
  ucp_S, ucp_S, ucp_S, ucp_S,         /* Sc, Sk, Sm, So */
  ucp_Z, ucp_Z, ucp_Z                 /* Zl, Zp, Zs */
};



/*************************************************
*         Search table and return type           *
*************************************************/

/* Three values are returned: the category is ucp_C, ucp_L, etc. The detailed
character type is ucp_Lu, ucp_Nd, etc. The script is ucp_Latin, etc.

Arguments:
  c           the character value
  type_ptr    the detailed character type is returned here
  script_ptr  the script is returned here

Returns:      the character type category
*/

static int
_pcre_ucp_findprop(const unsigned int c, int *type_ptr, int *script_ptr)
{
int bot = 0;
int top = sizeof(ucp_table)/sizeof(cnode);
int mid;

/* The table is searched using a binary chop. You might think that using
intermediate variables to hold some of the common expressions would speed
things up, but tests with gcc 3.4.4 on Linux showed that, on the contrary, it
makes things a lot slower. */

for (;;)
  {
  if (top <= bot)
    {
    *type_ptr = ucp_Cn;
    *script_ptr = ucp_Common;
    return ucp_C;
    }
  mid = (bot + top) >> 1;
  if (c == (ucp_table[mid].f0 & f0_charmask)) break;
  if (c < (ucp_table[mid].f0 & f0_charmask)) top = mid;
  else
    {
    if ((ucp_table[mid].f0 & f0_rangeflag) != 0 &&
        c <= (ucp_table[mid].f0 & f0_charmask) +
             (ucp_table[mid].f1 & f1_rangemask)) break;
    bot = mid + 1;
    }
  }

/* Found an entry in the table. Set the script and detailed type values, and
return the general type. */

*script_ptr = (ucp_table[mid].f0 & f0_scriptmask) >> f0_scriptshift;
*type_ptr = (ucp_table[mid].f1 & f1_typemask) >> f1_typeshift;

return ucp_gentype[*type_ptr];
}



/*************************************************
*       Search table and return other case       *
*************************************************/

/* If the given character is a letter, and there is another case for the
letter, return the other case. Otherwise, return -1.

Arguments:
  c           the character value

Returns:      the other case or NOTACHAR if none
*/

static unsigned int
_pcre_ucp_othercase(const unsigned int c)
{
int bot = 0;
int top = sizeof(ucp_table)/sizeof(cnode);
int mid, offset;

/* The table is searched using a binary chop. You might think that using
intermediate variables to hold some of the common expressions would speed
things up, but tests with gcc 3.4.4 on Linux showed that, on the contrary, it
makes things a lot slower. */

for (;;)
  {
  if (top <= bot) return -1;
  mid = (bot + top) >> 1;
  if (c == (ucp_table[mid].f0 & f0_charmask)) break;
  if (c < (ucp_table[mid].f0 & f0_charmask)) top = mid;
  else
    {
    if ((ucp_table[mid].f0 & f0_rangeflag) != 0 &&
        c <= (ucp_table[mid].f0 & f0_charmask) +
             (ucp_table[mid].f1 & f1_rangemask)) break;
    bot = mid + 1;
    }
  }

/* Found an entry in the table. Return NOTACHAR for a range entry. Otherwise
return the other case if there is one, else NOTACHAR. */

if ((ucp_table[mid].f0 & f0_rangeflag) != 0) return NOTACHAR;

offset = ucp_table[mid].f1 & f1_casemask;
if ((offset & f1_caseneg) != 0) offset |= f1_caseneg;
return (offset == 0)? NOTACHAR : c + offset;
}


/* End pcre_ucp_searchfuncs.c */
/* Begin pcre_tables.c */
/* This table translates Unicode property names into type and code values. It
is searched by binary chop, so must be in collating sequence of name. */

static const ucp_type_table _pcre_utt[] = {
  { "Any",                 PT_ANY,  0 },
  { "Arabic",              PT_SC,   ucp_Arabic },
  { "Armenian",            PT_SC,   ucp_Armenian },
  { "Balinese",            PT_SC,   ucp_Balinese },
  { "Bengali",             PT_SC,   ucp_Bengali },
  { "Bopomofo",            PT_SC,   ucp_Bopomofo },
  { "Braille",             PT_SC,   ucp_Braille },
  { "Buginese",            PT_SC,   ucp_Buginese },
  { "Buhid",               PT_SC,   ucp_Buhid },
  { "C",                   PT_GC,   ucp_C },
  { "Canadian_Aboriginal", PT_SC,   ucp_Canadian_Aboriginal },
  { "Cc",                  PT_PC,   ucp_Cc },
  { "Cf",                  PT_PC,   ucp_Cf },
  { "Cherokee",            PT_SC,   ucp_Cherokee },
  { "Cn",                  PT_PC,   ucp_Cn },
  { "Co",                  PT_PC,   ucp_Co },
  { "Common",              PT_SC,   ucp_Common },
  { "Coptic",              PT_SC,   ucp_Coptic },
  { "Cs",                  PT_PC,   ucp_Cs },
  { "Cuneiform",           PT_SC,   ucp_Cuneiform },
  { "Cypriot",             PT_SC,   ucp_Cypriot },
  { "Cyrillic",            PT_SC,   ucp_Cyrillic },
  { "Deseret",             PT_SC,   ucp_Deseret },
  { "Devanagari",          PT_SC,   ucp_Devanagari },
  { "Ethiopic",            PT_SC,   ucp_Ethiopic },
  { "Georgian",            PT_SC,   ucp_Georgian },
  { "Glagolitic",          PT_SC,   ucp_Glagolitic },
  { "Gothic",              PT_SC,   ucp_Gothic },
  { "Greek",               PT_SC,   ucp_Greek },
  { "Gujarati",            PT_SC,   ucp_Gujarati },
  { "Gurmukhi",            PT_SC,   ucp_Gurmukhi },
  { "Han",                 PT_SC,   ucp_Han },
  { "Hangul",              PT_SC,   ucp_Hangul },
  { "Hanunoo",             PT_SC,   ucp_Hanunoo },
  { "Hebrew",              PT_SC,   ucp_Hebrew },
  { "Hiragana",            PT_SC,   ucp_Hiragana },
  { "Inherited",           PT_SC,   ucp_Inherited },
  { "Kannada",             PT_SC,   ucp_Kannada },
  { "Katakana",            PT_SC,   ucp_Katakana },
  { "Kharoshthi",          PT_SC,   ucp_Kharoshthi },
  { "Khmer",               PT_SC,   ucp_Khmer },
  { "L",                   PT_GC,   ucp_L },
  { "L&",                  PT_LAMP, 0 },
  { "Lao",                 PT_SC,   ucp_Lao },
  { "Latin",               PT_SC,   ucp_Latin },
  { "Limbu",               PT_SC,   ucp_Limbu },
  { "Linear_B",            PT_SC,   ucp_Linear_B },
  { "Ll",                  PT_PC,   ucp_Ll },
  { "Lm",                  PT_PC,   ucp_Lm },
  { "Lo",                  PT_PC,   ucp_Lo },
  { "Lt",                  PT_PC,   ucp_Lt },
  { "Lu",                  PT_PC,   ucp_Lu },
  { "M",                   PT_GC,   ucp_M },
  { "Malayalam",           PT_SC,   ucp_Malayalam },
  { "Mc",                  PT_PC,   ucp_Mc },
  { "Me",                  PT_PC,   ucp_Me },
  { "Mn",                  PT_PC,   ucp_Mn },
  { "Mongolian",           PT_SC,   ucp_Mongolian },
  { "Myanmar",             PT_SC,   ucp_Myanmar },
  { "N",                   PT_GC,   ucp_N },
  { "Nd",                  PT_PC,   ucp_Nd },
  { "New_Tai_Lue",         PT_SC,   ucp_New_Tai_Lue },
  { "Nko",                 PT_SC,   ucp_Nko },
  { "Nl",                  PT_PC,   ucp_Nl },
  { "No",                  PT_PC,   ucp_No },
  { "Ogham",               PT_SC,   ucp_Ogham },
  { "Old_Italic",          PT_SC,   ucp_Old_Italic },
  { "Old_Persian",         PT_SC,   ucp_Old_Persian },
  { "Oriya",               PT_SC,   ucp_Oriya },
  { "Osmanya",             PT_SC,   ucp_Osmanya },
  { "P",                   PT_GC,   ucp_P },
  { "Pc",                  PT_PC,   ucp_Pc },
  { "Pd",                  PT_PC,   ucp_Pd },
  { "Pe",                  PT_PC,   ucp_Pe },
  { "Pf",                  PT_PC,   ucp_Pf },
  { "Phags_Pa",            PT_SC,   ucp_Phags_Pa },
  { "Phoenician",          PT_SC,   ucp_Phoenician },
  { "Pi",                  PT_PC,   ucp_Pi },
  { "Po",                  PT_PC,   ucp_Po },
  { "Ps",                  PT_PC,   ucp_Ps },
  { "Runic",               PT_SC,   ucp_Runic },
  { "S",                   PT_GC,   ucp_S },
  { "Sc",                  PT_PC,   ucp_Sc },
  { "Shavian",             PT_SC,   ucp_Shavian },
  { "Sinhala",             PT_SC,   ucp_Sinhala },
  { "Sk",                  PT_PC,   ucp_Sk },
  { "Sm",                  PT_PC,   ucp_Sm },
  { "So",                  PT_PC,   ucp_So },
  { "Syloti_Nagri",        PT_SC,   ucp_Syloti_Nagri },
  { "Syriac",              PT_SC,   ucp_Syriac },
  { "Tagalog",             PT_SC,   ucp_Tagalog },
  { "Tagbanwa",            PT_SC,   ucp_Tagbanwa },
  { "Tai_Le",              PT_SC,   ucp_Tai_Le },
  { "Tamil",               PT_SC,   ucp_Tamil },
  { "Telugu",              PT_SC,   ucp_Telugu },
  { "Thaana",              PT_SC,   ucp_Thaana },
  { "Thai",                PT_SC,   ucp_Thai },
  { "Tibetan",             PT_SC,   ucp_Tibetan },
  { "Tifinagh",            PT_SC,   ucp_Tifinagh },
  { "Ugaritic",            PT_SC,   ucp_Ugaritic },
  { "Yi",                  PT_SC,   ucp_Yi },
  { "Z",                   PT_GC,   ucp_Z },
  { "Zl",                  PT_PC,   ucp_Zl },
  { "Zp",                  PT_PC,   ucp_Zp },
  { "Zs",                  PT_PC,   ucp_Zs }
};

const int _pcre_utt_size = sizeof(_pcre_utt)/sizeof(ucp_type_table);

/* End pcre_tables.c */
#endif

/* Begin pcre_exec.c */
/* Maximum number of ints of offset to save on the stack for recursive calls.
If the offset vector is bigger, malloc is used. This should be a multiple of 3,
because the offset vector is always a multiple of 3 long. */

#define REC_STACK_SAVE_MAX 30
/* End pcre_exec.c */
/* Begin pcre_internal.h */
/* The maximum remaining length of subject we are prepared to search for a
req_byte match. */

#define REQ_BYTE_MAX 1000
/* End pcre_internal.h */
/* Begin pcre_tables.c */
/* Table of sizes for the fixed-length opcodes. It's defined in a macro so that
the definition is next to the definition of the opcodes in pcre_internal.h. */

static const uschar _pcre_OP_lengths[] = { OP_LENGTHS };
/* End pcre_tables.c */
/* Begin pcre_exec.c */
/* Min and max values for the common repeats; for the maxima, 0 => infinity */

static const char rep_min[] = { 0, 0, 1, 1, 0, 0 };
static const char rep_max[] = { 0, 0, 0, 0, 1, 1 };
/* End pcre_exec.c */
/* Begin pcre_compile.c */
/*************************************************
*      Code parameters and static tables         *
*************************************************/

/* This value specifies the size of stack workspace that is used during the
first pre-compile phase that determines how much memory is required. The regex
is partly compiled into this space, but the compiled parts are discarded as
soon as they can be, so that hopefully there will never be an overrun. The code
does, however, check for an overrun. The largest amount I've seen used is 218,
so this number is very generous.

The same workspace is used during the second, actual compile phase for
remembering forward references to groups so that they can be filled in at the
end. Each entry in this list occupies LINK_SIZE bytes, so even when LINK_SIZE
is 4 there is plenty of room. */

#define COMPILE_WORK_SIZE (4096)


/* Table for handling escaped characters in the range '0'-'z'. Positive returns
are simple data values; negative values are for special things like \d and so
on. Zero means further processing is needed (for things like \x), or the escape
is invalid. */

static const short int escapes[] = {
     0,      0,      0,      0,      0,      0,      0,      0,   /* 0 - 7 */
     0,      0,    ':',    ';',    '<',    '=',    '>',    '?',   /* 8 - ? */
   '@', -ESC_A, -ESC_B, -ESC_C, -ESC_D, -ESC_E,      0, -ESC_G,   /* @ - G */
     0,      0,      0,      0,      0,      0,      0,      0,   /* H - O */
-ESC_P, -ESC_Q, -ESC_R, -ESC_S,      0,      0,      0, -ESC_W,   /* P - W */
-ESC_X,      0, -ESC_Z,    '[',   '\\',    ']',    '^',    '_',   /* X - _ */
   '`',      7, -ESC_b,      0, -ESC_d,  ESC_e,  ESC_f,      0,   /* ` - g */
     0,      0,      0, -ESC_k,      0,      0,  ESC_n,      0,   /* h - o */
-ESC_p,      0,  ESC_r, -ESC_s,  ESC_tee,    0,      0, -ESC_w,   /* p - w */
     0,      0, -ESC_z                                            /* x - z */
};


/* Tables of names of POSIX character classes and their lengths. The list is
terminated by a zero length entry. The first three must be alpha, lower, upper,
as this is assumed for handling case independence. */

static const char *const posix_names[] = {
  "alpha", "lower", "upper",
  "alnum", "ascii", "blank", "cntrl", "digit", "graph",
  "print", "punct", "space", "word",  "xdigit" };

static const uschar posix_name_lengths[] = {
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 4, 6, 0 };

/* Table of class bit maps for each POSIX class. Each class is formed from a
base map, with an optional addition or removal of another map. Then, for some
classes, there is some additional tweaking: for [:blank:] the vertical space
characters are removed, and for [:alpha:] and [:alnum:] the underscore
character is removed. The triples in the table consist of the base map offset,
second map offset or -1 if no second map, and a non-negative value for map
addition or a negative value for map subtraction (if there are two maps). The
absolute value of the third field has these meanings: 0 => no tweaking, 1 =>
remove vertical space characters, 2 => remove underscore. */

static const int posix_class_maps[] = {
  cbit_word,  cbit_digit, -2,             /* alpha */
  cbit_lower, -1,          0,             /* lower */
  cbit_upper, -1,          0,             /* upper */
  cbit_word,  -1,          2,             /* alnum - word without underscore */
  cbit_print, cbit_cntrl,  0,             /* ascii */
  cbit_space, -1,          1,             /* blank - a GNU extension */
  cbit_cntrl, -1,          0,             /* cntrl */
  cbit_digit, -1,          0,             /* digit */
  cbit_graph, -1,          0,             /* graph */
  cbit_print, -1,          0,             /* print */
  cbit_punct, -1,          0,             /* punct */
  cbit_space, -1,          0,             /* space */
  cbit_word,  -1,          0,             /* word - a Perl extension */
  cbit_xdigit,-1,          0              /* xdigit */
};


#define STRING(a)  # a
#define XSTRING(s) STRING(s)

/* The texts of compile-time error messages. These are "char *" because they
are passed to the outside world. Do not ever re-use any error number, because
they are documented. Always add a new error instead. Messages marked DEAD below
are no longer used. */

static const char *error_texts[] = {
  "no error",
  "\\ at end of pattern",
  "\\c at end of pattern",
  "unrecognized character follows \\",
  "numbers out of order in {} quantifier",
  /* 5 */
  "number too big in {} quantifier",
  "missing terminating ] for character class",
  "invalid escape sequence in character class",
  "range out of order in character class",
  "nothing to repeat",
  /* 10 */
  "operand of unlimited repeat could match the empty string",  /** DEAD **/
  "internal error: unexpected repeat",
  "unrecognized character after (?",
  "POSIX named classes are supported only within a class",
  "missing )",
  /* 15 */
  "reference to non-existent subpattern",
  "erroffset passed as NULL",
  "unknown option bit(s) set",
  "missing ) after comment",
  "parentheses nested too deeply",  /** DEAD **/
  /* 20 */
  "regular expression too large",
  "failed to get memory",
  "unmatched parentheses",
  "internal error: code overflow",
  "unrecognized character after (?<",
  /* 25 */
  "lookbehind assertion is not fixed length",
  "malformed number or name after (?(",
  "conditional group contains more than two branches",
  "assertion expected after (?(",
  "(?R or (?digits must be followed by )",
  /* 30 */
  "unknown POSIX class name",
  "POSIX collating elements are not supported",
  "this version of PCRE is not compiled with PCRE_UTF8 support",
  "spare error",  /** DEAD **/
  "character value in \\x{...} sequence is too large",
  /* 35 */
  "invalid condition (?(0)",
  "\\C not allowed in lookbehind assertion",
  "PCRE does not support \\L, \\l, \\N, \\U, or \\u",
  "number after (?C is > 255",
  "closing ) for (?C expected",
  /* 40 */
  "recursive call could loop indefinitely",
  "unrecognized character after (?P",
  "syntax error in subpattern name (missing terminator)",
  "two named subpatterns have the same name",
  "invalid UTF-8 string",
  /* 45 */
  "support for \\P, \\p, and \\X has not been compiled",
  "malformed \\P or \\p sequence",
  "unknown property name after \\P or \\p",
  "subpattern name is too long (maximum " XSTRING(MAX_NAME_SIZE) " characters)",
  "too many named subpatterns (maximum " XSTRING(MAX_NAME_COUNT) ")",
  /* 50 */
  "repeated subpattern is too long",
  "octal value is greater than \\377 (not in UTF-8 mode)",
  "internal error: overran compiling workspace",
  "internal error: previously-checked referenced subpattern not found",
  "DEFINE group contains more than one branch",
  /* 55 */
  "repeating a DEFINE group is not allowed",
  "inconsistent NEWLINE options",
  "\\g is not followed by an (optionally braced) non-zero number"
};


/* Table to identify digits and hex digits. This is used when compiling
patterns. Note that the tables in chartables are dependent on the locale, and
may mark arbitrary characters as digits - but the PCRE compiling code expects
to handle only 0-9, a-z, and A-Z as digits when compiling. That is why we have
a private table here. It costs 256 bytes, but it is a lot faster than doing
character value tests (at least in some simple cases I timed), and in some
applications one wants PCRE to compile efficiently as well as match
efficiently.

For convenience, we use the same bit definitions as in chartables:

  0x04   decimal digit
  0x08   hexadecimal digit

Then we can use ctype_digit and ctype_xdigit in the code. */

static const unsigned char digitab[] =
  {
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /*   0-  7 */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /*   8- 15 */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /*  16- 23 */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /*  24- 31 */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /*    - '  */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /*  ( - /  */
  0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c, /*  0 - 7  */
  0x0c,0x0c,0x00,0x00,0x00,0x00,0x00,0x00, /*  8 - ?  */
  0x00,0x08,0x08,0x08,0x08,0x08,0x08,0x00, /*  @ - G  */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /*  H - O  */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /*  P - W  */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /*  X - _  */
  0x00,0x08,0x08,0x08,0x08,0x08,0x08,0x00, /*  ` - g  */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /*  h - o  */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /*  p - w  */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /*  x -127 */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /* 128-135 */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /* 136-143 */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /* 144-151 */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /* 152-159 */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /* 160-167 */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /* 168-175 */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /* 176-183 */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /* 184-191 */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /* 192-199 */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /* 200-207 */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /* 208-215 */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /* 216-223 */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /* 224-231 */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /* 232-239 */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /* 240-247 */
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};/* 248-255 */


/* Definition to allow mutual recursion */

static bool
  compile_regex(int, int, uschar **, const uschar **, int *, bool, int, int *,
    int *, branch_chain *, compile_data *, int *);
/* End pcre_compile.c */
/* Begin pcre_exec.c */
/* The chain of eptrblocks for tail recursions uses memory in stack workspace,
obtained at top level, the size of which is defined by EPTR_WORK_SIZE. */

#define EPTR_WORK_SIZE (1000)

/* Flag bits for the match() function */

#define match_condassert     0x01  /* Called to check a condition assertion */
#define match_cbegroup       0x02  /* Could-be-empty unlimited repeat group */
#define match_tail_recursed  0x04  /* Tail recursive call */

/* Non-error returns from the match() function. Error returns are externally
defined PCRE_ERROR_xxx codes, which are all negative. */

#define MATCH_MATCH        1
#define MATCH_NOMATCH      0
/* End pcre_exec.c */
/* Begin pcregrep.c */
/*************************************************
*               Global variables                 *
*************************************************/
/* End pcregrep.c */
/* Begin pcre_globals.c */
/* This module contains global variables that are exported by the PCRE library.
PCRE is thread-clean and doesn't use any global variables in the normal sense.
However, it calls memory allocation and freeing functions via the four
indirections below, and it can optionally do callouts, using the fifth
indirection. These values can be changed by the caller, but are shared between
all threads. However, when compiling for Virtual Pascal, things are done
differently, and global variables are not used (see pcre.in). */

static int   (*pcre_callout)(pcre_callout_block *) = NULL;
/* End pcre_globals.c */
/* Begin pcre_internal.h */
/* When UTF-8 encoding is being used, a character is no longer just a single
byte. The macros for character handling generate simple sequences when used in
byte-mode, and more complicated ones for UTF-8 characters. */

#ifndef SUPPORT_UTF8
#define GETCHAR(c, eptr) c = *eptr;
#define GETCHARTEST(c, eptr) c = *eptr;
#define GETCHARINC(c, eptr) c = *eptr++;
#define GETCHARINCTEST(c, eptr) c = *eptr++;
#define GETCHARLEN(c, eptr, len) c = *eptr;
#define BACKCHAR(eptr)

#else   /* SUPPORT_UTF8 */

/* Get the next UTF-8 character, not advancing the pointer. This is called when
we know we are in UTF-8 mode. */

#define GETCHAR(c, eptr) \
  c = *eptr; \
  if (c >= 0xc0) \
    { \
    int gcii; \
    int gcaa = _pcre_utf8_table4[c & 0x3f];  /* Number of additional bytes */ \
    int gcss = 6*gcaa; \
    c = (c & _pcre_utf8_table3[gcaa]) << gcss; \
    for (gcii = 1; gcii <= gcaa; gcii++) \
      { \
      gcss -= 6; \
      c |= (eptr[gcii] & 0x3f) << gcss; \
      } \
    }

/* Get the next UTF-8 character, testing for UTF-8 mode, and not advancing the
pointer. */

#define GETCHARTEST(c, eptr) \
  c = *eptr; \
  if (utf8 && c >= 0xc0) \
    { \
    int gcii; \
    int gcaa = _pcre_utf8_table4[c & 0x3f];  /* Number of additional bytes */ \
    int gcss = 6*gcaa; \
    c = (c & _pcre_utf8_table3[gcaa]) << gcss; \
    for (gcii = 1; gcii <= gcaa; gcii++) \
      { \
      gcss -= 6; \
      c |= (eptr[gcii] & 0x3f) << gcss; \
      } \
    }

/* Get the next UTF-8 character, advancing the pointer. This is called when we
know we are in UTF-8 mode. */

#define GETCHARINC(c, eptr) \
  c = *eptr++; \
  if (c >= 0xc0) \
    { \
    int gcaa = _pcre_utf8_table4[c & 0x3f];  /* Number of additional bytes */ \
    int gcss = 6*gcaa; \
    c = (c & _pcre_utf8_table3[gcaa]) << gcss; \
    while (gcaa-- > 0) \
      { \
      gcss -= 6; \
      c |= (*eptr++ & 0x3f) << gcss; \
      } \
    }

/* Get the next character, testing for UTF-8 mode, and advancing the pointer */

#define GETCHARINCTEST(c, eptr) \
  c = *eptr++; \
  if (utf8 && c >= 0xc0) \
    { \
    int gcaa = _pcre_utf8_table4[c & 0x3f];  /* Number of additional bytes */ \
    int gcss = 6*gcaa; \
    c = (c & _pcre_utf8_table3[gcaa]) << gcss; \
    while (gcaa-- > 0) \
      { \
      gcss -= 6; \
      c |= (*eptr++ & 0x3f) << gcss; \
      } \
    }

/* Get the next UTF-8 character, not advancing the pointer, incrementing length
if there are extra bytes. This is called when we know we are in UTF-8 mode. */

#define GETCHARLEN(c, eptr, len) \
  c = *eptr; \
  if (c >= 0xc0) \
    { \
    int gcii; \
    int gcaa = _pcre_utf8_table4[c & 0x3f];  /* Number of additional bytes */ \
    int gcss = 6*gcaa; \
    c = (c & _pcre_utf8_table3[gcaa]) << gcss; \
    for (gcii = 1; gcii <= gcaa; gcii++) \
      { \
      gcss -= 6; \
      c |= (eptr[gcii] & 0x3f) << gcss; \
      } \
    len += gcaa; \
    }

/* If the pointer is not at the start of a character, move it back until
it is. Called only in UTF-8 mode. */

#define BACKCHAR(eptr) while((*eptr & 0xc0) == 0x80) eptr--;
#endif
/* End pcre_internal.h */
/* Begin pcre_ord2utf8.c */
/*************************************************
*       Convert character value to UTF-8         *
*************************************************/

/* This function takes an integer value in the range 0 - 0x7fffffff
and encodes it as a UTF-8 character in 0 to 6 bytes.

Arguments:
  cvalue     the character value
  buffer     pointer to buffer for result - at least 6 bytes long

Returns:     number of characters placed in the buffer
*/

static int
_pcre_ord2utf8(int cvalue, uschar *buffer)
{
#ifdef SUPPORT_UTF8
size_t i, j;
for (i = 0; i < _pcre_utf8_table1_size; i++)
  if (cvalue <= _pcre_utf8_table1[i]) break;
buffer += i;
for (j = i; j > 0; j--)
 {
 *buffer-- = 0x80 | (cvalue & 0x3f);
 cvalue >>= 6;
 }
*buffer = _pcre_utf8_table2[i] | cvalue;
return i + 1;
#else
return 0;   /* Keep compiler happy; this function won't ever be */
#endif      /* called when SUPPORT_UTF8 is not defined. */
}
/* End pcre_ord2utf8.c */

/* Begin pcre_fullinfo.c */
/*************************************************
*        Return info about compiled pattern      *
*************************************************/

/* This is a newer "info" function which has an extensible interface so
that additional items can be added compatibly.

Arguments:
  argument_re      points to compiled code
  extra_data       points extra data, or NULL
  what             what information is required
  where            where to put the information

Returns:           0 if data returned, negative on error
*/

int
pcre_fullinfo(const pcre *argument_re, const pcre_extra *extra_data, int what,
  void *where)
{
const real_pcre *re = (const real_pcre *)argument_re;
const pcre_study_data *study = NULL;

if (re == NULL || where == NULL) return PCRE_ERROR_NULL;

if (extra_data != NULL && (extra_data->flags & PCRE_EXTRA_STUDY_DATA) != 0)
  study = (const pcre_study_data *)extra_data->study_data;

if (re->magic_number != MAGIC_NUMBER)
  {
  return PCRE_ERROR_BADMAGIC;
  }

switch (what)
  {
  case PCRE_INFO_OPTIONS:
  *((unsigned long int *)where) = re->options & PUBLIC_OPTIONS;
  break;

  case PCRE_INFO_SIZE:
  *((size_t *)where) = re->size;
  break;

  case PCRE_INFO_STUDYSIZE:
  *((size_t *)where) = (study == NULL)? 0 : study->size;
  break;

  case PCRE_INFO_CAPTURECOUNT:
  *((int *)where) = re->top_bracket;
  break;

  case PCRE_INFO_BACKREFMAX:
  *((int *)where) = re->top_backref;
  break;

  case PCRE_INFO_FIRSTBYTE:
  *((int *)where) =
    ((re->options & PCRE_FIRSTSET) != 0)? re->first_byte :
    ((re->options & PCRE_STARTLINE) != 0)? -1 : -2;
  break;

  /* Make sure we pass back the pointer to the bit vector in the external
  block, not the internal copy (with flipped integer fields). */

  case PCRE_INFO_FIRSTTABLE:
  *((const uschar **)where) =
    (study != NULL && (study->options & PCRE_STUDY_MAPPED) != 0)?
      ((const pcre_study_data *)extra_data->study_data)->start_bits : NULL;
  break;

  case PCRE_INFO_LASTLITERAL:
  *((int *)where) =
    ((re->options & PCRE_REQCHSET) != 0)? re->req_byte : -1;
  break;

  case PCRE_INFO_NAMEENTRYSIZE:
  *((int *)where) = re->name_entry_size;
  break;

  case PCRE_INFO_NAMECOUNT:
  *((int *)where) = re->name_count;
  break;

  case PCRE_INFO_NAMETABLE:
  *((const uschar **)where) = (const uschar *)re + re->name_table_offset;
  break;

  case PCRE_INFO_DEFAULT_TABLES:
  *((const uschar **)where) = (const uschar *)(_pcre_default_tables);
  break;

  default: return PCRE_ERROR_BADOPTION;
  }

return 0;
}
/* End pcre_fullinfo.c */
/* Begin pcre_newline.c */
/*************************************************
*      Check for newline at given position       *
*************************************************/

/* It is guaranteed that the initial value of ptr is less than the end of the
string that is being processed.

Arguments:
  ptr          pointer to possible newline
  type         the newline type
  endptr       pointer to the end of the string
  lenptr       where to return the length
  utf8         true if in utf8 mode

Returns:       true or false
*/

static bool
_pcre_is_newline(const uschar *ptr, int type, const uschar *endptr,
  int *lenptr, bool utf8)
{
int c;
if (utf8) { GETCHAR(c, ptr); } else c = *ptr;

if (type == NLTYPE_ANYCRLF) switch(c)
  {
  case 0x000a: *lenptr = 1; return true;             /* LF */
  case 0x000d: *lenptr = (ptr < endptr - 1 && ptr[1] == 0x0a)? 2 : 1;
               return true;                          /* CR */
  default: return false;
  }

/* NLTYPE_ANY */

else switch(c)
  {
  case 0x000a:                                       /* LF */
  case 0x000b:                                       /* VT */
  case 0x000c: *lenptr = 1; return true;             /* FF */
  case 0x000d: *lenptr = (ptr < endptr - 1 && ptr[1] == 0x0a)? 2 : 1;
               return true;                          /* CR */
  case 0x0085: *lenptr = utf8? 2 : 1; return true;   /* NEL */
  case 0x2028:                                       /* LS */
  case 0x2029: *lenptr = 3; return true;             /* PS */
  default: return false;
  }
}



/*************************************************
*     Check for newline at previous position     *
*************************************************/

/* It is guaranteed that the initial value of ptr is greater than the start of
the string that is being processed.

Arguments:
  ptr          pointer to possible newline
  type         the newline type
  startptr     pointer to the start of the string
  lenptr       where to return the length
  utf8         true if in utf8 mode

Returns:       true or false
*/

static bool
_pcre_was_newline(const uschar *ptr, int type, const uschar *startptr,
  int *lenptr, bool utf8)
{
int c;
ptr--;
if (utf8)
  {
  BACKCHAR(ptr);
  GETCHAR(c, ptr);
  }
else c = *ptr;

if (type == NLTYPE_ANYCRLF) switch(c)
  {
  case 0x000a: *lenptr = (ptr > startptr && ptr[-1] == 0x0d)? 2 : 1;
               return true;                         /* LF */
  case 0x000d: *lenptr = 1; return true;            /* CR */
  default: return true;
  }

else switch(c)
  {
  case 0x000a: *lenptr = (ptr > startptr && ptr[-1] == 0x0d)? 2 : 1;
               return true;                         /* LF */
  case 0x000b:                                      /* VT */
  case 0x000c:                                      /* FF */
  case 0x000d: *lenptr = 1; return true;            /* CR */
  case 0x0085: *lenptr = utf8? 2 : 1; return true;  /* NEL */
  case 0x2028:                                      /* LS */
  case 0x2029: *lenptr = 3; return true;            /* PS */
  default: return false;
  }
}

/* End pcre_newline.c */
#define NLBLOCK cd
#define PSSTART start_pattern  /* Field containing processed string start */
#define PSEND   end_pattern    /* Field containing processed string end */
/* Begin pcre_compile.c */
/*************************************************
*            Handle escapes                      *
*************************************************/

/* This function is called when a \ has been encountered. It either returns a
positive value for a simple escape such as \n, or a negative value which
encodes one of the more complicated things such as \d. A backreference to group
n is returned as -(ESC_REF + n); ESC_REF is the highest ESC_xxx macro. When
UTF-8 is enabled, a positive value greater than 255 may be returned. On entry,
ptr is pointing at the \. On exit, it is on the final character of the escape
sequence.

Arguments:
  ptrptr         points to the pattern position pointer
  errorcodeptr   points to the errorcode variable
  bracount       number of previous extracting brackets
  options        the options bits
  isclass        true if inside a character class

Returns:         zero or positive => a data character
                 negative => a special escape sequence
                 on error, errorptr is set
*/

static int
check_escape(const uschar **ptrptr, int *errorcodeptr, int bracount,
  int options, bool isclass)
{
bool utf8 = (options & PCRE_UTF8) != 0;
const uschar *ptr = *ptrptr + 1;
int c, i;

GETCHARINCTEST(c, ptr);           /* Get character value, increment pointer */
ptr--;                            /* Set pointer back to the last byte */

/* If backslash is at the end of the pattern, it's an error. */

if (c == 0) *errorcodeptr = ERR1;

/* Non-alphamerics are literals. For digits or letters, do an initial lookup in
a table. A non-zero result is something that can be returned immediately.
Otherwise further processing may be required. */

else if (c < '0' || c > 'z') {}                           /* Not alphameric */
else if ((i = escapes[c - '0']) != 0) c = i;

/* Escapes that need further processing, or are illegal. */

else
  {
  const uschar *oldptr;
  bool braced, negated;

  switch (c)
    {
    /* A number of Perl escapes are not handled by PCRE. We give an explicit
    error. */

    case 'l':
    case 'L':
    case 'N':
    case 'u':
    case 'U':
    *errorcodeptr = ERR37;
    break;

    /* \g must be followed by a number, either plain or braced. If positive, it
    is an absolute backreference. If negative, it is a relative backreference.
    This is a Perl 5.10 feature. */

    case 'g':
    if (ptr[1] == '{')
      {
      braced = true;
      ptr++;
      }
    else braced = false;

    if (ptr[1] == '-')
      {
      negated = true;
      ptr++;
      }
    else negated = false;

    c = 0;
    while ((digitab[ptr[1]] & ctype_digit) != 0)
      c = c * 10 + *(++ptr) - '0';

    if (c == 0 || (braced && *(++ptr) != '}'))
      {
      *errorcodeptr = ERR57;
      return 0;
      }

    if (negated)
      {
      if (c > bracount)
        {
        *errorcodeptr = ERR15;
        return 0;
        }
      c = bracount - (c - 1);
      }

    c = -(ESC_REF + c);
    break;

    /* The handling of escape sequences consisting of a string of digits
    starting with one that is not zero is not straightforward. By experiment,
    the way Perl works seems to be as follows:

    Outside a character class, the digits are read as a decimal number. If the
    number is less than 10, or if there are that many previous extracting
    left brackets, then it is a back reference. Otherwise, up to three octal
    digits are read to form an escaped byte. Thus \123 is likely to be octal
    123 (cf \0123, which is octal 012 followed by the literal 3). If the octal
    value is greater than 377, the least significant 8 bits are taken. Inside a
    character class, \ followed by a digit is always an octal number. */

    case '1': case '2': case '3': case '4': case '5':
    case '6': case '7': case '8': case '9':

    if (!isclass)
      {
      oldptr = ptr;
      c -= '0';
      while ((digitab[ptr[1]] & ctype_digit) != 0)
        c = c * 10 + *(++ptr) - '0';
      if (c < 10 || c <= bracount)
        {
        c = -(ESC_REF + c);
        break;
        }
      ptr = oldptr;      /* Put the pointer back and fall through */
      }

    /* Handle an octal number following \. If the first digit is 8 or 9, Perl
    generates a binary zero byte and treats the digit as a following literal.
    Thus we have to pull back the pointer by one. */

    if ((c = *ptr) >= '8')
      {
      ptr--;
      c = 0;
      break;
      }

    /* \0 always starts an octal number, but we may drop through to here with a
    larger first octal digit. The original code used just to take the least
    significant 8 bits of octal numbers (I think this is what early Perls used
    to do). Nowadays we allow for larger numbers in UTF-8 mode, but no more
    than 3 octal digits. */

    case '0':
    c -= '0';
    while(i++ < 2 && ptr[1] >= '0' && ptr[1] <= '7')
        c = c * 8 + *(++ptr) - '0';
    if (!utf8 && c > 255) *errorcodeptr = ERR51;
    break;

    /* \x is complicated. \x{ddd} is a character number which can be greater
    than 0xff in utf8 mode, but only if the ddd are hex digits. If not, { is
    treated as a data character. */

    case 'x':
    if (ptr[1] == '{')
      {
      const uschar *pt = ptr + 2;
      int count = 0;

      c = 0;
      while ((digitab[*pt] & ctype_xdigit) != 0)
        {
        register int cc = *pt++;
        if (c == 0 && cc == '0') continue;     /* Leading zeroes */
        count++;

        if (cc >= 'a') cc -= 32;               /* Convert to upper case */
        c = (c << 4) + cc - ((cc < 'A')? '0' : ('A' - 10));
        }

      if (*pt == '}')
        {
        if (c < 0 || count > (utf8? 8 : 2)) *errorcodeptr = ERR34;
        ptr = pt;
        break;
        }

      /* If the sequence of hex digits does not end with '}', then we don't
      recognize this construct; fall through to the normal \x handling. */
      }

    /* Read just a single-byte hex-defined char */

    c = 0;
    while (i++ < 2 && (digitab[ptr[1]] & ctype_xdigit) != 0)
      {
      int cc;                               /* Some compilers don't like ++ */
      cc = *(++ptr);                        /* in initializers */
      if (cc >= 'a') cc -= 32;              /* Convert to upper case */
      c = c * 16 + cc - ((cc < 'A')? '0' : ('A' - 10));
      }
    break;

    /* For \c, a following letter is upper-cased; then the 0x40 bit is flipped.
    This coding is ASCII-specific, but then the whole concept of \cx is
    ASCII-specific. (However, an EBCDIC equivalent has now been added.) */

    case 'c':
    c = *(++ptr);
    if (c == 0)
      {
      *errorcodeptr = ERR2;
      return 0;
      }

    if (c >= 'a' && c <= 'z') c -= 32;
    c ^= 0x40;
    break;

    /* PCRE_EXTRA enables extensions to Perl in the matter of escapes. Any
    other alphameric following \ is an error if PCRE_EXTRA was set; otherwise,
    for Perl compatibility, it is a literal. This code looks a bit odd, but
    there used to be some cases other than the default, and there may be again
    in future, so I haven't "optimized" it. */

    default:
    if ((options & PCRE_EXTRA) != 0)
      {
      *errorcodeptr = ERR3;
      }
    break;
    }
  }

*ptrptr = ptr;
return c;
}



#ifdef SUPPORT_UCP
/*************************************************
*               Handle \P and \p                 *
*************************************************/

/* This function is called after \P or \p has been encountered, provided that
PCRE is compiled with support for Unicode properties. On entry, ptrptr is
pointing at the P or p. On exit, it is pointing at the final character of the
escape sequence.

Argument:
  ptrptr         points to the pattern position pointer
  negptr         points to a boolean that is set true for negation else false
  dptr           points to an int that is set to the detailed property value
  errorcodeptr   points to the error code variable

Returns:         type value from ucp_type_table, or -1 for an invalid type
*/

static int
get_ucp(const uschar **ptrptr, bool *negptr, int *dptr, int *errorcodeptr)
{
int c, i, bot, top;
const uschar *ptr = *ptrptr;
char name[32];

c = *(++ptr);
if (c == 0) goto ERROR_RETURN;

*negptr = false;

/* \P or \p can be followed by a name in {}, optionally preceded by ^ for
negation. */

if (c == '{')
  {
  if (ptr[1] == '^')
    {
    *negptr = true;
    ptr++;
    }
  for (i = 0; i < sizeof(name) - 1; i++)
    {
    c = *(++ptr);
    if (c == 0) goto ERROR_RETURN;
    if (c == '}') break;
    name[i] = c;
    }
  if (c !='}') goto ERROR_RETURN;
  name[i] = 0;
  }

/* Otherwise there is just one following character */

else
  {
  name[0] = c;
  name[1] = 0;
  }

*ptrptr = ptr;

/* Search for a recognized property name using binary chop */

bot = 0;
top = _pcre_utt_size;

while (bot < top)
  {
  i = (bot + top) >> 1;
  c = strcmp(name, _pcre_utt[i].name);
  if (c == 0)
    {
    *dptr = _pcre_utt[i].value;
    return _pcre_utt[i].type;
    }
  if (c > 0) bot = i + 1; else top = i;
  }

*errorcodeptr = ERR47;
*ptrptr = ptr;
return -1;

ERROR_RETURN:
*errorcodeptr = ERR46;
*ptrptr = ptr;
return -1;
}
#endif




/*************************************************
*            Check for counted repeat            *
*************************************************/

/* This function is called when a '{' is encountered in a place where it might
start a quantifier. It looks ahead to see if it really is a quantifier or not.
It is only a quantifier if it is one of the forms {ddd} {ddd,} or {ddd,ddd}
where the ddds are digits.

Arguments:
  p         pointer to the first char after '{'

Returns:    true or false
*/

static bool
is_counted_repeat(const uschar *p)
{
if ((digitab[*p++] & ctype_digit) == 0) return false;
while ((digitab[*p] & ctype_digit) != 0) p++;
if (*p == '}') return true;

if (*p++ != ',') return false;
if (*p == '}') return true;

if ((digitab[*p++] & ctype_digit) == 0) return false;
while ((digitab[*p] & ctype_digit) != 0) p++;

return (*p == '}');
}



/*************************************************
*         Read repeat counts                     *
*************************************************/

/* Read an item of the form {n,m} and return the values. This is called only
after is_counted_repeat() has confirmed that a repeat-count quantifier exists,
so the syntax is guaranteed to be correct, but we need to check the values.

Arguments:
  p              pointer to first char after '{'
  minp           pointer to int for min
  maxp           pointer to int for max
                 returned as -1 if no max
  errorcodeptr   points to error code variable

Returns:         pointer to '}' on success;
                 current ptr on error, with errorcodeptr set non-zero
*/

static const uschar *
read_repeat_counts(const uschar *p, int *minp, int *maxp, int *errorcodeptr)
{
int min = 0;
int max = -1;

/* Read the minimum value and do a paranoid check: a negative value indicates
an integer overflow. */

while ((digitab[*p] & ctype_digit) != 0) min = min * 10 + *p++ - '0';
if (min < 0 || min > 65535)
  {
  *errorcodeptr = ERR5;
  return p;
  }

/* Read the maximum value if there is one, and again do a paranoid on its size.
Also, max must not be less than min. */

if (*p == '}') max = min; else
  {
  if (*(++p) != '}')
    {
    max = 0;
    while((digitab[*p] & ctype_digit) != 0) max = max * 10 + *p++ - '0';
    if (max < 0 || max > 65535)
      {
      *errorcodeptr = ERR5;
      return p;
      }
    if (max < min)
      {
      *errorcodeptr = ERR4;
      return p;
      }
    }
  }

/* Fill in the required variables, and pass back the pointer to the terminating
'}'. */

*minp = min;
*maxp = max;
return p;
}



/*************************************************
*       Find forward referenced subpattern       *
*************************************************/

/* This function scans along a pattern's text looking for capturing
subpatterns, and counting them. If it finds a named pattern that matches the
name it is given, it returns its number. Alternatively, if the name is NULL, it
returns when it reaches a given numbered subpattern. This is used for forward
references to subpatterns. We know that if (?P< is encountered, the name will
be terminated by '>' because that is checked in the first pass.

Arguments:
  ptr          current position in the pattern
  count        current count of capturing parens so far encountered
  name         name to seek, or NULL if seeking a numbered subpattern
  lorn         name length, or subpattern number if name is NULL
  xmode        true if we are in /x mode

Returns:       the number of the named subpattern, or -1 if not found
*/

static int
find_parens(const uschar *ptr, int count, const uschar *name, int lorn,
  bool xmode)
{
const uschar *thisname;

for (; *ptr != 0; ptr++)
  {
  int term;

  /* Skip over backslashed characters and also entire \Q...\E */

  if (*ptr == '\\')
    {
    if (*(++ptr) == 0) return -1;
    if (*ptr == 'Q') for (;;)
      {
      while (*(++ptr) != 0 && *ptr != '\\');
      if (*ptr == 0) return -1;
      if (*(++ptr) == 'E') break;
      }
    continue;
    }

  /* Skip over character classes */

  if (*ptr == '[')
    {
    while (*(++ptr) != ']')
      {
      if (*ptr == '\\')
        {
        if (*(++ptr) == 0) return -1;
        if (*ptr == 'Q') for (;;)
          {
          while (*(++ptr) != 0 && *ptr != '\\');
          if (*ptr == 0) return -1;
          if (*(++ptr) == 'E') break;
          }
        continue;
        }
      }
    continue;
    }

  /* Skip comments in /x mode */

  if (xmode && *ptr == '#')
    {
    while (*(++ptr) != 0 && *ptr != '\n');
    if (*ptr == 0) return -1;
    continue;
    }

  /* An opening parens must now be a real metacharacter */

  if (*ptr != '(') continue;
  if (ptr[1] != '?')
    {
    count++;
    if (name == NULL && count == lorn) return count;
    continue;
    }

  ptr += 2;
  if (*ptr == 'P') ptr++;                      /* Allow optional P */

  /* We have to disambiguate (?<! and (?<= from (?<name> */

  if ((*ptr != '<' || ptr[1] == '!' || ptr[1] == '=') &&
       *ptr != '\'')
    continue;

  count++;

  if (name == NULL && count == lorn) return count;
  term = *ptr++;
  if (term == '<') term = '>';
  thisname = ptr;
  while (*ptr != term) ptr++;
  if (name != NULL && lorn == ptr - thisname &&
      strncmp((const char *)name, (const char *)thisname, lorn) == 0)
    return count;
  }

return -1;
}



/*************************************************
*      Find first significant op code            *
*************************************************/

/* This is called by several functions that scan a compiled expression looking
for a fixed first character, or an anchoring op code etc. It skips over things
that do not influence this. For some calls, a change of option is important.
For some calls, it makes sense to skip negative forward and all backward
assertions, and also the \b assertion; for others it does not.

Arguments:
  code         pointer to the start of the group
  options      pointer to external options
  optbit       the option bit whose changing is significant, or
                 zero if none are
  skipassert   true if certain assertions are to be skipped

Returns:       pointer to the first significant opcode
*/

static const uschar*
first_significant_code(const uschar *code, int *options, int optbit,
  bool skipassert)
{
for (;;)
  {
  switch ((int)*code)
    {
    case OP_OPT:
    if (optbit > 0 && ((int)code[1] & optbit) != (*options & optbit))
      *options = (int)code[1];
    code += 2;
    break;

    case OP_ASSERT_NOT:
    case OP_ASSERTBACK:
    case OP_ASSERTBACK_NOT:
    if (!skipassert) return code;
    do code += GET(code, 1); while (*code == OP_ALT);
    code += _pcre_OP_lengths[*code];
    break;

    case OP_WORD_BOUNDARY:
    case OP_NOT_WORD_BOUNDARY:
    if (!skipassert) return code;
    /* Fall through */

    case OP_CALLOUT:
    case OP_CREF:
    case OP_RREF:
    case OP_DEF:
    code += _pcre_OP_lengths[*code];
    break;

    default:
    return code;
    }
  }
/* Control never reaches here */
}




/*************************************************
*        Find the fixed length of a pattern      *
*************************************************/

/* Scan a pattern and compute the fixed length of subject that will match it,
if the length is fixed. This is needed for dealing with backward assertions.
In UTF8 mode, the result is in characters rather than bytes.

Arguments:
  code     points to the start of the pattern (the bracket)
  options  the compiling options

Returns:   the fixed length, or -1 if there is no fixed length,
             or -2 if \C was encountered
*/

static int
find_fixedlength(uschar *code, int options)
{
int length = -1;

register int branchlength = 0;
register uschar *cc = code + 1 + LINK_SIZE;

/* Scan along the opcodes for this branch. If we get to the end of the
branch, check the length against that of the other branches. */

for (;;)
  {
  int d;
  register int op = *cc;

  switch (op)
    {
    case OP_CBRA:
    case OP_BRA:
    case OP_ONCE:
    case OP_COND:
    d = find_fixedlength(cc + ((op == OP_CBRA)? 2:0), options);
    if (d < 0) return d;
    branchlength += d;
    do cc += GET(cc, 1); while (*cc == OP_ALT);
    cc += 1 + LINK_SIZE;
    break;

    /* Reached end of a branch; if it's a ket it is the end of a nested
    call. If it's ALT it is an alternation in a nested call. If it is
    END it's the end of the outer call. All can be handled by the same code. */

    case OP_ALT:
    case OP_KET:
    case OP_KETRMAX:
    case OP_KETRMIN:
    case OP_END:
    if (length < 0) length = branchlength;
      else if (length != branchlength) return -1;
    if (*cc != OP_ALT) return length;
    cc += 1 + LINK_SIZE;
    branchlength = 0;
    break;

    /* Skip over assertive subpatterns */

    case OP_ASSERT:
    case OP_ASSERT_NOT:
    case OP_ASSERTBACK:
    case OP_ASSERTBACK_NOT:
    do cc += GET(cc, 1); while (*cc == OP_ALT);
    /* Fall through */

    /* Skip over things that don't match chars */

    case OP_REVERSE:
    case OP_CREF:
    case OP_RREF:
    case OP_DEF:
    case OP_OPT:
    case OP_CALLOUT:
    case OP_SOD:
    case OP_SOM:
    case OP_EOD:
    case OP_EODN:
    case OP_CIRC:
    case OP_DOLL:
    case OP_NOT_WORD_BOUNDARY:
    case OP_WORD_BOUNDARY:
    cc += _pcre_OP_lengths[*cc];
    break;

    /* Handle literal characters */

    case OP_CHAR:
    case OP_CHARNC:
    case OP_NOT:
    branchlength++;
    cc += 2;
#ifdef SUPPORT_UTF8
    if ((options & PCRE_UTF8) != 0)
      {
      while ((*cc & 0xc0) == 0x80) cc++;
      }
#endif
    break;

    /* Handle exact repetitions. The count is already in characters, but we
    need to skip over a multibyte character in UTF8 mode.  */

    case OP_EXACT:
    branchlength += GET2(cc,1);
    cc += 4;
#ifdef SUPPORT_UTF8
    if ((options & PCRE_UTF8) != 0)
      {
      while((*cc & 0x80) == 0x80) cc++;
      }
#endif
    break;

    case OP_TYPEEXACT:
    branchlength += GET2(cc,1);
    cc += 4;
    break;

    /* Handle single-char matchers */

    case OP_PROP:
    case OP_NOTPROP:
    cc += 2;
    /* Fall through */

    case OP_NOT_DIGIT:
    case OP_DIGIT:
    case OP_NOT_WHITESPACE:
    case OP_WHITESPACE:
    case OP_NOT_WORDCHAR:
    case OP_WORDCHAR:
    case OP_ANY:
    branchlength++;
    cc++;
    break;

    /* The single-byte matcher isn't allowed */

    case OP_ANYBYTE:
    return -2;

    /* Check a class for variable quantification */

#ifdef SUPPORT_UTF8
    case OP_XCLASS:
    cc += GET(cc, 1) - 33;
    /* Fall through */
#endif

    case OP_CLASS:
    case OP_NCLASS:
    cc += 33;

    switch (*cc)
      {
      case OP_CRSTAR:
      case OP_CRMINSTAR:
      case OP_CRQUERY:
      case OP_CRMINQUERY:
      return -1;

      case OP_CRRANGE:
      case OP_CRMINRANGE:
      if (GET2(cc,1) != GET2(cc,3)) return -1;
      branchlength += GET2(cc,1);
      cc += 5;
      break;

      default:
      branchlength++;
      }
    break;

    /* Anything else is variable length */

    default:
    return -1;
    }
  }
/* Control never gets here */
}




/*************************************************
*    Scan compiled regex for numbered bracket    *
*************************************************/

/* This little function scans through a compiled pattern until it finds a
capturing bracket with the given number.

Arguments:
  code        points to start of expression
  utf8        true in UTF-8 mode
  number      the required bracket number

Returns:      pointer to the opcode for the bracket, or NULL if not found
*/

static const uschar *
find_bracket(const uschar *code, bool utf8, int number)
{
for (;;)
  {
  register int c = *code;
  if (c == OP_END) return NULL;

  /* XCLASS is used for classes that cannot be represented just by a bit
  map. This includes negated single high-valued characters. The length in
  the table is zero; the actual length is stored in the compiled code. */

  if (c == OP_XCLASS) code += GET(code, 1);

  /* Handle capturing bracket */

  else if (c == OP_CBRA)
    {
    int n = GET2(code, 1+LINK_SIZE);
    if (n == number) return (uschar *)code;
    code += _pcre_OP_lengths[c];
    }

  /* In UTF-8 mode, opcodes that are followed by a character may be followed by
  a multi-byte character. The length in the table is a minimum, so we have to
  arrange to skip the extra bytes. */

  else
    {
    code += _pcre_OP_lengths[c];
#ifdef SUPPORT_UTF8
    if (utf8) switch(c)
      {
      case OP_CHAR:
      case OP_CHARNC:
      case OP_EXACT:
      case OP_UPTO:
      case OP_MINUPTO:
      case OP_POSUPTO:
      case OP_STAR:
      case OP_MINSTAR:
      case OP_POSSTAR:
      case OP_PLUS:
      case OP_MINPLUS:
      case OP_POSPLUS:
      case OP_QUERY:
      case OP_MINQUERY:
      case OP_POSQUERY:
      if (code[-1] >= 0xc0) code += _pcre_utf8_table4[code[-1] & 0x3f];
      break;
      }
#endif
    }
  }
}



/*************************************************
*   Scan compiled regex for recursion reference  *
*************************************************/

/* This little function scans through a compiled pattern until it finds an
instance of OP_RECURSE.

Arguments:
  code        points to start of expression
  utf8        true in UTF-8 mode

Returns:      pointer to the opcode for OP_RECURSE, or NULL if not found
*/

static const uschar *
find_recurse(const uschar *code, bool utf8)
{
for (;;)
  {
  register int c = *code;
  if (c == OP_END) return NULL;
  if (c == OP_RECURSE) return code;

  /* XCLASS is used for classes that cannot be represented just by a bit
  map. This includes negated single high-valued characters. The length in
  the table is zero; the actual length is stored in the compiled code. */

  if (c == OP_XCLASS) code += GET(code, 1);

  /* Otherwise, we get the item's length from the table. In UTF-8 mode, opcodes
  that are followed by a character may be followed by a multi-byte character.
  The length in the table is a minimum, so we have to arrange to skip the extra
  bytes. */

  else
    {
    code += _pcre_OP_lengths[c];
#ifdef SUPPORT_UTF8
    if (utf8) switch(c)
      {
      case OP_CHAR:
      case OP_CHARNC:
      case OP_EXACT:
      case OP_UPTO:
      case OP_MINUPTO:
      case OP_POSUPTO:
      case OP_STAR:
      case OP_MINSTAR:
      case OP_POSSTAR:
      case OP_PLUS:
      case OP_MINPLUS:
      case OP_POSPLUS:
      case OP_QUERY:
      case OP_MINQUERY:
      case OP_POSQUERY:
      if (code[-1] >= 0xc0) code += _pcre_utf8_table4[code[-1] & 0x3f];
      break;
      }
#endif
    }
  }
}



/*************************************************
*    Scan compiled branch for non-emptiness      *
*************************************************/

/* This function scans through a branch of a compiled pattern to see whether it
can match the empty string or not. It is called from could_be_empty()
below and from compile_branch() when checking for an unlimited repeat of a
group that can match nothing. Note that first_significant_code() skips over
assertions. If we hit an unclosed bracket, we return "empty" - this means we've
struck an inner bracket whose current branch will already have been scanned.

Arguments:
  code        points to start of search
  endcode     points to where to stop
  utf8        true if in UTF8 mode

Returns:      true if what is matched could be empty
*/

static bool
could_be_empty_branch(const uschar *code, const uschar *endcode, bool utf8)
{
register int c;
for (code = first_significant_code(code + _pcre_OP_lengths[*code], NULL, 0, true);
     code < endcode;
     code = first_significant_code(code + _pcre_OP_lengths[c], NULL, 0, true))
  {
  const uschar *ccode;

  c = *code;

  if (c == OP_BRA || c == OP_CBRA || c == OP_ONCE)
    {
    bool empty_branch;
    if (GET(code, 1) == 0) return true;    /* Hit unclosed bracket */

    /* Scan a closed bracket */

    empty_branch = false;
    do
      {
      if (!empty_branch && could_be_empty_branch(code, endcode, utf8))
        empty_branch = true;
      code += GET(code, 1);
      }
    while (*code == OP_ALT);
    if (!empty_branch) return false;   /* All branches are non-empty */

    /* Move past the KET and fudge things so that the increment in the "for"
    above has no effect. */

    c = OP_END;
    code += 1 + LINK_SIZE - _pcre_OP_lengths[c];
    continue;
    }

  /* Handle the other opcodes */

  switch (c)
    {
    /* Check for quantifiers after a class */

#ifdef SUPPORT_UTF8
    case OP_XCLASS:
    ccode = code + GET(code, 1);
    goto CHECK_CLASS_REPEAT;
#endif

    case OP_CLASS:
    case OP_NCLASS:
    ccode = code + 33;

#ifdef SUPPORT_UTF8
    CHECK_CLASS_REPEAT:
#endif

    switch (*ccode)
      {
      case OP_CRSTAR:            /* These could be empty; continue */
      case OP_CRMINSTAR:
      case OP_CRQUERY:
      case OP_CRMINQUERY:
      break;

      default:                   /* Non-repeat => class must match */
      case OP_CRPLUS:            /* These repeats aren't empty */
      case OP_CRMINPLUS:
      return false;

      case OP_CRRANGE:
      case OP_CRMINRANGE:
      if (GET2(ccode, 1) > 0) return false;  /* Minimum > 0 */
      break;
      }
    break;

    /* Opcodes that must match a character */

    case OP_PROP:
    case OP_NOTPROP:
    case OP_EXTUNI:
    case OP_NOT_DIGIT:
    case OP_DIGIT:
    case OP_NOT_WHITESPACE:
    case OP_WHITESPACE:
    case OP_NOT_WORDCHAR:
    case OP_WORDCHAR:
    case OP_ANY:
    case OP_ANYBYTE:
    case OP_CHAR:
    case OP_CHARNC:
    case OP_NOT:
    case OP_PLUS:
    case OP_MINPLUS:
    case OP_POSPLUS:
    case OP_EXACT:
    case OP_NOTPLUS:
    case OP_NOTMINPLUS:
    case OP_NOTPOSPLUS:
    case OP_NOTEXACT:
    case OP_TYPEPLUS:
    case OP_TYPEMINPLUS:
    case OP_TYPEPOSPLUS:
    case OP_TYPEEXACT:
    return false;

    /* End of branch */

    case OP_KET:
    case OP_KETRMAX:
    case OP_KETRMIN:
    case OP_ALT:
    return true;

    /* In UTF-8 mode, STAR, MINSTAR, POSSTAR, QUERY, MINQUERY, POSQUERY, UPTO,
    MINUPTO, and POSUPTO may be followed by a multibyte character */

#ifdef SUPPORT_UTF8
    case OP_STAR:
    case OP_MINSTAR:
    case OP_POSSTAR:
    case OP_QUERY:
    case OP_MINQUERY:
    case OP_POSQUERY:
    case OP_UPTO:
    case OP_MINUPTO:
    case OP_POSUPTO:
    if (utf8) while ((code[2] & 0xc0) == 0x80) code++;
    break;
#endif
    }
  }

return true;
}



/*************************************************
*    Scan compiled regex for non-emptiness       *
*************************************************/

/* This function is called to check for left recursive calls. We want to check
the current branch of the current pattern to see if it could match the empty
string. If it could, we must look outwards for branches at other levels,
stopping when we pass beyond the bracket which is the subject of the recursion.

Arguments:
  code        points to start of the recursion
  endcode     points to where to stop (current RECURSE item)
  bcptr       points to the chain of current (unclosed) branch starts
  utf8        true if in UTF-8 mode

Returns:      true if what is matched could be empty
*/

static bool
could_be_empty(const uschar *code, const uschar *endcode, branch_chain *bcptr,
  bool utf8)
{
while (bcptr != NULL && bcptr->current >= code)
  {
  if (!could_be_empty_branch(bcptr->current, endcode, utf8)) return false;
  bcptr = bcptr->outer;
  }
return true;
}



/*************************************************
*           Check for POSIX class syntax         *
*************************************************/

/* This function is called when the sequence "[:" or "[." or "[=" is
encountered in a character class. It checks whether this is followed by an
optional ^ and then a sequence of letters, terminated by a matching ":]" or
".]" or "=]".

Argument:
  ptr      pointer to the initial [
  endptr   where to return the end pointer
  cd       pointer to compile data

Returns:   true or false
*/

static bool
check_posix_syntax(const uschar *ptr, const uschar **endptr, compile_data *cd)
{
int terminator;          /* Don't combine these lines; the Solaris cc */
terminator = *(++ptr);   /* compiler warns about "non-constant" initializer. */
if (*(++ptr) == '^') ptr++;
while ((cd->ctypes[*ptr] & ctype_letter) != 0) ptr++;
if (*ptr == terminator && ptr[1] == ']')
  {
  *endptr = ptr;
  return true;
  }
return false;
}




/*************************************************
*          Check POSIX class name                *
*************************************************/

/* This function is called to check the name given in a POSIX-style class entry
such as [:alnum:].

Arguments:
  ptr        points to the first letter
  len        the length of the name

Returns:     a value representing the name, or -1 if unknown
*/

static int
check_posix_name(const uschar *ptr, int len)
{
register int yield = 0;
while (posix_name_lengths[yield] != 0)
  {
  if (len == posix_name_lengths[yield] &&
    strncmp((const char *)ptr, posix_names[yield], len) == 0) return yield;
  yield++;
  }
return -1;
}


/*************************************************
*    Adjust OP_RECURSE items in repeated group   *
*************************************************/

/* OP_RECURSE items contain an offset from the start of the regex to the group
that is referenced. This means that groups can be replicated for fixed
repetition simply by copying (because the recursion is allowed to refer to
earlier groups that are outside the current group). However, when a group is
optional (i.e. the minimum quantifier is zero), OP_BRAZERO is inserted before
it, after it has been compiled. This means that any OP_RECURSE items within it
that refer to the group itself or any contained groups have to have their
offsets adjusted. That one of the jobs of this function. Before it is called,
the partially compiled regex must be temporarily terminated with OP_END.

This function has been extended with the possibility of forward references for
recursions and subroutine calls. It must also check the list of such references
for the group we are dealing with. If it finds that one of the recursions in
the current group is on this list, it adjusts the offset in the list, not the
value in the reference (which is a group number).

Arguments:
  group      points to the start of the group
  adjust     the amount by which the group is to be moved
  utf8       true in UTF-8 mode
  cd         contains pointers to tables etc.
  save_hwm   the hwm forward reference pointer at the start of the group

Returns:     nothing
*/

static void
adjust_recurse(uschar *group, int adjust, bool utf8, compile_data *cd,
  uschar *save_hwm)
{
uschar *ptr = group;
while ((ptr = (uschar *)find_recurse(ptr, utf8)) != NULL)
  {
  int offset;
  uschar *hc;

  /* See if this recursion is on the forward reference list. If so, adjust the
  reference. */

  for (hc = save_hwm; hc < cd->hwm; hc += LINK_SIZE)
    {
    offset = GET(hc, 0);
    if (cd->start_code + offset == ptr + 1)
      {
      PUT(hc, 0, offset + adjust);
      break;
      }
    }

  /* Otherwise, adjust the recursion offset if it's after the start of this
  group. */

  if (hc >= cd->hwm)
    {
    offset = GET(ptr, 1);
    if (cd->start_code + offset >= group) PUT(ptr, 1, offset + adjust);
    }

  ptr += 1 + LINK_SIZE;
  }
}



/*************************************************
*        Insert an automatic callout point       *
*************************************************/

/* This function is called when the PCRE_AUTO_CALLOUT option is set, to insert
callout points before each pattern item.

Arguments:
  code           current code pointer
  ptr            current pattern pointer
  cd             pointers to tables etc

Returns:         new code pointer
*/

static uschar *
auto_callout(uschar *code, const uschar *ptr, compile_data *cd)
{
*code++ = OP_CALLOUT;
*code++ = 255;
PUT(code, 0, ptr - cd->start_pattern);  /* Pattern offset */
PUT(code, LINK_SIZE, 0);                /* Default length */
return code + 2*LINK_SIZE;
}



/*************************************************
*         Complete a callout item                *
*************************************************/

/* A callout item contains the length of the next item in the pattern, which
we can't fill in till after we have reached the relevant point. This is used
for both automatic and manual callouts.

Arguments:
  previous_callout   points to previous callout item
  ptr                current pattern pointer
  cd                 pointers to tables etc

Returns:             nothing
*/

static void
complete_callout(uschar *previous_callout, const uschar *ptr, compile_data *cd)
{
int length = ptr - cd->start_pattern - GET(previous_callout, 2);
PUT(previous_callout, 2 + LINK_SIZE, length);
}



#ifdef SUPPORT_UCP
/*************************************************
*           Get othercase range                  *
*************************************************/

/* This function is passed the start and end of a class range, in UTF-8 mode
with UCP support. It searches up the characters, looking for internal ranges of
characters in the "other" case. Each call returns the next one, updating the
start address.

Arguments:
  cptr        points to starting character value; updated
  d           end value
  ocptr       where to put start of othercase range
  odptr       where to put end of othercase range

Yield:        true when range returned; false when no more
*/

static bool
get_othercase_range(unsigned int *cptr, unsigned int d, unsigned int *ocptr,
  unsigned int *odptr)
{
unsigned int c, othercase, next;

for (c = *cptr; c <= d; c++)
  { if ((othercase = _pcre_ucp_othercase(c)) != NOTACHAR) break; }

if (c > d) return false;

*ocptr = othercase;
next = othercase + 1;

for (++c; c <= d; c++)
  {
  if (_pcre_ucp_othercase(c) != next) break;
  next++;
  }

*odptr = next - 1;
*cptr = c;

return true;
}
#endif  /* SUPPORT_UCP */



/*************************************************
*     Check if auto-possessifying is possible    *
*************************************************/

/* This function is called for unlimited repeats of certain items, to see
whether the next thing could possibly match the repeated item. If not, it makes
sense to automatically possessify the repeated item.

Arguments:
  op_code       the repeated op code
  this          data for this item, depends on the opcode
  utf8          true in UTF-8 mode
  utf8_char     used for utf8 character bytes, NULL if not relevant
  ptr           next character in pattern
  options       options bits
  cd            contains pointers to tables etc.

Returns:        true if possessifying is wanted
*/

static bool
check_auto_possessive(int op_code, int item, bool utf8, uschar *utf8_char,
  const uschar *ptr, int options, compile_data *cd)
{
int next;

/* Skip whitespace and comments in extended mode */

if ((options & PCRE_EXTENDED) != 0)
  {
  for (;;)
    {
    while ((cd->ctypes[*ptr] & ctype_space) != 0) ptr++;
    if (*ptr == '#')
      {
      while (*(++ptr) != 0)
        if (IS_NEWLINE(ptr)) { ptr += cd->nllen; break; }
      }
    else break;
    }
  }

/* If the next item is one that we can handle, get its value. A non-negative
value is a character, a negative value is an escape value. */

if (*ptr == '\\')
  {
  int temperrorcode = 0;
  next = check_escape(&ptr, &temperrorcode, cd->bracount, options, false);
  if (temperrorcode != 0) return false;
  ptr++;    /* Point after the escape sequence */
  }

else if ((cd->ctypes[*ptr] & ctype_meta) == 0)
  {
#ifdef SUPPORT_UTF8
  if (utf8) { GETCHARINC(next, ptr); } else
#endif
  next = *ptr++;
  }

else return false;

/* Skip whitespace and comments in extended mode */

if ((options & PCRE_EXTENDED) != 0)
  {
  for (;;)
    {
    while ((cd->ctypes[*ptr] & ctype_space) != 0) ptr++;
    if (*ptr == '#')
      {
      while (*(++ptr) != 0)
        if (IS_NEWLINE(ptr)) { ptr += cd->nllen; break; }
      }
    else break;
    }
  }

/* If the next thing is itself optional, we have to give up. */

if (*ptr == '*' || *ptr == '?' || strncmp((char *)ptr, "{0,", 3) == 0)
  return false;

/* Now compare the next item with the previous opcode. If the previous is a
positive single character match, "item" either contains the character or, if
"item" is greater than 127 in utf8 mode, the character's bytes are in
utf8_char. */


/* Handle cases when the next item is a character. */

if (next >= 0) switch(op_code)
  {
  case OP_CHAR:
#ifdef SUPPORT_UTF8
  if (utf8 && item > 127) { GETCHAR(item, utf8_char); }
#endif
  return item != next;

  /* For CHARNC (caseless character) we must check the other case. If we have
  Unicode property support, we can use it to test the other case of
  high-valued characters. */

  case OP_CHARNC:
#ifdef SUPPORT_UTF8
  if (utf8 && item > 127) { GETCHAR(item, utf8_char); }
#endif
  if (item == next) return false;
#ifdef SUPPORT_UTF8
  if (utf8)
    {
    unsigned int othercase;
    if (next < 128) othercase = cd->fcc[next]; else
#ifdef SUPPORT_UCP
    othercase = _pcre_ucp_othercase((unsigned int)next);
#else
    othercase = NOTACHAR;
#endif
    return (unsigned int)item != othercase;
    }
  else
#endif  /* SUPPORT_UTF8 */
  return (item != cd->fcc[next]);  /* Non-UTF-8 mode */

  /* For OP_NOT, "item" must be a single-byte character. */

  case OP_NOT:
  if (next < 0) return false;  /* Not a character */
  if (item == next) return true;
  if ((options & PCRE_CASELESS) == 0) return false;
#ifdef SUPPORT_UTF8
  if (utf8)
    {
    unsigned int othercase;
    if (next < 128) othercase = cd->fcc[next]; else
#ifdef SUPPORT_UCP
    othercase = _pcre_ucp_othercase(next);
#else
    othercase = NOTACHAR;
#endif
    return (unsigned int)item == othercase;
    }
  else
#endif  /* SUPPORT_UTF8 */
  return (item == cd->fcc[next]);  /* Non-UTF-8 mode */

  case OP_DIGIT:
  return next > 127 || (cd->ctypes[next] & ctype_digit) == 0;

  case OP_NOT_DIGIT:
  return next <= 127 && (cd->ctypes[next] & ctype_digit) != 0;

  case OP_WHITESPACE:
  return next > 127 || (cd->ctypes[next] & ctype_space) == 0;

  case OP_NOT_WHITESPACE:
  return next <= 127 && (cd->ctypes[next] & ctype_space) != 0;

  case OP_WORDCHAR:
  return next > 127 || (cd->ctypes[next] & ctype_word) == 0;

  case OP_NOT_WORDCHAR:
  return next <= 127 && (cd->ctypes[next] & ctype_word) != 0;

  default:
  return false;
  }


/* Handle the case when the next item is \d, \s, etc. */

switch(op_code)
  {
  case OP_CHAR:
  case OP_CHARNC:
#ifdef SUPPORT_UTF8
  if (utf8 && item > 127) { GETCHAR(item, utf8_char); }
#endif
  switch(-next)
    {
    case ESC_d:
    return item > 127 || (cd->ctypes[item] & ctype_digit) == 0;

    case ESC_D:
    return item <= 127 && (cd->ctypes[item] & ctype_digit) != 0;

    case ESC_s:
    return item > 127 || (cd->ctypes[item] & ctype_space) == 0;

    case ESC_S:
    return item <= 127 && (cd->ctypes[item] & ctype_space) != 0;

    case ESC_w:
    return item > 127 || (cd->ctypes[item] & ctype_word) == 0;

    case ESC_W:
    return item <= 127 && (cd->ctypes[item] & ctype_word) != 0;

    default:
    return false;
    }

  case OP_DIGIT:
  return next == -ESC_D || next == -ESC_s || next == -ESC_W;

  case OP_NOT_DIGIT:
  return next == -ESC_d;

  case OP_WHITESPACE:
  return next == -ESC_S || next == -ESC_d || next == -ESC_w;

  case OP_NOT_WHITESPACE:
  return next == -ESC_s;

  case OP_WORDCHAR:
  return next == -ESC_W || next == -ESC_s;

  case OP_NOT_WORDCHAR:
  return next == -ESC_w || next == -ESC_d;

  default:
  return false;
  }

/* Control does not reach here */
}



/*************************************************
*           Compile one branch                   *
*************************************************/

/* Scan the pattern, compiling it into the a vector. If the options are
changed during the branch, the pointer is used to change the external options
bits. This function is used during the pre-compile phase when we are trying
to find out the amount of memory needed, as well as during the real compile
phase. The value of lengthptr distinguishes the two phases.

Arguments:
  optionsptr     pointer to the option bits
  codeptr        points to the pointer to the current code point
  ptrptr         points to the current pattern pointer
  errorcodeptr   points to error code variable
  firstbyteptr   set to initial literal character, or < 0 (REQ_UNSET, REQ_NONE)
  reqbyteptr     set to the last literal character required, else < 0
  bcptr          points to current branch chain
  cd             contains pointers to tables etc.
  lengthptr      NULL during the real compile phase
                 points to length accumulator during pre-compile phase

Returns:         true on success
                 false, with *errorcodeptr set non-zero on error
*/

static bool
compile_branch(int *optionsptr, uschar **codeptr, const uschar **ptrptr,
  int *errorcodeptr, int *firstbyteptr, int *reqbyteptr, branch_chain *bcptr,
  compile_data *cd, int *lengthptr)
{
int repeat_type, op_type;
int repeat_min = 0, repeat_max = 0;      /* To please picky compilers */
int bravalue = 0;
int greedy_default, greedy_non_default;
int firstbyte, reqbyte;
int zeroreqbyte, zerofirstbyte;
int req_caseopt, reqvary, tempreqvary;
int options = *optionsptr;
int after_manual_callout = 0;
int length_prevgroup = 0;
register int c;
register uschar *code = *codeptr;
uschar *last_code = code;
uschar *orig_code = code;
uschar *tempcode;
bool inescq = false;
bool groupsetfirstbyte = false;
const uschar *ptr = *ptrptr;
const uschar *tempptr;
uschar *previous = NULL;
uschar *previous_callout = NULL;
uschar *save_hwm = NULL;
uschar classbits[32];

#ifdef SUPPORT_UTF8
bool class_utf8;
bool utf8 = (options & PCRE_UTF8) != 0;
uschar *class_utf8data;
uschar utf8_char[6];
#else
bool utf8 = false;
uschar *utf8_char = NULL;
#endif

/* Set up the default and non-default settings for greediness */

greedy_default = ((options & PCRE_UNGREEDY) != 0);
greedy_non_default = greedy_default ^ 1;

/* Initialize no first byte, no required byte. REQ_UNSET means "no char
matching encountered yet". It gets changed to REQ_NONE if we hit something that
matches a non-fixed char first char; reqbyte just remains unset if we never
find one.

When we hit a repeat whose minimum is zero, we may have to adjust these values
to take the zero repeat into account. This is implemented by setting them to
zerofirstbyte and zeroreqbyte when such a repeat is encountered. The individual
item types that can be repeated set these backoff variables appropriately. */

firstbyte = reqbyte = zerofirstbyte = zeroreqbyte = REQ_UNSET;

/* The variable req_caseopt contains either the REQ_CASELESS value or zero,
according to the current setting of the caseless flag. REQ_CASELESS is a bit
value > 255. It is added into the firstbyte or reqbyte variables to record the
case status of the value. This is used only for ASCII characters. */

req_caseopt = ((options & PCRE_CASELESS) != 0)? REQ_CASELESS : 0;

/* Switch on next character until the end of the branch */

for (;; ptr++)
  {
  bool negate_class;
  bool possessive_quantifier;
  bool is_quantifier;
  bool is_recurse;
  int class_charcount;
  int class_lastchar;
  int newoptions;
  int recno;
  int skipbytes;
  int subreqbyte;
  int subfirstbyte;
  int terminator;
  int mclength;
  uschar mcbuffer[8];

  /* Get next byte in the pattern */

  c = *ptr;

  /* If we are in the pre-compile phase, accumulate the length used for the
  previous cycle of this loop. */

  if (lengthptr != NULL)
    {
    if (code > cd->start_workspace + COMPILE_WORK_SIZE) /* Check for overrun */
      {
      *errorcodeptr = ERR52;
      goto FAILED;
      }

    /* There is at least one situation where code goes backwards: this is the
    case of a zero quantifier after a class (e.g. [ab]{0}). At compile time,
    the class is simply eliminated. However, it is created first, so we have to
    allow memory for it. Therefore, don't ever reduce the length at this point.
    */

    if (code < last_code) code = last_code;
    *lengthptr += code - last_code;
    DPRINTF(("length=%d added %d c=%c\n", *lengthptr, code - last_code, c));

    /* If "previous" is set and it is not at the start of the work space, move
    it back to there, in order to avoid filling up the work space. Otherwise,
    if "previous" is NULL, reset the current code pointer to the start. */

    if (previous != NULL)
      {
      if (previous > orig_code)
        {
        memmove(orig_code, previous, code - previous);
        code -= previous - orig_code;
        previous = orig_code;
        }
      }
    else code = orig_code;

    /* Remember where this code item starts so we can pick up the length
    next time round. */

    last_code = code;
    }

  /* In the real compile phase, just check the workspace used by the forward
  reference list. */

  else if (cd->hwm > cd->start_workspace + COMPILE_WORK_SIZE)
    {
    *errorcodeptr = ERR52;
    goto FAILED;
    }

  /* If in \Q...\E, check for the end; if not, we have a literal */

  if (inescq && c != 0)
    {
    if (c == '\\' && ptr[1] == 'E')
      {
      inescq = false;
      ptr++;
      continue;
      }
    else
      {
      if (previous_callout != NULL)
        {
        if (lengthptr == NULL)  /* Don't attempt in pre-compile phase */
          complete_callout(previous_callout, ptr, cd);
        previous_callout = NULL;
        }
      if ((options & PCRE_AUTO_CALLOUT) != 0)
        {
        previous_callout = code;
        code = auto_callout(code, ptr, cd);
        }
      goto NORMAL_CHAR;
      }
    }

  /* Fill in length of a previous callout, except when the next thing is
  a quantifier. */

  is_quantifier = c == '*' || c == '+' || c == '?' ||
    (c == '{' && is_counted_repeat(ptr+1));

  if (!is_quantifier && previous_callout != NULL &&
       after_manual_callout-- <= 0)
    {
    if (lengthptr == NULL)      /* Don't attempt in pre-compile phase */
      complete_callout(previous_callout, ptr, cd);
    previous_callout = NULL;
    }

  /* In extended mode, skip white space and comments */

  if ((options & PCRE_EXTENDED) != 0)
    {
    if ((cd->ctypes[c] & ctype_space) != 0) continue;
    if (c == '#')
      {
      while (*(++ptr) != 0)
        {
        if (IS_NEWLINE(ptr)) { ptr += cd->nllen - 1; break; }
        }
      if (*ptr != 0) continue;

      /* Else fall through to handle end of string */
      c = 0;
      }
    }

  /* No auto callout for quantifiers. */

  if ((options & PCRE_AUTO_CALLOUT) != 0 && !is_quantifier)
    {
    previous_callout = code;
    code = auto_callout(code, ptr, cd);
    }

  switch(c)
    {
    /* ===================================================================*/
    case 0:                        /* The branch terminates at string end */
    case '|':                      /* or | or ) */
    case ')':
    *firstbyteptr = firstbyte;
    *reqbyteptr = reqbyte;
    *codeptr = code;
    *ptrptr = ptr;
    if (lengthptr != NULL)
      {
      *lengthptr += code - last_code;   /* To include callout length */
      DPRINTF((">> end branch\n"));
      }
    return true;


    /* ===================================================================*/
    /* Handle single-character metacharacters. In multiline mode, ^ disables
    the setting of any following char as a first character. */

    case '^':
    if ((options & PCRE_MULTILINE) != 0)
      {
      if (firstbyte == REQ_UNSET) firstbyte = REQ_NONE;
      }
    previous = NULL;
    *code++ = OP_CIRC;
    break;

    case '$':
    previous = NULL;
    *code++ = OP_DOLL;
    break;

    /* There can never be a first char if '.' is first, whatever happens about
    repeats. The value of reqbyte doesn't change either. */

    case '.':
    if (firstbyte == REQ_UNSET) firstbyte = REQ_NONE;
    zerofirstbyte = firstbyte;
    zeroreqbyte = reqbyte;
    previous = code;
    *code++ = OP_ANY;
    break;


    /* ===================================================================*/
    /* Character classes. If the included characters are all < 256, we build a
    32-byte bitmap of the permitted characters, except in the special case
    where there is only one such character. For negated classes, we build the
    map as usual, then invert it at the end. However, we use a different opcode
    so that data characters > 255 can be handled correctly.

    If the class contains characters outside the 0-255 range, a different
    opcode is compiled. It may optionally have a bit map for characters < 256,
    but those above are are explicitly listed afterwards. A flag byte tells
    whether the bitmap is present, and whether this is a negated class or not.
    */

    case '[':
    previous = code;

    /* PCRE supports POSIX class stuff inside a class. Perl gives an error if
    they are encountered at the top level, so we'll do that too. */

    if ((ptr[1] == ':' || ptr[1] == '.' || ptr[1] == '=') &&
        check_posix_syntax(ptr, &tempptr, cd))
      {
      *errorcodeptr = (ptr[1] == ':')? ERR13 : ERR31;
      goto FAILED;
      }

    /* If the first character is '^', set the negation flag and skip it. */

    if ((c = *(++ptr)) == '^')
      {
      negate_class = true;
      c = *(++ptr);
      }
    else
      {
      negate_class = false;
      }

    /* Keep a count of chars with values < 256 so that we can optimize the case
    of just a single character (as long as it's < 256). However, For higher
    valued UTF-8 characters, we don't yet do any optimization. */

    class_charcount = 0;
    class_lastchar = -1;

    /* Initialize the 32-char bit map to all zeros. We build the map in a
    temporary bit of memory, in case the class contains only 1 character (less
    than 256), because in that case the compiled code doesn't use the bit map.
    */

    memset(classbits, 0, 32 * sizeof(uschar));

#ifdef SUPPORT_UTF8
    class_utf8 = false;                       /* No chars >= 256 */
    class_utf8data = code + LINK_SIZE + 2;    /* For UTF-8 items */
#endif

    /* Process characters until ] is reached. By writing this as a "do" it
    means that an initial ] is taken as a data character. At the start of the
    loop, c contains the first byte of the character. */

    if (c != 0) do
      {
      const uschar *oldptr;

#ifdef SUPPORT_UTF8
      if (utf8 && c > 127)
        {                           /* Braces are required because the */
        GETCHARLEN(c, ptr, ptr);    /* macro generates multiple statements */
        }
#endif

      /* Inside \Q...\E everything is literal except \E */

      if (inescq)
        {
        if (c == '\\' && ptr[1] == 'E')     /* If we are at \E */
          {
          inescq = false;                   /* Reset literal state */
          ptr++;                            /* Skip the 'E' */
          continue;                         /* Carry on with next */
          }
        goto CHECK_RANGE;                   /* Could be range if \E follows */
        }

      /* Handle POSIX class names. Perl allows a negation extension of the
      form [:^name:]. A square bracket that doesn't match the syntax is
      treated as a literal. We also recognize the POSIX constructions
      [.ch.] and [=ch=] ("collating elements") and fault them, as Perl
      5.6 and 5.8 do. */

      if (c == '[' &&
          (ptr[1] == ':' || ptr[1] == '.' || ptr[1] == '=') &&
          check_posix_syntax(ptr, &tempptr, cd))
        {
        bool local_negate = false;
        int posix_class, taboffset, tabopt;
        register const uschar *cbits = cd->cbits;
        uschar pbits[32];

        if (ptr[1] != ':')
          {
          *errorcodeptr = ERR31;
          goto FAILED;
          }

        ptr += 2;
        if (*ptr == '^')
          {
          local_negate = true;
          ptr++;
          }

        posix_class = check_posix_name(ptr, tempptr - ptr);
        if (posix_class < 0)
          {
          *errorcodeptr = ERR30;
          goto FAILED;
          }

        /* If matching is caseless, upper and lower are converted to
        alpha. This relies on the fact that the class table starts with
        alpha, lower, upper as the first 3 entries. */

        if ((options & PCRE_CASELESS) != 0 && posix_class <= 2)
          posix_class = 0;

        /* We build the bit map for the POSIX class in a chunk of local store
        because we may be adding and subtracting from it, and we don't want to
        subtract bits that may be in the main map already. At the end we or the
        result into the bit map that is being built. */

        posix_class *= 3;

        /* Copy in the first table (always present) */

        memcpy(pbits, cbits + posix_class_maps[posix_class],
          32 * sizeof(uschar));

        /* If there is a second table, add or remove it as required. */

        taboffset = posix_class_maps[posix_class + 1];
        tabopt = posix_class_maps[posix_class + 2];

        if (taboffset >= 0)
          {
          if (tabopt >= 0)
            for (c = 0; c < 32; c++) pbits[c] |= cbits[c + taboffset];
          else
            for (c = 0; c < 32; c++) pbits[c] &= ~cbits[c + taboffset];
          }

        /* Not see if we need to remove any special characters. An option
        value of 1 removes vertical space and 2 removes underscore. */

        if (tabopt < 0) tabopt = -tabopt;
        if (tabopt == 1) pbits[1] &= ~0x3c;
          else if (tabopt == 2) pbits[11] &= 0x7f;

        /* Add the POSIX table or its complement into the main table that is
        being built and we are done. */

        if (local_negate)
          for (c = 0; c < 32; c++) classbits[c] |= ~pbits[c];
        else
          for (c = 0; c < 32; c++) classbits[c] |= pbits[c];

        ptr = tempptr + 1;
        class_charcount = 10;  /* Set > 1; assumes more than 1 per class */
        continue;    /* End of POSIX syntax handling */
        }

      /* Backslash may introduce a single character, or it may introduce one
      of the specials, which just set a flag. The sequence \b is a special
      case. Inside a class (and only there) it is treated as backspace.
      Elsewhere it marks a word boundary. Other escapes have preset maps ready
      to or into the one we are building. We assume they have more than one
      character in them, so set class_charcount bigger than one. */

      if (c == '\\')
        {
        c = check_escape(&ptr, errorcodeptr, cd->bracount, options, true);
        if (*errorcodeptr != 0) goto FAILED;

        if (-c == ESC_b) c = '\b';       /* \b is backslash in a class */
        else if (-c == ESC_X) c = 'X';   /* \X is literal X in a class */
        else if (-c == ESC_R) c = 'R';   /* \R is literal R in a class */
        else if (-c == ESC_Q)            /* Handle start of quoted string */
          {
          if (ptr[1] == '\\' && ptr[2] == 'E')
            {
            ptr += 2; /* avoid empty string */
            }
          else inescq = true;
          continue;
          }

        if (c < 0)
          {
          register const uschar *cbits = cd->cbits;
          class_charcount += 2;     /* Greater than 1 is what matters */

          /* Save time by not doing this in the pre-compile phase. */

          if (lengthptr == NULL) switch (-c)
            {
            case ESC_d:
            for (c = 0; c < 32; c++) classbits[c] |= cbits[c+cbit_digit];
            continue;

            case ESC_D:
            for (c = 0; c < 32; c++) classbits[c] |= ~cbits[c+cbit_digit];
            continue;

            case ESC_w:
            for (c = 0; c < 32; c++) classbits[c] |= cbits[c+cbit_word];
            continue;

            case ESC_W:
            for (c = 0; c < 32; c++) classbits[c] |= ~cbits[c+cbit_word];
            continue;

            case ESC_s:
            for (c = 0; c < 32; c++) classbits[c] |= cbits[c+cbit_space];
            classbits[1] &= ~0x08;   /* Perl 5.004 onwards omits VT from \s */
            continue;

            case ESC_S:
            for (c = 0; c < 32; c++) classbits[c] |= ~cbits[c+cbit_space];
            classbits[1] |= 0x08;    /* Perl 5.004 onwards omits VT from \s */
            continue;

            case ESC_E: /* Perl ignores an orphan \E */
            continue;

            default:    /* Not recognized; fall through */
            break;      /* Need "default" setting to stop compiler warning. */
            }

          /* In the pre-compile phase, just do the recognition. */

          else if (c == -ESC_d || c == -ESC_D || c == -ESC_w ||
                   c == -ESC_W || c == -ESC_s || c == -ESC_S) continue;

          /* We need to deal with \P and \p in both phases. */

#ifdef SUPPORT_UCP
          if (-c == ESC_p || -c == ESC_P)
            {
            bool negated;
            int pdata;
            int ptype = get_ucp(&ptr, &negated, &pdata, errorcodeptr);
            if (ptype < 0) goto FAILED;
            class_utf8 = true;
            *class_utf8data++ = ((-c == ESC_p) != negated)?
              XCL_PROP : XCL_NOTPROP;
            *class_utf8data++ = ptype;
            *class_utf8data++ = pdata;
            class_charcount -= 2;   /* Not a < 256 character */
            continue;
            }
#endif
          /* Unrecognized escapes are faulted if PCRE is running in its
          strict mode. By default, for compatibility with Perl, they are
          treated as literals. */

          if ((options & PCRE_EXTRA) != 0)
            {
            *errorcodeptr = ERR7;
            goto FAILED;
            }

          class_charcount -= 2;  /* Undo the default count from above */
          c = *ptr;              /* Get the final character and fall through */
          }

        /* Fall through if we have a single character (c >= 0). This may be
        greater than 256 in UTF-8 mode. */

        }   /* End of backslash handling */

      /* A single character may be followed by '-' to form a range. However,
      Perl does not permit ']' to be the end of the range. A '-' character
      at the end is treated as a literal. Perl ignores orphaned \E sequences
      entirely. The code for handling \Q and \E is messy. */

      CHECK_RANGE:
      while (ptr[1] == '\\' && ptr[2] == 'E')
        {
        inescq = false;
        ptr += 2;
        }

      oldptr = ptr;

      if (!inescq && ptr[1] == '-')
        {
        int d;
        ptr += 2;
        while (*ptr == '\\' && ptr[1] == 'E') ptr += 2;

        /* If we hit \Q (not followed by \E) at this point, go into escaped
        mode. */

        while (*ptr == '\\' && ptr[1] == 'Q')
          {
          ptr += 2;
          if (*ptr == '\\' && ptr[1] == 'E') { ptr += 2; continue; }
          inescq = true;
          break;
          }

        if (*ptr == 0 || (!inescq && *ptr == ']'))
          {
          ptr = oldptr;
          goto LONE_SINGLE_CHARACTER;
          }

#ifdef SUPPORT_UTF8
        if (utf8)
          {                           /* Braces are required because the */
          GETCHARLEN(d, ptr, ptr);    /* macro generates multiple statements */
          }
        else
#endif
        d = *ptr;  /* Not UTF-8 mode */

        /* The second part of a range can be a single-character escape, but
        not any of the other escapes. Perl 5.6 treats a hyphen as a literal
        in such circumstances. */

        if (!inescq && d == '\\')
          {
          d = check_escape(&ptr, errorcodeptr, cd->bracount, options, true);
          if (*errorcodeptr != 0) goto FAILED;

          /* \b is backslash; \X is literal X; \R is literal R; any other
          special means the '-' was literal */

          if (d < 0)
            {
            if (d == -ESC_b) d = '\b';
            else if (d == -ESC_X) d = 'X';
            else if (d == -ESC_R) d = 'R'; else
              {
              ptr = oldptr;
              goto LONE_SINGLE_CHARACTER;  /* A few lines below */
              }
            }
          }

        /* Check that the two values are in the correct order. Optimize
        one-character ranges */

        if (d < c)
          {
          *errorcodeptr = ERR8;
          goto FAILED;
          }

        if (d == c) goto LONE_SINGLE_CHARACTER;  /* A few lines below */

        /* In UTF-8 mode, if the upper limit is > 255, or > 127 for caseless
        matching, we have to use an XCLASS with extra data items. Caseless
        matching for characters > 127 is available only if UCP support is
        available. */

#ifdef SUPPORT_UTF8
        if (utf8 && (d > 255 || ((options & PCRE_CASELESS) != 0 && d > 127)))
          {
          class_utf8 = true;

          /* With UCP support, we can find the other case equivalents of
          the relevant characters. There may be several ranges. Optimize how
          they fit with the basic range. */

#ifdef SUPPORT_UCP
          if ((options & PCRE_CASELESS) != 0)
            {
            unsigned int occ, ocd;
            unsigned int cc = c;
            unsigned int origd = d;
            while (get_othercase_range(&cc, origd, &occ, &ocd))
              {
              if (occ >= c && ocd <= d) continue;  /* Skip embedded ranges */

              if (occ < c  && ocd >= c - 1)        /* Extend the basic range */
                {                                  /* if there is overlap,   */
                c = occ;                           /* noting that if occ < c */
                continue;                          /* we can't have ocd > d  */
                }                                  /* because a subrange is  */
              if (ocd > d && occ <= d + 1)         /* always shorter than    */
                {                                  /* the basic range.       */
                d = ocd;
                continue;
                }

              if (occ == ocd)
                {
                *class_utf8data++ = XCL_SINGLE;
                }
              else
                {
                *class_utf8data++ = XCL_RANGE;
                class_utf8data += _pcre_ord2utf8(occ, class_utf8data);
                }
              class_utf8data += _pcre_ord2utf8(ocd, class_utf8data);
              }
            }
#endif  /* SUPPORT_UCP */

          /* Now record the original range, possibly modified for UCP caseless
          overlapping ranges. */

          *class_utf8data++ = XCL_RANGE;
          class_utf8data += _pcre_ord2utf8(c, class_utf8data);
          class_utf8data += _pcre_ord2utf8(d, class_utf8data);

          /* With UCP support, we are done. Without UCP support, there is no
          caseless matching for UTF-8 characters > 127; we can use the bit map
          for the smaller ones. */

#ifdef SUPPORT_UCP
          continue;    /* With next character in the class */
#else
          if ((options & PCRE_CASELESS) == 0 || c > 127) continue;

          /* Adjust upper limit and fall through to set up the map */

          d = 127;

#endif  /* SUPPORT_UCP */
          }
#endif  /* SUPPORT_UTF8 */

        /* We use the bit map for all cases when not in UTF-8 mode; else
        ranges that lie entirely within 0-127 when there is UCP support; else
        for partial ranges without UCP support. */

        class_charcount += d - c + 1;
        class_lastchar = d;

        /* We can save a bit of time by skipping this in the pre-compile. */

        if (lengthptr == NULL) for (; c <= d; c++)
          {
          classbits[c/8] |= (1 << (c&7));
          if ((options & PCRE_CASELESS) != 0)
            {
            int uc = cd->fcc[c];           /* flip case */
            classbits[uc/8] |= (1 << (uc&7));
            }
          }

        continue;   /* Go get the next char in the class */
        }

      /* Handle a lone single character - we can get here for a normal
      non-escape char, or after \ that introduces a single character or for an
      apparent range that isn't. */

      LONE_SINGLE_CHARACTER:

      /* Handle a character that cannot go in the bit map */

#ifdef SUPPORT_UTF8
      if (utf8 && (c > 255 || ((options & PCRE_CASELESS) != 0 && c > 127)))
        {
        class_utf8 = true;
        *class_utf8data++ = XCL_SINGLE;
        class_utf8data += _pcre_ord2utf8(c, class_utf8data);

#ifdef SUPPORT_UCP
        if ((options & PCRE_CASELESS) != 0)
          {
          unsigned int othercase;
          if ((othercase = _pcre_ucp_othercase(c)) != NOTACHAR)
            {
            *class_utf8data++ = XCL_SINGLE;
            class_utf8data += _pcre_ord2utf8(othercase, class_utf8data);
            }
          }
#endif  /* SUPPORT_UCP */

        }
      else
#endif  /* SUPPORT_UTF8 */

      /* Handle a single-byte character */
        {
        classbits[c/8] |= (1 << (c&7));
        if ((options & PCRE_CASELESS) != 0)
          {
          c = cd->fcc[c];   /* flip case */
          classbits[c/8] |= (1 << (c&7));
          }
        class_charcount++;
        class_lastchar = c;
        }
      }

    /* Loop until ']' reached. This "while" is the end of the "do" above. */

    while ((c = *(++ptr)) != 0 && (c != ']' || inescq));

    if (c == 0)                          /* Missing terminating ']' */
      {
      *errorcodeptr = ERR6;
      goto FAILED;
      }

    /* If class_charcount is 1, we saw precisely one character whose value is
    less than 256. In non-UTF-8 mode we can always optimize. In UTF-8 mode, we
    can optimize the negative case only if there were no characters >= 128
    because OP_NOT and the related opcodes like OP_NOTSTAR operate on
    single-bytes only. This is an historical hangover. Maybe one day we can
    tidy these opcodes to handle multi-byte characters.

    The optimization throws away the bit map. We turn the item into a
    1-character OP_CHAR[NC] if it's positive, or OP_NOT if it's negative. Note
    that OP_NOT does not support multibyte characters. In the positive case, it
    can cause firstbyte to be set. Otherwise, there can be no first char if
    this item is first, whatever repeat count may follow. In the case of
    reqbyte, save the previous value for reinstating. */

#ifdef SUPPORT_UTF8
    if (class_charcount == 1 &&
          (!utf8 ||
          (!class_utf8 && (!negate_class || class_lastchar < 128))))

#else
    if (class_charcount == 1)
#endif
      {
      zeroreqbyte = reqbyte;

      /* The OP_NOT opcode works on one-byte characters only. */

      if (negate_class)
        {
        if (firstbyte == REQ_UNSET) firstbyte = REQ_NONE;
        zerofirstbyte = firstbyte;
        *code++ = OP_NOT;
        *code++ = static_cast<uschar>(class_lastchar);
        break;
        }

      /* For a single, positive character, get the value into mcbuffer, and
      then we can handle this with the normal one-character code. */

#ifdef SUPPORT_UTF8
      if (utf8 && class_lastchar > 127)
        mclength = _pcre_ord2utf8(class_lastchar, mcbuffer);
      else
#endif
        {
        mcbuffer[0] = class_lastchar;
        mclength = 1;
        }
      goto ONE_CHAR;
      }       /* End of 1-char optimization */

    /* The general case - not the one-char optimization. If this is the first
    thing in the branch, there can be no first char setting, whatever the
    repeat count. Any reqbyte setting must remain unchanged after any kind of
    repeat. */

    if (firstbyte == REQ_UNSET) firstbyte = REQ_NONE;
    zerofirstbyte = firstbyte;
    zeroreqbyte = reqbyte;

    /* If there are characters with values > 255, we have to compile an
    extended class, with its own opcode. If there are no characters < 256,
    we can omit the bitmap in the actual compiled code. */

#ifdef SUPPORT_UTF8
    if (class_utf8)
      {
      *class_utf8data++ = XCL_END;    /* Marks the end of extra data */
      *code++ = OP_XCLASS;
      code += LINK_SIZE;
      *code = negate_class? XCL_NOT : 0;

      /* If the map is required, move up the extra data to make room for it;
      otherwise just move the code pointer to the end of the extra data. */

      if (class_charcount > 0)
        {
        *code++ |= XCL_MAP;
        memmove(code + 32, code, class_utf8data - code);
        memcpy(code, classbits, 32);
        code = class_utf8data + 32;
        }
      else code = class_utf8data;

      /* Now fill in the complete length of the item */

      PUT(previous, 1, code - previous);
      break;   /* End of class handling */
      }
#endif

    /* If there are no characters > 255, negate the 32-byte map if necessary,
    and copy it into the code vector. If this is the first thing in the branch,
    there can be no first char setting, whatever the repeat count. Any reqbyte
    setting must remain unchanged after any kind of repeat. */

    if (negate_class)
      {
      *code++ = OP_NCLASS;
      if (lengthptr == NULL)    /* Save time in the pre-compile phase */
        for (c = 0; c < 32; c++) code[c] = static_cast<uschar>(~classbits[c]);
      }
    else
      {
      *code++ = OP_CLASS;
      memcpy(code, classbits, 32);
      }
    code += 32;
    break;


    /* ===================================================================*/
    /* Various kinds of repeat; '{' is not necessarily a quantifier, but this
    has been tested above. */

    case '{':
    if (!is_quantifier) goto NORMAL_CHAR;
    ptr = read_repeat_counts(ptr+1, &repeat_min, &repeat_max, errorcodeptr);
    if (*errorcodeptr != 0) goto FAILED;
    goto REPEAT;

    case '*':
    repeat_min = 0;
    repeat_max = -1;
    goto REPEAT;

    case '+':
    repeat_min = 1;
    repeat_max = -1;
    goto REPEAT;

    case '?':
    repeat_min = 0;
    repeat_max = 1;

    REPEAT:
    if (previous == NULL)
      {
      *errorcodeptr = ERR9;
      goto FAILED;
      }

    if (repeat_min == 0)
      {
      firstbyte = zerofirstbyte;    /* Adjust for zero repeat */
      reqbyte = zeroreqbyte;        /* Ditto */
      }

    /* Remember whether this is a variable length repeat */

    reqvary = (repeat_min == repeat_max)? 0 : REQ_VARY;

    op_type = 0;                    /* Default single-char op codes */
    possessive_quantifier = false;  /* Default not possessive quantifier */

    /* Save start of previous item, in case we have to move it up to make space
    for an inserted OP_ONCE for the additional '+' extension. */

    tempcode = previous;

    /* If the next character is '+', we have a possessive quantifier. This
    implies greediness, whatever the setting of the PCRE_UNGREEDY option.
    If the next character is '?' this is a minimizing repeat, by default,
    but if PCRE_UNGREEDY is set, it works the other way round. We change the
    repeat type to the non-default. */

    if (ptr[1] == '+')
      {
      repeat_type = 0;                  /* Force greedy */
      possessive_quantifier = true;
      ptr++;
      }
    else if (ptr[1] == '?')
      {
      repeat_type = greedy_non_default;
      ptr++;
      }
    else repeat_type = greedy_default;

    /* If previous was a character match, abolish the item and generate a
    repeat item instead. If a char item has a minumum of more than one, ensure
    that it is set in reqbyte - it might not be if a sequence such as x{3} is
    the first thing in a branch because the x will have gone into firstbyte
    instead.  */

    if (*previous == OP_CHAR || *previous == OP_CHARNC)
      {
      /* Deal with UTF-8 characters that take up more than one byte. It's
      easier to write this out separately than try to macrify it. Use c to
      hold the length of the character in bytes, plus 0x80 to flag that it's a
      length rather than a small character. */

#ifdef SUPPORT_UTF8
      if (utf8 && (code[-1] & 0x80) != 0)
        {
        uschar *lastchar = code - 1;
        while((*lastchar & 0xc0) == 0x80) lastchar--;
        c = code - lastchar;            /* Length of UTF-8 character */
        memcpy(utf8_char, lastchar, c); /* Save the char */
        c |= 0x80;                      /* Flag c as a length */
        }
      else
#endif

      /* Handle the case of a single byte - either with no UTF8 support, or
      with UTF-8 disabled, or for a UTF-8 character < 128. */

        {
        c = code[-1];
        if (repeat_min > 1) reqbyte = c | req_caseopt | cd->req_varyopt;
        }

      /* If the repetition is unlimited, it pays to see if the next thing on
      the line is something that cannot possibly match this character. If so,
      automatically possessifying this item gains some performance in the case
      where the match fails. */

      if (!possessive_quantifier &&
          repeat_max < 0 &&
          check_auto_possessive(*previous, c, utf8, utf8_char, ptr + 1,
            options, cd))
        {
        repeat_type = 0;    /* Force greedy */
        possessive_quantifier = true;
        }

      goto OUTPUT_SINGLE_REPEAT;   /* Code shared with single character types */
      }

    /* If previous was a single negated character ([^a] or similar), we use
    one of the special opcodes, replacing it. The code is shared with single-
    character repeats by setting opt_type to add a suitable offset into
    repeat_type. We can also test for auto-possessification. OP_NOT is
    currently used only for single-byte chars. */

    else if (*previous == OP_NOT)
      {
      op_type = OP_NOTSTAR - OP_STAR;  /* Use "not" opcodes */
      c = previous[1];
      if (!possessive_quantifier &&
          repeat_max < 0 &&
          check_auto_possessive(OP_NOT, c, utf8, NULL, ptr + 1, options, cd))
        {
        repeat_type = 0;    /* Force greedy */
        possessive_quantifier = true;
        }
      goto OUTPUT_SINGLE_REPEAT;
      }

    /* If previous was a character type match (\d or similar), abolish it and
    create a suitable repeat item. The code is shared with single-character
    repeats by setting op_type to add a suitable offset into repeat_type. Note
    the the Unicode property types will be present only when SUPPORT_UCP is
    defined, but we don't wrap the little bits of code here because it just
    makes it horribly messy. */

    else if (*previous < OP_EODN)
      {
      uschar *oldcode;
      int prop_type, prop_value;
      op_type = OP_TYPESTAR - OP_STAR;  /* Use type opcodes */
      c = *previous;

      if (!possessive_quantifier &&
          repeat_max < 0 &&
          check_auto_possessive(c, 0, utf8, NULL, ptr + 1, options, cd))
        {
        repeat_type = 0;    /* Force greedy */
        possessive_quantifier = true;
        }

      OUTPUT_SINGLE_REPEAT:
      if (*previous == OP_PROP || *previous == OP_NOTPROP)
        {
        prop_type = previous[1];
        prop_value = previous[2];
        }
      else prop_type = prop_value = -1;

      oldcode = code;
      code = previous;                  /* Usually overwrite previous item */

      /* If the maximum is zero then the minimum must also be zero; Perl allows
      this case, so we do too - by simply omitting the item altogether. */

      if (repeat_max == 0) goto END_REPEAT;

      /* All real repeats make it impossible to handle partial matching (maybe
      one day we will be able to remove this restriction). */

      if (repeat_max != 1) cd->nopartial = true;

      /* Combine the op_type with the repeat_type */

      repeat_type += op_type;

      /* A minimum of zero is handled either as the special case * or ?, or as
      an UPTO, with the maximum given. */

      if (repeat_min == 0)
        {
        if (repeat_max == -1) *code++ = static_cast<uschar>(OP_STAR + repeat_type);
        else if (repeat_max == 1) *code++ = static_cast<uschar>(OP_QUERY + repeat_type);
        else
          {
          *code++ = static_cast<uschar>(OP_UPTO + repeat_type);
          PUT2INC(code, 0, repeat_max);
          }
        }

      /* A repeat minimum of 1 is optimized into some special cases. If the
      maximum is unlimited, we use OP_PLUS. Otherwise, the original item is
      left in place and, if the maximum is greater than 1, we use OP_UPTO with
      one less than the maximum. */

      else if (repeat_min == 1)
        {
        if (repeat_max == -1)
          *code++ = static_cast<uschar>(OP_PLUS + repeat_type);
        else
          {
          code = oldcode;                 /* leave previous item in place */
          if (repeat_max == 1) goto END_REPEAT;
          *code++ = static_cast<uschar>(OP_UPTO + repeat_type);
          PUT2INC(code, 0, repeat_max - 1);
          }
        }

      /* The case {n,n} is just an EXACT, while the general case {n,m} is
      handled as an EXACT followed by an UPTO. */

      else
        {
        *code++ = static_cast<uschar>(OP_EXACT + op_type);  /* NB EXACT doesn't have repeat_type */
        PUT2INC(code, 0, repeat_min);

        /* If the maximum is unlimited, insert an OP_STAR. Before doing so,
        we have to insert the character for the previous code. For a repeated
        Unicode property match, there are two extra bytes that define the
        required property. In UTF-8 mode, long characters have their length in
        c, with the 0x80 bit as a flag. */

        if (repeat_max < 0)
          {
#ifdef SUPPORT_UTF8
          if (utf8 && c >= 128)
            {
            memcpy(code, utf8_char, c & 7);
            code += c & 7;
            }
          else
#endif
            {
            *code++ = static_cast<uschar>(c);
            if (prop_type >= 0)
              {
              *code++ = static_cast<uschar>(prop_type);
              *code++ = static_cast<uschar>(prop_value);
              }
            }
          *code++ = static_cast<uschar>(OP_STAR + repeat_type);
          }

        /* Else insert an UPTO if the max is greater than the min, again
        preceded by the character, for the previously inserted code. If the
        UPTO is just for 1 instance, we can use QUERY instead. */

        else if (repeat_max != repeat_min)
          {
#ifdef SUPPORT_UTF8
          if (utf8 && c >= 128)
            {
            memcpy(code, utf8_char, c & 7);
            code += c & 7;
            }
          else
#endif
          *code++ = c;
          if (prop_type >= 0)
            {
            *code++ = prop_type;
            *code++ = prop_value;
            }
          repeat_max -= repeat_min;

          if (repeat_max == 1)
            {
            *code++ = OP_QUERY + repeat_type;
            }
          else
            {
            *code++ = OP_UPTO + repeat_type;
            PUT2INC(code, 0, repeat_max);
            }
          }
        }

      /* The character or character type itself comes last in all cases. */

#ifdef SUPPORT_UTF8
      if (utf8 && c >= 128)
        {
        memcpy(code, utf8_char, c & 7);
        code += c & 7;
        }
      else
#endif
      *code++ = static_cast<uschar>(c);

      /* For a repeated Unicode property match, there are two extra bytes that
      define the required property. */

#ifdef SUPPORT_UCP
      if (prop_type >= 0)
        {
        *code++ = static_cast<uschar>(prop_type);
        *code++ = static_cast<uschar>(prop_value);
        }
#endif
      }

    /* If previous was a character class or a back reference, we put the repeat
    stuff after it, but just skip the item if the repeat was {0,0}. */

    else if (*previous == OP_CLASS ||
             *previous == OP_NCLASS ||
#ifdef SUPPORT_UTF8
             *previous == OP_XCLASS ||
#endif
             *previous == OP_REF)
      {
      if (repeat_max == 0)
        {
        code = previous;
        goto END_REPEAT;
        }

      /* All real repeats make it impossible to handle partial matching (maybe
      one day we will be able to remove this restriction). */

      if (repeat_max != 1) cd->nopartial = true;

      if (repeat_min == 0 && repeat_max == -1)
        *code++ = static_cast<uschar>(OP_CRSTAR + repeat_type);
      else if (repeat_min == 1 && repeat_max == -1)
        *code++ = static_cast<uschar>(OP_CRPLUS + repeat_type);
      else if (repeat_min == 0 && repeat_max == 1)
        *code++ = static_cast<uschar>(OP_CRQUERY + repeat_type);
      else
        {
        *code++ = static_cast<uschar>(OP_CRRANGE + repeat_type);
        PUT2INC(code, 0, repeat_min);
        if (repeat_max == -1) repeat_max = 0;  /* 2-byte encoding for max */
        PUT2INC(code, 0, repeat_max);
        }
      }

    /* If previous was a bracket group, we may have to replicate it in certain
    cases. */

    else if (*previous == OP_BRA  || *previous == OP_CBRA ||
             *previous == OP_ONCE || *previous == OP_COND)
      {
      register int i;
      int ketoffset = 0;
      int len = code - previous;
      uschar *bralink = NULL;

      /* Repeating a DEFINE group is pointless */

      if (*previous == OP_COND && previous[LINK_SIZE+1] == OP_DEF)
        {
        *errorcodeptr = ERR55;
        goto FAILED;
        }

      /* This is a paranoid check to stop integer overflow later on */

      if (len > MAX_DUPLENGTH)
        {
        *errorcodeptr = ERR50;
        goto FAILED;
        }

      /* If the maximum repeat count is unlimited, find the end of the bracket
      by scanning through from the start, and compute the offset back to it
      from the current code pointer. There may be an OP_OPT setting following
      the final KET, so we can't find the end just by going back from the code
      pointer. */

      if (repeat_max == -1)
        {
        register uschar *ket = previous;
        do ket += GET(ket, 1); while (*ket != OP_KET);
        ketoffset = code - ket;
        }

      /* The case of a zero minimum is special because of the need to stick
      OP_BRAZERO in front of it, and because the group appears once in the
      data, whereas in other cases it appears the minimum number of times. For
      this reason, it is simplest to treat this case separately, as otherwise
      the code gets far too messy. There are several special subcases when the
      minimum is zero. */

      if (repeat_min == 0)
        {
        /* If the maximum is also zero, we just omit the group from the output
        altogether. */

        if (repeat_max == 0)
          {
          code = previous;
          goto END_REPEAT;
          }

        /* If the maximum is 1 or unlimited, we just have to stick in the
        BRAZERO and do no more at this point. However, we do need to adjust
        any OP_RECURSE calls inside the group that refer to the group itself or
        any internal or forward referenced group, because the offset is from
        the start of the whole regex. Temporarily terminate the pattern while
        doing this. */

        if (repeat_max <= 1)
          {
          *code = OP_END;
          adjust_recurse(previous, 1, utf8, cd, save_hwm);
          memmove(previous+1, previous, len);
          code++;
          *previous++ = static_cast<uschar>(OP_BRAZERO + repeat_type);
          }

        /* If the maximum is greater than 1 and limited, we have to replicate
        in a nested fashion, sticking OP_BRAZERO before each set of brackets.
        The first one has to be handled carefully because it's the original
        copy, which has to be moved up. The remainder can be handled by code
        that is common with the non-zero minimum case below. We have to
        adjust the value or repeat_max, since one less copy is required. Once
        again, we may have to adjust any OP_RECURSE calls inside the group. */

        else
          {
          int offset;
          *code = OP_END;
          adjust_recurse(previous, 2 + LINK_SIZE, utf8, cd, save_hwm);
          memmove(previous + 2 + LINK_SIZE, previous, len);
          code += 2 + LINK_SIZE;
          *previous++ = static_cast<uschar>(OP_BRAZERO + repeat_type);
          *previous++ = OP_BRA;

          /* We chain together the bracket offset fields that have to be
          filled in later when the ends of the brackets are reached. */

          offset = (bralink == NULL)? 0 : previous - bralink;
          bralink = previous;
          PUTINC(previous, 0, offset);
          }

        repeat_max--;
        }

      /* If the minimum is greater than zero, replicate the group as many
      times as necessary, and adjust the maximum to the number of subsequent
      copies that we need. If we set a first char from the group, and didn't
      set a required char, copy the latter from the former. If there are any
      forward reference subroutine calls in the group, there will be entries on
      the workspace list; replicate these with an appropriate increment. */

      else
        {
        if (repeat_min > 1)
          {
          /* In the pre-compile phase, we don't actually do the replication. We
          just adjust the length as if we had. */

          if (lengthptr != NULL)
            *lengthptr += (repeat_min - 1)*length_prevgroup;

          /* This is compiling for real */

          else
            {
            if (groupsetfirstbyte && reqbyte < 0) reqbyte = firstbyte;
            for (i = 1; i < repeat_min; i++)
              {
              uschar *hc;
              uschar *this_hwm = cd->hwm;
              memcpy(code, previous, len);
              for (hc = save_hwm; hc < this_hwm; hc += LINK_SIZE)
                {
                PUT(cd->hwm, 0, GET(hc, 0) + len);
                cd->hwm += LINK_SIZE;
                }
              save_hwm = this_hwm;
              code += len;
              }
            }
          }

        if (repeat_max > 0) repeat_max -= repeat_min;
        }

      /* This code is common to both the zero and non-zero minimum cases. If
      the maximum is limited, it replicates the group in a nested fashion,
      remembering the bracket starts on a stack. In the case of a zero minimum,
      the first one was set up above. In all cases the repeat_max now specifies
      the number of additional copies needed. Again, we must remember to
      replicate entries on the forward reference list. */

      if (repeat_max >= 0)
        {
        /* In the pre-compile phase, we don't actually do the replication. We
        just adjust the length as if we had. For each repetition we must add 1
        to the length for BRAZERO and for all but the last repetition we must
        add 2 + 2*LINKSIZE to allow for the nesting that occurs. */

        if (lengthptr != NULL && repeat_max > 0)
          *lengthptr += repeat_max * (length_prevgroup + 1 + 2 + 2*LINK_SIZE) -
            2 - 2*LINK_SIZE;  /* Last one doesn't nest */

        /* This is compiling for real */

        else for (i = repeat_max - 1; i >= 0; i--)
          {
          uschar *hc;
          uschar *this_hwm = cd->hwm;

          *code++ = OP_BRAZERO + repeat_type;

          /* All but the final copy start a new nesting, maintaining the
          chain of brackets outstanding. */

          if (i != 0)
            {
            int offset;
            *code++ = OP_BRA;
            offset = (bralink == NULL)? 0 : code - bralink;
            bralink = code;
            PUTINC(code, 0, offset);
            }

          memcpy(code, previous, len);
          for (hc = save_hwm; hc < this_hwm; hc += LINK_SIZE)
            {
            PUT(cd->hwm, 0, GET(hc, 0) + len + ((i != 0)? 2+LINK_SIZE : 1));
            cd->hwm += LINK_SIZE;
            }
          save_hwm = this_hwm;
          code += len;
          }

        /* Now chain through the pending brackets, and fill in their length
        fields (which are holding the chain links pro tem). */

        while (bralink != NULL)
          {
          int oldlinkoffset;
          int offset = code - bralink + 1;
          uschar *bra = code - offset;
          oldlinkoffset = GET(bra, 1);
          bralink = (oldlinkoffset == 0)? NULL : bralink - oldlinkoffset;
          *code++ = OP_KET;
          PUTINC(code, 0, offset);
          PUT(bra, 1, offset);
          }
        }

      /* If the maximum is unlimited, set a repeater in the final copy. We
      can't just offset backwards from the current code point, because we
      don't know if there's been an options resetting after the ket. The
      correct offset was computed above.

      Then, when we are doing the actual compile phase, check to see whether
      this group is a non-atomic one that could match an empty string. If so,
      convert the initial operator to the S form (e.g. OP_BRA -> OP_SBRA) so
      that runtime checking can be done. [This check is also applied to
      atomic groups at runtime, but in a different way.] */

      else
        {
        uschar *ketcode = code - ketoffset;
        uschar *bracode = ketcode - GET(ketcode, 1);
        *ketcode = OP_KETRMAX + repeat_type;
        if (lengthptr == NULL && *bracode != OP_ONCE)
          {
          uschar *scode = bracode;
          do
            {
            if (could_be_empty_branch(scode, ketcode, utf8))
              {
              *bracode += OP_SBRA - OP_BRA;
              break;
              }
            scode += GET(scode, 1);
            }
          while (*scode == OP_ALT);
          }
        }
      }

    /* Else there's some kind of shambles */

    else
      {
      *errorcodeptr = ERR11;
      goto FAILED;
      }

    /* If the character following a repeat is '+', or if certain optimization
    tests above succeeded, possessive_quantifier is true. For some of the
    simpler opcodes, there is an special alternative opcode for this. For
    anything else, we wrap the entire repeated item inside OP_ONCE brackets.
    The '+' notation is just syntactic sugar, taken from Sun's Java package,
    but the special opcodes can optimize it a bit. The repeated item starts at
    tempcode, not at previous, which might be the first part of a string whose
    (former) last char we repeated.

    Possessifying an 'exact' quantifier has no effect, so we can ignore it. But
    an 'upto' may follow. We skip over an 'exact' item, and then test the
    length of what remains before proceeding. */

    if (possessive_quantifier)
      {
      int len;
      if (*tempcode == OP_EXACT || *tempcode == OP_TYPEEXACT ||
          *tempcode == OP_NOTEXACT)
        tempcode += _pcre_OP_lengths[*tempcode];
      len = code - tempcode;
      if (len > 0) switch (*tempcode)
        {
        case OP_STAR:  *tempcode = OP_POSSTAR; break;
        case OP_PLUS:  *tempcode = OP_POSPLUS; break;
        case OP_QUERY: *tempcode = OP_POSQUERY; break;
        case OP_UPTO:  *tempcode = OP_POSUPTO; break;

        case OP_TYPESTAR:  *tempcode = OP_TYPEPOSSTAR; break;
        case OP_TYPEPLUS:  *tempcode = OP_TYPEPOSPLUS; break;
        case OP_TYPEQUERY: *tempcode = OP_TYPEPOSQUERY; break;
        case OP_TYPEUPTO:  *tempcode = OP_TYPEPOSUPTO; break;

        case OP_NOTSTAR:  *tempcode = OP_NOTPOSSTAR; break;
        case OP_NOTPLUS:  *tempcode = OP_NOTPOSPLUS; break;
        case OP_NOTQUERY: *tempcode = OP_NOTPOSQUERY; break;
        case OP_NOTUPTO:  *tempcode = OP_NOTPOSUPTO; break;

        default:
        memmove(tempcode + 1+LINK_SIZE, tempcode, len);
        code += 1 + LINK_SIZE;
        len += 1 + LINK_SIZE;
        tempcode[0] = OP_ONCE;
        *code++ = OP_KET;
        PUTINC(code, 0, len);
        PUT(tempcode, 1, len);
        break;
        }
      }

    /* In all case we no longer have a previous item. We also set the
    "follows varying string" flag for subsequently encountered reqbytes if
    it isn't already set and we have just passed a varying length item. */

    END_REPEAT:
    previous = NULL;
    cd->req_varyopt |= reqvary;
    break;


    /* ===================================================================*/
    /* Start of nested parenthesized sub-expression, or comment or lookahead or
    lookbehind or option setting or condition or all the other extended
    parenthesis forms. First deal with the specials; all are introduced by ?,
    and the appearance of any of them means that this is not a capturing
    group. */

    case '(':
    newoptions = options;
    skipbytes = 0;
    bravalue = OP_CBRA;
    save_hwm = cd->hwm;

    if (*(++ptr) == '?')
      {
      int i, set, unset, namelen;
      int *optset;
      const uschar *name;
      uschar *slot;

      switch (*(++ptr))
        {
        case '#':                 /* Comment; skip to ket */
        ptr++;
        while (*ptr != 0 && *ptr != ')') ptr++;
        if (*ptr == 0)
          {
          *errorcodeptr = ERR18;
          goto FAILED;
          }
        continue;


        /* ------------------------------------------------------------ */
        case ':':                 /* Non-capturing bracket */
        bravalue = OP_BRA;
        ptr++;
        break;


        /* ------------------------------------------------------------ */
        case '(':
        bravalue = OP_COND;       /* Conditional group */

        /* A condition can be an assertion, a number (referring to a numbered
        group), a name (referring to a named group), or 'R', referring to
        recursion. R<digits> and R&name are also permitted for recursion tests.

        There are several syntaxes for testing a named group: (?(name)) is used
        by Python; Perl 5.10 onwards uses (?(<name>) or (?('name')).

        There are two unfortunate ambiguities, caused by history. (a) 'R' can
        be the recursive thing or the name 'R' (and similarly for 'R' followed
        by digits), and (b) a number could be a name that consists of digits.
        In both cases, we look for a name first; if not found, we try the other
        cases. */

        /* For conditions that are assertions, check the syntax, and then exit
        the switch. This will take control down to where bracketed groups,
        including assertions, are processed. */

        if (ptr[1] == '?' && (ptr[2] == '=' || ptr[2] == '!' || ptr[2] == '<'))
          break;

        /* Most other conditions use OP_CREF (a couple change to OP_RREF
        below), and all need to skip 3 bytes at the start of the group. */

        code[1+LINK_SIZE] = OP_CREF;
        skipbytes = 3;

        /* Check for a test for recursion in a named group. */

        if (ptr[1] == 'R' && ptr[2] == '&')
          {
          terminator = -1;
          ptr += 2;
          code[1+LINK_SIZE] = OP_RREF;    /* Change the type of test */
          }

        /* Check for a test for a named group's having been set, using the Perl
        syntax (?(<name>) or (?('name') */

        else if (ptr[1] == '<')
          {
          terminator = '>';
          ptr++;
          }
        else if (ptr[1] == '\'')
          {
          terminator = '\'';
          ptr++;
          }
        else terminator = 0;

        /* We now expect to read a name; any thing else is an error */

        if ((cd->ctypes[ptr[1]] & ctype_word) == 0)
          {
          ptr += 1;  /* To get the right offset */
          *errorcodeptr = ERR28;
          goto FAILED;
          }

        /* Read the name, but also get it as a number if it's all digits */

        recno = 0;
        name = ++ptr;
        while ((cd->ctypes[*ptr] & ctype_word) != 0)
          {
          if (recno >= 0)
            recno = ((digitab[*ptr] & ctype_digit) != 0)?
              recno * 10 + *ptr - '0' : -1;
          ptr++;
          }
        namelen = ptr - name;

        if ((terminator > 0 && *ptr++ != terminator) || *ptr++ != ')')
          {
          ptr--;      /* Error offset */
          *errorcodeptr = ERR26;
          goto FAILED;
          }

        /* Do no further checking in the pre-compile phase. */

        if (lengthptr != NULL) break;

        /* In the real compile we do the work of looking for the actual
        reference. */

        slot = cd->name_table;
        for (i = 0; i < cd->names_found; i++)
          {
          if (strncmp((char *)name, (char *)slot+2, namelen) == 0) break;
          slot += cd->name_entry_size;
          }

        /* Found a previous named subpattern */

        if (i < cd->names_found)
          {
          recno = GET2(slot, 0);
          PUT2(code, 2+LINK_SIZE, recno);
          }

        /* Search the pattern for a forward reference */

        else if ((i = find_parens(ptr, cd->bracount, name, namelen,
                        (options & PCRE_EXTENDED) != 0)) > 0)
          {
          PUT2(code, 2+LINK_SIZE, i);
          }

        /* If terminator == 0 it means that the name followed directly after
        the opening parenthesis [e.g. (?(abc)...] and in this case there are
        some further alternatives to try. For the cases where terminator != 0
        [things like (?(<name>... or (?('name')... or (?(R&name)... ] we have
        now checked all the possibilities, so give an error. */

        else if (terminator != 0)
          {
          *errorcodeptr = ERR15;
          goto FAILED;
          }

        /* Check for (?(R) for recursion. Allow digits after R to specify a
        specific group number. */

        else if (*name == 'R')
          {
          recno = 0;
          for (i = 1; i < namelen; i++)
            {
            if ((digitab[name[i]] & ctype_digit) == 0)
              {
              *errorcodeptr = ERR15;
              goto FAILED;
              }
            recno = recno * 10 + name[i] - '0';
            }
          if (recno == 0) recno = RREF_ANY;
          code[1+LINK_SIZE] = OP_RREF;      /* Change test type */
          PUT2(code, 2+LINK_SIZE, recno);
          }

        /* Similarly, check for the (?(DEFINE) "condition", which is always
        false. */

        else if (namelen == 6 && strncmp((char *)name, "DEFINE", 6) == 0)
          {
          code[1+LINK_SIZE] = OP_DEF;
          skipbytes = 1;
          }

        /* Check for the "name" actually being a subpattern number. */

        else if (recno > 0)
          {
          PUT2(code, 2+LINK_SIZE, recno);
          }

        /* Either an unidentified subpattern, or a reference to (?(0) */

        else
          {
          *errorcodeptr = (recno == 0)? ERR35: ERR15;
          goto FAILED;
          }
        break;


        /* ------------------------------------------------------------ */
        case '=':                 /* Positive lookahead */
        bravalue = OP_ASSERT;
        ptr++;
        break;


        /* ------------------------------------------------------------ */
        case '!':                 /* Negative lookahead */
        bravalue = OP_ASSERT_NOT;
        ptr++;
        break;


        /* ------------------------------------------------------------ */
        case '<':                 /* Lookbehind or named define */
        switch (ptr[1])
          {
          case '=':               /* Positive lookbehind */
          bravalue = OP_ASSERTBACK;
          ptr += 2;
          break;

          case '!':               /* Negative lookbehind */
          bravalue = OP_ASSERTBACK_NOT;
          ptr += 2;
          break;

          default:                /* Could be name define, else bad */
          if ((cd->ctypes[ptr[1]] & ctype_word) != 0) goto DEFINE_NAME;
          ptr++;                  /* Correct offset for error */
          *errorcodeptr = ERR24;
          goto FAILED;
          }
        break;


        /* ------------------------------------------------------------ */
        case '>':                 /* One-time brackets */
        bravalue = OP_ONCE;
        ptr++;
        break;


        /* ------------------------------------------------------------ */
        case 'C':                 /* Callout - may be followed by digits; */
        previous_callout = code;  /* Save for later completion */
        after_manual_callout = 1; /* Skip one item before completing */
        *code++ = OP_CALLOUT;
          {
          int n = 0;
          while ((digitab[*(++ptr)] & ctype_digit) != 0)
            n = n * 10 + *ptr - '0';
          if (*ptr != ')')
            {
            *errorcodeptr = ERR39;
            goto FAILED;
            }
          if (n > 255)
            {
            *errorcodeptr = ERR38;
            goto FAILED;
            }
          *code++ = static_cast<uschar>(n);
          PUT(code, 0, ptr - cd->start_pattern + 1);  /* Pattern offset */
          PUT(code, LINK_SIZE, 0);                    /* Default length */
          code += 2 * LINK_SIZE;
          }
        previous = NULL;
        continue;


        /* ------------------------------------------------------------ */
        case 'P':                 /* Python-style named subpattern handling */
        if (*(++ptr) == '=' || *ptr == '>')  /* Reference or recursion */
          {
          is_recurse = *ptr == '>';
          terminator = ')';
          goto NAMED_REF_OR_RECURSE;
          }
        else if (*ptr != '<')    /* Test for Python-style definition */
          {
          *errorcodeptr = ERR41;
          goto FAILED;
          }
        /* Fall through to handle (?P< as (?< is handled */


        /* ------------------------------------------------------------ */
        DEFINE_NAME:    /* Come here from (?< handling */
        case '\'':
          {
          terminator = (*ptr == '<')? '>' : '\'';
          name = ++ptr;

          while ((cd->ctypes[*ptr] & ctype_word) != 0) ptr++;
          namelen = ptr - name;

          /* In the pre-compile phase, just do a syntax check. */

          if (lengthptr != NULL)
            {
            if (*ptr != terminator)
              {
              *errorcodeptr = ERR42;
              goto FAILED;
              }
            if (cd->names_found >= MAX_NAME_COUNT)
              {
              *errorcodeptr = ERR49;
              goto FAILED;
              }
            if (namelen + 3 > cd->name_entry_size)
              {
              cd->name_entry_size = namelen + 3;
              if (namelen > MAX_NAME_SIZE)
                {
                *errorcodeptr = ERR48;
                goto FAILED;
                }
              }
            }

          /* In the real compile, create the entry in the table */

          else
            {
            slot = cd->name_table;
            for (i = 0; i < cd->names_found; i++)
              {
              int crc = memcmp(name, slot+2, namelen);
              if (crc == 0)
                {
                if (slot[2+namelen] == 0)
                  {
                  if ((options & PCRE_DUPNAMES) == 0)
                    {
                    *errorcodeptr = ERR43;
                    goto FAILED;
                    }
                  }
                else crc = -1;      /* Current name is substring */
                }
              if (crc < 0)
                {
                memmove(slot + cd->name_entry_size, slot,
                  (cd->names_found - i) * cd->name_entry_size);
                break;
                }
              slot += cd->name_entry_size;
              }

            PUT2(slot, 0, cd->bracount + 1);
            memcpy(slot + 2, name, namelen);
            slot[2+namelen] = 0;
            }
          }

        /* In both cases, count the number of names we've encountered. */

        ptr++;                    /* Move past > or ' */
        cd->names_found++;
        goto NUMBERED_GROUP;


        /* ------------------------------------------------------------ */
        case '&':                 /* Perl recursion/subroutine syntax */
        terminator = ')';
        is_recurse = true;
        /* Fall through */

        /* We come here from the Python syntax above that handles both
        references (?P=name) and recursion (?P>name), as well as falling
        through from the Perl recursion syntax (?&name). */

        NAMED_REF_OR_RECURSE:
        name = ++ptr;
        while ((cd->ctypes[*ptr] & ctype_word) != 0) ptr++;
        namelen = ptr - name;

        /* In the pre-compile phase, do a syntax check and set a dummy
        reference number. */

        if (lengthptr != NULL)
          {
          if (*ptr != terminator)
            {
            *errorcodeptr = ERR42;
            goto FAILED;
            }
          if (namelen > MAX_NAME_SIZE)
            {
            *errorcodeptr = ERR48;
            goto FAILED;
            }
          recno = 0;
          }

        /* In the real compile, seek the name in the table */

        else
          {
          slot = cd->name_table;
          for (i = 0; i < cd->names_found; i++)
            {
            if (strncmp((char *)name, (char *)slot+2, namelen) == 0) break;
            slot += cd->name_entry_size;
            }

          if (i < cd->names_found)         /* Back reference */
            {
            recno = GET2(slot, 0);
            }
          else if ((recno =                /* Forward back reference */
                    find_parens(ptr, cd->bracount, name, namelen,
                      (options & PCRE_EXTENDED) != 0)) <= 0)
            {
            *errorcodeptr = ERR15;
            goto FAILED;
            }
          }

        /* In both phases, we can now go to the code than handles numerical
        recursion or backreferences. */

        if (is_recurse) goto HANDLE_RECURSION;
          else goto HANDLE_REFERENCE;


        /* ------------------------------------------------------------ */
        case 'R':                 /* Recursion */
        ptr++;                    /* Same as (?0)      */
        /* Fall through */


        /* ------------------------------------------------------------ */
        case '0': case '1': case '2': case '3': case '4':   /* Recursion or */
        case '5': case '6': case '7': case '8': case '9':   /* subroutine */
          {
          const uschar *called;
          recno = 0;
          while((digitab[*ptr] & ctype_digit) != 0)
            recno = recno * 10 + *ptr++ - '0';
          if (*ptr != ')')
            {
            *errorcodeptr = ERR29;
            goto FAILED;
            }

          /* Come here from code above that handles a named recursion */

          HANDLE_RECURSION:

          previous = code;
          called = cd->start_code;

          /* When we are actually compiling, find the bracket that is being
          referenced. Temporarily end the regex in case it doesn't exist before
          this point. If we end up with a forward reference, first check that
          the bracket does occur later so we can give the error (and position)
          now. Then remember this forward reference in the workspace so it can
          be filled in at the end. */

          if (lengthptr == NULL)
            {
            *code = OP_END;
            if (recno != 0) called = find_bracket(cd->start_code, utf8, recno);

            /* Forward reference */

            if (called == NULL)
              {
              if (find_parens(ptr, cd->bracount, NULL, recno,
                   (options & PCRE_EXTENDED) != 0) < 0)
                {
                *errorcodeptr = ERR15;
                goto FAILED;
                }
              called = cd->start_code + recno;
              PUTINC(cd->hwm, 0, code + 2 + LINK_SIZE - cd->start_code);
              }

            /* If not a forward reference, and the subpattern is still open,
            this is a recursive call. We check to see if this is a left
            recursion that could loop for ever, and diagnose that case. */

            else if (GET(called, 1) == 0 &&
                     could_be_empty(called, code, bcptr, utf8))
              {
              *errorcodeptr = ERR40;
              goto FAILED;
              }
            }

          /* Insert the recursion/subroutine item, automatically wrapped inside
          "once" brackets. Set up a "previous group" length so that a
          subsequent quantifier will work. */

          *code = OP_ONCE;
          PUT(code, 1, 2 + 2*LINK_SIZE);
          code += 1 + LINK_SIZE;

          *code = OP_RECURSE;
          PUT(code, 1, called - cd->start_code);
          code += 1 + LINK_SIZE;

          *code = OP_KET;
          PUT(code, 1, 2 + 2*LINK_SIZE);
          code += 1 + LINK_SIZE;

          length_prevgroup = 3 + 3*LINK_SIZE;
          }

        /* Can't determine a first byte now */

        if (firstbyte == REQ_UNSET) firstbyte = REQ_NONE;
        continue;


        /* ------------------------------------------------------------ */
        default:              /* Other characters: check option setting */
        set = unset = 0;
        optset = &set;

        while (*ptr != ')' && *ptr != ':')
          {
          switch (*ptr++)
            {
            case '-': optset = &unset; break;

            case 'J':    /* Record that it changed in the external options */
            *optset |= PCRE_DUPNAMES;
            cd->external_options |= PCRE_JCHANGED;
            break;

            case 'i': *optset |= PCRE_CASELESS; break;
            case 'm': *optset |= PCRE_MULTILINE; break;
            case 's': *optset |= PCRE_DOTALL; break;
            case 'x': *optset |= PCRE_EXTENDED; break;
            case 'U': *optset |= PCRE_UNGREEDY; break;
            case 'X': *optset |= PCRE_EXTRA; break;

            default:  *errorcodeptr = ERR12;
                      ptr--;    /* Correct the offset */
                      goto FAILED;
            }
          }

        /* Set up the changed option bits, but don't change anything yet. */

        newoptions = (options | set) & (~unset);

        /* If the options ended with ')' this is not the start of a nested
        group with option changes, so the options change at this level. If this
        item is right at the start of the pattern, the options can be
        abstracted and made external in the pre-compile phase, and ignored in
        the compile phase. This can be helpful when matching -- for instance in
        caseless checking of required bytes.

        If the code pointer is not (cd->start_code + 1 + LINK_SIZE), we are
        definitely *not* at the start of the pattern because something has been
        compiled. In the pre-compile phase, however, the code pointer can have
        that value after the start, because it gets reset as code is discarded
        during the pre-compile. However, this can happen only at top level - if
        we are within parentheses, the starting BRA will still be present. At
        any parenthesis level, the length value can be used to test if anything
        has been compiled at that level. Thus, a test for both these conditions
        is necessary to ensure we correctly detect the start of the pattern in
        both phases.

        If we are not at the pattern start, compile code to change the ims
        options if this setting actually changes any of them. We also pass the
        new setting back so that it can be put at the start of any following
        branches, and when this group ends (if we are in a group), a resetting
        item can be compiled. */

        if (*ptr == ')')
          {
          if (code == cd->start_code + 1 + LINK_SIZE &&
               (lengthptr == NULL || *lengthptr == 2 + 2*LINK_SIZE))
            {
            cd->external_options = newoptions;
            options = newoptions;
            }
         else
            {
            if ((options & PCRE_IMS) != (newoptions & PCRE_IMS))
              {
              *code++ = OP_OPT;
              *code++ = newoptions & PCRE_IMS;
              }

            /* Change options at this level, and pass them back for use
            in subsequent branches. Reset the greedy defaults and the case
            value for firstbyte and reqbyte. */

            *optionsptr = options = newoptions;
            greedy_default = ((newoptions & PCRE_UNGREEDY) != 0);
            greedy_non_default = greedy_default ^ 1;
            req_caseopt = ((options & PCRE_CASELESS) != 0)? REQ_CASELESS : 0;
            }

          previous = NULL;       /* This item can't be repeated */
          continue;              /* It is complete */
          }

        /* If the options ended with ':' we are heading into a nested group
        with possible change of options. Such groups are non-capturing and are
        not assertions of any kind. All we need to do is skip over the ':';
        the newoptions value is handled below. */

        bravalue = OP_BRA;
        ptr++;
        }     /* End of switch for character following (? */
      }       /* End of (? handling */

    /* Opening parenthesis not followed by '?'. If PCRE_NO_AUTO_CAPTURE is set,
    all unadorned brackets become non-capturing and behave like (?:...)
    brackets. */

    else if ((options & PCRE_NO_AUTO_CAPTURE) != 0)
      {
      bravalue = OP_BRA;
      }

    /* Else we have a capturing group. */

    else
      {
      NUMBERED_GROUP:
      cd->bracount += 1;
      PUT2(code, 1+LINK_SIZE, cd->bracount);
      skipbytes = 2;
      }

    /* Process nested bracketed regex. Assertions may not be repeated, but
    other kinds can be. All their opcodes are >= OP_ONCE. We copy code into a
    non-register variable in order to be able to pass its address because some
    compilers complain otherwise. Pass in a new setting for the ims options if
    they have changed. */

    previous = (bravalue >= OP_ONCE)? code : NULL;
    *code = static_cast<uschar>(bravalue);
    tempcode = code;
    tempreqvary = cd->req_varyopt;     /* Save value before bracket */
    length_prevgroup = 0;              /* Initialize for pre-compile phase */

    if (!compile_regex(
         newoptions,                   /* The complete new option state */
         options & PCRE_IMS,           /* The previous ims option state */
         &tempcode,                    /* Where to put code (updated) */
         &ptr,                         /* Input pointer (updated) */
         errorcodeptr,                 /* Where to put an error message */
         (bravalue == OP_ASSERTBACK ||
          bravalue == OP_ASSERTBACK_NOT), /* true if back assert */
         skipbytes,                    /* Skip over bracket number */
         &subfirstbyte,                /* For possible first char */
         &subreqbyte,                  /* For possible last char */
         bcptr,                        /* Current branch chain */
         cd,                           /* Tables block */
         (lengthptr == NULL)? NULL :   /* Actual compile phase */
           &length_prevgroup           /* Pre-compile phase */
         ))
      goto FAILED;

    /* At the end of compiling, code is still pointing to the start of the
    group, while tempcode has been updated to point past the end of the group
    and any option resetting that may follow it. The pattern pointer (ptr)
    is on the bracket. */

    /* If this is a conditional bracket, check that there are no more than
    two branches in the group, or just one if it's a DEFINE group. */

    if (bravalue == OP_COND)
      {
      uschar *tc = code;
      int condcount = 0;

      do {
         condcount++;
         tc += GET(tc,1);
         }
      while (*tc != OP_KET);

      /* A DEFINE group is never obeyed inline (the "condition" is always
      false). It must have only one branch. */

      if (code[LINK_SIZE+1] == OP_DEF)
        {
        if (condcount > 1)
          {
          *errorcodeptr = ERR54;
          goto FAILED;
          }
        bravalue = OP_DEF;   /* Just a flag to suppress char handling below */
        }

      /* A "normal" conditional group. If there is just one branch, we must not
      make use of its firstbyte or reqbyte, because this is equivalent to an
      empty second branch. */

      else
        {
        if (condcount > 2)
          {
          *errorcodeptr = ERR27;
          goto FAILED;
          }
        if (condcount == 1) subfirstbyte = subreqbyte = REQ_NONE;
        }
      }

    /* Error if hit end of pattern */

    if (*ptr != ')')
      {
      *errorcodeptr = ERR14;
      goto FAILED;
      }

    /* In the pre-compile phase, update the length by the length of the nested
    group, less the brackets at either end. Then reduce the compiled code to
    just the brackets so that it doesn't use much memory if it is duplicated by
    a quantifier. */

    if (lengthptr != NULL)
      {
      *lengthptr += length_prevgroup - 2 - 2*LINK_SIZE;
      code++;
      PUTINC(code, 0, 1 + LINK_SIZE);
      *code++ = OP_KET;
      PUTINC(code, 0, 1 + LINK_SIZE);
      }

    /* Otherwise update the main code pointer to the end of the group. */

    else code = tempcode;

    /* For a DEFINE group, required and first character settings are not
    relevant. */

    if (bravalue == OP_DEF) break;

    /* Handle updating of the required and first characters for other types of
    group. Update for normal brackets of all kinds, and conditions with two
    branches (see code above). If the bracket is followed by a quantifier with
    zero repeat, we have to back off. Hence the definition of zeroreqbyte and
    zerofirstbyte outside the main loop so that they can be accessed for the
    back off. */

    zeroreqbyte = reqbyte;
    zerofirstbyte = firstbyte;
    groupsetfirstbyte = false;

    if (bravalue >= OP_ONCE)
      {
      /* If we have not yet set a firstbyte in this branch, take it from the
      subpattern, remembering that it was set here so that a repeat of more
      than one can replicate it as reqbyte if necessary. If the subpattern has
      no firstbyte, set "none" for the whole branch. In both cases, a zero
      repeat forces firstbyte to "none". */

      if (firstbyte == REQ_UNSET)
        {
        if (subfirstbyte >= 0)
          {
          firstbyte = subfirstbyte;
          groupsetfirstbyte = true;
          }
        else firstbyte = REQ_NONE;
        zerofirstbyte = REQ_NONE;
        }

      /* If firstbyte was previously set, convert the subpattern's firstbyte
      into reqbyte if there wasn't one, using the vary flag that was in
      existence beforehand. */

      else if (subfirstbyte >= 0 && subreqbyte < 0)
        subreqbyte = subfirstbyte | tempreqvary;

      /* If the subpattern set a required byte (or set a first byte that isn't
      really the first byte - see above), set it. */

      if (subreqbyte >= 0) reqbyte = subreqbyte;
      }

    /* For a forward assertion, we take the reqbyte, if set. This can be
    helpful if the pattern that follows the assertion doesn't set a different
    char. For example, it's useful for /(?=abcde).+/. We can't set firstbyte
    for an assertion, however because it leads to incorrect effect for patterns
    such as /(?=a)a.+/ when the "real" "a" would then become a reqbyte instead
    of a firstbyte. This is overcome by a scan at the end if there's no
    firstbyte, looking for an asserted first char. */

    else if (bravalue == OP_ASSERT && subreqbyte >= 0) reqbyte = subreqbyte;
    break;     /* End of processing '(' */


    /* ===================================================================*/
    /* Handle metasequences introduced by \. For ones like \d, the ESC_ values
    are arranged to be the negation of the corresponding OP_values. For the
    back references, the values are ESC_REF plus the reference number. Only
    back references and those types that consume a character may be repeated.
    We can test for values between ESC_b and ESC_Z for the latter; this may
    have to change if any new ones are ever created. */

    case '\\':
    tempptr = ptr;
    c = check_escape(&ptr, errorcodeptr, cd->bracount, options, false);
    if (*errorcodeptr != 0) goto FAILED;

    if (c < 0)
      {
      if (-c == ESC_Q)            /* Handle start of quoted string */
        {
        if (ptr[1] == '\\' && ptr[2] == 'E') ptr += 2; /* avoid empty string */
          else inescq = true;
        continue;
        }

      if (-c == ESC_E) continue;  /* Perl ignores an orphan \E */

      /* For metasequences that actually match a character, we disable the
      setting of a first character if it hasn't already been set. */

      if (firstbyte == REQ_UNSET && -c > ESC_b && -c < ESC_Z)
        firstbyte = REQ_NONE;

      /* Set values to reset to if this is followed by a zero repeat. */

      zerofirstbyte = firstbyte;
      zeroreqbyte = reqbyte;

      /* \k<name> or \k'name' is a back reference by name (Perl syntax) */

      if (-c == ESC_k && (ptr[1] == '<' || ptr[1] == '\''))
        {
        is_recurse = false;
        terminator = (*(++ptr) == '<')? '>' : '\'';
        goto NAMED_REF_OR_RECURSE;
        }

      /* Back references are handled specially; must disable firstbyte if
      not set to cope with cases like (?=(\w+))\1: which would otherwise set
      ':' later. */

      if (-c >= ESC_REF)
        {
        recno = -c - ESC_REF;

        HANDLE_REFERENCE:    /* Come here from named backref handling */
        if (firstbyte == REQ_UNSET) firstbyte = REQ_NONE;
        previous = code;
        *code++ = OP_REF;
        PUT2INC(code, 0, recno);
        cd->backref_map |= (recno < 32)? (1 << recno) : 1;
        if (recno > cd->top_backref) cd->top_backref = recno;
        }

      /* So are Unicode property matches, if supported. */

#ifdef SUPPORT_UCP
      else if (-c == ESC_P || -c == ESC_p)
        {
        bool negated;
        int pdata;
        int ptype = get_ucp(&ptr, &negated, &pdata, errorcodeptr);
        if (ptype < 0) goto FAILED;
        previous = code;
        *code++ = ((-c == ESC_p) != negated)? OP_PROP : OP_NOTPROP;
        *code++ = static_cast<uschar>(ptype);
        *code++ = static_cast<uschar>(pdata);
        }
#endif

      /* For the rest (including \X when Unicode properties are supported), we
      can obtain the OP value by negating the escape value. */

      else
        {
        previous = (-c > ESC_b && -c < ESC_Z)? code : NULL;
        *code++ = static_cast<uschar>(-c);
        }
      continue;
      }

    /* We have a data character whose value is in c. In UTF-8 mode it may have
    a value > 127. We set its representation in the length/buffer, and then
    handle it as a data character. */

#ifdef SUPPORT_UTF8
    if (utf8 && c > 127)
      mclength = _pcre_ord2utf8(c, mcbuffer);
    else
#endif

     {
     mcbuffer[0] = c;
     mclength = 1;
     }
    goto ONE_CHAR;

    /* ===================================================================*/
    /* Handle a literal character. It is guaranteed not to be whitespace or #
    when the extended flag is set. If we are in UTF-8 mode, it may be a
    multi-byte literal character. */

    default:
    NORMAL_CHAR:
    mclength = 1;
    mcbuffer[0] = c;

#ifdef SUPPORT_UTF8
    if (utf8 && c >= 0xc0)
      {
      while ((ptr[1] & 0xc0) == 0x80)
        mcbuffer[mclength++] = *(++ptr);
      }
#endif

    /* At this point we have the character's bytes in mcbuffer, and the length
    in mclength. When not in UTF-8 mode, the length is always 1. */

    ONE_CHAR:
    previous = code;
    *code++ = ((options & PCRE_CASELESS) != 0)? OP_CHARNC : OP_CHAR;
    for (c = 0; c < mclength; c++) *code++ = static_cast<uschar>(mcbuffer[c]);

    /* Set the first and required bytes appropriately. If no previous first
    byte, set it from this character, but revert to none on a zero repeat.
    Otherwise, leave the firstbyte value alone, and don't change it on a zero
    repeat. */

    if (firstbyte == REQ_UNSET)
      {
      zerofirstbyte = REQ_NONE;
      zeroreqbyte = reqbyte;

      /* If the character is more than one byte long, we can set firstbyte
      only if it is not to be matched caselessly. */

      if (mclength == 1 || req_caseopt == 0)
        {
        firstbyte = mcbuffer[0] | req_caseopt;
        if (mclength != 1) reqbyte = code[-1] | cd->req_varyopt;
        }
      else firstbyte = reqbyte = REQ_NONE;
      }

    /* firstbyte was previously set; we can set reqbyte only the length is
    1 or the matching is caseful. */

    else
      {
      zerofirstbyte = firstbyte;
      zeroreqbyte = reqbyte;
      if (mclength == 1 || req_caseopt == 0)
        reqbyte = code[-1] | req_caseopt | cd->req_varyopt;
      }

    break;            /* End of literal character handling */
    }
  }                   /* end of big loop */

/* Control never reaches here by falling through, only by a goto for all the
error states. Pass back the position in the pattern so that it can be displayed
to the user for diagnosing the error. */

FAILED:
*ptrptr = ptr;
return false;
}




/*************************************************
*     Compile sequence of alternatives           *
*************************************************/

/* On entry, ptr is pointing past the bracket character, but on return it
points to the closing bracket, or vertical bar, or end of string. The code
variable is pointing at the byte into which the BRA operator has been stored.
If the ims options are changed at the start (for a (?ims: group) or during any
branch, we need to insert an OP_OPT item at the start of every following branch
to ensure they get set correctly at run time, and also pass the new options
into every subsequent branch compile.

This function is used during the pre-compile phase when we are trying to find
out the amount of memory needed, as well as during the real compile phase. The
value of lengthptr distinguishes the two phases.

Argument:
  options        option bits, including any changes for this subpattern
  oldims         previous settings of ims option bits
  codeptr        -> the address of the current code pointer
  ptrptr         -> the address of the current pattern pointer
  errorcodeptr   -> pointer to error code variable
  lookbehind     true if this is a lookbehind assertion
  skipbytes      skip this many bytes at start (for brackets and OP_COND)
  firstbyteptr   place to put the first required character, or a negative number
  reqbyteptr     place to put the last required character, or a negative number
  bcptr          pointer to the chain of currently open branches
  cd             points to the data block with tables pointers etc.
  lengthptr      NULL during the real compile phase
                 points to length accumulator during pre-compile phase

Returns:         true on success
*/

static bool
compile_regex(int options, int oldims, uschar **codeptr, const uschar **ptrptr,
  int *errorcodeptr, bool lookbehind, int skipbytes, int *firstbyteptr,
  int *reqbyteptr, branch_chain *bcptr, compile_data *cd, int *lengthptr)
{
const uschar *ptr = *ptrptr;
uschar *code = *codeptr;
uschar *last_branch = code;
uschar *start_bracket = code;
uschar *reverse_count = NULL;
int firstbyte, reqbyte;
int branchfirstbyte, branchreqbyte;
int length;
branch_chain bc;

bc.outer = bcptr;
bc.current = code;

firstbyte = reqbyte = REQ_UNSET;

/* Accumulate the length for use in the pre-compile phase. Start with the
length of the BRA and KET and any extra bytes that are required at the
beginning. We accumulate in a local variable to save frequent testing of
lenthptr for NULL. We cannot do this by looking at the value of code at the
start and end of each alternative, because compiled items are discarded during
the pre-compile phase so that the work space is not exceeded. */

length = 2 + 2*LINK_SIZE + skipbytes;

/* WARNING: If the above line is changed for any reason, you must also change
the code that abstracts option settings at the start of the pattern and makes
them global. It tests the value of length for (2 + 2*LINK_SIZE) in the
pre-compile phase to find out whether anything has yet been compiled or not. */

/* Offset is set zero to mark that this bracket is still open */

PUT(code, 1, 0);
code += 1 + LINK_SIZE + skipbytes;

/* Loop for each alternative branch */

for (;!alarm_clock.alarmed;)
  {
  /* Handle a change of ims options at the start of the branch */

  if ((options & PCRE_IMS) != oldims)
    {
    *code++ = OP_OPT;
    *code++ = options & PCRE_IMS;
    length += 2;
    }

  /* Set up dummy OP_REVERSE if lookbehind assertion */

  if (lookbehind)
    {
    *code++ = OP_REVERSE;
    reverse_count = code;
    PUTINC(code, 0, 0);
    length += 1 + LINK_SIZE;
    }

  /* Now compile the branch; in the pre-compile phase its length gets added
  into the length. */

  if (!compile_branch(&options, &code, &ptr, errorcodeptr, &branchfirstbyte,
        &branchreqbyte, &bc, cd, (lengthptr == NULL)? NULL : &length))
    {
    *ptrptr = ptr;
    return false;
    }

  /* In the real compile phase, there is some post-processing to be done. */

  if (lengthptr == NULL)
    {
    /* If this is the first branch, the firstbyte and reqbyte values for the
    branch become the values for the regex. */

    if (*last_branch != OP_ALT)
      {
      firstbyte = branchfirstbyte;
      reqbyte = branchreqbyte;
      }

    /* If this is not the first branch, the first char and reqbyte have to
    match the values from all the previous branches, except that if the
    previous value for reqbyte didn't have REQ_VARY set, it can still match,
    and we set REQ_VARY for the regex. */

    else
      {
      /* If we previously had a firstbyte, but it doesn't match the new branch,
      we have to abandon the firstbyte for the regex, but if there was
      previously no reqbyte, it takes on the value of the old firstbyte. */

      if (firstbyte >= 0 && firstbyte != branchfirstbyte)
        {
        if (reqbyte < 0) reqbyte = firstbyte;
        firstbyte = REQ_NONE;
        }

      /* If we (now or from before) have no firstbyte, a firstbyte from the
      branch becomes a reqbyte if there isn't a branch reqbyte. */

      if (firstbyte < 0 && branchfirstbyte >= 0 && branchreqbyte < 0)
          branchreqbyte = branchfirstbyte;

      /* Now ensure that the reqbytes match */

      if ((reqbyte & ~REQ_VARY) != (branchreqbyte & ~REQ_VARY))
        reqbyte = REQ_NONE;
      else reqbyte |= branchreqbyte;   /* To "or" REQ_VARY */
      }

    /* If lookbehind, check that this branch matches a fixed-length string, and
    put the length into the OP_REVERSE item. Temporarily mark the end of the
    branch with OP_END. */

    if (lookbehind)
      {
      int fixed_length;
      *code = OP_END;
      fixed_length = find_fixedlength(last_branch, options);
      DPRINTF(("fixed length = %d\n", fixed_length));
      if (fixed_length < 0)
        {
        *errorcodeptr = (fixed_length == -2)? ERR36 : ERR25;
        *ptrptr = ptr;
        return false;
        }
      PUT(reverse_count, 0, fixed_length);
      }
    }

  /* Reached end of expression, either ')' or end of pattern. Go back through
  the alternative branches and reverse the chain of offsets, with the field in
  the BRA item now becoming an offset to the first alternative. If there are
  no alternatives, it points to the end of the group. The length in the
  terminating ket is always the length of the whole bracketed item. If any of
  the ims options were changed inside the group, compile a resetting op-code
  following, except at the very end of the pattern. Return leaving the pointer
  at the terminating char. */

  if (*ptr != '|')
    {
    int branch_length = code - last_branch;
    do
      {
      int prev_length = GET(last_branch, 1);
      PUT(last_branch, 1, branch_length);
      branch_length = prev_length;
      last_branch -= branch_length;
      }
    while (branch_length > 0);

    /* Fill in the ket */

    *code = OP_KET;
    PUT(code, 1, code - start_bracket);
    code += 1 + LINK_SIZE;

    /* Resetting option if needed */

    if ((options & PCRE_IMS) != oldims && *ptr == ')')
      {
      *code++ = OP_OPT;
      *code++ = static_cast<uschar>(oldims);
      length += 2;
      }

    /* Set values to pass back */

    *codeptr = code;
    *ptrptr = ptr;
    *firstbyteptr = firstbyte;
    *reqbyteptr = reqbyte;
    if (lengthptr != NULL) *lengthptr += length;
    return true;
    }

  /* Another branch follows; insert an "or" node. Its length field points back
  to the previous branch while the bracket remains open. At the end the chain
  is reversed. It's done like this so that the start of the bracket has a
  zero offset until it is closed, making it possible to detect recursion. */

  *code = OP_ALT;
  PUT(code, 1, code - last_branch);
  bc.current = last_branch = code;
  code += 1 + LINK_SIZE;
  ptr++;
  length += 1 + LINK_SIZE;
  }
return false;
}




/*************************************************
*          Check for anchored expression         *
*************************************************/

/* Try to find out if this is an anchored regular expression. Consider each
alternative branch. If they all start with OP_SOD or OP_CIRC, or with a bracket
all of whose alternatives start with OP_SOD or OP_CIRC (recurse ad lib), then
it's anchored. However, if this is a multiline pattern, then only OP_SOD
counts, since OP_CIRC can match in the middle.

We can also consider a regex to be anchored if OP_SOM starts all its branches.
This is the code for \G, which means "match at start of match position, taking
into account the match offset".

A branch is also implicitly anchored if it starts with .* and DOTALL is set,
because that will try the rest of the pattern at all possible matching points,
so there is no point trying again.... er ....

.... except when the .* appears inside capturing parentheses, and there is a
subsequent back reference to those parentheses. We haven't enough information
to catch that case precisely.

At first, the best we could do was to detect when .* was in capturing brackets
and the highest back reference was greater than or equal to that level.
However, by keeping a bitmap of the first 31 back references, we can catch some
of the more common cases more precisely.

Arguments:
  code           points to start of expression (the bracket)
  options        points to the options setting
  bracket_map    a bitmap of which brackets we are inside while testing; this
                  handles up to substring 31; after that we just have to take
                  the less precise approach
  backref_map    the back reference bitmap

Returns:     true or false
*/

static bool
is_anchored(register const uschar *code, int *options, unsigned int bracket_map,
  unsigned int backref_map)
{
do {
   const uschar *scode = first_significant_code(code + _pcre_OP_lengths[*code],
     options, PCRE_MULTILINE, false);
   register int op = *scode;

   /* Non-capturing brackets */

   if (op == OP_BRA)
     {
     if (!is_anchored(scode, options, bracket_map, backref_map)) return false;
     }

   /* Capturing brackets */

   else if (op == OP_CBRA)
     {
     int n = GET2(scode, 1+LINK_SIZE);
     int new_map = bracket_map | ((n < 32)? (1 << n) : 1);
     if (!is_anchored(scode, options, new_map, backref_map)) return false;
     }

   /* Other brackets */

   else if (op == OP_ASSERT || op == OP_ONCE || op == OP_COND)
     {
     if (!is_anchored(scode, options, bracket_map, backref_map)) return false;
     }

   /* .* is not anchored unless DOTALL is set and it isn't in brackets that
   are or may be referenced. */

   else if ((op == OP_TYPESTAR || op == OP_TYPEMINSTAR ||
             op == OP_TYPEPOSSTAR) &&
            (*options & PCRE_DOTALL) != 0)
     {
     if (scode[1] != OP_ANY || (bracket_map & backref_map) != 0) return false;
     }

   /* Check for explicit anchoring */

   else if (op != OP_SOD && op != OP_SOM &&
           ((*options & PCRE_MULTILINE) != 0 || op != OP_CIRC))
     return false;
   code += GET(code, 1);
   }
while (*code == OP_ALT);   /* Loop for each alternative */
return true;
}



/*************************************************
*         Check for starting with ^ or .*        *
*************************************************/

/* This is called to find out if every branch starts with ^ or .* so that
"first char" processing can be done to speed things up in multiline
matching and for non-DOTALL patterns that start with .* (which must start at
the beginning or after \n). As in the case of is_anchored() (see above), we
have to take account of back references to capturing brackets that contain .*
because in that case we can't make the assumption.

Arguments:
  code           points to start of expression (the bracket)
  bracket_map    a bitmap of which brackets we are inside while testing; this
                  handles up to substring 31; after that we just have to take
                  the less precise approach
  backref_map    the back reference bitmap

Returns:         true or false
*/

static bool
is_startline(const uschar *code, unsigned int bracket_map,
  unsigned int backref_map)
{
do {
   const uschar *scode = first_significant_code(code + _pcre_OP_lengths[*code],
     NULL, 0, false);
   register int op = *scode;

   /* Non-capturing brackets */

   if (op == OP_BRA)
     {
     if (!is_startline(scode, bracket_map, backref_map)) return false;
     }

   /* Capturing brackets */

   else if (op == OP_CBRA)
     {
     int n = GET2(scode, 1+LINK_SIZE);
     int new_map = bracket_map | ((n < 32)? (1 << n) : 1);
     if (!is_startline(scode, new_map, backref_map)) return false;
     }

   /* Other brackets */

   else if (op == OP_ASSERT || op == OP_ONCE || op == OP_COND)
     { if (!is_startline(scode, bracket_map, backref_map)) return false; }

   /* .* means "start at start or after \n" if it isn't in brackets that
   may be referenced. */

   else if (op == OP_TYPESTAR || op == OP_TYPEMINSTAR || op == OP_TYPEPOSSTAR)
     {
     if (scode[1] != OP_ANY || (bracket_map & backref_map) != 0) return false;
     }

   /* Check for explicit circumflex */

   else if (op != OP_CIRC) return false;

   /* Move on to the next alternative */

   code += GET(code, 1);
   }
while (*code == OP_ALT);  /* Loop for each alternative */
return true;
}



/*************************************************
*       Check for asserted fixed first char      *
*************************************************/

/* During compilation, the "first char" settings from forward assertions are
discarded, because they can cause conflicts with actual literals that follow.
However, if we end up without a first char setting for an unanchored pattern,
it is worth scanning the regex to see if there is an initial asserted first
char. If all branches start with the same asserted char, or with a bracket all
of whose alternatives start with the same asserted char (recurse ad lib), then
we return that char, otherwise -1.

Arguments:
  code       points to start of expression (the bracket)
  options    pointer to the options (used to check casing changes)
  inassert   true if in an assertion

Returns:     -1 or the fixed first char
*/

static int
find_firstassertedchar(const uschar *code, int *options, bool inassert)
{
register int c = -1;
do {
   int d;
   const uschar *scode =
     first_significant_code(code + 1+LINK_SIZE, options, PCRE_CASELESS, true);
   register int op = *scode;

   switch(op)
     {
     default:
     return -1;

     case OP_BRA:
     case OP_CBRA:
     case OP_ASSERT:
     case OP_ONCE:
     case OP_COND:
     if ((d = find_firstassertedchar(scode, options, op == OP_ASSERT)) < 0)
       return -1;
     if (c < 0) c = d; else if (c != d) return -1;
     break;

     case OP_EXACT:       /* Fall through */
     scode += 2;

     case OP_CHAR:
     case OP_CHARNC:
     case OP_PLUS:
     case OP_MINPLUS:
     case OP_POSPLUS:
     if (!inassert) return -1;
     if (c < 0)
       {
       c = scode[1];
       if ((*options & PCRE_CASELESS) != 0) c |= REQ_CASELESS;
       }
     else if (c != scode[1]) return -1;
     break;
     }

   code += GET(code, 1);
   }
while (*code == OP_ALT);
return c;
}
/* End pcre_compile.c */
#undef NLBLOCK
#undef PSSTART
#undef PSEND



/* Begin pcre_valid_utf8.c */


/*************************************************
*         Validate a UTF-8 string                *
*************************************************/

/* This function is called (optionally) at the start of compile or match, to
validate that a supposed UTF-8 string is actually valid. The early check means
that subsequent code can assume it is dealing with a valid string. The check
can be turned off for maximum performance, but the consequences of supplying
an invalid string are then undefined.

Arguments:
  string       points to the string
  length       length of string, or -1 if the string is zero-terminated

Returns:       < 0    if the string is a valid UTF-8 string
               >= 0   otherwise; the value is the offset of the bad byte
*/

static int
_pcre_valid_utf8(const uschar *string, int length)
{
#ifdef SUPPORT_UTF8
register const uschar *p;

if (length < 0)
  {
  for (p = string; *p != 0; p++);
  length = p - string;
  }

for (p = string; length-- > 0; p++)
  {
  register int ab;
  register int c = *p;
  if (c < 128) continue;
  if (c < 0xc0) return p - string;
  ab = _pcre_utf8_table4[c & 0x3f];  /* Number of additional bytes */
  if (length < ab) return p - string;
  length -= ab;

  /* Check top bits in the second byte */
  if ((*(++p) & 0xc0) != 0x80) return p - string;

  /* Check for overlong sequences for each different length */
  switch (ab)
    {
    /* Check for xx00 000x */
    case 1:
    if ((c & 0x3e) == 0) return p - string;
    continue;   /* We know there aren't any more bytes to check */

    /* Check for 1110 0000, xx0x xxxx */
    case 2:
    if (c == 0xe0 && (*p & 0x20) == 0) return p - string;
    break;

    /* Check for 1111 0000, xx00 xxxx */
    case 3:
    if (c == 0xf0 && (*p & 0x30) == 0) return p - string;
    break;

    /* Check for 1111 1000, xx00 0xxx */
    case 4:
    if (c == 0xf8 && (*p & 0x38) == 0) return p - string;
    break;

    /* Check for leading 0xfe or 0xff, and then for 1111 1100, xx00 00xx */
    case 5:
    if (c == 0xfe || c == 0xff ||
       (c == 0xfc && (*p & 0x3c) == 0)) return p - string;
    break;
    }

  /* Check for valid bytes after the 2nd, if any; all must start 10 */
  while (--ab > 0)
    {
    if ((*(++p) & 0xc0) != 0x80) return p - string;
    }
  }
#endif

return -1;
}

/* End pcre_valid_utf8.c */


#define NLBLOCK cd
#define PSSTART start_pattern  /* Field containing processed string start */
#define PSEND   end_pattern    /* Field containing processed string end */
/* Begin pcre_compile.c */
/*************************************************
*        Compile a Regular Expression            *
*************************************************/

/* This function takes a string and returns a pointer to a block of store
holding a compiled version of the expression. The original API for this
function had no error code return variable; it is retained for backwards
compatibility. The new function is given a new name.

Arguments:
  pattern       the regular expression
  options       various option bits
  errorcodeptr  pointer to error code variable (pcre_compile2() only)
                  can be NULL if you don't want a code value
  errorptr      pointer to pointer to error text
  erroroffset   ptr offset in pattern where error was detected
  tables        pointer to character tables or NULL

Returns:        pointer to compiled data block, or NULL on error,
                with errorptr and erroroffset set
*/

pcre *
pcre_compile(const char *pattern, int options, const char **errorptr,
  int *erroroffset, const unsigned char *tables)
{
return pcre_compile2(pattern, options, NULL, errorptr, erroroffset, tables);
}


pcre *
pcre_compile2(const char *pattern, int options, int *errorcodeptr,
  const char **errorptr, int *erroroffset, const unsigned char *tables)
{
real_pcre *re;
int length = 1;  /* For final END opcode */
int firstbyte, reqbyte, newline;
int errorcode = 0;
#ifdef SUPPORT_UTF8
bool utf8;
#endif
size_t size;
uschar *code;
const uschar *codestart;
const uschar *ptr;
compile_data compile_block;
compile_data *cd = &compile_block;

/* This space is used for "compiling" into during the first phase, when we are
computing the amount of memory that is needed. Compiled items are thrown away
as soon as possible, so that a fairly large buffer should be sufficient for
this purpose. The same space is used in the second phase for remembering where
to fill in forward references to subpatterns. */

uschar cworkspace[COMPILE_WORK_SIZE];


/* Set this early so that early errors get offset 0. */

ptr = (const uschar *)pattern;

/* We can't pass back an error message if errorptr is NULL; I guess the best we
can do is just return NULL, but we can set a code value if there is a code
pointer. */

if (errorptr == NULL)
  {
  if (errorcodeptr != NULL) *errorcodeptr = 99;
  return NULL;
  }

*errorptr = NULL;
if (errorcodeptr != NULL) *errorcodeptr = ERR0;

/* However, we can give a message for this error */

if (erroroffset == NULL)
  {
  errorcode = ERR16;
  goto PCRE_EARLY_ERROR_RETURN2;
  }

*erroroffset = 0;

/* Can't support UTF8 unless PCRE has been compiled to include the code. */

#ifdef SUPPORT_UTF8
utf8 = (options & PCRE_UTF8) != 0;
if (utf8 && (options & PCRE_NO_UTF8_CHECK) == 0 &&
     (*erroroffset = _pcre_valid_utf8((uschar *)pattern, -1)) >= 0)
  {
  errorcode = ERR44;
  goto PCRE_EARLY_ERROR_RETURN2;
  }
#else
if ((options & PCRE_UTF8) != 0)
  {
  errorcode = ERR32;
  goto PCRE_EARLY_ERROR_RETURN;
  }
#endif

if ((options & ~PUBLIC_OPTIONS) != 0)
  {
  errorcode = ERR17;
  goto PCRE_EARLY_ERROR_RETURN;
  }

/* Set up pointers to the individual character tables */

if (tables == NULL) tables = _pcre_default_tables;
cd->lcc = tables + lcc_offset;
cd->fcc = tables + fcc_offset;
cd->cbits = tables + cbits_offset;
cd->ctypes = tables + ctypes_offset;

/* Handle different types of newline. The three bits give seven cases. The
current code allows for fixed one- or two-byte sequences, plus "any" and
"anycrlf". */

switch (options & (PCRE_NEWLINE_CRLF | PCRE_NEWLINE_ANY))
  {
  case 0: newline = NEWLINE; break;   /* Compile-time default */
  case PCRE_NEWLINE_CR: newline = '\r'; break;
  case PCRE_NEWLINE_LF: newline = '\n'; break;
  case PCRE_NEWLINE_CR+
       PCRE_NEWLINE_LF: newline = ('\r' << 8) | '\n'; break;
  case PCRE_NEWLINE_ANY: newline = -1; break;
  case PCRE_NEWLINE_ANYCRLF: newline = -2; break;
  default: errorcode = ERR56; goto PCRE_EARLY_ERROR_RETURN;
  }

if (newline == -2)
  {
  cd->nltype = NLTYPE_ANYCRLF;
  }
else if (newline < 0)
  {
  cd->nltype = NLTYPE_ANY;
  }
else
  {
  cd->nltype = NLTYPE_FIXED;
  if (newline > 255)
    {
    cd->nllen = 2;
    cd->nl[0] = (newline >> 8) & 255;
    cd->nl[1] = newline & 255;
    }
  else
    {
    cd->nllen = 1;
    cd->nl[0] = newline;
    }
  }

/* Maximum back reference and backref bitmap. The bitmap records up to 31 back
references to help in deciding whether (.*) can be treated as anchored or not.
*/

cd->top_backref = 0;
cd->backref_map = 0;

/* Pretend to compile the pattern while actually just accumulating the length
of memory required. This behaviour is triggered by passing a non-NULL final
argument to compile_regex(). We pass a block of workspace (cworkspace) for it
to compile parts of the pattern into; the compiled code is discarded when it is
no longer needed, so hopefully this workspace will never overflow, though there
is a test for its doing so. */

cd->bracount = 0;
cd->names_found = 0;
cd->name_entry_size = 0;
cd->name_table = NULL;
cd->start_workspace = cworkspace;
cd->start_code = cworkspace;
cd->hwm = cworkspace;
cd->start_pattern = (const uschar *)pattern;
cd->end_pattern = (const uschar *)(pattern + strlen(pattern));
cd->req_varyopt = 0;
cd->nopartial = false;
cd->external_options = options;

/* Now do the pre-compile. On error, errorcode will be set non-zero, so we
don't need to look at the result of the function here. The initial options have
been put into the cd block so that they can be changed if an option setting is
found within the regex right at the beginning. Bringing initial option settings
outside can help speed up starting point checks. */

code = cworkspace;
*code = OP_BRA;
(void)compile_regex(cd->external_options, cd->external_options & PCRE_IMS,
  &code, &ptr, &errorcode, false, 0, &firstbyte, &reqbyte, NULL, cd, &length);
if (errorcode != 0) goto PCRE_EARLY_ERROR_RETURN;

if (length > MAX_PATTERN_SIZE)
  {
  errorcode = ERR20;
  goto PCRE_EARLY_ERROR_RETURN;
  }

/* Compute the size of data block needed and get it, either from malloc or
externally provided function. Integer overflow should no longer be possible
because nowadays we limit the maximum value of cd->names_found and
cd->name_entry_size. */

size = length + sizeof(real_pcre) + cd->names_found * (cd->name_entry_size + 3);
re = static_cast<real_pcre *>(malloc(size));

if (re == NULL)
  {
  errorcode = ERR21;
  goto PCRE_EARLY_ERROR_RETURN;
  }

/* Put in the magic number, and save the sizes, initial options, and character
table pointer. NULL is used for the default character tables. The nullpad field
is at the end; it's there to help in the case when a regex compiled on a system
with 4-byte pointers is run on another with 8-byte pointers. */

re->magic_number = MAGIC_NUMBER;
re->size = size;
re->options = cd->external_options;
re->dummy1 = 0;
re->first_byte = 0;
re->req_byte = 0;
re->name_table_offset = sizeof(real_pcre);
re->name_entry_size = cd->name_entry_size;
re->name_count = cd->names_found;
re->ref_count = 0;
re->tables = (tables == _pcre_default_tables)? NULL : tables;
re->nullpad = NULL;

/* The starting points of the name/number translation table and of the code are
passed around in the compile data block. The start/end pattern and initial
options are already set from the pre-compile phase, as is the name_entry_size
field. Reset the bracket count and the names_found field. Also reset the hwm
field; this time it's used for remembering forward references to subpatterns.
*/

cd->bracount = 0;
cd->names_found = 0;
cd->name_table = (uschar *)re + re->name_table_offset;
codestart = cd->name_table + re->name_entry_size * re->name_count;
cd->start_code = codestart;
cd->hwm = cworkspace;
cd->req_varyopt = 0;
cd->nopartial = false;

/* Set up a starting, non-extracting bracket, then compile the expression. On
error, errorcode will be set non-zero, so we don't need to look at the result
of the function here. */

ptr = (const uschar *)pattern;
code = (uschar *)codestart;
*code = OP_BRA;
(void)compile_regex(re->options, re->options & PCRE_IMS, &code, &ptr,
  &errorcode, false, 0, &firstbyte, &reqbyte, NULL, cd, NULL);
re->top_bracket = static_cast<unsigned short>(cd->bracount);
re->top_backref = static_cast<unsigned short>(cd->top_backref);

if (cd->nopartial) re->options |= PCRE_NOPARTIAL;

/* If not reached end of pattern on success, there's an excess bracket. */

if (errorcode == 0 && *ptr != 0) errorcode = ERR22;

/* Fill in the terminating state and check for disastrous overflow, but
if debugging, leave the test till after things are printed out. */

*code++ = OP_END;

if (code - codestart > length) errorcode = ERR23;

/* Fill in any forward references that are required. */

while (errorcode == 0 && cd->hwm > cworkspace)
  {
  int offset, recno;
  const uschar *groupptr;
  cd->hwm -= LINK_SIZE;
  offset = GET(cd->hwm, 0);
  recno = GET(codestart, offset);
  groupptr = find_bracket(codestart, (re->options & PCRE_UTF8) != 0, recno);
  if (groupptr == NULL) errorcode = ERR53;
    else PUT(((uschar *)codestart), offset, groupptr - codestart);
  }

/* Give an error if there's back reference to a non-existent capturing
subpattern. */

if (errorcode == 0 && re->top_backref > re->top_bracket) errorcode = ERR15;

/* Failed to compile, or error while post-processing */

if (errorcode != 0)
  {
  free(re);
  PCRE_EARLY_ERROR_RETURN:
  *erroroffset = ptr - (const uschar *)pattern;
  PCRE_EARLY_ERROR_RETURN2:
  *errorptr = error_texts[errorcode];
  if (errorcodeptr != NULL) *errorcodeptr = errorcode;
  return NULL;
  }

/* If the anchored option was not passed, set the flag if we can determine that
the pattern is anchored by virtue of ^ characters or \A or anything else (such
as starting with .* when DOTALL is set).

Otherwise, if we know what the first byte has to be, save it, because that
speeds up unanchored matches no end. If not, see if we can set the
PCRE_STARTLINE flag. This is helpful for multiline matches when all branches
start with ^. and also when all branches start with .* for non-DOTALL matches.
*/

if ((re->options & PCRE_ANCHORED) == 0)
  {
  int temp_options = re->options;   /* May get changed during these scans */
  if (is_anchored(codestart, &temp_options, 0, cd->backref_map))
    re->options |= PCRE_ANCHORED;
  else
    {
    if (firstbyte < 0)
      firstbyte = find_firstassertedchar(codestart, &temp_options, false);
    if (firstbyte >= 0)   /* Remove caseless flag for non-caseable chars */
      {
      int ch = firstbyte & 255;
      re->first_byte = static_cast<unsigned short>(((firstbyte & REQ_CASELESS) != 0 &&
         cd->fcc[ch] == ch)? ch : firstbyte);
      re->options |= PCRE_FIRSTSET;
      }
    else if (is_startline(codestart, 0, cd->backref_map))
      re->options |= PCRE_STARTLINE;
    }
  }

/* For an anchored pattern, we use the "required byte" only if it follows a
variable length item in the regex. Remove the caseless flag for non-caseable
bytes. */

if (reqbyte >= 0 &&
     ((re->options & PCRE_ANCHORED) == 0 || (reqbyte & REQ_VARY) != 0))
  {
  int ch = reqbyte & 255;
  re->req_byte = static_cast<uschar>(((reqbyte & REQ_CASELESS) != 0 &&
    cd->fcc[ch] == ch)? (reqbyte & ~REQ_CASELESS) : reqbyte);
  re->options |= PCRE_REQCHSET;
  }

return (pcre *)re;
}

/* End pcre_compile.c */
#undef NLBLOCK
#undef PSSTART
#undef PSEND
/* Begin pcre_exec.c */
/*************************************************
*          Match a back-reference                *
*************************************************/

/* If a back reference hasn't been set, the length that is passed is greater
than the number of characters left in the string, so the match fails.

Arguments:
  offset      index into the offset vector
  eptr        points into the subject
  length      length to be matched
  md          points to match data block
  ims         the ims flags

Returns:      true if matched
*/

static bool
match_ref(int offset, register USPTR eptr, int length, match_data *md,
  unsigned long int ims)
{
    USPTR p = md->start_subject + md->offset_vector[offset];

    /* Always fail if not enough characters left */

    if (length > md->end_subject - eptr)
    {
        return false;
    }

    /* Separate the caselesss case for speed */

    if ((ims & PCRE_CASELESS) != 0)
    {
        while (length-- > 0)
        {
            if (md->lcc[*p] != md->lcc[*eptr])
            {
                p++;
                eptr++;
                return false;
            }
            p++;
            eptr++;
        }
    }
    else
    {
        while (length-- > 0)
        {
            if (*p != *eptr)
            {
                p++;
                eptr++;
                return false;
            }
            p++;
            eptr++;
        }
    }

    return true;
}
/* End pcre_exec.c */

#ifdef SUPPORT_UTF8
/* Begin pcre_xclass.c */
/*************************************************
*       Match character against an XCLASS        *
*************************************************/

/* This function is called to match a character against an extended class that
might contain values > 255.

Arguments:
  c           the character
  data        points to the flag byte of the XCLASS data

Returns:      true if character matches, else false
*/

static bool
_pcre_xclass(int c, const uschar *data)
{
int t;
bool negated = (*data & XCL_NOT) != 0;

/* Character values < 256 are matched against a bitmap, if one is present. If
not, we still carry on, because there may be ranges that start below 256 in the
additional data. */

if (c < 256)
  {
  if ((*data & XCL_MAP) != 0 && (data[1 + c/8] & (1 << (c&7))) != 0)
    return !negated;   /* char found */
  }

/* First skip the bit map if present. Then match against the list of Unicode
properties or large chars or ranges that end with a large char. We won't ever
encounter XCL_PROP or XCL_NOTPROP when UCP support is not compiled. */

if ((*data++ & XCL_MAP) != 0) data += 32;

while ((t = *data++) != XCL_END)
  {
  int x, y;
  if (t == XCL_SINGLE)
    {
    GETCHARINC(x, data);
    if (c == x) return !negated;
    }
  else if (t == XCL_RANGE)
    {
    GETCHARINC(x, data);
    GETCHARINC(y, data);
    if (c >= x && c <= y) return !negated;
    }

#ifdef SUPPORT_UCP
  else  /* XCL_PROP & XCL_NOTPROP */
    {
    int chartype, script;
    int category = _pcre_ucp_findprop(c, &chartype, &script);

    switch(*data)
      {
      case PT_ANY:
      if (t == XCL_PROP) return !negated;
      break;

      case PT_LAMP:
      if ((chartype == ucp_Lu || chartype == ucp_Ll || chartype == ucp_Lt) ==
          (t == XCL_PROP)) return !negated;
      break;

      case PT_GC:
      if ((data[1] == category) == (t == XCL_PROP)) return !negated;
      break;

      case PT_PC:
      if ((data[1] == chartype) == (t == XCL_PROP)) return !negated;
      break;

      case PT_SC:
      if ((data[1] == script) == (t == XCL_PROP)) return !negated;
      break;

      /* This should never occur, but compilers may mutter if there is no
      default. */

      default:
      return false;
      }

    data += 2;
    }
#endif  /* SUPPORT_UCP */
  }

return negated;   /* char did not match */
}
/* End pcre_xclass.c */
#endif
#define NLBLOCK md
#define PSSTART start_subject  /* Field containing processed string start */
#define PSEND   end_subject    /* Field containing processed string end */
/* Begin pcre_exec.c */


/***************************************************************************
****************************************************************************
                   RECURSION IN THE match() FUNCTION

The match() function is highly recursive, though not every recursive call
increases the recursive depth. Nevertheless, some regular expressions can cause
it to recurse to a great depth. I was writing for Unix, so I just let it call
itself recursively. This uses the stack for saving everything that has to be
saved for a recursive call. On Unix, the stack can be large, and this works
fine.

It turns out that on some non-Unix-like systems there are problems with
programs that use a lot of stack. (This despite the fact that every last chip
has oodles of memory these days, and techniques for extending the stack have
been known for decades.) So....

There is a fudge, triggered by defining NO_RECURSE, which avoids recursive
calls by keeping local variables that need to be preserved in blocks of memory
obtained from malloc() instead instead of on the stack. Macros are used to
achieve this so that the actual code doesn't look very different to what it
always used to.
****************************************************************************
***************************************************************************/


/* These versions of the macros use the stack, as normal. There are debugging
versions and production versions. */

#define REGISTER register
#define RMATCH(rx,ra,rb,rc,rd,re,rf,rg) \
  rx = match(ra,rb,rc,rd,re,rf,rg,rdepth+1)
#define RRETURN(ra) return ra


/***************************************************************************
***************************************************************************/



/*************************************************
*         Match from current position            *
*************************************************/

/* This function is called recursively in many circumstances. Whenever it
returns a negative (error) response, the outer incarnation must also return the
same response.

Performance note: It might be tempting to extract commonly used fields from the
md structure (e.g. utf8, end_subject) into individual variables to improve
performance. Tests using gcc on a SPARC disproved this; in the first case, it
made performance worse.

Arguments:
   eptr        pointer to current character in subject
   ecode       pointer to current position in compiled code
   offset_top  current top pointer
   md          pointer to "static" info for the match
   ims         current /i, /m, and /s options
   eptrb       pointer to chain of blocks containing eptr at start of
                 brackets - for testing for empty matches
   flags       can contain
                 match_condassert - this is an assertion condition
                 match_cbegroup - this is the start of an unlimited repeat
                   group that can match an empty string
                 match_tail_recursed - this is a tail_recursed group
   rdepth      the recursion depth

Returns:       MATCH_MATCH if matched            )  these values are >= 0
               MATCH_NOMATCH if failed to match  )
               a negative PCRE_ERROR_xxx value if aborted by an error condition
                 (e.g. stopped by repeated call or recursion limit)
*/

static int
match(REGISTER USPTR eptr, REGISTER const uschar *ecode,
  int offset_top, match_data *md, unsigned long int ims, eptrblock *eptrb,
  int flags, unsigned int rdepth)
{
/* These variables do not need to be preserved over recursion in this function,
so they can be ordinary variables in all cases. Mark some of them with
"register" because they are used a lot in loops. */

register int  rrc;         /* Returns from recursive calls */
register int  i;           /* Used for loops not involving calls to RMATCH() */
register unsigned int c;   /* Character values not kept over RMATCH() calls */
register bool utf8;        /* Local copy of UTF-8 flag for speed */

bool minimize, possessive; /* Quantifier options */

/* When recursion is not being used, all "local" variables that have to be
preserved over calls to RMATCH() are part of a "frame" which is obtained from
heap storage. Set up the top-level frame here; others are obtained from the
heap whenever RMATCH() does a "recursion". See the macro definitions above. */

#define fi i
#define fc c


#ifdef SUPPORT_UTF8                /* Many of these variables are used only  */
const uschar *charptr;             /* in small blocks of the code. My normal */
#endif                             /* style of coding would have declared    */
const uschar *callpat;             /* them within each of those blocks.      */
const uschar *data;                /* However, in order to accommodate the   */
const uschar *next;                /* version of this code that uses an      */
USPTR         pp;                  /* external "stack" implemented on the    */
const uschar *prev;                /* heap, it is easier to declare them all */
USPTR         saved_eptr;          /* here, so the declarations can be cut   */
                                   /* out in a block. The only declarations  */
recursion_info new_recursive;      /* within blocks below are for variables  */
                                   /* that do not have to be preserved over  */
bool cur_is_word;                  /* a recursive call to RMATCH().          */
bool condition;
bool prev_is_word;

unsigned long int original_ims;

#ifdef SUPPORT_UCP
int prop_type;
int prop_value;
int prop_fail_result;
int prop_category;
int prop_chartype;
int prop_script;
int oclength;
uschar occhars[8];
#endif

int ctype;
int length;
int max;
int min;
int number;
int offset;
int op;
int save_capture_last;
int save_offset1, save_offset2, save_offset3;
int stacksave[REC_STACK_SAVE_MAX];

eptrblock newptrb;

/* These statements are here to stop the compiler complaining about unitialized
variables. */

#ifdef SUPPORT_UCP
prop_value = 0;
prop_fail_result = 0;
#endif

/* This label is used for tail recursion, which is used in a few cases even
when NO_RECURSE is not defined, in order to reduce the amount of stack that is
used. Thanks to Ian Taylor for noticing this possibility and sending the
original patch. */

TAIL_RECURSE:

/* OK, now we can get on with the real code of the function. Recursive calls
are specified by the macro RMATCH and RRETURN is used to return. When
NO_RECURSE is *not* defined, these just turn into a recursive call to match()
and a "return", respectively (possibly with some debugging if DEBUG is
defined). However, RMATCH isn't like a function call because it's quite a
complicated macro. It has to be used in one particular way. This shouldn't,
however, impact performance when true recursion is being used. */

/* First check that we haven't called match() too many times, or that we
haven't exceeded the recursive call limit. */

if (md->match_call_count++ >= md->match_limit) RRETURN(PCRE_ERROR_MATCHLIMIT);
if (rdepth >= md->match_limit_recursion) RRETURN(PCRE_ERROR_RECURSIONLIMIT);

original_ims = ims;    /* Save for resetting on ')' */

#ifdef SUPPORT_UTF8
utf8 = md->utf8;       /* Local copy of the flag */
#else
utf8 = false;
#endif

/* At the start of a group with an unlimited repeat that may match an empty
string, the match_cbegroup flag is set. When this is the case, add the current
subject pointer to the chain of such remembered pointers, to be checked when we
hit the closing ket, in order to break infinite loops that match no characters.
When match() is called in other circumstances, don't add to the chain. If this
is a tail recursion, use a block from the workspace, as the one on the stack is
already used. */

if ((flags & match_cbegroup) != 0)
  {
  eptrblock *p;
  if ((flags & match_tail_recursed) != 0)
    {
    if (md->eptrn >= EPTR_WORK_SIZE) RRETURN(PCRE_ERROR_NULLWSLIMIT);
    p = md->eptrchain + md->eptrn++;
    }
  else p = &newptrb;
  p->epb_saved_eptr = eptr;
  p->epb_prev = eptrb;
  eptrb = p;
  }

/* Now start processing the opcodes. */

for (;!alarm_clock.alarmed;)
  {
  minimize = possessive = false;
  op = *ecode;

  /* For partial matching, remember if we ever hit the end of the subject after
  matching at least one subject character. */

  if (md->partial &&
      eptr >= md->end_subject &&
      eptr > md->start_match)
    md->hitend = true;

  switch(op)
    {
    /* Handle a capturing bracket. If there is space in the offset vector, save
    the current subject position in the working slot at the top of the vector.
    We mustn't change the current values of the data slot, because they may be
    set from a previous iteration of this group, and be referred to by a
    reference inside the group.

    If the bracket fails to match, we need to restore this value and also the
    values of the final offsets, in case they were set by a previous iteration
    of the same bracket.

    If there isn't enough space in the offset vector, treat this as if it were
    a non-capturing bracket. Don't worry about setting the flag for the error
    case here; that is handled in the code for KET. */

    case OP_CBRA:
    case OP_SCBRA:
    number = GET2(ecode, 1+LINK_SIZE);
    offset = number << 1;

    if (offset < md->offset_max)
      {
      save_offset1 = md->offset_vector[offset];
      save_offset2 = md->offset_vector[offset+1];
      save_offset3 = md->offset_vector[md->offset_end - number];
      save_capture_last = md->capture_last;

      DPRINTF(("saving %d %d %d\n", save_offset1, save_offset2, save_offset3));
      md->offset_vector[md->offset_end - number] = eptr - md->start_subject;

      flags = (op == OP_SCBRA)? match_cbegroup : 0;
      do
        {
        RMATCH(rrc, eptr, ecode + _pcre_OP_lengths[*ecode], offset_top, md,
          ims, eptrb, flags);
        if (rrc != MATCH_NOMATCH) RRETURN(rrc);
        md->capture_last = save_capture_last;
        ecode += GET(ecode, 1);
        }
      while (*ecode == OP_ALT);

      DPRINTF(("bracket %d failed\n", number));

      md->offset_vector[offset] = save_offset1;
      md->offset_vector[offset+1] = save_offset2;
      md->offset_vector[md->offset_end - number] = save_offset3;

      RRETURN(MATCH_NOMATCH);
      }

    /* Insufficient room for saving captured contents. Treat as a non-capturing
    bracket. */

    DPRINTF(("insufficient capture room: treat as non-capturing\n"));

    /* Non-capturing bracket. Loop for all the alternatives. When we get to the
    final alternative within the brackets, we would return the result of a
    recursive call to match() whatever happened. We can reduce stack usage by
    turning this into a tail recursion. */

    case OP_BRA:
    case OP_SBRA:
    DPRINTF(("start non-capturing bracket\n"));
    flags = (op >= OP_SBRA)? match_cbegroup : 0;
    for (;;)
      {
      if (ecode[GET(ecode, 1)] != OP_ALT)
        {
        ecode += _pcre_OP_lengths[*ecode];
        flags |= match_tail_recursed;
        DPRINTF(("bracket 0 tail recursion\n"));
        goto TAIL_RECURSE;
        }

      /* For non-final alternatives, continue the loop for a NOMATCH result;
      otherwise return. */

      RMATCH(rrc, eptr, ecode + _pcre_OP_lengths[*ecode], offset_top, md, ims,
        eptrb, flags);
      if (rrc != MATCH_NOMATCH) RRETURN(rrc);
      ecode += GET(ecode, 1);
      }
    /* Control never reaches here. */

    /* Conditional group: compilation checked that there are no more than
    two branches. If the condition is false, skipping the first branch takes us
    past the end if there is only one branch, but that's OK because that is
    exactly what going to the ket would do. As there is only one branch to be
    obeyed, we can use tail recursion to avoid using another stack frame. */

    case OP_COND:
    case OP_SCOND:
    if (ecode[LINK_SIZE+1] == OP_RREF)         /* Recursion test */
      {
      offset = GET2(ecode, LINK_SIZE + 2);     /* Recursion group number*/
      condition = md->recursive != NULL &&
        (offset == RREF_ANY || offset == md->recursive->group_num);
      ecode += condition? 3 : GET(ecode, 1);
      }

    else if (ecode[LINK_SIZE+1] == OP_CREF)    /* Group used test */
      {
      offset = GET2(ecode, LINK_SIZE+2) << 1;  /* Doubled ref number */
      condition = offset < offset_top && md->offset_vector[offset] >= 0;
      ecode += condition? 3 : GET(ecode, 1);
      }

    else if (ecode[LINK_SIZE+1] == OP_DEF)     /* DEFINE - always false */
      {
      condition = false;
      ecode += GET(ecode, 1);
      }

    /* The condition is an assertion. Call match() to evaluate it - setting
    the final argument match_condassert causes it to stop at the end of an
    assertion. */

    else
      {
      RMATCH(rrc, eptr, ecode + 1 + LINK_SIZE, offset_top, md, ims, NULL,
          match_condassert);
      if (rrc == MATCH_MATCH)
        {
        condition = true;
        ecode += 1 + LINK_SIZE + GET(ecode, LINK_SIZE + 2);
        while (*ecode == OP_ALT) ecode += GET(ecode, 1);
        }
      else if (rrc != MATCH_NOMATCH)
        {
        RRETURN(rrc);         /* Need braces because of following else */
        }
      else
        {
        condition = false;
        ecode += GET(ecode, 1);
        }
      }

    /* We are now at the branch that is to be obeyed. As there is only one,
    we can use tail recursion to avoid using another stack frame. If the second
    alternative doesn't exist, we can just plough on. */

    if (condition || *ecode == OP_ALT)
      {
      ecode += 1 + LINK_SIZE;
      flags = match_tail_recursed | ((op == OP_SCOND)? match_cbegroup : 0);
      goto TAIL_RECURSE;
      }
    else
      {
      ecode += 1 + LINK_SIZE;
      }
    break;

    /* End of the pattern. If we are in a top-level recursion, we should
    restore the offsets appropriately and continue from after the call. */

    case OP_END:
    if (md->recursive != NULL && md->recursive->group_num == 0)
      {
      recursion_info *rec = md->recursive;
      DPRINTF(("End of pattern in a (?0) recursion\n"));
      md->recursive = rec->prevrec;
      memmove(md->offset_vector, rec->offset_save,
        rec->saved_max * sizeof(int));
      md->start_match = rec->save_start;
      ims = original_ims;
      ecode = rec->after_call;
      break;
      }

    /* Otherwise, if PCRE_NOTEMPTY is set, fail if we have matched an empty
    string - backtracking will then try other alternatives, if any. */

    if (md->notempty && eptr == md->start_match) RRETURN(MATCH_NOMATCH);
    md->end_match_ptr = eptr;          /* Record where we ended */
    md->end_offset_top = offset_top;   /* and how many extracts were taken */
    RRETURN(MATCH_MATCH);

    /* Change option settings */

    case OP_OPT:
    ims = ecode[1];
    ecode += 2;
    DPRINTF(("ims set to %02lx\n", ims));
    break;

    /* Assertion brackets. Check the alternative branches in turn - the
    matching won't pass the KET for an assertion. If any one branch matches,
    the assertion is true. Lookbehind assertions have an OP_REVERSE item at the
    start of each branch to move the current point backwards, so the code at
    this level is identical to the lookahead case. */

    case OP_ASSERT:
    case OP_ASSERTBACK:
    do
      {
      RMATCH(rrc, eptr, ecode + 1 + LINK_SIZE, offset_top, md, ims, NULL, 0);
      if (rrc == MATCH_MATCH) break;
      if (rrc != MATCH_NOMATCH) RRETURN(rrc);
      ecode += GET(ecode, 1);
      }
    while (*ecode == OP_ALT);
    if (*ecode == OP_KET) RRETURN(MATCH_NOMATCH);

    /* If checking an assertion for a condition, return MATCH_MATCH. */

    if ((flags & match_condassert) != 0) RRETURN(MATCH_MATCH);

    /* Continue from after the assertion, updating the offsets high water
    mark, since extracts may have been taken during the assertion. */

    do ecode += GET(ecode,1); while (*ecode == OP_ALT);
    ecode += 1 + LINK_SIZE;
    offset_top = md->end_offset_top;
    continue;

    /* Negative assertion: all branches must fail to match */

    case OP_ASSERT_NOT:
    case OP_ASSERTBACK_NOT:
    do
      {
      RMATCH(rrc, eptr, ecode + 1 + LINK_SIZE, offset_top, md, ims, NULL, 0);
      if (rrc == MATCH_MATCH) RRETURN(MATCH_NOMATCH);
      if (rrc != MATCH_NOMATCH) RRETURN(rrc);
      ecode += GET(ecode,1);
      }
    while (*ecode == OP_ALT);

    if ((flags & match_condassert) != 0) RRETURN(MATCH_MATCH);

    ecode += 1 + LINK_SIZE;
    continue;

    /* Move the subject pointer back. This occurs only at the start of
    each branch of a lookbehind assertion. If we are too close to the start to
    move back, this match function fails. When working with UTF-8 we move
    back a number of characters, not bytes. */

    case OP_REVERSE:
#ifdef SUPPORT_UTF8
    if (utf8)
      {
      i = GET(ecode, 1);
      while (i-- > 0)
        {
        eptr--;
        if (eptr < md->start_subject) RRETURN(MATCH_NOMATCH);
        BACKCHAR(eptr)
        }
      }
    else
#endif

    /* No UTF-8 support, or not in UTF-8 mode: count is byte count */

      {
      eptr -= GET(ecode, 1);
      if (eptr < md->start_subject) RRETURN(MATCH_NOMATCH);
      }

    /* Skip to next op code */

    ecode += 1 + LINK_SIZE;
    break;

    /* The callout item calls an external function, if one is provided, passing
    details of the match so far. This is mainly for debugging, though the
    function is able to force a failure. */

    case OP_CALLOUT:
    if (pcre_callout != NULL)
      {
      pcre_callout_block cb;
      cb.version          = 1;   /* Version 1 of the callout block */
      cb.callout_number   = ecode[1];
      cb.offset_vector    = md->offset_vector;
      cb.subject          = (PCRE_SPTR)md->start_subject;
      cb.subject_length   = md->end_subject - md->start_subject;
      cb.start_match      = md->start_match - md->start_subject;
      cb.current_position = eptr - md->start_subject;
      cb.pattern_position = GET(ecode, 2);
      cb.next_item_length = GET(ecode, 2 + LINK_SIZE);
      cb.capture_top      = offset_top/2;
      cb.capture_last     = md->capture_last;
      cb.callout_data     = md->callout_data;
      if ((rrc = (*pcre_callout)(&cb)) > 0) RRETURN(MATCH_NOMATCH);
      if (rrc < 0) RRETURN(rrc);
      }
    ecode += 2 + 2*LINK_SIZE;
    break;

    /* Recursion either matches the current regex, or some subexpression. The
    offset data is the offset to the starting bracket from the start of the
    whole pattern. (This is so that it works from duplicated subpatterns.)

    If there are any capturing brackets started but not finished, we have to
    save their starting points and reinstate them after the recursion. However,
    we don't know how many such there are (offset_top records the completed
    total) so we just have to save all the potential data. There may be up to
    65535 such values, which is too large to put on the stack, but using malloc
    for small numbers seems expensive. As a compromise, the stack is used when
    there are no more than REC_STACK_SAVE_MAX values to store; otherwise malloc
    is used. A problem is what to do if the malloc fails ... there is no way of
    returning to the top level with an error. Save the top REC_STACK_SAVE_MAX
    values on the stack, and accept that the rest may be wrong.

    There are also other values that have to be saved. We use a chained
    sequence of blocks that actually live on the stack. Thanks to Robin Houston
    for the original version of this logic. */

    case OP_RECURSE:
      {
      callpat = md->start_code + GET(ecode, 1);
      new_recursive.group_num = (callpat == md->start_code)? 0 :
        GET2(callpat, 1 + LINK_SIZE);

      /* Add to "recursing stack" */

      new_recursive.prevrec = md->recursive;
      md->recursive = &new_recursive;

      /* Find where to continue from afterwards */

      ecode += 1 + LINK_SIZE;
      new_recursive.after_call = ecode;

      /* Now save the offset data. */

      new_recursive.saved_max = md->offset_end;
      if (new_recursive.saved_max <= REC_STACK_SAVE_MAX)
        new_recursive.offset_save = stacksave;
      else
        {
        new_recursive.offset_save =
          static_cast<int *>(malloc(new_recursive.saved_max * sizeof(int)));
        if (new_recursive.offset_save == NULL) RRETURN(PCRE_ERROR_NOMEMORY);
        }

      memcpy(new_recursive.offset_save, md->offset_vector,
            new_recursive.saved_max * sizeof(int));
      new_recursive.save_start = md->start_match;
      md->start_match = eptr;

      /* OK, now we can do the recursion. For each top-level alternative we
      restore the offset and recursion data. */

      DPRINTF(("Recursing into group %d\n", new_recursive.group_num));
      flags = (*callpat >= OP_SBRA)? match_cbegroup : 0;
      do
        {
        RMATCH(rrc, eptr, callpat + _pcre_OP_lengths[*callpat], offset_top,
          md, ims, eptrb, flags);
        if (rrc == MATCH_MATCH)
          {
          DPRINTF(("Recursion matched\n"));
          md->recursive = new_recursive.prevrec;
          if (new_recursive.offset_save != stacksave)
            free(new_recursive.offset_save);
          RRETURN(MATCH_MATCH);
          }
        else if (rrc != MATCH_NOMATCH)
          {
          DPRINTF(("Recursion gave error %d\n", rrc));
          RRETURN(rrc);
          }

        md->recursive = &new_recursive;
        memcpy(md->offset_vector, new_recursive.offset_save,
            new_recursive.saved_max * sizeof(int));
        callpat += GET(callpat, 1);
        }
      while (*callpat == OP_ALT);

      DPRINTF(("Recursion didn't match\n"));
      md->recursive = new_recursive.prevrec;
      if (new_recursive.offset_save != stacksave)
        free(new_recursive.offset_save);
      RRETURN(MATCH_NOMATCH);
      }
    /* Control never reaches here */

    /* "Once" brackets are like assertion brackets except that after a match,
    the point in the subject string is not moved back. Thus there can never be
    a move back into the brackets. Friedl calls these "atomic" subpatterns.
    Check the alternative branches in turn - the matching won't pass the KET
    for this kind of subpattern. If any one branch matches, we carry on as at
    the end of a normal bracket, leaving the subject pointer. */

    case OP_ONCE:
    prev = ecode;
    saved_eptr = eptr;

    do
      {
      RMATCH(rrc, eptr, ecode + 1 + LINK_SIZE, offset_top, md, ims,
        eptrb, 0);
      if (rrc == MATCH_MATCH) break;
      if (rrc != MATCH_NOMATCH) RRETURN(rrc);
      ecode += GET(ecode,1);
      }
    while (*ecode == OP_ALT);

    /* If hit the end of the group (which could be repeated), fail */

    if (*ecode != OP_ONCE && *ecode != OP_ALT) RRETURN(MATCH_NOMATCH);

    /* Continue as from after the assertion, updating the offsets high water
    mark, since extracts may have been taken. */

    do ecode += GET(ecode, 1); while (*ecode == OP_ALT);

    offset_top = md->end_offset_top;
    eptr = md->end_match_ptr;

    /* For a non-repeating ket, just continue at this level. This also
    happens for a repeating ket if no characters were matched in the group.
    This is the forcible breaking of infinite loops as implemented in Perl
    5.005. If there is an options reset, it will get obeyed in the normal
    course of events. */

    if (*ecode == OP_KET || eptr == saved_eptr)
      {
      ecode += 1+LINK_SIZE;
      break;
      }

    /* The repeating kets try the rest of the pattern or restart from the
    preceding bracket, in the appropriate order. The second "call" of match()
    uses tail recursion, to avoid using another stack frame. We need to reset
    any options that changed within the bracket before re-running it, so
    check the next opcode. */

    if (ecode[1+LINK_SIZE] == OP_OPT)
      {
      ims = (ims & ~PCRE_IMS) | ecode[4];
      DPRINTF(("ims set to %02lx at group repeat\n", ims));
      }

    if (*ecode == OP_KETRMIN)
      {
      RMATCH(rrc, eptr, ecode + 1 + LINK_SIZE, offset_top, md, ims, eptrb, 0);
      if (rrc != MATCH_NOMATCH) RRETURN(rrc);
      ecode = prev;
      flags = match_tail_recursed;
      goto TAIL_RECURSE;
      }
    else  /* OP_KETRMAX */
      {
      RMATCH(rrc, eptr, prev, offset_top, md, ims, eptrb, match_cbegroup);
      if (rrc != MATCH_NOMATCH) RRETURN(rrc);
      ecode += 1 + LINK_SIZE;
      flags = match_tail_recursed;
      goto TAIL_RECURSE;
      }
    /* Control never gets here */

    /* An alternation is the end of a branch; scan along to find the end of the
    bracketed group and go to there. */

    case OP_ALT:
    do ecode += GET(ecode,1); while (*ecode == OP_ALT);
    break;

    /* BRAZERO and BRAMINZERO occur just before a bracket group, indicating
    that it may occur zero times. It may repeat infinitely, or not at all -
    i.e. it could be ()* or ()? in the pattern. Brackets with fixed upper
    repeat limits are compiled as a number of copies, with the optional ones
    preceded by BRAZERO or BRAMINZERO. */

    case OP_BRAZERO:
      {
      next = ecode+1;
      RMATCH(rrc, eptr, next, offset_top, md, ims, eptrb, 0);
      if (rrc != MATCH_NOMATCH) RRETURN(rrc);
      do next += GET(next,1); while (*next == OP_ALT);
      ecode = next + 1 + LINK_SIZE;
      }
    break;

    case OP_BRAMINZERO:
      {
      next = ecode+1;
      do next += GET(next, 1); while (*next == OP_ALT);
      RMATCH(rrc, eptr, next + 1+LINK_SIZE, offset_top, md, ims, eptrb, 0);
      if (rrc != MATCH_NOMATCH) RRETURN(rrc);
      ecode++;
      }
    break;

    /* End of a group, repeated or non-repeating. */

    case OP_KET:
    case OP_KETRMIN:
    case OP_KETRMAX:
    prev = ecode - GET(ecode, 1);

    /* If this was a group that remembered the subject start, in order to break
    infinite repeats of empty string matches, retrieve the subject start from
    the chain. Otherwise, set it NULL. */

    if (*prev >= OP_SBRA)
      {
      saved_eptr = eptrb->epb_saved_eptr;   /* Value at start of group */
      eptrb = eptrb->epb_prev;              /* Backup to previous group */
      }
    else saved_eptr = NULL;

    /* If we are at the end of an assertion group, stop matching and return
    MATCH_MATCH, but record the current high water mark for use by positive
    assertions. Do this also for the "once" (atomic) groups. */

    if (*prev == OP_ASSERT || *prev == OP_ASSERT_NOT ||
        *prev == OP_ASSERTBACK || *prev == OP_ASSERTBACK_NOT ||
        *prev == OP_ONCE)
      {
      md->end_match_ptr = eptr;      /* For ONCE */
      md->end_offset_top = offset_top;
      RRETURN(MATCH_MATCH);
      }

    /* For capturing groups we have to check the group number back at the start
    and if necessary complete handling an extraction by setting the offsets and
    bumping the high water mark. Note that whole-pattern recursion is coded as
    a recurse into group 0, so it won't be picked up here. Instead, we catch it
    when the OP_END is reached. Other recursion is handled here. */

    if (*prev == OP_CBRA || *prev == OP_SCBRA)
      {
      number = GET2(prev, 1+LINK_SIZE);
      offset = number << 1;

      md->capture_last = number;
      if (offset >= md->offset_max) md->offset_overflow = true; else
        {
        md->offset_vector[offset] =
          md->offset_vector[md->offset_end - number];
        md->offset_vector[offset+1] = eptr - md->start_subject;
        if (offset_top <= offset) offset_top = offset + 2;
        }

      /* Handle a recursively called group. Restore the offsets
      appropriately and continue from after the call. */

      if (md->recursive != NULL && md->recursive->group_num == number)
        {
        recursion_info *rec = md->recursive;
        DPRINTF(("Recursion (%d) succeeded - continuing\n", number));
        md->recursive = rec->prevrec;
        md->start_match = rec->save_start;
        memcpy(md->offset_vector, rec->offset_save,
          rec->saved_max * sizeof(int));
        ecode = rec->after_call;
        ims = original_ims;
        break;
        }
      }

    /* For both capturing and non-capturing groups, reset the value of the ims
    flags, in case they got changed during the group. */

    ims = original_ims;
    DPRINTF(("ims reset to %02lx\n", ims));

    /* For a non-repeating ket, just continue at this level. This also
    happens for a repeating ket if no characters were matched in the group.
    This is the forcible breaking of infinite loops as implemented in Perl
    5.005. If there is an options reset, it will get obeyed in the normal
    course of events. */

    if (*ecode == OP_KET || eptr == saved_eptr)
      {
      ecode += 1 + LINK_SIZE;
      break;
      }

    /* The repeating kets try the rest of the pattern or restart from the
    preceding bracket, in the appropriate order. In the second case, we can use
    tail recursion to avoid using another stack frame. */

    flags = (*prev >= OP_SBRA)? match_cbegroup : 0;

    if (*ecode == OP_KETRMIN)
      {
      RMATCH(rrc, eptr, ecode + 1+LINK_SIZE, offset_top, md, ims, eptrb, 0);
      if (rrc != MATCH_NOMATCH) RRETURN(rrc);
      ecode = prev;
      flags |= match_tail_recursed;
      goto TAIL_RECURSE;
      }
    else  /* OP_KETRMAX */
      {
      RMATCH(rrc, eptr, prev, offset_top, md, ims, eptrb, flags);
      if (rrc != MATCH_NOMATCH) RRETURN(rrc);
      ecode += 1 + LINK_SIZE;
      flags = match_tail_recursed;
      goto TAIL_RECURSE;
      }
    /* Control never gets here */

    /* Start of subject unless notbol, or after internal newline if multiline */

    case OP_CIRC:
    if (md->notbol && eptr == md->start_subject) RRETURN(MATCH_NOMATCH);
    if ((ims & PCRE_MULTILINE) != 0)
      {
      if (eptr != md->start_subject &&
          (eptr == md->end_subject || !WAS_NEWLINE(eptr)))
        RRETURN(MATCH_NOMATCH);
      ecode++;
      break;
      }
    /* ... else fall through */

    /* Start of subject assertion */

    case OP_SOD:
    if (eptr != md->start_subject) RRETURN(MATCH_NOMATCH);
    ecode++;
    break;

    /* Start of match assertion */

    case OP_SOM:
    if (eptr != md->start_subject + md->start_offset) RRETURN(MATCH_NOMATCH);
    ecode++;
    break;

    /* Assert before internal newline if multiline, or before a terminating
    newline unless endonly is set, else end of subject unless noteol is set. */

    case OP_DOLL:
    if ((ims & PCRE_MULTILINE) != 0)
      {
      if (eptr < md->end_subject)
        { if (!IS_NEWLINE(eptr)) RRETURN(MATCH_NOMATCH); }
      else
        { if (md->noteol) RRETURN(MATCH_NOMATCH); }
      ecode++;
      break;
      }
    else
      {
      if (md->noteol) RRETURN(MATCH_NOMATCH);
      if (!md->endonly)
        {
        if (eptr != md->end_subject &&
            (!IS_NEWLINE(eptr) || eptr != md->end_subject - md->nllen))
          RRETURN(MATCH_NOMATCH);
        ecode++;
        break;
        }
      }
    /* ... else fall through for endonly */

    /* End of subject assertion (\z) */

    case OP_EOD:
    if (eptr < md->end_subject) RRETURN(MATCH_NOMATCH);
    ecode++;
    break;

    /* End of subject or ending \n assertion (\Z) */

    case OP_EODN:
    if (eptr != md->end_subject &&
        (!IS_NEWLINE(eptr) || eptr != md->end_subject - md->nllen))
      RRETURN(MATCH_NOMATCH);
    ecode++;
    break;

    /* Word boundary assertions */

    case OP_NOT_WORD_BOUNDARY:
    case OP_WORD_BOUNDARY:
      {

      /* Find out if the previous and current characters are "word" characters.
      It takes a bit more work in UTF-8 mode. Characters > 255 are assumed to
      be "non-word" characters. */

#ifdef SUPPORT_UTF8
      if (utf8)
        {
        if (eptr == md->start_subject) prev_is_word = false; else
          {
          const uschar *lastptr = eptr - 1;
          while((*lastptr & 0xc0) == 0x80) lastptr--;
          GETCHAR(c, lastptr);
          prev_is_word = c < 256 && (md->ctypes[c] & ctype_word) != 0;
          }
        if (eptr >= md->end_subject) cur_is_word = false; else
          {
          GETCHAR(c, eptr);
          cur_is_word = c < 256 && (md->ctypes[c] & ctype_word) != 0;
          }
        }
      else
#endif

      /* More streamlined when not in UTF-8 mode */

        {
        prev_is_word = (eptr != md->start_subject) &&
          ((md->ctypes[eptr[-1]] & ctype_word) != 0);
        cur_is_word = (eptr < md->end_subject) &&
          ((md->ctypes[*eptr] & ctype_word) != 0);
        }

      /* Now see if the situation is what we want */

      if ((*ecode++ == OP_WORD_BOUNDARY)?
           cur_is_word == prev_is_word : cur_is_word != prev_is_word)
        RRETURN(MATCH_NOMATCH);
      }
    break;

    /* Match a single character type; inline for speed */

    case OP_ANY:
    if ((ims & PCRE_DOTALL) == 0)
      {
      if (IS_NEWLINE(eptr)) RRETURN(MATCH_NOMATCH);
      }
    if (eptr++ >= md->end_subject) RRETURN(MATCH_NOMATCH);
    if (utf8)
      while (eptr < md->end_subject && (*eptr & 0xc0) == 0x80) eptr++;
    ecode++;
    break;

    /* Match a single byte, even in UTF-8 mode. This opcode really does match
    any byte, even newline, independent of the setting of PCRE_DOTALL. */

    case OP_ANYBYTE:
    if (eptr++ >= md->end_subject) RRETURN(MATCH_NOMATCH);
    ecode++;
    break;

    case OP_NOT_DIGIT:
    if (eptr >= md->end_subject) RRETURN(MATCH_NOMATCH);
    GETCHARINCTEST(c, eptr);
    if (
#ifdef SUPPORT_UTF8
       c < 256 &&
#endif
       (md->ctypes[c] & ctype_digit) != 0
       )
      RRETURN(MATCH_NOMATCH);
    ecode++;
    break;

    case OP_DIGIT:
    if (eptr >= md->end_subject) RRETURN(MATCH_NOMATCH);
    GETCHARINCTEST(c, eptr);
    if (
#ifdef SUPPORT_UTF8
       c >= 256 ||
#endif
       (md->ctypes[c] & ctype_digit) == 0
       )
      RRETURN(MATCH_NOMATCH);
    ecode++;
    break;

    case OP_NOT_WHITESPACE:
    if (eptr >= md->end_subject) RRETURN(MATCH_NOMATCH);
    GETCHARINCTEST(c, eptr);
    if (
#ifdef SUPPORT_UTF8
       c < 256 &&
#endif
       (md->ctypes[c] & ctype_space) != 0
       )
      RRETURN(MATCH_NOMATCH);
    ecode++;
    break;

    case OP_WHITESPACE:
    if (eptr >= md->end_subject) RRETURN(MATCH_NOMATCH);
    GETCHARINCTEST(c, eptr);
    if (
#ifdef SUPPORT_UTF8
       c >= 256 ||
#endif
       (md->ctypes[c] & ctype_space) == 0
       )
      RRETURN(MATCH_NOMATCH);
    ecode++;
    break;

    case OP_NOT_WORDCHAR:
    if (eptr >= md->end_subject) RRETURN(MATCH_NOMATCH);
    GETCHARINCTEST(c, eptr);
    if (
#ifdef SUPPORT_UTF8
       c < 256 &&
#endif
       (md->ctypes[c] & ctype_word) != 0
       )
      RRETURN(MATCH_NOMATCH);
    ecode++;
    break;

    case OP_WORDCHAR:
    if (eptr >= md->end_subject) RRETURN(MATCH_NOMATCH);
    GETCHARINCTEST(c, eptr);
    if (
#ifdef SUPPORT_UTF8
       c >= 256 ||
#endif
       (md->ctypes[c] & ctype_word) == 0
       )
      RRETURN(MATCH_NOMATCH);
    ecode++;
    break;

    case OP_ANYNL:
    if (eptr >= md->end_subject) RRETURN(MATCH_NOMATCH);
    GETCHARINCTEST(c, eptr);
    switch(c)
      {
      default: RRETURN(MATCH_NOMATCH);
      case 0x000d:
      if (eptr < md->end_subject && *eptr == 0x0a) eptr++;
      break;
      case 0x000a:
      case 0x000b:
      case 0x000c:
      case 0x0085:
      case 0x2028:
      case 0x2029:
      break;
      }
    ecode++;
    break;

#ifdef SUPPORT_UCP
    /* Check the next character by Unicode property. We will get here only
    if the support is in the binary; otherwise a compile-time error occurs. */

    case OP_PROP:
    case OP_NOTPROP:
    if (eptr >= md->end_subject) RRETURN(MATCH_NOMATCH);
    GETCHARINCTEST(c, eptr);
      {
      int chartype, script;
      int category = _pcre_ucp_findprop(c, &chartype, &script);

      switch(ecode[1])
        {
        case PT_ANY:
        if (op == OP_NOTPROP) RRETURN(MATCH_NOMATCH);
        break;

        case PT_LAMP:
        if ((chartype == ucp_Lu ||
             chartype == ucp_Ll ||
             chartype == ucp_Lt) == (op == OP_NOTPROP))
          RRETURN(MATCH_NOMATCH);
         break;

        case PT_GC:
        if ((ecode[2] != category) == (op == OP_PROP))
          RRETURN(MATCH_NOMATCH);
        break;

        case PT_PC:
        if ((ecode[2] != chartype) == (op == OP_PROP))
          RRETURN(MATCH_NOMATCH);
        break;

        case PT_SC:
        if ((ecode[2] != script) == (op == OP_PROP))
          RRETURN(MATCH_NOMATCH);
        break;

        default:
        RRETURN(PCRE_ERROR_INTERNAL);
        }

      ecode += 3;
      }
    break;

    /* Match an extended Unicode sequence. We will get here only if the support
    is in the binary; otherwise a compile-time error occurs. */

    case OP_EXTUNI:
    if (eptr >= md->end_subject) RRETURN(MATCH_NOMATCH);
    GETCHARINCTEST(c, eptr);
      {
      int chartype, script;
      int category = _pcre_ucp_findprop(c, &chartype, &script);
      if (category == ucp_M) RRETURN(MATCH_NOMATCH);
      while (eptr < md->end_subject)
        {
        int len = 1;
        if (!utf8) c = *eptr; else
          {
          GETCHARLEN(c, eptr, len);
          }
        category = _pcre_ucp_findprop(c, &chartype, &script);
        if (category != ucp_M) break;
        eptr += len;
        }
      }
    ecode++;
    break;
#endif


    /* Match a back reference, possibly repeatedly. Look past the end of the
    item to see if there is repeat information following. The code is similar
    to that for character classes, but repeated for efficiency. Then obey
    similar code to character type repeats - written out again for speed.
    However, if the referenced string is the empty string, always treat
    it as matched, any number of times (otherwise there could be infinite
    loops). */

    case OP_REF:
      {
      offset = GET2(ecode, 1) << 1;               /* Doubled ref number */
      ecode += 3;                                 /* Advance past item */

      /* If the reference is unset, set the length to be longer than the amount
      of subject left; this ensures that every attempt at a match fails. We
      can't just fail here, because of the possibility of quantifiers with zero
      minima. */

      length = (offset >= offset_top || md->offset_vector[offset] < 0)?
        md->end_subject - eptr + 1 :
        md->offset_vector[offset+1] - md->offset_vector[offset];

      /* Set up for repetition, or handle the non-repeated case */

      switch (*ecode)
        {
        case OP_CRSTAR:
        case OP_CRMINSTAR:
        case OP_CRPLUS:
        case OP_CRMINPLUS:
        case OP_CRQUERY:
        case OP_CRMINQUERY:
        c = *ecode++ - OP_CRSTAR;
        minimize = (c & 1) != 0;
        min = rep_min[c];                 /* Pick up values from tables; */
        max = rep_max[c];                 /* zero for max => infinity */
        if (max == 0) max = INT_MAX;
        break;

        case OP_CRRANGE:
        case OP_CRMINRANGE:
        minimize = (*ecode == OP_CRMINRANGE);
        min = GET2(ecode, 1);
        max = GET2(ecode, 3);
        if (max == 0) max = INT_MAX;
        ecode += 5;
        break;

        default:               /* No repeat follows */
        if (!match_ref(offset, eptr, length, md, ims)) RRETURN(MATCH_NOMATCH);
        eptr += length;
        continue;              /* With the main loop */
        }

      /* If the length of the reference is zero, just continue with the
      main loop. */

      if (length == 0) continue;

      /* First, ensure the minimum number of matches are present. We get back
      the length of the reference string explicitly rather than passing the
      address of eptr, so that eptr can be a register variable. */

      for (i = 1; i <= min; i++)
        {
        if (!match_ref(offset, eptr, length, md, ims)) RRETURN(MATCH_NOMATCH);
        eptr += length;
        }

      /* If min = max, continue at the same level without recursion.
      They are not both allowed to be zero. */

      if (min == max) continue;

      /* If minimizing, keep trying and advancing the pointer */

      if (minimize)
        {
        for (fi = min;; fi++)
          {
          RMATCH(rrc, eptr, ecode, offset_top, md, ims, eptrb, 0);
          if (rrc != MATCH_NOMATCH) RRETURN(rrc);
          if (fi >= max || !match_ref(offset, eptr, length, md, ims))
            RRETURN(MATCH_NOMATCH);
          eptr += length;
          }
        /* Control never gets here */
        }

      /* If maximizing, find the longest string and work backwards */

      else
        {
        pp = eptr;
        for (i = min; i < max; i++)
          {
          if (!match_ref(offset, eptr, length, md, ims)) break;
          eptr += length;
          }
        while (eptr >= pp)
          {
          RMATCH(rrc, eptr, ecode, offset_top, md, ims, eptrb, 0);
          if (rrc != MATCH_NOMATCH) RRETURN(rrc);
          eptr -= length;
          }
        RRETURN(MATCH_NOMATCH);
        }
      }
    /* Control never gets here */



    /* Match a bit-mapped character class, possibly repeatedly. This op code is
    used when all the characters in the class have values in the range 0-255,
    and either the matching is caseful, or the characters are in the range
    0-127 when UTF-8 processing is enabled. The only difference between
    OP_CLASS and OP_NCLASS occurs when a data character outside the range is
    encountered.

    First, look past the end of the item to see if there is repeat information
    following. Then obey similar code to character type repeats - written out
    again for speed. */

    case OP_NCLASS:
    case OP_CLASS:
      {
      data = ecode + 1;                /* Save for matching */
      ecode += 33;                     /* Advance past the item */

      switch (*ecode)
        {
        case OP_CRSTAR:
        case OP_CRMINSTAR:
        case OP_CRPLUS:
        case OP_CRMINPLUS:
        case OP_CRQUERY:
        case OP_CRMINQUERY:
        c = *ecode++ - OP_CRSTAR;
        minimize = (c & 1) != 0;
        min = rep_min[c];                 /* Pick up values from tables; */
        max = rep_max[c];                 /* zero for max => infinity */
        if (max == 0) max = INT_MAX;
        break;

        case OP_CRRANGE:
        case OP_CRMINRANGE:
        minimize = (*ecode == OP_CRMINRANGE);
        min = GET2(ecode, 1);
        max = GET2(ecode, 3);
        if (max == 0) max = INT_MAX;
        ecode += 5;
        break;

        default:               /* No repeat follows */
        min = max = 1;
        break;
        }

      /* First, ensure the minimum number of matches are present. */

#ifdef SUPPORT_UTF8
      /* UTF-8 mode */
      if (utf8)
        {
        for (i = 1; i <= min; i++)
          {
          if (eptr >= md->end_subject) RRETURN(MATCH_NOMATCH);
          GETCHARINC(c, eptr);
          if (c > 255)
            {
            if (op == OP_CLASS) RRETURN(MATCH_NOMATCH);
            }
          else
            {
            if ((data[c/8] & (1 << (c&7))) == 0) RRETURN(MATCH_NOMATCH);
            }
          }
        }
      else
#endif
      /* Not UTF-8 mode */
        {
        for (i = 1; i <= min; i++)
          {
          if (eptr >= md->end_subject) RRETURN(MATCH_NOMATCH);
          c = *eptr++;
          if ((data[c/8] & (1 << (c&7))) == 0) RRETURN(MATCH_NOMATCH);
          }
        }

      /* If max == min we can continue with the main loop without the
      need to recurse. */

      if (min == max) continue;

      /* If minimizing, keep testing the rest of the expression and advancing
      the pointer while it matches the class. */

      if (minimize)
        {
#ifdef SUPPORT_UTF8
        /* UTF-8 mode */
        if (utf8)
          {
          for (fi = min;; fi++)
            {
            RMATCH(rrc, eptr, ecode, offset_top, md, ims, eptrb, 0);
            if (rrc != MATCH_NOMATCH) RRETURN(rrc);
            if (fi >= max || eptr >= md->end_subject) RRETURN(MATCH_NOMATCH);
            GETCHARINC(c, eptr);
            if (c > 255)
              {
              if (op == OP_CLASS) RRETURN(MATCH_NOMATCH);
              }
            else
              {
              if ((data[c/8] & (1 << (c&7))) == 0) RRETURN(MATCH_NOMATCH);
              }
            }
          }
        else
#endif
        /* Not UTF-8 mode */
          {
          for (fi = min;; fi++)
            {
            RMATCH(rrc, eptr, ecode, offset_top, md, ims, eptrb, 0);
            if (rrc != MATCH_NOMATCH) RRETURN(rrc);
            if (fi >= max || eptr >= md->end_subject) RRETURN(MATCH_NOMATCH);
            c = *eptr++;
            if ((data[c/8] & (1 << (c&7))) == 0) RRETURN(MATCH_NOMATCH);
            }
          }
        /* Control never gets here */
        }

      /* If maximizing, find the longest possible run, then work backwards. */

      else
        {
        pp = eptr;

#ifdef SUPPORT_UTF8
        /* UTF-8 mode */
        if (utf8)
          {
          for (i = min; i < max; i++)
            {
            int len = 1;
            if (eptr >= md->end_subject) break;
            GETCHARLEN(c, eptr, len);
            if (c > 255)
              {
              if (op == OP_CLASS) break;
              }
            else
              {
              if ((data[c/8] & (1 << (c&7))) == 0) break;
              }
            eptr += len;
            }
          for (;;)
            {
            RMATCH(rrc, eptr, ecode, offset_top, md, ims, eptrb, 0);
            if (rrc != MATCH_NOMATCH) RRETURN(rrc);
            if (eptr-- == pp) break;        /* Stop if tried at original pos */
            BACKCHAR(eptr);
            }
          }
        else
#endif
          /* Not UTF-8 mode */
          {
          for (i = min; i < max; i++)
            {
            if (eptr >= md->end_subject) break;
            c = *eptr;
            if ((data[c/8] & (1 << (c&7))) == 0) break;
            eptr++;
            }
          while (eptr >= pp)
            {
            RMATCH(rrc, eptr, ecode, offset_top, md, ims, eptrb, 0);
            if (rrc != MATCH_NOMATCH) RRETURN(rrc);
            eptr--;
            }
          }

        RRETURN(MATCH_NOMATCH);
        }
      }
    /* Control never gets here */


    /* Match an extended character class. This opcode is encountered only
    in UTF-8 mode, because that's the only time it is compiled. */

#ifdef SUPPORT_UTF8
    case OP_XCLASS:
      {
      data = ecode + 1 + LINK_SIZE;                /* Save for matching */
      ecode += GET(ecode, 1);                      /* Advance past the item */

      switch (*ecode)
        {
        case OP_CRSTAR:
        case OP_CRMINSTAR:
        case OP_CRPLUS:
        case OP_CRMINPLUS:
        case OP_CRQUERY:
        case OP_CRMINQUERY:
        c = *ecode++ - OP_CRSTAR;
        minimize = (c & 1) != 0;
        min = rep_min[c];                 /* Pick up values from tables; */
        max = rep_max[c];                 /* zero for max => infinity */
        if (max == 0) max = INT_MAX;
        break;

        case OP_CRRANGE:
        case OP_CRMINRANGE:
        minimize = (*ecode == OP_CRMINRANGE);
        min = GET2(ecode, 1);
        max = GET2(ecode, 3);
        if (max == 0) max = INT_MAX;
        ecode += 5;
        break;

        default:               /* No repeat follows */
        min = max = 1;
        break;
        }

      /* First, ensure the minimum number of matches are present. */

      for (i = 1; i <= min; i++)
        {
        if (eptr >= md->end_subject) RRETURN(MATCH_NOMATCH);
        GETCHARINC(c, eptr);
        if (!_pcre_xclass(c, data)) RRETURN(MATCH_NOMATCH);
        }

      /* If max == min we can continue with the main loop without the
      need to recurse. */

      if (min == max) continue;

      /* If minimizing, keep testing the rest of the expression and advancing
      the pointer while it matches the class. */

      if (minimize)
        {
        for (fi = min;; fi++)
          {
          RMATCH(rrc, eptr, ecode, offset_top, md, ims, eptrb, 0);
          if (rrc != MATCH_NOMATCH) RRETURN(rrc);
          if (fi >= max || eptr >= md->end_subject) RRETURN(MATCH_NOMATCH);
          GETCHARINC(c, eptr);
          if (!_pcre_xclass(c, data)) RRETURN(MATCH_NOMATCH);
          }
        /* Control never gets here */
        }

      /* If maximizing, find the longest possible run, then work backwards. */

      else
        {
        pp = eptr;
        for (i = min; i < max; i++)
          {
          int len = 1;
          if (eptr >= md->end_subject) break;
          GETCHARLEN(c, eptr, len);
          if (!_pcre_xclass(c, data)) break;
          eptr += len;
          }
        for(;;)
          {
          RMATCH(rrc, eptr, ecode, offset_top, md, ims, eptrb, 0);
          if (rrc != MATCH_NOMATCH) RRETURN(rrc);
          if (eptr-- == pp) break;        /* Stop if tried at original pos */
          BACKCHAR(eptr)
          }
        RRETURN(MATCH_NOMATCH);
        }

      /* Control never gets here */
      }
#endif    /* End of XCLASS */

    /* Match a single character, casefully */

    case OP_CHAR:
#ifdef SUPPORT_UTF8
    if (utf8)
      {
      length = 1;
      ecode++;
      GETCHARLEN(fc, ecode, length);
      if (length > md->end_subject - eptr) RRETURN(MATCH_NOMATCH);
      while (length-- > 0) if (*ecode++ != *eptr++) RRETURN(MATCH_NOMATCH);
      }
    else
#endif

    /* Non-UTF-8 mode */
      {
      if (md->end_subject - eptr < 1) RRETURN(MATCH_NOMATCH);
      if (ecode[1] != *eptr++) RRETURN(MATCH_NOMATCH);
      ecode += 2;
      }
    break;

    /* Match a single character, caselessly */

    case OP_CHARNC:
#ifdef SUPPORT_UTF8
    if (utf8)
      {
      length = 1;
      ecode++;
      GETCHARLEN(fc, ecode, length);

      if (length > md->end_subject - eptr) RRETURN(MATCH_NOMATCH);

      /* If the pattern character's value is < 128, we have only one byte, and
      can use the fast lookup table. */

      if (fc < 128)
        {
        if (md->lcc[*ecode++] != md->lcc[*eptr++]) RRETURN(MATCH_NOMATCH);
        }

      /* Otherwise we must pick up the subject character */

      else
        {
        unsigned int dc;
        GETCHARINC(dc, eptr);
        ecode += length;

        /* If we have Unicode property support, we can use it to test the other
        case of the character, if there is one. */

        if (fc != dc)
          {
#ifdef SUPPORT_UCP
          if (dc != _pcre_ucp_othercase(fc))
#endif
            RRETURN(MATCH_NOMATCH);
          }
        }
      }
    else
#endif   /* SUPPORT_UTF8 */

    /* Non-UTF-8 mode */
      {
      if (md->end_subject - eptr < 1) RRETURN(MATCH_NOMATCH);
      if (md->lcc[ecode[1]] != md->lcc[*eptr++]) RRETURN(MATCH_NOMATCH);
      ecode += 2;
      }
    break;

    /* Match a single character repeatedly. */

    case OP_EXACT:
    min = max = GET2(ecode, 1);
    ecode += 3;
    goto REPEATCHAR;

    case OP_POSUPTO:
    possessive = true;
    /* Fall through */

    case OP_UPTO:
    case OP_MINUPTO:
    min = 0;
    max = GET2(ecode, 1);
    minimize = *ecode == OP_MINUPTO;
    ecode += 3;
    goto REPEATCHAR;

    case OP_POSSTAR:
    possessive = true;
    min = 0;
    max = INT_MAX;
    ecode++;
    goto REPEATCHAR;

    case OP_POSPLUS:
    possessive = true;
    min = 1;
    max = INT_MAX;
    ecode++;
    goto REPEATCHAR;

    case OP_POSQUERY:
    possessive = true;
    min = 0;
    max = 1;
    ecode++;
    goto REPEATCHAR;

    case OP_STAR:
    case OP_MINSTAR:
    case OP_PLUS:
    case OP_MINPLUS:
    case OP_QUERY:
    case OP_MINQUERY:
    c = *ecode++ - OP_STAR;
    minimize = (c & 1) != 0;
    min = rep_min[c];                 /* Pick up values from tables; */
    max = rep_max[c];                 /* zero for max => infinity */
    if (max == 0) max = INT_MAX;

    /* Common code for all repeated single-character matches. We can give
    up quickly if there are fewer than the minimum number of characters left in
    the subject. */

    REPEATCHAR:
#ifdef SUPPORT_UTF8
    if (utf8)
      {
      length = 1;
      charptr = ecode;
      GETCHARLEN(fc, ecode, length);
      if (min * length > md->end_subject - eptr) RRETURN(MATCH_NOMATCH);
      ecode += length;

      /* Handle multibyte character matching specially here. There is
      support for caseless matching if UCP support is present. */

      if (length > 1)
        {
#ifdef SUPPORT_UCP
        unsigned int othercase;
        if ((ims & PCRE_CASELESS) != 0 &&
            (othercase = _pcre_ucp_othercase(fc)) != NOTACHAR)
          oclength = _pcre_ord2utf8(othercase, occhars);
        else oclength = 0;
#endif  /* SUPPORT_UCP */

        for (i = 1; i <= min; i++)
          {
          if (memcmp(eptr, charptr, length) == 0) eptr += length;
#ifdef SUPPORT_UCP
          /* Need braces because of following else */
          else if (oclength == 0) { RRETURN(MATCH_NOMATCH); }
          else
            {
            if (memcmp(eptr, occhars, oclength) != 0) RRETURN(MATCH_NOMATCH);
            eptr += oclength;
            }
#else   /* without SUPPORT_UCP */
          else { RRETURN(MATCH_NOMATCH); }
#endif  /* SUPPORT_UCP */
          }

        if (min == max) continue;

        if (minimize)
          {
          for (fi = min;; fi++)
            {
            RMATCH(rrc, eptr, ecode, offset_top, md, ims, eptrb, 0);
            if (rrc != MATCH_NOMATCH) RRETURN(rrc);
            if (fi >= max || eptr >= md->end_subject) RRETURN(MATCH_NOMATCH);
            if (memcmp(eptr, charptr, length) == 0) eptr += length;
#ifdef SUPPORT_UCP
            /* Need braces because of following else */
            else if (oclength == 0) { RRETURN(MATCH_NOMATCH); }
            else
              {
              if (memcmp(eptr, occhars, oclength) != 0) RRETURN(MATCH_NOMATCH);
              eptr += oclength;
              }
#else   /* without SUPPORT_UCP */
            else { RRETURN (MATCH_NOMATCH); }
#endif  /* SUPPORT_UCP */
            }
          /* Control never gets here */
          }

        else  /* Maximize */
          {
          pp = eptr;
          for (i = min; i < max; i++)
            {
            if (eptr > md->end_subject - length) break;
            if (memcmp(eptr, charptr, length) == 0) eptr += length;
#ifdef SUPPORT_UCP
            else if (oclength == 0) break;
            else
              {
              if (memcmp(eptr, occhars, oclength) != 0) break;
              eptr += oclength;
              }
#else   /* without SUPPORT_UCP */
            else break;
#endif  /* SUPPORT_UCP */
            }

          if (possessive) continue;
          for(;;)
           {
           RMATCH(rrc, eptr, ecode, offset_top, md, ims, eptrb, 0);
           if (rrc != MATCH_NOMATCH) RRETURN(rrc);
           if (eptr == pp) RRETURN(MATCH_NOMATCH);
#ifdef SUPPORT_UCP
           eptr--;
           BACKCHAR(eptr);
#else   /* without SUPPORT_UCP */
           eptr -= length;
#endif  /* SUPPORT_UCP */
           }
          }
        /* Control never gets here */
        }

      /* If the length of a UTF-8 character is 1, we fall through here, and
      obey the code as for non-UTF-8 characters below, though in this case the
      value of fc will always be < 128. */
      }
    else
#endif  /* SUPPORT_UTF8 */

    /* When not in UTF-8 mode, load a single-byte character. */
      {
      if (min > md->end_subject - eptr) RRETURN(MATCH_NOMATCH);
      fc = *ecode++;
      }

    /* The value of fc at this point is always less than 256, though we may or
    may not be in UTF-8 mode. The code is duplicated for the caseless and
    caseful cases, for speed, since matching characters is likely to be quite
    common. First, ensure the minimum number of matches are present. If min =
    max, continue at the same level without recursing. Otherwise, if
    minimizing, keep trying the rest of the expression and advancing one
    matching character if failing, up to the maximum. Alternatively, if
    maximizing, find the maximum number of characters and work backwards. */

    DPRINTF(("matching %c{%d,%d} against subject %.*s\n", fc, min, max,
      max, eptr));

    if ((ims & PCRE_CASELESS) != 0)
      {
      fc = md->lcc[fc];
      for (i = 1; i <= min; i++)
        if (fc != md->lcc[*eptr++]) RRETURN(MATCH_NOMATCH);
      if (min == max) continue;
      if (minimize)
        {
        for (fi = min;; fi++)
          {
          RMATCH(rrc, eptr, ecode, offset_top, md, ims, eptrb, 0);
          if (rrc != MATCH_NOMATCH) RRETURN(rrc);
          if (fi >= max || eptr >= md->end_subject ||
              fc != md->lcc[*eptr++])
            RRETURN(MATCH_NOMATCH);
          }
        /* Control never gets here */
        }
      else  /* Maximize */
        {
        pp = eptr;
        for (i = min; i < max; i++)
          {
          if (eptr >= md->end_subject || fc != md->lcc[*eptr]) break;
          eptr++;
          }
        if (possessive) continue;
        while (eptr >= pp)
          {
          RMATCH(rrc, eptr, ecode, offset_top, md, ims, eptrb, 0);
          eptr--;
          if (rrc != MATCH_NOMATCH) RRETURN(rrc);
          }
        RRETURN(MATCH_NOMATCH);
        }
      /* Control never gets here */
      }

    /* Caseful comparisons (includes all multi-byte characters) */

    else
      {
      for (i = 1; i <= min; i++) if (fc != *eptr++) RRETURN(MATCH_NOMATCH);
      if (min == max) continue;
      if (minimize)
        {
        for (fi = min;; fi++)
          {
          RMATCH(rrc, eptr, ecode, offset_top, md, ims, eptrb, 0);
          if (rrc != MATCH_NOMATCH) RRETURN(rrc);
          if (fi >= max || eptr >= md->end_subject || fc != *eptr++)
            RRETURN(MATCH_NOMATCH);
          }
        /* Control never gets here */
        }
      else  /* Maximize */
        {
        pp = eptr;
        for (i = min; i < max; i++)
          {
          if (eptr >= md->end_subject || fc != *eptr) break;
          eptr++;
          }
        if (possessive) continue;
        while (eptr >= pp)
          {
          RMATCH(rrc, eptr, ecode, offset_top, md, ims, eptrb, 0);
          eptr--;
          if (rrc != MATCH_NOMATCH) RRETURN(rrc);
          }
        RRETURN(MATCH_NOMATCH);
        }
      }
    /* Control never gets here */

    /* Match a negated single one-byte character. The character we are
    checking can be multibyte. */

    case OP_NOT:
    if (eptr >= md->end_subject) RRETURN(MATCH_NOMATCH);
    ecode++;
    GETCHARINCTEST(c, eptr);
    if ((ims & PCRE_CASELESS) != 0)
      {
#ifdef SUPPORT_UTF8
      if (c < 256)
#endif
      c = md->lcc[c];
      if (md->lcc[*ecode++] == c) RRETURN(MATCH_NOMATCH);
      }
    else
      {
      if (*ecode++ == c) RRETURN(MATCH_NOMATCH);
      }
    break;

    /* Match a negated single one-byte character repeatedly. This is almost a
    repeat of the code for a repeated single character, but I haven't found a
    nice way of commoning these up that doesn't require a test of the
    positive/negative option for each character match. Maybe that wouldn't add
    very much to the time taken, but character matching *is* what this is all
    about... */

    case OP_NOTEXACT:
    min = max = GET2(ecode, 1);
    ecode += 3;
    goto REPEATNOTCHAR;

    case OP_NOTUPTO:
    case OP_NOTMINUPTO:
    min = 0;
    max = GET2(ecode, 1);
    minimize = *ecode == OP_NOTMINUPTO;
    ecode += 3;
    goto REPEATNOTCHAR;

    case OP_NOTPOSSTAR:
    possessive = true;
    min = 0;
    max = INT_MAX;
    ecode++;
    goto REPEATNOTCHAR;

    case OP_NOTPOSPLUS:
    possessive = true;
    min = 1;
    max = INT_MAX;
    ecode++;
    goto REPEATNOTCHAR;

    case OP_NOTPOSQUERY:
    possessive = true;
    min = 0;
    max = 1;
    ecode++;
    goto REPEATNOTCHAR;

    case OP_NOTPOSUPTO:
    possessive = true;
    min = 0;
    max = GET2(ecode, 1);
    ecode += 3;
    goto REPEATNOTCHAR;

    case OP_NOTSTAR:
    case OP_NOTMINSTAR:
    case OP_NOTPLUS:
    case OP_NOTMINPLUS:
    case OP_NOTQUERY:
    case OP_NOTMINQUERY:
    c = *ecode++ - OP_NOTSTAR;
    minimize = (c & 1) != 0;
    min = rep_min[c];                 /* Pick up values from tables; */
    max = rep_max[c];                 /* zero for max => infinity */
    if (max == 0) max = INT_MAX;

    /* Common code for all repeated single-byte matches. We can give up quickly
    if there are fewer than the minimum number of bytes left in the
    subject. */

    REPEATNOTCHAR:
    if (min > md->end_subject - eptr) RRETURN(MATCH_NOMATCH);
    fc = *ecode++;

    /* The code is duplicated for the caseless and caseful cases, for speed,
    since matching characters is likely to be quite common. First, ensure the
    minimum number of matches are present. If min = max, continue at the same
    level without recursing. Otherwise, if minimizing, keep trying the rest of
    the expression and advancing one matching character if failing, up to the
    maximum. Alternatively, if maximizing, find the maximum number of
    characters and work backwards. */

    DPRINTF(("negative matching %c{%d,%d} against subject %.*s\n", fc, min, max,
      max, eptr));

    if ((ims & PCRE_CASELESS) != 0)
      {
      fc = md->lcc[fc];

#ifdef SUPPORT_UTF8
      /* UTF-8 mode */
      if (utf8)
        {
        register unsigned int d;
        for (i = 1; i <= min; i++)
          {
          GETCHARINC(d, eptr);
          if (d < 256) d = md->lcc[d];
          if (fc == d) RRETURN(MATCH_NOMATCH);
          }
        }
      else
#endif

      /* Not UTF-8 mode */
        {
        for (i = 1; i <= min; i++)
          if (fc == md->lcc[*eptr++]) RRETURN(MATCH_NOMATCH);
        }

      if (min == max) continue;

      if (minimize)
        {
#ifdef SUPPORT_UTF8
        /* UTF-8 mode */
        if (utf8)
          {
          register unsigned int d;
          for (fi = min;; fi++)
            {
            RMATCH(rrc, eptr, ecode, offset_top, md, ims, eptrb, 0);
            if (rrc != MATCH_NOMATCH) RRETURN(rrc);
            GETCHARINC(d, eptr);
            if (d < 256) d = md->lcc[d];
            if (fi >= max || eptr >= md->end_subject || fc == d)
              RRETURN(MATCH_NOMATCH);
            }
          }
        else
#endif
        /* Not UTF-8 mode */
          {
          for (fi = min;; fi++)
            {
            RMATCH(rrc, eptr, ecode, offset_top, md, ims, eptrb, 0);
            if (rrc != MATCH_NOMATCH) RRETURN(rrc);
            if (fi >= max || eptr >= md->end_subject || fc == md->lcc[*eptr++])
              RRETURN(MATCH_NOMATCH);
            }
          }
        /* Control never gets here */
        }

      /* Maximize case */

      else
        {
        pp = eptr;

#ifdef SUPPORT_UTF8
        /* UTF-8 mode */
        if (utf8)
          {
          register unsigned int d;
          for (i = min; i < max; i++)
            {
            int len = 1;
            if (eptr >= md->end_subject) break;
            GETCHARLEN(d, eptr, len);
            if (d < 256) d = md->lcc[d];
            if (fc == d) break;
            eptr += len;
            }
        if (possessive) continue;
        for(;;)
            {
            RMATCH(rrc, eptr, ecode, offset_top, md, ims, eptrb, 0);
            if (rrc != MATCH_NOMATCH) RRETURN(rrc);
            if (eptr-- == pp) break;        /* Stop if tried at original pos */
            BACKCHAR(eptr);
            }
          }
        else
#endif
        /* Not UTF-8 mode */
          {
          for (i = min; i < max; i++)
            {
            if (eptr >= md->end_subject || fc == md->lcc[*eptr]) break;
            eptr++;
            }
          if (possessive) continue;
          while (eptr >= pp)
            {
            RMATCH(rrc, eptr, ecode, offset_top, md, ims, eptrb, 0);
            if (rrc != MATCH_NOMATCH) RRETURN(rrc);
            eptr--;
            }
          }

        RRETURN(MATCH_NOMATCH);
        }
      /* Control never gets here */
      }

    /* Caseful comparisons */

    else
      {
#ifdef SUPPORT_UTF8
      /* UTF-8 mode */
      if (utf8)
        {
        register unsigned int d;
        for (i = 1; i <= min; i++)
          {
          GETCHARINC(d, eptr);
          if (fc == d) RRETURN(MATCH_NOMATCH);
          }
        }
      else
#endif
      /* Not UTF-8 mode */
        {
        for (i = 1; i <= min; i++)
          if (fc == *eptr++) RRETURN(MATCH_NOMATCH);
        }

      if (min == max) continue;

      if (minimize)
        {
#ifdef SUPPORT_UTF8
        /* UTF-8 mode */
        if (utf8)
          {
          register unsigned int d;
          for (fi = min;; fi++)
            {
            RMATCH(rrc, eptr, ecode, offset_top, md, ims, eptrb, 0);
            if (rrc != MATCH_NOMATCH) RRETURN(rrc);
            GETCHARINC(d, eptr);
            if (fi >= max || eptr >= md->end_subject || fc == d)
              RRETURN(MATCH_NOMATCH);
            }
          }
        else
#endif
        /* Not UTF-8 mode */
          {
          for (fi = min;; fi++)
            {
            RMATCH(rrc, eptr, ecode, offset_top, md, ims, eptrb, 0);
            if (rrc != MATCH_NOMATCH) RRETURN(rrc);
            if (fi >= max || eptr >= md->end_subject || fc == *eptr++)
              RRETURN(MATCH_NOMATCH);
            }
          }
        /* Control never gets here */
        }

      /* Maximize case */

      else
        {
        pp = eptr;

#ifdef SUPPORT_UTF8
        /* UTF-8 mode */
        if (utf8)
          {
          register unsigned int d;
          for (i = min; i < max; i++)
            {
            int len = 1;
            if (eptr >= md->end_subject) break;
            GETCHARLEN(d, eptr, len);
            if (fc == d) break;
            eptr += len;
            }
          if (possessive) continue;
          for(;;)
            {
            RMATCH(rrc, eptr, ecode, offset_top, md, ims, eptrb, 0);
            if (rrc != MATCH_NOMATCH) RRETURN(rrc);
            if (eptr-- == pp) break;        /* Stop if tried at original pos */
            BACKCHAR(eptr);
            }
          }
        else
#endif
        /* Not UTF-8 mode */
          {
          for (i = min; i < max; i++)
            {
            if (eptr >= md->end_subject || fc == *eptr) break;
            eptr++;
            }
          if (possessive) continue;
          while (eptr >= pp)
            {
            RMATCH(rrc, eptr, ecode, offset_top, md, ims, eptrb, 0);
            if (rrc != MATCH_NOMATCH) RRETURN(rrc);
            eptr--;
            }
          }

        RRETURN(MATCH_NOMATCH);
        }
      }
    /* Control never gets here */

    /* Match a single character type repeatedly; several different opcodes
    share code. This is very similar to the code for single characters, but we
    repeat it in the interests of efficiency. */

    case OP_TYPEEXACT:
    min = max = GET2(ecode, 1);
    minimize = true;
    ecode += 3;
    goto REPEATTYPE;

    case OP_TYPEUPTO:
    case OP_TYPEMINUPTO:
    min = 0;
    max = GET2(ecode, 1);
    minimize = *ecode == OP_TYPEMINUPTO;
    ecode += 3;
    goto REPEATTYPE;

    case OP_TYPEPOSSTAR:
    possessive = true;
    min = 0;
    max = INT_MAX;
    ecode++;
    goto REPEATTYPE;

    case OP_TYPEPOSPLUS:
    possessive = true;
    min = 1;
    max = INT_MAX;
    ecode++;
    goto REPEATTYPE;

    case OP_TYPEPOSQUERY:
    possessive = true;
    min = 0;
    max = 1;
    ecode++;
    goto REPEATTYPE;

    case OP_TYPEPOSUPTO:
    possessive = true;
    min = 0;
    max = GET2(ecode, 1);
    ecode += 3;
    goto REPEATTYPE;

    case OP_TYPESTAR:
    case OP_TYPEMINSTAR:
    case OP_TYPEPLUS:
    case OP_TYPEMINPLUS:
    case OP_TYPEQUERY:
    case OP_TYPEMINQUERY:
    c = *ecode++ - OP_TYPESTAR;
    minimize = (c & 1) != 0;
    min = rep_min[c];                 /* Pick up values from tables; */
    max = rep_max[c];                 /* zero for max => infinity */
    if (max == 0) max = INT_MAX;

    /* Common code for all repeated single character type matches. Note that
    in UTF-8 mode, '.' matches a character of any length, but for the other
    character types, the valid characters are all one-byte long. */

    REPEATTYPE:
    ctype = *ecode++;      /* Code for the character type */

#ifdef SUPPORT_UCP
    if (ctype == OP_PROP || ctype == OP_NOTPROP)
      {
      prop_fail_result = ctype == OP_NOTPROP;
      prop_type = *ecode++;
      prop_value = *ecode++;
      }
    else prop_type = -1;
#endif

    /* First, ensure the minimum number of matches are present. Use inline
    code for maximizing the speed, and do the type test once at the start
    (i.e. keep it out of the loop). Also we can test that there are at least
    the minimum number of bytes before we start. This isn't as effective in
    UTF-8 mode, but it does no harm. Separate the UTF-8 code completely as that
    is tidier. Also separate the UCP code, which can be the same for both UTF-8
    and single-bytes. */

    if (min > md->end_subject - eptr) RRETURN(MATCH_NOMATCH);
    if (min > 0)
      {
#ifdef SUPPORT_UCP
      if (prop_type >= 0)
        {
        switch(prop_type)
          {
          case PT_ANY:
          if (prop_fail_result) RRETURN(MATCH_NOMATCH);
          for (i = 1; i <= min; i++)
            {
            if (eptr >= md->end_subject) RRETURN(MATCH_NOMATCH);
            GETCHARINC(c, eptr);
            }
          break;

          case PT_LAMP:
          for (i = 1; i <= min; i++)
            {
            if (eptr >= md->end_subject) RRETURN(MATCH_NOMATCH);
            GETCHARINC(c, eptr);
            prop_category = _pcre_ucp_findprop(c, &prop_chartype, &prop_script);
            if ((prop_chartype == ucp_Lu ||
                 prop_chartype == ucp_Ll ||
                 prop_chartype == ucp_Lt) == prop_fail_result)
              RRETURN(MATCH_NOMATCH);
            }
          break;

          case PT_GC:
          for (i = 1; i <= min; i++)
            {
            if (eptr >= md->end_subject) RRETURN(MATCH_NOMATCH);
            GETCHARINC(c, eptr);
            prop_category = _pcre_ucp_findprop(c, &prop_chartype, &prop_script);
            if ((prop_category == prop_value) == prop_fail_result)
              RRETURN(MATCH_NOMATCH);
            }
          break;

          case PT_PC:
          for (i = 1; i <= min; i++)
            {
            if (eptr >= md->end_subject) RRETURN(MATCH_NOMATCH);
            GETCHARINC(c, eptr);
            prop_category = _pcre_ucp_findprop(c, &prop_chartype, &prop_script);
            if ((prop_chartype == prop_value) == prop_fail_result)
              RRETURN(MATCH_NOMATCH);
            }
          break;

          case PT_SC:
          for (i = 1; i <= min; i++)
            {
            if (eptr >= md->end_subject) RRETURN(MATCH_NOMATCH);
            GETCHARINC(c, eptr);
            prop_category = _pcre_ucp_findprop(c, &prop_chartype, &prop_script);
            if ((prop_script == prop_value) == prop_fail_result)
              RRETURN(MATCH_NOMATCH);
            }
          break;

          default:
          RRETURN(PCRE_ERROR_INTERNAL);
          }
        }

      /* Match extended Unicode sequences. We will get here only if the
      support is in the binary; otherwise a compile-time error occurs. */

      else if (ctype == OP_EXTUNI)
        {
        for (i = 1; i <= min; i++)
          {
          GETCHARINCTEST(c, eptr);
          prop_category = _pcre_ucp_findprop(c, &prop_chartype, &prop_script);
          if (prop_category == ucp_M) RRETURN(MATCH_NOMATCH);
          while (eptr < md->end_subject)
            {
            int len = 1;
            if (!utf8) c = *eptr; else
              {
              GETCHARLEN(c, eptr, len);
              }
            prop_category = _pcre_ucp_findprop(c, &prop_chartype, &prop_script);
            if (prop_category != ucp_M) break;
            eptr += len;
            }
          }
        }

      else
#endif     /* SUPPORT_UCP */

/* Handle all other cases when the coding is UTF-8 */

#ifdef SUPPORT_UTF8
      if (utf8) switch(ctype)
        {
        case OP_ANY:
        for (i = 1; i <= min; i++)
          {
          if (eptr >= md->end_subject ||
               ((ims & PCRE_DOTALL) == 0 && IS_NEWLINE(eptr)))
            RRETURN(MATCH_NOMATCH);
          eptr++;
          while (eptr < md->end_subject && (*eptr & 0xc0) == 0x80) eptr++;
          }
        break;

        case OP_ANYBYTE:
        eptr += min;
        break;

        case OP_ANYNL:
        for (i = 1; i <= min; i++)
          {
          if (eptr >= md->end_subject) RRETURN(MATCH_NOMATCH);
          GETCHARINC(c, eptr);
          switch(c)
            {
            default: RRETURN(MATCH_NOMATCH);
            case 0x000d:
            if (eptr < md->end_subject && *eptr == 0x0a) eptr++;
            break;
            case 0x000a:
            case 0x000b:
            case 0x000c:
            case 0x0085:
            case 0x2028:
            case 0x2029:
            break;
            }
          }
        break;

        case OP_NOT_DIGIT:
        for (i = 1; i <= min; i++)
          {
          if (eptr >= md->end_subject) RRETURN(MATCH_NOMATCH);
          GETCHARINC(c, eptr);
          if (c < 128 && (md->ctypes[c] & ctype_digit) != 0)
            RRETURN(MATCH_NOMATCH);
          }
        break;

        case OP_DIGIT:
        for (i = 1; i <= min; i++)
          {
          if (eptr >= md->end_subject ||
             *eptr >= 128 || (md->ctypes[*eptr++] & ctype_digit) == 0)
            RRETURN(MATCH_NOMATCH);
          /* No need to skip more bytes - we know it's a 1-byte character */
          }
        break;

        case OP_NOT_WHITESPACE:
        for (i = 1; i <= min; i++)
          {
          if (eptr >= md->end_subject ||
             (*eptr < 128 && (md->ctypes[*eptr++] & ctype_space) != 0))
            RRETURN(MATCH_NOMATCH);
          while (eptr < md->end_subject && (*eptr & 0xc0) == 0x80) eptr++;
          }
        break;

        case OP_WHITESPACE:
        for (i = 1; i <= min; i++)
          {
          if (eptr >= md->end_subject ||
             *eptr >= 128 || (md->ctypes[*eptr++] & ctype_space) == 0)
            RRETURN(MATCH_NOMATCH);
          /* No need to skip more bytes - we know it's a 1-byte character */
          }
        break;

        case OP_NOT_WORDCHAR:
        for (i = 1; i <= min; i++)
          {
          if (eptr >= md->end_subject ||
             (*eptr < 128 && (md->ctypes[*eptr++] & ctype_word) != 0))
            RRETURN(MATCH_NOMATCH);
          while (eptr < md->end_subject && (*eptr & 0xc0) == 0x80) eptr++;
          }
        break;

        case OP_WORDCHAR:
        for (i = 1; i <= min; i++)
          {
          if (eptr >= md->end_subject ||
             *eptr >= 128 || (md->ctypes[*eptr++] & ctype_word) == 0)
            RRETURN(MATCH_NOMATCH);
          /* No need to skip more bytes - we know it's a 1-byte character */
          }
        break;

        default:
        RRETURN(PCRE_ERROR_INTERNAL);
        }  /* End switch(ctype) */

      else
#endif     /* SUPPORT_UTF8 */

      /* Code for the non-UTF-8 case for minimum matching of operators other
      than OP_PROP and OP_NOTPROP. We can assume that there are the minimum
      number of bytes present, as this was tested above. */

      switch(ctype)
        {
        case OP_ANY:
        if ((ims & PCRE_DOTALL) == 0)
          {
          for (i = 1; i <= min; i++)
            {
            if (IS_NEWLINE(eptr)) RRETURN(MATCH_NOMATCH);
            eptr++;
            }
          }
        else eptr += min;
        break;

        case OP_ANYBYTE:
        eptr += min;
        break;

        /* Because of the CRLF case, we can't assume the minimum number of
        bytes are present in this case. */

        case OP_ANYNL:
        for (i = 1; i <= min; i++)
          {
          if (eptr >= md->end_subject) RRETURN(MATCH_NOMATCH);
          switch(*eptr++)
            {
            default: RRETURN(MATCH_NOMATCH);
            case 0x000d:
            if (eptr < md->end_subject && *eptr == 0x0a) eptr++;
            break;
            case 0x000a:
            case 0x000b:
            case 0x000c:
            case 0x0085:
            break;
            }
          }
        break;

        case OP_NOT_DIGIT:
        for (i = 1; i <= min; i++)
          if ((md->ctypes[*eptr++] & ctype_digit) != 0) RRETURN(MATCH_NOMATCH);
        break;

        case OP_DIGIT:
        for (i = 1; i <= min; i++)
          if ((md->ctypes[*eptr++] & ctype_digit) == 0) RRETURN(MATCH_NOMATCH);
        break;

        case OP_NOT_WHITESPACE:
        for (i = 1; i <= min; i++)
          if ((md->ctypes[*eptr++] & ctype_space) != 0) RRETURN(MATCH_NOMATCH);
        break;

        case OP_WHITESPACE:
        for (i = 1; i <= min; i++)
          if ((md->ctypes[*eptr++] & ctype_space) == 0) RRETURN(MATCH_NOMATCH);
        break;

        case OP_NOT_WORDCHAR:
        for (i = 1; i <= min; i++)
          if ((md->ctypes[*eptr++] & ctype_word) != 0)
            RRETURN(MATCH_NOMATCH);
        break;

        case OP_WORDCHAR:
        for (i = 1; i <= min; i++)
          if ((md->ctypes[*eptr++] & ctype_word) == 0)
            RRETURN(MATCH_NOMATCH);
        break;

        default:
        RRETURN(PCRE_ERROR_INTERNAL);
        }
      }

    /* If min = max, continue at the same level without recursing */

    if (min == max) continue;

    /* If minimizing, we have to test the rest of the pattern before each
    subsequent match. Again, separate the UTF-8 case for speed, and also
    separate the UCP cases. */

    if (minimize)
      {
#ifdef SUPPORT_UCP
      if (prop_type >= 0)
        {
        switch(prop_type)
          {
          case PT_ANY:
          for (fi = min;; fi++)
            {
            RMATCH(rrc, eptr, ecode, offset_top, md, ims, eptrb, 0);
            if (rrc != MATCH_NOMATCH) RRETURN(rrc);
            if (fi >= max || eptr >= md->end_subject) RRETURN(MATCH_NOMATCH);
            GETCHARINC(c, eptr);
            if (prop_fail_result) RRETURN(MATCH_NOMATCH);
            }
          /* Control never gets here */

          case PT_LAMP:
          for (fi = min;; fi++)
            {
            RMATCH(rrc, eptr, ecode, offset_top, md, ims, eptrb, 0);
            if (rrc != MATCH_NOMATCH) RRETURN(rrc);
            if (fi >= max || eptr >= md->end_subject) RRETURN(MATCH_NOMATCH);
            GETCHARINC(c, eptr);
            prop_category = _pcre_ucp_findprop(c, &prop_chartype, &prop_script);
            if ((prop_chartype == ucp_Lu ||
                 prop_chartype == ucp_Ll ||
                 prop_chartype == ucp_Lt) == prop_fail_result)
              RRETURN(MATCH_NOMATCH);
            }
          /* Control never gets here */

          case PT_GC:
          for (fi = min;; fi++)
            {
            RMATCH(rrc, eptr, ecode, offset_top, md, ims, eptrb, 0);
            if (rrc != MATCH_NOMATCH) RRETURN(rrc);
            if (fi >= max || eptr >= md->end_subject) RRETURN(MATCH_NOMATCH);
            GETCHARINC(c, eptr);
            prop_category = _pcre_ucp_findprop(c, &prop_chartype, &prop_script);
            if ((prop_category == prop_value) == prop_fail_result)
              RRETURN(MATCH_NOMATCH);
            }
          /* Control never gets here */

          case PT_PC:
          for (fi = min;; fi++)
            {
            RMATCH(rrc, eptr, ecode, offset_top, md, ims, eptrb, 0);
            if (rrc != MATCH_NOMATCH) RRETURN(rrc);
            if (fi >= max || eptr >= md->end_subject) RRETURN(MATCH_NOMATCH);
            GETCHARINC(c, eptr);
            prop_category = _pcre_ucp_findprop(c, &prop_chartype, &prop_script);
            if ((prop_chartype == prop_value) == prop_fail_result)
              RRETURN(MATCH_NOMATCH);
            }
          /* Control never gets here */

          case PT_SC:
          for (fi = min;; fi++)
            {
            RMATCH(rrc, eptr, ecode, offset_top, md, ims, eptrb, 0);
            if (rrc != MATCH_NOMATCH) RRETURN(rrc);
            if (fi >= max || eptr >= md->end_subject) RRETURN(MATCH_NOMATCH);
            GETCHARINC(c, eptr);
            prop_category = _pcre_ucp_findprop(c, &prop_chartype, &prop_script);
            if ((prop_script == prop_value) == prop_fail_result)
              RRETURN(MATCH_NOMATCH);
            }
          /* Control never gets here */

          default:
          RRETURN(PCRE_ERROR_INTERNAL);
          }
        }

      /* Match extended Unicode sequences. We will get here only if the
      support is in the binary; otherwise a compile-time error occurs. */

      else if (ctype == OP_EXTUNI)
        {
        for (fi = min;; fi++)
          {
          RMATCH(rrc, eptr, ecode, offset_top, md, ims, eptrb, 0);
          if (rrc != MATCH_NOMATCH) RRETURN(rrc);
          if (fi >= max || eptr >= md->end_subject) RRETURN(MATCH_NOMATCH);
          GETCHARINCTEST(c, eptr);
          prop_category = _pcre_ucp_findprop(c, &prop_chartype, &prop_script);
          if (prop_category == ucp_M) RRETURN(MATCH_NOMATCH);
          while (eptr < md->end_subject)
            {
            int len = 1;
            if (!utf8) c = *eptr; else
              {
              GETCHARLEN(c, eptr, len);
              }
            prop_category = _pcre_ucp_findprop(c, &prop_chartype, &prop_script);
            if (prop_category != ucp_M) break;
            eptr += len;
            }
          }
        }

      else
#endif     /* SUPPORT_UCP */

#ifdef SUPPORT_UTF8
      /* UTF-8 mode */
      if (utf8)
        {
        for (fi = min;; fi++)
          {
          RMATCH(rrc, eptr, ecode, offset_top, md, ims, eptrb, 0);
          if (rrc != MATCH_NOMATCH) RRETURN(rrc);
          if (fi >= max || eptr >= md->end_subject ||
               (ctype == OP_ANY && (ims & PCRE_DOTALL) == 0 &&
                IS_NEWLINE(eptr)))
            RRETURN(MATCH_NOMATCH);

          GETCHARINC(c, eptr);
          switch(ctype)
            {
            case OP_ANY:        /* This is the DOTALL case */
            break;

            case OP_ANYBYTE:
            break;

            case OP_ANYNL:
            switch(c)
              {
              default: RRETURN(MATCH_NOMATCH);
              case 0x000d:
              if (eptr < md->end_subject && *eptr == 0x0a) eptr++;
              break;
              case 0x000a:
              case 0x000b:
              case 0x000c:
              case 0x0085:
              case 0x2028:
              case 0x2029:
              break;
              }
            break;

            case OP_NOT_DIGIT:
            if (c < 256 && (md->ctypes[c] & ctype_digit) != 0)
              RRETURN(MATCH_NOMATCH);
            break;

            case OP_DIGIT:
            if (c >= 256 || (md->ctypes[c] & ctype_digit) == 0)
              RRETURN(MATCH_NOMATCH);
            break;

            case OP_NOT_WHITESPACE:
            if (c < 256 && (md->ctypes[c] & ctype_space) != 0)
              RRETURN(MATCH_NOMATCH);
            break;

            case OP_WHITESPACE:
            if  (c >= 256 || (md->ctypes[c] & ctype_space) == 0)
              RRETURN(MATCH_NOMATCH);
            break;

            case OP_NOT_WORDCHAR:
            if (c < 256 && (md->ctypes[c] & ctype_word) != 0)
              RRETURN(MATCH_NOMATCH);
            break;

            case OP_WORDCHAR:
            if (c >= 256 || (md->ctypes[c] & ctype_word) == 0)
              RRETURN(MATCH_NOMATCH);
            break;

            default:
            RRETURN(PCRE_ERROR_INTERNAL);
            }
          }
        }
      else
#endif
      /* Not UTF-8 mode */
        {
        for (fi = min;; fi++)
          {
          RMATCH(rrc, eptr, ecode, offset_top, md, ims, eptrb, 0);
          if (rrc != MATCH_NOMATCH) RRETURN(rrc);
          if (fi >= max || eptr >= md->end_subject ||
               ((ims & PCRE_DOTALL) == 0 && IS_NEWLINE(eptr)))
            RRETURN(MATCH_NOMATCH);

          c = *eptr++;
          switch(ctype)
            {
            case OP_ANY:   /* This is the DOTALL case */
            break;

            case OP_ANYBYTE:
            break;

            case OP_ANYNL:
            switch(c)
              {
              default: RRETURN(MATCH_NOMATCH);
              case 0x000d:
              if (eptr < md->end_subject && *eptr == 0x0a) eptr++;
              break;
              case 0x000a:
              case 0x000b:
              case 0x000c:
              case 0x0085:
              break;
              }
            break;

            case OP_NOT_DIGIT:
            if ((md->ctypes[c] & ctype_digit) != 0) RRETURN(MATCH_NOMATCH);
            break;

            case OP_DIGIT:
            if ((md->ctypes[c] & ctype_digit) == 0) RRETURN(MATCH_NOMATCH);
            break;

            case OP_NOT_WHITESPACE:
            if ((md->ctypes[c] & ctype_space) != 0) RRETURN(MATCH_NOMATCH);
            break;

            case OP_WHITESPACE:
            if  ((md->ctypes[c] & ctype_space) == 0) RRETURN(MATCH_NOMATCH);
            break;

            case OP_NOT_WORDCHAR:
            if ((md->ctypes[c] & ctype_word) != 0) RRETURN(MATCH_NOMATCH);
            break;

            case OP_WORDCHAR:
            if ((md->ctypes[c] & ctype_word) == 0) RRETURN(MATCH_NOMATCH);
            break;

            default:
            RRETURN(PCRE_ERROR_INTERNAL);
            }
          }
        }
      /* Control never gets here */
      }

    /* If maximizing, it is worth using inline code for speed, doing the type
    test once at the start (i.e. keep it out of the loop). Again, keep the
    UTF-8 and UCP stuff separate. */

    else
      {
      pp = eptr;  /* Remember where we started */

#ifdef SUPPORT_UCP
      if (prop_type >= 0)
        {
        switch(prop_type)
          {
          case PT_ANY:
          for (i = min; i < max; i++)
            {
            int len = 1;
            if (eptr >= md->end_subject) break;
            GETCHARLEN(c, eptr, len);
            if (prop_fail_result) break;
            eptr+= len;
            }
          break;

          case PT_LAMP:
          for (i = min; i < max; i++)
            {
            int len = 1;
            if (eptr >= md->end_subject) break;
            GETCHARLEN(c, eptr, len);
            prop_category = _pcre_ucp_findprop(c, &prop_chartype, &prop_script);
            if ((prop_chartype == ucp_Lu ||
                 prop_chartype == ucp_Ll ||
                 prop_chartype == ucp_Lt) == prop_fail_result)
              break;
            eptr+= len;
            }
          break;

          case PT_GC:
          for (i = min; i < max; i++)
            {
            int len = 1;
            if (eptr >= md->end_subject) break;
            GETCHARLEN(c, eptr, len);
            prop_category = _pcre_ucp_findprop(c, &prop_chartype, &prop_script);
            if ((prop_category == prop_value) == prop_fail_result)
              break;
            eptr+= len;
            }
          break;

          case PT_PC:
          for (i = min; i < max; i++)
            {
            int len = 1;
            if (eptr >= md->end_subject) break;
            GETCHARLEN(c, eptr, len);
            prop_category = _pcre_ucp_findprop(c, &prop_chartype, &prop_script);
            if ((prop_chartype == prop_value) == prop_fail_result)
              break;
            eptr+= len;
            }
          break;

          case PT_SC:
          for (i = min; i < max; i++)
            {
            int len = 1;
            if (eptr >= md->end_subject) break;
            GETCHARLEN(c, eptr, len);
            prop_category = _pcre_ucp_findprop(c, &prop_chartype, &prop_script);
            if ((prop_script == prop_value) == prop_fail_result)
              break;
            eptr+= len;
            }
          break;
          }

        /* eptr is now past the end of the maximum run */

        if (possessive) continue;
        for(;;)
          {
          RMATCH(rrc, eptr, ecode, offset_top, md, ims, eptrb, 0);
          if (rrc != MATCH_NOMATCH) RRETURN(rrc);
          if (eptr-- == pp) break;        /* Stop if tried at original pos */
          BACKCHAR(eptr);
          }
        }

      /* Match extended Unicode sequences. We will get here only if the
      support is in the binary; otherwise a compile-time error occurs. */

      else if (ctype == OP_EXTUNI)
        {
        for (i = min; i < max; i++)
          {
          if (eptr >= md->end_subject) break;
          GETCHARINCTEST(c, eptr);
          prop_category = _pcre_ucp_findprop(c, &prop_chartype, &prop_script);
          if (prop_category == ucp_M) break;
          while (eptr < md->end_subject)
            {
            int len = 1;
            if (!utf8) c = *eptr; else
              {
              GETCHARLEN(c, eptr, len);
              }
            prop_category = _pcre_ucp_findprop(c, &prop_chartype, &prop_script);
            if (prop_category != ucp_M) break;
            eptr += len;
            }
          }

        /* eptr is now past the end of the maximum run */

        if (possessive) continue;
        for(;;)
          {
          RMATCH(rrc, eptr, ecode, offset_top, md, ims, eptrb, 0);
          if (rrc != MATCH_NOMATCH) RRETURN(rrc);
          if (eptr-- == pp) break;        /* Stop if tried at original pos */
          for (;;)                        /* Move back over one extended */
            {
            int len = 1;
            BACKCHAR(eptr);
            if (!utf8) c = *eptr; else
              {
              GETCHARLEN(c, eptr, len);
              }
            prop_category = _pcre_ucp_findprop(c, &prop_chartype, &prop_script);
            if (prop_category != ucp_M) break;
            eptr--;
            }
          }
        }

      else
#endif   /* SUPPORT_UCP */

#ifdef SUPPORT_UTF8
      /* UTF-8 mode */

      if (utf8)
        {
        switch(ctype)
          {
          case OP_ANY:

          /* Special code is required for UTF8, but when the maximum is
          unlimited we don't need it, so we repeat the non-UTF8 code. This is
          probably worth it, because .* is quite a common idiom. */

          if (max < INT_MAX)
            {
            if ((ims & PCRE_DOTALL) == 0)
              {
              for (i = min; i < max; i++)
                {
                if (eptr >= md->end_subject || IS_NEWLINE(eptr)) break;
                eptr++;
                while (eptr < md->end_subject && (*eptr & 0xc0) == 0x80) eptr++;
                }
              }
            else
              {
              for (i = min; i < max; i++)
                {
                if (eptr >= md->end_subject) break;
                eptr++;
                while (eptr < md->end_subject && (*eptr & 0xc0) == 0x80) eptr++;
                }
              }
            }

          /* Handle unlimited UTF-8 repeat */

          else
            {
            if ((ims & PCRE_DOTALL) == 0)
              {
              for (i = min; i < max; i++)
                {
                if (eptr >= md->end_subject || IS_NEWLINE(eptr)) break;
                eptr++;
                }
              break;
              }
            else
              {
              c = max - min;
              if (c > (unsigned int)(md->end_subject - eptr))
                c = md->end_subject - eptr;
              eptr += c;
              }
            }
          break;

          /* The byte case is the same as non-UTF8 */

          case OP_ANYBYTE:
          c = max - min;
          if (c > (unsigned int)(md->end_subject - eptr))
            c = md->end_subject - eptr;
          eptr += c;
          break;

          case OP_ANYNL:
          for (i = min; i < max; i++)
            {
            int len = 1;
            if (eptr >= md->end_subject) break;
            GETCHARLEN(c, eptr, len);
            if (c == 0x000d)
              {
              if (++eptr >= md->end_subject) break;
              if (*eptr == 0x000a) eptr++;
              }
            else
              {
              if (c != 0x000a && c != 0x000b && c != 0x000c &&
                  c != 0x0085 && c != 0x2028 && c != 0x2029)
                break;
              eptr += len;
              }
            }
          break;

          case OP_NOT_DIGIT:
          for (i = min; i < max; i++)
            {
            int len = 1;
            if (eptr >= md->end_subject) break;
            GETCHARLEN(c, eptr, len);
            if (c < 256 && (md->ctypes[c] & ctype_digit) != 0) break;
            eptr+= len;
            }
          break;

          case OP_DIGIT:
          for (i = min; i < max; i++)
            {
            int len = 1;
            if (eptr >= md->end_subject) break;
            GETCHARLEN(c, eptr, len);
            if (c >= 256 ||(md->ctypes[c] & ctype_digit) == 0) break;
            eptr+= len;
            }
          break;

          case OP_NOT_WHITESPACE:
          for (i = min; i < max; i++)
            {
            int len = 1;
            if (eptr >= md->end_subject) break;
            GETCHARLEN(c, eptr, len);
            if (c < 256 && (md->ctypes[c] & ctype_space) != 0) break;
            eptr+= len;
            }
          break;

          case OP_WHITESPACE:
          for (i = min; i < max; i++)
            {
            int len = 1;
            if (eptr >= md->end_subject) break;
            GETCHARLEN(c, eptr, len);
            if (c >= 256 ||(md->ctypes[c] & ctype_space) == 0) break;
            eptr+= len;
            }
          break;

          case OP_NOT_WORDCHAR:
          for (i = min; i < max; i++)
            {
            int len = 1;
            if (eptr >= md->end_subject) break;
            GETCHARLEN(c, eptr, len);
            if (c < 256 && (md->ctypes[c] & ctype_word) != 0) break;
            eptr+= len;
            }
          break;

          case OP_WORDCHAR:
          for (i = min; i < max; i++)
            {
            int len = 1;
            if (eptr >= md->end_subject) break;
            GETCHARLEN(c, eptr, len);
            if (c >= 256 || (md->ctypes[c] & ctype_word) == 0) break;
            eptr+= len;
            }
          break;

          default:
          RRETURN(PCRE_ERROR_INTERNAL);
          }

        /* eptr is now past the end of the maximum run */

        if (possessive) continue;
        for(;;)
          {
          RMATCH(rrc, eptr, ecode, offset_top, md, ims, eptrb, 0);
          if (rrc != MATCH_NOMATCH) RRETURN(rrc);
          if (eptr-- == pp) break;        /* Stop if tried at original pos */
          BACKCHAR(eptr);
          }
        }
      else
#endif

      /* Not UTF-8 mode */
        {
        switch(ctype)
          {
          case OP_ANY:
          if ((ims & PCRE_DOTALL) == 0)
            {
            for (i = min; i < max; i++)
              {
              if (eptr >= md->end_subject || IS_NEWLINE(eptr)) break;
              eptr++;
              }
            break;
            }
          /* For DOTALL case, fall through and treat as \C */

          case OP_ANYBYTE:
          c = max - min;
          if (c > (unsigned int)(md->end_subject - eptr))
            c = md->end_subject - eptr;
          eptr += c;
          break;

          case OP_ANYNL:
          for (i = min; i < max; i++)
            {
            if (eptr >= md->end_subject) break;
            c = *eptr;
            if (c == 0x000d)
              {
              if (++eptr >= md->end_subject) break;
              if (*eptr == 0x000a) eptr++;
              }
            else
              {
              if (c != 0x000a && c != 0x000b && c != 0x000c && c != 0x0085)
                break;
              eptr++;
              }
            }
          break;

          case OP_NOT_DIGIT:
          for (i = min; i < max; i++)
            {
            if (eptr >= md->end_subject || (md->ctypes[*eptr] & ctype_digit) != 0)
              break;
            eptr++;
            }
          break;

          case OP_DIGIT:
          for (i = min; i < max; i++)
            {
            if (eptr >= md->end_subject || (md->ctypes[*eptr] & ctype_digit) == 0)
              break;
            eptr++;
            }
          break;

          case OP_NOT_WHITESPACE:
          for (i = min; i < max; i++)
            {
            if (eptr >= md->end_subject || (md->ctypes[*eptr] & ctype_space) != 0)
              break;
            eptr++;
            }
          break;

          case OP_WHITESPACE:
          for (i = min; i < max; i++)
            {
            if (eptr >= md->end_subject || (md->ctypes[*eptr] & ctype_space) == 0)
              break;
            eptr++;
            }
          break;

          case OP_NOT_WORDCHAR:
          for (i = min; i < max; i++)
            {
            if (eptr >= md->end_subject || (md->ctypes[*eptr] & ctype_word) != 0)
              break;
            eptr++;
            }
          break;

          case OP_WORDCHAR:
          for (i = min; i < max; i++)
            {
            if (eptr >= md->end_subject || (md->ctypes[*eptr] & ctype_word) == 0)
              break;
            eptr++;
            }
          break;

          default:
          RRETURN(PCRE_ERROR_INTERNAL);
          }

        /* eptr is now past the end of the maximum run */

        if (possessive) continue;
        while (eptr >= pp)
          {
          RMATCH(rrc, eptr, ecode, offset_top, md, ims, eptrb, 0);
          eptr--;
          if (rrc != MATCH_NOMATCH) RRETURN(rrc);
          }
        }

      /* Get here if we can't make it match with any permitted repetitions */

      RRETURN(MATCH_NOMATCH);
      }
    /* Control never gets here */

    /* There's been some horrible disaster. Arrival here can only mean there is
    something seriously wrong in the code above or the OP_xxx definitions. */

    default:
    DPRINTF(("Unknown opcode %d\n", *ecode));
    RRETURN(PCRE_ERROR_UNKNOWN_OPCODE);
    }

  /* Do not stick any code in here without much thought; it is assumed
  that "continue" in the code above comes out to here to repeat the main
  loop. */

  }             /* End of main loop */
/* Control never reaches here */
}


/***************************************************************************
****************************************************************************
                   RECURSION IN THE match() FUNCTION

Undefine all the macros that were defined above to handle this. */

/* These two are defined as macros in both cases */

#undef fc
#undef fi

/***************************************************************************
***************************************************************************/



/*************************************************
*         Execute a Regular Expression           *
*************************************************/

/* This function applies a compiled re to a subject string and picks out
portions of the string if it matches. Two elements in the vector are set for
each substring: the offsets to the start and end of the substring.

Arguments:
  argument_re     points to the compiled expression
  extra_data      points to extra data or is NULL
  subject         points to the subject string
  length          length of subject string (may contain binary zeros)
  start_offset    where to start in the subject string
  options         option bits
  offsets         points to a vector of ints to be filled in with offsets
  offsetcount     the number of elements in the vector

Returns:          > 0 => success; value is the number of elements filled in
                  = 0 => success, but offsets is not big enough
                   -1 => failed to match
                 < -1 => some kind of unexpected problem
*/

int
pcre_exec(const pcre *argument_re, const pcre_extra *extra_data,
  PCRE_SPTR subject, int length, int start_offset, int options, int *offsets,
  int offsetcount)
{
int rc, resetcount, ocount;
int first_byte = -1;
int req_byte = -1;
int req_byte2 = -1;
int newline;
unsigned long int ims;
bool using_temporary_offsets = false;
bool anchored;
bool startline;
bool firstline;
bool first_byte_caseless = false;
bool req_byte_caseless = false;
bool utf8;
match_data match_block;
match_data *md = &match_block;
const uschar *tables;
const uschar *start_bits = NULL;
USPTR start_match = (USPTR)subject + start_offset;
USPTR end_subject;
USPTR req_byte_ptr = start_match - 1;
eptrblock eptrchain[EPTR_WORK_SIZE];

const pcre_study_data *study;

const real_pcre *external_re = (const real_pcre *)argument_re;
const real_pcre *re = external_re;

/* Plausibility checks */

if ((options & ~PUBLIC_EXEC_OPTIONS) != 0) return PCRE_ERROR_BADOPTION;
if (re == NULL || subject == NULL ||
   (offsets == NULL && offsetcount > 0)) return PCRE_ERROR_NULL;
if (offsetcount < 0) return PCRE_ERROR_BADCOUNT;

/* Fish out the optional data from the extra_data structure, first setting
the default values. */

study = NULL;
md->match_limit = MATCH_LIMIT;
md->match_limit_recursion = MATCH_LIMIT_RECURSION;
md->callout_data = NULL;

/* The table pointer is always in native byte order. */

tables = external_re->tables;

if (extra_data != NULL)
  {
  register unsigned int flags = extra_data->flags;
  if ((flags & PCRE_EXTRA_STUDY_DATA) != 0)
    study = (const pcre_study_data *)extra_data->study_data;
  if ((flags & PCRE_EXTRA_MATCH_LIMIT) != 0)
    md->match_limit = extra_data->match_limit;
  if ((flags & PCRE_EXTRA_MATCH_LIMIT_RECURSION) != 0)
    md->match_limit_recursion = extra_data->match_limit_recursion;
  if ((flags & PCRE_EXTRA_CALLOUT_DATA) != 0)
    md->callout_data = extra_data->callout_data;
  if ((flags & PCRE_EXTRA_TABLES) != 0) tables = extra_data->tables;
  }

/* If the exec call supplied NULL for tables, use the inbuilt ones. This
is a feature that makes it possible to save compiled regex and re-use them
in other programs later. */

if (tables == NULL) tables = _pcre_default_tables;

/* Check that the first field in the block is the magic number. If it is not,
test for a regex that was compiled on a host of opposite endianness. If this is
the case, flipped values are put in internal_re and internal_study if there was
study data too. */

if (re->magic_number != MAGIC_NUMBER)
  {
  return PCRE_ERROR_BADMAGIC;
  }

/* Set up other data */

anchored = ((re->options | options) & PCRE_ANCHORED) != 0;
startline = (re->options & PCRE_STARTLINE) != 0;
firstline = (re->options & PCRE_FIRSTLINE) != 0;

/* The code starts after the real_pcre block and the capture name table. */

md->start_code = (const uschar *)external_re + re->name_table_offset +
  re->name_count * re->name_entry_size;

md->start_subject = (USPTR)subject;
md->start_offset = start_offset;
md->end_subject = md->start_subject + length;
end_subject = md->end_subject;

md->endonly = (re->options & PCRE_DOLLAR_ENDONLY) != 0;
utf8 = md->utf8 = (re->options & PCRE_UTF8) != 0;

md->notbol = (options & PCRE_NOTBOL) != 0;
md->noteol = (options & PCRE_NOTEOL) != 0;
md->notempty = (options & PCRE_NOTEMPTY) != 0;
md->partial = (options & PCRE_PARTIAL) != 0;
md->hitend = false;

md->recursive = NULL;                   /* No recursion at top level */
md->eptrchain = eptrchain;              /* Make workspace generally available */

md->lcc = tables + lcc_offset;
md->ctypes = tables + ctypes_offset;

/* Handle different types of newline. The three bits give eight cases. If
nothing is set at run time, whatever was used at compile time applies. */

switch ((((options & PCRE_NEWLINE_BITS) == 0)? re->options : (pcre_uint32)options) &
       PCRE_NEWLINE_BITS)
  {
  case 0: newline = NEWLINE; break;   /* Compile-time default */
  case PCRE_NEWLINE_CR: newline = '\r'; break;
  case PCRE_NEWLINE_LF: newline = '\n'; break;
  case PCRE_NEWLINE_CR+
       PCRE_NEWLINE_LF: newline = ('\r' << 8) | '\n'; break;
  case PCRE_NEWLINE_ANY: newline = -1; break;
  case PCRE_NEWLINE_ANYCRLF: newline = -2; break;
  default: return PCRE_ERROR_BADNEWLINE;
  }

if (newline == -2)
  {
  md->nltype = NLTYPE_ANYCRLF;
  }
else if (newline < 0)
  {
  md->nltype = NLTYPE_ANY;
  }
else
  {
  md->nltype = NLTYPE_FIXED;
  if (newline > 255)
    {
    md->nllen = 2;
    md->nl[0] = (newline >> 8) & 255;
    md->nl[1] = newline & 255;
    }
  else
    {
    md->nllen = 1;
    md->nl[0] = newline;
    }
  }

/* Partial matching is supported only for a restricted set of regexes at the
moment. */

if (md->partial && (re->options & PCRE_NOPARTIAL) != 0)
  return PCRE_ERROR_BADPARTIAL;

/* Check a UTF-8 string if required. Unfortunately there's no way of passing
back the character offset. */

#ifdef SUPPORT_UTF8
if (utf8 && (options & PCRE_NO_UTF8_CHECK) == 0)
  {
  if (_pcre_valid_utf8((uschar *)subject, length) >= 0)
    return PCRE_ERROR_BADUTF8;
  if (start_offset > 0 && start_offset < length)
    {
    int tb = ((uschar *)subject)[start_offset];
    if (tb > 127)
      {
      tb &= 0xc0;
      if (tb != 0 && tb != 0xc0) return PCRE_ERROR_BADUTF8_OFFSET;
      }
    }
  }
#endif

/* The ims options can vary during the matching as a result of the presence
of (?ims) items in the pattern. They are kept in a local variable so that
restoring at the exit of a group is easy. */

ims = re->options & (PCRE_CASELESS|PCRE_MULTILINE|PCRE_DOTALL);

/* If the expression has got more back references than the offsets supplied can
hold, we get a temporary chunk of working store to use during the matching.
Otherwise, we can use the vector supplied, rounding down its size to a multiple
of 3. */

ocount = offsetcount - (offsetcount % 3);

if (re->top_backref > 0 && re->top_backref >= ocount/3)
  {
  ocount = re->top_backref * 3 + 3;
  md->offset_vector = static_cast<int *>(malloc(ocount * sizeof(int)));
  if (md->offset_vector == NULL) return PCRE_ERROR_NOMEMORY;
  using_temporary_offsets = true;
  DPRINTF(("Got memory to hold back references\n"));
  }
else md->offset_vector = offsets;

md->offset_end = ocount;
md->offset_max = (2*ocount)/3;
md->offset_overflow = false;
md->capture_last = -1;

/* Compute the minimum number of offsets that we need to reset each time. Doing
this makes a huge difference to execution time when there aren't many brackets
in the pattern. */

resetcount = 2 + re->top_bracket * 2;
if (resetcount > offsetcount) resetcount = ocount;

/* Reset the working variable associated with each extraction. These should
never be used unless previously set, but they get saved and restored, and so we
initialize them to avoid reading uninitialized locations. */

if (md->offset_vector != NULL)
  {
  register int *iptr = md->offset_vector + ocount;
  register int *iend = iptr - resetcount/2 + 1;
  while (--iptr >= iend) *iptr = -1;
  }

/* Set up the first character to match, if available. The first_byte value is
never set for an anchored regular expression, but the anchoring may be forced
at run time, so we have to test for anchoring. The first char may be unset for
an unanchored pattern, of course. If there's no first char and the pattern was
studied, there may be a bitmap of possible first characters. */

if (!anchored)
  {
  if ((re->options & PCRE_FIRSTSET) != 0)
    {
    first_byte = re->first_byte & 255;
    if ((first_byte_caseless = ((re->first_byte & REQ_CASELESS) != 0)) == true)
      first_byte = md->lcc[first_byte];
    }
  else
    if (!startline && study != NULL &&
      (study->options & PCRE_STUDY_MAPPED) != 0)
        start_bits = study->start_bits;
  }

/* For anchored or unanchored matches, there may be a "last known required
character" set. */

if ((re->options & PCRE_REQCHSET) != 0)
  {
  req_byte = re->req_byte & 255;
  req_byte_caseless = (re->req_byte & REQ_CASELESS) != 0;
  req_byte2 = (tables + fcc_offset)[req_byte];  /* case flipped */
  }


/* ==========================================================================*/

/* Loop for handling unanchored repeated matching attempts; for anchored regexs
the loop runs just once. */

for(;;)
  {
  USPTR save_end_subject = end_subject;

  /* Reset the maximum number of extractions we might see. */

  if (md->offset_vector != NULL)
    {
    register int *iptr = md->offset_vector;
    register int *iend = iptr + resetcount;
    while (iptr < iend) *iptr++ = -1;
    }

  /* Advance to a unique first char if possible. If firstline is true, the
  start of the match is constrained to the first line of a multiline string.
  That is, the match must be before or at the first newline. Implement this by
  temporarily adjusting end_subject so that we stop scanning at a newline. If
  the match fails at the newline, later code breaks this loop. */

  if (firstline)
    {
    USPTR t = start_match;
    while (t < md->end_subject && !IS_NEWLINE(t)) t++;
    end_subject = t;
    }

  /* Now test for a unique first byte */

  if (first_byte >= 0)
    {
    if (first_byte_caseless)
      while (start_match < end_subject &&
             md->lcc[*start_match] != first_byte)
        start_match++;
    else
      while (start_match < end_subject && *start_match != first_byte)
        start_match++;
    }

  /* Or to just after a linebreak for a multiline match if possible */

  else if (startline)
    {
    if (start_match > md->start_subject + start_offset)
      {
      while (start_match <= end_subject && !WAS_NEWLINE(start_match))
        start_match++;

      /* If we have just passed a CR and the newline option is ANY or ANYCRLF,
      and we are now at a LF, advance the match position by one more character.
      */

      if (start_match[-1] == '\r' &&
           (md->nltype == NLTYPE_ANY || md->nltype == NLTYPE_ANYCRLF) &&
           start_match < end_subject &&
           *start_match == '\n')
        start_match++;
      }
    }

  /* Or to a non-unique first char after study */

  else if (start_bits != NULL)
    {
    while (start_match < end_subject)
      {
      register unsigned int c = *start_match;
      if ((start_bits[c/8] & (1 << (c&7))) == 0) start_match++; else break;
      }
    }

  /* Restore fudged end_subject */

  end_subject = save_end_subject;

  /* If req_byte is set, we know that that character must appear in the subject
  for the match to succeed. If the first character is set, req_byte must be
  later in the subject; otherwise the test starts at the match point. This
  optimization can save a huge amount of backtracking in patterns with nested
  unlimited repeats that aren't going to match. Writing separate code for
  cased/caseless versions makes it go faster, as does using an autoincrement
  and backing off on a match.

  HOWEVER: when the subject string is very, very long, searching to its end can
  take a long time, and give bad performance on quite ordinary patterns. This
  showed up when somebody was matching something like /^\d+C/ on a 32-megabyte
  string... so we don't do this when the string is sufficiently long.

  ALSO: this processing is disabled when partial matching is requested.
  */

  if (req_byte >= 0 &&
      end_subject - start_match < REQ_BYTE_MAX &&
      !md->partial)
    {
    register USPTR p = start_match + ((first_byte >= 0)? 1 : 0);

    /* We don't need to repeat the search if we haven't yet reached the
    place we found it at last time. */

    if (p > req_byte_ptr)
      {
      if (req_byte_caseless)
        {
        while (p < end_subject)
          {
          register int pp = *p++;
          if (pp == req_byte || pp == req_byte2) { p--; break; }
          }
        }
      else
        {
        while (p < end_subject)
          {
          if (*p++ == req_byte) { p--; break; }
          }
        }

      /* If we can't find the required character, break the matching loop,
      forcing a match failure. */

      if (p >= end_subject)
        {
        rc = MATCH_NOMATCH;
        break;
        }

      /* If we have found the required character, save the point where we
      found it, so that we don't search again next time round the loop if
      the start hasn't passed this character yet. */

      req_byte_ptr = p;
      }
    }

  /* OK, we can now run the match. */

  md->start_match = start_match;
  md->match_call_count = 0;
  md->eptrn = 0;                          /* Next free eptrchain slot */
  rc = match(start_match, md->start_code, 2, md, ims, NULL, 0, 0);

  /* Any return other than MATCH_NOMATCH breaks the loop. */

  if (rc != MATCH_NOMATCH) break;

  /* If PCRE_FIRSTLINE is set, the match must happen before or at the first
  newline in the subject (though it may continue over the newline). Therefore,
  if we have just failed to match, starting at a newline, do not continue. */

  if (firstline && IS_NEWLINE(start_match)) break;

  /* Advance the match position by one character. */

  start_match++;
#ifdef SUPPORT_UTF8
  if (utf8)
    while(start_match < end_subject && (*start_match & 0xc0) == 0x80)
      start_match++;
#endif

  /* Break the loop if the pattern is anchored or if we have passed the end of
  the subject. */

  if (anchored || start_match > end_subject) break;

  /* If we have just passed a CR and the newline option is CRLF or ANY or
  ANYCRLF, and we are now at a LF, advance the match position by one more
  character. */

  if (start_match[-1] == '\r' &&
       (md->nltype == NLTYPE_ANY ||
        md->nltype == NLTYPE_ANYCRLF ||
        md->nllen == 2) &&
       start_match < end_subject &&
       *start_match == '\n')
    start_match++;

  }   /* End of for(;;) "bumpalong" loop */

/* ==========================================================================*/

/* We reach here when rc is not MATCH_NOMATCH, or if one of the stopping
conditions is true:

(1) The pattern is anchored;

(2) We are past the end of the subject;

(3) PCRE_FIRSTLINE is set and we have failed to match at a newline, because
    this option requests that a match occur at or before the first newline in
    the subject.

When we have a match and the offset vector is big enough to deal with any
backreferences, captured substring offsets will already be set up. In the case
where we had to get some local store to hold offsets for backreference
processing, copy those that we can. In this case there need not be overflow if
certain parts of the pattern were not used, even though there are more
capturing parentheses than vector slots. */

if (rc == MATCH_MATCH)
  {
  if (using_temporary_offsets)
    {
    if (offsetcount >= 4)
      {
      memcpy(offsets + 2, md->offset_vector + 2,
        (offsetcount - 2) * sizeof(int));
      DPRINTF(("Copied offsets from temporary memory\n"));
      }
    if (md->end_offset_top > offsetcount) md->offset_overflow = true;
    DPRINTF(("Freeing temporary memory\n"));
    free(md->offset_vector);
    }

  /* Set the return code to the number of captured strings, or 0 if there are
  too many to fit into the vector. */

  rc = md->offset_overflow? 0 : md->end_offset_top/2;

  /* If there is space, set up the whole thing as substring 0. */

  if (offsetcount < 2) rc = 0; else
    {
    offsets[0] = start_match - md->start_subject;
    offsets[1] = md->end_match_ptr - md->start_subject;
    }

  DPRINTF((">>>> returning %d\n", rc));
  return rc;
  }

/* Control gets here if there has been an error, or if the overall match
attempt has failed at all permitted starting positions. */

if (using_temporary_offsets)
  {
  DPRINTF(("Freeing temporary memory\n"));
  free(md->offset_vector);
  }

if (rc != MATCH_NOMATCH)
  {
  DPRINTF((">>>> error: returning %d\n", rc));
  return rc;
  }
else if (md->partial && md->hitend)
  {
  DPRINTF((">>>> returning PCRE_ERROR_PARTIAL\n"));
  return PCRE_ERROR_PARTIAL;
  }
else
  {
  DPRINTF((">>>> returning PCRE_ERROR_NOMATCH\n"));
  return PCRE_ERROR_NOMATCH;
  }
}

/* End pcre_exec.c */
#undef NLBLOCK
#undef PSSTART
#undef PSEND
