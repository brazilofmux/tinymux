/*! \file dbutil.cpp
 * \brief Flatfile serialization primitives (libmux).
 *
 * Pure FILE*-based helpers for reading and writing integers and
 * escaped strings.  These have no engine state dependencies and
 * are used by both the driver (restart db) and the engine (db_rw).
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "core.h"
#include "dbutil.h"

// putref — write an integer on its own line.
//
void putref(FILE *f, int ref)
{
    UTF8 buf[I32BUF_SIZE+1];
    size_t n = mux_ltoa(ref, buf);
    buf[n] = '\n';
    fwrite(buf, sizeof(char), n+1, f);
}

// getref — read an integer from the next line.
//
int getref(FILE *f)
{
    static UTF8 buf[SBUF_SIZE];
    if (nullptr != fgets(reinterpret_cast<char *>(buf), sizeof(buf), f))
    {
        return mux_atol(buf);
    }
    else
    {
        return 0;
    }
}

// String escape/de-escape tables for the flatfile format.
//
// Encode table (for putstring): maps bytes to escape codes.
//
// Code 0 - Any byte (pass through).
// Code 1 - NUL  (0x00) — terminator.
// Code 2 - '"'  (0x22) — needs escaping.
// Code 3 - '\\' (0x5C) — needs escaping.
// Code 4 - ESC  (0x1B) — needs escaping.
// Code 5 - LF   (0x0A) — needs escaping.
// Code 6 - CR   (0x0D) — needs escaping.
// Code 7 - TAB  (0x09) — needs escaping.
//
static const unsigned char encode_table[256] =
{
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
//
    1, 0, 0, 0, 0, 0, 0, 0, 0, 7, 5, 0, 0, 6, 0, 0, // 0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, // 1
    0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 2
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 3
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 4
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, // 5
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 6
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 7
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 8
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 9
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // A
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // B
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // C
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // D
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // E
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0  // F
};

// putstring — write an escaped, quoted string on its own line.
//
void putstring(FILE *f, const UTF8 *pRaw)
{
    static UTF8 aBuffer[2*LBUF_SIZE+4];
    UTF8 *pBuffer = aBuffer;

    // Always leave room for four characters. One at the beginning and
    // three on the end. '\\"\n' or '\""\n'
    //
    *pBuffer++ = '"';

    if (pRaw)
    {
        for (;;)
        {
            UTF8 ch;
            while ((ch = encode_table[static_cast<unsigned char>(*pRaw)]) == 0)
            {
                *pBuffer++ = *pRaw++;
            }

            if (1 == ch)
            {
                break;
            }

            pRaw++;

            switch (ch)
            {
            case 2: ch = '"'; break;
            case 3: ch = '\\'; break;
            case 4: ch = 'e'; break;
            case 5: ch = 'n'; break;
            case 6: ch = 'r'; break;
            case 7: ch = 't'; break;
            }

            *pBuffer++ = '\\';
            *pBuffer++ = ch;
        }
    }

    *pBuffer++ = '"';
    *pBuffer++ = '\n';

    fwrite(aBuffer, sizeof(UTF8), pBuffer - aBuffer, f);
}

// Decode table (for getstring_noalloc): maps bytes to escape codes.
//
// Code 0 - Any byte.
// Code 1 - NUL  (0x00)
// Code 2 - '"'  (0x22)
// Code 3 - '\\' (0x5C)
// Code 4 - 'e'  (0x65) or 'E' (0x45)
// Code 5 - 'n'  (0x6E) or 'N' (0x4E)
// Code 6 - 'r'  (0x72) or 'R' (0x52)
// Code 7 - 't'  (0x74) or 'T' (0x54)
//
static const unsigned char decode_table[256] =
{
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
//
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 1
    0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 2
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 3
    0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, // 4
    0, 0, 6, 0, 7, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, // 5
    0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, // 6
    0, 0, 6, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 7
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 8
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 9
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // A
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // B
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // C
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // D
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // E
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0  // F
};

#define STATE_START     0
#define STATE_HAVE_ESC  1

// Action 0 - Emit X.
// Action 1 - Get a Buffer.
// Action 2 - Emit X. Move to START state.
// Action 3 - Terminate parse.
// Action 4 - Move to ESC state.
// Action 5 - Emit ESC (0x1B). Move to START state.
// Action 6 - Emit LF  (0x0A). Move to START state.
// Action 7 - Emit CR  (0x0D). Move to START state.
// Action 8 - Emit TAB (0x09). Move to START state.
//
static const int action_table[2][8] =
{
//   Any  '\0' '"'  '\\' 'e'  'n'  'r'  't'
    { 0,   1,   3,   4,   0,   0,   0,   0}, // STATE_START
    { 2,   1,   2,   2,   5,   6,   7,   8}  // STATE_HAVE_ESC
};

// getstring_noalloc — read and de-escape a string from the next line(s).
//
void *getstring_noalloc(FILE *f, bool new_strings, size_t *pnBuffer)
{
    static UTF8 buf[2*LBUF_SIZE + 20];
    int c = fgetc(f);
    if (  new_strings
       && c == '"')
    {
        size_t nBufferLeft = sizeof(buf)-10;
        int iState = STATE_START;
        UTF8 *pOutput = buf;
        for (;;)
        {
            // Fetch up to and including the next LF.
            //
            UTF8 *pInput = pOutput + 6;
            if (fgets(reinterpret_cast<char *>(pInput), static_cast<int>(nBufferLeft), f) == nullptr)
            {
                // EOF or ERROR.
                //
                *pOutput = 0;
                if (pnBuffer)
                {
                    *pnBuffer = pOutput - buf;
                }
                return buf;
            }

            size_t nOutput = 0;

            // De-escape this data. removing the '\\' prefixes.
            // Terminate when you hit a '"'.
            //
            for (;;)
            {
                UTF8 ch = *pInput++;
                if (iState == STATE_START)
                {
                    if (decode_table[static_cast<unsigned char>(ch)] == 0)
                    {
                        // As long as decode_table[*p] is 0, just keep copying the characters.
                        //
                        UTF8 *p = pOutput;
                        do
                        {
                            *pOutput++ = ch;
                            ch = *pInput++;
                        } while (decode_table[static_cast<unsigned char>(ch)] == 0);
                        nOutput = pOutput - p;
                    }
                }
                int iAction = action_table[iState][decode_table[static_cast<unsigned char>(ch)]];
                if (iAction <= 2)
                {
                    if (1 == iAction)
                    {
                        // Get Buffer and remain in the current state.
                        //
                        break;
                    }
                    else
                    {
                        // 2 == iAction
                        // Emit X and move to START state.
                        //
                        *pOutput++ = ch;
                        nOutput++;
                        iState = STATE_START;
                    }
                }
                else if (3 == iAction)
                {
                    // Terminate parsing.
                    //
                    *pOutput = 0;
                    if (pnBuffer)
                    {
                        *pnBuffer = pOutput - buf;
                    }
                    return buf;
                }
                else if (4 == iAction)
                {
                    // Move to ESC state.
                    //
                    iState = STATE_HAVE_ESC;
                }
                else if (5 == iAction)
                {
                    *pOutput++ = ESC_CHAR;
                    nOutput++;
                    iState = STATE_START;
                }
                else if (6 == iAction)
                {
                    *pOutput++ = '\n';
                    nOutput++;
                    iState = STATE_START;
                }
                else if (7 == iAction)
                {
                    *pOutput++ = '\r';
                    nOutput++;
                    iState = STATE_START;
                }
                else
                {
                    // if (8 == iAction)
                    *pOutput++ = '\t';
                    nOutput++;
                    iState = STATE_START;
                }
            }

            nBufferLeft -= nOutput;

            // Do we have any more room?
            //
            if (nBufferLeft <= 0)
            {
                *pOutput = 0;
                if (pnBuffer)
                {
                    *pnBuffer = pOutput - buf;
                }
                return buf;
            }
        }
    }
    else
    {
        ungetc(c, f);

        UTF8 *p = buf;
        for (;;)
        {
            // Fetch up to and including the next LF.
            //
            if (fgets(reinterpret_cast<char *>(p), LBUF_SIZE, f) == nullptr)
            {
                // EOF or ERROR.
                //
                p[0] = '\0';
            }
            else
            {
                // How much data did we fetch?
                //
                size_t nLine = strlen(reinterpret_cast<char *>(p));
                if (nLine >= 2)
                {
                    if (p[nLine-2] == '\r')
                    {
                        // Line is continued on the next line.
                        //
                        p += nLine;
                        continue;
                    }

                    // Eat '\n'
                    //
                    p[nLine-1] = '\0';
                }
            }
            if (pnBuffer)
            {
                *pnBuffer = p - buf;
            }
            return buf;
        }
    }
}
