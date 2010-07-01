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
    if (m_fFlags || NULL != m_pvFlags)
    {
        if (!m_fFlags)
        {
            fprintf(stderr, "WARNING: +FLAG LIST flagcount missing when list of flags is present.\n");
        }
        if (NULL == m_pvFlags)
        {
            fprintf(stderr, "WARNING: +FLAG LIST list of flags is missing then flagcount is present.\n");
        }
        if (m_fFlags && NULL != m_pvFlags)
        {
            if (m_nFlags != m_pvFlags->size())
            {
                fprintf(stderr, "WARNING: flag count (%d) does not agree with flagcount (%d) in +FLAG LIST\n", m_pvFlags->size(), m_nFlags);
            }
        }
        for (vector<P6H_FLAGINFO *>::iterator it = m_pvFlags->begin(); it != m_pvFlags->end(); ++it)
        {
            (*it)->Validate();
        }
    }
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
    if (!fLabels)
    {
        fprintf(fp, "<\n");
    }
}

typedef struct
{
    const char *pName;
    const int  mask;
} NameMask;

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
                nnm = sizeof(upgrade_obj_toggles_room)/sizeof(upgrade_obj_toggles_room[0]);;
                break;

            case P6H_OLD_TYPE_THING:
                pnm = upgrade_obj_toggles_thing;
                nnm = sizeof(upgrade_obj_toggles_thing)/sizeof(upgrade_obj_toggles_thing[0]);;
                break;

            case P6H_OLD_TYPE_EXIT:
                pnm = upgrade_obj_toggles_exit;
                nnm = sizeof(upgrade_obj_toggles_exit)/sizeof(upgrade_obj_toggles_exit[0]);;
                break;

            case P6H_OLD_TYPE_PLAYER:
                pnm = upgrade_obj_toggles_player;
                nnm = sizeof(upgrade_obj_toggles_player)/sizeof(upgrade_obj_toggles_player[0]);;
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

void P6H_FLAGINFO::Validate()
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
    if (m_fSizeHint)
    {
        fprintf(fp, "~%d\n", m_nSizeHint);
    }
    bool fLabels = ((m_flags & DBF_LABELS) == DBF_LABELS);
    for (vector<P6H_OBJECTINFO *>::iterator it = m_vObjects.begin(); it != m_vObjects.end(); ++it)
    {
        (*it)->Write(fp, fLabels);
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
    // Addition flatfile flags.
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
    for (vector<P6H_OBJECTINFO *>::iterator it = m_vObjects.begin(); it != m_vObjects.end(); ++it)
    {
        (*it)->Upgrade();
    }
}

int t5x_convert_type[8] =
{
    T5X_TYPE_ROOM,
    T5X_TYPE_THING,
    T5X_TYPE_EXIT,
    T5X_TYPE_PLAYER,
    T5X_NOTYPE,
    T5X_NOTYPE,
    T5X_TYPE_GARBAGE,
    T5X_NOTYPE,
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

struct ltstr
{
    bool operator()(const char* s1, const char* s2) const
    {
        return strcmp(s1, s2) < 0;
    }
};

void P6H_GAME::ConvertT5X()
{
    g_t5xgame.SetFlags(MANDFLAGS_V2 | 2);

    // Build set of attribute names.
    //
    int iNextAttr = A_USER_START;
    map<const char *, int, ltstr> AttrNames;
    for (vector<P6H_OBJECTINFO *>::iterator itObj = m_vObjects.begin(); itObj != m_vObjects.end(); ++itObj)
    {
        if (NULL != (*itObj)->m_pvai)
        {
            for (vector<P6H_ATTRINFO *>::iterator itAttr = (*itObj)->m_pvai->begin(); itAttr != (*itObj)->m_pvai->end(); ++itAttr)
            {
                if (NULL != (*itAttr)->m_pName)
                {
                    char *pAttrName = t5x_ConvertAttributeName((*itAttr)->m_pName);
                    if (AttrNames.find(pAttrName) == AttrNames.end())
                    {
                        AttrNames[pAttrName] = iNextAttr++;
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
        g_t5xgame.AddNumAndName(it->second, StringClone(buffer));
        delete it->first;
    }
    g_t5xgame.SetNextAttr(iNextAttr);

    int dbRefMax = 0;
    for (vector<P6H_OBJECTINFO *>::iterator it = m_vObjects.begin(); it != m_vObjects.end(); ++it)
    {
        T5X_OBJECTINFO *poi = new T5X_OBJECTINFO;
        poi->SetRef((*it)->m_dbRef);
        poi->SetName(StringClone((*it)->m_pName));
        if ((*it)->m_fLocation)
        {
            poi->SetLocation((*it)->m_dbLocation);
        }
        if ((*it)->m_fContents)
        {
            poi->SetContents((*it)->m_dbContents);
        }
        if ((*it)->m_fExits)
        {
            poi->SetExits((*it)->m_dbExits);
            poi->SetLink((*it)->m_dbExits);

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
        int flags1 = 0;
        int flags2 = 0;
        int flags3 = 0;
        if ((*it)->m_fType)
        {
            flags1 |= t5x_convert_type[(*it)->m_iType];
        }
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

        g_t5xgame.AddObject(poi);

        if (dbRefMax < (*it)->m_dbRef)
        {
            dbRefMax = (*it)->m_dbRef;
        }
    }
    g_t5xgame.SetSizeHint(dbRefMax);
    g_t5xgame.SetRecordPlayers(0);
}
