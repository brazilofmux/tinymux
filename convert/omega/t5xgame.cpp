#include "omega.h"
#include "t5xgame.h"

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
    case le_is:
        fprintf(fp, "(=");
        m_le[0]->Write(fp);
        fprintf(fp, ")");
        break;

    case le_carry:
        fprintf(fp, "(+");
        m_le[0]->Write(fp);
        fprintf(fp, ")");
        break;

    case le_indirect:
        fprintf(fp, "(@");
        m_le[0]->Write(fp);
        fprintf(fp, ")");
        break;

    case le_owner:
        fprintf(fp, "($");
        m_le[0]->Write(fp);
        fprintf(fp, ")");
        break;

    case le_and:
        fprintf(fp, "(");
        m_le[0]->Write(fp);
        fprintf(fp, "&");
        m_le[1]->Write(fp);
        fprintf(fp, ")");
        break;

    case le_or:
        fprintf(fp, "(");
        m_le[0]->Write(fp);
        fprintf(fp, "|");
        m_le[1]->Write(fp);
        fprintf(fp, ")");
        break;

    case le_not:
        fprintf(fp, "(!");
        m_le[0]->Write(fp);
        fprintf(fp, ")");
        break;

    case le_ref:
        fprintf(fp, "%d\n", m_dbRef);
        break;

    case le_attr1:
        fprintf(fp, "%s:%s\n", m_p[0], m_p[1]);
        break;

    case le_attr2:
        fprintf(fp, "%d:%s\n", m_dbRef, m_p[1]);
        break;

    case le_eval1:
        fprintf(fp, "%s/%s\n", m_p[0], m_p[1]);
        break;

    case le_eval2:
        fprintf(fp, "%d/%s\n", m_dbRef, m_p[1]);
        break;
    }
}
 
void T5X_ATTRNAMEINFO::SetNumAndName(int iNum, char *pName)
{
    m_fNumAndName = true;
    m_iNum = iNum;
    free(m_pName);
    m_pName = pName;
}

static char *EncodeString(const char *str)
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
        else if ('\r' == *str)
        {
            *p++ = '\\';
            *p++ = 'r';
            str++;
        }
        else if ('\n' == *str)
        {
            *p++ = '\\';
            *p++ = 'n';
            str++;
        }
        else if ('\t' == *str)
        {
            *p++ = '\\';
            *p++ = 't';
            str++;
        }
        else if ('\x1B' == *str)
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

void T5X_ATTRNAMEINFO::Write(FILE *fp)
{
    if (m_fNumAndName)
    {
        fprintf(fp, "+A%d\n\"%s\"\n", m_iNum, EncodeString(m_pName));
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

    int ver = m_flags & V_MASK;
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

void T5X_OBJECTINFO::Write(FILE *fp, bool bWriteLock)
{
    fprintf(fp, "!%d\n", m_dbRef);
    if (NULL != m_pName)
    {
        fprintf(fp, "\"%s\"\n", EncodeString(m_pName));
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
            (*it)->Write(fp);
        }
    }
    fprintf(fp, "<\n");
}

void T5X_OBJECTINFO::Validate()
{
}

void T5X_ATTRINFO::Write(FILE *fp) const
{
    if (m_fNumAndValue)
    {
        fprintf(fp, ">%d\n\"%s\"\n", m_iNum, EncodeString(m_pValue));
    }
}

void T5X_GAME::Write(FILE *fp)
{
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
        (*it)->Write(fp);
    } 
    for (vector<T5X_OBJECTINFO *>::iterator it = m_vObjects.begin(); it != m_vObjects.end(); ++it)
    {
        (*it)->Write(fp, (m_flags & V_ATRKEY) == 0);
    } 

    fprintf(fp, "***END OF DUMP***\n");
}

