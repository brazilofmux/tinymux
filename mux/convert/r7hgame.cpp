#include "omega.h"
#include "p6hgame.h"
#include "r7hgame.h"

typedef struct _r7h_gameflaginfo
{
    int         mask;
    const char *pName;
} r7h_gameflaginfo;

r7h_gameflaginfo r7h_gameflagnames[] =
{
    { R7H_V_ZONE,         "V_ZONE"        },
    { R7H_V_LINK,         "V_LINK"        },
    { R7H_V_GDBM,         "V_GDBM"        },
    { R7H_V_ATRNAME,      "V_ATRNAME"     },
    { R7H_V_ATRKEY,       "V_ATRKEY"      },
    { R7H_V_PERNKEY,      "V_PERNEY"      },
    { R7H_V_PARENT,       "V_PARENT"      },
    { R7H_V_COMM,         "V_COMM"        },
    { R7H_V_ATRMONEY,     "V_ATRMONEY"    },
    { R7H_V_XFLAGS,       "V_XFLAGS"      },
};
#define R7H_NUM_GAMEFLAGNAMES (sizeof(r7h_gameflagnames)/sizeof(r7h_gameflagnames[0]))

R7H_GAME g_r7hgame;
R7H_LOCKEXP *r7hl_ParseKey(char *pKey);

// The first character of an attribute name must be either alphabetic,
// '_', '#', '.', or '~'. It's handled by the following table.
//
// Characters thereafter may be letters, numbers, and characters from
// the set {'?!`/-_.@#$^&~=+<>()}. Lower-case letters are turned into
// uppercase before being used, but lower-case letters are valid input.
//
bool r7h_AttrNameInitialSet[256] =
{
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
//
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 1
    0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0,  // 2
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 3
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 4
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1,  // 5
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 6
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 0,  // 7

    0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 0,  // 8
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 1,  // 9
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,  // A
    0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,  // B
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // C
    1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1,  // D
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // E
    1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1   // F
};

bool r7h_AttrNameSet[256] =
{
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
//
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 1
    0, 1, 0, 1, 1, 0, 1, 1, 1, 1, 0, 1, 0, 1, 1, 1,  // 2
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1,  // 3
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 4
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1,  // 5
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 6
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 0,  // 7

    0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 0,  // 8
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 1,  // 9
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,  // A
    0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,  // B
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // C
    1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1,  // D
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // E
    1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1   // F
};

char *r7h_ConvertAttributeName(const char *pName)
{
    char aBuffer[256];
    char *pBuffer = aBuffer;
    if (  '\0' != *pName
       && pBuffer < aBuffer + sizeof(aBuffer) - 1)
    {
        if (r7h_AttrNameInitialSet[(unsigned char) *pName])
        {
            *pBuffer++ = *pName++;
        }
        else
        {
            *pBuffer++ = 'X';
        }
    }
    while (  '\0' != *pName
          && pBuffer < aBuffer + sizeof(aBuffer) - 1)
    {
        if (r7h_AttrNameSet[(unsigned char) *pName])
        {
            *pBuffer++ = *pName++;
        }
        else
        {
            *pBuffer++ = 'X';
            pName++;
        }
    }
    *pBuffer = '\0';
    return StringClone(aBuffer);
}

void R7H_LOCKEXP::Write(FILE *fp)
{
    switch (m_op)
    {
    case R7H_LOCKEXP::le_is:
        fprintf(fp, "(=");
        m_le[0]->Write(fp);
        fprintf(fp, ")");
        break;

    case R7H_LOCKEXP::le_carry:
        fprintf(fp, "(+");
        m_le[0]->Write(fp);
        fprintf(fp, ")");
        break;

    case R7H_LOCKEXP::le_indirect:
        fprintf(fp, "(@");
        m_le[0]->Write(fp);
        fprintf(fp, ")");
        break;

    case R7H_LOCKEXP::le_owner:
        fprintf(fp, "($");
        m_le[0]->Write(fp);
        fprintf(fp, ")");
        break;

    case R7H_LOCKEXP::le_and:
        fprintf(fp, "(");
        m_le[0]->Write(fp);
        fprintf(fp, "&");
        m_le[1]->Write(fp);
        fprintf(fp, ")");
        break;

    case R7H_LOCKEXP::le_or:
        fprintf(fp, "(");
        m_le[0]->Write(fp);
        fprintf(fp, "|");
        m_le[1]->Write(fp);
        fprintf(fp, ")");
        break;

    case R7H_LOCKEXP::le_not:
        fprintf(fp, "(!");
        m_le[0]->Write(fp);
        fprintf(fp, ")");
        break;

    case R7H_LOCKEXP::le_attr:
        m_le[0]->Write(fp);
        fprintf(fp, ":");
        m_le[1]->Write(fp);

        // The code in 2.6 an earlier does not always emit a NL.  It's really
        // a beneign typo, but we reproduce it to make regression testing
        // easier.
        //
        if (m_le[0]->m_op != R7H_LOCKEXP::le_text)
        {
            fprintf(fp, "\n");
        }
        break;

    case R7H_LOCKEXP::le_eval:
        m_le[0]->Write(fp);
        fprintf(fp, "/");
        m_le[1]->Write(fp);
        fprintf(fp, "\n");
        break;

    case R7H_LOCKEXP::le_ref:
        fprintf(fp, "%d", m_dbRef);
        break;

    case R7H_LOCKEXP::le_text:
        fprintf(fp, "%s", m_p[0]);
        break;
    }
}

char *R7H_LOCKEXP::Write(char *p)
{
    switch (m_op)
    {
    case R7H_LOCKEXP::le_is:
        *p++ = '=';
        if (le_ref != m_le[0]->m_op)
        {
            *p++ = '(';
        }
        p = m_le[0]->Write(p);
        if (le_ref != m_le[0]->m_op)
        {
            *p++ = ')';
        }
        break;

    case R7H_LOCKEXP::le_carry:
        *p++ = '+';
        if (le_ref != m_le[0]->m_op)
        {
            *p++ = '(';
        }
        p = m_le[0]->Write(p);
        if (le_ref != m_le[0]->m_op)
        {
            *p++ = ')';
        }
        break;

    case R7H_LOCKEXP::le_indirect:
        *p++ = '@';
        if (le_ref != m_le[0]->m_op)
        {
            *p++ = '(';
        }
        p = m_le[0]->Write(p);
        if (le_ref != m_le[0]->m_op)
        {
            *p++ = ')';
        }
        break;

    case R7H_LOCKEXP::le_owner:
        *p++ = '$';
        if (le_ref != m_le[0]->m_op)
        {
            *p++ = '(';
        }
        p = m_le[0]->Write(p);
        if (le_ref != m_le[0]->m_op)
        {
            *p++ = ')';
        }
        break;

    case R7H_LOCKEXP::le_or:
        p = m_le[0]->Write(p);
        *p++ = '|';
        p = m_le[1]->Write(p);
        break;

    case R7H_LOCKEXP::le_not:
        *p++ = '!';
        if (  le_and == m_le[0]->m_op
           || le_or == m_le[0]->m_op)
        {
            *p++ = '(';
        }
        p = m_le[0]->Write(p);
        if (  le_and == m_le[0]->m_op
           || le_or == m_le[0]->m_op)
        {
            *p++ = ')';
        }
        break;

    case R7H_LOCKEXP::le_attr:
        p = m_le[0]->Write(p);
        *p++ = ':';
        p = m_le[1]->Write(p);
        break;

    case R7H_LOCKEXP::le_eval:
        p = m_le[0]->Write(p);
        *p++ = '/';
        p = m_le[1]->Write(p);
        break;

    case R7H_LOCKEXP::le_and:
        if (le_or == m_le[0]->m_op)
        {
            *p++ = '(';
        }
        p = m_le[0]->Write(p);
        if (le_or == m_le[0]->m_op)
        {
            *p++ = ')';
        }
        *p++ = '&';
        if (le_or == m_le[1]->m_op)
        {
            *p++ = '(';
        }
        p = m_le[1]->Write(p);
        if (le_or == m_le[1]->m_op)
        {
            *p++ = ')';
        }
        break;

    case R7H_LOCKEXP::le_ref:
        sprintf(p, "(#%d)", m_dbRef);
        p += strlen(p);
        break;

    case R7H_LOCKEXP::le_text:
        sprintf(p, "%s", m_p[0]);
        p += strlen(p);
        break;

    default:
        fprintf(stderr, "%d not recognized.\n", m_op);
        break;
    }
    return p;
}

bool R7H_LOCKEXP::ConvertFromP6H(P6H_LOCKEXP *p)
{
    switch (p->m_op)
    {
    case P6H_LOCKEXP::le_is:
        m_op = R7H_LOCKEXP::le_is;
        m_le[0] = new R7H_LOCKEXP;
        if (!m_le[0]->ConvertFromP6H(p->m_le[0]))
        {
            delete m_le[0];
            m_le[0] = NULL;
            return false;
        }
        break;

    case P6H_LOCKEXP::le_carry:
        m_op = R7H_LOCKEXP::le_carry;
        m_le[0] = new R7H_LOCKEXP;
        if (!m_le[0]->ConvertFromP6H(p->m_le[0]))
        {
            delete m_le[0];
            m_le[0] = NULL;
            return false;
        }
        break;

    case P6H_LOCKEXP::le_indirect:
        m_op = R7H_LOCKEXP::le_indirect;
        m_le[0] = new R7H_LOCKEXP;
        if (!m_le[0]->ConvertFromP6H(p->m_le[0]))
        {
            delete m_le[0];
            m_le[0] = NULL;
            return false;
        }
        break;

    case P6H_LOCKEXP::le_indirect2:
        return false;
        break;

    case P6H_LOCKEXP::le_owner:
        m_op = R7H_LOCKEXP::le_owner;
        m_le[0] = new R7H_LOCKEXP;
        if (!m_le[0]->ConvertFromP6H(p->m_le[0]))
        {
            delete m_le[0];
            m_le[0] = NULL;
            return false;
        }
        break;

    case P6H_LOCKEXP::le_or:
        m_op = R7H_LOCKEXP::le_or;
        m_le[0] = new R7H_LOCKEXP;
        m_le[1] = new R7H_LOCKEXP;
        if (  !m_le[0]->ConvertFromP6H(p->m_le[0])
           || !m_le[1]->ConvertFromP6H(p->m_le[1]))
        {
            delete m_le[0];
            delete m_le[1];
            m_le[0] = m_le[1] = NULL;
            return false;
        }
        break;

    case P6H_LOCKEXP::le_not:
        m_op = R7H_LOCKEXP::le_not;
        m_le[0] = new R7H_LOCKEXP;
        if (!m_le[0]->ConvertFromP6H(p->m_le[0]))
        {
            delete m_le[0];
            m_le[0] = NULL;
            return false;
        }
        break;

    case P6H_LOCKEXP::le_attr:
        m_op = R7H_LOCKEXP::le_attr;
        m_le[0] = new R7H_LOCKEXP;
        m_le[1] = new R7H_LOCKEXP;
        if (  !m_le[0]->ConvertFromP6H(p->m_le[0])
           || !m_le[1]->ConvertFromP6H(p->m_le[1]))
        {
            delete m_le[0];
            delete m_le[1];
            m_le[0] = m_le[1] = NULL;
            return false;
        }
        break;

    case P6H_LOCKEXP::le_eval:
        m_op = R7H_LOCKEXP::le_eval;
        m_le[0] = new R7H_LOCKEXP;
        m_le[1] = new R7H_LOCKEXP;
        if (  !m_le[0]->ConvertFromP6H(p->m_le[0])
           || !m_le[1]->ConvertFromP6H(p->m_le[1]))
        {
            delete m_le[0];
            delete m_le[1];
            m_le[0] = m_le[1] = NULL;
            return false;
        }
        break;

    case P6H_LOCKEXP::le_and:
        m_op = R7H_LOCKEXP::le_and;
        m_le[0] = new R7H_LOCKEXP;
        m_le[1] = new R7H_LOCKEXP;
        if (  !m_le[0]->ConvertFromP6H(p->m_le[0])
           || !m_le[1]->ConvertFromP6H(p->m_le[1]))
        {
            delete m_le[0];
            delete m_le[1];
            m_le[0] = m_le[1] = NULL;
            return false;
        }
        break;

    case P6H_LOCKEXP::le_ref:
        m_op = R7H_LOCKEXP::le_ref;
        m_dbRef = p->m_dbRef;
        break;

    case P6H_LOCKEXP::le_text:
        m_op = R7H_LOCKEXP::le_text;
        m_p[0] = StringClone(p->m_p[0]);
        break;

    case P6H_LOCKEXP::le_class:
        return false;
        break;

    case P6H_LOCKEXP::le_true:
        m_op = R7H_LOCKEXP::le_text;
        m_p[0] = StringClone("1");
        break;

    case P6H_LOCKEXP::le_false:
        m_op = R7H_LOCKEXP::le_text;
        m_p[0] = StringClone("0");
        break;

    default:
        fprintf(stderr, "%d not recognized.\n", m_op);
        break;
    }
    return true;
}

void R7H_ATTRNAMEINFO::SetNumAndName(int iNum, char *pName)
{
    m_fNumAndName = true;
    m_iNum = iNum;
    free(m_pName);
    m_pName = pName;
}

void R7H_ATTRNAMEINFO::Write(FILE *fp)
{
    if (m_fNumAndName)
    {
        fprintf(fp, "+A%d\n%s\n", m_iNum, m_pName);
    }
}

void R7H_OBJECTINFO::SetName(char *pName)
{
    if (NULL != m_pName)
    {
        free(m_pName);
    }
    m_pName = pName;
}

const int r7h_locknums[] =
{
    R7H_A_LOCK,
    R7H_A_LENTER,
    R7H_A_LLEAVE,
    R7H_A_LPAGE,
    R7H_A_LUSE,
    R7H_A_LGIVE,
    R7H_A_LTPORT,
    R7H_A_LDROP,
    R7H_A_LRECEIVE,
    R7H_A_LLINK,
    R7H_A_LTELOUT,
    R7H_A_LCONTROL,
    R7H_A_LUSER,
    R7H_A_LPARENT,
    R7H_A_LMAIL,
    R7H_A_LSHARE,
    R7H_A_LZONEWIZ,
    R7H_A_LZONETO,
    R7H_A_LTWINK,
    R7H_A_LSPEECH,
    R7H_A_LDARK,
    R7H_A_LDROPTO,
    R7H_A_LOPEN,
    R7H_A_LCHOWN,
    R7H_A_LALTNAME,
    R7H_A_LGIVETO,
    R7H_A_LGETFROM,
};

void R7H_OBJECTINFO::SetAttrs(int nAttrs, vector<R7H_ATTRINFO *> *pvai)
{
    if (  (  NULL == pvai
          && 0 != nAttrs)
       || (  NULL != pvai
          && nAttrs != pvai->size()))
    {
        fprintf(stderr, "WARNING: attr count disagreement.\n");
    }

    m_fAttrCount = true;
    m_nAttrCount = nAttrs;
    if (NULL != m_pvai)
    {
        delete m_pvai;
    }
    m_pvai = pvai;

    if (NULL != m_pvai)
    {
        for (vector<R7H_ATTRINFO *>::iterator it = m_pvai->begin(); it != m_pvai->end(); ++it)
        {
            (*it)->m_fIsLock = false;
            for (int i = 0; i < sizeof(r7h_locknums)/sizeof(r7h_locknums[0]); i++)
            {
                if (r7h_locknums[i] == (*it)->m_iNum)
                {
                    (*it)->m_fIsLock = true;
                    (*it)->m_pKeyTree = r7hl_ParseKey((*it)->m_pValueEncoded);
                    if (NULL == (*it)->m_pKeyTree)
                    {
                       fprintf(stderr, "WARNING: Lock key '%s' is not valid.\n", (*it)->m_pValueEncoded);
                    }
                    break;
                }
            }
        }
    }
}

void R7H_ATTRINFO::SetNumOwnerFlagsAndValue(int iNum, int dbAttrOwner, int iAttrFlags, char *pValue)
{
    m_fNumAndValue = true;
    free(m_pAllocated);
    m_pAllocated = pValue;

    m_iNum    = iNum;
    m_pValueUnencoded  = pValue;
    m_pValueEncoded = NULL;
    m_iFlags  = iAttrFlags;
    m_dbOwner = dbAttrOwner;

    m_kState  = kEncode;
}

void R7H_ATTRINFO::SetNumAndValue(int iNum, char *pValue)
{
    m_fNumAndValue = true;
    free(m_pAllocated);
    m_pAllocated = pValue;

    m_iNum    = iNum;
    m_pValueUnencoded  = NULL;
    m_pValueEncoded = pValue;
    m_iFlags  = 0;
    m_dbOwner = R7H_NOTHING;

    m_kState  = kDecode;
}

void R7H_ATTRINFO::EncodeDecode(int dbObjOwner)
{
    if (kEncode == m_kState)
    {
        // If using the default owner and flags (almost all attributes will),
        // just store the string.
        //
        if (  (  m_dbOwner == dbObjOwner
              || R7H_NOTHING == m_dbOwner)
           && 0 == m_iFlags)
        {
            m_pValueEncoded = m_pValueUnencoded;
        }
        else
        {
            // Encode owner and flags into the attribute text.
            //
            if (R7H_NOTHING == m_dbOwner)
            {
                m_dbOwner = dbObjOwner;
            }

            char buffer[65536];
            sprintf(buffer, "%c%d:%d:", ATR_INFO_CHAR, m_dbOwner, m_iFlags);
            size_t n = strlen(buffer);
            sprintf(buffer + n, "%s", m_pValueUnencoded);

            free(m_pAllocated);
            m_pAllocated = StringClone(buffer);

            m_pValueEncoded = m_pAllocated;
            m_pValueUnencoded = m_pAllocated + n;
        }
        m_kState = kNone;
    }
    else if (kDecode == m_kState)
    {
        // See if the first char of the attribute is the special character
        //
        m_iFlags = 0;
        if (ATR_INFO_CHAR != *m_pValueEncoded)
        {
            m_dbOwner = dbObjOwner;
            m_pValueUnencoded = m_pValueEncoded;
        }

        // It has the special character, crack the attr apart.
        //
        char *cp = m_pValueEncoded + 1;

        // Get the attribute owner
        //
        bool neg = false;
        if (*cp == '-')
        {
            neg = true;
            cp++;
        }
        int tmp_owner = 0;
        unsigned int ch = *cp;
        while (isdigit(ch))
        {
            cp++;
            tmp_owner = 10*tmp_owner + (ch-'0');
            ch = *cp;
        }
        if (neg)
        {
            tmp_owner = -tmp_owner;
        }

        // If delimiter is not ':', just return attribute
        //
        if (*cp++ != ':')
        {
            m_dbOwner = dbObjOwner;
            m_pValueUnencoded = m_pValueEncoded;
            return;
        }

        // Get the attribute flags.
        //
        int tmp_flags = 0;
        ch = *cp;
        while (isdigit(ch))
        {
            cp++;
            tmp_flags = 10*tmp_flags + (ch-'0');
            ch = *cp;
        }

        // If delimiter is not ':', just return attribute.
        //
        if (*cp++ != ':')
        {
            m_dbOwner = dbObjOwner;
            m_pValueUnencoded = m_pValueEncoded;
            return;
        }

        // Get the attribute text.
        //
        if (tmp_owner < 0)
        {
            m_dbOwner = dbObjOwner;
        }
        else
        {
            m_dbOwner = tmp_owner;
        }
        m_iFlags = tmp_flags;
        m_pValueUnencoded = cp;

        m_kState = kNone;
    }
}

void R7H_GAME::AddNumAndName(int iNum, char *pName)
{
    R7H_ATTRNAMEINFO *pani = new R7H_ATTRNAMEINFO;
    pani->SetNumAndName(iNum, pName);
    m_vAttrNames.push_back(pani);
}

void R7H_GAME::AddObject(R7H_OBJECTINFO *poi)
{
    m_mObjects[poi->m_dbRef] = poi;
}

void R7H_GAME::ValidateFlags() const
{
    int flags = m_flags;

    int ver = (m_flags & R7H_V_MASK);
    fprintf(stderr, "INFO: Flatfile version is %d\n", ver);
    if (ver < 7 || 7 < ver)
    {
        fprintf(stderr, "WARNING: Expecting version to be 7.\n");
    }
    flags &= ~R7H_V_MASK;
    int tflags = flags;

    fprintf(stderr, "INFO: Flatfile flags are ");
    for (int i = 0; i < R7H_NUM_GAMEFLAGNAMES; i++)
    {
        if (r7h_gameflagnames[i].mask & tflags)
        {
            fprintf(stderr, "%s ", r7h_gameflagnames[i].pName);
            tflags &= ~r7h_gameflagnames[i].mask;
        }
    }
    fprintf(stderr, "\n");
    if (0 != tflags)
    {
        fprintf(stderr, "Unknown flags: 0x%x\n", tflags);
        exit(1);
    }

    // Validate mandatory flags are present.
    //
    if (  7 == ver
       && (flags & R7H_MANDFLAGS) != R7H_MANDFLAGS)
    {
        fprintf(stderr, "WARNING: Not all mandatory flags for v7 are present.\n");
    }

    // Validate that this is a flatfile and not a structure file.
    //
    if (  (flags & R7H_V_GDBM) != 0
       || (flags & R7H_V_ATRNAME) != 0
       || (flags & R7H_V_ATRMONEY) != 0)
    {
        fprintf(stderr, "WARNING: Expected a flatfile (with strings) instead of a structure file (with only object anchors).\n");
    }
}

void R7H_GAME::Pass2()
{
    for (map<int, R7H_OBJECTINFO *, lti>::iterator itObj = m_mObjects.begin(); itObj != m_mObjects.end(); ++itObj)
    {
        if (NULL != itObj->second->m_pvai)
        {
            for (vector<R7H_ATTRINFO *>::iterator itAttr = itObj->second->m_pvai->begin(); itAttr != itObj->second->m_pvai->end(); ++itAttr)
            {
                (*itAttr)->EncodeDecode(itObj->second->m_dbOwner);
            }
        }
    }
}

void R7H_GAME::ValidateObjects() const
{
    int dbRefMax = 0;
    for (map<int, R7H_OBJECTINFO *, lti>::const_iterator it = m_mObjects.begin(); it != m_mObjects.end(); ++it)
    {
        it->second->Validate();
        if (dbRefMax < it->first)
        {
            dbRefMax = it->first;
        }
    }

    if (!m_fSizeHint)
    {
        fprintf(stderr, "WARNING: +S phrase for next object was missing.\n");
    }
    else
    {
        if (m_nSizeHint < dbRefMax+1)
        {
            fprintf(stderr, "WARNING: +S phrase does not leave room for the dbrefs.\n");
        }
        else if (m_nSizeHint != dbRefMax+1)
        {
            fprintf(stderr, "WARNING: +S phrase does not agree with last object.\n");
        }
    }
}

void R7H_ATTRNAMEINFO::Validate(int ver) const
{
    if (m_fNumAndName)
    {
        if (m_iNum < A_USER_START)
        {
            fprintf(stderr, "WARNING: User attribute (%s) uses an attribute number (%d) which is below %d.\n", m_pName, m_iNum, A_USER_START);
        }
        char *p = strchr(m_pName, ':');
        if (NULL == p)
        {
            fprintf(stderr, "WARNING, User attribute (%s) does not contain a flag sub-field.\n", m_pName);
        }
        else
        {
            char *q = m_pName;
            while (q != p)
            {
                if (!isdigit(*q))
                {
                    fprintf(stderr, "WARNING, User attribute (%s) flag sub-field is not numeric.\n", m_pName);
                    break;
                }
                q++;
            }

            if (ver <= 2)
            {
                q = p + 1;
                bool fValid = true;
                if (!r7h_AttrNameInitialSet[(unsigned char)*q])
                {
                    fValid = false;
                }
                else if ('\0' != *q)
                {
                    q++;
                    while ('\0' != *q)
                    {
                        if (!r7h_AttrNameSet[(unsigned char)*q])
                        {
                            fValid = false;
                            break;
                        }
                        q++;
                    }
                }
                if (!fValid)
                {
                    fprintf(stderr, "WARNING, User attribute (%s) name is not valid.\n", m_pName);
                }
            }
        }
    }
    else
    {
        fprintf(stderr, "WARNING: Unexpected ATTRNAMEINFO -- internal error.\n");
    }
}

void R7H_GAME::ValidateAttrNames(int ver) const
{
    if (!m_fNextAttr)
    {
        fprintf(stderr, "WARNING: +N phrase for attribute count was missing.\n");
    }
    else
    {
        int n = 256;
        for (vector<R7H_ATTRNAMEINFO *>::const_iterator it = m_vAttrNames.begin(); it != m_vAttrNames.end(); ++it)
        {
            (*it)->Validate(ver);
            if ((*it)->m_fNumAndName)
            {
                int iNum = (*it)->m_iNum;
                if (n <= iNum)
                {
                    n = iNum + 1;
                }
            }
        }
        if (m_nNextAttr != n)
        {
            fprintf(stderr, "WARNING: +N phrase (%d) does not agree with the maximum attribute number (%d).\n", m_nNextAttr, n);
        }
    }
}

void R7H_GAME::Validate() const
{
    fprintf(stderr, "RhostMUSH\n");

    int ver = (m_flags & R7H_V_MASK);
    ValidateFlags();
    ValidateAttrNames(ver);
    ValidateObjects();
}

void R7H_OBJECTINFO::Write(FILE *fp, bool bWriteLock)
{
    fprintf(fp, "!%d\n", m_dbRef);
    if (NULL != m_pName)
    {
        fprintf(fp, "%s\n", m_pName);
    }
    if (m_fLocation)
    {
        fprintf(fp, "%d\n", m_dbLocation);
    }
    if (m_fZone)
    {
        fprintf(fp, "%d\n", m_dbZone);
    }
    if (m_fContents)
    {
        fprintf(fp, "%d\n", m_dbContents);
    }
    if (m_fExits)
    {
        fprintf(fp, "%d\n", m_dbExits);
    }
    if (m_fLink)
    {
        fprintf(fp, "%d\n", m_dbLink);
    }
    if (m_fNext)
    {
        fprintf(fp, "%d\n", m_dbNext);
    }
    if (bWriteLock)
    {
        if (NULL == m_ple)
        {
            fprintf(fp, "\n");
        }
        else
        {
            m_ple->Write(fp);
            fprintf(fp, "\n");
        }
    }
    if (m_fOwner)
    {
        fprintf(fp, "%d\n", m_dbOwner);
    }
    if (m_fParent)
    {
        fprintf(fp, "%d\n", m_dbParent);
    }
    if (m_fPennies)
    {
        fprintf(fp, "%d\n", m_iPennies);
    }
    if (m_fFlags1)
    {
        fprintf(fp, "%d\n", m_iFlags1);
    }
    if (m_fFlags2)
    {
        fprintf(fp, "%d\n", m_iFlags2);
    }
    if (m_fFlags3)
    {
        fprintf(fp, "%d\n", m_iFlags3);
    }
    if (m_fFlags4)
    {
        fprintf(fp, "%d\n", m_iFlags4);
    }
    if (m_fToggles1)
    {
        fprintf(fp, "%d\n", m_iToggles1);
    }
    if (m_fToggles2)
    {
        fprintf(fp, "%d\n", m_iToggles2);
    }
    if (m_fToggles3)
    {
        fprintf(fp, "%d\n", m_iToggles3);
    }
    if (m_fToggles4)
    {
        fprintf(fp, "%d\n", m_iToggles4);
    }
    if (m_fToggles5)
    {
        fprintf(fp, "%d\n", m_iToggles5);
    }
    if (m_fToggles6)
    {
        fprintf(fp, "%d\n", m_iToggles6);
    }
    if (m_fToggles7)
    {
        fprintf(fp, "%d\n", m_iToggles7);
    }
    if (m_fToggles8)
    {
        fprintf(fp, "%d\n", m_iToggles8);
    }
    if (  m_fZones
       && NULL != m_pvz)
    {
        for (vector<int>::iterator it = m_pvz->begin(); it != m_pvz->end(); ++it)
        {
            fprintf(fp, "%d\n", (*it));
        }
    }
    if (  m_fAttrCount
       && NULL != m_pvai)
    {
        for (vector<R7H_ATTRINFO *>::iterator it = m_pvai->begin(); it != m_pvai->end(); ++it)
        {
            (*it)->Write(fp);
        }
    }
    fprintf(fp, "<\n");
}

void R7H_ATTRINFO::Validate() const
{
    if (  m_fNumAndValue
       && m_fIsLock
       && NULL != m_pKeyTree)
    {
        char buffer[65536];
        char *p = m_pKeyTree->Write(buffer);
        *p = '\0';
        if (strcmp(m_pValueUnencoded, buffer) != 0)
        {
            fprintf(stderr, "WARNING: Re-generated lock key '%s' does not agree with parsed key '%s'.\n", buffer, m_pValueUnencoded);
        }
    }
}

void R7H_OBJECTINFO::Validate() const
{
    map<int, R7H_OBJECTINFO *, lti>::const_iterator itFound;
    if (  m_fLocation
       && -1 != m_dbLocation)
    {
        itFound = g_r7hgame.m_mObjects.find(m_dbLocation);
        if (itFound == g_r7hgame.m_mObjects.end())
        {
            fprintf(stderr, "WARNING: Location (#%d) of object #%d does not exist.\n", m_dbLocation, m_dbRef);
        }
    }
    if (  m_fContents
       && -1 != m_dbContents)
    {
        itFound = g_r7hgame.m_mObjects.find(m_dbContents);
        if (itFound == g_r7hgame.m_mObjects.end())
        {
            fprintf(stderr, "WARNING: Contents (#%d) of object #%d does not exist.\n", m_dbContents, m_dbRef);
        }
    }
    if (  m_fExits
       && -1 != m_dbExits)
    {
        itFound = g_r7hgame.m_mObjects.find(m_dbExits);
        if (itFound == g_r7hgame.m_mObjects.end())
        {
            fprintf(stderr, "WARNING: Exits (#%d) of object #%d does not exist.\n", m_dbExits, m_dbRef);
        }
    }
    if (  m_fNext
       && -1 != m_dbNext)
    {
        itFound = g_r7hgame.m_mObjects.find(m_dbNext);
        if (itFound == g_r7hgame.m_mObjects.end())
        {
            fprintf(stderr, "WARNING: Next (#%d) of object #%d does not exist.\n", m_dbNext, m_dbRef);
        }
    }
    if (  m_fParent
       && -1 != m_dbParent)
    {
        itFound = g_r7hgame.m_mObjects.find(m_dbParent);
        if (itFound == g_r7hgame.m_mObjects.end())
        {
            fprintf(stderr, "WARNING: Parent (#%d) of object #%d does not exist.\n", m_dbParent, m_dbRef);
        }
    }
    if (  m_fOwner
       && -1 != m_dbOwner)
    {
        itFound = g_r7hgame.m_mObjects.find(m_dbOwner);
        if (itFound == g_r7hgame.m_mObjects.end())
        {
            fprintf(stderr, "WARNING: Owner (#%d) of object #%d does not exist.\n", m_dbOwner, m_dbRef);
        }
    }
    if (  m_fZone
       && -1 != m_dbZone)
    {
        itFound = g_r7hgame.m_mObjects.find(m_dbZone);
        if (itFound == g_r7hgame.m_mObjects.end())
        {
            fprintf(stderr, "WARNING: Zone (#%d) of object #%d does not exist.\n", m_dbZone, m_dbRef);
        }
    }
    if (  m_fLink
       && -1 != m_dbLink)
    {
        itFound = g_r7hgame.m_mObjects.find(m_dbLink);
        if (itFound == g_r7hgame.m_mObjects.end())
        {
            fprintf(stderr, "WARNING: Link (#%d) of object #%d does not exist.\n", m_dbLink, m_dbRef);
        }
    }

    if (  m_fAttrCount
       && NULL != m_pvai)
    {
        for (vector<R7H_ATTRINFO *>::iterator it = m_pvai->begin(); it != m_pvai->end(); ++it)
        {
            (*it)->Validate();
        }
    }
}

void R7H_ATTRINFO::Write(FILE *fp) const
{
    if (m_fNumAndValue)
    {
        fprintf(fp, ">%d\n%s\n", m_iNum, m_pValueEncoded);
    }
}

void R7H_GAME::Write(FILE *fp)
{
    // TIMESTAMPS and escapes occured near the same time, but are not related.
    //
    fprintf(fp, "+V%d\n", m_flags);
    if (m_fSizeHint)
    {
        fprintf(fp, "+S%d\n", m_nSizeHint);
    }
    if (m_fNextAttr)
    {
        fprintf(fp, "+N%d\n", m_nNextAttr);
    }
    if (m_fRecordPlayers)
    {
        fprintf(fp, "-R%d\n", m_nRecordPlayers);
    }
    for (vector<R7H_ATTRNAMEINFO *>::iterator it = m_vAttrNames.begin(); it != m_vAttrNames.end(); ++it)
    {
        (*it)->Write(fp);
    }
    for (map<int, R7H_OBJECTINFO *, lti>::iterator it = m_mObjects.begin(); it != m_mObjects.end(); ++it)
    {
        it->second->Write(fp, (m_flags & R7H_V_ATRKEY) == 0);
    }

    fprintf(fp, "***END OF DUMP***\n");
}

static int p6h_convert_type[] =
{
    R7H_NOTYPE,        //  0
    R7H_TYPE_ROOM,     //  1
    R7H_TYPE_THING,    //  2
    R7H_NOTYPE,        //  3
    R7H_TYPE_EXIT,     //  4
    R7H_NOTYPE,        //  5
    R7H_NOTYPE,        //  6
    R7H_NOTYPE,        //  7
    R7H_TYPE_PLAYER,   //  8
    R7H_NOTYPE,        //  9
    R7H_NOTYPE,        // 10
    R7H_NOTYPE,        // 11
    R7H_NOTYPE,        // 12
    R7H_NOTYPE,        // 13
    R7H_NOTYPE,        // 14
    R7H_NOTYPE,        // 15
    R7H_TYPE_GARBAGE,  // 16
};

static NameMask p6h_convert_obj_flags1[] =
{
    { "TRANSPARENT",    R7H_SEETHRU    },
    { "WIZARD",         R7H_WIZARD     },
    { "LINK_OK",        R7H_LINK_OK    },
    { "DARK",           R7H_DARK       },
    { "JUMP_OK",        R7H_JUMP_OK    },
    { "STICKY",         R7H_STICKY     },
    { "DESTROY_OK",     R7H_DESTROY_OK },
    { "HAVEN",          R7H_HAVEN      },
    { "QUIET",          R7H_QUIET      },
    { "HALT",           R7H_HALT       },
    { "DEBUG",          R7H_TRACE      },
    { "GOING",          R7H_GOING      },
    { "MONITOR",        R7H_MONITOR    },
    { "MYOPIC",         R7H_MYOPIC     },
    { "PUPPET",         R7H_PUPPET     },
    { "CHOWN_OK",       R7H_CHOWN_OK   },
    { "ENTER_OK",       R7H_ENTER_OK   },
    { "VISUAL",         R7H_VISUAL     },
    { "OPAQUE",         R7H_OPAQUE     },
    { "VERBOSE",        R7H_VERBOSE    },
    { "NOSPOOF",        R7H_NOSPOOF    },
    { "SAFE",           R7H_SAFE       },
    { "AUDIBLE",        R7H_HEARTHRU   },
    { "TERSE",          R7H_TERSE      },
};

static NameMask p6h_convert_obj_flags2[] =
{
    { "ABODE",          R7H_ABODE      },
    { "FLOATING",       R7H_FLOATING   },
    { "UNFINDABLE",     R7H_UNFINDABLE },
    { "LIGHT",          R7H_LIGHT      },
    { "ANSI",           R7H_ANSI|R7H_ANSICOLOR },
    { "COLOR",          R7H_ANSI|R7H_ANSICOLOR },
    { "SUSPECT",        R7H_SUSPECT    },
    { "SLAVE",          R7H_SLAVE      },
};

static NameMask p6h_convert_obj_flags3[] =
{
    { "NO_COMMAND",     R7H_NOCOMMAND  },
};

static NameMask p6h_convert_obj_flags4[] =
{
};

static NameMask p6h_convert_obj_toggles1[] =
{
    { "Boot",          1 << R7H_POWER_BOOT          },
    { "Long_Fingers",  1 << R7H_POWER_LONG_FINGERS  },
    { "No_Quota",      1 << R7H_POWER_FREE_QUOTA    },
    { "Quotas",        1 << R7H_POWER_CHANGE_QUOTAS },
    { "See_All",       1 << R7H_POWER_EX_ALL        },
    { "See_Queue",     (1 << R7H_POWER_SEE_QUEUE) | (1 << R7H_POWER_SEE_QUEUE_ALL) },
};

static NameMask p6h_convert_obj_toggles2[] =
{
    { "Announce",  1 << R7H_POWER_FREE_WALL },
    { "Halt",     (1 << R7H_POWER_HALT_QUEUE) | (1 << R7H_POWER_HALT_QUEUE_ALL) },
    { "Search",    1 << R7H_POWER_SEARCH_ANY },
    { "Tport_Anything", 1 << R7H_POWER_TEL_ANYTHING },
    { "Tport_Anywhere", 1 << R7H_POWER_TEL_ANYWHERE },
    { "Unkillable",     1 << R7H_POWER_NOKILL  },
};

static NameMask p6h_convert_obj_toggles3[] =
{
    { "Hide", 1 << R7H_POWER_HIDEBIT },
};

static NameMask p6h_convert_obj_toggles4[] =
{
};

static NameMask p6h_convert_obj_toggles5[] =
{
};

static NameMask p6h_convert_obj_toggles6[] =
{
};

static NameMask p6h_convert_obj_toggles7[] =
{
};

static NameMask p6h_convert_obj_toggles8[] =
{
};

static struct
{
    const char *pName;
    int         iNum;
} r7h_known_attrs[] =
{
    { "AAHEAR",         R7H_A_AAHEAR      },
    { "ACLONE",         R7H_A_ACLONE      },
    { "ACONNECT",       R7H_A_ACONNECT    },
    { "ADESC",          -1                },  // rename ADESC to XADESC
    { "ADESCRIBE",      R7H_A_ADESC       },  // rename ADESCRIBE to ADESC
    { "ADFAIL",         -1                },  // rename ADFAIL to XADFAIL
    { "ADISCONNECT",    R7H_A_ADISCONNECT },
    { "ADROP",          R7H_A_ADROP       },
    { "AEFAIL",         R7H_A_AEFAIL      },
    { "AENTER",         R7H_A_AENTER      },
    { "AFAIL",          -1                }, // rename AFAIL to XAFAIL
    { "AFAILURE",       R7H_A_AFAIL       }, // rename AFAILURE to AFAIL
    { "AGFAIL",         -1                }, // rename AGFAIL to XAGFAIL
    { "AHEAR",          R7H_A_AHEAR       },
    { "AKILL",          -1                }, // rename AKILL to XAKILL
    { "ALEAVE",         R7H_A_ALEAVE      },
    { "ALFAIL",         R7H_A_ALFAIL      },
    { "ALIAS",          R7H_A_ALIAS       },
    { "ALLOWANCE",      -1 },
    { "AMHEAR",         R7H_A_AMHEAR      },
    { "AMOVE",          R7H_A_AMOVE       },
    { "APAY",           -1                }, // rename APAY to XAPAY
    { "APAYMENT",       R7H_A_APAY        }, // rename APAYMENT to APAY
    { "ARFAIL",         -1                },
    { "ASUCC",          -1                }, // rename ASUCC to XASUCC
    { "ASUCCESS",       R7H_A_ASUCC       }, // rename ASUCCESS to ASUCC
    { "ATFAIL",         -1                }, // rename ATFAIL to XATFAIL
    { "ATPORT",         R7H_A_ATPORT      },
    { "ATOFAIL",        -1                }, // rename ATOFAIL to XATOFAIL
    { "AUFAIL",         R7H_A_AUFAIL      },
    { "AUSE",           R7H_A_AUSE        },
    { "AWAY",           R7H_A_AWAY        },
    { "CHARGES",        R7H_A_CHARGES     },
    { "CMDCHECK",       -1                }, // rename CMDCHECK to XCMDCHECK
    { "COMMENT",        R7H_A_COMMENT     },
    { "CONFORMAT",      R7H_A_LCON_FMT    },
    { "CONNINFO",       -1                },
    { "COST",           R7H_A_COST        },
    { "CREATED",        -1                }, // rename CREATED to XCREATED
    { "DAILY",          -1                }, // rename DAILY to XDAILY
    { "DESC",           -1                }, // rename DESC to XDESC
    { "DESCRIBE",       R7H_A_DESC        }, // rename DESCRIBE to DESC
    { "DEFAULTLOCK",    -1                }, // rename DEFAULTLOCK to XDEFAULTLOCK
    { "DESTROYER",      -1                }, // rename DESTROYER to XDESTROYER
    { "DFAIL",          -1                }, // rename DFAIL to XDFAIL
    { "DROP",           R7H_A_DROP        },
    { "DROPLOCK",       -1                }, // rename DROPLOCK to XDROPLOCK
    { "EALIAS",         R7H_A_EALIAS      },
    { "EFAIL",          R7H_A_EFAIL       },
    { "ENTER",          R7H_A_ENTER       },
    { "ENTERLOCK",      -1                }, // rename ENTERLOCK to XENTERLOCK
    { "EXITFORMAT",     R7H_A_LEXIT_FMT   },
    { "EXITTO",         R7H_A_EXITTO      },
    { "FAIL",           -1                }, // rename FAIL to XFAIL
    { "FAILURE",        R7H_A_FAIL        }, // rename FAILURE to FAIL
    { "FILTER",         R7H_A_FILTER      },
    { "FORWARDLIST",    R7H_A_FORWARDLIST },
    { "GETFROMLOCK",    -1                }, // rename GETFROMLOCK to XGETFROMLOCK
    { "GFAIL",          -1                }, // rename GFAIL to XGFAIL
    { "GIVELOCK",       -1                }, // rename GIVELOCK to XGIVELOCK
    { "HTDESC",         -1                }, // rename HTDESC to XHTDESC
    { "IDESC",          -1                }, // rename IDESC to XIDESC
    { "IDESCRIBE",      R7H_A_IDESC       }, // rename IDESCRIBE to IDESC
    { "IDLE",           R7H_A_IDLE        },
    { "IDLETIMEOUT",    -1                }, // rename IDLETIMEOUT to XIDLETIMEOUT
    { "INFILTER",       R7H_A_INFILTER    },
    { "INPREFIX",       R7H_A_INPREFIX    },
    { "KILL",           -1                }, // rename KILL to XKILL
    { "LALIAS",         R7H_A_LALIAS      },
    { "LAST",           R7H_A_LAST        },
    { "LASTPAGE",       -1                }, // rename LASTPAGE to XLASTPAGE
    { "LASTSITE",       R7H_A_LASTSITE    },
    { "LASTIP",         R7H_A_LASTIP      },
    { "LEAVE",          R7H_A_LEAVE       },
    { "LEAVELOCK",      -1                }, // rename LEAVELOCK to XLEAVELOCK
    { "LFAIL",          R7H_A_LFAIL       },
    { "LINKLOCK",       -1                }, // rename LINKLOCK to XLINKLOCK
    { "LISTEN",         R7H_A_LISTEN      },
    { "LOGINDATA",      -1                }, // rename LOGINDATA to XLOGINDATA
    { "MAILCURF",       -1                }, // rename MAILCURF to XMAILCURF
    { "MAILFLAGS",      -1                }, // rename MAILFLAGS to XMAILFLAGS
    { "MAILFOLDERS",    -1                }, // rename MAILFOLDERS to XMAILFOLDERS
    { "MAILLOCK",       -1                }, // rename MAILLOCK to XMAILLOCK
    { "MAILMSG",        -1                }, // rename MAILMSG to XMAILMSG
    { "MAILSUB",        -1                }, // rename MAILSUB to XMAILSUB
    { "MAILSUCC",       -1                }, // rename MAILSUCC to XMAILSUCC
    { "MAILTO",         -1                }, // rename MAILTO to XMAILTO
    { "MFAIL",          -1                }, // rename MFAIL to XMFAIL
    { "MODIFIED",       -1                }, // rename MODIFIED to XMODIFIED
    { "MONIKER",        -1                }, // rename MONIKER to XMONIKER
    { "MOVE",           R7H_A_MOVE        },
    { "NAME",           -1                }, // rename NAME to XNAME
    { "NAMEFORMAT",     R7H_A_NAME_FMT    },
    { "ODESC",          -1                }, // rename ODESC to XODESC
    { "ODESCRIBE",      R7H_A_ODESC       }, // rename ODESCRIBE to ODESC
    { "ODFAIL",         -1                }, // rename ODFAIL to XODFAIL
    { "ODROP",          R7H_A_ODROP       },
    { "OEFAIL",         R7H_A_OEFAIL      },
    { "OENTER",         R7H_A_OENTER      },
    { "OFAIL",          -1                }, // rename OFAIL to XOFAIL
    { "OFAILURE",       R7H_A_OFAIL       }, // rename OFAILURE to OFAIL
    { "OGFAIL",         -1                }, // rename OGFAIL to XOGFAIL
    { "OKILL",          -1                }, // rename OKILL to XOKILL
    { "OLEAVE",         R7H_A_OLEAVE      },
    { "OLFAIL",         R7H_A_OLFAIL      },
    { "OMOVE",          R7H_A_OMOVE       },
    { "OPAY",           -1                }, // rename OPAY to XOPAY
    { "OPAYMENT",       R7H_A_OPAY        }, // rename OPAYMENT to OPAY
    { "OPENLOCK",       -1                }, // rename OPENLOCK to XOPENLOCK
    { "ORFAIL",         -1                }, // rename ORFAIL to XORFAIL
    { "OSUCC",          -1                }, // rename OSUCC to XSUCC
    { "OSUCCESS",       R7H_A_OSUCC       }, // rename OSUCCESS to OSUCC
    { "OTFAIL",         -1                }, // rename OTFAIL to XOTFAIL
    { "OTPORT",         R7H_A_OTPORT      },
    { "OTOFAIL",        -1                }, // rename OTOFAIL to XOTOFAIL
    { "OUFAIL",         R7H_A_OUFAIL      },
    { "OUSE",           R7H_A_OUSE        },
    { "OXENTER",        R7H_A_OXENTER     },
    { "OXLEAVE",        R7H_A_OXLEAVE     },
    { "OXTPORT",        R7H_A_OXTPORT     },
    { "PAGELOCK",       -1                }, // rename PAGELOCK to XPAGELOCK
    { "PARENTLOCK",     -1                }, // rename PARENTLOCK to XPARENTLOCK
    { "PAY",            -1                }, // rename PAY to XPAY
    { "PAYMENT",        R7H_A_PAY         }, // rename PAYMENT to PAY
    { "PREFIX",         R7H_A_PREFIX      },
    { "PROGCMD",        -1                }, // rename PROGCMD to XPROGCMD
    { "QUEUEMAX",       -1                }, // rename QUEUEMAX to XQUEUEMAX
    { "QUOTA",          -1                }, // rename QUOTA to XQUOTA
    { "RECEIVELOCK",    -1                },
    { "REJECT",         -1                }, // rename REJECT to XREJECT
    { "REASON",         -1                }, // rename REASON to XREASON
    { "RFAIL",          -1                }, // rename RFAIL to XRFAIL
    { "RQUOTA",         R7H_A_RQUOTA      },
    { "RUNOUT",         R7H_A_RUNOUT      },
    { "SAYSTRING",      -1                }, // rename SAYSTRING to XSAYSTRING
    { "SEMAPHORE",      R7H_A_SEMAPHORE   },
    { "SEX",            R7H_A_SEX         },
    { "SIGNATURE",      -1                }, // rename SIGNATURE to XSIGNATURE
    { "MAILSIGNATURE",  R7H_A_MAILSIG     }, // rename MAILSIGNATURE to SIGNATURE
    { "SPEECHMOD",      -1                }, // rename SPEECHMOD to XSPEECHMOD
    { "SPEECHLOCK",     -1                }, // rename SPEECHLOCK to XSPEECHLOCK
    { "STARTUP",        R7H_A_STARTUP     },
    { "SUCC",           R7H_A_SUCC        },
    { "TELOUTLOCK",     -1                }, // rename TELOUTLOCK to XTELOUTLOCK
    { "TFAIL",          -1                }, // rename TFAIL to XTFAIL
    { "TIMEOUT",        -1                }, // rename TIMEOUT to XTIMEOUT
    { "TPORT",          R7H_A_TPORT       },
    { "TPORTLOCK",      -1                }, // rename TPORTLOCK to XTPORTLOCK
    { "TOFAIL",         -1                }, // rename TOFAIL to XTOFAIL
    { "UFAIL",          R7H_A_UFAIL       },
    { "USE",            R7H_A_USE         },
    { "USELOCK",        -1                },
    { "USERLOCK",       -1                },
    { "VA",             R7H_A_VA          },
    { "VB",             R7H_A_VA+1        },
    { "VC",             R7H_A_VA+2        },
    { "VD",             R7H_A_VA+3        },
    { "VE",             R7H_A_VA+4        },
    { "VF",             R7H_A_VA+5        },
    { "VG",             R7H_A_VA+6        },
    { "VH",             R7H_A_VA+7        },
    { "VI",             R7H_A_VA+8        },
    { "VJ",             R7H_A_VA+9        },
    { "VK",             R7H_A_VA+10       },
    { "VL",             R7H_A_VA+11       },
    { "VM",             R7H_A_VA+12       },
    { "VN",             R7H_A_VA+13       },
    { "VO",             R7H_A_VA+14       },
    { "VP",             R7H_A_VA+15       },
    { "VQ",             R7H_A_VA+16       },
    { "VR",             R7H_A_VA+17       },
    { "VS",             R7H_A_VA+18       },
    { "VT",             R7H_A_VA+19       },
    { "VU",             R7H_A_VA+20       },
    { "VV",             R7H_A_VA+21       },
    { "VW",             R7H_A_VA+22       },
    { "VX",             R7H_A_VA+23       },
    { "VY",             R7H_A_VA+24       },
    { "VZ",             R7H_A_VA+25       },
    { "XYXXY",          R7H_A_PASS        },   // *Password
};

static struct
{
    const char *pName;
    int         iNum;
} p6h_locknames[] =
{
    { "Basic",       R7H_A_LOCK     },
    { "Enter",       R7H_A_LENTER   },
    { "Use",         R7H_A_LUSE     },
    { "Zone",        -1             },
    { "Page",        R7H_A_LPAGE    },
    { "Teleport",    R7H_A_LTPORT   },
    { "Speech",      R7H_A_LSPEECH  },
    { "Parent",      R7H_A_LPARENT  },
    { "Link",        R7H_A_LLINK    },
    { "Leave",       R7H_A_LLEAVE   },
    { "Drop",        R7H_A_LDROP    },
    { "Give",        R7H_A_LGIVE    },
    { "Receive",     R7H_A_LRECEIVE },
    { "Mail",        R7H_A_LMAIL    },
    { "Take",        R7H_A_LGETFROM },
    { "Open",        R7H_A_LOPEN    },
};

static NameMask p6h_attr_flags[] =
{
    { "private",        R7H_AF_PRIVATE  },
    { "no_clone",       R7H_AF_NOCLONE  },
    { "wizard",         R7H_AF_WIZARD   },
    { "visual",         R7H_AF_VISUAL   },
    { "mortal_dark",    R7H_AF_MDARK    },
    { "hidden",         R7H_AF_DARK     },
    { "locked",         R7H_AF_LOCK     },
    { "internal",       R7H_AF_INTERNAL },
};

static char *EncodeAttrValue(int iObjOwner, int iAttrOwner, int iAttrFlags, char *pValue)
{
    // If using the default owner and flags (almost all attributes will),
    // just store the string.
    //
    if (  (  iAttrOwner == iObjOwner
          || -1 == iAttrOwner)
       && 0 == iAttrFlags)
    {
        return pValue;
    }

    // Encode owner and flags into the attribute text.
    //
    if (-1 == iAttrOwner)
    {
        iAttrOwner = iObjOwner;
    }
    static char buffer[65536];
    sprintf(buffer, "%c%d:%d:%s", ATR_INFO_CHAR, iAttrOwner, iAttrFlags, pValue);
    return buffer;
}

void R7H_GAME::ConvertFromP6H()
{
    SetFlags(R7H_MANDFLAGS | 7);

    // Build internal attribute names.
    //
    map<const char *, int, ltstr> AttrNamesKnown;
    for (int i = 0; i < sizeof(r7h_known_attrs)/sizeof(r7h_known_attrs[0]); i++)
    {
        AttrNamesKnown[StringClone(r7h_known_attrs[i].pName)] = r7h_known_attrs[i].iNum;
    }

    // Build set of attribute names.
    //
    int iNextAttr = A_USER_START;
    map<const char *, int, ltstr> AttrNames;
    for (map<int, P6H_OBJECTINFO *, lti>::iterator itObj = g_p6hgame.m_mObjects.begin(); itObj != g_p6hgame.m_mObjects.end(); ++itObj)
    {
        if (NULL != itObj->second->m_pvai)
        {
            for (vector<P6H_ATTRINFO *>::iterator itAttr = itObj->second->m_pvai->begin(); itAttr != itObj->second->m_pvai->end(); ++itAttr)
            {
                if (NULL != (*itAttr)->m_pName)
                {
                    char *pAttrName = r7h_ConvertAttributeName((*itAttr)->m_pName);
                    map<const char *, int , ltstr>::iterator itFound = AttrNamesKnown.find(pAttrName);
                    if (itFound != AttrNamesKnown.end())
                    {
                        if (-1 == itFound->second)
                        {
                            // This is a known name, but it doesn't have the
                            // same meaning. Rename it.
                            //
                            char buffer[100];
                            sprintf(buffer, "X%s", pAttrName);
                            AttrNames[StringClone(buffer)] = iNextAttr;
                            AttrNamesKnown[StringClone(pAttrName)] = iNextAttr;
                            iNextAttr++;
                        }
                    }
                    else if (AttrNames.find(pAttrName) == AttrNames.end())
                    {
                        AttrNames[StringClone(pAttrName)] = iNextAttr++;
                    }
                }
            }
        }
    }

    // Add attribute names
    //
    for (map<const char *, int, ltstr>::iterator it = AttrNames.begin(); it != AttrNames.end(); ++it)
    {
        char buffer[256];
        sprintf(buffer, "%d:%s", 0, it->first);
        AddNumAndName(it->second, StringClone(buffer));
    }
    SetNextAttr(iNextAttr);

    int dbRefMax = 0;
    for (map<int, P6H_OBJECTINFO *, lti>::iterator it = g_p6hgame.m_mObjects.begin(); it != g_p6hgame.m_mObjects.end(); ++it)
    {
        if (  !it->second->m_fType
           || it->second->m_iType < 0
           || 16 < it->second->m_iType)
        {
            continue;
        }

        R7H_OBJECTINFO *poi = new R7H_OBJECTINFO;

        int iType = p6h_convert_type[it->second->m_iType];

        poi->SetRef(it->first);
        poi->SetName(StringClone(it->second->m_pName));
        if (it->second->m_fLocation)
        {
            int iLocation = it->second->m_dbLocation;
            if (  R7H_TYPE_EXIT == iType
               && -2 == iLocation)
            {
                poi->SetLocation(-1);
            }
            else
            {
                poi->SetLocation(iLocation);
            }
        }
        if (it->second->m_fContents)
        {
            poi->SetContents(it->second->m_dbContents);
        }
        if (it->second->m_fExits)
        {
            switch (iType)
            {
            case R7H_TYPE_PLAYER:
            case R7H_TYPE_THING:
                poi->SetExits(-1);
                poi->SetLink(it->second->m_dbExits);
                break;

            default:
                poi->SetExits(it->second->m_dbExits);
                poi->SetLink(-1);
                break;
            }
        }
        if (it->second->m_fNext)
        {
            poi->SetNext(it->second->m_dbNext);
        }
        if (it->second->m_fParent)
        {
            poi->SetParent(it->second->m_dbParent);
        }
        if (it->second->m_fOwner)
        {
            poi->SetOwner(it->second->m_dbOwner);
        }
        if (it->second->m_fZone)
        {
            poi->SetZone(it->second->m_dbZone);
        }
        if (it->second->m_fPennies)
        {
            poi->SetPennies(it->second->m_iPennies);
        }

        int flags1   = iType;
        int flags2   = 0;
        int flags3   = 0;
        int flags4   = 0;
        int toggles1 = 0;
        int toggles2 = 0;
        int toggles3 = 0;
        int toggles4 = 0;
        int toggles5 = 0;
        int toggles6 = 0;
        int toggles7 = 0;
        int toggles8 = 0;

        // Flagwords
        //
        char *pFlags = it->second->m_pFlags;
        if (NULL != pFlags)
        {
            // First flagword
            //
            for (int i = 0; i < sizeof(p6h_convert_obj_flags1)/sizeof(p6h_convert_obj_flags1[0]); i++)
            {
                if (NULL != strcasestr(pFlags, p6h_convert_obj_flags1[i].pName))
                {
                    flags1 |= p6h_convert_obj_flags1[i].mask;
                }
            }

            // Second flagword
            //
            for (int i = 0; i < sizeof(p6h_convert_obj_flags2)/sizeof(p6h_convert_obj_flags2[0]); i++)
            {
                if (NULL != strcasestr(pFlags, p6h_convert_obj_flags2[i].pName))
                {
                    flags2 |= p6h_convert_obj_flags2[i].mask;
                }
            }

            // Third flagword
            //
            for (int i = 0; i < sizeof(p6h_convert_obj_flags3)/sizeof(p6h_convert_obj_flags3[0]); i++)
            {
                if (NULL != strcasestr(pFlags, p6h_convert_obj_flags3[i].pName))
                {
                    flags3 |= p6h_convert_obj_flags3[i].mask;
                }
            }

            // Fourth flagword
            //
            for (int i = 0; i < sizeof(p6h_convert_obj_flags4)/sizeof(p6h_convert_obj_flags4[0]); i++)
            {
                if (NULL != strcasestr(pFlags, p6h_convert_obj_flags4[i].pName))
                {
                    flags4 |= p6h_convert_obj_flags4[i].mask;
                }
            }

            // KEEPALIVE flag is special.
            //
            if (NULL != strcasestr(pFlags, "KEEPALIVE"))
            {
                toggles2 |= R7H_TOG_KEEPALIVE;
            }
        }

        // Powers
        //
        char *pPowers = it->second->m_pPowers;
        if (NULL != pPowers)
        {
            // First toggleword
            //
            for (int i = 0; i < sizeof(p6h_convert_obj_toggles1)/sizeof(p6h_convert_obj_toggles1[0]); i++)
            {
                if (NULL != strcasestr(pPowers, p6h_convert_obj_toggles1[i].pName))
                {
                    toggles1 |= p6h_convert_obj_toggles1[i].mask;
                }
            }

            // Second toggleword
            //
            for (int i = 0; i < sizeof(p6h_convert_obj_toggles2)/sizeof(p6h_convert_obj_toggles2[0]); i++)
            {
                if (NULL != strcasestr(pPowers, p6h_convert_obj_toggles2[i].pName))
                {
                    toggles2 |= p6h_convert_obj_toggles2[i].mask;
                }
            }

            // Third toggleword
            //
            for (int i = 0; i < sizeof(p6h_convert_obj_toggles3)/sizeof(p6h_convert_obj_toggles3[0]); i++)
            {
                if (NULL != strcasestr(pPowers, p6h_convert_obj_toggles3[i].pName))
                {
                    toggles3 |= p6h_convert_obj_toggles3[i].mask;
                }
            }

            // Fourth toggleword
            //
            for (int i = 0; i < sizeof(p6h_convert_obj_toggles4)/sizeof(p6h_convert_obj_toggles4[0]); i++)
            {
                if (NULL != strcasestr(pPowers, p6h_convert_obj_toggles4[i].pName))
                {
                    toggles4 |= p6h_convert_obj_toggles4[i].mask;
                }
            }

            // Fifth toggleword
            //
            for (int i = 0; i < sizeof(p6h_convert_obj_toggles5)/sizeof(p6h_convert_obj_toggles5[0]); i++)
            {
                if (NULL != strcasestr(pPowers, p6h_convert_obj_toggles5[i].pName))
                {
                    toggles5 |= p6h_convert_obj_toggles5[i].mask;
                }
            }

            // Sixth toggleword
            //
            for (int i = 0; i < sizeof(p6h_convert_obj_toggles6)/sizeof(p6h_convert_obj_toggles6[0]); i++)
            {
                if (NULL != strcasestr(pPowers, p6h_convert_obj_toggles6[i].pName))
                {
                    toggles6 |= p6h_convert_obj_toggles6[i].mask;
                }
            }

            // Seventh toggleword
            //
            for (int i = 0; i < sizeof(p6h_convert_obj_toggles7)/sizeof(p6h_convert_obj_toggles7[0]); i++)
            {
                if (NULL != strcasestr(pPowers, p6h_convert_obj_toggles7[i].pName))
                {
                    toggles7 |= p6h_convert_obj_toggles7[i].mask;
                }
            }

            // Eighth toggleword
            //
            for (int i = 0; i < sizeof(p6h_convert_obj_toggles8)/sizeof(p6h_convert_obj_toggles8[0]); i++)
            {
                if (NULL != strcasestr(pPowers, p6h_convert_obj_toggles8[i].pName))
                {
                    toggles8 |= p6h_convert_obj_toggles8[i].mask;
                }
            }

            // Immortal, Builder, and Guest powers are special.
            //
            if (NULL != strcasestr(pPowers, "Immortal"))
            {
                flags1 |= R7H_IMMORTAL;
            }
            if (NULL != strcasestr(pPowers, "Builder"))
            {
                flags2 |= R7H_BUILDER;
            }
            if (NULL != strcasestr(pPowers, "Guest"))
            {
                flags2 |= R7H_GUEST_FLAG;
            }
        }
        poi->SetFlags1(flags1);
        poi->SetFlags2(flags2);
        poi->SetFlags3(flags3);
        poi->SetToggles1(toggles1);
        poi->SetToggles2(toggles2);
        poi->SetToggles3(toggles3);
        poi->SetToggles4(toggles4);
        poi->SetToggles5(toggles5);
        poi->SetToggles6(toggles6);
        poi->SetToggles7(toggles7);
        poi->SetToggles8(toggles8);

        if (it->second->m_fCreated)
        {
            time_t t = it->second->m_iCreated;
            char *pTime = ctime(&t);
            if (NULL != pTime)
            {
                char *p = strchr(pTime, '\n');
                if (NULL != p)
                {
                    size_t n = p - pTime;
                    pTime = StringCloneLen(pTime, n);

                    R7H_ATTRINFO *pai = new R7H_ATTRINFO;
                    pai->SetNumAndValue(R7H_A_CREATED_TIME, StringClone(pTime));

                    if (NULL == poi->m_pvai)
                    {
                        vector<R7H_ATTRINFO *> *pvai = new vector<R7H_ATTRINFO *>;
                        pvai->push_back(pai);
                        poi->SetAttrs(pvai->size(), pvai);
                    }
                    else
                    {
                        poi->m_pvai->push_back(pai);
                        poi->m_fAttrCount = true;
                        poi->m_nAttrCount = poi->m_pvai->size();
                    }
                }
            }
        }

        if (it->second->m_fModified)
        {
            time_t t = it->second->m_iModified;
            char *pTime = ctime(&t);
            if (NULL != pTime)
            {
                char *p = strchr(pTime, '\n');
                if (NULL != p)
                {
                    size_t n = p - pTime;
                    pTime = StringCloneLen(pTime, n);

                    R7H_ATTRINFO *pai = new R7H_ATTRINFO;
                    pai->SetNumAndValue(R7H_A_MODIFY_TIME, StringClone(pTime));

                    if (NULL == poi->m_pvai)
                    {
                        vector<R7H_ATTRINFO *> *pvai = new vector<R7H_ATTRINFO *>;
                        pvai->push_back(pai);
                        poi->SetAttrs(pvai->size(), pvai);
                    }
                    else
                    {
                        poi->m_pvai->push_back(pai);
                        poi->m_fAttrCount = true;
                        poi->m_nAttrCount = poi->m_pvai->size();
                    }
                }
            }
        }

        if (NULL != it->second->m_pvai)
        {
            vector<R7H_ATTRINFO *> *pvai = new vector<R7H_ATTRINFO *>;
            for (vector<P6H_ATTRINFO *>::iterator itAttr = it->second->m_pvai->begin(); itAttr != it->second->m_pvai->end(); ++itAttr)
            {
                if (  NULL != (*itAttr)->m_pName
                   && NULL != (*itAttr)->m_pValue)
                {
                    char *pAttrFlags = (*itAttr)->m_pFlags;
                    int iAttrFlags = 0;
                    for (int i = 0; i < sizeof(p6h_attr_flags)/sizeof(p6h_attr_flags[0]); i++)
                    {
                        if (strcasecmp(p6h_attr_flags[i].pName, pAttrFlags) == 0)
                        {
                            iAttrFlags |= p6h_attr_flags[i].mask;
                        }
                    }
                    char *pEncodedAttrValue = EncodeAttrValue(poi->m_dbOwner, (*itAttr)->m_dbOwner, iAttrFlags, (*itAttr)->m_pValue);
                    char *pAttrName = r7h_ConvertAttributeName((*itAttr)->m_pName);
                    map<const char *, int , ltstr>::iterator itFound = AttrNamesKnown.find(pAttrName);
                    if (itFound != AttrNamesKnown.end())
                    {
                        R7H_ATTRINFO *pai = new R7H_ATTRINFO;
                        int iNum = AttrNamesKnown[pAttrName];
                        if (R7H_A_PASS == iNum)
                        {
                            char buffer[200];
                            sprintf(buffer, "$P6H$$%s", (*itAttr)->m_pValue);
                            pEncodedAttrValue = EncodeAttrValue(poi->m_dbOwner, (*itAttr)->m_dbOwner, iAttrFlags, buffer);
                            pai->SetNumAndValue(AttrNamesKnown[pAttrName], StringClone(pEncodedAttrValue));
                        }
                        else
                        {
                            pai->SetNumAndValue(AttrNamesKnown[pAttrName], StringClone(pEncodedAttrValue));
                        }
                        pvai->push_back(pai);
                    }
                    else
                    {
                        itFound = AttrNames.find(pAttrName);
                        if (itFound != AttrNames.end())
                        {
                            R7H_ATTRINFO *pai = new R7H_ATTRINFO;
                            pai->SetNumAndValue(AttrNames[pAttrName], StringClone(pEncodedAttrValue));
                            pvai->push_back(pai);
                        }
                    }
                    free(pAttrName);
                }
            }
            if (0 < pvai->size())
            {
                poi->SetAttrs(pvai->size(), pvai);
                pvai = NULL;
            }
            delete pvai;
        }

        if (NULL != it->second->m_pvli)
        {
            for (vector<P6H_LOCKINFO *>::iterator itLock = it->second->m_pvli->begin(); itLock != it->second->m_pvli->end(); ++itLock)
            {
                if (NULL != (*itLock)->m_pKeyTree)
                {
                    bool fFound = false;
                    int iLock;
                    for (int i = 0; i < sizeof(p6h_locknames)/sizeof(p6h_locknames[0]); i++)
                    {
                        if (strcmp(p6h_locknames[i].pName, (*itLock)->m_pType) == 0)
                        {
                            iLock = p6h_locknames[i].iNum;
                            fFound = true;
                            break;
                        }
                    }

                    if (fFound)
                    {
                        R7H_LOCKEXP *pLock = new R7H_LOCKEXP;
                        if (pLock->ConvertFromP6H((*itLock)->m_pKeyTree))
                        {
                            if (R7H_A_LOCK == iLock)
                            {
                                poi->SetDefaultLock(pLock);
                            }
                            else
                            {
                                char buffer[65536];
                                char *p = pLock->Write(buffer);
                                *p = '\0';

                                // Add it.
                                //
                                R7H_ATTRINFO *pai = new R7H_ATTRINFO;
                                pai->SetNumAndValue(iLock, StringClone(buffer));

                                if (NULL == poi->m_pvai)
                                {
                                    vector<R7H_ATTRINFO *> *pvai = new vector<R7H_ATTRINFO *>;
                                    pvai->push_back(pai);
                                    poi->SetAttrs(pvai->size(), pvai);
                                }
                                else
                                {
                                    poi->m_pvai->push_back(pai);
                                    poi->m_fAttrCount = true;
                                    poi->m_nAttrCount = poi->m_pvai->size();
                                }
                            }
                        }
                        else
                        {
                            delete pLock;
                            fprintf(stderr, "WARNING: Could not convert '%s' lock on #%d containing '%s'.\n", (*itLock)->m_pType, it->first, (*itLock)->m_pKey);
                        }
                    }
                }
            }
        }

        AddObject(poi);

        if (dbRefMax < it->first)
        {
            dbRefMax = it->first;
        }
    }

    // Release memory that we allocated.
    //
    for (map<const char *, int, ltstr>::iterator it = AttrNames.begin(); it != AttrNames.end(); ++it)
    {
        delete it->first;
    }
    for (map<const char *, int, ltstr>::iterator it = AttrNamesKnown.begin(); it != AttrNamesKnown.end(); ++it)
    {
        delete it->first;
    }

    SetSizeHint(dbRefMax+1);
    SetRecordPlayers(0);
}

void R7H_GAME::ResetPassword()
{
    for (map<int, R7H_OBJECTINFO *, lti>::iterator itObj = m_mObjects.begin(); itObj != m_mObjects.end(); ++itObj)
    {
        if (1 == itObj->first)
        {
            bool fFound = false;
            if (NULL != itObj->second->m_pvai)
            {
                for (vector<R7H_ATTRINFO *>::iterator itAttr = itObj->second->m_pvai->begin(); itAttr != itObj->second->m_pvai->end(); ++itAttr)
                {
                    if (R7H_A_PASS == (*itAttr)->m_iNum)
                    {
                        // Change it to 'potrzebie'.
                        //
                        (*itAttr)->SetNumAndValue(R7H_A_PASS, StringClone("XXNHc95o0HhAc"));

                        fFound = true;
                    }
                }
            }

            if (!fFound)
            {
                // Add it.
                //
                R7H_ATTRINFO *pai = new R7H_ATTRINFO;
                pai->SetNumAndValue(R7H_A_PASS, StringClone("XXNHc95o0HhAc"));

                if (NULL == itObj->second->m_pvai)
                {
                    vector<R7H_ATTRINFO *> *pvai = new vector<R7H_ATTRINFO *>;
                    pvai->push_back(pai);
                    itObj->second->SetAttrs(pvai->size(), pvai);
                }
                else
                {
                    itObj->second->m_pvai->push_back(pai);
                    itObj->second->m_fAttrCount = true;
                    itObj->second->m_nAttrCount = itObj->second->m_pvai->size();
                }
            }
        }
    }
}

void R7H_GAME::Extract(FILE *fp, int dbExtract)
{
    fprintf(fp, "Extraction is not currently supported for this flatfile format.\n");
}
