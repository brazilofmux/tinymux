/*! \file unsplit.cpp
 * \brief Strip system include files and re-combining lines.
 *
 */

#include "copyright.h"

#include <stdio.h>

static unsigned char itt[256] =
{
//  0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
//
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  0,  0,  2,  0,  0,  // 0
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 1
    3,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  6,  0,  6,  6,  4,  // 2
    6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  5,  0,  0,  0,  0,  0,  // 3
    0,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  // 4
    6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  0,  7,  0,  0,  6,  // 5
    0,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  // 6
    6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  0,  0,  0,  0,  0,  // 7

    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 8
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 9
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // A
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // B
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // C
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // D
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // E
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0   // F
};

const unsigned char stt[13][8] =
{
//     0   1   2   3   4   5   6   7 
//   Any  LF  CR  SP   /   :  nm   \   classes
//
    { 13,  1, 11,  5,  0,  0,  0,  9 }, //  0 Have name --> emit ch
    { 13, 12, 11,  5,  8, 13,  0,  9 }, //  1 Have LF --> emit LF
    { 13,  1, 11,  5,  0,  0,  0,  9 }, //  2 Have SP + name --> emit SP ch
    { 13, 12, 12,  5,  8, 13,  0,  9 }, //  3 Have CRLF --> emit CR LF
    { 13,  4,  4,  4,  8, 13,  0,  9 }, //  4 Start
    { 13,  1, 11,  5,  6,  0,  2,  7 }, //  5 Have SP
    { 13,  1, 11,  5,  6,  0,  6,  9 }, //  6 Have SP '/' (name)
    { 13,  7,  7,  7,  6, 13,  2,  2 }, //  7 Have SP '\'
    { 13,  1, 11,  4,  8, 13,  8, 10 }, //  8 Have '/'
    { 13,  1, 11,  5,  8, 13,  0,  0 }, //  9 Have '\'
    { 13,  1, 11,  8,  8, 13,  8,  8 }, // 10 Have '/' '\'
    { 13,  3, 11, 13, 13, 13, 13, 13 }, // 11 Have CR
    { 13, 12, 12,  5,  8, 13,  0,  9 }, // 12 Have (CR) LF LF
};

int main(int argc, char *argv[])
{
    int ch;
    unsigned char iState = 4;
    while ((ch = getchar()) != EOF)
    {
        iState = stt[iState][itt[(unsigned char)ch]];
        if (iState <= 1)
        {
            putchar(ch);
        }
        else if (iState <= 3)
        {
            static int chs[2] = { ' ', '\r' };
            putchar(chs[iState-2]);
            putchar(ch);
        }
        else if (13 == iState)
        {
            return 1;
        }
    }
    return 0;
}
