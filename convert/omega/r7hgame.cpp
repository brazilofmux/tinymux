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

static char *EncodeString(const char *str, bool fExtraEscapes)
{
    static char buf[65536];
    char *p = buf;
    while (  '\0' != *str
          && p < buf + sizeof(buf) - 2)
    {
        if (  '\\' == *str
           || '"' == *str)
        {
            *p++ = '\\';
            *p++ = *str++;
        }
        else if (fExtraEscapes && '\r' == *str)
        {
            *p++ = '\\';
            *p++ = 'r';
            str++;
        }
        else if (fExtraEscapes && '\n' == *str)
        {
            *p++ = '\\';
            *p++ = 'n';
            str++;
        }
        else if (fExtraEscapes && '\t' == *str)
        {
            *p++ = '\\';
            *p++ = 't';
            str++;
        }
        else if (fExtraEscapes && '\x1B' == *str)
        {
            *p++ = '\\';
            *p++ = 'e';
            str++;
        }
        else
        {
            *p++ = *str++;
        }
    }
    *p = '\0';
    return buf;
}

void R7H_ATTRNAMEINFO::Write(FILE *fp, bool fExtraEscapes)
{
    if (m_fNumAndName)
    {
        fprintf(fp, "+A%d\n\"%s\"\n", m_iNum, EncodeString(m_pName, fExtraEscapes));
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
     42,  // A_LOCK
     59,  // A_LENTER
     60,  // A_LLEAVE
     61,  // A_LPAGE
     62,  // A_LUSE
     63,  // A_LGIVE
     85,  // A_LTPORT
     86,  // A_LDROP
     87,  // A_LRECEIVE
     93,  // A_LLINK
     94,  // A_LTELOUT
     97,  // A_LUSER
     98,  // A_LPARENT
    127,  // A_LGET
    209,  // A_LSPEECH
    225,  // A_LMAIL
    226,  // A_LOPEN
    231,  // A_LVISIBLE
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
                    (*it)->m_pKeyTree = r7hl_ParseKey((*it)->m_pValue);
                    if (NULL == (*it)->m_pKeyTree)
                    {
                       fprintf(stderr, "WARNING: Lock key '%s' is not valid.\n", (*it)->m_pValue);
                    }
                    break;
                }
            }
        }
    }
}

void R7H_ATTRINFO::SetNumAndValue(int iNum, char *pValue)
{
    m_fNumAndValue = true;
    m_iNum = iNum;
    if (NULL != m_pValue)
    {
        free(m_pValue);
    }
    m_pValue = pValue;
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
        if (m_nSizeHint < dbRefMax)
        {
            fprintf(stderr, "WARNING: +S phrase does not leave room for the dbrefs.\n");
        }
        else if (m_nSizeHint != dbRefMax)
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
                if (!r7h_AttrNameInitialSet[*q])
                {
                    fValid = false;
                }
                else if ('\0' != *q)
                {
                    q++;
                    while ('\0' != *q)
                    {
                        if (!r7h_AttrNameSet[*q])
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
    int ver = (m_flags & R7H_V_MASK);
    ValidateFlags();
    ValidateAttrNames(ver);
    ValidateObjects();
}

void R7H_OBJECTINFO::Write(FILE *fp, bool bWriteLock, bool fExtraEscapes)
{
    fprintf(fp, "!%d\n", m_dbRef);
    if (NULL != m_pName)
    {
        fprintf(fp, "\"%s\"\n", EncodeString(m_pName, fExtraEscapes));
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
    if (m_fPowers1)
    {
        fprintf(fp, "%d\n", m_iPowers1);
    }
    if (m_fPowers2)
    {
        fprintf(fp, "%d\n", m_iPowers2);
    }
    if (m_fAccessed)
    {
        fprintf(fp, "%d\n", m_iAccessed);
    }
    if (m_fModified)
    {
        fprintf(fp, "%d\n", m_iModified);
    }
    if (m_fCreated)
    {
        fprintf(fp, "%d\n", m_iCreated);
    }
    if (  m_fAttrCount
       && NULL != m_pvai)
    {
        for (vector<R7H_ATTRINFO *>::iterator it = m_pvai->begin(); it != m_pvai->end(); ++it)
        {
            (*it)->Write(fp, fExtraEscapes);
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
        if (strcmp(m_pValue, buffer) != 0)
        {
            fprintf(stderr, "WARNING: Re-generated lock key '%s' does not agree with parsed key '%s'.\n", buffer, m_pValue);
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

void R7H_ATTRINFO::Write(FILE *fp, bool fExtraEscapes) const
{
    if (m_fNumAndValue)
    {
        fprintf(fp, ">%d\n\"%s\"\n", m_iNum, EncodeString(m_pValue, fExtraEscapes));
    }
}

void R7H_GAME::Write(FILE *fp)
{
    // TIMESTAMPS and escapes occured near the same time, but are not related.
    //
    bool fExtraEscapes = false;
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
        (*it)->Write(fp, fExtraEscapes);
    } 
    for (map<int, R7H_OBJECTINFO *, lti>::iterator it = m_mObjects.begin(); it != m_mObjects.end(); ++it)
    {
        it->second->Write(fp, (m_flags & R7H_V_ATRKEY) == 0, fExtraEscapes);
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
    { "TRANSPARENT",    0x00000008UL },
    { "WIZARD",         0x00000010UL },
    { "LINK_OK",        0x00000020UL },
    { "DARK",           0x00000040UL },
    { "JUMP_OK",        0x00000080UL },
    { "STICKY",         0x00000100UL },
    { "DESTROY_OK",     0x00000200UL },
    { "HAVEN",          0x00000400UL },
    { "QUIET",          0x00000800UL },
    { "HALT",           0x00001000UL },
    { "DEBUG",          0x00002000UL },
    { "GOING",          0x00004000UL },
    { "MONITOR",        0x00008000UL },
    { "MYOPIC",         0x00010000UL },
    { "PUPPET",         0x00020000UL },
    { "CHOWN_OK",       0x00040000UL },
    { "ENTER_OK",       0x00080000UL },
    { "VISUAL",         0x00100000UL },
    { "OPAQUE",         0x00800000UL },
    { "VERBOSE",        0x01000000UL },
    { "NOSPOOF",        0x04000000UL },
    { "SAFE",           0x10000000UL },
    { "ROYALTY",        0x20000000UL },
    { "AUDIBLE",        0x40000000UL },
    { "TERSE",          0x80000000UL },
};

static NameMask p6h_convert_obj_flags2[] =
{
    { "ABODE",          0x00000002UL },
    { "FLOATING",       0x00000004UL },
    { "UNFINDABLE",     0x00000008UL },
    { "LIGHT",          0x00000020UL },
    { "ANSI",           0x00000200UL },
    { "COLOR",          0x00000200UL },
    { "FIXED",          0x00000800UL },
    { "UNINSPECTED",    0x00001000UL },
    { "NO_COMMAND",     0x00002000UL },
    { "KEEPALIVE",      0x00004000UL },
    { "GAGGED",         0x00040000UL },
    { "ON-VACATION",    0x01000000UL },
    { "SUSPECT",        0x10000000UL },
    { "NOACCENTS",      0x20000000UL },
    { "SLAVE",          0x80000000UL },
};

static NameMask p6h_convert_obj_powers1[] =
{
    { "Announce",       0x00000004UL },
    { "Boot",           0x00000008UL },
    { "Guest",          0x02000000UL },
    { "Halt",           0x00000010UL },
    { "Hide",           0x00000800UL },
    { "Idle",           0x00001000UL },
    { "Long_Fingers",   0x00004000UL },
    { "No_Pay",         0x00000200UL },
    { "No_Quota",       0x00000400UL },
    { "Poll",           0x00800000UL },
    { "Quotas",         0x00000001UL },
    { "Search",         0x00002000UL },
    { "See_All",        0x00000080UL },
    { "See_Queue",      0x00100000UL },
    { "Tport_Anything", 0x40000000UL },
    { "Tport_Anywhere", 0x20000000UL },
    { "Unkillable",     0x80000000UL },
};

static NameMask p6h_convert_obj_powers2[] =
{
    { "Builder",        0x00000001UL },
};

static struct
{
    const char *pName;
    int         iNum;
} r7h_known_attrs[] =
{
    { "AAHEAR",         27 },
    { "ACLONE",         20 },
    { "ACONNECT",       39 },
    { "ADESC",          -1 },  // rename ADESC to XADESC
    { "ADESCRIBE",      36 },  // rename ADESCRIBE to ADESC
    { "ADFAIL",         -1 },  // rename ADFAIL to XADFAIL
    { "ADISCONNECT",    40 },
    { "ADROP",          14 },
    { "AEFAIL",         68 },
    { "AENTER",         35 },
    { "AFAIL",          -1 }, // rename AFAIL to XAFAIL
    { "AFAILURE",       13 }, // rename AFAILURE to AFAIL
    { "AGFAIL",         -1 }, // rename AGFAIL to XAGFAIL
    { "AHEAR",          29 },
    { "AKILL",          -1 }, // rename AKILL to XAKILL
    { "ALEAVE",         52 },
    { "ALFAIL",         71 },
    { "ALIAS",          58 },
    { "ALLOWANCE",      -1 },
    { "AMAIL",         202 },
    { "AMHEAR",         28 },
    { "AMOVE",          57 },
    { "APAY",           -1 }, // rename APAY to XAPAY
    { "APAYMENT",       21 }, // rename APAYMENT to APAY
    { "ARFAIL",         -1 },
    { "ASUCC",          -1 }, // rename ASUCC to XASUCC
    { "ASUCCESS",       12 }, // rename AUCCESS to ASUCC
    { "ATFAIL",         -1 }, // rename ATFAIL to XATFAIL
    { "ATPORT",         82 },
    { "ATOFAIL",        -1 }, // rename ATOFAIL to XATOFAIL
    { "AUFAIL",         77 },
    { "AUSE",           16 },
    { "AWAY",           73 },
    { "CHARGES",        17 },
    { "CMDCHECK",       -1 }, // rename CMDCHECK to XCMDCHECK
    { "COMMENT",        44 },
    { "CONFORMAT",     242 },
    { "CONNINFO",       -1 },
    { "COST",           24 },
    { "CREATED",        -1 }, // rename CREATED to XCREATED
    { "DAILY",          -1 }, // rename DAILY to XDAILY
    { "DESC",           -1 }, // rename DESC to XDESC
    { "DESCRIBE",        6 }, // rename DESCRIBE to DESC
    { "DEFAULTLOCK",    -1 }, // rename DEFAULTLOCK to XDEFAULTLOCK
    { "DESCFORMAT",    244 },
    { "DESTINATION",   216 },
    { "DESTROYER",      -1 }, // rename DESTROYER to XDESTROYER
    { "DFAIL",          -1 }, // rename DFAIL to XDFAIL
    { "DROP",            9 },
    { "DROPLOCK",       -1 }, // rename DROPLOCK to XDROPLOCK
    { "EALIAS",         64 },
    { "EFAIL",          66 },
    { "ENTER",          33 },
    { "ENTERLOCK",      -1 }, // rename ENTERLOCK to XENTERLOCK
    { "EXITFORMAT",    241 },
    { "EXITTO",        216 },
    { "FAIL",           -1 }, // rename FAIL to XFAIL
    { "FAILURE",         3 }, // rename FAILURE to FAIL
    { "FILTER",         92 },
    { "FORWARDLIST",    95 },
    { "GETFROMLOCK",    -1 }, // rename GETFROMLOCK to XGETFROMLOCK
    { "GFAIL",          -1 }, // rename GFAIL to XGFAIL
    { "GIVELOCK",       -1 }, // rename GIVELOCK to XGIVELOCK
    { "HTDESC",         -1 }, // rename HTDESC to XHTDESC
    { "IDESC",          -1 }, // rename IDESC to XIDESC
    { "IDESCRIBE",      32 }, // rename IDESCRIBE to IDESC
    { "IDLE",           74 },
    { "IDLETIMEOUT",    -1 }, // rename IDLETIMEOUT to XIDLETIMEOUT
    { "INFILTER",       91 },
    { "INPREFIX",       89 },
    { "KILL",           -1 }, // rename KILL to XKILL
    { "LALIAS",         65 },
    { "LAST",           30 },
    { "LASTPAGE",       -1 }, // rename LASTPAGE to XLASTPAGE
    { "LASTSITE",       88 },
    { "LASTIP",        144 },
    { "LEAVE",          50 },
    { "LEAVELOCK",      -1 }, // rename LEAVELOCK to XLEAVELOCK
    { "LFAIL",          69 },
    { "LINKLOCK",       -1 }, // rename LINKLOCK to XLINKLOCK
    { "LISTEN",         26 },
    { "LOGINDATA",      -1 }, // rename LOGINDATA to XLOGINDATA
    { "MAILCURF",       -1 }, // rename MAILCURF to XMAILCURF
    { "MAILFLAGS",      -1 }, // rename MAILFLAGS to XMAILFLAGS
    { "MAILFOLDERS",    -1 }, // rename MAILFOLDERS to XMAILFOLDERS
    { "MAILLOCK",       -1 }, // rename MAILLOCK to XMAILLOCK
    { "MAILMSG",        -1 }, // rename MAILMSG to XMAILMSG
    { "MAILSUB",        -1 }, // rename MAILSUB to XMAILSUB
    { "MAILSUCC",       -1 }, // rename MAILSUCC to XMAILSUCC
    { "MAILTO",         -1 }, // rename MAILTO to XMAILTO
    { "MFAIL",          -1 }, // rename MFAIL to XMFAIL
    { "MODIFIED",       -1 }, // rename MODIFIED to XMODIFIED
    { "MONIKER",        -1 }, // rename MONIKER to XMONIKER
    { "MOVE",           55 },
    { "NAME",           -1 }, // rename NAME to XNAME
    { "NAMEFORMAT",    243 },
    { "ODESC",          -1 }, // rename ODESC to XODESC
    { "ODESCRIBE",      37 }, // rename ODESCRIBE to ODESC
    { "ODFAIL",         -1 }, // rename ODFAIL to XODFAIL
    { "ODROP",           8 },
    { "OEFAIL",         67 },
    { "OENTER",         53 },
    { "OFAIL",          -1 }, // rename OFAIL to XOFAIL
    { "OFAILURE",        2 }, // rename OFAILURE to OFAIL
    { "OGFAIL",         -1 }, // rename OGFAIL to XOGFAIL
    { "OKILL",          -1 }, // rename OKILL to XOKILL
    { "OLEAVE",         51 },
    { "OLFAIL",         70 },
    { "OMOVE",          56 },
    { "OPAY",           -1 }, // rename OPAY to XOPAY
    { "OPAYMENT",       22 }, // rename OPAYMENT to OPAY
    { "OPENLOCK",       -1 }, // rename OPENLOCK to XOPENLOCK
    { "ORFAIL",         -1 }, // rename ORFAIL to XORFAIL
    { "OSUCC",          -1 }, // rename OSUCC to XSUCC
    { "OSUCCESS",        1 }, // rename OSUCCESS to OSUCC
    { "OTFAIL",         -1 }, // rename OTFAIL to XOTFAIL
    { "OTPORT",         80 },
    { "OTOFAIL",        -1 }, // rename OTOFAIL to XOTOFAIL
    { "OUFAIL",         76 },
    { "OUSE",           46 },
    { "OXENTER",        34 },
    { "OXLEAVE",        54 },
    { "OXTPORT",        81 },
    { "PAGELOCK",       -1 }, // rename PAGELOCK to XPAGELOCK
    { "PARENTLOCK",     -1 }, // rename PARENTLOCK to XPARENTLOCK
    { "PAY",            -1 }, // rename PAY to XPAY
    { "PAYMENT",        23 }, // rename PAYMENT to PAY
    { "PREFIX",         90 },
    { "PROGCMD",        -1 }, // rename PROGCMD to XPROGCMD
    { "QUEUEMAX",       -1 }, // rename QUEUEMAX to XQUEUEMAX
    { "QUOTA",          -1 }, // rename QUOTA to XQUOTA
    { "RECEIVELOCK",    -1 },
    { "REJECT",         -1 }, // rename REJECT to XREJECT
    { "REASON",         -1 }, // rename REASON to XREASON
    { "RFAIL",          -1 }, // rename RFAIL to XRFAIL
    { "RQUOTA",         38 },
    { "RUNOUT",         18 },
    { "SAYSTRING",      -1 }, // rename SAYSTRING to XSAYSTRING
    { "SEMAPHORE",      47 },
    { "SEX",             7 },
    { "SIGNATURE",      -1 }, // rename SIGNATURE to XSIGNATURE
    { "MAILSIGNATURE", 203 }, // rename MAILSIGNATURE to SIGNATURE
    { "SPEECHMOD",      -1 }, // rename SPEECHMOD to XSPEECHMOD
    { "SPEECHLOCK",     -1 }, // rename SPEECHLOCK to XSPEECHLOCK
    { "STARTUP",        19 },
    { "SUCC",            4 },
    { "TELOUTLOCK",     -1 }, // rename TELOUTLOCK to XTELOUTLOCK
    { "TFAIL",          -1 }, // rename TFAIL to XTFAIL
    { "TIMEOUT",        -1 }, // rename TIMEOUT to XTIMEOUT
    { "TPORT",          79 },
    { "TPORTLOCK",      -1 }, // rename TPORTLOCK to XTPORTLOCK
    { "TOFAIL",         -1 }, // rename TOFAIL to XTOFAIL
    { "UFAIL",          75 },
    { "USE",            45 },
    { "USELOCK",        -1 },
    { "USERLOCK",       -1 },
    { "VA",            100 },
    { "VB",            101 },
    { "VC",            102 },
    { "VD",            103 },
    { "VE",            104 },
    { "VF",            105 },
    { "VG",            106 },
    { "VH",            107 },
    { "VI",            108 },
    { "VJ",            109 },
    { "VK",            110 },
    { "VL",            111 },
    { "VM",            112 },
    { "VRML_URL",      220 },
    { "VN",            113 },
    { "VO",            114 },
    { "VP",            115 },
    { "VQ",            116 },
    { "VR",            117 },
    { "VS",            118 },
    { "VT",            119 },
    { "VU",            120 },
    { "VV",            121 },
    { "VW",            122 },
    { "VX",            123 },
    { "VY",            124 },
    { "VZ",            125 },
    { "XYXXY",           5 },   // *Password
};

static struct
{
    const char *pName;
    int         iNum;
} p6h_locknames[] =
{
    { "Basic",       42 },
    { "Enter",       59 },
    { "Use",         62 },
    { "Zone",        -1 },
    { "Page",        61 },
    { "Teleport",    85 },
    { "Speech",     209 },
    { "Parent",      98 },
    { "Link",        93 },
    { "Leave",       60 },
    { "Drop",        86 },
    { "Give",        63 },
    { "Receive",     87 },
    { "Mail",       225 },
    { "Take",       127 },
    { "Open",       225 },
};

static NameMask p6h_attr_flags[] =
{
    { "no_command",     0x00000100UL },
    { "private",        0x00001000UL },
    { "no_clone",       0x00010000UL },
    { "wizard",         0x00000004UL },
    { "visual",         0x00000800UL },
    { "mortal_dark",    0x00000008UL },
    { "hidden",         0x00000002UL },
    { "regexp",         0x00008000UL },
    { "case",           0x00040000UL },
    { "locked",         0x00000040UL },
    { "internal",       0x00000010UL },
    { "debug",          0x00080000UL },
    { "noname",         0x00400000UL },
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

        // Flagwords
        //
        int flags1 = iType;
        int flags2 = 0;
        int flags3 = 0;
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
        }

        // Powers
        //
        int powers1 = 0;
        int powers2 = 0;
        char *pPowers = it->second->m_pPowers;
        if (NULL != pPowers)
        {
            // First powerword
            //
            for (int i = 0; i < sizeof(p6h_convert_obj_powers1)/sizeof(p6h_convert_obj_powers1[0]); i++)
            {
                if (NULL != strcasestr(pPowers, p6h_convert_obj_powers1[i].pName))
                {
                    powers1 |= p6h_convert_obj_powers1[i].mask;
                }
            }

            // Second powerword
            //
            for (int i = 0; i < sizeof(p6h_convert_obj_powers2)/sizeof(p6h_convert_obj_powers2[0]); i++)
            {
                if (NULL != strcasestr(pPowers, p6h_convert_obj_powers2[i].pName))
                {
                    powers2 |= p6h_convert_obj_powers2[i].mask;
                }
            }

            // Immortal power is special.
            //
            if (NULL != strcasestr(pPowers, "Immortal"))
            {
                flags1 |= 0x00200000;
            }
        }
        poi->SetFlags1(flags1);
        poi->SetFlags2(flags2);
        poi->SetFlags3(flags3);
        poi->SetPowers1(powers1);
        poi->SetPowers2(powers2);

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

                    // A_CREATED
                    //
                    R7H_ATTRINFO *pai = new R7H_ATTRINFO;
                    pai->SetNumAndValue(218, StringClone(pTime));
        
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

                    // A_MODIFIED
                    //
                    R7H_ATTRINFO *pai = new R7H_ATTRINFO;
                    pai->SetNumAndValue(219, StringClone(pTime));
        
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
                        if (5 == iNum)
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

    SetSizeHint(dbRefMax);
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
                    if (5 == (*itAttr)->m_iNum)
                    {
                        // Change it to 'potrzebie'.
                        //
                        free((*itAttr)->m_pValue);
                        (*itAttr)->m_pValue = StringClone("XXNHc95o0HhAc");

                        fFound = true;
                    }
                }
            }

            if (!fFound)
            {
                // Add it.
                //
                R7H_ATTRINFO *pai = new R7H_ATTRINFO;
                pai->SetNumAndValue(5, StringClone("XXNHc95o0HhAc"));

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
