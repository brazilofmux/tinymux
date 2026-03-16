#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

// sRGB to CIELAB conversion (D65 illuminant)
//
// Pipeline: sRGB (0-255) -> linear RGB (0-1) -> XYZ (D65) -> CIELAB
//
// References:
//   - IEC 61966-2-1:1999 (sRGB)
//   - CIE 15:2004 (CIELAB)
//   - TinyMUSH /tmp/tinymush/src/netmush/ansi.c (reference implementation)

typedef long long INT64;

typedef struct
{
    int r;
    int g;
    int b;
} RGB;

typedef struct
{
    double L;    // Lightness:       0 to 100
    double a;    // Green-Red axis: -128 to 128 (approximate)
    double b;    // Blue-Yellow:    -128 to 128 (approximate)
} LAB;

// For integer K-d tree operations, we scale L*a*b* to fixed point.
// L: 0-100 -> 0-10000  (multiply by 100)
// a: -128..128 -> -12800..12800
// b: -128..128 -> -12800..12800
//
// This gives sufficient precision for distance comparisons without
// floating-point in the search loop.
//
typedef struct
{
    int L;    // L * 100
    int a;    // a * 100
    int b;    // b * 100
} LABi;

typedef struct
{
    RGB   rgb;
    int   i256;
    bool  fDisable;
    LAB   lab;
    LABi  labi;
    int   child[2];
    int   color8;     // Nearest color in the 8-color palette
    int   color16;    // Nearest color in the 16-color palette
} ENTRY;

// D65 reference white point
//
#define D65_X 0.95047
#define D65_Y 1.00000
#define D65_Z 1.08883

// sRGB inverse gamma: convert 8-bit sRGB component to linear [0,1]
//
static double srgb_to_linear(int c)
{
    double v = c / 255.0;
    if (v <= 0.04045)
    {
        return v / 12.92;
    }
    else
    {
        return pow((v + 0.055) / 1.055, 2.4);
    }
}

// CIE f() function for XYZ -> Lab
//
static double cie_f(double t)
{
    if (t > 0.008856)   // (6/29)^3
    {
        return cbrt(t);
    }
    else
    {
        return 7.787 * t + 16.0 / 116.0;
    }
}

// Convert sRGB (0-255) to CIELAB
//
static void rgb2lab(const RGB *rgb, LAB *lab)
{
    // Step 1: sRGB inverse gamma
    //
    double lr = srgb_to_linear(rgb->r);
    double lg = srgb_to_linear(rgb->g);
    double lb = srgb_to_linear(rgb->b);

    // Step 2: Linear RGB to XYZ (sRGB matrix, D65)
    //
    double x = (lr * 0.4124564 + lg * 0.3575761 + lb * 0.1804375) / D65_X;
    double y = (lr * 0.2126729 + lg * 0.7151522 + lb * 0.0721750) / D65_Y;
    double z = (lr * 0.0193339 + lg * 0.1191920 + lb * 0.9503041) / D65_Z;

    // Step 3: XYZ to CIELAB
    //
    double fx = cie_f(x);
    double fy = cie_f(y);
    double fz = cie_f(z);

    lab->L = 116.0 * fy - 16.0;
    lab->a = 500.0 * (fx - fy);
    lab->b = 200.0 * (fy - fz);
}

// Convert floating-point Lab to scaled integer Lab
//
static void lab2labi(const LAB *lab, LABi *labi)
{
    labi->L = (int)(lab->L * 100.0 + 0.5);
    labi->a = (int)(lab->a * 100.0 + (lab->a >= 0 ? 0.5 : -0.5));
    labi->b = (int)(lab->b * 100.0 + (lab->b >= 0 ? 0.5 : -0.5));
}

// CIE76 color difference: Euclidean distance in CIELAB (squared)
//
// ΔE*ab = sqrt((L1-L2)^2 + (a1-a2)^2 + (b1-b2)^2)
//
// We return the squared distance to avoid sqrt in comparisons.
//
static INT64 diff(const LABi &lab1, const LABi &lab2)
{
    INT64 dL = (INT64)(lab1.L - lab2.L);
    INT64 da = (INT64)(lab1.a - lab2.a);
    INT64 db = (INT64)(lab1.b - lab2.b);
    return dL * dL + da * da + db * db;
}

// CIEDE2000 color difference (floating-point, for validation)
//
// Full implementation of the CIEDE2000 formula per CIE 142-2001.
//
static double ciede2000(const LAB &lab1, const LAB &lab2)
{
    double kL = 1.0, kC = 1.0, kH = 1.0;

    double C1 = sqrt(lab1.a * lab1.a + lab1.b * lab1.b);
    double C2 = sqrt(lab2.a * lab2.a + lab2.b * lab2.b);
    double Cab = (C1 + C2) / 2.0;

    double Cab7 = pow(Cab, 7.0);
    double G = 0.5 * (1.0 - sqrt(Cab7 / (Cab7 + 6103515625.0))); // 25^7

    double a1p = lab1.a * (1.0 + G);
    double a2p = lab2.a * (1.0 + G);

    double C1p = sqrt(a1p * a1p + lab1.b * lab1.b);
    double C2p = sqrt(a2p * a2p + lab2.b * lab2.b);

    double h1p = atan2(lab1.b, a1p);
    if (h1p < 0) h1p += 2.0 * M_PI;
    double h2p = atan2(lab2.b, a2p);
    if (h2p < 0) h2p += 2.0 * M_PI;

    double dLp = lab2.L - lab1.L;
    double dCp = C2p - C1p;

    double dhp;
    if (C1p * C2p == 0)
    {
        dhp = 0;
    }
    else if (fabs(h2p - h1p) <= M_PI)
    {
        dhp = h2p - h1p;
    }
    else if (h2p - h1p > M_PI)
    {
        dhp = h2p - h1p - 2.0 * M_PI;
    }
    else
    {
        dhp = h2p - h1p + 2.0 * M_PI;
    }

    double dHp = 2.0 * sqrt(C1p * C2p) * sin(dhp / 2.0);

    double Lp = (lab1.L + lab2.L) / 2.0;
    double Cp = (C1p + C2p) / 2.0;

    double hp;
    if (C1p * C2p == 0)
    {
        hp = h1p + h2p;
    }
    else if (fabs(h1p - h2p) <= M_PI)
    {
        hp = (h1p + h2p) / 2.0;
    }
    else if (h1p + h2p < 2.0 * M_PI)
    {
        hp = (h1p + h2p + 2.0 * M_PI) / 2.0;
    }
    else
    {
        hp = (h1p + h2p - 2.0 * M_PI) / 2.0;
    }

    double T = 1.0
        - 0.17 * cos(hp - M_PI / 6.0)
        + 0.24 * cos(2.0 * hp)
        + 0.32 * cos(3.0 * hp + M_PI / 30.0)
        - 0.20 * cos(4.0 * hp - 63.0 * M_PI / 180.0);

    double SL = 1.0 + 0.015 * (Lp - 50.0) * (Lp - 50.0) / sqrt(20.0 + (Lp - 50.0) * (Lp - 50.0));
    double SC = 1.0 + 0.045 * Cp;
    double SH = 1.0 + 0.015 * Cp * T;

    double Cp7 = pow(Cp, 7.0);
    double RC = 2.0 * sqrt(Cp7 / (Cp7 + 6103515625.0));
    double dtheta = 30.0 * M_PI / 180.0 * exp(-((hp * 180.0 / M_PI - 275.0) / 25.0) * ((hp * 180.0 / M_PI - 275.0) / 25.0));
    double RT = -sin(2.0 * dtheta) * RC;

    double dE = sqrt(
        (dLp / (kL * SL)) * (dLp / (kL * SL))
      + (dCp / (kC * SC)) * (dCp / (kC * SC))
      + (dHp / (kH * SH)) * (dHp / (kH * SH))
      + RT * (dCp / (kC * SC)) * (dHp / (kH * SH))
    );
    return dE;
}

//red (187,0,0)
//green (0,187,0)
//blue (0,0,187)
//cyan (0,187,187)
//magenta (187, 0, 187)
//yellow (187,187,0)
//black (0,0,0)
//white (187,187,187)

//bright red (255,85,85)
//bright green (85,255,85)
//bright blue (85,85,255)
//bright cyan (85,255,255)
//bright magenta (255,85,255)
//bright yellow (255,255,85)
//bright black (85,85,85)
//bright white (255,255,255)

#define NUM_ENTRIES (sizeof(table)/sizeof(table[0]))
ENTRY table[] =
{
    { {   0,   0,   0 },   0, true  }, // F600;6;COLOR FOREGROUND BLACK (0,0,0);XTERM 256-COLOR INDEX 0
    { { 187,   0,   0 },   1, true  }, // F601;7;COLOR FOREGROUND RED (187,0,0);XTERM 256-COLOR INDEX 1
    { {   0, 187,   0 },   2, true  }, // F602;8;COLOR FOREGROUND GREEN (0,187,0);XTERM 256-COLOR INDEX 2
    { { 187, 187,   0 },   3, true  }, // F603;9;COLOR FOREGROUND YELLOW (187,187,0);XTERM 256-COLOR INDEX 3
    { {   0,   0, 187 },   4, true  }, // F604;10;COLOR FOREGROUND BLUE (0,0,187);XTERM 256-COLOR INDEX 4
    { { 187,   0, 187 },   5, true  }, // F605;11;COLOR FOREGROUND MAGENTA (187,0,187);XTERM 256-COLOR INDEX 5
    { {   0, 187, 187 },   6, true  }, // F606;12;COLOR FOREGROUND CYAN (0,187,187);XTERM 256-COLOR INDEX 6
    { { 187, 187, 187 },   7, true  }, // F607;13;COLOR FOREGROUND WHITE (187,187,187);XTERM 256-COLOR INDEX 7
    { {  85,  85,  85 },   8, true  }, // F608;0;COLOR INTENSE FOREGROUND BLACK (85,85,85);XTERM 256-COLOR INDEX 8
    { { 255,  85,  85 },   9, true  }, // F609;0;COLOR INTENSE FOREGROUND RED (255,85,85);XTERM 256-COLOR INDEX 9
    { {  85, 255,  85 },  10, true  }, // F60A;0;COLOR INTENSE FOREGROUND GREEN (85,255,85);XTERM 256-COLOR INDEX 10
    { { 255, 255,  85 },  11, true  }, // F60B;0;COLOR INTENSE FOREGROUND YELLOW (255,255,85);XTERM 256-COLOR INDEX 11
    { {  85,  85, 255 },  12, true  }, // F60C;0;COLOR INTENSE FOREGROUND BLUE (85,85,255);XTERM 256-COLOR INDEX 12
    { { 255,  85, 255 },  13, true  }, // F60D;0;COLOR INTENSE FOREGROUND MAGENTA (255,85,255);XTERM 256-COLOR INDEX 13
    { {  85, 255, 255 },  14, true  }, // F60E;0;COLOR INTENSE FOREGROUND CYAN (85,255,255);XTERM 256-COLOR INDEX 14
    { { 255, 255, 255 },  15, true  }, // F60F;0;COLOR INTENSE FOREGROUND WHITE (255,255,255);XTERM 256-COLOR INDEX 15
    { {   0,   0,   0 },  16, false }, // F610;0;COLOR FOREGROUND (0,0,0);XTERM 256-COLOR INDEX 16
    { {   0,   0,  95 },  17, false }, // F611;0;COLOR FOREGROUND (0,0,95);XTERM 256-COLOR INDEX 17
    { {   0,   0, 135 },  18, false }, // F612;0;COLOR FOREGROUND (0,0,135);XTERM 256-COLOR INDEX 18
    { {   0,   0, 175 },  19, false }, // F613;0;COLOR FOREGROUND (0,0,175);XTERM 256-COLOR INDEX 19
    { {   0,   0, 215 },  20, false }, // F614;0;COLOR FOREGROUND (0,0,215);XTERM 256-COLOR INDEX 20
    { {   0,   0, 255 },  21, false }, // F615;0;COLOR FOREGROUND (0,0,255);XTERM 256-COLOR INDEX 21
    { {   0,  95,   0 },  22, false }, // F616;0;COLOR FOREGROUND (0,95,0);XTERM 256-COLOR INDEX 22
    { {   0,  95,  95 },  23, false }, // F617;0;COLOR FOREGROUND (0,95,95);XTERM 256-COLOR INDEX 23
    { {   0,  95, 135 },  24, false }, // F618;0;COLOR FOREGROUND (0,95,135);XTERM 256-COLOR INDEX 24
    { {   0,  95, 175 },  25, false }, // F619;0;COLOR FOREGROUND (0,95,175);XTERM 256-COLOR INDEX 25
    { {   0,  95, 215 },  26, false }, // F61A;0;COLOR FOREGROUND (0,95,215);XTERM 256-COLOR INDEX 26
    { {   0,  95, 255 },  27, false }, // F61B;0;COLOR FOREGROUND (0,95,255);XTERM 256-COLOR INDEX 27
    { {   0, 135,   0 },  28, false }, // F61C;0;COLOR FOREGROUND (0,135,0);XTERM 256-COLOR INDEX 28
    { {   0, 135,  95 },  29, false }, // F61D;0;COLOR FOREGROUND (0,135,95);XTERM 256-COLOR INDEX 29
    { {   0, 135, 135 },  30, false }, // F61E;0;COLOR FOREGROUND (0,135,135);XTERM 256-COLOR INDEX 30
    { {   0, 135, 175 },  31, false }, // F61F;0;COLOR FOREGROUND (0,135,175);XTERM 256-COLOR INDEX 31
    { {   0, 135, 215 },  32, false }, // F620;0;COLOR FOREGROUND (0,135,215);XTERM 256-COLOR INDEX 32
    { {   0, 135, 255 },  33, false }, // F621;0;COLOR FOREGROUND (0,135,255);XTERM 256-COLOR INDEX 33
    { {   0, 175,   0 },  34, false }, // F622;0;COLOR FOREGROUND (0,175,0);XTERM 256-COLOR INDEX 34
    { {   0, 175,  95 },  35, false }, // F623;0;COLOR FOREGROUND (0,175,95);XTERM 256-COLOR INDEX 35
    { {   0, 175, 135 },  36, false }, // F624;0;COLOR FOREGROUND (0,175,135);XTERM 256-COLOR INDEX 36
    { {   0, 175, 175 },  37, false }, // F625;0;COLOR FOREGROUND (0,175,175);XTERM 256-COLOR INDEX 37
    { {   0, 175, 215 },  38, false }, // F626;0;COLOR FOREGROUND (0,175,215);XTERM 256-COLOR INDEX 38
    { {   0, 175, 255 },  39, false }, // F627;0;COLOR FOREGROUND (0,175,255);XTERM 256-COLOR INDEX 39
    { {   0, 215,   0 },  40, false }, // F628;0;COLOR FOREGROUND (0,215,0);XTERM 256-COLOR INDEX 40
    { {   0, 215,  95 },  41, false }, // F629;0;COLOR FOREGROUND (0,215,95);XTERM 256-COLOR INDEX 41
    { {   0, 215, 135 },  42, false }, // F62A;0;COLOR FOREGROUND (0,215,135);XTERM 256-COLOR INDEX 42
    { {   0, 215, 175 },  43, false }, // F62B;0;COLOR FOREGROUND (0,215,175);XTERM 256-COLOR INDEX 43
    { {   0, 215, 215 },  44, false }, // F62C;0;COLOR FOREGROUND (0,215,215);XTERM 256-COLOR INDEX 44
    { {   0, 215, 255 },  45, false }, // F62D;0;COLOR FOREGROUND (0,215,255);XTERM 256-COLOR INDEX 45
    { {   0, 255,   0 },  46, false }, // F62E;0;COLOR FOREGROUND (0,255,0);XTERM 256-COLOR INDEX 46
    { {   0, 255,  95 },  47, false }, // F62F;0;COLOR FOREGROUND (0,255,95);XTERM 256-COLOR INDEX 47
    { {   0, 255, 135 },  48, false }, // F630;0;COLOR FOREGROUND (0,255,135);XTERM 256-COLOR INDEX 48
    { {   0, 255, 175 },  49, false }, // F631;0;COLOR FOREGROUND (0,255,175);XTERM 256-COLOR INDEX 49
    { {   0, 255, 215 },  50, false }, // F632;0;COLOR FOREGROUND (0,255,215);XTERM 256-COLOR INDEX 50
    { {   0, 255, 255 },  51, false }, // F633;0;COLOR FOREGROUND (0,255,255);XTERM 256-COLOR INDEX 51
    { {  95,   0,   0 },  52, false }, // F634;0;COLOR FOREGROUND (95,0,0);XTERM 256-COLOR INDEX 52
    { {  95,   0,  95 },  53, false }, // F635;0;COLOR FOREGROUND (95,0,95);XTERM 256-COLOR INDEX 53
    { {  95,   0, 135 },  54, false }, // F636;0;COLOR FOREGROUND (95,0,135);XTERM 256-COLOR INDEX 54
    { {  95,   0, 175 },  55, false }, // F637;0;COLOR FOREGROUND (95,0,175);XTERM 256-COLOR INDEX 55
    { {  95,   0, 215 },  56, false }, // F638;0;COLOR FOREGROUND (95,0,215);XTERM 256-COLOR INDEX 56
    { {  95,   0, 255 },  57, false }, // F639;0;COLOR FOREGROUND (95,0,255);XTERM 256-COLOR INDEX 57
    { {  95,  95,   0 },  58, false }, // F63A;0;COLOR FOREGROUND (95,95,0);XTERM 256-COLOR INDEX 58
    { {  95,  95,  95 },  59, false }, // F63B;0;COLOR FOREGROUND (95,95,95);XTERM 256-COLOR INDEX 59
    { {  95,  95, 135 },  60, false }, // F63C;0;COLOR FOREGROUND (95,95,135);XTERM 256-COLOR INDEX 60
    { {  95,  95, 175 },  61, false }, // F63D;0;COLOR FOREGROUND (95,95,175);XTERM 256-COLOR INDEX 61
    { {  95,  95, 215 },  62, false }, // F63E;0;COLOR FOREGROUND (95,95,215);XTERM 256-COLOR INDEX 62
    { {  95,  95, 255 },  63, false }, // F63F;0;COLOR FOREGROUND (95,95,255);XTERM 256-COLOR INDEX 63
    { {  95, 135,   0 },  64, false }, // F640;0;COLOR FOREGROUND (95,135,0);XTERM 256-COLOR INDEX 64
    { {  95, 135,  95 },  65, false }, // F641;0;COLOR FOREGROUND (95,135,95);XTERM 256-COLOR INDEX 65
    { {  95, 135, 135 },  66, false }, // F642;0;COLOR FOREGROUND (95,135,135);XTERM 256-COLOR INDEX 66
    { {  95, 135, 175 },  67, false }, // F643;0;COLOR FOREGROUND (95,135,175);XTERM 256-COLOR INDEX 67
    { {  95, 135, 215 },  68, false }, // F644;0;COLOR FOREGROUND (95,135,215);XTERM 256-COLOR INDEX 68
    { {  95, 135, 255 },  69, false }, // F645;0;COLOR FOREGROUND (95,135,255);XTERM 256-COLOR INDEX 69
    { {  95, 175,   0 },  70, false }, // F646;0;COLOR FOREGROUND (95,175,0);XTERM 256-COLOR INDEX 70
    { {  95, 175,  95 },  71, false }, // F647;0;COLOR FOREGROUND (95,175,95);XTERM 256-COLOR INDEX 71
    { {  95, 175, 135 },  72, false }, // F648;0;COLOR FOREGROUND (95,175,135);XTERM 256-COLOR INDEX 72
    { {  95, 175, 175 },  73, false }, // F649;0;COLOR FOREGROUND (95,175,175);XTERM 256-COLOR INDEX 73
    { {  95, 175, 215 },  74, false }, // F64A;0;COLOR FOREGROUND (95,175,215);XTERM 256-COLOR INDEX 74
    { {  95, 175, 255 },  75, false }, // F64B;0;COLOR FOREGROUND (95,175,255);XTERM 256-COLOR INDEX 75
    { {  95, 215,   0 },  76, false }, // F64C;0;COLOR FOREGROUND (95,215,0);XTERM 256-COLOR INDEX 76
    { {  95, 215,  95 },  77, false }, // F64D;0;COLOR FOREGROUND (95,215,95);XTERM 256-COLOR INDEX 77
    { {  95, 215, 135 },  78, false }, // F64E;0;COLOR FOREGROUND (95,215,135);XTERM 256-COLOR INDEX 78
    { {  95, 215, 175 },  79, false }, // F64F;0;COLOR FOREGROUND (95,215,175);XTERM 256-COLOR INDEX 79
    { {  95, 215, 215 },  80, false }, // F650;0;COLOR FOREGROUND (95,215,215);XTERM 256-COLOR INDEX 80
    { {  95, 215, 255 },  81, false }, // F651;0;COLOR FOREGROUND (95,215,255);XTERM 256-COLOR INDEX 81
    { {  95, 255,   0 },  82, false }, // F652;0;COLOR FOREGROUND (95,255,0);XTERM 256-COLOR INDEX 82
    { {  95, 255,  95 },  83, false }, // F653;0;COLOR FOREGROUND (95,255,95);XTERM 256-COLOR INDEX 83
    { {  95, 255, 135 },  84, false }, // F654;0;COLOR FOREGROUND (95,255,135);XTERM 256-COLOR INDEX 84
    { {  95, 255, 175 },  85, false }, // F655;0;COLOR FOREGROUND (95,255,175);XTERM 256-COLOR INDEX 85
    { {  95, 255, 215 },  86, false }, // F656;0;COLOR FOREGROUND (95,255,215);XTERM 256-COLOR INDEX 86
    { {  95, 255, 255 },  87, false }, // F657;0;COLOR FOREGROUND (95,255,255);XTERM 256-COLOR INDEX 87
    { { 135,   0,   0 },  88, false }, // F658;0;COLOR FOREGROUND (135,0,0);XTERM 256-COLOR INDEX 88
    { { 135,   0,  95 },  89, false }, // F659;0;COLOR FOREGROUND (135,0,95);XTERM 256-COLOR INDEX 89
    { { 135,   0, 135 },  90, false }, // F65A;0;COLOR FOREGROUND (135,0,135);XTERM 256-COLOR INDEX 90
    { { 135,   0, 175 },  91, false }, // F65B;0;COLOR FOREGROUND (135,0,175);XTERM 256-COLOR INDEX 91
    { { 135,   0, 215 },  92, false }, // F65C;0;COLOR FOREGROUND (135,0,215);XTERM 256-COLOR INDEX 92
    { { 135,   0, 255 },  93, false }, // F65D;0;COLOR FOREGROUND (135,0,255);XTERM 256-COLOR INDEX 93
    { { 135,  95,   0 },  94, false }, // F65E;0;COLOR FOREGROUND (135,95,0);XTERM 256-COLOR INDEX 94
    { { 135,  95,  95 },  95, false }, // F63F;0;COLOR FOREGROUND (135,95,95);XTERM 256-COLOR INDEX 95
    { { 135,  95, 135 },  96, false }, // F660;0;COLOR FOREGROUND (135,95,135);XTERM 256-COLOR INDEX 96
    { { 135,  95, 175 },  97, false }, // F661;0;COLOR FOREGROUND (135,95,175);XTERM 256-COLOR INDEX 97
    { { 135,  95, 215 },  98, false }, // F662;0;COLOR FOREGROUND (135,95,215);XTERM 256-COLOR INDEX 98
    { { 135,  95, 255 },  99, false }, // F663;0;COLOR FOREGROUND (135,95,255);XTERM 256-COLOR INDEX 99
    { { 135, 135,   0 }, 100, false }, // F664;0;COLOR FOREGROUND (135,135,0);XTERM 256-COLOR INDEX 100
    { { 135, 135,  95 }, 101, false }, // F665;0;COLOR FOREGROUND (135,135,95);XTERM 256-COLOR INDEX 101
    { { 135, 135, 135 }, 102, false }, // F666;0;COLOR FOREGROUND (135,135,135);XTERM 256-COLOR INDEX 102
    { { 135, 135, 175 }, 103, false }, // F667;0;COLOR FOREGROUND (135,135,175);XTERM 256-COLOR INDEX 103
    { { 135, 135, 215 }, 104, false }, // F668;0;COLOR FOREGROUND (135,135,215);XTERM 256-COLOR INDEX 104
    { { 135, 135, 255 }, 105, false }, // F669;0;COLOR FOREGROUND (135,135,255);XTERM 256-COLOR INDEX 105
    { { 135, 175,   0 }, 106, false }, // F66A;0;COLOR FOREGROUND (135,175,0);XTERM 256-COLOR INDEX 106
    { { 135, 175,  95 }, 107, false }, // F66B;0;COLOR FOREGROUND (135,175,95);XTERM 256-COLOR INDEX 107
    { { 135, 175, 135 }, 108, false }, // F66C;0;COLOR FOREGROUND (135,175,135);XTERM 256-COLOR INDEX 108
    { { 135, 175, 175 }, 109, false }, // F66D;0;COLOR FOREGROUND (135,175,175);XTERM 256-COLOR INDEX 109
    { { 135, 175, 215 }, 110, false }, // F66E;0;COLOR FOREGROUND (135,175,215);XTERM 256-COLOR INDEX 110
    { { 135, 175, 255 }, 111, false }, // F66F;0;COLOR FOREGROUND (135,175,255);XTERM 256-COLOR INDEX 111
    { { 135, 215,   0 }, 112, false }, // F670;0;COLOR FOREGROUND (135,215,0);XTERM 256-COLOR INDEX 112
    { { 135, 215,  95 }, 113, false }, // F671;0;COLOR FOREGROUND (135,215,95);XTERM 256-COLOR INDEX 113
    { { 135, 215, 135 }, 114, false }, // F672;0;COLOR FOREGROUND (135,215,135);XTERM 256-COLOR INDEX 114
    { { 135, 215, 175 }, 115, false }, // F673;0;COLOR FOREGROUND (135,215,175);XTERM 256-COLOR INDEX 115
    { { 135, 215, 215 }, 116, false }, // F674;0;COLOR FOREGROUND (135,215,215);XTERM 256-COLOR INDEX 116
    { { 135, 215, 255 }, 117, false }, // F675;0;COLOR FOREGROUND (135,215,255);XTERM 256-COLOR INDEX 117
    { { 135, 255,   0 }, 118, false }, // F676;0;COLOR FOREGROUND (135,255,0);XTERM 256-COLOR INDEX 118
    { { 135, 255,  95 }, 119, false }, // F677;0;COLOR FOREGROUND (135,255,95);XTERM 256-COLOR INDEX 119
    { { 135, 255, 135 }, 120, false }, // F678;0;COLOR FOREGROUND (135,255,135);XTERM 256-COLOR INDEX 120
    { { 135, 255, 175 }, 121, false }, // F679;0;COLOR FOREGROUND (135,255,175);XTERM 256-COLOR INDEX 121
    { { 135, 255, 215 }, 122, false }, // F67A;0;COLOR FOREGROUND (135,255,215);XTERM 256-COLOR INDEX 122
    { { 135, 255, 255 }, 123, false }, // F67B;0;COLOR FOREGROUND (135,255,255);XTERM 256-COLOR INDEX 123
    { { 175,   0,   0 }, 124, false }, // F67C;0;COLOR FOREGROUND (175,0,0);XTERM 256-COLOR INDEX 124
    { { 175,   0,  95 }, 125, false }, // F67D;0;COLOR FOREGROUND (175,0,95);XTERM 256-COLOR INDEX 125
    { { 175,   0, 135 }, 126, false }, // F67E;0;COLOR FOREGROUND (175,0,135);XTERM 256-COLOR INDEX 126
    { { 175,   0, 175 }, 127, false }, // F67F;0;COLOR FOREGROUND (175,0,175);XTERM 256-COLOR INDEX 127
    { { 175,   0, 215 }, 128, false }, // F680;0;COLOR FOREGROUND (175,0,215);XTERM 256-COLOR INDEX 128
    { { 175,   0, 255 }, 129, false }, // F681;0;COLOR FOREGROUND (175,0,255);XTERM 256-COLOR INDEX 129
    { { 175,  95,   0 }, 130, false }, // F682;0;COLOR FOREGROUND (175,95,0);XTERM 256-COLOR INDEX 130
    { { 175,  95,  95 }, 131, false }, // F683;0;COLOR FOREGROUND (175,95,95);XTERM 256-COLOR INDEX 131
    { { 175,  95, 135 }, 132, false }, // F684;0;COLOR FOREGROUND (175,95,135);XTERM 256-COLOR INDEX 132
    { { 175,  95, 175 }, 133, false }, // F685;0;COLOR FOREGROUND (175,95,175);XTERM 256-COLOR INDEX 133
    { { 175,  95, 215 }, 134, false }, // F686;0;COLOR FOREGROUND (175,95,215);XTERM 256-COLOR INDEX 134
    { { 175,  95, 255 }, 135, false }, // F687;0;COLOR FOREGROUND (175,95,255);XTERM 256-COLOR INDEX 135
    { { 175, 135,   0 }, 136, false }, // F688;0;COLOR FOREGROUND (175,135,0);XTERM 256-COLOR INDEX 136
    { { 175, 135,  95 }, 137, false }, // F689;0;COLOR FOREGROUND (175,135,95);XTERM 256-COLOR INDEX 137
    { { 175, 135, 135 }, 138, false }, // F68A;0;COLOR FOREGROUND (175,135,135);XTERM 256-COLOR INDEX 138
    { { 175, 135, 175 }, 139, false }, // F68B;0;COLOR FOREGROUND (175,135,175);XTERM 256-COLOR INDEX 139
    { { 175, 135, 215 }, 140, false }, // F68C;0;COLOR FOREGROUND (175,135,215);XTERM 256-COLOR INDEX 140
    { { 175, 135, 255 }, 141, false }, // F68D;0;COLOR FOREGROUND (175,135,255);XTERM 256-COLOR INDEX 141
    { { 175, 175,   0 }, 142, false }, // F68E;0;COLOR FOREGROUND (175,175,0);XTERM 256-COLOR INDEX 142
    { { 175, 175,  95 }, 143, false }, // F68F;0;COLOR FOREGROUND (175,175,95);XTERM 256-COLOR INDEX 143
    { { 175, 175, 135 }, 144, false }, // F690;0;COLOR FOREGROUND (175,175,135);XTERM 256-COLOR INDEX 144
    { { 175, 175, 175 }, 145, false }, // F691;0;COLOR FOREGROUND (175,175,175);XTERM 256-COLOR INDEX 145
    { { 175, 175, 215 }, 146, false }, // F692;0;COLOR FOREGROUND (175,175,215);XTERM 256-COLOR INDEX 146
    { { 175, 175, 255 }, 147, false }, // F693;0;COLOR FOREGROUND (175,175,255);XTERM 256-COLOR INDEX 147
    { { 175, 215,   0 }, 148, false }, // F694;0;COLOR FOREGROUND (175,215,0);XTERM 256-COLOR INDEX 148
    { { 175, 215,  95 }, 149, false }, // F695;0;COLOR FOREGROUND (175,215,95);XTERM 256-COLOR INDEX 149
    { { 175, 215, 135 }, 150, false }, // F696;0;COLOR FOREGROUND (175,215,135);XTERM 256-COLOR INDEX 150
    { { 175, 215, 175 }, 151, false }, // F697;0;COLOR FOREGROUND (175,215,175);XTERM 256-COLOR INDEX 151
    { { 175, 215, 215 }, 152, false }, // F698;0;COLOR FOREGROUND (175,215,215);XTERM 256-COLOR INDEX 152
    { { 175, 215, 255 }, 153, false }, // F699;0;COLOR FOREGROUND (175,215,255);XTERM 256-COLOR INDEX 153
    { { 175, 255,   0 }, 154, false }, // F69A;0;COLOR FOREGROUND (175,255,0);XTERM 256-COLOR INDEX 154
    { { 175, 255,  95 }, 155, false }, // F69B;0;COLOR FOREGROUND (175,255,95);XTERM 256-COLOR INDEX 155
    { { 175, 255, 135 }, 156, false }, // F69C;0;COLOR FOREGROUND (175,255,135);XTERM 256-COLOR INDEX 156
    { { 175, 255, 175 }, 157, false }, // F69D;0;COLOR FOREGROUND (175,255,175);XTERM 256-COLOR INDEX 157
    { { 175, 255, 215 }, 158, false }, // F69E;0;COLOR FOREGROUND (175,255,215);XTERM 256-COLOR INDEX 158
    { { 175, 255, 255 }, 159, false }, // F69F;0;COLOR FOREGROUND (175,255,255);XTERM 256-COLOR INDEX 159
    { { 215,   0,   0 }, 160, false }, // F6A0;0;COLOR FOREGROUND (215,0,0);XTERM 256-COLOR INDEX 160
    { { 215,   0,  95 }, 161, false }, // F6A1;0;COLOR FOREGROUND (215,0,95);XTERM 256-COLOR INDEX 161
    { { 215,   0, 135 }, 162, false }, // F6A2;0;COLOR FOREGROUND (215,0,135);XTERM 256-COLOR INDEX 162
    { { 215,   0, 175 }, 163, false }, // F6A3;0;COLOR FOREGROUND (215,0,175);XTERM 256-COLOR INDEX 163
    { { 215,   0, 215 }, 164, false }, // F6A4;0;COLOR FOREGROUND (215,0,215);XTERM 256-COLOR INDEX 164
    { { 215,   0, 255 }, 165, false }, // F6A5;0;COLOR FOREGROUND (215,0,255);XTERM 256-COLOR INDEX 165
    { { 215,  95,   0 }, 166, false }, // F6A6;0;COLOR FOREGROUND (215,95,0);XTERM 256-COLOR INDEX 166
    { { 215,  95,  95 }, 167, false }, // F6A7;0;COLOR FOREGROUND (215,95,95);XTERM 256-COLOR INDEX 167
    { { 215,  95, 135 }, 168, false }, // F6A8;0;COLOR FOREGROUND (215,95,135);XTERM 256-COLOR INDEX 168
    { { 215,  95, 175 }, 169, false }, // F6A9;0;COLOR FOREGROUND (215,95,175);XTERM 256-COLOR INDEX 169
    { { 215,  95, 215 }, 170, false }, // F6AA;0;COLOR FOREGROUND (215,95,215);XTERM 256-COLOR INDEX 170
    { { 215,  95, 255 }, 171, false }, // F6AB;0;COLOR FOREGROUND (215,95,255);XTERM 256-COLOR INDEX 171
    { { 215, 135,   0 }, 172, false }, // F6AC;0;COLOR FOREGROUND (215,135,0);XTERM 256-COLOR INDEX 172
    { { 215, 135,  95 }, 173, false }, // F6AD;0;COLOR FOREGROUND (215,135,95);XTERM 256-COLOR INDEX 173
    { { 215, 135, 135 }, 174, false }, // F6AE;0;COLOR FOREGROUND (215,135,135);XTERM 256-COLOR INDEX 174
    { { 215, 135, 175 }, 175, false }, // F6AF;0;COLOR FOREGROUND (215,135,175);XTERM 256-COLOR INDEX 175
    { { 215, 135, 215 }, 176, false }, // F6B0;0;COLOR FOREGROUND (215,135,215);XTERM 256-COLOR INDEX 176
    { { 215, 135, 255 }, 177, false }, // F6B1;0;COLOR FOREGROUND (215,135,255);XTERM 256-COLOR INDEX 177
    { { 215, 175,   0 }, 178, false }, // F6B2;0;COLOR FOREGROUND (215,175,0);XTERM 256-COLOR INDEX 178
    { { 215, 175,  95 }, 179, false }, // F6B3;0;COLOR FOREGROUND (215,175,95);XTERM 256-COLOR INDEX 179
    { { 215, 175, 135 }, 180, false }, // F6B4;0;COLOR FOREGROUND (215,175,135);XTERM 256-COLOR INDEX 180
    { { 215, 175, 175 }, 181, false }, // F6B5;0;COLOR FOREGROUND (215,175,175);XTERM 256-COLOR INDEX 181
    { { 215, 175, 215 }, 182, false }, // F6B6;0;COLOR FOREGROUND (215,175,215);XTERM 256-COLOR INDEX 182
    { { 215, 175, 255 }, 183, false }, // F6B7;0;COLOR FOREGROUND (215,175,255);XTERM 256-COLOR INDEX 183
    { { 215, 215,   0 }, 184, false }, // F6B8;0;COLOR FOREGROUND (215,215,0);XTERM 256-COLOR INDEX 184
    { { 215, 215,  95 }, 185, false }, // F6B9;0;COLOR FOREGROUND (215,215,95);XTERM 256-COLOR INDEX 185
    { { 215, 215, 135 }, 186, false }, // F6BA;0;COLOR FOREGROUND (215,215,135);XTERM 256-COLOR INDEX 186
    { { 215, 215, 175 }, 187, false }, // F6BB;0;COLOR FOREGROUND (215,215,175);XTERM 256-COLOR INDEX 187
    { { 215, 215, 215 }, 188, false }, // F6BC;0;COLOR FOREGROUND (215,215,215);XTERM 256-COLOR INDEX 188
    { { 215, 215, 255 }, 189, false }, // F6BD;0;COLOR FOREGROUND (215,215,255);XTERM 256-COLOR INDEX 189
    { { 215, 255,   0 }, 190, false }, // F6BE;0;COLOR FOREGROUND (215,255,0);XTERM 256-COLOR INDEX 190
    { { 215, 255,  95 }, 191, false }, // F6BF;0;COLOR FOREGROUND (215,255,95);XTERM 256-COLOR INDEX 191
    { { 215, 255, 135 }, 192, false }, // F6C0;0;COLOR FOREGROUND (215,255,135);XTERM 256-COLOR INDEX 192
    { { 215, 255, 175 }, 193, false }, // F6C1;0;COLOR FOREGROUND (215,255,175);XTERM 256-COLOR INDEX 193
    { { 215, 255, 215 }, 194, false }, // F6C2;0;COLOR FOREGROUND (215,255,215);XTERM 256-COLOR INDEX 194
    { { 215, 255, 255 }, 195, false }, // F6C3;0;COLOR FOREGROUND (215,255,255);XTERM 256-COLOR INDEX 195
    { { 255,   0,   0 }, 196, false }, // F6C4;0;COLOR FOREGROUND (255,0,0);XTERM 256-COLOR INDEX 196
    { { 255,   0,  95 }, 197, false }, // F6C5;0;COLOR FOREGROUND (255,0,95);XTERM 256-COLOR INDEX 197
    { { 255,   0, 135 }, 198, false }, // F6C6;0;COLOR FOREGROUND (255,0,135);XTERM 256-COLOR INDEX 198
    { { 255,   0, 175 }, 199, false }, // F6C7;0;COLOR FOREGROUND (255,0,175);XTERM 256-COLOR INDEX 199
    { { 255,   0, 215 }, 200, false }, // F6C8;0;COLOR FOREGROUND (255,0,215);XTERM 256-COLOR INDEX 200
    { { 255,   0, 255 }, 201, false }, // F6C9;0;COLOR FOREGROUND (255,0,255);XTERM 256-COLOR INDEX 201
    { { 255,  95,   0 }, 202, false }, // F6CA;0;COLOR FOREGROUND (255,95,0);XTERM 256-COLOR INDEX 202
    { { 255,  95,  95 }, 203, false }, // F6CB;0;COLOR FOREGROUND (255,95,95);XTERM 256-COLOR INDEX 203
    { { 255,  95, 135 }, 204, false }, // F6CC;0;COLOR FOREGROUND (255,95,135);XTERM 256-COLOR INDEX 204
    { { 255,  95, 175 }, 205, false }, // F6CD;0;COLOR FOREGROUND (255,95,175);XTERM 256-COLOR INDEX 205
    { { 255,  95, 215 }, 206, false }, // F6CE;0;COLOR FOREGROUND (255,95,215);XTERM 256-COLOR INDEX 206
    { { 255,  95, 255 }, 207, false }, // F6CF;0;COLOR FOREGROUND (255,95,255);XTERM 256-COLOR INDEX 207
    { { 255, 135,   0 }, 208, false }, // F6D0;0;COLOR FOREGROUND (255,135,0);XTERM 256-COLOR INDEX 208
    { { 255, 135,  95 }, 209, false }, // F6D1;0;COLOR FOREGROUND (255,135,95);XTERM 256-COLOR INDEX 209
    { { 255, 135, 135 }, 210, false }, // F6D2;0;COLOR FOREGROUND (255,135,135);XTERM 256-COLOR INDEX 210
    { { 255, 135, 175 }, 211, false }, // F6D3;0;COLOR FOREGROUND (255,135,175);XTERM 256-COLOR INDEX 211
    { { 255, 135, 215 }, 212, false }, // F6D4;0;COLOR FOREGROUND (255,135,215);XTERM 256-COLOR INDEX 212
    { { 255, 135, 255 }, 213, false }, // F6D5;0;COLOR FOREGROUND (255,135,255);XTERM 256-COLOR INDEX 213
    { { 255, 175,   0 }, 214, false }, // F6D6;0;COLOR FOREGROUND (255,175,0);XTERM 256-COLOR INDEX 214
    { { 255, 175,  95 }, 215, false }, // F6D7;0;COLOR FOREGROUND (255,175,95);XTERM 256-COLOR INDEX 215
    { { 255, 175, 135 }, 216, false }, // F6D8;0;COLOR FOREGROUND (255,175,135);XTERM 256-COLOR INDEX 216
    { { 255, 175, 175 }, 217, false }, // F6D9;0;COLOR FOREGROUND (255,175,175);XTERM 256-COLOR INDEX 217
    { { 255, 175, 215 }, 218, false }, // F6DA;0;COLOR FOREGROUND (255,175,215);XTERM 256-COLOR INDEX 218
    { { 255, 175, 255 }, 219, false }, // F6DB;0;COLOR FOREGROUND (255,175,255);XTERM 256-COLOR INDEX 219
    { { 255, 215,   0 }, 220, false }, // F6DC;0;COLOR FOREGROUND (255,215,0);XTERM 256-COLOR INDEX 220
    { { 255, 215,  95 }, 221, false }, // F6DD;0;COLOR FOREGROUND (255,215,95);XTERM 256-COLOR INDEX 221
    { { 255, 215, 135 }, 222, false }, // F6DE;0;COLOR FOREGROUND (255,215,135);XTERM 256-COLOR INDEX 222
    { { 255, 215, 175 }, 223, false }, // F6DF;0;COLOR FOREGROUND (255,215,175);XTERM 256-COLOR INDEX 223
    { { 255, 215, 215 }, 224, false }, // F6E0;0;COLOR FOREGROUND (255,215,215);XTERM 256-COLOR INDEX 224
    { { 255, 215, 255 }, 225, false }, // F6E1;0;COLOR FOREGROUND (255,215,255);XTERM 256-COLOR INDEX 225
    { { 255, 255,   0 }, 226, false }, // F6E2;0;COLOR FOREGROUND (255,255,0);XTERM 256-COLOR INDEX 226
    { { 255, 255,  95 }, 227, false }, // F6E3;0;COLOR FOREGROUND (255,255,95);XTERM 256-COLOR INDEX 227
    { { 255, 255, 135 }, 228, false }, // F6E4;0;COLOR FOREGROUND (255,255,135);XTERM 256-COLOR INDEX 228
    { { 255, 255, 175 }, 229, false }, // F6E5;0;COLOR FOREGROUND (255,255,175);XTERM 256-COLOR INDEX 229
    { { 255, 255, 215 }, 230, false }, // F6E6;0;COLOR FOREGROUND (255,255,215);XTERM 256-COLOR INDEX 230
    { { 255, 255, 255 }, 231, false }, // F6E7;0;COLOR FOREGROUND (255,255,255);XTERM 256-COLOR INDEX 231
    { {   8,   8,   8 }, 232, false }, // F6E8;0;COLOR FOREGROUND (8,8,8);XTERM 256-COLOR INDEX 232
    { {  18,  18,  18 }, 233, false }, // F6E9;0;COLOR FOREGROUND (18,18,18);XTERM 256-COLOR INDEX 233
    { {  28,  28,  28 }, 234, false }, // F6EA;0;COLOR FOREGROUND (28,28,28);XTERM 256-COLOR INDEX 234
    { {  38,  38,  38 }, 235, false }, // F6EB;0;COLOR FOREGROUND (38,38,38);XTERM 256-COLOR INDEX 235
    { {  48,  48,  48 }, 236, false }, // F6EC;0;COLOR FOREGROUND (48,48,48);XTERM 256-COLOR INDEX 236
    { {  58,  58,  58 }, 237, false }, // F6ED;0;COLOR FOREGROUND (58,58,58);XTERM 256-COLOR INDEX 237
    { {  68,  68,  68 }, 238, false }, // F6EE;0;COLOR FOREGROUND (68,68,68);XTERM 256-COLOR INDEX 238
    { {  78,  78,  78 }, 239, false }, // F6EF;0;COLOR FOREGROUND (78,78,78);XTERM 256-COLOR INDEX 239
    { {  88,  88,  88 }, 240, false }, // F6F0;0;COLOR FOREGROUND (88,88,88);XTERM 256-COLOR INDEX 240
    { {  98,  98,  98 }, 241, false }, // F6F1;0;COLOR FOREGROUND (98,98,98);XTERM 256-COLOR INDEX 241
    { { 108, 108, 108 }, 242, false }, // F6F2;0;COLOR FOREGROUND (108,108,108);XTERM 256-COLOR INDEX 242
    { { 118, 118, 118 }, 243, false }, // F6F3;0;COLOR FOREGROUND (118,118,118);XTERM 256-COLOR INDEX 243
    { { 128, 128, 128 }, 244, false }, // F6F4;0;COLOR FOREGROUND (128,128,128);XTERM 256-COLOR INDEX 244
    { { 138, 138, 138 }, 245, false }, // F6F5;0;COLOR FOREGROUND (138,138,138);XTERM 256-COLOR INDEX 245
    { { 148, 148, 148 }, 246, false }, // F6F6;0;COLOR FOREGROUND (148,148,148);XTERM 256-COLOR INDEX 246
    { { 158, 158, 158 }, 247, false }, // F6F7;0;COLOR FOREGROUND (158,158,158);XTERM 256-COLOR INDEX 247
    { { 168, 168, 168 }, 248, false }, // F6F8;0;COLOR FOREGROUND (168,168,168);XTERM 256-COLOR INDEX 248
    { { 178, 178, 178 }, 249, false }, // F6F9;0;COLOR FOREGROUND (178,178,178);XTERM 256-COLOR INDEX 249
    { { 188, 188, 188 }, 250, false }, // F6FA;0;COLOR FOREGROUND (188,188,188);XTERM 256-COLOR INDEX 250
    { { 198, 198, 198 }, 251, false }, // F6FB;0;COLOR FOREGROUND (198,198,198);XTERM 256-COLOR INDEX 251
    { { 208, 208, 208 }, 252, false }, // F6FC;0;COLOR FOREGROUND (208,208,208);XTERM 256-COLOR INDEX 252
    { { 218, 218, 218 }, 253, false }, // F6FD;0;COLOR FOREGROUND (218,218,218);XTERM 256-COLOR INDEX 253
    { { 228, 228, 228 }, 254, false }, // F6FE;0;COLOR FOREGROUND (228,228,228);XTERM 256-COLOR INDEX 254
    { { 238, 238, 238 }, 255, false }, // F6FF;0;COLOR FOREGROUND (238,238,238);XTERM 256-COLOR INDEX 255
};

// Brute-force nearest neighbor (reference implementation for validation)
//
int NearestIndex_brute(const LABi &labi, bool fDisable)
{
    int iNearest = -1;
    INT64 rNearest = 0;

    for (int i = 0; i < (int)NUM_ENTRIES; i++)
    {
        if (table[i].fDisable != fDisable) continue;

        INT64 r = diff(table[i].labi, labi);
        if (iNearest == -1 || r < rNearest)
        {
            rNearest = r;
            iNearest = i;
        }
    }
    return iNearest;
}

// K-d tree construction: sort comparators for L, a, b axes
//
int L_comp(const void *s1, const void *s2)
{
    ENTRY *pa = &table[*(int *)s1];
    ENTRY *pb = &table[*(int *)s2];
    if (pa->labi.L > pb->labi.L) return 1;
    if (pa->labi.L < pb->labi.L) return -1;
    return 0;
}

int a_comp(const void *s1, const void *s2)
{
    ENTRY *pa = &table[*(int *)s1];
    ENTRY *pb = &table[*(int *)s2];
    if (pa->labi.a > pb->labi.a) return 1;
    if (pa->labi.a < pb->labi.a) return -1;
    return 0;
}

int b_comp(const void *s1, const void *s2)
{
    ENTRY *pa = &table[*(int *)s1];
    ENTRY *pb = &table[*(int *)s2];
    if (pa->labi.b > pb->labi.b) return 1;
    if (pa->labi.b < pb->labi.b) return -1;
    return 0;
}

// Build K-d tree recursively, cycling through L, a, b axes
//
int kdtree(int npts_arg, int pts_arg[], int depth)
{
    if (0 == npts_arg) return -1;

    int *pts = new int[npts_arg];
    for (int i = 0; i < npts_arg; i++)
    {
        pts[i] = pts_arg[i];
    }

    int axis = depth % 3;
    if (0 == axis)
    {
        qsort(pts, npts_arg, sizeof(int), L_comp);
    }
    else if (1 == axis)
    {
        qsort(pts, npts_arg, sizeof(int), a_comp);
    }
    else
    {
        qsort(pts, npts_arg, sizeof(int), b_comp);
    }
    int iMedian = npts_arg / 2;
    int median = pts[iMedian];
    table[median].child[0] = kdtree(iMedian, pts, depth + 1);
    table[median].child[1] = kdtree(npts_arg - iMedian - 1, pts + iMedian + 1, depth + 1);

    delete [] pts;
    return median;
}

// K-d tree nearest-neighbor search: three mutually-recursive functions
// cycling through L*, a*, b* axes.
//
void NearestIndex_tree_a(int iHere, const LABi &labi, int &iBest, INT64 &rBest);
void NearestIndex_tree_b(int iHere, const LABi &labi, int &iBest, INT64 &rBest);

void NearestIndex_tree_L(int iHere, const LABi &labi, int &iBest, INT64 &rBest)
{
    if (-1 == iHere)
    {
        return;
    }

    if (-1 == iBest)
    {
        iBest = iHere;
        rBest = diff(labi, table[iBest].labi);
    }

    INT64 rHere = diff(labi, table[iHere].labi);
    if (rHere < rBest)
    {
        iBest = iHere;
        rBest = rHere;
    }

    INT64 d = (INT64)(labi.L - table[iHere].labi.L);
    int iNearChild = (d < 0) ? 0 : 1;
    NearestIndex_tree_a(table[iHere].child[iNearChild], labi, iBest, rBest);

    INT64 rAxis = d * d;
    if (rAxis < rBest)
    {
        NearestIndex_tree_a(table[iHere].child[1 - iNearChild], labi, iBest, rBest);
    }
}

void NearestIndex_tree_a(int iHere, const LABi &labi, int &iBest, INT64 &rBest)
{
    if (-1 == iHere)
    {
        return;
    }

    if (-1 == iBest)
    {
        iBest = iHere;
        rBest = diff(labi, table[iBest].labi);
    }

    INT64 rHere = diff(labi, table[iHere].labi);
    if (rHere < rBest)
    {
        iBest = iHere;
        rBest = rHere;
    }

    INT64 d = (INT64)(labi.a - table[iHere].labi.a);
    int iNearChild = (d < 0) ? 0 : 1;
    NearestIndex_tree_b(table[iHere].child[iNearChild], labi, iBest, rBest);

    INT64 rAxis = d * d;
    if (rAxis < rBest)
    {
        NearestIndex_tree_b(table[iHere].child[1 - iNearChild], labi, iBest, rBest);
    }
}

void NearestIndex_tree_b(int iHere, const LABi &labi, int &iBest, INT64 &rBest)
{
    if (-1 == iHere)
    {
        return;
    }

    if (-1 == iBest)
    {
        iBest = iHere;
        rBest = diff(labi, table[iBest].labi);
    }

    INT64 rHere = diff(labi, table[iHere].labi);
    if (rHere < rBest)
    {
        iBest = iHere;
        rBest = rHere;
    }

    INT64 d = (INT64)(labi.b - table[iHere].labi.b);
    int iNearChild = (d < 0) ? 0 : 1;
    NearestIndex_tree_L(table[iHere].child[iNearChild], labi, iBest, rBest);

    INT64 rAxis = d * d;
    if (rAxis < rBest)
    {
        NearestIndex_tree_L(table[iHere].child[1 - iNearChild], labi, iBest, rBest);
    }
}

// Exhaustive validation: compare K-d tree results against brute-force
// for all 16.7M RGB values.
//
void ValidateTree(int iRoot, bool fDisable)
{
    const char *label = fDisable ? "16-color" : "256-color";
    int mismatches = 0;
    int tested = 0;

    RGB rgb;
    for (rgb.r = 0; rgb.r < 256; rgb.r++)
    {
        for (rgb.g = 0; rgb.g < 256; rgb.g++)
        {
            for (rgb.b = 0; rgb.b < 256; rgb.b++)
            {
                LAB lab;
                LABi labi;
                rgb2lab(&rgb, &lab);
                lab2labi(&lab, &labi);

                int iBrute = NearestIndex_brute(labi, fDisable);
                INT64 rBrute = diff(labi, table[iBrute].labi);

                int iTree = -1;
                INT64 rTree;
                NearestIndex_tree_L(iRoot, labi, iTree, rTree);

                if (iBrute != iTree && rBrute != rTree)
                {
                    mismatches++;
                    if (mismatches <= 10)
                    {
                        printf("  MISMATCH %s: RGB(%d,%d,%d) brute=%d(dist=%lld) tree=%d(dist=%lld)\n",
                            label, rgb.r, rgb.g, rgb.b,
                            iBrute, rBrute, iTree, rTree);
                    }
                }
                tested++;
            }
        }
        if (rgb.r % 32 == 0)
        {
            printf("  %s validation: %d/256 R values done...\n", label, rgb.r);
        }
    }
    printf("  %s validation complete: %d tested, %d mismatches\n",
        label, tested, mismatches);
}

// Cross-validate CIE76 (K-d tree) against CIEDE2000 (brute-force).
// For each RGB value, find the CIE76 nearest and CIEDE2000 nearest,
// and report cases where they disagree.
//
void CrossValidate_CIE76_vs_CIEDE2000()
{
    int disagree = 0;
    int tested = 0;
    double max_de2000_error = 0;

    RGB rgb;
    for (rgb.r = 0; rgb.r < 256; rgb.r++)
    {
        for (rgb.g = 0; rgb.g < 256; rgb.g++)
        {
            for (rgb.b = 0; rgb.b < 256; rgb.b++)
            {
                LAB lab;
                LABi labi;
                rgb2lab(&rgb, &lab);
                lab2labi(&lab, &labi);

                // CIE76 nearest (brute-force over 240 entries)
                //
                int iCIE76 = NearestIndex_brute(labi, false);

                // CIEDE2000 nearest (brute-force over 240 entries)
                //
                int iDE2000 = -1;
                double rDE2000 = 0;
                for (int i = 0; i < (int)NUM_ENTRIES; i++)
                {
                    if (table[i].fDisable) continue;
                    double d = ciede2000(lab, table[i].lab);
                    if (iDE2000 == -1 || d < rDE2000)
                    {
                        rDE2000 = d;
                        iDE2000 = i;
                    }
                }

                if (iCIE76 != iDE2000)
                {
                    // Compute the CIEDE2000 distance of the CIE76 choice
                    //
                    double de_cie76_choice = ciede2000(lab, table[iCIE76].lab);
                    double error = de_cie76_choice - rDE2000;
                    if (error > max_de2000_error)
                    {
                        max_de2000_error = error;
                    }
                    disagree++;
                }
                tested++;
            }
        }
        if (rgb.r % 32 == 0)
        {
            printf("  CIE76 vs CIEDE2000: %d/256 R values done...\n", rgb.r);
        }
    }
    printf("  CIE76 vs CIEDE2000: %d tested, %d disagree (%.2f%%)\n",
        tested, disagree, 100.0 * disagree / tested);
    printf("  Max CIEDE2000 error when using CIE76 choice: %.4f\n", max_de2000_error);
}

// Output the palette table in the format used by stringutil.cpp
//
void DumpTable(int iRoot16, int iRoot256)
{
    printf("typedef struct\n");
    printf("{\n");
    printf("    int r;\n");
    printf("    int g;\n");
    printf("    int b;\n");
    printf("} RGB;\n");
    printf("\n");
    printf("typedef struct\n");
    printf("{\n");
    printf("    int L;    // L* scaled by 100\n");
    printf("    int a;    // a* scaled by 100\n");
    printf("    int b;    // b* scaled by 100\n");
    printf("} LABi;\n");
    printf("\n");
    printf("typedef struct\n");
    printf("{\n");
    printf("    RGB   rgb;\n");
    printf("    LABi  labi;\n");
    printf("    int   child[2];\n");
    printf("    int   color8;\n");
    printf("    int   color16;\n");
    printf("} PALETTE_ENTRY;\n");
    printf("\n");
    printf("#define PALETTE16_ROOT %d\n", iRoot16);
    printf("#define PALETTE256_ROOT %d\n", iRoot256);
    printf("#define PALETTE_SIZE (sizeof(palette)/sizeof(palette[0]))\n");
    printf("PALETTE_ENTRY palette[] =\n");
    printf("{\n");
    for (int i = 0; i < (int)NUM_ENTRIES; i++)
    {
        printf("    { { %3d, %3d, %3d }, { %5d, %6d, %6d }, { %3d, %3d }, %2d, %2d},\n",
            table[i].rgb.r,
            table[i].rgb.g,
            table[i].rgb.b,
            table[i].labi.L,
            table[i].labi.a,
            table[i].labi.b,
            table[i].child[0],
            table[i].child[1],
            table[i].color8,
            table[i].color16
        );
    }
    printf("};\n");
}

// Dump the palette Lab values for inspection
//
void DumpLabValues()
{
    printf("XTERM Palette CIELAB Values:\n");
    printf("%-5s  %-15s  %-30s\n", "Index", "RGB", "CIELAB (L*, a*, b*)");
    printf("%-5s  %-15s  %-30s\n", "-----", "---", "-------------------");
    for (int i = 0; i < (int)NUM_ENTRIES; i++)
    {
        printf("%3d    (%3d,%3d,%3d)    L*=%7.3f  a*=%8.3f  b*=%8.3f\n",
            i,
            table[i].rgb.r, table[i].rgb.g, table[i].rgb.b,
            table[i].lab.L, table[i].lab.a, table[i].lab.b);
    }
}

void usage()
{
    printf("Usage: kdtree_lab [options]\n");
    printf("  -t    Output palette table (default)\n");
    printf("  -l    Dump CIELAB values for all palette entries\n");
    printf("  -v    Validate K-d tree against brute-force (16.7M tests, ~minutes)\n");
    printf("  -x    Cross-validate CIE76 vs CIEDE2000 (16.7M tests, ~hours)\n");
    printf("  -a    All of the above\n");
}

int main(int argc, char *argv[])
{
    bool fTable = false;
    bool fLabDump = false;
    bool fValidate = false;
    bool fCrossValidate = false;

    if (argc < 2)
    {
        fTable = true;
    }
    else
    {
        for (int i = 1; i < argc; i++)
        {
            if (strcmp(argv[i], "-t") == 0) fTable = true;
            else if (strcmp(argv[i], "-l") == 0) fLabDump = true;
            else if (strcmp(argv[i], "-v") == 0) fValidate = true;
            else if (strcmp(argv[i], "-x") == 0) fCrossValidate = true;
            else if (strcmp(argv[i], "-a") == 0)
            {
                fTable = true;
                fLabDump = true;
                fValidate = true;
                fCrossValidate = true;
            }
            else
            {
                usage();
                return 1;
            }
        }
    }

    // Step 1: Convert all RGB entries to CIELAB
    //
    for (int i = 0; i < (int)NUM_ENTRIES; i++)
    {
        rgb2lab(&table[i].rgb, &table[i].lab);
        lab2labi(&table[i].lab, &table[i].labi);
    }

    // Step 2: Build 16-color K-d tree (entries with fDisable=true, indices 0-15)
    //
    int npts = 0;
    int pts[NUM_ENTRIES];
    for (int i = 0; i < (int)NUM_ENTRIES; i++)
    {
        if (table[i].fDisable)
        {
            pts[npts++] = i;
        }
    }
    int kdroot16 = kdtree(npts, pts, 0);

    // Step 3: Build 256-color K-d tree (entries with fDisable=false, indices 16-255)
    //
    npts = 0;
    for (int i = 0; i < (int)NUM_ENTRIES; i++)
    {
        if (!table[i].fDisable)
        {
            pts[npts++] = i;
        }
    }
    int kdroot256 = kdtree(npts, pts, 0);

    // Step 4: Compute color16 mapping (each entry -> nearest 16-color)
    //
    for (int i = 0; i < (int)NUM_ENTRIES; i++)
    {
        INT64 d;
        int j = -1;
        NearestIndex_tree_L(kdroot16, table[i].labi, j, d);
        table[i].color16 = j;
    }

    // Step 5: Compute color8 mapping (each entry -> nearest 8-color, brute force)
    //
    for (int i = 0; i < (int)NUM_ENTRIES; i++)
    {
        int iNearest = 0;
        INT64 rNearest = diff(table[i].labi, table[0].labi);

        for (int j = 1; j < 8; j++)
        {
            INT64 r = diff(table[i].labi, table[j].labi);
            if (r < rNearest)
            {
                rNearest = r;
                iNearest = j;
            }
        }
        table[i].color8 = iNearest;
    }

    // Output requested results
    //
    if (fLabDump)
    {
        DumpLabValues();
        printf("\n");
    }

    if (fValidate)
    {
        printf("Validating 16-color K-d tree...\n");
        ValidateTree(kdroot16, true);
        printf("\n");
        printf("Validating 256-color K-d tree...\n");
        ValidateTree(kdroot256, false);
        printf("\n");
    }

    if (fCrossValidate)
    {
        printf("Cross-validating CIE76 vs CIEDE2000...\n");
        CrossValidate_CIE76_vs_CIEDE2000();
        printf("\n");
    }

    if (fTable)
    {
        DumpTable(kdroot16, kdroot256);
    }

    return 0;
}
