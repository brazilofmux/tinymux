#include "omega.h"
#include "p6hgame.h"
#include "t5xgame.h"

#define DBF_NO_CHAT_SYSTEM      0x00000001
#define DBF_WARNINGS            0x00000002
#define DBF_CREATION_TIMES      0x00000004
#define DBF_NO_POWERS           0x00000008
#define DBF_NEW_LOCKS           0x00000010
#define DBF_NEW_STRINGS         0x00000020
#define DBF_TYPE_GARBAGE        0x00000040
#define DBF_SPLIT_IMMORTAL      0x00000080
#define DBF_NO_TEMPLE           0x00000100
#define DBF_LESS_GARBAGE        0x00000200
#define DBF_AF_VISUAL           0x00000400
#define DBF_VALUE_IS_COST       0x00000800
#define DBF_LINK_ANYWHERE       0x00001000
#define DBF_NO_STARTUP_FLAG     0x00002000
#define DBF_PANIC               0x00004000
#define DBF_AF_NODUMP           0x00008000
#define DBF_SPIFFY_LOCKS        0x00010000
#define DBF_NEW_FLAGS           0x00020000
#define DBF_NEW_POWERS          0x00040000
#define DBF_POWERS_LOGGED       0x00080000
#define DBF_LABELS              0x00100000
#define DBF_SPIFFY_AF_ANSI      0x00200000
#define DBF_HEAR_CONNECT        0x00400000
#define DBF_NEW_VERSION         0x00800000

// DBF_SPIFFY_LOCKS was introduced with PennMUSH 1.7.5p0 (11/14/2001) and
// replaced DBF_NEW_LOCKS.
//
// DBF_NEW_FLAGS, DBF_NEW_POWERS, DBF_POWERS_LOGGED, and DBF_LABELS were
// introduced with PennMUSH 1.7.7p40 (12/01/2004).
//
// DBF_SPIFFY_AF_ANSI was introduced with PennMUSH 1.8.3p0 (01/27/2007).
//
// DBF_HEAR_CONNECT was introduced with PennMUSH 1.8.3p10 (08/24/2009).
//

typedef struct _p6h_gameflaginfo
{
    int         mask;
    const char *pName;
} p6h_gameflaginfo;

p6h_gameflaginfo p6h_gameflagnames[] =
{
    { DBF_NO_CHAT_SYSTEM,   "NO_CHAT_SYSTEM"  },
    { DBF_WARNINGS,         "WARNINGS"        },
    { DBF_CREATION_TIMES,   "CREATION_TIMES"  },
    { DBF_NO_POWERS,        "NO_POWERS"       },
    { DBF_NEW_LOCKS,        "NEW_LOCKS"       },
    { DBF_NEW_STRINGS,      "NEW_STRINGS"     },
    { DBF_TYPE_GARBAGE,     "TYPE_GARBAGE"    },
    { DBF_SPLIT_IMMORTAL,   "SPLIT_IMMORTAL"  },
    { DBF_NO_TEMPLE,        "NO_TEMPLE"       },
    { DBF_LESS_GARBAGE,     "LESS_GARBAGE"    },
    { DBF_AF_VISUAL,        "AF_VISUAL"       },
    { DBF_VALUE_IS_COST,    "VALUE_IS_COST"   },
    { DBF_LINK_ANYWHERE,    "LINK_ANYWHERE"   },
    { DBF_NO_STARTUP_FLAG,  "NO_STARTUP_FLAG" },
    { DBF_PANIC,            "PANIC"           },
    { DBF_AF_NODUMP,        "AF_NODUMP"       },
    { DBF_SPIFFY_LOCKS,     "SPIFFY_LOCKS"    },
    { DBF_NEW_FLAGS,        "NEW_FLAGS"       },
    { DBF_NEW_POWERS,       "NEW_POWERS"      },
    { DBF_POWERS_LOGGED,    "POWERS_LOGGED"   },
    { DBF_LABELS,           "LABELS"          },
    { DBF_SPIFFY_AF_ANSI,   "SPIFFY_AF_ANSI"  },
    { DBF_HEAR_CONNECT,     "HEAR_CONNECT"    },
    { DBF_NEW_VERSION,      "NEW_VERSION"     },
};
#define P6H_NUM_GAMEFLAGNAMES (sizeof(p6h_gameflagnames)/sizeof(p6h_gameflagnames[0]))

P6H_GAME g_p6hgame;
P6H_LOCKEXP *p6hl_ParseKey(char *pKey);

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
        }
        *p++ = *str++;
    }
    *p = '\0';
    return buf;
}

char *P6H_LOCKEXP::Write(char *p)
{
    switch (m_op)
    {
    case P6H_LOCKEXP::le_is:
        *p++ = '=';
        p = m_le[0]->Write(p);
        break;

    case P6H_LOCKEXP::le_carry:
        *p++ = '+';
        p = m_le[0]->Write(p);
        break;

    case P6H_LOCKEXP::le_indirect:
        *p++ = '@';
        p = m_le[0]->Write(p);
        break;

    case P6H_LOCKEXP::le_indirect2:
        *p++ = '@';
        p = m_le[0]->Write(p);
        *p++ = '/';
        p = m_le[1]->Write(p);
        break;

    case P6H_LOCKEXP::le_owner:
        *p++ = '$';
        p = m_le[0]->Write(p);
        break;

    case P6H_LOCKEXP::le_or:
        p = m_le[0]->Write(p);
        *p++ = '|';
        p = m_le[1]->Write(p);
        break;

    case P6H_LOCKEXP::le_not:
        *p++ = '!';
        p = m_le[0]->Write(p);
        break;

    case P6H_LOCKEXP::le_attr:
        p = m_le[0]->Write(p);
        *p++ = ':';
        p = m_le[1]->Write(p);
        break;

    case P6H_LOCKEXP::le_eval:
        p = m_le[0]->Write(p);
        *p++ = '/';
        p = m_le[1]->Write(p);
        break;

    case P6H_LOCKEXP::le_and:
        p = m_le[0]->Write(p);
        *p++ = '&';
        p = m_le[1]->Write(p);
        break;

    case P6H_LOCKEXP::le_ref:
        sprintf(p, "#%d", m_dbRef);
        p += strlen(p);
        break;

    case P6H_LOCKEXP::le_text:
        sprintf(p, "%s", m_p[0]);
        p += strlen(p);
        break;

    case P6H_LOCKEXP::le_class:
        sprintf(p, "%s", m_p[0]);
        p += strlen(p);
        *p++ = '^';
        p = m_le[1]->Write(p);
        break;

    case P6H_LOCKEXP::le_true:
        sprintf(p, "#true");
        p += strlen(p);
        break;

    case P6H_LOCKEXP::le_false:
        sprintf(p, "#false");
        p += strlen(p);
        break;

    default:
        fprintf(stderr, "%d not recognized.\n", m_op);
        break;
    }
    return p;
}

bool P6H_LOCKEXP::ConvertFromT5X(T5X_LOCKEXP *p)
{
    switch (p->m_op)
    {
    case T5X_LOCKEXP::le_is:
        m_op = P6H_LOCKEXP::le_is;
        m_le[0] = new P6H_LOCKEXP;
        if (!m_le[0]->ConvertFromT5X(p->m_le[0]))
        {
            delete m_le[0];
            m_le[0] = NULL;
            return false;
        }
        break;

    case T5X_LOCKEXP::le_carry:
        m_op = P6H_LOCKEXP::le_carry;
        m_le[0] = new P6H_LOCKEXP;
        if (!m_le[0]->ConvertFromT5X(p->m_le[0]))
        {
            delete m_le[0];
            m_le[0] = NULL;
            return false;
        }
        break;

    case T5X_LOCKEXP::le_indirect:
        m_op = P6H_LOCKEXP::le_indirect;
        m_le[0] = new P6H_LOCKEXP;
        if (!m_le[0]->ConvertFromT5X(p->m_le[0]))
        {
            delete m_le[0];
            m_le[0] = NULL;
            return false;
        }
        break;

    case T5X_LOCKEXP::le_owner:
        m_op = P6H_LOCKEXP::le_owner;
        m_le[0] = new P6H_LOCKEXP;
        if (!m_le[0]->ConvertFromT5X(p->m_le[0]))
        {
            delete m_le[0];
            m_le[0] = NULL;
            return false;
        }
        break;

    case T5X_LOCKEXP::le_or:
        m_op = P6H_LOCKEXP::le_or;
        m_le[0] = new P6H_LOCKEXP;
        m_le[1] = new P6H_LOCKEXP;
        if (  !m_le[0]->ConvertFromT5X(p->m_le[0])
           || !m_le[1]->ConvertFromT5X(p->m_le[1]))
        {
            delete m_le[0];
            delete m_le[1];
            m_le[0] = m_le[1] = NULL;
            return false;
        }
        break;

    case T5X_LOCKEXP::le_not:
        m_op = P6H_LOCKEXP::le_not;
        m_le[0] = new P6H_LOCKEXP;
        if (!m_le[0]->ConvertFromT5X(p->m_le[0]))
        {
            delete m_le[0];
            m_le[0] = NULL;
            return false;
        }
        break;

    case T5X_LOCKEXP::le_attr:
        m_op = P6H_LOCKEXP::le_attr;
        m_le[0] = new P6H_LOCKEXP;
        m_le[1] = new P6H_LOCKEXP;
        if (  !m_le[0]->ConvertFromT5X(p->m_le[0])
           || !m_le[1]->ConvertFromT5X(p->m_le[1]))
        {
            delete m_le[0];
            delete m_le[1];
            m_le[0] = m_le[1] = NULL;
            return false;
        }
        break;

    case T5X_LOCKEXP::le_eval:
        m_op = P6H_LOCKEXP::le_eval;
        m_le[0] = new P6H_LOCKEXP;
        m_le[1] = new P6H_LOCKEXP;
        if (  !m_le[0]->ConvertFromT5X(p->m_le[0])
           || !m_le[1]->ConvertFromT5X(p->m_le[1]))
        {
            delete m_le[0];
            delete m_le[1];
            m_le[0] = m_le[1] = NULL;
            return false;
        }
        break;

    case T5X_LOCKEXP::le_and:
        m_op = P6H_LOCKEXP::le_and;
        m_le[0] = new P6H_LOCKEXP;
        m_le[1] = new P6H_LOCKEXP;
        if (  !m_le[0]->ConvertFromT5X(p->m_le[0])
           || !m_le[1]->ConvertFromT5X(p->m_le[1]))
        {
            delete m_le[0];
            delete m_le[1];
            m_le[0] = m_le[1] = NULL;
            return false;
        }
        break;

    case T5X_LOCKEXP::le_ref:
        m_op = P6H_LOCKEXP::le_ref;
        m_dbRef = p->m_dbRef;
        break;

    case T5X_LOCKEXP::le_text:
        m_op = P6H_LOCKEXP::le_text;
        m_p[0] = StringClone(p->m_p[0]);
        break;

    default:
        fprintf(stderr, "%d not recognized.\n", m_op);
        break;
    }
    return true;
}

void P6H_FLAGINFO::SetName(char *p)
{
    if (NULL != m_pName)
    {
        free(m_pName);
    }
    m_pName = p;
}

void P6H_FLAGINFO::SetLetter(char *p)
{
    if (NULL != m_pLetter)
    {
        free(m_pLetter);
    }
    m_pLetter = p;
}

void P6H_FLAGINFO::SetType(char *p)
{
    if (NULL != m_pType)
    {
        free(m_pType);
    }
    m_pType = p;
}

void P6H_FLAGINFO::SetPerms(char *p)
{
    if (NULL != m_pPerms)
    {
        free(m_pPerms);
    }
    m_pPerms = p;
}

void P6H_FLAGINFO::SetNegatePerms(char *p)
{
    if (NULL != m_pNegatePerms)
    {
        free(m_pNegatePerms);
    }
    m_pNegatePerms = p;
}

void P6H_FLAGINFO::Merge(P6H_FLAGINFO *pfi)
{
    if (NULL != pfi->m_pName && NULL == m_pName)
    {
        m_pName = pfi->m_pName;
        pfi->m_pName = NULL;
    }
    if (NULL != pfi->m_pLetter && NULL == m_pLetter)
    {
        m_pLetter = pfi->m_pLetter;
        pfi->m_pLetter = NULL;
    }
    if (NULL != pfi->m_pType && NULL == m_pType)
    {
        m_pType = pfi->m_pType;
        pfi->m_pType = NULL;
    }
    if (NULL != pfi->m_pPerms && NULL == m_pPerms)
    {
        m_pPerms = pfi->m_pPerms;
        pfi->m_pPerms = NULL;
    }
    if (NULL != pfi->m_pNegatePerms && NULL == m_pNegatePerms)
    {
        m_pNegatePerms = pfi->m_pNegatePerms;
        pfi->m_pNegatePerms = NULL;
    }
}

void P6H_FLAGALIASINFO::SetName(char *p)
{
    if (NULL != m_pName)
    {
        free(m_pName);
    }
    m_pName = p;
}

void P6H_FLAGALIASINFO::SetAlias(char *p)
{
    if (NULL != m_pAlias)
    {
        free(m_pAlias);
    }
    m_pAlias = p;
}

void P6H_FLAGALIASINFO::Merge(P6H_FLAGALIASINFO *pfai)
{
    if (NULL != pfai->m_pName && NULL == m_pName)
    {
        m_pName = pfai->m_pName;
        pfai->m_pName = NULL;
    }
    if (NULL != pfai->m_pAlias && NULL == m_pAlias)
    {
        m_pAlias = pfai->m_pAlias;
        pfai->m_pAlias = NULL;
    }
}

void P6H_ATTRNAMEINFO::SetName(char *p)
{
    if (NULL != m_pName)
    {
        free(m_pName);
    }
    m_pName = p;
}

void P6H_ATTRNAMEINFO::SetFlags(char *p)
{
    if (NULL != m_pFlags)
    {
        free(m_pFlags);
    }
    m_pFlags = p;
}

void P6H_ATTRNAMEINFO::SetData(char *p)
{
    if (NULL != m_pData)
    {
        free(m_pData);
    }
    m_pData = p;
}

void P6H_ATTRNAMEINFO::Merge(P6H_ATTRNAMEINFO *pani)
{
    if (NULL != pani->m_pName && NULL == m_pName)
    {
        m_pName = pani->m_pName;
        pani->m_pName = NULL;
    }
    if (NULL != pani->m_pFlags && NULL == m_pFlags)
    {
        m_pFlags = pani->m_pFlags;
        pani->m_pFlags = NULL;
    }
    if (pani->m_fCreator && !m_fCreator)
    {
        m_fCreator = true;
        m_dbCreator = pani->m_dbCreator;
    }
    if (NULL != pani->m_pData && NULL == m_pData)
    {
        m_pData = pani->m_pData;
        pani->m_pData = NULL;
    }
}

void P6H_ATTRNAMEINFO::Write(FILE *fp)
{
    if (NULL != m_pName)
    {
        fprintf(fp, " name \"%s\"\n", EncodeString(m_pName));
        if (NULL != m_pFlags)
        {
            fprintf(fp, "  flags \"%s\"\n", EncodeString(m_pFlags));
        }
        if (m_fCreator)
        {
            fprintf(fp, "  creator #%d\n", m_dbCreator);
        }
        if (NULL != m_pData)
        {
            fprintf(fp, "  data \"%s\"\n", EncodeString(m_pData));
        }
    }
}

void P6H_ATTRALIASINFO::SetName(char *p)
{
    if (NULL != m_pName)
    {
        free(m_pName);
    }
    m_pName = p;
}

void P6H_ATTRALIASINFO::SetAlias(char *p)
{
    if (NULL != m_pAlias)
    {
        free(m_pAlias);
    }
    m_pAlias = p;
}

void P6H_ATTRALIASINFO::Merge(P6H_ATTRALIASINFO *paai)
{
    if (NULL != paai->m_pName && NULL == m_pName)
    {
        m_pName = paai->m_pName;
        paai->m_pName = NULL;
    }
    if (NULL != paai->m_pAlias && NULL == m_pAlias)
    {
        m_pAlias = paai->m_pAlias;
        paai->m_pAlias = NULL;
    }
}

void P6H_ATTRALIASINFO::Write(FILE *fp)
{
    if (NULL != m_pName)
    {
        fprintf(fp, " name \"%s\"\n", EncodeString(m_pName));
        if (NULL != m_pAlias)
        {
            fprintf(fp, "  alias \"%s\"\n", EncodeString(m_pAlias));
        }
    }
}

void P6H_OBJECTINFO::SetName(char *pName)
{
    if (NULL != m_pName)
    {
        free(m_pName);
    }
    m_pName = pName;
}

void P6H_OBJECTINFO::SetLocks(int nLocks, vector<P6H_LOCKINFO *> *pvli)
{
    if (  (  NULL == pvli
          && 0 != nLocks)
       || (  NULL != pvli
          && nLocks != pvli->size()))
    {
        fprintf(stderr, "WARNING: lock count disagreement.\n");
    }

    m_fLockCount = true;
    m_nLockCount = nLocks;
    if (NULL != m_pvli)
    {
        delete m_pvli;
    }
    m_pvli = pvli;
}

void P6H_OBJECTINFO::SetFlags(char *pFlags)
{
    if (NULL != m_pFlags)
    {
        free(m_pFlags);
    }
    m_pFlags = pFlags;
}

void P6H_OBJECTINFO::SetPowers(char *pPowers)
{
    if (NULL != m_pPowers)
    {
        free(m_pPowers);
    }
    m_pPowers = pPowers;
}

void P6H_OBJECTINFO::SetWarnings(char *pWarnings)
{
    if (NULL != m_pWarnings)
    {
        free(m_pWarnings);
    }
    m_pWarnings = pWarnings;
}

void P6H_OBJECTINFO::SetAttrs(int nAttrs, vector<P6H_ATTRINFO *> *pvai)
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

void P6H_OBJECTINFO::Merge(P6H_OBJECTINFO *poi)
{
    if (NULL != poi->m_pName && NULL == m_pName)
    {
        m_pName = poi->m_pName;
        poi->m_pName = NULL;
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
    if (NULL != poi->m_pvli && NULL == m_pvli)
    {
        m_pvli = poi->m_pvli;
        poi->m_pvli = NULL;
    }
    if (poi->m_fLockCount && !m_fLockCount)
    {
        m_fLockCount = true;
        m_nLockCount = poi->m_nLockCount;
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
    if (poi->m_fType && !m_fType)
    {
        m_fType = true;
        m_iType = poi->m_iType;
    }
    if (poi->m_fCreated && !m_fCreated)
    {
        m_fCreated = true;
        m_iCreated = poi->m_iCreated;
    }
    if (poi->m_fModified && !m_fModified)
    {
        m_fModified = true;
        m_iModified = poi->m_iModified;
    }
    if (NULL != poi->m_pFlags && NULL == m_pFlags)
    {
        m_pFlags = poi->m_pFlags;
        poi->m_pFlags = NULL;
    }
    if (NULL != poi->m_pPowers && NULL == m_pPowers)
    {
        m_pPowers = poi->m_pPowers;
        poi->m_pPowers = NULL;
    }
    if (NULL != poi->m_pWarnings && NULL == m_pWarnings)
    {
        m_pWarnings = poi->m_pWarnings;
        poi->m_pWarnings = NULL;
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

void P6H_LOCKINFO::SetType(char *pType)
{
    if (NULL != m_pType)
    {
        free(m_pType);
    }
    m_pType = pType;
}

void P6H_LOCKINFO::SetFlags(char *pFlags)
{
    if (NULL != m_pFlags)
    {
        free(m_pFlags);
    }
    m_pFlags = pFlags;
}

void P6H_LOCKINFO::SetKey(char *pKey)
{
    if (NULL != m_pKey)
    {
        free(m_pKey);
    }
    m_pKey = pKey;

    if (NULL != m_pKey)
    {
        delete m_pKeyTree;
        m_pKeyTree = p6hl_ParseKey(m_pKey);
        if (NULL == m_pKeyTree)
        {
            fprintf(stderr, "WARNING: Lock key '%s' is not valid.\n", m_pKey);
        }
    }
}

void P6H_LOCKINFO::Merge(P6H_LOCKINFO *pli)
{
    if (NULL != pli->m_pType && NULL == m_pType)
    {
        m_pType = pli->m_pType;
        pli->m_pType = NULL;
    }
    if (pli->m_fCreator && !m_fCreator)
    {
        m_fCreator = true;
        m_dbCreator = pli->m_dbCreator;
    }
    if (NULL != pli->m_pFlags && NULL == m_pFlags)
    {
        m_pFlags = pli->m_pFlags;
        pli->m_pFlags = NULL;
    }
    if (pli->m_fDerefs && !m_fDerefs)
    {
        m_fDerefs = true;
        m_iDerefs = pli->m_iDerefs;
    }
    if (NULL != pli->m_pKey && NULL == m_pKey)
    {
        m_pKey = pli->m_pKey;
        pli->m_pKey = NULL;
    }
    if (pli->m_fFlags && !m_fFlags)
    {
        m_fFlags = true;
        m_iFlags = pli->m_iFlags;
    }
}

void P6H_ATTRINFO::SetName(char *pName)
{
    if (NULL != m_pName)
    {
        free(m_pName);
    }
    m_pName = pName;
}

void P6H_ATTRINFO::SetFlags(char *pFlags)
{
    if (NULL != m_pFlags)
    {
        free(m_pFlags);
    }
    m_pFlags = pFlags;
}

void P6H_ATTRINFO::SetValue(char *pValue)
{
    if (NULL != m_pValue)
    {
        free(m_pValue);
    }
    m_pValue = pValue;
}

void P6H_ATTRINFO::Merge(P6H_ATTRINFO *pai)
{
    if (NULL != pai->m_pName && NULL == m_pName)
    {
        m_pName = pai->m_pName;
        pai->m_pName = NULL;
    }
    if (pai->m_fOwner && !m_fOwner)
    {
        m_fOwner = true;
        m_dbOwner = pai->m_dbOwner;
    }
    if (NULL != pai->m_pFlags && NULL == m_pFlags)
    {
        m_pFlags = pai->m_pFlags;
        pai->m_pFlags = NULL;
    }
    if (pai->m_fDerefs && !m_fDerefs)
    {
        m_fDerefs = true;
        m_iDerefs = pai->m_iDerefs;
    }
    if (NULL != pai->m_pValue && NULL == m_pValue)
    {
        m_pValue = pai->m_pValue;
        pai->m_pValue = NULL;
    }
    if (pai->m_fFlags && !m_fFlags)
    {
        m_fFlags = true;
        m_iFlags = pai->m_iFlags;
    }
}

void P6H_GAME::SetSavedTime(char *p)
{
    if (NULL != m_pSavedTime)
    {
        free(m_pSavedTime);
        m_pSavedTime = NULL;
    }
    m_pSavedTime = p;
}

void P6H_GAME::AddObject(P6H_OBJECTINFO *poi)
{
    m_mObjects[poi->m_dbRef] = poi;
}

bool P6H_GAME::HasLabels() const
{
    bool fLabels = ((g_p6hgame.m_flags & DBF_LABELS) == DBF_LABELS);
    return fLabels;
}

void P6H_GAME::ValidateFlags() const
{
    bool f177p40 = HasLabels();
    if (f177p40)
    {
        fprintf(stderr, "INFO: Flatfile produced by 1.7.7p40 or later.\n");
    }
    else
    {
        fprintf(stderr, "INFO: Flatfile predates PennMUSH 1.7.7p40\n");
    }

    int flags = m_flags;
    int tflags = flags;
    fprintf(stderr, "INFO: Flatfile flags are ");
    for (int i = 0; i < P6H_NUM_GAMEFLAGNAMES; i++)
    {
        if (p6h_gameflagnames[i].mask & tflags)
        {
            fprintf(stderr, "%s ", p6h_gameflagnames[i].pName);
            tflags &= ~p6h_gameflagnames[i].mask;
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
    const int iMandatory = DBF_NO_CHAT_SYSTEM
                         | DBF_CREATION_TIMES
                         | DBF_NEW_STRINGS
                         | DBF_TYPE_GARBAGE
                         | DBF_LESS_GARBAGE
                         | DBF_NO_TEMPLE
                         | DBF_SPLIT_IMMORTAL
                         | DBF_SPIFFY_LOCKS;

    const int iMand177p40 = DBF_NEW_FLAGS
                          | DBF_NEW_POWERS
                          | DBF_LABELS;
    if ((flags & iMandatory) != iMandatory)
    {
        fprintf(stderr, "WARNING: Not all mandatory flags are present.\n");
    }

    if (f177p40)
    {
        if ((flags & iMand177p40) != iMand177p40)
        {
            fprintf(stderr, "WARNING: Not all mandatory flags for post-1.7.7p40 are are present.\n");
        }
    }
    else
    {
        if ((flags & iMand177p40) != 0)
        {
            fprintf(stderr, "WARNING: Unexpected flags for pre-1.7.7p40 appear.\n");
        }
    }
}

void P6H_GAME::ValidateSavedTime() const
{
}

void P6H_GAME::Validate() const
{
    fprintf(stderr, "PennMUSH\n");

    ValidateFlags();
    if (NULL != m_pSavedTime)
    {
        ValidateSavedTime();
    }
    if (m_fFlags || NULL != m_pvFlags)
    {
        if (!m_fFlags)
        {
            fprintf(stderr, "WARNING: +FLAG LIST flagcount missing when list of flags is present.\n");
        }
        else if (0 < m_nFlags && NULL == m_pvFlags)
        {
            fprintf(stderr, "WARNING: +FLAG LIST list of flags is missing then flagcount is present.\n");
        }
        if (m_fFlags && NULL != m_pvFlags)
        {
            if (m_nFlags != m_pvFlags->size())
            {
                fprintf(stderr, "WARNING: flag count (%lu) does not agree with flagcount (%d) in +FLAG LIST\n", m_pvFlags->size(), m_nFlags);
            }
            for (vector<P6H_FLAGINFO *>::iterator it = m_pvFlags->begin(); it != m_pvFlags->end(); ++it)
            {
                (*it)->Validate();
            }
        }
    }

    for (map<int, P6H_OBJECTINFO *, lti>::const_iterator it = m_mObjects.begin(); it != m_mObjects.end(); ++it)
    {
        it->second->Validate();
    }
}

void P6H_OBJECTINFO::Write(FILE *fp, bool fLabels)
{
    fprintf(fp, "!%d\n", m_dbRef);
    if (NULL != m_pName)
    {
        if (fLabels)
        {
            fprintf(fp, "name \"%s\"\n", EncodeString(m_pName));
        }
        else
        {
            fprintf(fp, "\"%s\"\n", EncodeString(m_pName));
        }
    }
    if (m_fLocation)
    {
        if (fLabels)
        {
            fprintf(fp, "location #%d\n", m_dbLocation);
        }
        else
        {
            fprintf(fp, "%d\n", m_dbLocation);
        }
    }
    if (m_fContents)
    {
        if (fLabels)
        {
            fprintf(fp, "contents #%d\n", m_dbContents);
        }
        else
        {
            fprintf(fp, "%d\n", m_dbContents);
        }
    }
    if (m_fExits)
    {
        if (fLabels)
        {
            fprintf(fp, "exits #%d\n", m_dbExits);
        }
        else
        {
            fprintf(fp, "%d\n", m_dbExits);
        }
    }
    if (m_fNext)
    {
        if (fLabels)
        {
            fprintf(fp, "next #%d\n", m_dbNext);
        }
        else
        {
            fprintf(fp, "%d\n", m_dbNext);
        }
    }
    if (m_fParent)
    {
        if (fLabels)
        {
            fprintf(fp, "parent #%d\n", m_dbParent);
        }
        else
        {
            fprintf(fp, "%d\n", m_dbParent);
        }
    }

    if (m_fLockCount)
    {
        if (NULL == m_pvli)
        {
            fprintf(fp, "lockcount 0\n");
        }
        else
        {
            fprintf(fp, "lockcount %lu\n", m_pvli->size());
            for (vector<P6H_LOCKINFO *>::iterator it = m_pvli->begin(); it != m_pvli->end(); ++it)
            {
                (*it)->Write(fp, fLabels);
            }
        }
    }
    if (m_fOwner)
    {
        if (fLabels)
        {
            fprintf(fp, "owner #%d\n", m_dbOwner);
        }
        else
        {
            fprintf(fp, "%d\n", m_dbOwner);
        }
    }
    if (m_fZone)
    {
        if (fLabels)
        {
            fprintf(fp, "zone #%d\n", m_dbZone);
        }
        else
        {
            fprintf(fp, "%d\n", m_dbZone);
        }
    }
    if (m_fPennies)
    {
        if (fLabels)
        {
            fprintf(fp, "pennies %d\n", m_iPennies);
        }
        else
        {
            fprintf(fp, "%d\n", m_iPennies);
        }
    }
    if (m_fType)
    {
        fprintf(fp, "type %d\n", m_iType);
    }
    if (NULL != m_pFlags)
    {
        fprintf(fp, "flags \"%s\"\n", EncodeString(m_pFlags));
    }
    if (m_fFlags)
    {
        fprintf(fp, "%d\n", m_iFlags);
    }
    if (m_fToggles)
    {
        fprintf(fp, "%d\n", m_iToggles);
    }
    if (NULL != m_pPowers)
    {
        fprintf(fp, "powers \"%s\"\n", EncodeString(m_pPowers));
    }
    if (m_fPowers)
    {
        fprintf(fp, "%d\n", m_iPowers);
    }
    if (NULL != m_pWarnings)
    {
        fprintf(fp, "warnings \"%s\"\n", EncodeString(m_pWarnings));
    }
    if (m_fCreated)
    {
        if (fLabels)
        {
            fprintf(fp, "created %d\n", m_iCreated);
        }
        else
        {
            fprintf(fp, "%d\n", m_iCreated);
        }
    }
    if (m_fModified)
    {
        if (fLabels)
        {
            fprintf(fp, "modified %d\n", m_iModified);
        }
        else
        {
            fprintf(fp, "%d\n", m_iModified);
        }
    }
    if (NULL == m_pvai)
    {
        if (fLabels)
        {
            fprintf(fp, "attrcount 0\n");
        }
    }
    else
    {
        if (fLabels)
        {
            fprintf(fp, "attrcount %lu\n", m_pvai->size());
        }
        for (vector<P6H_ATTRINFO *>::iterator it = m_pvai->begin(); it != m_pvai->end(); ++it)
        {
            (*it)->Write(fp, fLabels);
        }
    }
    if (!fLabels)
    {
        fprintf(fp, "<\n");
    }
}

void P6H_OBJECTINFO::Validate() const
{
    if (m_fLockCount || NULL != m_pvli)
    {
        if (!m_fLockCount)
        {
            fprintf(stderr, "WARNING: lock list missing when list of locks is present.\n");
        }
        else if (0 < m_nLockCount && NULL == m_pvli)
        {
            fprintf(stderr, "WARNING: list of locks is missing when lockcount is present.\n");
        }
        if (m_fLockCount && NULL != m_pvli)
        {
            if (m_nLockCount != m_pvli->size())
            {
                fprintf(stderr, "WARNING: lock count (%lu) does not agree with lockcount (%d)\n", m_pvli->size(), m_nLockCount);
            }
            for (vector<P6H_LOCKINFO *>::iterator it = m_pvli->begin(); it != m_pvli->end(); ++it)
            {
                (*it)->Validate();
            }
        }
    }
}

NameMask upgrade_obj_flags[] =
{
    { "CHOWN_OK",       0x00040000UL },
    { "DARK",           0x00000040UL },
    { "GOING",          0x00004000UL },
    { "HAVEN",          0x00000400UL },
    { "TRUST",          0x01000000UL },
    { "LINK_OK",        0x00000020UL },
    { "OPAQUE",         0x00800000UL },
    { "QUIET",          0x00000800UL },
    { "STICKY",         0x00000100UL },
    { "UNFINDABLE",     0x00002000UL },
    { "VISUAL",         0x00100000UL },
    { "WIZARD",         0x00000010UL },
    { "SAFE",           0x04000000UL },
    { "AUDIBLE",        0x10000000UL },
    { "DEBUG",          0x02000000UL },
    { "NO_WARN",        0x00020000UL },
    { "ENTER_OK",       0x00080000UL },
    { "HALT",           0x00001000UL },
    { "NO_COMMAND",     0x20000000UL },
    { "LIGHT",          0x00200000UL },
    { "ROYALTY",        0x00400000UL },
    { "TRANSPARENT",    0x00000200UL },
    { "VERBOSE",        0x00000080UL },
    { "GOING_TWICE",    0x40000000UL },
};

NameMask upgrade_obj_toggles_room[] =
{
    { "ABODE",          0x00000010UL },
    { "MONITOR",        0x00000100UL },
    { "FLOATING",       0x00000008UL },
    { "JUMP_OK",        0x00000020UL },
    { "NO_TEL",         0x00000040UL },
    { "UNINSPECTED",    0x00001000UL },
    { "LISTEN_PARENT",  0x00000400UL },
    { "Z_TEL",          0x00000200UL },
};

NameMask upgrade_obj_toggles_thing[] =
{
    { "DESTROY_OK",     0x00000008UL },
    { "PUPPET",         0x00000010UL },
    { "NO_LEAVE",       0x00000040UL },
    { "MONITOR",        0x00000020UL },
    { "LISTEN_PARENT",  0x00000080UL },
    { "Z_TEL",          0x00000100UL },
};

NameMask upgrade_obj_toggles_exit[] =
{
    { "CLOUDY",         0x00000008UL },
};

NameMask upgrade_obj_toggles_player[] =
{
    { "ANSI",           0x00000400UL },
    { "COLOR",          0x00080000UL },
    { "NOSPOOF",        0x00000020UL },
    { "SHARED",         0x00000800UL },
    { "CONNECTED",      0x00000200UL },
    { "GAGGED",         0x00000080UL },
    { "MYOPIC",         0x00000010UL },
    { "TERSE",          0x00000008UL },
    { "JURY_OK",        0x00001000UL },
    { "JUDGE",          0x00002000UL },
    { "FIXED",          0x00004000UL },
    { "UNREGISTERED",   0x00008000UL },
    { "ON-VACATION",    0x00010000UL },
    { "SUSPECT",        0x00000040UL },
    { "PARANOID",       0x00200000UL },
    { "NOACCENTS",      0x00100000UL },
    { "MONITOR",        0x00000100UL },
};

NameMask upgrade_obj_powers[] =
{
    { "Announce",       0x01000000UL },
    { "Boot",           0x00004000UL },
    { "Builder",        0x00000010UL },
    { "Cemit",          0x02000000UL },
    { "Chat_Privs",     0x00000200UL },
    { "Functions",      0x00200000UL },
    { "Guest",          0x00800000UL },
    { "Halt",           0x00080000UL },
    { "Hide",           0x00000400UL },
    { "Idle",           0x00001000UL },
    { "Immortal",       0x14000100UL },
    { "Link_Anywhere",  0x20000000UL },
    { "Login",          0x00000800UL },
    { "Long_Fingers",   0x00002000UL },
    { "No_Pay",         0x0000000100 },
    { "No_Quota",       0x10000000UL },
    { "Open_Anywhere",  0x40000000UL },
    { "Pemit_All",      0x08000000UL },
    { "Player_Create",  0x00400000UL },
    { "Poll",           0x00010000UL },
    { "Queue",          0x00020000UL },
    { "Quotas",         0x00008000UL },
    { "Search",         0x00100000UL },
    { "See_All",        0x00000080UL },
    { "See_Queue",      0x00040000UL },
    { "Tport_Anything", 0x00000040UL },
    { "Tport_Anywhere", 0x00000020UL },
    { "Unkillable",     0x04000000UL },
    { "Can_nspemit",    0x80000000UL },
};

int upgrade_type[8] =
{
    P6H_TYPE_ROOM,
    P6H_TYPE_THING,
    P6H_TYPE_EXIT,
    P6H_TYPE_PLAYER,
    P6H_NOTYPE,
    P6H_NOTYPE,
    P6H_TYPE_GARBAGE,
    P6H_NOTYPE,
};

void P6H_OBJECTINFO::Upgrade()
{
    char *pWarnings = StringClone("");
    SetWarnings(pWarnings);

    if (m_fFlags)
    {
        int iTypeCode = m_iFlags & P6H_OLD_TYPE_MASK;
        int iType = upgrade_type[iTypeCode];
        SetType(iType);

        char aBuffer[1000];
        char *pBuffer = aBuffer;

        // Remaining bits of m_flags.
        //
        bool fFirst = true;
        for (int i = 0; i < sizeof(upgrade_obj_flags)/sizeof(upgrade_obj_flags[0]); i++)
        {
            if (upgrade_obj_flags[i].mask & m_iFlags)
            {
                if (!fFirst)
                {
                    *pBuffer++ = ' ';
                }
                fFirst = false;
                strcpy(pBuffer, upgrade_obj_flags[i].pName);
                pBuffer += strlen(upgrade_obj_flags[i].pName);
            }
        }

        // Toggles.
        //
        if (m_fToggles)
        {
            NameMask *pnm = NULL;
            int       nnm = 0;

            switch (iTypeCode)
            {
            case P6H_OLD_TYPE_ROOM:
                pnm = upgrade_obj_toggles_room;
                nnm = sizeof(upgrade_obj_toggles_room)/sizeof(upgrade_obj_toggles_room[0]);
                break;

            case P6H_OLD_TYPE_THING:
                pnm = upgrade_obj_toggles_thing;
                nnm = sizeof(upgrade_obj_toggles_thing)/sizeof(upgrade_obj_toggles_thing[0]);
                break;

            case P6H_OLD_TYPE_EXIT:
                pnm = upgrade_obj_toggles_exit;
                nnm = sizeof(upgrade_obj_toggles_exit)/sizeof(upgrade_obj_toggles_exit[0]);
                break;

            case P6H_OLD_TYPE_PLAYER:
                pnm = upgrade_obj_toggles_player;
                nnm = sizeof(upgrade_obj_toggles_player)/sizeof(upgrade_obj_toggles_player[0]);
                break;
            }

            for (int i = 0; i < nnm; i++)
            {
                if (pnm[i].mask & m_iToggles)
                {
                    if (!fFirst)
                    {
                        *pBuffer++ = ' ';
                    }
                    fFirst = false;
                    strcpy(pBuffer, pnm[i].pName);
                    pBuffer += strlen(pnm[i].pName);
                }
            }
        }
        *pBuffer = '\0';
        SetFlags(StringClone(aBuffer));

        // Powers.
        //
        pBuffer = aBuffer;
        fFirst = true;
        if (m_fPowers)
        {
            for (int i = 0; i < sizeof(upgrade_obj_powers)/sizeof(upgrade_obj_powers[0]); i++)
            {
                if (upgrade_obj_powers[i].mask & m_iPowers)
                {
                    if (!fFirst)
                    {
                        *pBuffer++ = ' ';
                    }
                    fFirst = false;
                    strcpy(pBuffer, upgrade_obj_powers[i].pName);
                    pBuffer += strlen(upgrade_obj_powers[i].pName);
                }
            }
        }
        *pBuffer = '\0';
        SetPowers(StringClone(aBuffer));
    }
    m_fFlags = false;
    m_fToggles = false;
    m_fPowers = false;

    if (NULL != m_pvai)
    {
        for (vector<P6H_ATTRINFO *>::iterator it = m_pvai->begin(); it != m_pvai->end(); ++it)
        {
            (*it)->Upgrade();
        }
    }
    if (NULL != m_pvli)
    {
        for (vector<P6H_LOCKINFO *>::iterator it = m_pvli->begin(); it != m_pvli->end(); ++it)
        {
            (*it)->Upgrade();
        }
    }
}

// In many places, PennMUSH allows any character in a field that the game itself allows.
//
bool p6h_IsValidGameCharacter(int ch)
{
    return (0 != isgraph(ch));
}

void P6H_FLAGINFO::Validate() const
{
    if (NULL != m_pName)
    {
        if (strlen(m_pName) <= 1)
        {
            fprintf(stderr, "WARNING: Flag name (%s) should be longer than a single character.\n", m_pName);
        }
        if (NULL != strchr(m_pName, ' '))
        {
            fprintf(stderr, "WARNING: Flag name (%s) should not contain spaces.\n", m_pName);
        }
        for (char *p = m_pName; *p; p++)
        {
            if (  ' ' != *p
               && !p6h_IsValidGameCharacter(*p))
            {
                fprintf(stderr, "WARNING: Not all characters in flag name '%s' are valid.\n", m_pName);
                break;
            }
        }
    }
    if (NULL != m_pLetter)
    {
        if (1 < strlen(m_pLetter))
        {
            fprintf(stderr, "WARNING: Letter (%s), if used, should be a single character.\n", m_pLetter);
        }
        else
        {
            if (  '\0' != m_pLetter[0]
               && !p6h_IsValidGameCharacter(m_pLetter[0]))
            {
                fprintf(stderr, "WARNING: Letter (0x%02X) is not valid.\n", m_pLetter[0]);
            }
        }
        if (NULL != strchr(m_pLetter, ' '))
        {
            fprintf(stderr, "WARNING: Letter (%s) should not contain spaces.\n", m_pLetter);
        }
    }
    if (NULL != m_pType)
    {
        for (char *p = m_pType; *p; p++)
        {
            if (  ' ' != *p
               && !p6h_IsValidGameCharacter(*p))
            {
                fprintf(stderr, "WARNING: Not all characters in flag type '%s' are valid.\n", m_pType);
                break;
            }
        }
    }
    if (NULL != m_pPerms)
    {
        for (char *p = m_pPerms; *p; p++)
        {
            if (  ' ' != *p
               && !p6h_IsValidGameCharacter(*p))
            {
                fprintf(stderr, "WARNING: Not all characters in flag permissions '%s' are valid.\n", m_pPerms);
                break;
            }
        }
    }
    if (NULL != m_pNegatePerms)
    {
        for (char *p = m_pNegatePerms; *p; p++)
        {
            if (  ' ' != *p
               && !p6h_IsValidGameCharacter(*p))
            {
                fprintf(stderr, "WARNING: Not all characters in flag negate permissions '%s' are valid.\n", m_pNegatePerms);
                break;
            }
        }
    }
}

void P6H_FLAGINFO::Write(FILE *fp)
{
    if (NULL != m_pName)
    {
        fprintf(fp, " name \"%s\"\n", EncodeString(m_pName));
        if (NULL != m_pLetter)
        {
            fprintf(fp, "  letter \"%s\"\n", EncodeString(m_pLetter));
        }
        if (NULL != m_pType)
        {
            fprintf(fp, "  type \"%s\"\n", EncodeString(m_pType));
        }
        if (NULL != m_pPerms)
        {
            fprintf(fp, "  perms \"%s\"\n", EncodeString(m_pPerms));
        }
        if (NULL != m_pNegatePerms)
        {
            fprintf(fp, "  negate_perms \"%s\"\n", EncodeString(m_pNegatePerms));
        }
    }
}

void P6H_FLAGALIASINFO::Write(FILE *fp)
{
    if (NULL != m_pName)
    {
        fprintf(fp, " name \"%s\"\n", EncodeString(m_pName));
        if (NULL != m_pAlias)
        {
            fprintf(fp, "  alias \"%s\"\n", EncodeString(m_pAlias));
        }
    }
}

void P6H_LOCKINFO::Validate() const
{
    if (NULL != m_pType)
    {
        if (NULL != strchr(m_pType, ' '))
        {
            fprintf(stderr, "WARNING: Found blank in lock type '%s'.\n", m_pType);
        }
    }
    if (  NULL != m_pKey
       && NULL != m_pKeyTree)
    {
        char buffer[65536];
        char *p = m_pKeyTree->Write(buffer);
        *p = '\0';
        if (strcmp(m_pKey, buffer) != 0)
        {
             fprintf(stderr, "WARNING: Re-generated lock key '%s' does not agree with parsed key '%s'.\n", buffer, m_pKey);
             exit(1);
        }
    }
}

void P6H_LOCKINFO::Write(FILE *fp, bool fLabels) const
{
    if (NULL != m_pType)
    {
        if (fLabels)
        {
            fprintf(fp, " type \"%s\"\n", EncodeString(m_pType));
        }
        else
        {
            fprintf(fp, "type \"%s\"\n", EncodeString(m_pType));
        }
        if (m_fCreator)
        {
            if (fLabels)
            {
                fprintf(fp, "  creator #%d\n", m_dbCreator);
            }
            else
            {
                fprintf(fp, "creator %d\n", m_dbCreator);
            }
        }
        if (NULL != m_pFlags)
        {
            fprintf(fp, "  flags \"%s\"\n", EncodeString(m_pFlags));
        }
        if (m_fFlags)
        {
            if (fLabels)
            {
                fprintf(fp, "  flags %d\n", m_iFlags);
            }
            else
            {
                fprintf(fp, "flags %d\n", m_iFlags);
            }
        }
        if (m_fDerefs)
        {
            fprintf(fp, "  derefs %d\n", m_iDerefs);
        }
        if (NULL != m_pKey)
        {
            if (fLabels)
            {
                fprintf(fp, "  key \"%s\"\n", EncodeString(m_pKey));
            }
            else
            {
                fprintf(fp, "key \"%s\"\n", EncodeString(m_pKey));
            }
        }
    }
}

NameMask upgrade_lock_flags[] =
{
    { "visual",         0x00000001UL },
    { "no_inherit",     0x00000002UL },
    { "no_clone",       0x00000010UL },
    { "wizard",         0x00000004UL },
    { "locked",         0x00000008UL },
};

void P6H_LOCKINFO::Upgrade()
{
    char aBuffer[1000];
    char *pBuffer = aBuffer;
    bool fFirst = true;
    if (m_fFlags)
    {
        for (int i = 0; i < sizeof(upgrade_lock_flags)/sizeof(upgrade_lock_flags[0]); i++)
        {
            if (upgrade_lock_flags[i].mask & m_iFlags)
            {
                if (!fFirst)
                {
                    *pBuffer++ = ' ';
                }
                fFirst = false;
                strcpy(pBuffer, upgrade_lock_flags[i].pName);
                pBuffer += strlen(upgrade_lock_flags[i].pName);
            }
        }
    }
    *pBuffer = '\0';
    SetFlags(StringClone(aBuffer));
    m_fFlags = false;
    SetDerefs(0);
}

void P6H_ATTRINFO::Write(FILE *fp, bool fLabels) const
{
    if (fLabels)
    {
        if (NULL != m_pName)
        {
            fprintf(fp, " name \"%s\"\n", EncodeString(m_pName));
            if (m_fOwner)
            {
                fprintf(fp, "  owner #%d\n", m_dbOwner);
            }
            if (NULL != m_pFlags)
            {
                fprintf(fp, "  flags \"%s\"\n", EncodeString(m_pFlags));
            }
            if (m_fFlags)
            {
                fprintf(fp, "  flags %d\n", m_iFlags);
            }
            if (m_fDerefs)
            {
                fprintf(fp, "  derefs %d\n", m_iDerefs);
            }
            if (NULL != m_pValue)
            {
                fprintf(fp, "  value \"%s\"\n", EncodeString(m_pValue));
            }
        }
    }
    else if (  NULL != m_pName
            && m_fOwner
            && m_fFlags
            && NULL != m_pValue)
    {
        fprintf(fp, "]%s^%d^%d\n\"%s\"\n", m_pName, m_dbOwner, m_iFlags, EncodeString(m_pValue));
    }
}

NameMask upgrade_attr_flags[] =
{
    { "no_command",     0x00000020UL },
    { "no_inherit",     0x00000080UL },
    { "no_clone",       0x00000100UL },
    { "wizard",         0x00000004UL },
    { "visual",         0x00000200UL },
    { "mortal_dark",    0x00000040UL },
    { "regexp",         0x00000400UL },
    { "case",           0x00000800UL },
    { "locked",         0x00000010UL },
    { "safe",           0x00001000UL },
    { "internal",       0x00000002UL },
    { "prefixmatch",    0x00200000UL },
    { "veiled",         0x00400000UL },
    { "debug",          0x00800000UL },
    { "public",         0x02000000UL },
    { "nearby",         0x01000000UL },
    { "noname",         0x08000000UL },
    { "nospace",        0x10000000UL },
};

void P6H_ATTRINFO::Upgrade()
{
    char aBuffer[1000];
    char *pBuffer = aBuffer;
    bool fFirst = true;
    if (m_fFlags)
    {
        for (int i = 0; i < sizeof(upgrade_attr_flags)/sizeof(upgrade_attr_flags[0]); i++)
        {
            if (upgrade_attr_flags[i].mask & m_iFlags)
            {
                if (!fFirst)
                {
                    *pBuffer++ = ' ';
                }
                fFirst = false;
                strcpy(pBuffer, upgrade_attr_flags[i].pName);
                pBuffer += strlen(upgrade_attr_flags[i].pName);
            }
        }
    }
    *pBuffer = '\0';
    SetFlags(StringClone(aBuffer));
    m_fFlags = false;
    SetDerefs(0);
}

void P6H_GAME::Write(FILE *fp)
{
    fprintf(fp, "+V%d\n", (m_flags + 5) * 256 + 2);
    if (m_fDbVersion)
    {
        fprintf(fp, "dbversion %d\n", m_nDbVersion);
    }
    if (NULL != m_pSavedTime)
    {
        fprintf(fp, "savedtime \"%s\"\n", m_pSavedTime);
    }
    if (m_fFlags)
    {
        fprintf(fp, "+FLAGS LIST\nflagcount %d\n", m_nFlags);
    }
    if (NULL != m_pvFlags)
    {
        for (vector<P6H_FLAGINFO *>::iterator it = m_pvFlags->begin(); it != m_pvFlags->end(); ++it)
        {
            (*it)->Write(fp);
        }
    }
    if (m_fFlagAliases)
    {
        fprintf(fp, "flagaliascount %d\n", m_nFlagAliases);
    }
    if (NULL != m_pvFlagAliases)
    {
        for (vector<P6H_FLAGALIASINFO *>::iterator it = m_pvFlagAliases->begin(); it != m_pvFlagAliases->end(); ++it)
        {
            (*it)->Write(fp);
        }
    }
    if (m_fPowers)
    {
        fprintf(fp, "+POWER LIST\nflagcount %d\n", m_nPowers);
    }
    if (NULL != m_pvPowers)
    {
        for (vector<P6H_FLAGINFO *>::iterator it = m_pvPowers->begin(); it != m_pvPowers->end(); ++it)
        {
            (*it)->Write(fp);
        }
    }
    if (m_fPowerAliases)
    {
        fprintf(fp, "flagaliascount %d\n", m_nPowerAliases);
    }
    if (NULL != m_pvPowerAliases)
    {
        for (vector<P6H_FLAGALIASINFO *>::iterator it = m_pvPowerAliases->begin(); it != m_pvPowerAliases->end(); ++it)
        {
            (*it)->Write(fp);
        }
    }
    if (m_fAttributeNames)
    {
        fprintf(fp, "+ATTRIBUTES LIST\nattrcount %d\n", m_nAttributeNames);
    }
    if (NULL != m_pvAttributeNames)
    {
        for (vector<P6H_ATTRNAMEINFO *>::iterator it = m_pvAttributeNames->begin(); it != m_pvAttributeNames->end(); ++it)
        {
            (*it)->Write(fp);
        }
    }
    if (m_fAttributeAliases)
    {
        fprintf(fp, "attraliascount %d\n", m_nAttributeAliases);
    }
    if (NULL != m_pvAttributeAliases)
    {
        for (vector<P6H_ATTRALIASINFO *>::iterator it = m_pvAttributeAliases->begin(); it != m_pvAttributeAliases->end(); ++it)
        {
            (*it)->Write(fp);
        }
    }
    if (m_fSizeHint)
    {
        fprintf(fp, "~%d\n", m_nSizeHint);
    }
    bool fLabels = HasLabels();
    for (map<int, P6H_OBJECTINFO *, lti>::iterator it = m_mObjects.begin(); it != m_mObjects.end(); ++it)
    {
        it->second->Write(fp, fLabels);
    }

    fprintf(fp, "***END OF DUMP***\n");
}

static struct
{
   const char *pName;
   const char *pLetter;
   const char *pType;
   const char *pPerms;
   const char *pNegatePerms;
} upgrade_flags[] =
{
    { "CHOWN_OK",           "C",   "ROOM EXIT THING",         "",                     ""                },
    { "DARK",               "D",   "PLAYER ROOM EXIT THING",  "",                     ""                },
    { "GOING",              "G",   "PLAYER ROOM EXIT THING",  "internal",             ""                },
    { "HAVEN",              "H",   "PLAYER ROOM EXIT THING",  "",                     ""                },
    { "TRUST",              "I",   "PLAYER ROOM EXIT THING",  "trusted",              "trusted"         },
    { "LINK_OK",            "L",   "PLAYER ROOM EXIT THING",  "",                     ""                },
    { "OPAQUE",             "O",   "PLAYER ROOM EXIT THING",  "",                     ""                },
    { "QUIET",              "Q",   "PLAYER ROOM EXIT THING",  "",                     ""                },
    { "STICKY",             "S",   "PLAYER ROOM EXIT THING",  "",                     ""                },
    { "UNFINDABLE",         "U",   "PLAYER ROOM EXIT THING",  "",                     ""                },
    { "VISUAL",             "V",   "PLAYER ROOM EXIT THING",  "",                     ""                },
    { "WIZARD",             "W",   "PLAYER ROOM EXIT THING",  "trusted wizard log",   "trusted wizard"  },
    { "SAFE",               "X",   "PLAYER ROOM EXIT THING",  "",                     ""                },
    { "AUDIBLE",            "a",   "PLAYER ROOM EXIT THING",  "",                     ""                },
    { "DEBUG",              "b",   "PLAYER ROOM EXIT THING",  "",                     ""                },
    { "NO_WARN",            "w",   "PLAYER ROOM EXIT THING",  "",                     ""                },
    { "ENTER_OK",           "e",   "PLAYER ROOM EXIT THING",  "",                     ""                },
    { "HALT",               "h",   "PLAYER ROOM EXIT THING",  "",                     ""                },
    { "NO_COMMAND",         "n",   "PLAYER ROOM EXIT THING",  "",                     ""                },
    { "LIGHT",              "l",   "PLAYER ROOM EXIT THING",  "",                     ""                },
    { "ROYALTY",            "r",   "PLAYER ROOM EXIT THING",  "trusted royalty log",  "trusted royalty" },
    { "TRANSPARENT",        "t",   "PLAYER ROOM EXIT THING",  "",                     ""                },
    { "VERBOSE",            "v",   "PLAYER ROOM EXIT THING",  "",                     ""                },
    { "ANSI",               "A",   "PLAYER",                  "",                     ""                },
    { "COLOR",              "C",   "PLAYER",                  "",                     ""                },
    { "MONITOR",            "M",   "PLAYER ROOM THING",       "",                     ""                },
    { "NOSPOOF",            "\"",  "PLAYER ROOM EXIT THING",  "odark",                "odark"           },
    { "SHARED",             "Z",   "PLAYER",                  "",                     ""                },
    { "CONNECTED",          "c",   "PLAYER",                  "internal mdark",       "internal mdark"  },
    { "GAGGED",             "g",   "PLAYER",                  "wizard",               "wizard"          },
    { "MYOPIC",             "m",   "PLAYER",                  "",                     ""                },
    { "TERSE",              "x",   "PLAYER THING",            "",                     ""                },
    { "JURY_OK",            "j",   "PLAYER",                  "royalty",              "royalty"         },
    { "JUDGE",              "J",   "PLAYER",                  "royalty",              "royalty"         },
    { "FIXED",              "F",   "PLAYER",                  "wizard",               "wizard"          },
    { "UNREGISTERED",       "?",   "PLAYER",                  "royalty",              "royalty"         },
    { "ON-VACATION",        "o",   "PLAYER",                  "",                     ""                },
    { "SUSPECT",            "s",   "PLAYER ROOM EXIT THING",  "wizard mdark log",     "wizard mdark"    },
    { "PARANOID",           "",    "PLAYER ROOM EXIT THING",  "odark",                "odark"           },
    { "NOACCENTS",          "~",   "PLAYER",                  "",                     ""                },
    { "DESTROY_OK",         "d",   "THING",                   "",                     ""                },
    { "PUPPET",             "p",   "ROOM THING",              "",                     ""                },
    { "NO_LEAVE",           "N",   "THING",                   "",                     ""                },
    { "LISTEN_PARENT",      "^",   "ROOM THING",              "",                     ""                },
    { "Z_TEL",              "Z",   "ROOM THING",              "",                     ""                },
    { "ABODE",              "A",   "ROOM",                    "",                     ""                },
    { "FLOATING",           "F",   "ROOM",                    "",                     ""                },
    { "JUMP_OK",            "J",   "ROOM",                    "",                     ""                },
    { "NO_TEL",             "N",   "ROOM",                    "",                     ""                },
    { "UNINSPECTED",        "u",   "ROOM",                    "royalty",              "royalty"         },
    { "CLOUDY",             "x",   "EXIT",                    "",                     ""                },
    { "GOING_TWICE",        "",    "PLAYER ROOM EXIT THING",  "internal dark",        "internal dark"   },
    { "MISTRUST",           "m",   "ROOM EXIT THING",         "trusted",              "trusted"         },
    { "ORPHAN",             "i",   "PLAYER ROOM EXIT THING",  "",                     ""                },
    { "HEAVY",              "",    "PLAYER ROOM EXIT THING",  "royalty",              ""                },
    { "CHAN_USEFIRSTMATCH", "",    "PLAYER ROOM EXIT THING",  "trusted",              "trusted"         },
};

static struct
{
   const char *pName;
   const char *pAlias;
} upgrade_flagaliases[] =
{
    { "LISTEN_PARENT",       "^"               },
    { "CHAN_USEFIRSTMATCH",  "CHAN_FIRSTMATCH" },
    { "CHAN_USEFIRSTMATCH",  "CHAN_MATCHFIRST" },
    { "COLOR",               "COLOUR"          },
    { "DESTROY_OK",          "DEST_OK"         },
    { "TRUST",               "INHERIT"         },
    { "JURY_OK",             "JURYOK"          },
    { "MONITOR",             "LISTENER"        },
    { "NO_COMMAND",          "NOCOMMAND"       },
    { "NO_LEAVE",            "NOLEAVE"         },
    { "NO_WARN",             "NOWARN"          },
    { "JUMP_OK",             "TEL-OK"          },
    { "JUMP_OK",             "TEL_OK"          },
    { "JUMP_OK",             "TELOK"           },
    { "DEBUG",               "TRACE"           },
    { "MONITOR",             "WATCHER"         },
    { "SHARED",              "ZONE"            },
};

static struct
{
   const char *pName;
   const char *pLetter;
   const char *pType;
   const char *pPerms;
   const char *pNegatePerms;
} upgrade_powers[] =
{
    { "Announce",        "",  "PLAYER ROOM EXIT THING",  "wizard log",  "wizard" },
    { "Boot",            "",  "PLAYER ROOM EXIT THING",  "wizard log",  "wizard" },
    { "Builder",         "",  "PLAYER ROOM EXIT THING",  "wizard log",  "wizard" },
    { "Cemit",           "",  "PLAYER ROOM EXIT THING",  "wizard log",  "wizard" },
    { "Chat_Privs",      "",  "PLAYER ROOM EXIT THING",  "wizard log",  "wizard" },
    { "Functions",       "",  "PLAYER ROOM EXIT THING",  "wizard log",  "wizard" },
    { "Guest",           "",  "PLAYER ROOM EXIT THING",  "wizard log",  "wizard" },
    { "Halt",            "",  "PLAYER ROOM EXIT THING",  "wizard log",  "wizard" },
    { "Hide",            "",  "PLAYER ROOM EXIT THING",  "wizard log",  "wizard" },
    { "Idle",            "",  "PLAYER ROOM EXIT THING",  "wizard log",  "wizard" },
    { "Immortal",        "",  "PLAYER ROOM EXIT THING",  "wizard log",  "wizard" },
    { "Link_Anywhere",   "",  "PLAYER ROOM EXIT THING",  "wizard log",  "wizard" },
    { "Login",           "",  "PLAYER ROOM EXIT THING",  "wizard log",  "wizard" },
    { "Long_Fingers",    "",  "PLAYER ROOM EXIT THING",  "wizard log",  "wizard" },
    { "No_Pay",          "",  "PLAYER ROOM EXIT THING",  "wizard log",  "wizard" },
    { "No_Quota",        "",  "PLAYER ROOM EXIT THING",  "wizard log",  "wizard" },
    { "Open_Anywhere",   "",  "PLAYER ROOM EXIT THING",  "wizard log",  "wizard" },
    { "Pemit_All",       "",  "PLAYER ROOM EXIT THING",  "wizard log",  "wizard" },
    { "Player_Create",   "",  "PLAYER ROOM EXIT THING",  "wizard log",  "wizard" },
    { "Poll",            "",  "PLAYER ROOM EXIT THING",  "wizard log",  "wizard" },
    { "Queue",           "",  "PLAYER ROOM EXIT THING",  "wizard log",  "wizard" },
    { "Quotas",          "",  "PLAYER ROOM EXIT THING",  "wizard log",  "wizard" },
    { "Search",          "",  "PLAYER ROOM EXIT THING",  "wizard log",  "wizard" },
    { "See_All",         "",  "PLAYER ROOM EXIT THING",  "wizard log",  "wizard" },
    { "See_Queue",       "",  "PLAYER ROOM EXIT THING",  "wizard log",  "wizard" },
    { "Tport_Anything",  "",  "PLAYER ROOM EXIT THING",  "wizard log",  "wizard" },
    { "Tport_Anywhere",  "",  "PLAYER ROOM EXIT THING",  "wizard log",  "wizard" },
    { "Unkillable",      "",  "PLAYER ROOM EXIT THING",  "wizard log",  "wizard" },
    { "Can_nspemit",     "",  "PLAYER ROOM EXIT THING",  "wizard log",  "wizard" },
    { "SQL_OK",          "",  "PLAYER ROOM EXIT THING",  "wizard log",  "" },
    { "DEBIT",           "",  "PLAYER ROOM EXIT THING",  "wizard log",  "" },
};

static struct
{
   const char *pName;
   const char *pAlias;
} upgrade_poweraliases[] =
{
    { "Cemit",     "@cemit" },
    { "Announce",  "@wall" },
    { "Announce",  "wall" },
};

void P6H_GAME::Upgrade()
{
    bool fLabels = HasLabels();
    if (fLabels)
    {
        return;
    }

    // Additional flatfile flags.
    //
    m_flags = m_flags
            | DBF_LABELS
            | DBF_NEW_FLAGS
            | DBF_NEW_POWERS
            | DBF_POWERS_LOGGED
            | DBF_WARNINGS;

    // savedtime
    //
    time_t ct;
    time(&ct);
    char *pTime = ctime(&ct);
    if (NULL != pTime)
    {
        char *p = strchr(pTime, '\n');
        if (NULL != p)
        {
            size_t n = p - pTime;
            pTime = StringCloneLen(pTime, n);
            SetSavedTime(pTime);
        }
    }

    // Add Flags
    //
    vector<P6H_FLAGINFO *> *pvFlags= new vector<P6H_FLAGINFO *>;
    for (int i = 0; i < sizeof(upgrade_flags)/sizeof(upgrade_flags[0]); i++)
    {
        P6H_FLAGINFO *pfi = new P6H_FLAGINFO;
        pfi->SetName(StringClone(upgrade_flags[i].pName));
        pfi->SetLetter(StringClone(upgrade_flags[i].pLetter));
        pfi->SetType(StringClone(upgrade_flags[i].pType));
        pfi->SetPerms(StringClone(upgrade_flags[i].pPerms));
        pfi->SetNegatePerms(StringClone(upgrade_flags[i].pNegatePerms));
        pvFlags->push_back(pfi);
    }
    SetFlagList(pvFlags);
    pvFlags = NULL;
    SetFlagCount(sizeof(upgrade_flags)/sizeof(upgrade_flags[0]));

    // Add FlagAliases
    //
    vector<P6H_FLAGALIASINFO *> *pvFlagAliases= new vector<P6H_FLAGALIASINFO *>;
    for (int i = 0; i < sizeof(upgrade_flagaliases)/sizeof(upgrade_flagaliases[0]); i++)
    {
        P6H_FLAGALIASINFO *pfai = new P6H_FLAGALIASINFO;
        pfai->SetName(StringClone(upgrade_flagaliases[i].pName));
        pfai->SetAlias(StringClone(upgrade_flagaliases[i].pAlias));
        pvFlagAliases->push_back(pfai);
    }
    SetFlagAliasList(pvFlagAliases);
    pvFlagAliases = NULL;
    SetFlagAliasCount(sizeof(upgrade_flagaliases)/sizeof(upgrade_flagaliases[0]));

    // Add Powers
    //
    pvFlags= new vector<P6H_FLAGINFO *>;
    for (int i = 0; i < sizeof(upgrade_powers)/sizeof(upgrade_powers[0]); i++)
    {
        P6H_FLAGINFO *pfi = new P6H_FLAGINFO;
        pfi->SetName(StringClone(upgrade_powers[i].pName));
        pfi->SetLetter(StringClone(upgrade_powers[i].pLetter));
        pfi->SetType(StringClone(upgrade_powers[i].pType));
        pfi->SetPerms(StringClone(upgrade_powers[i].pPerms));
        pfi->SetNegatePerms(StringClone(upgrade_powers[i].pNegatePerms));
        pvFlags->push_back(pfi);
    }
    SetPowerList(pvFlags);
    pvFlags = NULL;
    SetPowerCount(sizeof(upgrade_powers)/sizeof(upgrade_powers[0]));

    // Add PowerAliases
    //
    pvFlagAliases= new vector<P6H_FLAGALIASINFO *>;
    for (int i = 0; i < sizeof(upgrade_poweraliases)/sizeof(upgrade_poweraliases[0]); i++)
    {
        P6H_FLAGALIASINFO *pfai = new P6H_FLAGALIASINFO;
        pfai->SetName(StringClone(upgrade_poweraliases[i].pName));
        pfai->SetAlias(StringClone(upgrade_poweraliases[i].pAlias));
        pvFlagAliases->push_back(pfai);
    }
    SetPowerAliasList(pvFlagAliases);
    pvFlagAliases = NULL;
    SetPowerAliasCount(sizeof(upgrade_poweraliases)/sizeof(upgrade_poweraliases[0]));

    // Upgrade objects.
    //
    for (map<int, P6H_OBJECTINFO *, lti>::iterator it = m_mObjects.begin(); it != m_mObjects.end(); ++it)
    {
        it->second->Upgrade();
    }
}

void P6H_GAME::ResetPassword()
{
    bool fLabels = HasLabels();

    for (map<int, P6H_OBJECTINFO *, lti>::iterator itObj = m_mObjects.begin(); itObj != m_mObjects.end(); ++itObj)
    {
        if (1 == itObj->second->m_dbRef)
        {
            bool fFound = false;
            if (NULL != itObj->second->m_pvai)
            {
                for (vector<P6H_ATTRINFO *>::iterator itAttr = itObj->second->m_pvai->begin(); itAttr != itObj->second->m_pvai->end(); ++itAttr)
                {
                    if (strcasecmp("XYXXY", (*itAttr)->m_pName) == 0)
                    {
                        // Change it to 'potrzebie'.
                        //
                        free((*itAttr)->m_pValue);
                        (*itAttr)->m_pValue = StringClone("XX41009057111400169070");

                        fFound = true;
                    }
                }
            }

            if (!fFound)
            {
                // Add it.
                //
                P6H_ATTRINFO *pai = new P6H_ATTRINFO;
                pai->SetName(StringClone("XYXXY"));
                pai->SetOwner(1);
                if (fLabels)
                {
                    pai->SetFlags(StringClone("no_command wizard locked internal"));
                }
                else
                {
                    pai->SetFlags(54);
                }
                pai->SetDerefs(0);
                pai->SetValue(StringClone("XX41009057111400169070"));

                if (NULL == itObj->second->m_pvai)
                {
                    vector<P6H_ATTRINFO *> *pvai = new vector<P6H_ATTRINFO *>;
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

int t5x_convert_type[] =
{
    P6H_TYPE_ROOM,
    P6H_TYPE_THING,
    P6H_TYPE_EXIT,
    P6H_TYPE_PLAYER,
    P6H_TYPE_GARBAGE,
    P6H_NOTYPE,
    P6H_NOTYPE,
    P6H_NOTYPE,
};

NameMask t5x_convert_obj_flags1[] =
{
    { "TRANSPARENT",    T5X_SEETHRU    },
    { "WIZARD",         T5X_WIZARD     },
    { "LINK_OK",        T5X_LINK_OK    },
    { "DARK",           T5X_DARK       },
    { "JUMP_OK",        T5X_JUMP_OK    },
    { "STICKY",         T5X_STICKY     },
    { "DESTROY_OK",     T5X_DESTROY_OK },
    { "HAVEN",          T5X_HAVEN      },
    { "QUIET",          T5X_QUIET      },
    { "HALT",           T5X_HALT       },
    { "DEBUG",          T5X_TRACE      },
    { "GOING",          T5X_GOING      },
    { "MONITOR",        T5X_MONITOR    },
    { "MYOPIC",         T5X_MYOPIC     },
    { "PUPPET",         T5X_PUPPET     },
    { "CHOWN_OK",       T5X_CHOWN_OK   },
    { "ENTER_OK",       T5X_ENTER_OK   },
    { "VISUAL",         T5X_VISUAL     },
    { "OPAQUE",         T5X_OPAQUE     },
    { "VERBOSE",        T5X_VERBOSE    },
    { "NOSPOOF",        T5X_NOSPOOF    },
    { "SAFE",           T5X_SAFE       },
    { "ROYALTY",        T5X_ROYALTY    },
    { "AUDIBLE",        T5X_HEARTHRU   },
    { "TERSE",          T5X_TERSE      },
};

NameMask t5x_convert_obj_flags2[] =
{
    { "ABODE",          T5X_ABODE       },
    { "FLOATING",       T5X_FLOATING    },
    { "UNFINDABLE",     T5X_UNFINDABLE  },
    { "LIGHT",          T5X_LIGHT       },
    { "ANSI",           T5X_ANSI        },
    { "COLOR",          T5X_ANSI        },
    { "FIXED",          T5X_FIXED       },
    { "UNINSPECTED",    T5X_UNINSPECTED },
    { "NO_COMMAND",     T5X_NO_COMMAND  },
    { "KEEPALIVE",      T5X_KEEPALIVE   },
    { "GAGGED",         T5X_GAGGED      },
    { "ON-VACATION",    T5X_VACATION    },
    { "SUSPECT",        T5X_SUSPECT     },
    { "NOACCENTS",      T5X_ASCII       },
    { "SLAVE",          T5X_SLAVE       },
};

NameMask t5x_convert_obj_powers1[] =
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

NameMask t5x_convert_obj_powers2[] =
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
    { "ADESCRIBE",      36 },
    { "ADISCONNECT",    40 },
    { "ADROP",          14 },
    { "AEFAIL",         68 },
    { "AENTER",         35 },
    { "AFAILURE",       13 },
    { "AHEAR",          29 },
    { "ALEAVE",         52 },
    { "ALFAIL",         71 },
    { "ALIAS",          58 },
    { "AMAIL",         202 },
    { "AMHEAR",         28 },
    { "AMOVE",          57 },
    { "APAYMENT",       21 },
    { "ASUCCESS",       12 },
    { "ATPORT",         82 },
    { "AUFAIL",         77 },
    { "AUSE",           16 },
    { "AWAY",           73 },
    { "CHARGES",        17 },
    { "COMMENT",        44 },
    { "CONFORMAT",     242 },
    { "COST",           24 },
    { "DESCRIBE",        6 },
    { "DESCFORMAT",    244 },
    { "DESTINATION",   216 },
    { "DROP",            9 },
    { "EALIAS",         64 },
    { "EFAIL",          66 },
    { "ENTER",          33 },
    { "EXITFORMAT",    241 },
    { "EXITTO",        216 },
    { "FAILURE",         3 },
    { "FILTER",         92 },
    { "FORWARDLIST",    95 },
    { "IDESCRIBE",      32 },
    { "IDLE",           74 },
    { "INFILTER",       91 },
    { "INPREFIX",       89 },
    { "LALIAS",         65 },
    { "LAST",           30 },
    { "LASTSITE",       88 },
    { "LASTIP",        144 },
    { "LEAVE",          50 },
    { "LFAIL",          69 },
    { "LISTEN",         26 },
    { "MOVE",           55 },
    { "NAMEFORMAT",    243 },
    { "ODESCRIBE",      37 },
    { "ODROP",           8 },
    { "OEFAIL",         67 },
    { "OENTER",         53 },
    { "OFAILURE",        2 },
    { "OLEAVE",         51 },
    { "OLFAIL",         70 },
    { "OMOVE",          56 },
    { "OPAYMENT",       22 },
    { "OSUCCESS",        1 },
    { "OTPORT",         80 },
    { "OUFAIL",         76 },
    { "OUSE",           46 },
    { "OXENTER",        34 },
    { "OXLEAVE",        54 },
    { "OXTPORT",        81 },
    { "PAYMENT",        23 },
    { "PREFIX",         90 },
    { "RQUOTA",         38 },
    { "RUNOUT",         18 },
    { "SEMAPHORE",      47 },
    { "SEX",             7 },
    { "MAILSIGNATURE", 203 },
    { "STARTUP",        19 },
    { "SUCC",            4 },
    { "TPORT",          79 },
    { "UFAIL",          75 },
    { "USE",            45 },
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
    { "XYXXY",           5 },
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

void P6H_GAME::ConvertFromT5X()
{
    SetFlags( DBF_NO_CHAT_SYSTEM
            | DBF_CREATION_TIMES
            | DBF_NEW_STRINGS
            | DBF_TYPE_GARBAGE
            | DBF_LESS_GARBAGE
            | DBF_SPIFFY_LOCKS
            | DBF_NEW_FLAGS
            | DBF_NEW_POWERS
            | DBF_NO_TEMPLE
            | DBF_SPLIT_IMMORTAL
            | DBF_LABELS);

    // savedtime
    //
    time_t ct;
    time(&ct);
    char *pTime = ctime(&ct);
    if (NULL != pTime)
    {
        char *p = strchr(pTime, '\n');
        if (NULL != p)
        {
            size_t n = p - pTime;
            pTime = StringCloneLen(pTime, n);
            SetSavedTime(pTime);
        }
    }

    // Add Flags
    //
    vector<P6H_FLAGINFO *> *pvFlags= new vector<P6H_FLAGINFO *>;
    for (int i = 0; i < sizeof(upgrade_flags)/sizeof(upgrade_flags[0]); i++)
    {
        P6H_FLAGINFO *pfi = new P6H_FLAGINFO;
        pfi->SetName(StringClone(upgrade_flags[i].pName));
        pfi->SetLetter(StringClone(upgrade_flags[i].pLetter));
        pfi->SetType(StringClone(upgrade_flags[i].pType));
        pfi->SetPerms(StringClone(upgrade_flags[i].pPerms));
        pfi->SetNegatePerms(StringClone(upgrade_flags[i].pNegatePerms));
        pvFlags->push_back(pfi);
    }
    SetFlagList(pvFlags);
    pvFlags = NULL;
    SetFlagCount(sizeof(upgrade_flags)/sizeof(upgrade_flags[0]));

    // Add FlagAliases
    //
    vector<P6H_FLAGALIASINFO *> *pvFlagAliases= new vector<P6H_FLAGALIASINFO *>;
    for (int i = 0; i < sizeof(upgrade_flagaliases)/sizeof(upgrade_flagaliases[0]); i++)
    {
        P6H_FLAGALIASINFO *pfai = new P6H_FLAGALIASINFO;
        pfai->SetName(StringClone(upgrade_flagaliases[i].pName));
        pfai->SetAlias(StringClone(upgrade_flagaliases[i].pAlias));
        pvFlagAliases->push_back(pfai);
    }
    SetFlagAliasList(pvFlagAliases);
    pvFlagAliases = NULL;
    SetFlagAliasCount(sizeof(upgrade_flagaliases)/sizeof(upgrade_flagaliases[0]));

    // Add Powers
    //
    pvFlags= new vector<P6H_FLAGINFO *>;
    for (int i = 0; i < sizeof(upgrade_powers)/sizeof(upgrade_powers[0]); i++)
    {
        P6H_FLAGINFO *pfi = new P6H_FLAGINFO;
        pfi->SetName(StringClone(upgrade_powers[i].pName));
        pfi->SetLetter(StringClone(upgrade_powers[i].pLetter));
        pfi->SetType(StringClone(upgrade_powers[i].pType));
        pfi->SetPerms(StringClone(upgrade_powers[i].pPerms));
        pfi->SetNegatePerms(StringClone(upgrade_powers[i].pNegatePerms));
        pvFlags->push_back(pfi);
    }
    SetPowerList(pvFlags);
    pvFlags = NULL;
    SetPowerCount(sizeof(upgrade_powers)/sizeof(upgrade_powers[0]));

    // Add PowerAliases
    //
    pvFlagAliases= new vector<P6H_FLAGALIASINFO *>;
    for (int i = 0; i < sizeof(upgrade_poweraliases)/sizeof(upgrade_poweraliases[0]); i++)
    {
        P6H_FLAGALIASINFO *pfai = new P6H_FLAGALIASINFO;
        pfai->SetName(StringClone(upgrade_poweraliases[i].pName));
        pfai->SetAlias(StringClone(upgrade_poweraliases[i].pAlias));
        pvFlagAliases->push_back(pfai);
    }
    SetPowerAliasList(pvFlagAliases);
    pvFlagAliases = NULL;
    SetPowerAliasCount(sizeof(upgrade_poweraliases)/sizeof(upgrade_poweraliases[0]));

    // Build internal attribute names.
    //
    map<int, const char *, lti> AttrNames;
    for (int i = 0; i < sizeof(t5x_known_attrs)/sizeof(t5x_known_attrs[0]); i++)
    {
        AttrNames[t5x_known_attrs[i].iNum] = StringClone(t5x_known_attrs[i].pName);
    }
    for (map<int, T5X_ATTRNAMEINFO *, lti>::iterator it = g_t5xgame.m_mAttrNames.begin(); it != g_t5xgame.m_mAttrNames.end(); ++it)
    {
        AttrNames[it->second->m_iNum] = StringClone(it->second->m_pNameUnencoded);
    }

    // Upgrade objects.
    //
    for (map<int, T5X_OBJECTINFO *, lti>::iterator it = g_t5xgame.m_mObjects.begin(); it != g_t5xgame.m_mObjects.end(); ++it)
    {
        if (!it->second->m_fFlags1)
        {
            continue;
        }
        int iType = (it->second->m_iFlags1) & T5X_TYPE_MASK;

        if (  iType < 0
           || 7 < iType)
        {
            continue;
        }

        P6H_OBJECTINFO *poi = new P6H_OBJECTINFO;

        poi->SetRef(it->second->m_dbRef);
        poi->SetName(StringClone(it->second->m_pName));
        poi->SetLocation(it->second->m_dbLocation);
        poi->SetContents(it->second->m_dbContents);

        switch (iType)
        {
        case T5X_TYPE_PLAYER:
        case T5X_TYPE_THING:
            poi->SetExits(it->second->m_dbLink);
            break;

        default:
            poi->SetExits(it->second->m_dbExits);
            break;
        }

        poi->SetNext(it->second->m_dbNext);
        poi->SetParent(it->second->m_dbParent);
        poi->SetOwner(it->second->m_dbOwner);
        poi->SetZone(it->second->m_dbZone);
        poi->SetPennies(it->second->m_iPennies);
        poi->SetType(t5x_convert_type[iType]);

        char aBuffer[1000];
        char *pBuffer = aBuffer;

        // Convert flags1 and flags2.
        //
        bool fFirst = true;
        for (int i = 0; i < sizeof(t5x_convert_obj_flags1)/sizeof(t5x_convert_obj_flags1[0]); i++)
        {
            if (t5x_convert_obj_flags1[i].mask & it->second->m_iFlags1)
            {
                if (!fFirst)
                {
                    *pBuffer++ = ' ';
                }
                fFirst = false;
                strcpy(pBuffer, t5x_convert_obj_flags1[i].pName);
                pBuffer += strlen(t5x_convert_obj_flags1[i].pName);
            }
        }
        for (int i = 0; i < sizeof(t5x_convert_obj_flags2)/sizeof(t5x_convert_obj_flags2[0]); i++)
        {
            if (t5x_convert_obj_flags2[i].mask & it->second->m_iFlags2)
            {
                if (!fFirst)
                {
                    *pBuffer++ = ' ';
                }
                fFirst = false;
                strcpy(pBuffer, t5x_convert_obj_flags2[i].pName);
                pBuffer += strlen(t5x_convert_obj_flags2[i].pName);
            }
        }
        *pBuffer = '\0';
        poi->SetFlags(StringClone(aBuffer));

        // Convert powers.
        //
        fFirst = true;
        pBuffer = aBuffer;
        for (int i = 0; i < sizeof(t5x_convert_obj_powers1)/sizeof(t5x_convert_obj_powers1[0]); i++)
        {
            if (t5x_convert_obj_powers1[i].mask & it->second->m_iPowers1)
            {
                if (!fFirst)
                {
                    *pBuffer++ = ' ';
                }
                fFirst = false;
                strcpy(pBuffer, t5x_convert_obj_powers1[i].pName);
                pBuffer += strlen(t5x_convert_obj_powers1[i].pName);
            }
        }
        for (int i = 0; i < sizeof(t5x_convert_obj_powers2)/sizeof(t5x_convert_obj_powers2[0]); i++)
        {
            if (t5x_convert_obj_powers2[i].mask & it->second->m_iPowers2)
            {
                if (!fFirst)
                {
                    *pBuffer++ = ' ';
                }
                fFirst = false;
                strcpy(pBuffer, t5x_convert_obj_powers2[i].pName);
                pBuffer += strlen(t5x_convert_obj_powers2[i].pName);
            }
        }

        // Immortal power is special.
        //
        if (0x00200000 & it->second->m_iFlags1)
        {
            if (!fFirst)
            {
                *pBuffer++ = ' ';
            }
            fFirst = false;
            strcpy(pBuffer, "Immortal");
            pBuffer += strlen("Immortal");
        }
        *pBuffer = '\0';
        poi->SetPowers(StringClone(aBuffer));

        poi->SetWarnings(StringClone(""));

        if (NULL != it->second->m_pvai)
        {
            vector<P6H_ATTRINFO *> *pvai = new vector<P6H_ATTRINFO *>;
            vector<P6H_LOCKINFO *> *pvli = new vector<P6H_LOCKINFO *>;
            for (vector<T5X_ATTRINFO *>::iterator itAttr = it->second->m_pvai->begin(); itAttr != it->second->m_pvai->end(); ++itAttr)
            {
                if ((*itAttr)->m_fNumAndValue)
                {
                    if ((*itAttr)->m_fIsLock)
                    {
                        const char *pType = NULL;
                        for (int i = 0; i < sizeof(t5x_locknames)/sizeof(t5x_locknames[0]); i++)
                        {
                            if (t5x_locknames[i].iNum == (*itAttr)->m_iNum)
                            {
                                pType = t5x_locknames[i].pName;
                            }
                        }

                        if (  NULL != pType
                           && NULL != (*itAttr)->m_pKeyTree)
                        {
                            P6H_LOCKEXP *ple = new P6H_LOCKEXP;
                            if (ple->ConvertFromT5X((*itAttr)->m_pKeyTree))
                            {
                                char *p = ple->Write(aBuffer);
                                *p = '\0';

                                P6H_LOCKINFO *pli = new P6H_LOCKINFO;
                                pli->SetType(StringClone(pType));
                                pli->SetCreator(it->first);
                                pli->SetFlags(StringClone(""));
                                pli->SetDerefs(0);
                                pli->SetKey(StringClone(aBuffer));

                                pvli->push_back(pli);
                            }
                            else
                            {
                                delete ple;
                                fprintf(stderr, "WARNING: Could not convert '%s' lock on #%d containing '%s'.\n", pType, it->first, (*itAttr)->m_pValueUnencoded);
                            }
                        }
                    }
                    else
                    {
                        if (  T5X_A_CREATED == (*itAttr)->m_iNum
                           || T5X_A_MODIFIED  == (*itAttr)->m_iNum)
                        {
                            time_t t;
                            if (ConvertTimeString((*itAttr)->m_pValueUnencoded, &t))
                            {
                                switch ((*itAttr)->m_iNum)
                                {
                                case T5X_A_CREATED: poi->SetCreated(t); break;
                                case T5X_A_MODIFIED: poi->SetModified(t); break;
                                }
                            }
                        }
                        else
                        {
                            map<int, const char *, lti>::iterator itFound = AttrNames.find((*itAttr)->m_iNum);
                            if (itFound != AttrNames.end())
                            {
                                P6H_ATTRINFO *pai = new P6H_ATTRINFO;
                                pai->SetName(StringClone(itFound->second));
                                pai->SetDerefs(0);
                                char *pValue = (*itAttr)->m_pValueUnencoded;
                                if (T5X_A_PASS == (*itAttr)->m_iNum)
                                {
                                    const char sP6HPrefix[] = "$P6H$$";
                                    if (memcmp(pValue, sP6HPrefix, sizeof(sP6HPrefix)-1) == 0)
                                    {
                                        pValue += sizeof(sP6HPrefix) - 1;
                                    }
                                }
                                pai->SetValue(StringClone(pValue));
                                pai->SetOwner((*itAttr)->m_dbOwner);

                                pBuffer = aBuffer;
                                fFirst = true;
                                for (int i = 0; i < sizeof(t5x_attr_flags)/sizeof(t5x_attr_flags[0]); i++)
                                {
                                    if (t5x_attr_flags[i].mask & (*itAttr)->m_iFlags)
                                    {
                                        if (!fFirst)
                                        {
                                            *pBuffer++ = ' ';
                                        }
                                        fFirst = false;
                                        strcpy(pBuffer, t5x_attr_flags[i].pName);
                                        pBuffer += strlen(t5x_attr_flags[i].pName);
                                    }
                                }
                                *pBuffer = '\0';
                                pai->SetFlags(StringClone(aBuffer));

                                pvai->push_back(pai);
                            }
                        }
                    }
                }
            }
            if (0 < pvai->size())
            {
                poi->SetAttrs(pvai->size(), pvai);
                pvai = NULL;
            }
            if (0 < pvli->size())
            {
                poi->SetLocks(pvli->size(), pvli);
                pvli = NULL;
            }
            delete pvai;
            delete pvli;
        }

        AddObject(poi);
    }

    // Set database size hints.
    //
    SetSizeHint(g_t5xgame.m_mObjects.size());

    // Release memory that we allocated.
    //
    for (map<int, const char *, lti>::iterator it = AttrNames.begin(); it != AttrNames.end(); ++it)
    {
        delete it->second;
    }
}

void P6H_GAME::Extract(FILE *fp, int dbExtract) const
{
    map<int, P6H_OBJECTINFO *, lti>::const_iterator itFound;
    itFound = m_mObjects.find(dbExtract);
    if (itFound == m_mObjects.end())
    {
        fprintf(stderr, "WARNING: Object #%d does not exist in the database.\n", dbExtract);
    }
    else
    {
        itFound->second->Extract(fp);
    }
}

static struct
{
    const char *pFragment;
    size_t      nFragment;
    bool        fColor;
    bool        fEvalOnly;
    const char *pSubstitution;
    size_t      nSubstitution;
    bool        fNeedEval;
} fragments[] =
{
    { "\x1B[0m",      4, true,  false, "n",    1, true  },
    { "\x1B[1m",      4, true,  false, "h",    1, true  },
    { "\x1B[4m",      4, true,  false, "u",    1, true  },
    { "\x1B[5m",      4, true,  false, "f",    1, true  },
    { "\x1B[7m",      4, true,  false, "i",    1, true  },
    { "\x1B[30m",     5, true,  false, "x",    1, true  },
    { "\x1B[31m",     5, true,  false, "r",    1, true  },
    { "\x1B[32m",     5, true,  false, "g",    1, true  },
    { "\x1B[33m",     5, true,  false, "y",    1, true  },
    { "\x1B[34m",     5, true,  false, "b",    1, true  },
    { "\x1B[35m",     5, true,  false, "m",    1, true  },
    { "\x1B[36m",     5, true,  false, "c",    1, true  },
    { "\x1B[37m",     5, true,  false, "w",    1, true  },
    { "\x1B[40m",     5, true,  false, "X",    1, true  },
    { "\x1B[41m",     5, true,  false, "R",    1, true  },
    { "\x1B[42m",     5, true,  false, "G",    1, true  },
    { "\x1B[43m",     5, true,  false, "Y",    1, true  },
    { "\x1B[44m",     5, true,  false, "B",    1, true  },
    { "\x1B[45m",     5, true,  false, "M",    1, true  },
    { "\x1B[46m",     5, true,  false, "C",    1, true  },
    { "\x1B[47m",     5, true,  false, "W",    1, true  },
    { "\t",           1, false, false, "%t",   2, true  },
    { "\r\n",         2, false, false, "%r",   2, true  },
    { "\r",           1, false, false, "",     0, false },
    { "\n",           1, false, false, "",     0, false },
    { "  ",           2, false, false, "%b ",  3, true  },
    { "%",            1, false, true, "\\%",   2, true  },
    { "\\",           1, false, true, "\\\\",  2, true  },
    { "[",            1, false, true, "\\[",   2, true  },
    { "]",            1, false, true, "\\]",   2, true  },
    { "{",            1, false, true, "\\{",   2, true  },
    { "}",            1, false, true, "\\}",   2, true  },
    { ",",            1, false, true, "\\,",   2, true  },
    { "(",            1, false, true, "\\(",   2, true  },
    { "$",            1, false, true, "\\$",   2, true  },
};

static bool ScanForFragment(const char *p, bool fEval, int &iFragment, size_t &nSkip)
{
    if (  NULL == p
       && '\0' == *p)
    {
        nSkip = 0;
        return false;
    }

    for (int i = 0; i < sizeof(fragments)/sizeof(fragments[0]); i++)
    {
        if (  (  !fragments[i].fEvalOnly
              || fEval)
           && strncmp(p, fragments[i].pFragment, fragments[i].nFragment) == 0)
        {
            iFragment = i;
            return true;
        }
    }
    const char *q = p + 1;
    for ( ; '\0' != *q; q++)
    {
        bool fFound = false;
        for (int i = 0; i < sizeof(fragments)/sizeof(fragments[0]); i++)
        {
            if (*q == fragments[i].pFragment[0])
            {
                fFound = true;
                break;
            }
        }
        if (fFound)
        {
            break;
        }
    }
    nSkip = q - p;
    return false;
}

static char *EncodeSubstitutions(char *pValue, bool &fNeedEval)
{
    static char buffer[65536];
    char *q = buffer;
    char *p = pValue;
    bool fEval = false;

    bool aCodes[sizeof(fragments)/sizeof(fragments[0])];
    bool fInColor = false;
    char temp[65536];
    char *qsave;

    while (  '\0' != *p
          && q < ((fInColor)?(temp + sizeof(temp) - 1):(buffer + sizeof(buffer) - 1)))
    {
        int iFragment;
        size_t nSkip;
        if (ScanForFragment(p, fEval, iFragment, nSkip))
        {
            if (  !fEval
               && fragments[iFragment].fNeedEval)
            {
                fEval = true;
                fInColor = false;
                p = pValue;
                q = buffer;
            }
            else
            {
                size_t nskp = fragments[iFragment].nFragment;
                if (fragments[iFragment].fColor)
                {
                    if ('n' == fragments[iFragment].pSubstitution[0])
                    {
                        if (fInColor)
                        {
                            size_t n = q - temp;
                            q = qsave;
                            if (0 < n)
                            {
                                memcpy(q, "[ansi(", 6);
                                q += 6;
                                for (int i = 0; i < sizeof(fragments)/sizeof(fragments[0]); i++)
                                {
                                    if (aCodes[i])
                                    {
                                        *q++ = fragments[i].pSubstitution[0];
                                    }
                                }
                                *q++ = ',';
                                memcpy(q, temp, n);
                                q += n;
                                memcpy(q, ")]", 2);
                                q += 2;
                            }
                            fInColor = false;
                        }
                    }
                    else
                    {
                        if (!fInColor)
                        {
                            qsave = q;
                            q = temp;
                            fInColor = true;
                            for (int i = 0; i < sizeof(fragments)/sizeof(fragments[0]); i++)
                            {
                                aCodes[i] = false;
                            }
                        }
                        aCodes[iFragment] = true;
                    }
                }
                else
                {
                    size_t ncpy = fragments[iFragment].nSubstitution;
                    if (q + ncpy < ((fInColor)?(temp + sizeof(temp) - 1):(buffer + sizeof(buffer) - 1)))
                    {
                        memcpy(q, fragments[iFragment].pSubstitution, ncpy);
                        q += ncpy;
                    }
                }
                p += nskp;
            }
        }
        else
        {
            if (q + nSkip < ((fInColor)?(temp + sizeof(temp) - 1):(buffer + sizeof(buffer) - 1)))
            {
                memcpy(q, p, nSkip);
                q += nSkip;
            }
            p += nSkip;
        }
    }
    if (  fInColor
       && q != temp)
    {
        size_t n = q - temp;
        q = qsave;
        if (0 < n)
        {
            memcpy(q, "[ansi(", 6);
            q += 6;
            for (int i = 0; i < sizeof(fragments)/sizeof(fragments[0]); i++)
            {
                if (aCodes[i])
                {
                    *q++ = fragments[i].pSubstitution[0];
                }
            }
            *q++ = ',';
            memcpy(q, temp, n);
            q += n;
            memcpy(q, ")]", 2);
            q += 2;
        }
        fInColor = false;
    }
    *q = '\0';
    fNeedEval = fEval;
    return buffer;
}

static char *StripColor(char *pValue)
{
    static char buffer[65536];
    char *q = buffer;
    char *p = pValue;
    bool fEval = false;

    while (  '\0' != *p
          && q < buffer + sizeof(buffer) - 1)
    {
        int iFragment;
        size_t nSkip;
        if (ScanForFragment(p, fEval, iFragment, nSkip))
        {
            size_t nskp = fragments[iFragment].nFragment;
            if (fragments[iFragment].fColor)
            {
                size_t ncpy = fragments[iFragment].nSubstitution;
                if (q + ncpy < buffer + sizeof(buffer) - 1)
                {
                    memcpy(q, fragments[iFragment].pSubstitution, ncpy);
                    q += ncpy;
                }
            }
            p += nskp;
        }
        else
        {
            if (q + nSkip < buffer + sizeof(buffer) - 1)
            {
                memcpy(q, p, nSkip);
                q += nSkip;
            }
            p += nSkip;
        }
    }
    *q = '\0';
    return buffer;
}

void P6H_OBJECTINFO::Extract(FILE *fp) const
{
    fprintf(fp, "@@ Extracting #%d\n", m_dbRef);
    fprintf(fp, "@@ encoding is latin-1\n");
    if (NULL != m_pName)
    {
        bool fNeedEval;
        fprintf(fp, "@@ %s\n", EncodeSubstitutions(m_pName, fNeedEval));
    }
    char *pStrippedObjName = StringClone(StripColor(m_pName));

    // Object flags.
    //
    if (NULL != m_pFlags)
    {
        fprintf(fp, "@set %s=%s\n", pStrippedObjName, m_pFlags);
    }
    if (m_fFlags)
    {
        fprintf(fp, "@@ Omega doesn't support extracting object flags from older flatfiles versions, yet.\n");
    }

    // Object powers.
    //
    if (NULL != m_pPowers)
    {
        fprintf(fp, "@power %s=%s\n", pStrippedObjName, m_pPowers);
    }

    // Attribute values.
    //
    if (NULL != m_pvai)
    {
        for (vector<P6H_ATTRINFO *>::iterator it = m_pvai->begin(); it != m_pvai->end(); ++it)
        {
            (*it)->Extract(fp, pStrippedObjName);
        }
    }
    if (NULL != m_pvli)
    {
        for (vector<P6H_LOCKINFO *>::iterator it = m_pvli->begin(); it != m_pvli->end(); ++it)
        {
            (*it)->Extract(fp, pStrippedObjName);
        }
    }
    free(pStrippedObjName);
}

void P6H_ATTRINFO::Extract(FILE *fp, char *pObjName) const
{
    if (NULL != m_pName)
    {
        if (NULL != m_pValue)
        {
            bool fNeedEval;
            const char *p = EncodeSubstitutions(m_pValue, fNeedEval);
            if (fNeedEval)
            {
                fprintf(fp, "@set %s=%s:%s\n", pObjName, m_pName, p);
            }
            else
            {
                fprintf(fp, "&%s %s=%s\n", m_pName, pObjName, p);
            }
        }
        else
        {
            fprintf(fp, "&%s %s=\n", m_pName, pObjName);
        }
        if (  NULL != m_pFlags
           && '\0' != m_pFlags[0])
        {
            fprintf(fp, "@set %s/%s=%s\n", pObjName, m_pName, m_pFlags);
        }
    }
}

void P6H_LOCKINFO::Extract(FILE *fp, char *pObjName) const
{
    if (  NULL != m_pType
       && NULL != m_pKey)
    {
        bool fNeedEval;
        const char *p = EncodeSubstitutions(m_pKey, fNeedEval);
        if (fNeedEval)
        {
            fprintf(fp, "@wait 0={@lock/%s %s=%s}\n", m_pType, pObjName, p);
        }
        else
        {
            fprintf(fp, "@lock/%s %s=%s\n", m_pType, pObjName, p);
        }
        if (  NULL != m_pFlags
           && '\0' != *m_pFlags)
        {
            if (fNeedEval)
            {
                fprintf(fp, "@wait 0={@lset %s/%s=%s}\n", pObjName, m_pType, m_pFlags);
            }
            else
            {
                fprintf(fp, "@lset %s/%s=%s\n", pObjName, m_pType, m_pFlags);
            }
        }
    }
}
