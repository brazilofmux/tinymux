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
};
#define P6H_NUM_GAMEFLAGNAMES (sizeof(p6h_gameflagnames)/sizeof(p6h_gameflagnames[0]))

P6H_GAME g_p6hgame;

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
        pfai->m_pName = NULL;;
    }
    if (NULL != pfai->m_pAlias && NULL == m_pAlias)
    {
        m_pAlias = pfai->m_pAlias;
        pfai->m_pAlias = NULL;;
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
}

void P6H_LOCKINFO::Merge(P6H_LOCKINFO *pli)
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
    m_vObjects.push_back(poi);
}

void P6H_GAME::ValidateFlags()
{
    int flags = m_flags;
    bool f177p40 = ((flags & DBF_LABELS) == DBF_LABELS);
    if (f177p40)
    {
        fprintf(stderr, "INFO: Flatfile produced by 1.7.7p40 or later.\n");
    }
    else
    {
        fprintf(stderr, "INFO: Flatfile predates PennMUSH 1.7.7p40\n");
    }

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
                         | DBF_SPIFFY_LOCKS;

    const int iMand177p40 = DBF_NEW_FLAGS
                          | DBF_NEW_POWERS
                          | DBF_POWERS_LOGGED
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
        }
        *p++ = *str++;
    }
    *p = '\0';
    return buf;
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
            fprintf(fp, "lockcount %d\n", m_pvli->size());
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
    if (m_fAttrCount)
    {
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
                fprintf(fp, "attrcount %d\n", m_pvai->size());
            }
            for (vector<P6H_ATTRINFO *>::iterator it = m_pvai->begin(); it != m_pvai->end(); ++it)
            {
                (*it)->Write(fp, fLabels);
            }
        }
    }
    if (!fLabels)
    {
        fprintf(fp, "<\n");
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
        if (NULL != m_pLetter)
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
            && m_fFlags
            && m_fDerefs
            && NULL != m_pValue)
    {
        fprintf(fp, "]%s^%d^%d\n\"%s\"\n", m_pName, m_iFlags, m_iDerefs, EncodeString(m_pValue));
    }
}

void P6H_GAME::Write(FILE *fp)
{
    fprintf(fp, "+V%d\n", (m_flags + 5) * 256 + 2);
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
    if (m_fObjects)
    {
        fprintf(fp, "~%d\n", m_nObjects);
    }
    bool fLabels = ((m_flags & DBF_LABELS) == DBF_LABELS);
    for (vector<P6H_OBJECTINFO *>::iterator it = m_vObjects.begin(); it != m_vObjects.end(); ++it)
    {
        (*it)->Write(fp, fLabels);
    } 

    fprintf(fp, "***END OF DUMP***\n");
}

