/*
 * co_nearest_color.c — Standalone nearest-color functions for test harness.
 *
 * Uses simple Euclidean distance in RGB space. The real libmux.so
 * uses CIE97 perceptual distance with K-d tree, but for unit testing
 * correctness of the rendering pipeline, exact palette choice doesn't
 * matter — only that a valid index is returned.
 */

#include "color_ops.h"

/* Standard xterm 16-color palette RGB values. */
static const struct { unsigned char r, g, b; } palette16[16] = {
    {  0,   0,   0}, {187,   0,   0}, {  0, 187,   0}, {187, 187,   0},
    {  0,   0, 187}, {187,   0, 187}, {  0, 187, 187}, {187, 187, 187},
    { 85,  85,  85}, {255,  85,  85}, { 85, 255,  85}, {255, 255,  85},
    { 85,  85, 255}, {255,  85, 255}, { 85, 255, 255}, {255, 255, 255},
};

int co_nearest_xterm16(const unsigned char *rgb)
{
    int best = 0;
    int best_dist = 256 * 256 * 3;
    for (int i = 0; i < 16; i++) {
        int dr = (int)rgb[0] - (int)palette16[i].r;
        int dg = (int)rgb[1] - (int)palette16[i].g;
        int db = (int)rgb[2] - (int)palette16[i].b;
        int dist = dr*dr + dg*dg + db*db;
        if (dist < best_dist) {
            best_dist = dist;
            best = i;
            if (dist == 0) break;
        }
    }
    return best;
}

int co_nearest_xterm256(const unsigned char *rgb)
{
    /* Simple 6x6x6 cube approximation for test harness. */
    int r = (rgb[0] + 25) / 51;
    int g = (rgb[1] + 25) / 51;
    int b = (rgb[2] + 25) / 51;
    if (r > 5) r = 5;
    if (g > 5) g = 5;
    if (b > 5) b = 5;
    return 16 + 36 * r + 6 * g + b;
}
