/* host_main.c -- host side of the color_ops three-way differential.
 *
 * Runs the identical case battery against the co_* functions as compiled
 * for the host (libmux), producing a transcript in the same format as the
 * guest.  This is the fourth route, and the one Macbook's host differential
 * already covered -- it is here so that "host vs guest" is a diff of two
 * files rather than an argument.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <color_ops.h>

static void o_str(const char *s) { fputs(s, stdout); }
static void o_uint(unsigned long long v) { printf("%llu", v); }
static void o_bytes(const unsigned char *p, unsigned long n)
{
    unsigned long i;
    for (i = 0; i < n; i++) printf("%02x", p[i]);
}

#include "cases.h"

int main(void)
{
    run_all(FUZZ_ITERS);
    fflush(stdout);
    return 0;
}
