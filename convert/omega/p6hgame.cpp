#include "omega.h"
#include "p6hgame.h"

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
};
#define P6H_NUM_GAMEFLAGNAMES (sizeof(p6h_gameflagnames)/sizeof(p6h_gameflagnames[0]))

P6H_GAME g_p6hgame;

FLAGINFO::FLAGINFO()
{
    m_pName = NULL;
    m_pLetter = NULL;
    m_pType = NULL;
    m_pPerms = NULL;
    m_pNegatePerms = NULL;
}

FLAGINFO::~FLAGINFO()
{
    if (NULL != m_pName)
    {
        free(m_pName);
        m_pName = NULL;
    }
    if (NULL != m_pLetter)
    {
        free(m_pLetter);
        m_pLetter = NULL;
    }
    if (NULL != m_pType)
    {
        free(m_pType);
        m_pType = NULL;
    }
    if (NULL != m_pPerms)
    {
        free(m_pPerms);
        m_pPerms = NULL;
    }
    if (NULL != m_pNegatePerms)
    {
        free(m_pNegatePerms);
        m_pNegatePerms = NULL;
    }
}

void FLAGINFO::SetName(char *p)
{
    if (NULL != m_pName)
    {
        free(m_pName);
    }
    m_pName = p;
}

void FLAGINFO::SetLetter(char *p)
{
    if (NULL != m_pLetter)
    {
        free(m_pLetter);
    }
    m_pLetter = p;
}

void FLAGINFO::SetType(char *p)
{
    if (NULL != m_pType)
    {
        free(m_pType);
    }
    m_pType = p;
}

void FLAGINFO::SetPerms(char *p)
{
    if (NULL != m_pPerms)
    {
        free(m_pPerms);
    }
    m_pPerms = p;
}

void FLAGINFO::SetNegatePerms(char *p)
{
    if (NULL != m_pNegatePerms)
    {
        free(m_pNegatePerms);
    }
    m_pNegatePerms = p;
}

void FLAGINFO::Merge(FLAGINFO *pfi)
{
    if (NULL != pfi->m_pName && NULL == m_pName)
    {
        m_pName = pfi->m_pName;
        pfi->m_pName = NULL;;
    }
    if (NULL != pfi->m_pLetter && NULL == m_pLetter)
    {
        m_pLetter = pfi->m_pLetter;
        pfi->m_pLetter = NULL;;
    }
    if (NULL != pfi->m_pType && NULL == m_pType)
    {
        m_pType = pfi->m_pType;
        pfi->m_pType = NULL;;
    }
    if (NULL != pfi->m_pPerms && NULL == m_pPerms)
    {
        m_pPerms = pfi->m_pPerms;
        pfi->m_pPerms = NULL;;
    }
    if (NULL != pfi->m_pNegatePerms && NULL == m_pNegatePerms)
    {
        m_pNegatePerms = pfi->m_pNegatePerms;
        pfi->m_pNegatePerms = NULL;;
    }
}

FLAGALIASINFO::FLAGALIASINFO()
{
    m_pName = NULL;
    m_pAlias = NULL;
}

FLAGALIASINFO::~FLAGALIASINFO()
{
    if (NULL != m_pName)
    {
        free(m_pName);
        m_pName = NULL;
    }
    if (NULL != m_pAlias)
    {
        free(m_pAlias);
        m_pAlias = NULL;
    }
}

void FLAGALIASINFO::SetName(char *p)
{
    if (NULL != m_pName)
    {
        free(m_pName);
    }
    m_pName = p;
}

void FLAGALIASINFO::SetAlias(char *p)
{
    if (NULL != m_pAlias)
    {
        free(m_pAlias);
    }
    m_pAlias = p;
}

void FLAGALIASINFO::Merge(FLAGALIASINFO *pfai)
{
    if (NULL != pfai->m_pName && NULL == m_pName)
    {
        m_pName = pfai->m_pName;
        pfai->m_pName = NULL;;
    }
    if (NULL != pfai->m_pAlias && NULL == m_pAlias)
    {
        m_pAlias = pfai->m_pAlias;
        pfai->m_pAlias = NULL;;
    }
}

void OBJECTINFO::SetName(char *pName)
{
    if (NULL != m_pName)
    {
        free(m_pName);
    }
    m_pName = pName;
}

void OBJECTINFO::SetLocks(int nLocks, vector<LOCKINFO *> *pvli)
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

void OBJECTINFO::SetFlags(char *pFlags)
{
    if (NULL != m_pFlags)
    {
        free(m_pFlags);
    }
    m_pFlags = pFlags;
}

void OBJECTINFO::SetPowers(char *pPowers)
{
    if (NULL != m_pPowers)
    {
        free(m_pPowers);
    }
    m_pPowers = pPowers;
}

void OBJECTINFO::SetWarnings(char *pWarnings)
{
    if (NULL != m_pWarnings)
    {
        free(m_pWarnings);
    }
    m_pWarnings = pWarnings;
}

void OBJECTINFO::SetAttrs(int nAttrs, vector<ATTRINFO *> *pvai)
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

void OBJECTINFO::Merge(OBJECTINFO *poi)
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
        poi->m_pFlags = NULL;;
    }
    if (NULL != poi->m_pPowers && NULL == m_pPowers)
    {
        m_pPowers = poi->m_pPowers;
        poi->m_pPowers = NULL;;
    }
    if (NULL != poi->m_pWarnings && NULL == m_pWarnings)
    {
        m_pWarnings = poi->m_pWarnings;
        poi->m_pWarnings = NULL;;
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

OBJECTINFO::~OBJECTINFO()
{
    delete m_pName;
    delete m_pFlags;
    delete m_pPowers;
    delete m_pWarnings;
    delete m_pvli;
    delete m_pvai;
}

void LOCKINFO::SetType(char *pType)
{
    if (NULL != m_pType)
    {
        free(m_pType);
    }
    m_pType = pType;
}

void LOCKINFO::SetFlags(char *pFlags)
{
    if (NULL != m_pFlags)
    {
        free(m_pFlags);
    }
    m_pFlags = pFlags;
}

void LOCKINFO::SetKey(char *pKey)
{
    if (NULL != m_pKey)
    {
        free(m_pKey);
    }
    m_pKey = pKey;
}

void LOCKINFO::Merge(LOCKINFO *pli)
{
    if (NULL != pli->m_pType && NULL == m_pType)
    {
        m_pType = pli->m_pType;
        pli->m_pType = NULL;;
    }
    if (pli->m_fCreator && !m_fCreator)
    {
        m_fCreator = true;
        m_dbCreator = pli->m_dbCreator;
    }
    if (NULL != pli->m_pFlags && NULL == m_pFlags)
    {
        m_pFlags = pli->m_pFlags;
        pli->m_pFlags = NULL;;
    }
    if (pli->m_fDerefs && !m_fDerefs)
    {
        m_fDerefs = true;
        m_iDerefs = pli->m_iDerefs;
    }
    if (NULL != pli->m_pKey && NULL == m_pKey)
    {
        m_pKey = pli->m_pKey;
        pli->m_pKey = NULL;;
    }
    if (pli->m_fFlags && !m_fFlags)
    {
        m_fFlags = true;
        m_iFlags = pli->m_iFlags;
    }
}

LOCKINFO::~LOCKINFO()
{
    delete m_pType;
    delete m_pFlags;
    delete m_pKey;
}

void ATTRINFO::SetName(char *pName)
{
    if (NULL != m_pName)
    {
        free(m_pName);
    }
    m_pName = pName;
}

void ATTRINFO::SetFlags(char *pFlags)
{
    if (NULL != m_pFlags)
    {
        free(m_pFlags);
    }
    m_pFlags = pFlags;
}

void ATTRINFO::SetValue(char *pValue)
{
    if (NULL != m_pValue)
    {
        free(m_pValue);
    }
    m_pValue = pValue;
}

void ATTRINFO::Merge(ATTRINFO *pai)
{
    if (NULL != pai->m_pName && NULL == m_pName)
    {
        m_pName = pai->m_pName;
        pai->m_pName = NULL;;
    }
    if (pai->m_fOwner && !m_fOwner)
    {
        m_fOwner = true;
        m_dbOwner = pai->m_dbOwner;
    }
    if (NULL != pai->m_pFlags && NULL == m_pFlags)
    {
        m_pFlags = pai->m_pFlags;
        pai->m_pFlags = NULL;;
    }
    if (pai->m_fDerefs && !m_fDerefs)
    {
        m_fDerefs = true;
        m_iDerefs = pai->m_iDerefs;
    }
    if (NULL != pai->m_pValue && NULL == m_pValue)
    {
        m_pValue = pai->m_pValue;
        pai->m_pValue = NULL;;
    }
    if (pai->m_fFlags && !m_fFlags)
    {
        m_fFlags = true;
        m_iFlags = pai->m_iFlags;
    }
}

ATTRINFO::~ATTRINFO()
{
    delete m_pName;
    delete m_pFlags;
    delete m_pValue;
}

P6H_GAME::P6H_GAME()
{
    m_flags = 0;
    m_pSavedTime = NULL;
    m_fObjects = false;
    m_nObjects = 0;
    m_fFlags = false;
    m_nFlags = 0;
    m_pvFlags = NULL;
    m_fFlagAliases = false;
    m_nFlagAliases = 0;
    m_fPowers = false;
    m_nPowers = 0;
    m_pvPowers = NULL;
    m_fPowerAliases = false;
    m_nPowerAliases = 0;
    m_pvPowerAliases = NULL;
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

void P6H_GAME::AddObject(OBJECTINFO *poi)
{
    m_vObjects.push_back(poi);
}

void P6H_GAME::ValidateFlags()
{
    int flags = m_flags;
    fprintf(stderr, "Flags: ", flags);

    for (int i = 0; i < P6H_NUM_GAMEFLAGNAMES; i++)
    {
        if (p6h_gameflagnames[i].mask & flags)
        {
            fprintf(stderr, "%s ", p6h_gameflagnames[i].pName);
            flags &= ~p6h_gameflagnames[i].mask;
        }
    }
    fprintf(stderr, "\n");

    if (0 != flags)
    {
        fprintf(stderr, "Unknown flags: 0x%x\n", flags);
        exit(1);
    }
}

void P6H_GAME::ValidateSavedTime()
{
}

void P6H_GAME::Validate()
{
    ValidateFlags();
    if (NULL != m_pSavedTime)
    {
        ValidateSavedTime();
    }
}

char *EncodeString(const char *str)
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
        }
        *p++ = *str++;
    }
    *p = '\0';
    return buf;
}

void P6H_GAME::WriteObject(const OBJECTINFO &oi)
{
    printf("!%d\n", oi.m_dbRef);
    if (NULL != oi.m_pName)
    {
        printf("name \"%s\"\n", EncodeString(oi.m_pName));
    }
    if (oi.m_fLocation)
    {
        printf("location #%d\n", oi.m_dbLocation);
    }
    if (oi.m_fContents)
    {
        printf("contents #%d\n", oi.m_dbContents);
    }
    if (oi.m_fExits)
    {
        printf("exits #%d\n", oi.m_dbExits);
    }
    if (oi.m_fNext)
    {
        printf("next #%d\n", oi.m_dbNext);
    }
    if (oi.m_fParent)
    {
        printf("parent #%d\n", oi.m_dbParent);
    }

    if (oi.m_fLockCount)
    {
        if (NULL == oi.m_pvli)
        {
            printf("lockcount 0\n");
        }
        else
        {
            printf("lockcount %d\n", oi.m_pvli->size());
            for (vector<LOCKINFO *>::iterator it = oi.m_pvli->begin(); it != oi.m_pvli->end(); ++it)
            {
                oi.WriteLock(**it);
            }
        }
    }
    if (oi.m_fOwner)
    {
        printf("owner #%d\n", oi.m_dbOwner);
    }
    if (oi.m_fZone)
    {
        printf("zone #%d\n", oi.m_dbZone);
    }
    if (oi.m_fPennies)
    {
        printf("pennies %d\n", oi.m_iPennies);
    }
    if (oi.m_fType)
    {
        printf("type %d\n", oi.m_iType);
    }
    if (NULL != oi.m_pFlags)
    {
        printf("flags \"%s\"\n", EncodeString(oi.m_pFlags));
    }
    if (NULL != oi.m_pPowers)
    {
        printf("powers \"%s\"\n", EncodeString(oi.m_pPowers));
    }
    if (NULL != oi.m_pWarnings)
    {
        printf("warnings \"%s\"\n", EncodeString(oi.m_pWarnings));
    }
    if (oi.m_fCreated)
    {
        printf("created %d\n", oi.m_iCreated);
    }
    if (oi.m_fModified)
    {
        printf("modified %d\n", oi.m_iModified);
    }
    if (oi.m_fAttrCount)
    {
        if (NULL == oi.m_pvai)
        {
            printf("attrcount 0\n");
        }
        else
        {
            printf("attrcount %d\n", oi.m_pvai->size());
            for (vector<ATTRINFO *>::iterator it = oi.m_pvai->begin(); it != oi.m_pvai->end(); ++it)
            {
                oi.WriteAttr(**it);
            }
        }
    }
}

void P6H_GAME::WriteFlag(const FLAGINFO &fi)
{
    if (NULL != fi.m_pName)
    {
        printf(" name \"%s\"\n", EncodeString(fi.m_pName));
        if (NULL != fi.m_pLetter)
        {
            printf("  letter \"%s\"\n", EncodeString(fi.m_pLetter));
        }
        if (NULL != fi.m_pType)
        {
            printf("  type \"%s\"\n", EncodeString(fi.m_pType));
        }
        if (NULL != fi.m_pLetter)
        {
            printf("  perms \"%s\"\n", EncodeString(fi.m_pPerms));
        }
        if (NULL != fi.m_pNegatePerms)
        {
            printf("  negate_perms \"%s\"\n", EncodeString(fi.m_pNegatePerms));
        }
    }
}

void P6H_GAME::WriteFlagAlias(const FLAGALIASINFO &fai)
{
    if (NULL != fai.m_pName)
    {
        printf(" name \"%s\"\n", EncodeString(fai.m_pName));
        if (NULL != fai.m_pAlias)
        {
            printf("  alias \"%s\"\n", EncodeString(fai.m_pAlias));
        }
    }
}

void OBJECTINFO::WriteLock(const LOCKINFO &li) const
{
    if (NULL != li.m_pType)
    {
        printf(" type \"%s\"\n", EncodeString(li.m_pType));
        if (li.m_fCreator)
        {
            printf("  creator #%d\n", li.m_dbCreator);
        }
        if (NULL != li.m_pFlags)
        {
            printf("  flags \"%s\"\n", EncodeString(li.m_pFlags));
        }
        if (li.m_fFlags)
        {
            printf("  flags %d\n", li.m_iFlags);
        }
        if (li.m_fDerefs)
        {
            printf("  derefs %d\n", li.m_iDerefs);
        }
        if (NULL != li.m_pKey)
        {
            printf("  key \"%s\"\n", EncodeString(li.m_pKey));
        }
    }
}

void OBJECTINFO::WriteAttr(const ATTRINFO &ai) const
{
    if (NULL != ai.m_pName)
    {
        printf(" name \"%s\"\n", EncodeString(ai.m_pName));
        if (ai.m_fOwner)
        {
            printf("  owner #%d\n", ai.m_dbOwner);
        }
        if (NULL != ai.m_pFlags)
        {
            printf("  flags \"%s\"\n", EncodeString(ai.m_pFlags));
        }
        if (ai.m_fFlags)
        {
            printf("  flags %d\n", ai.m_iFlags);
        }
        if (ai.m_fDerefs)
        {
            printf("  derefs %d\n", ai.m_iDerefs);
        }
        if (NULL != ai.m_pValue)
        {
            printf("  value \"%s\"\n", EncodeString(ai.m_pValue));
        }
    }
}

void P6H_GAME::Write(FILE *fp)
{
    printf("+V%d\n", (m_flags + 5) * 256 + 2);
    if (NULL != m_pSavedTime)
    {
        printf("savedtime \"%s\"\n", m_pSavedTime);
    }
    if (m_fFlags)
    {
        printf("+FLAGS LIST\nflagcount %d\n", m_nFlags);
    }
    if (NULL != m_pvFlags)
    {
        for (vector<FLAGINFO *>::iterator it = m_pvFlags->begin(); it != m_pvFlags->end(); ++it)
        {
            WriteFlag(**it);
        }
    }
    if (m_fFlagAliases)
    {
        printf("flagaliascount %d\n", m_nFlagAliases);
    }
    if (NULL != m_pvFlagAliases)
    {
        for (vector<FLAGALIASINFO *>::iterator it = m_pvFlagAliases->begin(); it != m_pvFlagAliases->end(); ++it)
        {
            WriteFlagAlias(**it);
        }
    }
    if (m_fPowers)
    {
        printf("+POWER LIST\nflagcount %d\n", m_nPowers);
    }
    if (NULL != m_pvPowers)
    {
        for (vector<FLAGINFO *>::iterator it = m_pvPowers->begin(); it != m_pvPowers->end(); ++it)
        {
            WriteFlag(**it);
        }
    }
    if (m_fPowerAliases)
    {
        printf("flagaliascount %d\n", m_nPowerAliases);
    }
    if (NULL != m_pvPowerAliases)
    {
        for (vector<FLAGALIASINFO *>::iterator it = m_pvPowerAliases->begin(); it != m_pvPowerAliases->end(); ++it)
        {
            WriteFlagAlias(**it);
        }
    }
    if (m_fObjects)
    {
        printf("~%d\n", m_nObjects);
    }
    for (vector<OBJECTINFO *>::iterator it = m_vObjects.begin(); it != m_vObjects.end(); ++it)
    {
        WriteObject(**it);
    } 

    printf("***END OF DUMP***\n");
}

