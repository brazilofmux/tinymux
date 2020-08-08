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

class T5X_LOCKEXP;

class P6H_LOCKEXP
{
public:
    typedef enum
    {
        le_is,
        le_carry,
        le_indirect,
        le_indirect2,
        le_owner,
        le_and,
        le_or,
        le_not,
        le_attr,
        le_eval,
        le_ref,
        le_text,
        le_class,
        le_true,
        le_false,
        le_none,
    } P6H_OP;

    P6H_OP m_op;

    P6H_LOCKEXP *m_le[2];
    int          m_dbRef;
    char        *m_p[2];

    void SetIs(P6H_LOCKEXP *p)
    {
        m_op = le_is;
        m_le[0] = p;
    }
    void SetCarry(P6H_LOCKEXP *p)
    {
        m_op = le_carry;
        m_le[0] = p;
    }
    void SetIndir(P6H_LOCKEXP *p)
    {
        m_op = le_indirect;
        m_le[0] = p;
    }
    void SetIndir(P6H_LOCKEXP *p, P6H_LOCKEXP *q)
    {
        m_op = le_indirect2;
        m_le[0] = p;
        m_le[1] = q;
    }
    void SetOwner(P6H_LOCKEXP *p)
    {
        m_op = le_owner;
        m_le[0] = p;
    }
    void SetAnd(P6H_LOCKEXP *p, P6H_LOCKEXP *q)
    {
        m_op = le_and;
        m_le[0] = p;
        m_le[1] = q;
    }
    void SetOr(P6H_LOCKEXP *p, P6H_LOCKEXP *q)
    {
        m_op = le_or;
        m_le[0] = p;
        m_le[1] = q;
    }
    void SetNot(P6H_LOCKEXP *p)
    {
        m_op = le_not;
        m_le[0] = p;
    }
    void SetRef(int dbRef)
    {
        m_op = le_ref;
        m_dbRef = dbRef;
    }
    void SetTrue()
    {
        m_op = le_true;
    }
    void SetFalse()
    {
        m_op = le_false;
    }
    void SetText(char *p)
    {
        m_op = le_text;
        m_p[0] = p;
    }
    void SetAttr(P6H_LOCKEXP *p, P6H_LOCKEXP *q)
    {
        m_op = le_attr;
        m_le[0] = p;
        m_le[1] = q;
    }
    void SetEval(P6H_LOCKEXP *p, P6H_LOCKEXP *q)
    {
        m_op = le_eval;
        m_le[0] = p;
        m_le[1] = q;
    }
    void SetClass(char *p, P6H_LOCKEXP *q)
    {
        m_op = le_class;
        m_p[0] = p;
        m_le[1] = q;
    }

    char *Write(char *p);

    bool ConvertFromT5X(T5X_LOCKEXP *p);

    P6H_LOCKEXP()
    {
        m_op = le_none;
        m_le[0] = m_le[1] = NULL;
        m_p[0] = m_p[1] = NULL;
        m_dbRef = 0;
    }
    ~P6H_LOCKEXP()
    {
        delete m_le[0];
        delete m_le[1];
        free(m_p[0]);
        free(m_p[1]);
        m_le[0] = m_le[1] = NULL;
        m_p[0] = m_p[1] = NULL;
    }
};

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

    void Validate() const;

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
    P6H_LOCKEXP *m_pKeyTree;
    void SetKey(char *pKey);

    bool m_fFlags;
    int  m_iFlags;
    void SetFlags(int iFlags)  { m_fFlags = true; m_iFlags = iFlags; }

    void Merge(P6H_LOCKINFO *pli);

    void Validate() const;

    void Write(FILE *fp, bool fLabels) const;
    void Extract(FILE *fp, char *pObjName) const;

    void Upgrade();

    P6H_LOCKINFO()
    {
        m_pType = NULL;
        m_fCreator = false;
        m_pFlags = NULL;
        m_fDerefs = false;
        m_pKey = NULL;
        m_pKeyTree = NULL;
        m_fFlags = false;
    }
    ~P6H_LOCKINFO()
    {
        free(m_pType);
        free(m_pFlags);
        free(m_pKey);
        delete m_pKeyTree;
        m_pType = NULL;
        m_pFlags = NULL;
        m_pKey = NULL;
        m_pKeyTree = NULL;
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
    void Extract(FILE *fp, char *pObjName) const;

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

class P6H_ATTRNAMEINFO
{
public:
    char *m_pName;
    void SetName(char *pName);

    char *m_pFlags;
    void SetFlags(char *pFlags);

    bool m_fCreator;
    int  m_dbCreator;
    void SetCreator(int dbCreator) { m_fCreator = true; m_dbCreator = dbCreator; }

    char *m_pData;
    void SetData(char *pData);

    void Merge(P6H_ATTRNAMEINFO *pani);

    void Write(FILE *fp);

    P6H_ATTRNAMEINFO()
    {
        m_pName = NULL;
        m_pFlags = NULL;
        m_fCreator = false;
        m_pData = NULL;
    }
    ~P6H_ATTRNAMEINFO()
    {
        free(m_pName);
        free(m_pFlags);
        free(m_pData);
        m_pName = NULL;
        m_pFlags = NULL;
        m_pData = NULL;
    }
};

class P6H_ATTRALIASINFO
{
public:
    char *m_pName;
    void SetName(char *pName);

    char *m_pAlias;
    void SetAlias(char *pAlias);

    void Merge(P6H_ATTRALIASINFO *paai);

    void Write(FILE *fp);

    P6H_ATTRALIASINFO()
    {
        m_pName = NULL;
        m_pAlias = NULL;
    }
    ~P6H_ATTRALIASINFO()
    {
        free(m_pName);
        free(m_pAlias);
        m_pName = NULL;
        m_pAlias = NULL;
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

    void Validate() const;

    void Write(FILE *fp, bool fLabels);
    void Extract(FILE *fp) const;

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
    bool HasLabels() const;

    bool m_fDbVersion;
    int m_nDbVersion;
    void SetDbVersion(int nDbVersion) { m_fDbVersion = true; m_nDbVersion = nDbVersion; }

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

    bool m_fAttributeNames;
    int  m_nAttributeNames;
    void SetAttributeNameCount(int nAttributeNames) { m_fAttributeNames = true; m_nAttributeNames = nAttributeNames; }

    vector<P6H_ATTRNAMEINFO *> *m_pvAttributeNames;
    void SetAttributeNameList(vector<P6H_ATTRNAMEINFO *> *pvani) { m_pvAttributeNames = pvani; }

    bool m_fAttributeAliases;
    int  m_nAttributeAliases;
    void SetAttributeAliasCount(int nAttributeAliases) { m_fAttributeAliases = true; m_nAttributeAliases = nAttributeAliases; }

    vector<P6H_ATTRALIASINFO *> *m_pvAttributeAliases;
    void SetAttributeAliasList(vector<P6H_ATTRALIASINFO *> *pvaai) { m_pvAttributeAliases = pvaai; }

    bool m_fSizeHint;
    int  m_nSizeHint;
    void SetSizeHint(int nSizeHint) { m_fSizeHint = true; m_nSizeHint = nSizeHint; }

    map<int, P6H_OBJECTINFO *, lti> m_mObjects;
    void AddObject(P6H_OBJECTINFO *poi);

    void Validate() const;
    void ValidateFlags() const;
    void ValidateSavedTime() const;

    void Write(FILE *fp);
    void Extract(FILE *fp, int dbExtract) const;

    void Upgrade();

    void ConvertFromT5X();

    void ResetPassword();

    P6H_GAME()
    {
        m_flags = 0;
        m_fDbVersion = false;
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
        m_fAttributeNames = false;
        m_nAttributeNames = 0;
        m_pvAttributeNames = NULL;
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
        if (NULL != m_pvAttributeNames)
        {
            for (vector<P6H_ATTRNAMEINFO *>::iterator it = m_pvAttributeNames->begin(); it != m_pvAttributeNames->end(); ++it)
            {
                delete *it;
            }
            delete m_pvAttributeNames;
            m_pvAttributeNames = NULL;
        }
        for (map<int, P6H_OBJECTINFO *, lti>::iterator it = m_mObjects.begin(); it != m_mObjects.end(); ++it)
        {
            delete it->second;
        }
        m_mObjects.clear();
    }
};

extern P6H_GAME g_p6hgame;
extern int p6hparse();
extern FILE *p6hin;

#endif
