/* msvc_compat.h — force-included when building the tools with MSVC.
 *
 * Ragel's -G2 C output declares its end-of-input pointer as
 *
 *     const char *eof __attribute__((unused)) = pe;
 *
 * __attribute__ is a GCC/Clang extension that MSVC does not accept, so the
 * checked-in unformat.c and reformat.c do not compile with cl.exe as they
 * stand.  The declaration comes out of ragel itself rather than out of the
 * .rl source, so there is nothing to fix upstream in unformat.rl, and the
 * generated .c files must not be hand-edited -- the Makefile regenerates
 * them wherever ragel is installed.
 *
 * Defining the keyword away is therefore the portable fix.  ((unused)) is
 * only a warning suppressant; discarding it costs nothing but an unreferenced
 * local warning, which the build silences separately.
 *
 * This header is force-included via /FI and is not referenced by any source
 * file, so it has no effect on the gcc build path.
 */

#ifndef TOOLS_MSVC_COMPAT_H
#define TOOLS_MSVC_COMPAT_H

#ifdef _MSC_VER
#define __attribute__(x)
#endif

#endif /* TOOLS_MSVC_COMPAT_H */
