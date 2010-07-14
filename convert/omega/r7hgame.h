#ifndef _R7HGAME_H_
#define _R7HGAME_H_

#define R7H_V_MASK          0x000000ff
#define R7H_V_ZONE          0x00000100
#define R7H_V_LINK          0x00000200
#define R7H_V_GDBM          0x00000400
#define R7H_V_ATRNAME       0x00000800
#define R7H_V_ATRKEY        0x00001000
#define R7H_V_PERNKEY       0x00001000
#define R7H_V_PARENT        0x00002000
#define R7H_V_COMM          0x00004000
#define R7H_V_ATRMONEY      0x00008000
#define R7H_V_XFLAGS        0x00010000

#define R7H_MANDFLAGS  (R7H_V_LINK|R7H_V_PARENT|R7H_V_XFLAGS)
#define R7H_OFLAGS     (R7H_V_GDBM|R7HV_ATRKEY|R7H_V_ATRNAME|R7H_V_ATRMONEY)

#define A_USER_START    256     // Start of user-named attributes.

// Object types
//
#define R7H_TYPE_ROOM     0x0
#define R7H_TYPE_THING    0x1
#define R7H_TYPE_EXIT     0x2
#define R7H_TYPE_PLAYER   0x3
#define R7H_TYPE_GARBAGE  0x5
#define R7H_NOTYPE        0x7
#define R7H_TYPE_MASK     0x7

// Attribute flags
//
#define R7H_AF_ODARK        0x00000001
#define R7H_AF_DARK         0x00000002
#define R7H_AF_WIZARD       0x00000004
#define R7H_AF_MDARK        0x00000008
#define R7H_AF_INTERNAL     0x00000010
#define R7H_AF_NOCMD        0x00000020
#define R7H_AF_LOCK         0x00000040
#define R7H_AF_DELETED      0x00000080
#define R7H_AF_NOPROG       0x00000100
#define R7H_AF_GOD          0x00000200
#define R7H_AF_ADMIN        0x00000400
#define R7H_AF_BUILDER      0x00000800
#define R7H_AF_IS_LOCK      0x00001000
#define R7H_AF_GUILDMASTER  0x00002000
#define R7H_AF_IMMORTAL     0x00004000
#define R7H_AF_PRIVATE      0x00008000
#define R7H_AF_NONBLOCKING  0x00010000
#define R7H_AF_VISUAL       0x00020000
#define R7H_AF_NOANSI       0x00040000
#define R7H_AF_PINVIS       0x00080000
#define R7H_AF_NORETURN     0x00100000
#define R7H_AF_NOCLONE      0x00200000
#define R7H_AF_NOPARSE      0x00400000
#define R7H_AF_SAFE         0x00800000
#define R7H_AF_USELOCK      0x01000000
#define R7H_AF_SINGLETHREAD 0x02000000
#define R7H_AF_DEFAULT      0x04000000
#define R7H_AF_ATRLOCK      0x08000000
#define R7H_AF_LOGGED       0x10000000


// Attribute Numbers
//
#define R7H_A_MODIFY_TIME   227
#define R7H_A_CREATED_TIME  228

// Object Flagword 1
//
#define R7H_SEETHRU         0x00000008UL
#define R7H_WIZARD          0x00000010UL
#define R7H_LINK_OK         0x00000020UL
#define R7H_DARK            0x00000040UL
#define R7H_JUMP_OK         0x00000080UL
#define R7H_STICKY          0x00000100UL
#define R7H_DESTROY_OK      0x00000200UL
#define R7H_HAVEN           0x00000400UL
#define R7H_QUIET           0x00000800UL
#define R7H_HALT            0x00001000UL
#define R7H_TRACE           0x00002000UL
#define R7H_GOING           0x00004000UL
#define R7H_MONITOR         0x00008000UL
#define R7H_MYOPIC          0x00010000UL
#define R7H_PUPPET          0x00020000UL
#define R7H_CHOWN_OK        0x00040000UL
#define R7H_ENTER_OK        0x00080000UL
#define R7H_VISUAL          0x00100000UL
#define R7H_IMMORTAL        0x00200000UL
#define R7H_HAS_STARTUP     0x00400000UL
#define R7H_OPAQUE          0x00800000UL
#define R7H_VERBOSE         0x01000000UL
#define R7H_INHERIT         0x02000000UL
#define R7H_NOSPOOF         0x04000000UL
#define R7H_ROBOT           0x08000000UL
#define R7H_SAFE            0x10000000UL
#define R7H_CONTROL_OK      0x20000000UL
#define R7H_HEARTHRU        0x40000000UL
#define R7H_TERSE           0x80000000UL

// Object Flagword 2
//
#define R7H_KEY             0x00000001UL
#define R7H_ABODE           0x00000002UL
#define R7H_FLOATING        0x00000004UL
#define R7H_UNFINDABLE      0x00000008UL
#define R7H_PARENT_OK       0x00000010UL
#define R7H_LIGHT           0x00000020UL
#define R7H_HAS_LISTEN      0x00000040UL
#define R7H_HAS_FWDLIST     0x00000080UL
#define R7H_ADMIN           0x00000100UL
#define R7H_GUILDOBJ        0x00000200UL
#define R7H_GUILDMASTER     0x00000400UL
#define R7H_NO_WALLS        0x00000800UL
#define R7H_OLD_TEMPLE      0x00001000UL
#define R7H_OLD_NOROBOT     0x00002000UL
#define R7H_SCLOAK          0x00004000UL
#define R7H_CLOAK           0x00008000UL
#define R7H_FUBAR           0x00010000UL
#define R7H_INDESTRUCTABLE  0x00020000UL
#define R7H_NO_YELL         0x00040000UL
#define R7H_NO_TEL          0x00080000UL
#define R7H_FREE            0x00100000UL
#define R7H_GUEST_FLAG      0x00200000UL
#define R7H_RECOVER         0x00400000UL
#define R7H_BYEROOM         0x00800000UL
#define R7H_WANDERER        0x01000000UL
#define R7H_ANSI            0x02000000UL
#define R7H_ANSICOLOR       0x04000000UL
#define R7H_NOFLASH         0x08000000UL
#define R7H_SUSPECT         0x10000000UL
#define R7H_BUILDER         0x20000000UL
#define R7H_CONNECTED       0x40000000UL
#define R7H_SLAVE           0x80000000UL

// Object Flagword 3
//
#define R7H_NOCONNECT       0x00000001UL
#define R7H_DPSHIFT         0x00000002UL
#define R7H_NOPOSSESS       0x00000004UL
#define R7H_COMBAT          0x00000008UL
#define R7H_IC              0x00000010UL
#define R7H_ZONEMASTER      0x00000020UL
#define R7H_ALTQUOTA        0x00000040UL
#define R7H_NOEXAMINE       0x00000080UL
#define R7H_NOMODIFY        0x00000100UL
#define R7H_CMDCHECK        0x00000200UL
#define R7H_DOORRED         0x00000400UL
#define R7H_PRIVATE         0x00000800UL
#define R7H_NOMOVE          0x00001000UL
#define R7H_STOP            0x00002000UL
#define R7H_NOSTOP          0x00004000UL
#define R7H_NOCOMMAND       0x00008000UL
#define R7H_AUDIT           0x00010000UL
#define R7H_SEE_OEMIT       0x00020000UL
#define R7H_NO_GOBJ         0x00040000UL
#define R7H_NO_PESTER       0x00080000UL
#define R7H_LRFLAG          0x00100000UL
#define R7H_TELOK           0x00200000UL
#define R7H_NO_OVERRIDE     0x00400000UL
#define R7H_NO_USELOCK      0x00800000UL
#define R7H_DR_PURGE        0x01000000UL
#define R7H_NO_ANSINAME     0x02000000UL
#define R7H_SPOOF           0x04000000UL
#define R7H_SIDEFX          0x08000000UL
#define R7H_ZONECONTENTS    0x10000000UL
#define R7H_NOWHO           0x20000000UL
#define R7H_ANONYMOUS       0x40000000UL
#define R7H_BACKSTAGE       0x80000000UL

// Object Flagword 4
//
#define R7H_NOBACKSTAGE     0x00000001UL
#define R7H_LOGIN           0x00000002UL
#define R7H_INPROGRAM       0x00000004UL
#define R7H_COMMANDS        0x00000008UL
#define R7H_MARKER0         0x00000010UL
#define R7H_MARKER1         0x00000020UL
#define R7H_MARKER2         0x00000040UL
#define R7H_MARKER3         0x00000080UL
#define R7H_MARKER4         0x00000100UL
#define R7H_MARKER5         0x00000200UL
#define R7H_MARKER6         0x00000400UL
#define R7H_MARKER7         0x00000800UL
#define R7H_MARKER8         0x00001000UL
#define R7H_MARKER9         0x00002000UL
#define R7H_BOUNCE          0x00004000UL
#define R7H_SHOWFAILCMD     0x00008000UL
#define R7H_NOUNDERLINE     0x00010000UL
#define R7H_NONAME          0x00020000UL
#define R7H_ZONEPARENT      0x00040000UL
#define R7H_SPAMMONITOR     0x00080000UL
#define R7H_BLIND           0x00100000UL
#define R7H_NOCODE          0x00200000UL

#define ATR_INFO_CHAR 0x01

class P6H_LOCKEXP;

class R7H_LOCKEXP
{
public:
    typedef enum
    {
        le_is,
        le_carry,
        le_indirect,
        le_owner,
        le_and,
        le_or,
        le_not,
        le_attr,
        le_eval,
        le_ref,
        le_text,
        le_none,
    } R7H_OP;

    R7H_OP m_op;

    R7H_LOCKEXP *m_le[2];
    int          m_dbRef;
    char        *m_p[2];

    void SetIs(R7H_LOCKEXP *p)
    {
        m_op = le_is;
        m_le[0] = p;
    }
    void SetCarry(R7H_LOCKEXP *p)
    {
        m_op = le_carry;
        m_le[0] = p;
    }
    void SetIndir(R7H_LOCKEXP *p)
    {
        m_op = le_indirect;
        m_le[0] = p;
    }
    void SetOwner(R7H_LOCKEXP *p)
    {
        m_op = le_owner;
        m_le[0] = p;
    }
    void SetAnd(R7H_LOCKEXP *p, R7H_LOCKEXP *q)
    {
        m_op = le_and;
        m_le[0] = p;
        m_le[1] = q;
    }
    void SetOr(R7H_LOCKEXP *p, R7H_LOCKEXP *q)
    {
        m_op = le_or;
        m_le[0] = p;
        m_le[1] = q;
    }
    void SetNot(R7H_LOCKEXP *p)
    {
        m_op = le_not;
        m_le[0] = p;
    }
    void SetAttr(R7H_LOCKEXP *p, R7H_LOCKEXP *q)
    {
        m_op = le_attr;
        m_le[0] = p;
        m_le[1] = q;
    }
    void SetEval(R7H_LOCKEXP *p, R7H_LOCKEXP *q)
    {
        m_op = le_eval;
        m_le[0] = p;
        m_le[1] = q;
    }
    void SetRef(int dbRef)
    {
        m_op = le_ref;
        m_dbRef = dbRef;
    }
    void SetText(char *p)
    {
        m_op = le_text;
        m_p[0] = p;
    }

    void Write(FILE *fp);
    char *Write(char *p);

    bool ConvertFromP6H(P6H_LOCKEXP *p);

    R7H_LOCKEXP()
    {
        m_op = le_none;
        m_le[0] = m_le[1] = NULL;
        m_p[0] = m_p[1] = NULL;
        m_dbRef = 0;
    }
    ~R7H_LOCKEXP()
    {
        delete m_le[0];
        delete m_le[1];
        free(m_p[0]);
        free(m_p[1]);
        m_le[0] = m_le[1] = NULL;
        m_p[0] = m_p[1] = NULL;
    }
};

class R7H_ATTRNAMEINFO
{
public:
    bool  m_fNumAndName;
    int   m_iNum;
    char *m_pName;
    void  SetNumAndName(int iNum, char *pName);

    void Validate(int ver) const;

    void Write(FILE *fp);

    R7H_ATTRNAMEINFO()
    {
        m_fNumAndName = false;
        m_pName = NULL;
    }
    ~R7H_ATTRNAMEINFO()
    {
        free(m_pName);
        m_pName = NULL;
    }
};

class R7H_ATTRINFO
{
public:
    bool m_fNumAndValue;
    int  m_iNum;
    char *m_pValue;
    void SetNumAndValue(int iNum, char *pValue);

    bool m_fIsLock;
    R7H_LOCKEXP *m_pKeyTree;

    void Validate() const;

    void Write(FILE *fp) const;

    R7H_ATTRINFO()
    {
        m_fNumAndValue = false;
        m_fIsLock = false;
        m_pValue = NULL;
        m_pKeyTree = NULL;
    }
    ~R7H_ATTRINFO()
    {
        free(m_pValue);
        delete m_pKeyTree;
        m_pValue = NULL;
        m_pKeyTree = NULL;
    }
};

class R7H_OBJECTINFO
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

    bool m_fFlags1;
    int  m_iFlags1;
    void SetFlags1(int iFlags1) { m_fFlags1 = true; m_iFlags1 = iFlags1; }

    bool m_fFlags2;
    int  m_iFlags2;
    void SetFlags2(int iFlags2) { m_fFlags2 = true; m_iFlags2 = iFlags2; }

    bool m_fFlags3;
    int  m_iFlags3;
    void SetFlags3(int iFlags3) { m_fFlags3 = true; m_iFlags3 = iFlags3; }

    bool m_fFlags4;
    int  m_iFlags4;
    void SetFlags4(int iFlags4) { m_fFlags4 = true; m_iFlags4 = iFlags4; }

    bool m_fToggles1;
    int  m_iToggles1;
    void SetToggles1(int iToggles1) { m_fToggles1 = true; m_iToggles1 = iToggles1; }

    bool m_fToggles2;
    int  m_iToggles2;
    void SetToggles2(int iToggles2) { m_fToggles2 = true; m_iToggles2 = iToggles2; }

    bool m_fToggles3;
    int  m_iToggles3;
    void SetToggles3(int iToggles3) { m_fToggles3 = true; m_iToggles3 = iToggles3; }

    bool m_fToggles4;
    int  m_iToggles4;
    void SetToggles4(int iToggles4) { m_fToggles4 = true; m_iToggles4 = iToggles4; }

    bool m_fToggles5;
    int  m_iToggles5;
    void SetToggles5(int iToggles5) { m_fToggles5 = true; m_iToggles5 = iToggles5; }

    bool m_fToggles6;
    int  m_iToggles6;
    void SetToggles6(int iToggles6) { m_fToggles6 = true; m_iToggles6 = iToggles6; }

    bool m_fToggles7;
    int  m_iToggles7;
    void SetToggles7(int iToggles7) { m_fToggles7 = true; m_iToggles7 = iToggles7; }

    bool m_fToggles8;
    int  m_iToggles8;
    void SetToggles8(int iToggles8) { m_fToggles8 = true; m_iToggles8 = iToggles8; }

    bool m_fZones;
    vector<int> *m_pvz;
    void SetZones(vector<int> *pvz) { m_fZones = true; delete m_pvz; m_pvz = pvz; }

    bool m_fLink;
    int  m_dbLink;
    void SetLink(int dbLink) { m_fLink = true; m_dbLink = dbLink; }

    bool m_fAttrCount;
    int  m_nAttrCount;
    vector<R7H_ATTRINFO *> *m_pvai;
    void SetAttrs(int nAttrCount, vector<R7H_ATTRINFO *> *pvai);

    R7H_LOCKEXP *m_ple;
    void SetDefaultLock(R7H_LOCKEXP *p) { delete m_ple; m_ple = p; }

    void Validate() const;

    void Write(FILE *fp, bool bWriteLock);

    R7H_OBJECTINFO()
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
        m_fFlags1 = false;
        m_fFlags2 = false;
        m_fFlags3 = false;
        m_fFlags4 = false;
        m_fToggles1 = false;
        m_fToggles2 = false;
        m_fToggles3 = false;
        m_fToggles4 = false;
        m_fToggles5 = false;
        m_fToggles6 = false;
        m_fToggles7 = false;
        m_fToggles8 = false;
        m_fAttrCount = false;
        m_pvai = NULL;
        m_ple = NULL;
        m_pvz = NULL;
    }
    ~R7H_OBJECTINFO()
    {
        free(m_pName);
        delete m_ple;
        delete m_pvz;
        m_pName = NULL;
        m_ple = NULL;
        m_pvz = NULL;
        if (NULL != m_pvai)
        {
            for (vector<R7H_ATTRINFO *>::iterator it = m_pvai->begin(); it != m_pvai->end(); ++it)
            {
               delete *it;
            }
            delete m_pvai;
            m_pvai = NULL;
        }
    }
};

class R7H_GAME
{
public:
    int  m_flags;
    void SetFlags(int flags) { m_flags = flags; }
    int  GetFlags()          { return m_flags;  }

    bool m_fSizeHint;
    int  m_nSizeHint;
    void SetSizeHint(int nSizeHint) { m_fSizeHint = true; m_nSizeHint = nSizeHint; }

    bool m_fNextAttr;
    int  m_nNextAttr;
    void SetNextAttr(int nNextAttr) { m_fNextAttr = true; m_nNextAttr = nNextAttr; }

    bool m_fRecordPlayers;
    int  m_nRecordPlayers;
    void SetRecordPlayers(int nRecordPlayers) { m_fRecordPlayers = true; m_nRecordPlayers = nRecordPlayers; }

    vector<R7H_ATTRNAMEINFO *> m_vAttrNames;
    void AddNumAndName(int iNum, char *pName);

    map<int, R7H_OBJECTINFO *, lti> m_mObjects;
    void AddObject(R7H_OBJECTINFO *poi);

    void Validate() const;
    void ValidateFlags() const;
    void ValidateAttrNames(int ver) const;
    void ValidateObjects() const;

    void Write(FILE *fp);

    void ConvertFromP6H();

    void ResetPassword();

    R7H_GAME()
    {
        m_flags = 0;
        m_fSizeHint = false;
        m_fNextAttr = false;
        m_fRecordPlayers = false;
    }
    ~R7H_GAME()
    {
        for (vector<R7H_ATTRNAMEINFO *>::iterator it = m_vAttrNames.begin(); it != m_vAttrNames.end(); ++it)
        {
            delete *it;
        }
        m_vAttrNames.clear();
        for (map<int, R7H_OBJECTINFO *, lti>::iterator it = m_mObjects.begin(); it != m_mObjects.end(); ++it)
        {
            delete it->second;
        }
        m_mObjects.clear();
    }
};

extern R7H_GAME g_r7hgame;
extern int r7hparse();
extern FILE *r7hin;

char *r7h_ConvertAttributeName(const char *);

#endif
