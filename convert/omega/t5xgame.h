#ifndef _T5XGAME_H_
#define _T5XGAME_H_

#define T5X_V_MASK      0x000000ff  /* Database version */
#define T5X_V_ZONE      0x00000100  /* ZONE/DOMAIN field */
#define T5X_V_LINK      0x00000200  /* LINK field (exits from objs) */
#define T5X_V_DATABASE  0x00000400  /* attrs in a separate database */
#define T5X_V_ATRNAME   0x00000800  /* NAME is an attr, not in the hdr */
#define T5X_V_ATRKEY    0x00001000  /* KEY is an attr, not in the hdr */
#define T5X_V_PARENT    0x00002000  /* db has the PARENT field */
#define T5X_V_ATRMONEY  0x00008000  /* Money is kept in an attribute */
#define T5X_V_XFLAGS    0x00010000  /* An extra word of flags */
#define T5X_V_POWERS    0x00020000  /* Powers? */
#define T5X_V_3FLAGS    0x00040000  /* Adding a 3rd flag word */
#define T5X_V_QUOTED    0x00080000  /* Quoted strings, ala PennMUSH */

#define T5X_MANDFLAGS_V2  (T5X_V_LINK|T5X_V_PARENT|T5X_V_XFLAGS|T5X_V_ZONE|T5X_V_POWERS|T5X_V_3FLAGS|T5X_V_QUOTED)
#define T5X_OFLAGS_V2     (T5X_V_DATABASE|T5X_V_ATRKEY|T5X_V_ATRNAME|T5X_V_ATRMONEY)

#define T5X_MANDFLAGS_V3  (T5X_V_LINK|T5X_V_PARENT|T5X_V_XFLAGS|T5X_V_ZONE|T5X_V_POWERS|T5X_V_3FLAGS|T5X_V_QUOTED|T5X_V_ATRKEY)
#define T5X_OFLAGS_V3     (T5X_V_DATABASE|T5X_V_ATRNAME|T5X_V_ATRMONEY)

#define A_USER_START    256     // Start of user-named attributes.

/* Object types */
#define T5X_TYPE_ROOM     0x0
#define T5X_TYPE_THING    0x1
#define T5X_TYPE_EXIT     0x2
#define T5X_TYPE_PLAYER   0x3
#define T5X_TYPE_GARBAGE  0x5
#define T5X_NOTYPE        0x7
#define T5X_TYPE_MASK     0x7

#define ATR_INFO_CHAR 0x01

typedef unsigned char UTF8;

class P6H_LOCKEXP;
class T6H_LOCKEXP;

class T5X_LOCKEXP
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
    } T5X_OP;

    T5X_OP m_op;

    T5X_LOCKEXP *m_le[2];
    int          m_dbRef;
    char        *m_p[2];

    void SetIs(T5X_LOCKEXP *p)
    {
        m_op = le_is;
        m_le[0] = p;
    }
    void SetCarry(T5X_LOCKEXP *p)
    {
        m_op = le_carry;
        m_le[0] = p;
    }
    void SetIndir(T5X_LOCKEXP *p)
    {
        m_op = le_indirect;
        m_le[0] = p;
    }
    void SetOwner(T5X_LOCKEXP *p)
    {
        m_op = le_owner;
        m_le[0] = p;
    }
    void SetAnd(T5X_LOCKEXP *p, T5X_LOCKEXP *q)
    {
        m_op = le_and;
        m_le[0] = p;
        m_le[1] = q;
    }
    void SetOr(T5X_LOCKEXP *p, T5X_LOCKEXP *q)
    {
        m_op = le_or;
        m_le[0] = p;
        m_le[1] = q;
    }
    void SetNot(T5X_LOCKEXP *p)
    {
        m_op = le_not;
        m_le[0] = p;
    }
    void SetAttr(T5X_LOCKEXP *p, T5X_LOCKEXP *q)
    {
        m_op = le_attr;
        m_le[0] = p;
        m_le[1] = q;
    }
    void SetEval(T5X_LOCKEXP *p, T5X_LOCKEXP *q)
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
    bool ConvertFromT6H(T6H_LOCKEXP *p);

    T5X_LOCKEXP()
    {
        m_op = le_none;
        m_le[0] = m_le[1] = NULL;
        m_p[0] = m_p[1] = NULL;
        m_dbRef = 0;
    }
    ~T5X_LOCKEXP()
    {
        delete m_le[0];
        delete m_le[1];
        free(m_p[0]);
        free(m_p[1]);
        m_le[0] = m_le[1] = NULL;
        m_p[0] = m_p[1] = NULL;
    } 
};

class T5X_ATTRNAMEINFO
{
public:
    bool  m_fNumAndName;
    int   m_iNum;
    char *m_pName;
    void  SetNumAndName(int iNum, char *pName);

    void Validate(int ver) const;

    void Write(FILE *fp, bool fExtraEscapes);

    void Upgrade();
    void Downgrade();

    T5X_ATTRNAMEINFO()
    {
        m_fNumAndName = false;
        m_pName = NULL;
    }
    ~T5X_ATTRNAMEINFO()
    {
        free(m_pName);
        m_pName = NULL;
    }
};

class T5X_ATTRINFO
{
public:
    bool m_fNumAndValue;
    int  m_iNum;
    char *m_pValue;
    void SetNumAndValue(int iNum, char *pValue);

    bool m_fIsLock;
    T5X_LOCKEXP *m_pKeyTree;

    void Validate() const;

    void Write(FILE *fp, bool fExtraEscapes) const;

    void Upgrade();
    void Downgrade();

    T5X_ATTRINFO()
    {
        m_fNumAndValue = false;
        m_fIsLock = false;
        m_pValue = NULL;
        m_pKeyTree = NULL;
    }
    ~T5X_ATTRINFO()
    {
        free(m_pValue);
        delete m_pKeyTree;
        m_pValue = NULL;
        m_pKeyTree = NULL;
    }
};

class T5X_OBJECTINFO
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
    vector<T5X_ATTRINFO *> *m_pvai;
    void SetAttrs(int nAttrCount, vector<T5X_ATTRINFO *> *pvai);

    T5X_LOCKEXP *m_ple;
    void SetDefaultLock(T5X_LOCKEXP *p) { free(m_ple); m_ple = p; }

    void Validate() const;

    void Write(FILE *fp, bool bWriteLock, bool fExtraEscapes);

    void Upgrade();
    void Downgrade();

    T5X_OBJECTINFO()
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
        m_pvai = NULL;
        m_ple = NULL;
    }
    ~T5X_OBJECTINFO()
    {
        free(m_pName);
        m_pName = NULL;
        if (NULL != m_pvai)
        {
            for (vector<T5X_ATTRINFO *>::iterator it = m_pvai->begin(); it != m_pvai->end(); ++it)
            {
               delete *it;
            } 
            delete m_pvai;
            m_pvai = NULL;
        }
    }
};


class T5X_GAME
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

    vector<T5X_ATTRNAMEINFO *> m_vAttrNames;
    void AddNumAndName(int iNum, char *pName);

    map<int, T5X_OBJECTINFO *, lti> m_mObjects;
    void AddObject(T5X_OBJECTINFO *poi);

    void Validate() const;
    void ValidateFlags() const;
    void ValidateAttrNames(int ver) const;
    void ValidateObjects() const;

    void Write(FILE *fp);
 
    bool Upgrade3();
    bool Upgrade2();
    bool Downgrade1();
    bool Downgrade2();

    void ConvertFromP6H();
    void ConvertFromT6H();

    void ResetPassword();

    T5X_GAME()
    {
        m_flags = 0;
        m_fSizeHint = false;
        m_fNextAttr = false;
        m_fRecordPlayers = false;
    }
    ~T5X_GAME()
    {
        for (vector<T5X_ATTRNAMEINFO *>::iterator it = m_vAttrNames.begin(); it != m_vAttrNames.end(); ++it)
        {
            delete *it;
        } 
        m_vAttrNames.clear();
        for (map<int, T5X_OBJECTINFO *, lti>::iterator it = m_mObjects.begin(); it != m_mObjects.end(); ++it)
        {
            delete it->second;
        } 
        m_mObjects.clear();
    }
};

extern T5X_GAME g_t5xgame;
extern int t5xparse();
extern FILE *t5xin;

char *t5x_ConvertAttributeName(const char *);

#endif
