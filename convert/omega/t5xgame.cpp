#include "omega.h"
#include "t5xgame.h"
#include "p6hgame.h"

typedef struct _t5x_gameflaginfo
{
    int         mask;
    const char *pName;
} t5x_gameflaginfo;

t5x_gameflaginfo t5x_gameflagnames[] =
{
    { V_ZONE,     "V_ZONE"     },
    { V_LINK,     "V_LINK"     },
    { V_DATABASE, "V_DATABASE" },
    { V_ATRNAME,  "V_ATRNAME"  },
    { V_ATRKEY,   "V_ATRKEY"   },
    { V_PARENT,   "V_PARENT"   },
    { V_ATRMONEY, "V_ATRMONEY" },
    { V_XFLAGS,   "V_XFLAGS"   },
    { V_POWERS,   "V_POWERS"   },
    { V_3FLAGS,   "V_3FLAGS"   },
    { V_QUOTED,   "V_QUOTED"   },
};
#define T5X_NUM_GAMEFLAGNAMES (sizeof(t5x_gameflagnames)/sizeof(t5x_gameflagnames[0]))

T5X_GAME g_t5xgame;
T5X_LOCKEXP *t5xl_ParseKey(char *pKey);

// The first character of an attribute name must be either alphabetic,
// '_', '#', '.', or '~'. It's handled by the following table.
//
// Characters thereafter may be letters, numbers, and characters from
// the set {'?!`/-_.@#$^&~=+<>()}. Lower-case letters are turned into
// uppercase before being used, but lower-case letters are valid input.
//
bool t5x_AttrNameInitialSet[256] =
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

bool t5x_AttrNameSet[256] =
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

char *t5x_ConvertAttributeName(const char *pName)
{
    char aBuffer[256];
    char *pBuffer = aBuffer;
    if (  '\0' != *pName
       && pBuffer < aBuffer + sizeof(aBuffer) - 1)
    {
        if (t5x_AttrNameInitialSet[(unsigned char) *pName])
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
        if (t5x_AttrNameSet[(unsigned char) *pName])
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

void T5X_LOCKEXP::Write(FILE *fp)
{
    switch (m_op)
    {
    case T5X_LOCKEXP::le_is:
        fprintf(fp, "(=");
        m_le[0]->Write(fp);
        fprintf(fp, ")");
        break;

    case T5X_LOCKEXP::le_carry:
        fprintf(fp, "(+");
        m_le[0]->Write(fp);
        fprintf(fp, ")");
        break;

    case T5X_LOCKEXP::le_indirect:
        fprintf(fp, "(@");
        m_le[0]->Write(fp);
        fprintf(fp, ")");
        break;

    case T5X_LOCKEXP::le_owner:
        fprintf(fp, "($");
        m_le[0]->Write(fp);
        fprintf(fp, ")");
        break;

    case T5X_LOCKEXP::le_and:
        fprintf(fp, "(");
        m_le[0]->Write(fp);
        fprintf(fp, "&");
        m_le[1]->Write(fp);
        fprintf(fp, ")");
        break;

    case T5X_LOCKEXP::le_or:
        fprintf(fp, "(");
        m_le[0]->Write(fp);
        fprintf(fp, "|");
        m_le[1]->Write(fp);
        fprintf(fp, ")");
        break;

    case T5X_LOCKEXP::le_not:
        fprintf(fp, "(!");
        m_le[0]->Write(fp);
        fprintf(fp, ")");
        break;

    case T5X_LOCKEXP::le_attr:
        m_le[0]->Write(fp);
        fprintf(fp, ":");
        m_le[1]->Write(fp);

        // The code in 2.6 an earlier does not always emit a NL.  It's really
        // a beneign typo, but we reproduce it to make regression testing
        // easier.
        //
        if (m_le[0]->m_op != T5X_LOCKEXP::le_text)
        {
            fprintf(fp, "\n");
        }
        break;

    case T5X_LOCKEXP::le_eval:
        m_le[0]->Write(fp);
        fprintf(fp, "/");
        m_le[1]->Write(fp);
        fprintf(fp, "\n");
        break;

    case T5X_LOCKEXP::le_ref:
        fprintf(fp, "%d\n", m_dbRef);
        break;

    case T5X_LOCKEXP::le_text:
        fprintf(fp, "%s", m_p[0]);
        break;
    }
}

char *T5X_LOCKEXP::Write(char *p)
{
    switch (m_op)
    {
    case T5X_LOCKEXP::le_is:
        *p++ = '=';
        *p++ = '(';
        p = m_le[0]->Write(p);
        *p++ = ')';
        break;

    case T5X_LOCKEXP::le_carry:
        *p++ = '+';
        *p++ = '(';
        p = m_le[0]->Write(p);
        *p++ = ')';
        break;

    case T5X_LOCKEXP::le_indirect:
        *p++ = '@';
        *p++ = '(';
        p = m_le[0]->Write(p);
        *p++ = ')';
        break;

    case T5X_LOCKEXP::le_owner:
        *p++ = '$';
        *p++ = '(';
        p = m_le[0]->Write(p);
        *p++ = ')';
        break;

    case T5X_LOCKEXP::le_or:
        p = m_le[0]->Write(p);
        *p++ = '|';
        p = m_le[1]->Write(p);
        break;

    case T5X_LOCKEXP::le_not:
        *p++ = '!';
        p = m_le[0]->Write(p);
        break;

    case T5X_LOCKEXP::le_attr:
        p = m_le[0]->Write(p);
        *p++ = ':';
        p = m_le[1]->Write(p);
        break;

    case T5X_LOCKEXP::le_eval:
        p = m_le[0]->Write(p);
        *p++ = '/';
        p = m_le[1]->Write(p);
        break;

    case T5X_LOCKEXP::le_and:
        p = m_le[0]->Write(p);
        *p++ = '&';
        p = m_le[1]->Write(p);
        break;

    case T5X_LOCKEXP::le_ref:
        sprintf(p, "#%d", m_dbRef);
        p += strlen(p);
        break;

    case T5X_LOCKEXP::le_text:
        sprintf(p, "%s", m_p[0]);
        p += strlen(p);
        break;

    default:
        fprintf(stderr, "%d not recognized.\n", m_op);
        break;
    }
    return p;
}

bool T5X_LOCKEXP::ConvertFromP6H(P6H_LOCKEXP *p)
{
    switch (p->m_op)
    {
    case P6H_LOCKEXP::le_is:
        m_op = T5X_LOCKEXP::le_is;
        m_le[0] = new T5X_LOCKEXP;
        if (!m_le[0]->ConvertFromP6H(p->m_le[0]))
        {
            delete m_le[0];
            m_le[0] = NULL;
            return false;
        }
        break;

    case P6H_LOCKEXP::le_carry:
        m_op = T5X_LOCKEXP::le_carry;
        m_le[0] = new T5X_LOCKEXP;
        if (!m_le[0]->ConvertFromP6H(p->m_le[0]))
        {
            delete m_le[0];
            m_le[0] = NULL;
            return false;
        }
        break;

    case P6H_LOCKEXP::le_indirect:
        m_op = T5X_LOCKEXP::le_indirect;
        m_le[0] = new T5X_LOCKEXP;
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
        m_op = T5X_LOCKEXP::le_owner;
        m_le[0] = new T5X_LOCKEXP;
        if (!m_le[0]->ConvertFromP6H(p->m_le[0]))
        {
            delete m_le[0];
            m_le[0] = NULL;
            return false;
        }
        break;

    case P6H_LOCKEXP::le_or:
        m_op = T5X_LOCKEXP::le_or;
        m_le[0] = new T5X_LOCKEXP;
        m_le[1] = new T5X_LOCKEXP;
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
        m_op = T5X_LOCKEXP::le_not;
        m_le[0] = new T5X_LOCKEXP;
        if (!m_le[0]->ConvertFromP6H(p->m_le[0]))
        {
            delete m_le[0];
            m_le[0] = NULL;
            return false;
        }
        break;

    case P6H_LOCKEXP::le_attr:
        m_op = T5X_LOCKEXP::le_attr;
        m_le[0] = new T5X_LOCKEXP;
        m_le[1] = new T5X_LOCKEXP;
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
        m_op = T5X_LOCKEXP::le_eval;
        m_le[0] = new T5X_LOCKEXP;
        m_le[1] = new T5X_LOCKEXP;
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
        m_op = T5X_LOCKEXP::le_and;
        m_le[0] = new T5X_LOCKEXP;
        m_le[1] = new T5X_LOCKEXP;
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
        m_op = T5X_LOCKEXP::le_ref;
        m_dbRef = p->m_dbRef;
        break;

    case P6H_LOCKEXP::le_text:
        m_op = T5X_LOCKEXP::le_text;
        m_p[0] = StringClone(p->m_p[0]);
        break;

    case P6H_LOCKEXP::le_class:
        return false;
        break;

    case P6H_LOCKEXP::le_true:
        m_op = T5X_LOCKEXP::le_text;
        m_p[0] = StringClone("1");
        break;

    case P6H_LOCKEXP::le_false:
        m_op = T5X_LOCKEXP::le_text;
        m_p[0] = StringClone("0");
        break;

    default:
        fprintf(stderr, "%d not recognized.\n", m_op);
        break;
    }
    return true;
}
 
void T5X_ATTRNAMEINFO::SetNumAndName(int iNum, char *pName)
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

void T5X_ATTRNAMEINFO::Write(FILE *fp, bool fExtraEscapes)
{
    if (m_fNumAndName)
    {
        fprintf(fp, "+A%d\n\"%s\"\n", m_iNum, EncodeString(m_pName, fExtraEscapes));
    }
}

void T5X_OBJECTINFO::SetName(char *pName)
{
    if (NULL != m_pName)
    {
        free(m_pName);
    }
    m_pName = pName;
}

const int t5x_locknums[] =
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

void T5X_OBJECTINFO::SetAttrs(int nAttrs, vector<T5X_ATTRINFO *> *pvai)
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
        for (vector<T5X_ATTRINFO *>::iterator it = m_pvai->begin(); it != m_pvai->end(); ++it)
        {
            (*it)->m_fIsLock = false;
            for (int i = 0; i < sizeof(t5x_locknums)/sizeof(t5x_locknums[0]); i++)
            {
                if (t5x_locknums[i] == (*it)->m_iNum)
                {
                    (*it)->m_fIsLock = true;
                    break;
                }
            }
        }
    }
}

void T5X_ATTRINFO::SetNumAndValue(int iNum, char *pValue)
{
    m_fNumAndValue = true;
    m_iNum = iNum;
    if (NULL != m_pValue)
    {
        free(m_pValue);
    }
    m_pValue = pValue;
}

void T5X_GAME::AddNumAndName(int iNum, char *pName)
{
    T5X_ATTRNAMEINFO *pani = new T5X_ATTRNAMEINFO;
    pani->SetNumAndName(iNum, pName);
    m_vAttrNames.push_back(pani);
}

void T5X_GAME::AddObject(T5X_OBJECTINFO *poi)
{
    m_vObjects.push_back(poi);
}

void T5X_GAME::ValidateFlags()
{
    int flags = m_flags;

    int ver = (m_flags & V_MASK);
    fprintf(stderr, "INFO: Flatfile version is %d\n", ver);
    if (ver < 1 || 3 < ver)
    {
        fprintf(stderr, "WARNING: Expecting version to be between 1 and 3.\n");
    }
    flags &= ~V_MASK;
    int tflags = flags;

    fprintf(stderr, "INFO: Flatfile flags are ");
    for (int i = 0; i < T5X_NUM_GAMEFLAGNAMES; i++)
    {
        if (t5x_gameflagnames[i].mask & tflags)
        {
            fprintf(stderr, "%s ", t5x_gameflagnames[i].pName);
            tflags &= ~t5x_gameflagnames[i].mask;
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
    if (  2 == ver
       && (flags & MANDFLAGS_V2) != MANDFLAGS_V2)
    {
        fprintf(stderr, "WARNING: Not all mandatory flags for v2 are present.\n");
    }
    else if (  3 == ver
            && (flags & MANDFLAGS_V3) != MANDFLAGS_V3)
    {
        fprintf(stderr, "WARNING: Not all mandatory flags for v3 are present.\n");
    }

    // Validate that this is a flatfile and not a structure file.
    //
    if (  (flags & V_DATABASE) != 0
       || (flags & V_ATRNAME) != 0
       || (flags & V_ATRMONEY) != 0)
    {
        fprintf(stderr, "WARNING: Expected a flatfile (with strings) instead of a structure file (with only object anchors).\n");
    }
}

void T5X_GAME::ValidateObjects()
{
    if (!m_fSizeHint)
    {
        fprintf(stderr, "WARNING: +S phrase for next object was missing.\n");
    }
    else
    {
        int dbRefMax = 0;
        for (vector<T5X_OBJECTINFO *>::iterator it = m_vObjects.begin(); it != m_vObjects.end(); ++it)
        {
            (*it)->Validate();
            if (dbRefMax < (*it)->m_dbRef)
            {
                dbRefMax = (*it)->m_dbRef;
            }
        } 
      
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

void T5X_GAME::ValidateAttrNames()
{
    if (!m_fNextAttr)
    {
        fprintf(stderr, "WARNING: +N phrase for attribute count was missing.\n");
    }
    else
    {
        int n = 256;
        for (vector<T5X_ATTRNAMEINFO *>::iterator it = m_vAttrNames.begin(); it != m_vAttrNames.end(); ++it)
        {
            if ((*it)->m_fNumAndName)
            {
                int iNum = (*it)->m_iNum;
                if (iNum < A_USER_START)
                {
                    fprintf(stderr, "WARNING: User attribute (%s) uses an attribute number (%d) which is below %d.\n", (*it)->m_pName, iNum, A_USER_START);
                }
                if (n <= iNum)
                {
                    n = iNum + 1;
                }
            }
            else
            {
                fprintf(stderr, "WARNING: Unexpected ATTRNAMEINFO -- internal error.\n");
            }
        }
        if (m_nNextAttr != n)
        {
            fprintf(stderr, "WARNING: +N phrase (%d) does not agree with the maximum attribute number (%d).\n", m_nNextAttr, n);
        }
    }
}

void T5X_GAME::Validate()
{
    ValidateFlags();
    ValidateAttrNames();
    ValidateObjects();
}

void T5X_OBJECTINFO::Write(FILE *fp, bool bWriteLock, bool fExtraEscapes)
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
    if (  m_fAttrCount
       && NULL != m_pvai)
    {
        for (vector<T5X_ATTRINFO *>::iterator it = m_pvai->begin(); it != m_pvai->end(); ++it)
        {
            (*it)->Write(fp, fExtraEscapes);
        }
    }
    fprintf(fp, "<\n");
}

void T5X_ATTRINFO::Validate()
{
    if (m_fIsLock)
    {
        delete m_pKeyTree;
        m_pKeyTree = t5xl_ParseKey(m_pValue);
        if (NULL == m_pKeyTree)
        {
            fprintf(stderr, "WARNING: Lock key '%s' is not valid.\n", m_pValue);
            exit(1);
        }
        else
        {
            char buffer[65536];
            char *p = m_pKeyTree->Write(buffer);
            *p = '\0';
            if (strcmp(m_pValue, buffer) != 0)
            {
                 fprintf(stderr, "WARNING: Re-generated lock key '%s' does not agree with parsed key '%s'.\n", buffer, m_pValue);
                 exit(1);
            }
        }
    }
}

void T5X_OBJECTINFO::Validate()
{
    if (  m_fAttrCount
       && NULL != m_pvai)
    {
        for (vector<T5X_ATTRINFO *>::iterator it = m_pvai->begin(); it != m_pvai->end(); ++it)
        {
            (*it)->Validate();
        }
    }
}

void T5X_ATTRINFO::Write(FILE *fp, bool fExtraEscapes) const
{
    if (m_fNumAndValue)
    {
        fprintf(fp, ">%d\n\"%s\"\n", m_iNum, EncodeString(m_pValue, fExtraEscapes));
    }
}

void T5X_GAME::Write(FILE *fp)
{
    int ver = (m_flags & V_MASK);
    bool fExtraEscapes = (2 <= ver);
    fprintf(fp, "+X%d\n", m_flags);
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
    for (vector<T5X_ATTRNAMEINFO *>::iterator it = m_vAttrNames.begin(); it != m_vAttrNames.end(); ++it)
    {
        (*it)->Write(fp, fExtraEscapes);
    } 
    for (vector<T5X_OBJECTINFO *>::iterator it = m_vObjects.begin(); it != m_vObjects.end(); ++it)
    {
        (*it)->Write(fp, (m_flags & V_ATRKEY) == 0, fExtraEscapes);
    } 

    fprintf(fp, "***END OF DUMP***\n");
}

int t5x_convert_type[] =
{
    T5X_NOTYPE,        //  0
    T5X_TYPE_ROOM,     //  1
    T5X_TYPE_THING,    //  2
    T5X_NOTYPE,        //  3
    T5X_TYPE_EXIT,     //  4
    T5X_NOTYPE,        //  5
    T5X_NOTYPE,        //  6
    T5X_NOTYPE,        //  7
    T5X_TYPE_PLAYER,   //  8
    T5X_NOTYPE,        //  9
    T5X_NOTYPE,        // 10
    T5X_NOTYPE,        // 11
    T5X_NOTYPE,        // 12
    T5X_NOTYPE,        // 13
    T5X_NOTYPE,        // 14
    T5X_NOTYPE,        // 15
    T5X_TYPE_GARBAGE,  // 16
};

NameMask t5x_convert_flag1[] =
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

NameMask t5x_convert_flag2[] =
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

NameMask t5x_convert_powers1[] =
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

NameMask t5x_convert_powers2[] =
{
    { "Builder",        0x00000001UL },
};

static struct
{
    const char *pName;
    int         iNum;
} t5x_known_attrs[] =
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
    { "CREATED",        -1 }, // rename CREATED to XCREATED QQQ
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
    { "MODIFIED",       -1 }, // rename MODIFIED to XMODIFIED QQQ
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
} t5x_locknames[] =
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

NameMask t5x_attr_flags[] =
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

char *EncodeAttrValue(int iObjOwner, int iAttrOwner, int iAttrFlags, char *pValue)
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
    sprintf(buffer, "%c%d:%d:%s", 0x01, iAttrOwner, iAttrFlags, pValue);
    return buffer;
}

struct ltstr
{
    bool operator()(const char* s1, const char* s2) const
    {
        return strcmp(s1, s2) < 0;
    }
};

void T5X_GAME::ConvertFromP6H()
{
    SetFlags(MANDFLAGS_V2 | 2);

    // Build internal attribute names.
    //
    map<const char *, int, ltstr> AttrNamesKnown;
    for (int i = 0; i < sizeof(t5x_known_attrs)/sizeof(t5x_known_attrs[0]); i++)
    {
        AttrNamesKnown[StringClone(t5x_known_attrs[i].pName)] = t5x_known_attrs[i].iNum;
    }

    // Build set of attribute names.
    //
    int iNextAttr = A_USER_START;
    map<const char *, int, ltstr> AttrNames;
    for (vector<P6H_OBJECTINFO *>::iterator itObj = g_p6hgame.m_vObjects.begin(); itObj != g_p6hgame.m_vObjects.end(); ++itObj)
    {
        if (NULL != (*itObj)->m_pvai)
        {
            for (vector<P6H_ATTRINFO *>::iterator itAttr = (*itObj)->m_pvai->begin(); itAttr != (*itObj)->m_pvai->end(); ++itAttr)
            {
                if (NULL != (*itAttr)->m_pName)
                {
                    char *pAttrName = t5x_ConvertAttributeName((*itAttr)->m_pName);
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
    for (vector<P6H_OBJECTINFO *>::iterator it = g_p6hgame.m_vObjects.begin(); it != g_p6hgame.m_vObjects.end(); ++it)
    {
        if (  !(*it)->m_fType
           || (*it)->m_iType < 0
           || 16 < (*it)->m_iType)
        {
            continue;
        }

        T5X_OBJECTINFO *poi = new T5X_OBJECTINFO;

        int iType = t5x_convert_type[(*it)->m_iType];

        poi->SetRef((*it)->m_dbRef);
        poi->SetName(StringClone((*it)->m_pName));
        if ((*it)->m_fLocation)
        {
            int iLocation = (*it)->m_dbLocation;
            if (  T5X_TYPE_EXIT == iType
               && -2 == iLocation)
            {
                poi->SetLocation(-1);
            }
            else
            {
                poi->SetLocation(iLocation);
            }
        }
        if ((*it)->m_fContents)
        {
            poi->SetContents((*it)->m_dbContents);
        }
        if ((*it)->m_fExits)
        {
            switch (iType)
            {
            case T5X_TYPE_PLAYER:
            case T5X_TYPE_THING:
                poi->SetExits(-1);
                poi->SetLink((*it)->m_dbExits);
                break;

            default:
                poi->SetExits((*it)->m_dbExits);
                poi->SetLink(-1);
                break;
            }
        }
        if ((*it)->m_fNext)
        {
            poi->SetNext((*it)->m_dbNext);
        }
        if ((*it)->m_fParent)
        {
            poi->SetParent((*it)->m_dbParent);
        }
        if ((*it)->m_fOwner)
        {
            poi->SetOwner((*it)->m_dbOwner);
        }
        if ((*it)->m_fZone)
        {
            poi->SetZone((*it)->m_dbZone);
        }
        if ((*it)->m_fPennies)
        {
            poi->SetPennies((*it)->m_iPennies);
        }

        // Flagwords
        //
        int flags1 = iType;
        int flags2 = 0;
        int flags3 = 0;
        char *pFlags = (*it)->m_pFlags;
        if (NULL != pFlags)
        {
            // First flagword
            //
            for (int i = 0; i < sizeof(t5x_convert_flag1)/sizeof(t5x_convert_flag1[0]); i++)
            {
                if (NULL != strcasestr(pFlags, t5x_convert_flag1[i].pName))
                {
                    flags1 |= t5x_convert_flag1[i].mask;
                }
            }

            // Second flagword
            //
            for (int i = 0; i < sizeof(t5x_convert_flag2)/sizeof(t5x_convert_flag2[0]); i++)
            {
                if (NULL != strcasestr(pFlags, t5x_convert_flag2[i].pName))
                {
                    flags2 |= t5x_convert_flag2[i].mask;
                }
            }
        }

        // Powers
        //
        int powers1 = 0;
        int powers2 = 0;
        char *pPowers = (*it)->m_pPowers;
        if (NULL != pPowers)
        {
            // First powerword
            //
            for (int i = 0; i < sizeof(t5x_convert_powers1)/sizeof(t5x_convert_powers1[0]); i++)
            {
                if (NULL != strcasestr(pPowers, t5x_convert_powers1[i].pName))
                {
                    powers1 |= t5x_convert_powers1[i].mask;
                }
            }

            // Second powerword
            //
            for (int i = 0; i < sizeof(t5x_convert_powers2)/sizeof(t5x_convert_powers2[0]); i++)
            {
                if (NULL != strcasestr(pPowers, t5x_convert_powers2[i].pName))
                {
                    powers2 |= t5x_convert_powers2[i].mask;
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

        if (NULL != (*it)->m_pvai)
        {
            vector<T5X_ATTRINFO *> *pvai = new vector<T5X_ATTRINFO *>;
            for (vector<P6H_ATTRINFO *>::iterator itAttr = (*it)->m_pvai->begin(); itAttr != (*it)->m_pvai->end(); ++itAttr)
            {
                if (  NULL != (*itAttr)->m_pName
                   && NULL != (*itAttr)->m_pValue)
                {
                    char *pAttrFlags = (*itAttr)->m_pFlags;
                    int iAttrFlags = 0;
                    for (int i = 0; i < sizeof(t5x_attr_flags)/sizeof(t5x_attr_flags[0]); i++)
                    {
                        if (strcasecmp(t5x_attr_flags[i].pName, pAttrFlags) == 0)
                        {
                            iAttrFlags |= t5x_attr_flags[i].mask;
                        }
                    }
                    char *pEncodedAttrValue = EncodeAttrValue(poi->m_dbOwner, (*itAttr)->m_dbOwner, iAttrFlags, (*itAttr)->m_pValue);
                    char *pAttrName = t5x_ConvertAttributeName((*itAttr)->m_pName);
                    map<const char *, int , ltstr>::iterator itFound = AttrNamesKnown.find(pAttrName);
                    if (itFound != AttrNamesKnown.end())
                    {
                        T5X_ATTRINFO *pai = new T5X_ATTRINFO;
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
                            T5X_ATTRINFO *pai = new T5X_ATTRINFO;
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

        if (NULL != (*it)->m_pvli)
        {
            for (vector<P6H_LOCKINFO *>::iterator itLock = (*it)->m_pvli->begin(); itLock != (*it)->m_pvli->end(); ++itLock)
            {
                if (NULL != (*itLock)->m_pKeyTree)
                {
                    bool fFound = false;
                    int iLock;
                    for (int i = 0; i < sizeof(t5x_locknames)/sizeof(t5x_locknames[0]); i++)
                    {
                        if (strcmp(t5x_locknames[i].pName, (*itLock)->m_pType) == 0)
                        {
                            iLock = t5x_locknames[i].iNum;
                            fFound = true;
                            break;
                        }
                    }

                    if (fFound)
                    {
                        T5X_LOCKEXP *pLock = new T5X_LOCKEXP;
                        if (pLock->ConvertFromP6H((*itLock)->m_pKeyTree))
                        {
                            char buffer[65536];
                            char *p = pLock->Write(buffer);
                            *p = '\0';

                            // Add it.
                            //
                            T5X_ATTRINFO *pai = new T5X_ATTRINFO;
                            pai->SetNumAndValue(iLock, StringClone(buffer));

                            if (NULL == poi->m_pvai)
                            {
                                vector<T5X_ATTRINFO *> *pvai = new vector<T5X_ATTRINFO *>;
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
                            fprintf(stderr, "WARNING: Could not convert '%s' lock on #%d containing '%s'.\n", (*itLock)->m_pType, (*it)->m_dbRef, (*itLock)->m_pKey);
                        }
                    }
                }
            }
        }

        AddObject(poi);

        if (dbRefMax < (*it)->m_dbRef)
        {
            dbRefMax = (*it)->m_dbRef;
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

void T5X_GAME::ResetPassword()
{
    int ver = (m_flags & V_MASK);
    bool fSHA1 = (2 <= ver);
    for (vector<T5X_OBJECTINFO *>::iterator itObj = m_vObjects.begin(); itObj != m_vObjects.end(); ++itObj)
    {
        if (1 == (*itObj)->m_dbRef)
        {
            bool fFound = false;
            if (NULL != (*itObj)->m_pvai)
            {
                for (vector<T5X_ATTRINFO *>::iterator itAttr = (*itObj)->m_pvai->begin(); itAttr != (*itObj)->m_pvai->end(); ++itAttr)
                {
                    if (5 == (*itAttr)->m_iNum)
                    {
                        // Change it to 'potrzebie'.
                        //
                        free((*itAttr)->m_pValue);
                        if (fSHA1)
                        {
                            (*itAttr)->m_pValue = StringClone("$SHA1$X0PG0reTn66s$FxO7KKs/CJ+an2rDWgGO4zpo1co=");
                        }
                        else
                        {
                            (*itAttr)->m_pValue = StringClone("XXNHc95o0HhAc");
                        }

                        fFound = true;
                    }
                }
            }

            if (!fFound)
            {
                // Add it.
                //
                T5X_ATTRINFO *pai = new T5X_ATTRINFO;
                if (fSHA1)
                {
                    pai->SetNumAndValue(5, StringClone("$SHA1$X0PG0reTn66s$FxO7KKs/CJ+an2rDWgGO4zpo1co="));
                }
                else
                {
                    pai->SetNumAndValue(5, StringClone("XXNHc95o0HhAc"));
                }

                if (NULL == (*itObj)->m_pvai)
                {
                    vector<T5X_ATTRINFO *> *pvai = new vector<T5X_ATTRINFO *>;
                    pvai->push_back(pai);
                    (*itObj)->SetAttrs(pvai->size(), pvai);
                }
                else
                {
                    (*itObj)->m_pvai->push_back(pai);
                    (*itObj)->m_fAttrCount = true;
                    (*itObj)->m_nAttrCount = (*itObj)->m_pvai->size();
                }
            }
        }
    } 
}
