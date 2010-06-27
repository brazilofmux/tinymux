#ifndef _P6HGAME_H_
#define _p6HGAME_H_

class FLAGINFO
{
public:
    char *m_pName;
    void SetName(char *p);

    char *m_pLetter;
    void SetLetter(char *p);

    char *m_pType;
    void SetType(char *p);

    char *m_pPerms;
    void SetPerms(char *p);

    char *m_pNegatePerms;
    void SetNegatePerms(char *p);

    void Merge(FLAGINFO *pfi);

    FLAGINFO();
    ~FLAGINFO();
};

class FLAGALIASINFO
{
public:
    char *m_pName;
    void SetName(char *p);

    char *m_pAlias;
    void SetAlias(char *p);

    void Merge(FLAGALIASINFO *pfi);

    FLAGALIASINFO();
    ~FLAGALIASINFO();
};

class LOCKINFO
{
public:
    char *m_pType;
    void SetType(char *p);

    bool m_fCreator;
    int  m_dbCreator;
    void SetCreator(int dbCreator) { m_fCreator = true; m_dbCreator = dbCreator; }

    char *m_pFlags;
    void SetFlags(char *pFlags);

    bool m_fDerefs;
    int  m_iDerefs;
    void SetDerefs(int iDerefs) { m_fDerefs = true; m_iDerefs = iDerefs; }

    char *m_pKey;
    void SetKey(char *pKey);

    bool m_fFlags;
    int  m_iFlags;
    void SetFlags(int iFlags)  { m_fFlags = true; m_iFlags = iFlags; }

    void Merge(LOCKINFO *pli);

    LOCKINFO()
    {
        m_pType = NULL;
        m_fCreator = false;
        m_pFlags = NULL;
        m_fDerefs = false;
        m_pKey = NULL;
        m_fFlags = false;
    }
    ~LOCKINFO();
};

class ATTRINFO
{
public:
    char *m_pName;
    void SetName(char *p);

    bool m_fOwner;
    int  m_dbOwner;
    void SetOwner(int dbOwner) { m_fOwner = true; m_dbOwner = dbOwner; }

    char *m_pFlags;
    void SetFlags(char *pFlags);

    bool m_fDerefs;
    int  m_iDerefs;
    void SetDerefs(int iDerefs) { m_fDerefs = true; m_iDerefs = iDerefs; }

    char *m_pValue;
    void SetValue(char *pValue);

    bool m_fFlags;
    int  m_iFlags;
    void SetFlags(int iFlags)  { m_fFlags = true; m_iFlags = iFlags; }

    void Merge(ATTRINFO *pai);

    ATTRINFO()
    {
        m_pName = NULL;
        m_fOwner = false;
        m_pFlags = NULL;
        m_fDerefs = false;
        m_pValue = NULL;
        m_fFlags = false;
    }
    ~ATTRINFO();
};

class OBJECTINFO
{
public:
    bool m_fRef;
    int  m_dbRef;
    void SetRef(int dbRef) { m_fRef = true; m_dbRef = dbRef; }

    char *m_pName;
    void SetName(char *p);

    bool m_fLocation;
    int  m_dbLocation;
    void SetLocation(int dbLocation) { m_fLocation = true; m_dbLocation = dbLocation; }

    bool m_fContents;
    int  m_dbContents;
    void SetContents(int dbContents) { m_fContents = true; m_dbContents = dbContents; }

    bool m_fExits;
    int  m_dbExits;
    void SetExits(int dbExits) { m_fExits = true; m_dbExits = dbExits; }

    bool m_fNext;
    int  m_dbNext;
    void SetNext(int dbNext) { m_fNext = true; m_dbNext = dbNext; }

    bool m_fParent;
    int  m_dbParent;
    void SetParent(int dbParent) { m_fParent = true; m_dbParent = dbParent; }

    bool m_fOwner;
    int  m_dbOwner;
    void SetOwner(int dbOwner) { m_fOwner = true; m_dbOwner = dbOwner; }

    bool m_fZone;
    int  m_dbZone;
    void SetZone(int dbZone) { m_fZone = true; m_dbZone = dbZone; }

    bool m_fPennies;
    int  m_iPennies;
    void SetPennies(int iPennies) { m_fPennies = true; m_iPennies = iPennies; }

    bool m_fCreated;
    int  m_iCreated;
    void SetCreated(int iCreated) { m_fCreated = true; m_iCreated = iCreated; }

    bool m_fModified;
    int  m_iModified;
    void SetModified(int iModified) { m_fModified = true; m_iModified = iModified; }

    bool m_fType;
    int  m_iType;
    void SetType(int iType) { m_fType = true; m_iType = iType; }

    char *m_pFlags;
    void SetFlags(char *pFlags);

    char *m_pPowers;
    void SetPowers(char *pPowers);

    char *m_pWarnings;
    void SetWarnings(char *pWarnings);

    bool m_fLockCount;
    int  m_nLockCount;
    vector<LOCKINFO *> *m_pvli;
    void SetLocks(int nLockCount, vector<LOCKINFO *> *pvli);

    bool m_fAttrCount;
    int  m_nAttrCount;
    vector<ATTRINFO *> *m_pvai;
    void SetAttrs(int nAttrCount, vector<ATTRINFO *> *pvai);

    void Merge(OBJECTINFO *poi);
    void WriteLock(const LOCKINFO &li) const;
    void WriteAttr(const ATTRINFO &ai) const;

    OBJECTINFO()  {
        m_fRef = false;
        m_pName = NULL;
        m_fLocation = false;
        m_fContents = false;
        m_fExits = false;
        m_fNext = false;
        m_fParent = false;
        m_fOwner = false;
        m_fZone = false;
        m_fPennies = false;
        m_fType = false;
        m_fPennies = false;
        m_fCreated = false;
        m_fModified = false;
        m_fType = false;
        m_pFlags = NULL;
        m_pPowers = NULL;
        m_pWarnings = NULL;
        m_fLockCount = false;
        m_pvli = NULL;
        m_fAttrCount = false;
        m_pvai = NULL;
    }
    ~OBJECTINFO();
};


class P6H_GAME
{
public:
    void Validate();
    void ValidateFlags();
    void ValidateSavedTime();

    void Write(FILE *fp);
    void WriteFlag(const FLAGINFO &fi);
    void WriteFlagAlias(const FLAGALIASINFO &fai);
    void WriteObject(const OBJECTINFO &oi);

    P6H_GAME();

    int m_flags;
    void SetFlags(int flags) { m_flags = flags; }
    int  GetFlags()          { return m_flags;  }

    char *m_pSavedTime;
    void SetSavedTime(char *p);

    bool m_fFlags;
    int  m_nFlags;
    void SetFlagCount(int nFlags)  { m_fFlags = true; m_nFlags = nFlags; }

    vector<FLAGINFO *> *m_pvFlags;
    void SetFlagList(vector<FLAGINFO *> *pvfi) { m_pvFlags = pvfi; }

    bool m_fFlagAliases;
    int  m_nFlagAliases;
    void SetFlagAliasCount(int nFlagAliases)  { m_fFlagAliases = true; m_nFlagAliases = nFlagAliases; }

    vector<FLAGALIASINFO *> *m_pvFlagAliases;
    void SetFlagAliasList(vector<FLAGALIASINFO *> *pvfai) { m_pvFlagAliases = pvfai; }
  
    bool m_fPowers;
    int  m_nPowers;
    void SetPowerCount(int nPowers)  { m_fPowers = true; m_nPowers = nPowers; }

    vector<FLAGINFO *> *m_pvPowers;
    void SetPowerList(vector<FLAGINFO *> *pvpi) { m_pvPowers = pvpi; }

    bool m_fPowerAliases;
    int  m_nPowerAliases;
    void SetPowerAliasCount(int nPowerAliases)  { m_fPowerAliases = true; m_nPowerAliases = nPowerAliases; }
  
    vector<FLAGALIASINFO *> *m_pvPowerAliases;
    void SetPowerAliasList(vector<FLAGALIASINFO *> *pvpai) { m_pvPowerAliases = pvpai; }
  
    bool m_fObjects;
    int  m_nObjects;
    void SetObjectCount(int nObjects) { m_fObjects = true; m_nObjects = nObjects; }

    vector<OBJECTINFO *> m_vObjects;
    void AddObject(OBJECTINFO *poi);
};

extern P6H_GAME g_p6hgame;

char *StringClone(const char *str);

#endif
