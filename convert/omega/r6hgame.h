#ifndef _R6HGAME_H_
#define _R6HGAME_H_

#define R6H_V_MASK          0x000000ff      /* Database version */
#define R6H_V_ZONE          0x00000100      /* ZONE/DOMAIN field */
#define R6H_V_LINK          0x00000200      /* LINK field (exits from objs) */
#define R6H_V_GDBM          0x00000400      /* attrs are in a gdbm db, not here */
#define R6H_V_ATRNAME       0x00000800      /* NAME is an attr, not in the hdr */
#define R6H_V_ATRKEY        0x00001000      /* KEY is an attr, not in the hdr */
#define R6H_V_PERNKEY       0x00001000      /* PERN: Extra locks in object hdr */
#define R6H_V_PARENT        0x00002000      /* db has the PARENT field */
#define R6H_V_COMM          0x00004000      /* PERN: Comm status in header */
#define R6H_V_ATRMONEY      0x00008000      /* Money is kept in an attribute */
#define R6H_V_XFLAGS        0x00010000      /* An extra word of flags */

#define R6H_MANDFLAGS  (R6H_V_LINK|R6H_V_PARENT|R6H_V_XFLAGS)
#define R6H_OFLAGS     (R6H_V_GDBM|R6HV_ATRKEY|R6H_V_ATRNAME|R6H_V_ATRMONEY)

#define A_USER_START    256     // Start of user-named attributes.

/* Object types */
#define R6H_TYPE_ROOM     0x0
#define R6H_TYPE_THING    0x1
#define R6H_TYPE_EXIT     0x2
#define R6H_TYPE_PLAYER   0x3
#define R6H_TYPE_GARBAGE  0x5
#define R6H_NOTYPE        0x7
#define R6H_TYPE_MASK     0x7

#define ATR_INFO_CHAR 0x01

class P6H_LOCKEXP;

class R6H_LOCKEXP
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
    } R6H_OP;

    R6H_OP m_op;

    R6H_LOCKEXP *m_le[2];
    int          m_dbRef;
    char        *m_p[2];

    void SetIs(R6H_LOCKEXP *p)
    {
        m_op = le_is;
        m_le[0] = p;
    }
    void SetCarry(R6H_LOCKEXP *p)
    {
        m_op = le_carry;
        m_le[0] = p;
    }
    void SetIndir(R6H_LOCKEXP *p)
    {
        m_op = le_indirect;
        m_le[0] = p;
    }
    void SetOwner(R6H_LOCKEXP *p)
    {
        m_op = le_owner;
        m_le[0] = p;
    }
    void SetAnd(R6H_LOCKEXP *p, R6H_LOCKEXP *q)
    {
        m_op = le_and;
        m_le[0] = p;
        m_le[1] = q;
    }
    void SetOr(R6H_LOCKEXP *p, R6H_LOCKEXP *q)
    {
        m_op = le_or;
        m_le[0] = p;
        m_le[1] = q;
    }
    void SetNot(R6H_LOCKEXP *p)
    {
        m_op = le_not;
        m_le[0] = p;
    }
    void SetAttr(R6H_LOCKEXP *p, R6H_LOCKEXP *q)
    {
        m_op = le_attr;
        m_le[0] = p;
        m_le[1] = q;
    }
    void SetEval(R6H_LOCKEXP *p, R6H_LOCKEXP *q)
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

    R6H_LOCKEXP()
    {
        m_op = le_none;
        m_le[0] = m_le[1] = NULL;
        m_p[0] = m_p[1] = NULL;
        m_dbRef = 0;
    }
    ~R6H_LOCKEXP()
    {
        delete m_le[0];
        delete m_le[1];
        free(m_p[0]);
        free(m_p[1]);
        m_le[0] = m_le[1] = NULL;
        m_p[0] = m_p[1] = NULL;
    } 
};

class R6H_ATTRNAMEINFO
{
public:
    bool  m_fNumAndName;
    int   m_iNum;
    char *m_pName;
    void  SetNumAndName(int iNum, char *pName);

    void Validate(int ver) const;

    void Write(FILE *fp, bool fExtraEscapes);

    R6H_ATTRNAMEINFO()
    {
        m_fNumAndName = false;
        m_pName = NULL;
    }
    ~R6H_ATTRNAMEINFO()
    {
        free(m_pName);
        m_pName = NULL;
    }
};

class R6H_ATTRINFO
{
public:
    bool m_fNumAndValue;
    int  m_iNum;
    char *m_pValue;
    void SetNumAndValue(int iNum, char *pValue);

    bool m_fIsLock;
    R6H_LOCKEXP *m_pKeyTree;

    void Validate() const;

    void Write(FILE *fp, bool fExtraEscapes) const;

    R6H_ATTRINFO()
    {
        m_fNumAndValue = false;
        m_fIsLock = false;
        m_pValue = NULL;
        m_pKeyTree = NULL;
    }
    ~R6H_ATTRINFO()
    {
        free(m_pValue);
        delete m_pKeyTree;
        m_pValue = NULL;
        m_pKeyTree = NULL;
    }
};

class R6H_OBJECTINFO
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

    bool m_fPowers1;
    int  m_iPowers1;
    void SetPowers1(int iPowers1) { m_fPowers1 = true; m_iPowers1 = iPowers1; }

    bool m_fPowers2;
    int  m_iPowers2;
    void SetPowers2(int iPowers2) { m_fPowers2 = true; m_iPowers2 = iPowers2; }

    bool m_fLink;
    int  m_dbLink;
    void SetLink(int dbLink) { m_fLink = true; m_dbLink = dbLink; }

    bool m_fAttrCount;
    int  m_nAttrCount;
    vector<R6H_ATTRINFO *> *m_pvai;
    void SetAttrs(int nAttrCount, vector<R6H_ATTRINFO *> *pvai);

    R6H_LOCKEXP *m_ple;
    void SetDefaultLock(R6H_LOCKEXP *p) { free(m_ple); m_ple = p; }

    bool m_fAccessed;
    int  m_iAccessed;
    void SetAccessed(int iAccessed) { m_fAccessed = true; m_iAccessed = iAccessed; }

    bool m_fModified;
    int  m_iModified;
    void SetModified(int iModified) { m_fModified = true; m_iModified = iModified; }

    bool m_fCreated;
    int  m_iCreated;
    void SetCreated(int iCreated) { m_fCreated = true; m_iCreated = iCreated; }

    void Validate() const;

    void Write(FILE *fp, bool bWriteLock, bool fExtraEscapes);

    R6H_OBJECTINFO()
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
        m_fPennies = false;
        m_fAttrCount = false;
        m_fAccessed = false;
        m_fModified = false;
        m_fCreated = false;
        m_pvai = NULL;
        m_ple = NULL;
    }
    ~R6H_OBJECTINFO()
    {
        free(m_pName);
        m_pName = NULL;
        if (NULL != m_pvai)
        {
            for (vector<R6H_ATTRINFO *>::iterator it = m_pvai->begin(); it != m_pvai->end(); ++it)
            {
               delete *it;
            } 
            delete m_pvai;
            m_pvai = NULL;
        }
    }
};


class R6H_GAME
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

    vector<R6H_ATTRNAMEINFO *> m_vAttrNames;
    void AddNumAndName(int iNum, char *pName);

    map<int, R6H_OBJECTINFO *, lti> m_mObjects;
    void AddObject(R6H_OBJECTINFO *poi);

    void Validate() const;
    void ValidateFlags() const;
    void ValidateAttrNames(int ver) const;
    void ValidateObjects() const;

    void Write(FILE *fp);
 
    void ConvertFromP6H();

    void ResetPassword();

    R6H_GAME()
    {
        m_flags = 0;
        m_fSizeHint = false;
        m_fNextAttr = false;
        m_fRecordPlayers = false;
    }
    ~R6H_GAME()
    {
        for (vector<R6H_ATTRNAMEINFO *>::iterator it = m_vAttrNames.begin(); it != m_vAttrNames.end(); ++it)
        {
            delete *it;
        } 
        m_vAttrNames.clear();
        for (map<int, R6H_OBJECTINFO *, lti>::iterator it = m_mObjects.begin(); it != m_mObjects.end(); ++it)
        {
            delete it->second;
        } 
        m_mObjects.clear();
    }
};

extern R6H_GAME g_r6hgame;
extern int r6hparse();
extern FILE *r6hin;

char *r6h_ConvertAttributeName(const char *);

#endif
