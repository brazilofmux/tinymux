/*! \file svdrand.h
 * \brief Random Numbers — PCG-XSL-RR-128/64 (pcg64).
 *
 */

#ifndef SVDRAND_H
#define SVDRAND_H

LIBMUX_API void SeedRandomNumberGenerator();
LIBMUX_API int32_t RandomINT32(int32_t lLow, int32_t lHigh);

#endif // SVDRAND_H
