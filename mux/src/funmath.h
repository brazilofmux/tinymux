/*! \file funmath.h
 * \brief declarations for math-related functions.
 *
 */

#include "copyright.h"

#ifndef FUNMATH_H
#define FUNMATH_H

XFUNCTION(fun_add);
XFUNCTION(fun_ladd);
XFUNCTION(fun_iadd);
XFUNCTION(fun_sub);
XFUNCTION(fun_isub);
XFUNCTION(fun_mul);
XFUNCTION(fun_imul);
XFUNCTION(fun_gt);
XFUNCTION(fun_gte);
XFUNCTION(fun_lt);
XFUNCTION(fun_lte);
XFUNCTION(fun_eq);
XFUNCTION(fun_neq);
XFUNCTION(fun_max);
XFUNCTION(fun_lmax);
XFUNCTION(fun_lmin);
XFUNCTION(fun_min);
XFUNCTION(fun_sign);
XFUNCTION(fun_isign);
XFUNCTION(fun_shl);
XFUNCTION(fun_shr);
XFUNCTION(fun_inc);
XFUNCTION(fun_dec);
XFUNCTION(fun_trunc);
XFUNCTION(fun_fdiv);
XFUNCTION(fun_idiv);
XFUNCTION(fun_floordiv);
XFUNCTION(fun_mod);
XFUNCTION(fun_remainder);
XFUNCTION(fun_abs);
XFUNCTION(fun_iabs);
XFUNCTION(fun_dist2d);
XFUNCTION(fun_dist3d);
XFUNCTION(fun_vadd);
XFUNCTION(fun_vsub);
XFUNCTION(fun_vmul);
XFUNCTION(fun_vdot);
XFUNCTION(fun_vcross);
XFUNCTION(fun_vmag);
XFUNCTION(fun_vunit);
XFUNCTION(fun_floor);
XFUNCTION(fun_ceil);
XFUNCTION(fun_round);
XFUNCTION(fun_pi);
XFUNCTION(fun_e);
XFUNCTION(fun_ctu);
XFUNCTION(fun_sin);
XFUNCTION(fun_cos);
XFUNCTION(fun_tan);
XFUNCTION(fun_asin);
XFUNCTION(fun_acos);
XFUNCTION(fun_atan);
XFUNCTION(fun_atan2);
XFUNCTION(fun_exp);
XFUNCTION(fun_power);
XFUNCTION(fun_fmod);
XFUNCTION(fun_ln);
XFUNCTION(fun_log);
XFUNCTION(fun_sqrt);
XFUNCTION(fun_isnum);
XFUNCTION(fun_israt);
XFUNCTION(fun_isint);
XFUNCTION(fun_and);
XFUNCTION(fun_or);
XFUNCTION(fun_andbool);
XFUNCTION(fun_orbool);
XFUNCTION(fun_cand);
XFUNCTION(fun_cor);
XFUNCTION(fun_candbool);
XFUNCTION(fun_corbool);
XFUNCTION(fun_xor);
XFUNCTION(fun_not);
XFUNCTION(fun_t);
XFUNCTION(fun_spellnum);
XFUNCTION(fun_roman);
XFUNCTION(fun_land);
XFUNCTION(fun_lor);
XFUNCTION(fun_band);
XFUNCTION(fun_bor);
XFUNCTION(fun_bnand);
XFUNCTION(fun_bxor);
XFUNCTION(fun_crc32);
XFUNCTION(fun_sha1);
XFUNCTION(fun_digest);

bool mux_digest_sha1(const UTF8 *data[], const size_t lens[], int count, UINT8 *out_digest, unsigned int *out_len);

#endif // !FUNMATH_H
