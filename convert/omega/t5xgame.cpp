#include "omega.h"
#include "t5xgame.h"

#define V_MASK      0x000000ff  /* Database version */
#define V_ZONE      0x00000100  /* ZONE/DOMAIN field */
#define V_LINK      0x00000200  /* LINK field (exits from objs) */
#define V_DATABASE  0x00000400  /* attrs in a separate database */
#define V_ATRNAME   0x00000800  /* NAME is an attr, not in the hdr */
#define V_ATRKEY    0x00001000  /* KEY is an attr, not in the hdr */
#define V_PARENT    0x00002000  /* db has the PARENT field */
#define V_ATRMONEY  0x00008000  /* Money is kept in an attribute */
#define V_XFLAGS    0x00010000  /* An extra word of flags */
#define V_POWERS    0x00020000  /* Powers? */
#define V_3FLAGS    0x00040000  /* Adding a 3rd flag word */
#define V_QUOTED    0x00080000  /* Quoted strings, ala PennMUSH */

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
 
void T5X_ATTRNAMEINFO::SetNumAndName(int iNum, char *pName)
{
    m_fNumAndName = true;
    m_iNum = iNum;
    if (NULL != m_pName)
    {
        free(m_pName);
    }
    m_pName = pName;
}

static char *EncodeString(const char *str)
{
    static char buf[10000];
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

void T5X_ATTRNAMEINFO::Write()
{
    if (m_fNumAndName)
    {
        printf("+A%d\n\"%s\"\n", m_iNum, EncodeString(m_pName));
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

void T5X_OBJECTINFO::Merge(T5X_OBJECTINFO *poi)
{
    if (NULL != poi->m_pName && NULL == m_pName)
    {
        m_pName = poi->m_pName;
        poi->m_pName = NULL;;
    }
    if (poi->m_fLocation && !m_fLocation)
    {
        m_fLocation = true;
        m_dbLocation = poi->m_dbLocation;
    }
    if (poi->m_fContents && !m_fContents)
    {
        m_fContents = true;
        m_dbContents = poi->m_dbContents;
    }
    if (poi->m_fExits && !m_fExits)
    {
        m_fExits = true;
        m_dbExits = poi->m_dbExits;
    }
    if (poi->m_fNext && !m_fNext)
    {
        m_fNext = true;
        m_dbNext = poi->m_dbNext;
    }
    if (poi->m_fParent && !m_fParent)
    {
        m_fParent = true;
        m_dbParent = poi->m_dbParent;
    }
    if (poi->m_fOwner && !m_fOwner)
    {
        m_fOwner = true;
        m_dbOwner = poi->m_dbOwner;
    }
    if (poi->m_fZone && !m_fZone)
    {
        m_fZone = true;
        m_dbZone = poi->m_dbZone;
    }
    if (poi->m_fPennies && !m_fPennies)
    {
        m_fPennies = true;
        m_iPennies = poi->m_iPennies;
    }
    if (poi->m_fFlags1 && !m_fFlags1)
    {
        m_fFlags1 = true;
        m_iFlags1 = poi->m_iFlags1;
    }
    if (poi->m_fFlags2 && !m_fFlags2)
    {
        m_fFlags2 = true;
        m_iFlags2 = poi->m_iFlags2;
    }
    if (poi->m_fFlags3 && !m_fFlags3)
    {
        m_fFlags3 = true;
        m_iFlags3 = poi->m_iFlags3;
    }
    if (poi->m_fPowers1 && !m_fPowers1)
    {
        m_fPowers1 = true;
        m_iPowers1 = poi->m_iPowers1;
    }
    if (poi->m_fPowers2 && !m_fPowers2)
    {
        m_fPowers2 = true;
        m_iPowers2 = poi->m_iPowers2;
    }
    if (NULL != poi->m_pvai && NULL == m_pvai)
    {
        m_pvai = poi->m_pvai;
        poi->m_pvai = NULL;
    }
    if (poi->m_fAttrCount && !m_fAttrCount)
    {
        m_fAttrCount = true;
        m_nAttrCount = poi->m_nAttrCount;
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

void T5X_ATTRINFO::Merge(T5X_ATTRINFO *pai)
{
    if (pai->m_fNumAndValue && !m_fNumAndValue)
    {
        m_fNumAndValue = true;
        m_iNum = pai->m_iNum;
        m_pValue = pai->m_pValue;
        pai->m_pValue = NULL;;
    }
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
    fprintf(stderr, "Flags: ", flags);

    int ver = m_flags & V_MASK;
    fprintf(stderr, "Version: %d\n", ver);
    flags &= ~V_MASK;
    for (int i = 0; i < T5X_NUM_GAMEFLAGNAMES; i++)
    {
        if (t5x_gameflagnames[i].mask & flags)
        {
            fprintf(stderr, "%s ", t5x_gameflagnames[i].pName);
            flags &= ~t5x_gameflagnames[i].mask;
        }
    }
    fprintf(stderr, "\n");

    if (0 != flags)
    {
        fprintf(stderr, "Unknown flags: 0x%x\n", flags);
        exit(1);
    }
}

void T5X_GAME::ValidateSavedTime()
{
}

void T5X_GAME::Validate()
{
    ValidateFlags();
}

void T5X_GAME::WriteObject(const T5X_OBJECTINFO &oi)
{
    printf("!%d\n", oi.m_dbRef);
    if (NULL != oi.m_pName)
    {
        printf("\"%s\"\n", EncodeString(oi.m_pName));
    }
    if (oi.m_fLocation)
    {
        printf("%d\n", oi.m_dbLocation);
    }
    if (oi.m_fZone)
    {
        printf("%d\n", oi.m_dbZone);
    }
    if (oi.m_fContents)
    {
        printf("%d\n", oi.m_dbContents);
    }
    if (oi.m_fExits)
    {
        printf("%d\n", oi.m_dbExits);
    }
    if (oi.m_fLink)
    {
        printf("%d\n", oi.m_dbLink);
    }
    if (oi.m_fNext)
    {
        printf("%d\n", oi.m_dbNext);
    }
    if (oi.m_fOwner)
    {
        printf("%d\n", oi.m_dbOwner);
    }
    if (oi.m_fParent)
    {
        printf("%d\n", oi.m_dbParent);
    }
    if (oi.m_fPennies)
    {
        printf("%d\n", oi.m_iPennies);
    }
    if (oi.m_fFlags1)
    {
        printf("%d\n", oi.m_iFlags1);
    }
    if (oi.m_fFlags2)
    {
        printf("%d\n", oi.m_iFlags2);
    }
    if (oi.m_fFlags3)
    {
        printf("%d\n", oi.m_iFlags3);
    }
    if (oi.m_fPowers1)
    {
        printf("%d\n", oi.m_iPowers1);
    }
    if (oi.m_fPowers2)
    {
        printf("%d\n", oi.m_iPowers2);
    }
    if (  oi.m_fAttrCount
       && NULL != oi.m_pvai)
    {
        for (vector<T5X_ATTRINFO *>::iterator it = oi.m_pvai->begin(); it != oi.m_pvai->end(); ++it)
        {
            oi.WriteAttr(**it);
        }
    }
    printf("<\n");
}

void T5X_OBJECTINFO::WriteAttr(const T5X_ATTRINFO &ai) const
{
    if (ai.m_fNumAndValue)
    {
        printf(">%d\n\"%s\"\n", ai.m_iNum, EncodeString(ai.m_pValue));
    }
}

void T5X_GAME::Write(FILE *fp)
{
    printf("+X%d\n", m_flags);
    if (m_fObjects)
    {
        printf("+S%d\n", m_nObjects);
    }
    if (m_fAttrs)
    {
        printf("+N%d\n", m_nAttrs);
    }
    if (m_fRecordPlayers)
    {
        printf("-R%d\n", m_nRecordPlayers);
    }
    if (m_fFlags)
    {
        printf("+FLAGS LIST\nflagcount %d\n", m_nFlags);
    }
    for (vector<T5X_ATTRNAMEINFO *>::iterator it = m_vAttrNames.begin(); it != m_vAttrNames.end(); ++it)
    {
        (*it)->Write();
    } 
    for (vector<T5X_OBJECTINFO *>::iterator it = m_vObjects.begin(); it != m_vObjects.end(); ++it)
    {
        WriteObject(**it);
    } 

    printf("***END OF DUMP***\n");
}

