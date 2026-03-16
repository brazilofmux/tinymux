
/* In C++ mode, the OTT arrays are string_desc (from utf8tables.h) but these
 * functions return co_string_desc*.  The types are ABI-identical, so a cast
 * is safe.  In C mode, both sides are co_string_desc and no cast is needed.
 */
#ifdef __cplusplus
#define CO_OTT_CAST(x) reinterpret_cast<const co_string_desc *>(x)
#else
#define CO_OTT_CAST(x) (x)
#endif

/*
 * co_dfa_toupper — Run the toupper DFA on a single code point.
 *
 * p points to the first byte of a UTF-8 code point.
 * On return, *bXor is set:
 *   - returns NULL:  identity (no change)
 *   - bXor == 1:     XOR each byte of input with result->p[]
 *   - bXor == 0:     literal replacement (may differ in byte count)
 */
static inline const co_string_desc *co_dfa_toupper(const unsigned char *p, int *bXor)
{
    unsigned short iState = TR_TOUPPER_START_STATE;
    do
    {
        unsigned char ch = *p++;
        unsigned char iColumn = tr_toupper_itt[ch];
        unsigned short iOffset = tr_toupper_sot[iState];
        for (;;)
        {
            int y = tr_toupper_sbt[iOffset];
            if (y < 128)
            {
                /* RUN phrase. */
                if (iColumn < y)
                {
                    iState = tr_toupper_sbt[iOffset+1];
                    break;
                }
                else
                {
                    iColumn = (unsigned char)(iColumn - y);
                    iOffset += 2;
                }
            }
            else
            {
                /* COPY phrase. */
                y = 256 - y;
                if (iColumn < y)
                {
                    iState = tr_toupper_sbt[iOffset+iColumn+1];
                    break;
                }
                else
                {
                    iColumn = (unsigned char)(iColumn - y);
                    iOffset = (unsigned short)(iOffset + y + 1);
                }
            }
        }
    } while (iState < TR_TOUPPER_ACCEPTING_STATES_START);

    if (TR_TOUPPER_DEFAULT == iState - TR_TOUPPER_ACCEPTING_STATES_START)
    {
        *bXor = 0;
        return (const co_string_desc *)0;
    }
    else
    {
        *bXor = (TR_TOUPPER_XOR_START <= iState - TR_TOUPPER_ACCEPTING_STATES_START);
        return CO_OTT_CAST(tr_toupper_ott + iState - TR_TOUPPER_ACCEPTING_STATES_START - 1);
    }
}

static inline const co_string_desc *co_dfa_tolower(const unsigned char *p, int *bXor)
{
    unsigned char iState = TR_TOLOWER_START_STATE;
    do
    {
        unsigned char ch = *p++;
        unsigned char iColumn = tr_tolower_itt[ch];
        unsigned short iOffset = tr_tolower_sot[iState];
        for (;;)
        {
            int y = tr_tolower_sbt[iOffset];
            if (y < 128)
            {
                if (iColumn < y)
                {
                    iState = tr_tolower_sbt[iOffset+1];
                    break;
                }
                else
                {
                    iColumn = (unsigned char)(iColumn - y);
                    iOffset += 2;
                }
            }
            else
            {
                y = 256 - y;
                if (iColumn < y)
                {
                    iState = tr_tolower_sbt[iOffset+iColumn+1];
                    break;
                }
                else
                {
                    iColumn = (unsigned char)(iColumn - y);
                    iOffset = (unsigned short)(iOffset + y + 1);
                }
            }
        }
    } while (iState < TR_TOLOWER_ACCEPTING_STATES_START);

    if (TR_TOLOWER_DEFAULT == iState - TR_TOLOWER_ACCEPTING_STATES_START)
    {
        *bXor = 0;
        return (const co_string_desc *)0;
    }
    else
    {
        *bXor = (TR_TOLOWER_XOR_START <= iState - TR_TOLOWER_ACCEPTING_STATES_START);
        return CO_OTT_CAST(tr_tolower_ott + iState - TR_TOLOWER_ACCEPTING_STATES_START - 1);
    }
}

static inline const co_string_desc *co_dfa_totitle(const unsigned char *p, int *bXor)
{
    unsigned short iState = TR_TOTITLE_START_STATE;
    do
    {
        unsigned char ch = *p++;
        unsigned char iColumn = tr_totitle_itt[ch];
        unsigned short iOffset = tr_totitle_sot[iState];
        for (;;)
        {
            int y = tr_totitle_sbt[iOffset];
            if (y < 128)
            {
                if (iColumn < y)
                {
                    iState = tr_totitle_sbt[iOffset+1];
                    break;
                }
                else
                {
                    iColumn = (unsigned char)(iColumn - y);
                    iOffset += 2;
                }
            }
            else
            {
                y = 256 - y;
                if (iColumn < y)
                {
                    iState = tr_totitle_sbt[iOffset+iColumn+1];
                    break;
                }
                else
                {
                    iColumn = (unsigned char)(iColumn - y);
                    iOffset = (unsigned short)(iOffset + y + 1);
                }
            }
        }
    } while (iState < TR_TOTITLE_ACCEPTING_STATES_START);

    if (TR_TOTITLE_DEFAULT == iState - TR_TOTITLE_ACCEPTING_STATES_START)
    {
        *bXor = 0;
        return (const co_string_desc *)0;
    }
    else
    {
        *bXor = (TR_TOTITLE_XOR_START <= iState - TR_TOTITLE_ACCEPTING_STATES_START);
        return CO_OTT_CAST(tr_totitle_ott + iState - TR_TOTITLE_ACCEPTING_STATES_START - 1);
    }
}

static inline const co_string_desc *co_dfa_foldmatch(const unsigned char *p, int *bXor)
{
    int iState = TR_FOLDMATCH_START_STATE;
    do
    {
        unsigned char ch = *p++;
        unsigned char iColumn = tr_foldmatch_itt[ch];
        unsigned short iOffset = tr_foldmatch_sot[iState];
        for (;;)
        {
            int y = tr_foldmatch_sbt[iOffset];
            if (y < 128)
            {
                if (iColumn < y)
                {
                    iState = tr_foldmatch_sbt[iOffset+1];
                    break;
                }
                else
                {
                    iColumn = (unsigned char)(iColumn - y);
                    iOffset += 2;
                }
            }
            else
            {
                y = 256 - y;
                if (iColumn < y)
                {
                    iState = tr_foldmatch_sbt[iOffset+iColumn+1];
                    break;
                }
                else
                {
                    iColumn = (unsigned char)(iColumn - y);
                    iOffset = (unsigned short)(iOffset + y + 1);
                }
            }
        }
    } while (iState < TR_FOLDMATCH_ACCEPTING_STATES_START);

    if (TR_FOLDMATCH_DEFAULT == iState - TR_FOLDMATCH_ACCEPTING_STATES_START)
    {
        *bXor = 0;
        return (const co_string_desc *)0;
    }
    else
    {
        *bXor = (TR_FOLDMATCH_XOR_START <= iState - TR_FOLDMATCH_ACCEPTING_STATES_START);
        return CO_OTT_CAST(tr_foldmatch_ott + iState - TR_FOLDMATCH_ACCEPTING_STATES_START - 1);
    }
}

#endif /* UNICODE_TABLES_C_H */
