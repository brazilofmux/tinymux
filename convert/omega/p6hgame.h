#ifndef _P6HGAME_H_
#define _P6HGAME_H_

#define P6H_OLD_TYPE_ROOM       0x0
#define P6H_OLD_TYPE_THING      0x1
#define P6H_OLD_TYPE_EXIT       0x2
#define P6H_OLD_TYPE_PLAYER     0x3
#define P6H_OLD_TYPE_GARBAGE    0x6
#define P6H_OLD_NOTYPE          0x7
#define P6H_OLD_TYPE_MASK       0x7

#define P6H_TYPE_ROOM           0x1
#define P6H_TYPE_THING          0x2
#define P6H_TYPE_EXIT           0x4
#define P6H_TYPE_PLAYER         0x8
#define P6H_TYPE_GARBAGE        0x10
#define P6H_NOTYPE              0xFFFF

class P6H_FLAGINFO
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

    void Validate();

    void Merge(P6H_FLAGINFO *pfi);

    void Write(FILE *fp);

    P6H_FLAGINFO()
    {
        m_pName = NULL;
        m_pLetter = NULL;
        m_pType = NULL;
        m_pPerms = NULL;
        m_pNegatePerms = NULL;
    }

    ~P6H_FLAGINFO()
    {
        free(m_pName);
        free(m_pLetter);
        free(m_pType);
        free(m_pPerms);
        free(m_pNegatePerms);
        m_pName = NULL;
        m_pLetter = NULL;
        m_pType = NULL;
        m_pPerms = NULL;
        m_pNegatePerms = NULL;
    }
};

class P6H_FLAGALIASINFO
{
public:
    char *m_pName;
    void SetName(char *p);

    char *m_pAlias;
    void SetAlias(char *p);

    void Merge(P6H_FLAGALIASINFO *pfi);

    void Write(FILE *fp);

    P6H_FLAGALIASINFO()
    {
        m_pName = NULL;
        m_pAlias = NULL;
    }

    ~P6H_FLAGALIASINFO()
    {
        free(m_pName);
        free(m_pAlias);
        m_pName = NULL;
        m_pAlias = NULL;
    }
};

class P6H_LOCKINFO
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

    void Merge(P6H_LOCKINFO *pli);

    void Write(FILE *fp, bool fLabels) const;

    void Upgrade();

    P6H_LOCKINFO()
    {
        m_pType = NULL;
        m_fCreator = false;
        m_pFlags = NULL;
        m_fDerefs = false;
        m_pKey = NULL;
        m_fFlags = false;
    }
    ~P6H_LOCKINFO()
    {
        free(m_pType);
        free(m_pFlags);
        free(m_pKey);
        m_pType = NULL;
        m_pFlags = NULL;
        m_pKey = NULL;
    }
};

class P6H_ATTRINFO
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

    void Merge(P6H_ATTRINFO *pai);

    void Write(FILE *fp, bool fLabels) const;

    void Upgrade();

    P6H_ATTRINFO()
    {
        m_pName = NULL;
        m_fOwner = false;
        m_pFlags = NULL;
        m_fDerefs = false;
        m_pValue = NULL;
        m_fFlags = false;
    }
    ~P6H_ATTRINFO()
    {
        free(m_pName);
        free(m_pFlags);
        free(m_pValue);
        m_pName = NULL;
        m_pFlags = NULL;
        m_pValue = NULL;
    }
};

class P6H_OBJECTINFO
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

    bool m_fFlags;
    int  m_iFlags;
    void SetFlags(int iFlags) { m_fFlags = true; m_iFlags = iFlags; }

    bool m_fToggles;
    int  m_iToggles;
    void SetToggles(int iToggles) { m_fToggles = true; m_iToggles = iToggles; }

    char *m_pPowers;
    void SetPowers(char *pPowers);

    bool m_fPowers;
    int  m_iPowers;
    void SetPowers(int iPowers) { m_fPowers = true; m_iPowers = iPowers; }

    char *m_pWarnings;
    void SetWarnings(char *pWarnings);

    bool m_fLockCount;
    int  m_nLockCount;
    vector<P6H_LOCKINFO *> *m_pvli;
    void SetLocks(int nLockCount, vector<P6H_LOCKINFO *> *pvli);

    bool m_fAttrCount;
    int  m_nAttrCount;
    vector<P6H_ATTRINFO *> *m_pvai;
    void SetAttrs(int nAttrCount, vector<P6H_ATTRINFO *> *pvai);

    void Merge(P6H_OBJECTINFO *poi);

    void Write(FILE *fp, bool fLabels);

    void Upgrade();

    P6H_OBJECTINFO()
    {
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
        m_fFlags = false;
        m_fToggles = false;
        m_pPowers = NULL;
        m_fPowers = false;
        m_pWarnings = NULL;
        m_fLockCount = false;
        m_pvli = NULL;
        m_fAttrCount = false;
        m_pvai = NULL;
    }
    ~P6H_OBJECTINFO()
    {
        free(m_pName);
        free(m_pFlags);
        free(m_pPowers);
        free(m_pWarnings);
        m_pName = NULL;
        m_pFlags = NULL;
        m_pPowers = NULL;
        m_pWarnings = NULL;
        if (NULL != m_pvli)
        {
            for (vector<P6H_LOCKINFO *>::iterator it = m_pvli->begin(); it != m_pvli->end(); ++it)
            {
                delete *it;
            } 
            delete m_pvli;
            m_pvli = NULL;
        }
        if (NULL != m_pvai)
        {
            for (vector<P6H_ATTRINFO *>::iterator it = m_pvai->begin(); it != m_pvai->end(); ++it)
            {
                delete *it;
            } 
            delete m_pvai;
            m_pvai = NULL;
        }
    }
};


class P6H_GAME
{
public:
    int m_flags;
    void SetFlags(int flags) { m_flags = flags; }
    int  GetFlags()          { return m_flags;  }

    char *m_pSavedTime;
    void SetSavedTime(char *p);

    bool m_fFlags;
    int  m_nFlags;
    void SetFlagCount(int nFlags)  { m_fFlags = true; m_nFlags = nFlags; }

    vector<P6H_FLAGINFO *> *m_pvFlags;
    void SetFlagList(vector<P6H_FLAGINFO *> *pvfi) { m_pvFlags = pvfi; }

    bool m_fFlagAliases;
    int  m_nFlagAliases;
    void SetFlagAliasCount(int nFlagAliases)  { m_fFlagAliases = true; m_nFlagAliases = nFlagAliases; }

    vector<P6H_FLAGALIASINFO *> *m_pvFlagAliases;
    void SetFlagAliasList(vector<P6H_FLAGALIASINFO *> *pvfai) { m_pvFlagAliases = pvfai; }
  
    bool m_fPowers;
    int  m_nPowers;
    void SetPowerCount(int nPowers)  { m_fPowers = true; m_nPowers = nPowers; }

    vector<P6H_FLAGINFO *> *m_pvPowers;
    void SetPowerList(vector<P6H_FLAGINFO *> *pvpi) { m_pvPowers = pvpi; }

    bool m_fPowerAliases;
    int  m_nPowerAliases;
    void SetPowerAliasCount(int nPowerAliases)  { m_fPowerAliases = true; m_nPowerAliases = nPowerAliases; }
  
    vector<P6H_FLAGALIASINFO *> *m_pvPowerAliases;
    void SetPowerAliasList(vector<P6H_FLAGALIASINFO *> *pvpai) { m_pvPowerAliases = pvpai; }
  
    bool m_fSizeHint;
    int  m_nSizeHint;
    void SetSizeHint(int nSizeHint) { m_fSizeHint = true; m_nSizeHint = nSizeHint; }

    vector<P6H_OBJECTINFO *> m_vObjects;
    void AddObject(P6H_OBJECTINFO *poi);

    void Validate();
    void ValidateFlags();
    void ValidateSavedTime();

    void Write(FILE *fp);

    void Upgrade();

    void ConvertT5X();

    void ResetPassword();

    P6H_GAME()
    {
        m_flags = 0;
        m_pSavedTime = NULL;
        m_fSizeHint = false;
        m_nSizeHint = 0;
        m_fFlags = false;
        m_nFlags = 0;
        m_pvFlags = NULL;
        m_fFlagAliases = false;
        m_nFlagAliases = 0;
        m_pvFlagAliases = NULL;
        m_fPowers = false;
        m_nPowers = 0;
        m_pvPowers = NULL;
        m_fPowerAliases = false;
        m_nPowerAliases = 0;
        m_pvPowerAliases = NULL;
    }
    ~P6H_GAME()
    {
        free(m_pSavedTime);
        m_pSavedTime = NULL;
        if (NULL != m_pvFlags)
        {
            for (vector<P6H_FLAGINFO *>::iterator it = m_pvFlags->begin(); it != m_pvFlags->end(); ++it)
            {
                delete *it;
            } 
            delete m_pvFlags;
            m_pvFlags = NULL;
        }
        if (NULL != m_pvFlagAliases)
        {
            for (vector<P6H_FLAGALIASINFO *>::iterator it = m_pvFlagAliases->begin(); it != m_pvFlagAliases->end(); ++it)
            {
                delete *it;
            } 
            delete m_pvFlagAliases;
            m_pvFlagAliases = NULL;
        }
        if (NULL != m_pvPowers)
        {
            for (vector<P6H_FLAGINFO *>::iterator it = m_pvPowers->begin(); it != m_pvPowers->end(); ++it)
            {
                delete *it;
            } 
            delete m_pvPowers;
            m_pvPowers = NULL;
        }
        if (NULL != m_pvPowerAliases)
        {
            for (vector<P6H_FLAGALIASINFO *>::iterator it = m_pvPowerAliases->begin(); it != m_pvPowerAliases->end(); ++it)
            {
                delete *it;
            } 
            delete m_pvPowerAliases;
            m_pvPowerAliases = NULL;
        }
        for (vector<P6H_OBJECTINFO *>::iterator it = m_vObjects.begin(); it != m_vObjects.end(); ++it)
        {
            delete *it;
        } 
        m_vObjects.clear();
    }
};

extern P6H_GAME g_p6hgame;

#endif
