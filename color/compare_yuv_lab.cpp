// Compare YUV nearest-neighbor vs CIELAB nearest-neighbor for the XTERM
// 256-color palette.  For each of 16.7M RGB values, find the nearest palette
// entry under both metrics and report disagreements.

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

typedef long long INT64;

typedef struct { int r, g, b; } RGB;

// ---- YUV (original TinyMUX metric) ----

typedef struct { int y, u, v, y2; } YUV;

void rgb2yuv16(const RGB *rgb, YUV *yuv)
{
    int abs_y = ( 2104*rgb->r + 4310*rgb->g +  802*rgb->b + 4096 +  131072);
    int abs_u = (-1214*rgb->r - 2384*rgb->g + 3598*rgb->b + 4096 + 1048576);
    int abs_v = ( 3598*rgb->r - 3013*rgb->g -  585*rgb->b + 4096 + 1048576);
    if (abs_y < 0) abs_y = -abs_y;
    if (abs_u < 0) abs_u = -abs_u;
    if (abs_v < 0) abs_v = -abs_v;
    yuv->y = (abs_y >> 13); if (yuv->y > 235) yuv->y = 235;
    yuv->u = (abs_u >> 13); if (yuv->u > 240) yuv->u = 240;
    yuv->v = (abs_v >> 13); if (yuv->v > 240) yuv->v = 240;
    yuv->y2 = yuv->y + (yuv->y / 2);
}

INT64 diff_yuv(const YUV &a, const YUV &b)
{
    INT64 dy = (INT64)(a.y2 - b.y2);
    INT64 du = (INT64)(a.u - b.u);
    INT64 dv = (INT64)(a.v - b.v);
    return dy*dy + du*du + dv*dv;
}

// ---- CIELAB (CIE76) ----

typedef struct { double L, a, b; } LAB;
typedef struct { int L, a, b; } LABi;

static double srgb_to_linear(int c)
{
    double v = c / 255.0;
    return (v <= 0.04045) ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
}

static double cie_f(double t)
{
    return (t > 0.008856) ? cbrt(t) : 7.787 * t + 16.0 / 116.0;
}

void rgb2lab(const RGB *rgb, LABi *labi)
{
    double lr = srgb_to_linear(rgb->r);
    double lg = srgb_to_linear(rgb->g);
    double lb = srgb_to_linear(rgb->b);

    double x = (lr * 0.4124564 + lg * 0.3575761 + lb * 0.1804375) / 0.95047;
    double y = (lr * 0.2126729 + lg * 0.7151522 + lb * 0.0721750) / 1.00000;
    double z = (lr * 0.0193339 + lg * 0.1191920 + lb * 0.9503041) / 1.08883;

    double fx = cie_f(x), fy = cie_f(y), fz = cie_f(z);

    double L = 116.0 * fy - 16.0;
    double a = 500.0 * (fx - fy);
    double b = 200.0 * (fy - fz);

    labi->L = (int)(L * 100.0 + 0.5);
    labi->a = (int)(a * 100.0 + (a >= 0 ? 0.5 : -0.5));
    labi->b = (int)(b * 100.0 + (b >= 0 ? 0.5 : -0.5));
}

INT64 diff_lab(const LABi &a, const LABi &b)
{
    INT64 dL = (INT64)(a.L - b.L);
    INT64 da = (INT64)(a.a - b.a);
    INT64 db = (INT64)(a.b - b.b);
    return dL*dL + da*da + db*db;
}

// ---- Palette ----

struct ENTRY {
    RGB  rgb;
    YUV  yuv;
    LABi labi;
};

#define NUM_ENTRIES 256
ENTRY pal[NUM_ENTRIES];

// Same RGB values as kdtree.cpp / kdtree_lab.cpp
//
static const RGB palette_rgb[256] = {
    {  0,  0,  0},{187,  0,  0},{  0,187,  0},{187,187,  0},{  0,  0,187},{187,  0,187},{  0,187,187},{187,187,187},
    { 85, 85, 85},{255, 85, 85},{ 85,255, 85},{255,255, 85},{ 85, 85,255},{255, 85,255},{ 85,255,255},{255,255,255},
    {  0,  0,  0},{  0,  0, 95},{  0,  0,135},{  0,  0,175},{  0,  0,215},{  0,  0,255},
    {  0, 95,  0},{  0, 95, 95},{  0, 95,135},{  0, 95,175},{  0, 95,215},{  0, 95,255},
    {  0,135,  0},{  0,135, 95},{  0,135,135},{  0,135,175},{  0,135,215},{  0,135,255},
    {  0,175,  0},{  0,175, 95},{  0,175,135},{  0,175,175},{  0,175,215},{  0,175,255},
    {  0,215,  0},{  0,215, 95},{  0,215,135},{  0,215,175},{  0,215,215},{  0,215,255},
    {  0,255,  0},{  0,255, 95},{  0,255,135},{  0,255,175},{  0,255,215},{  0,255,255},
    { 95,  0,  0},{ 95,  0, 95},{ 95,  0,135},{ 95,  0,175},{ 95,  0,215},{ 95,  0,255},
    { 95, 95,  0},{ 95, 95, 95},{ 95, 95,135},{ 95, 95,175},{ 95, 95,215},{ 95, 95,255},
    { 95,135,  0},{ 95,135, 95},{ 95,135,135},{ 95,135,175},{ 95,135,215},{ 95,135,255},
    { 95,175,  0},{ 95,175, 95},{ 95,175,135},{ 95,175,175},{ 95,175,215},{ 95,175,255},
    { 95,215,  0},{ 95,215, 95},{ 95,215,135},{ 95,215,175},{ 95,215,215},{ 95,215,255},
    { 95,255,  0},{ 95,255, 95},{ 95,255,135},{ 95,255,175},{ 95,255,215},{ 95,255,255},
    {135,  0,  0},{135,  0, 95},{135,  0,135},{135,  0,175},{135,  0,215},{135,  0,255},
    {135, 95,  0},{135, 95, 95},{135, 95,135},{135, 95,175},{135, 95,215},{135, 95,255},
    {135,135,  0},{135,135, 95},{135,135,135},{135,135,175},{135,135,215},{135,135,255},
    {135,175,  0},{135,175, 95},{135,175,135},{135,175,175},{135,175,215},{135,175,255},
    {135,215,  0},{135,215, 95},{135,215,135},{135,215,175},{135,215,215},{135,215,255},
    {135,255,  0},{135,255, 95},{135,255,135},{135,255,175},{135,255,215},{135,255,255},
    {175,  0,  0},{175,  0, 95},{175,  0,135},{175,  0,175},{175,  0,215},{175,  0,255},
    {175, 95,  0},{175, 95, 95},{175, 95,135},{175, 95,175},{175, 95,215},{175, 95,255},
    {175,135,  0},{175,135, 95},{175,135,135},{175,135,175},{175,135,215},{175,135,255},
    {175,175,  0},{175,175, 95},{175,175,135},{175,175,175},{175,175,215},{175,175,255},
    {175,215,  0},{175,215, 95},{175,215,135},{175,215,175},{175,215,215},{175,215,255},
    {175,255,  0},{175,255, 95},{175,255,135},{175,255,175},{175,255,215},{175,255,255},
    {215,  0,  0},{215,  0, 95},{215,  0,135},{215,  0,175},{215,  0,215},{215,  0,255},
    {215, 95,  0},{215, 95, 95},{215, 95,135},{215, 95,175},{215, 95,215},{215, 95,255},
    {215,135,  0},{215,135, 95},{215,135,135},{215,135,175},{215,135,215},{215,135,255},
    {215,175,  0},{215,175, 95},{215,175,135},{215,175,175},{215,175,215},{215,175,255},
    {215,215,  0},{215,215, 95},{215,215,135},{215,215,175},{215,215,215},{215,215,255},
    {215,255,  0},{215,255, 95},{215,255,135},{215,255,175},{215,255,215},{215,255,255},
    {255,  0,  0},{255,  0, 95},{255,  0,135},{255,  0,175},{255,  0,215},{255,  0,255},
    {255, 95,  0},{255, 95, 95},{255, 95,135},{255, 95,175},{255, 95,215},{255, 95,255},
    {255,135,  0},{255,135, 95},{255,135,135},{255,135,175},{255,135,215},{255,135,255},
    {255,175,  0},{255,175, 95},{255,175,135},{255,175,175},{255,175,215},{255,175,255},
    {255,215,  0},{255,215, 95},{255,215,135},{255,215,175},{255,215,215},{255,215,255},
    {255,255,  0},{255,255, 95},{255,255,135},{255,255,175},{255,255,215},{255,255,255},
    {  8,  8,  8},{ 18, 18, 18},{ 28, 28, 28},{ 38, 38, 38},{ 48, 48, 48},{ 58, 58, 58},
    { 68, 68, 68},{ 78, 78, 78},{ 88, 88, 88},{ 98, 98, 98},{108,108,108},{118,118,118},
    {128,128,128},{138,138,138},{148,148,148},{158,158,158},{168,168,168},{178,178,178},
    {188,188,188},{198,198,198},{208,208,208},{218,218,218},{228,228,228},{238,238,238},
};

int main()
{
    // Initialize palette with both YUV and Lab representations
    //
    for (int i = 0; i < 256; i++)
    {
        pal[i].rgb = palette_rgb[i];
        rgb2yuv16(&pal[i].rgb, &pal[i].yuv);
        rgb2lab(&pal[i].rgb, &pal[i].labi);
    }

    // Compare: for each RGB, brute-force both metrics over entries 16-255
    //
    int disagree = 0;
    int tested = 0;
    int disagree_8 = 0;     // 8-color disagreements
    int disagree_16 = 0;    // 16-color disagreements

    RGB rgb;
    for (rgb.r = 0; rgb.r < 256; rgb.r++)
    {
        for (rgb.g = 0; rgb.g < 256; rgb.g++)
        {
            for (rgb.b = 0; rgb.b < 256; rgb.b++)
            {
                YUV yuv;
                LABi labi;
                rgb2yuv16(&rgb, &yuv);
                rgb2lab(&rgb, &labi);

                // 256-color: brute-force over entries 16-255
                //
                int iYUV = 16, iLAB = 16;
                INT64 rYUV = diff_yuv(yuv, pal[16].yuv);
                INT64 rLAB = diff_lab(labi, pal[16].labi);

                for (int i = 17; i < 256; i++)
                {
                    INT64 ry = diff_yuv(yuv, pal[i].yuv);
                    if (ry < rYUV) { rYUV = ry; iYUV = i; }

                    INT64 rl = diff_lab(labi, pal[i].labi);
                    if (rl < rLAB) { rLAB = rl; iLAB = i; }
                }

                if (iYUV != iLAB) disagree++;

                // 16-color
                //
                int iYUV16 = 0, iLAB16 = 0;
                INT64 rYUV16 = diff_yuv(yuv, pal[0].yuv);
                INT64 rLAB16 = diff_lab(labi, pal[0].labi);
                for (int i = 1; i < 16; i++)
                {
                    INT64 ry = diff_yuv(yuv, pal[i].yuv);
                    if (ry < rYUV16) { rYUV16 = ry; iYUV16 = i; }
                    INT64 rl = diff_lab(labi, pal[i].labi);
                    if (rl < rLAB16) { rLAB16 = rl; iLAB16 = i; }
                }
                if (iYUV16 != iLAB16) disagree_16++;

                // 8-color
                //
                int iYUV8 = 0, iLAB8 = 0;
                INT64 rYUV8 = diff_yuv(yuv, pal[0].yuv);
                INT64 rLAB8 = diff_lab(labi, pal[0].labi);
                for (int i = 1; i < 8; i++)
                {
                    INT64 ry = diff_yuv(yuv, pal[i].yuv);
                    if (ry < rYUV8) { rYUV8 = ry; iYUV8 = i; }
                    INT64 rl = diff_lab(labi, pal[i].labi);
                    if (rl < rLAB8) { rLAB8 = rl; iLAB8 = i; }
                }
                if (iYUV8 != iLAB8) disagree_8++;

                tested++;
            }
        }
        if (rgb.r % 32 == 0)
        {
            printf("  %d/256 R values done...\n", rgb.r);
        }
    }

    printf("\nResults (%d RGB values tested):\n", tested);
    printf("  256-color: %d disagree (%.2f%%)\n", disagree, 100.0 * disagree / tested);
    printf("  16-color:  %d disagree (%.2f%%)\n", disagree_16, 100.0 * disagree_16 / tested);
    printf("  8-color:   %d disagree (%.2f%%)\n", disagree_8, 100.0 * disagree_8 / tested);

    return 0;
}
