#include "omega.h"
#include "t6hgame.h"
#include "p6hgame.h"
#include "t5xgame.h"

//
// The differences between TinyMUSH 3.x flatfiles are minor.
//
// TinyMUSH 3.1p0 introduced V_TIMESTAMPS and V_VISUALATTRS.  TinyMUSH 3.1p5
// added support for escaping CR, LF, and ESC in strings, but there is no
// corresponding flatfile flag which advertises this.  V_CREATETIME has been
// defined but not released.  Omega will consume V_CREATETIME but it will
// not generate it.
//

typedef struct _t6h_gameflaginfo
{
    unsigned    mask;
    const char *pName;
} t6h_gameflaginfo;

t6h_gameflaginfo t6h_gameflagnames[] =
{
    { T6H_V_ZONE,         "V_ZONE"        },
    { T6H_V_LINK,         "V_LINK"        },
    { T6H_V_GDBM,         "V_GDBM"        },
    { T6H_V_ATRNAME,      "V_ATRNAME"     },
    { T6H_V_ATRKEY,       "V_ATRKEY"      },
    { T6H_V_PARENT,       "V_PARENT"      },
    { T6H_V_COMM,         "V_COMM"        },
    { T6H_V_ATRMONEY,     "V_ATRMONEY"    },
    { T6H_V_XFLAGS,       "V_XFLAGS"      },
    { T6H_V_POWERS,       "V_POWERS"      },
    { T6H_V_3FLAGS,       "V_3FLAGS"      },
    { T6H_V_QUOTED,       "V_QUOTED"      },
    { T6H_V_TQUOTAS,      "V_TQUOTAS"     },
    { T6H_V_TIMESTAMPS,   "V_TIMESTAMPS"  },
    { T6H_V_VISUALATTRS,  "V_VISUALATTRS" },
    { T6H_V_CREATETIME,   "V_CREATETIME"  },
    { T6H_V_DBCLEAN,      "V_DBCLEAN"     },
};
#define T6H_NUM_GAMEFLAGNAMES (sizeof(t6h_gameflagnames)/sizeof(t6h_gameflagnames[0]))

T6H_GAME g_t6hgame;
T6H_LOCKEXP *t6hl_ParseKey(char *pKey);

// The first character of an attribute name must be either alphabetic,
// '_', '#', '.', or '~'. It's handled by the following table.
//
// Characters thereafter may be letters, numbers, and characters from
// the set {'?!`/-_.@#$^&~=+<>()%}. Lower-case letters are turned into
// uppercase before being used, but lower-case letters are valid input.
//
bool t6h_AttrNameInitialSet[256] =
{
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
//
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 1
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0,  // 2
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 3
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 4
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1,  // 5
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 6
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,  // 7

    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 8
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 9
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // A
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // B
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // C
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // D
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // E
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0   // F
};

bool t6h_AttrNameSet[256] =
{
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
//
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 1
    0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 1, 1, 1,  // 2
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1,  // 3
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 4
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1,  // 5
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 6
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 0,  // 7

    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 8
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 9
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // A
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // B
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // C
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // D
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // E
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0   // F
};

char *t6h_ConvertAttributeName(const char *pName)
{
    char aBuffer[256];
    char *pBuffer = aBuffer;
    if (  '\0' != *pName
       && pBuffer < aBuffer + sizeof(aBuffer) - 1)
    {
        if (t6h_AttrNameInitialSet[(unsigned char) *pName])
        {
            *pBuffer++ = *pName++;
        }
        else
        {
            *pBuffer++ = 'X';
        }
    }
    while (  '\0' != *pName
          && pBuffer < aBuffer + sizeof(aBuffer) - 1)
    {
        if (t6h_AttrNameSet[(unsigned char) *pName])
        {
            *pBuffer++ = *pName++;
        }
        else
        {
            *pBuffer++ = 'X';
            pName++;
        }
    }
    *pBuffer = '\0';
    return StringClone(aBuffer);
}

void T6H_LOCKEXP::Write(FILE *fp)
{
    switch (m_op)
    {
    case T6H_LOCKEXP::le_is:
        fprintf(fp, "(=");
        m_le[0]->Write(fp);
        fprintf(fp, ")");
        break;

    case T6H_LOCKEXP::le_carry:
        fprintf(fp, "(+");
        m_le[0]->Write(fp);
        fprintf(fp, ")");
        break;

    case T6H_LOCKEXP::le_indirect:
        fprintf(fp, "(@");
        m_le[0]->Write(fp);
        fprintf(fp, ")");
        break;

    case T6H_LOCKEXP::le_owner:
        fprintf(fp, "($");
        m_le[0]->Write(fp);
        fprintf(fp, ")");
        break;

    case T6H_LOCKEXP::le_and:
        fprintf(fp, "(");
        m_le[0]->Write(fp);
        fprintf(fp, "&");
        m_le[1]->Write(fp);
        fprintf(fp, ")");
        break;

    case T6H_LOCKEXP::le_or:
        fprintf(fp, "(");
        m_le[0]->Write(fp);
        fprintf(fp, "|");
        m_le[1]->Write(fp);
        fprintf(fp, ")");
        break;

    case T6H_LOCKEXP::le_not:
        fprintf(fp, "(!");
        m_le[0]->Write(fp);
        fprintf(fp, ")");
        break;

    case T6H_LOCKEXP::le_attr:
        {
            bool fText = (m_le[0]->m_op == T6H_LOCKEXP::le_text);
            bool fWildcard = false;
            if (fText)
            {
                char ch = m_le[0]->m_p[0][0];
                if ('+' == ch || '=' == ch)
                {
                    fWildcard = true;
                }
            }

            if (fWildcard) fprintf(fp, "(");

            m_le[0]->Write(fp);
            fprintf(fp, ":");
            m_le[1]->Write(fp);

            // The legacy behavior (from TinyMUX 1.6) not always emit a NL.
            // It's really a beneign typo, but we reproduce it to make
            // regression testing easier.
            //
            if (!fText)
            {
                fprintf(fp, "\n");
            }

            if (fWildcard) fprintf(fp, ")");
        }
        break;

    case T6H_LOCKEXP::le_eval:
        m_le[0]->Write(fp);
        fprintf(fp, "/");
        m_le[1]->Write(fp);
        fprintf(fp, "\n");
        break;

    case T6H_LOCKEXP::le_ref:
        fprintf(fp, "%d", m_dbRef);
        break;

    case T6H_LOCKEXP::le_text:
        fprintf(fp, "%s", m_p[0]);
        break;
    }
}

char *T6H_LOCKEXP::Write(char *p)
{
    switch (m_op)
    {
    case T6H_LOCKEXP::le_is:
        *p++ = '=';
        if (le_ref != m_le[0]->m_op)
        {
            *p++ = '(';
        }
        p = m_le[0]->Write(p);
        if (le_ref != m_le[0]->m_op)
        {
            *p++ = ')';
        }
        break;

    case T6H_LOCKEXP::le_carry:
        *p++ = '+';
        if (le_ref != m_le[0]->m_op)
        {
            *p++ = '(';
        }
        p = m_le[0]->Write(p);
        if (le_ref != m_le[0]->m_op)
        {
            *p++ = ')';
        }
        break;

    case T6H_LOCKEXP::le_indirect:
        *p++ = '@';
        if (le_ref != m_le[0]->m_op)
        {
            *p++ = '(';
        }
        p = m_le[0]->Write(p);
        if (le_ref != m_le[0]->m_op)
        {
            *p++ = ')';
        }
        break;

    case T6H_LOCKEXP::le_owner:
        *p++ = '$';
        if (le_ref != m_le[0]->m_op)
        {
            *p++ = '(';
        }
        p = m_le[0]->Write(p);
        if (le_ref != m_le[0]->m_op)
        {
            *p++ = ')';
        }
        break;

    case T6H_LOCKEXP::le_or:
        p = m_le[0]->Write(p);
        *p++ = '|';
        p = m_le[1]->Write(p);
        break;

    case T6H_LOCKEXP::le_not:
        *p++ = '!';
        if (  le_and == m_le[0]->m_op
           || le_or == m_le[0]->m_op)
        {
            *p++ = '(';
        }
        p = m_le[0]->Write(p);
        if (  le_and == m_le[0]->m_op
           || le_or == m_le[0]->m_op)
        {
            *p++ = ')';
        }
        break;

    case T6H_LOCKEXP::le_attr:
        p = m_le[0]->Write(p);
        *p++ = ':';
        p = m_le[1]->Write(p);
        break;

    case T6H_LOCKEXP::le_eval:
        p = m_le[0]->Write(p);
        *p++ = '/';
        p = m_le[1]->Write(p);
        break;

    case T6H_LOCKEXP::le_and:
        if (le_or == m_le[0]->m_op)
        {
            *p++ = '(';
        }
        p = m_le[0]->Write(p);
        if (le_or == m_le[0]->m_op)
        {
            *p++ = ')';
        }
        *p++ = '&';
        if (le_or == m_le[1]->m_op)
        {
            *p++ = '(';
        }
        p = m_le[1]->Write(p);
        if (le_or == m_le[1]->m_op)
        {
            *p++ = ')';
        }
        break;

    case T6H_LOCKEXP::le_ref:
        sprintf(p, "(#%d)", m_dbRef);
        p += strlen(p);
        break;

    case T6H_LOCKEXP::le_text:
        sprintf(p, "%s", m_p[0]);
        p += strlen(p);
        break;

    default:
        fprintf(stderr, "%d not recognized.\n", m_op);
        break;
    }
    return p;
}

bool T6H_LOCKEXP::ConvertFromP6H(P6H_LOCKEXP *p)
{
    switch (p->m_op)
    {
    case P6H_LOCKEXP::le_is:
        m_op = T6H_LOCKEXP::le_is;
        m_le[0] = new T6H_LOCKEXP;
        if (!m_le[0]->ConvertFromP6H(p->m_le[0]))
        {
            delete m_le[0];
            m_le[0] = NULL;
            return false;
        }
        break;

    case P6H_LOCKEXP::le_carry:
        m_op = T6H_LOCKEXP::le_carry;
        m_le[0] = new T6H_LOCKEXP;
        if (!m_le[0]->ConvertFromP6H(p->m_le[0]))
        {
            delete m_le[0];
            m_le[0] = NULL;
            return false;
        }
        break;

    case P6H_LOCKEXP::le_indirect:
        m_op = T6H_LOCKEXP::le_indirect;
        m_le[0] = new T6H_LOCKEXP;
        if (!m_le[0]->ConvertFromP6H(p->m_le[0]))
        {
            delete m_le[0];
            m_le[0] = NULL;
            return false;
        }
        break;

    case P6H_LOCKEXP::le_indirect2:
        return false;
        break;

    case P6H_LOCKEXP::le_owner:
        m_op = T6H_LOCKEXP::le_owner;
        m_le[0] = new T6H_LOCKEXP;
        if (!m_le[0]->ConvertFromP6H(p->m_le[0]))
        {
            delete m_le[0];
            m_le[0] = NULL;
            return false;
        }
        break;

    case P6H_LOCKEXP::le_or:
        m_op = T6H_LOCKEXP::le_or;
        m_le[0] = new T6H_LOCKEXP;
        m_le[1] = new T6H_LOCKEXP;
        if (  !m_le[0]->ConvertFromP6H(p->m_le[0])
           || !m_le[1]->ConvertFromP6H(p->m_le[1]))
        {
            delete m_le[0];
            delete m_le[1];
            m_le[0] = m_le[1] = NULL;
            return false;
        }
        break;

    case P6H_LOCKEXP::le_not:
        m_op = T6H_LOCKEXP::le_not;
        m_le[0] = new T6H_LOCKEXP;
        if (!m_le[0]->ConvertFromP6H(p->m_le[0]))
        {
            delete m_le[0];
            m_le[0] = NULL;
            return false;
        }
        break;

    case P6H_LOCKEXP::le_attr:
        m_op = T6H_LOCKEXP::le_attr;
        m_le[0] = new T6H_LOCKEXP;
        m_le[1] = new T6H_LOCKEXP;
        if (  !m_le[0]->ConvertFromP6H(p->m_le[0])
           || !m_le[1]->ConvertFromP6H(p->m_le[1]))
        {
            delete m_le[0];
            delete m_le[1];
            m_le[0] = m_le[1] = NULL;
            return false;
        }
        break;

    case P6H_LOCKEXP::le_eval:
        m_op = T6H_LOCKEXP::le_eval;
        m_le[0] = new T6H_LOCKEXP;
        m_le[1] = new T6H_LOCKEXP;
        if (  !m_le[0]->ConvertFromP6H(p->m_le[0])
           || !m_le[1]->ConvertFromP6H(p->m_le[1]))
        {
            delete m_le[0];
            delete m_le[1];
            m_le[0] = m_le[1] = NULL;
            return false;
        }
        break;

    case P6H_LOCKEXP::le_and:
        m_op = T6H_LOCKEXP::le_and;
        m_le[0] = new T6H_LOCKEXP;
        m_le[1] = new T6H_LOCKEXP;
        if (  !m_le[0]->ConvertFromP6H(p->m_le[0])
           || !m_le[1]->ConvertFromP6H(p->m_le[1]))
        {
            delete m_le[0];
            delete m_le[1];
            m_le[0] = m_le[1] = NULL;
            return false;
        }
        break;

    case P6H_LOCKEXP::le_ref:
        m_op = T6H_LOCKEXP::le_ref;
        m_dbRef = p->m_dbRef;
        break;

    case P6H_LOCKEXP::le_text:
        m_op = T6H_LOCKEXP::le_text;
        m_p[0] = StringClone(p->m_p[0]);
        break;

    case P6H_LOCKEXP::le_class:
        return false;
        break;

    case P6H_LOCKEXP::le_true:
        m_op = T6H_LOCKEXP::le_text;
        m_p[0] = StringClone("1");
        break;

    case P6H_LOCKEXP::le_false:
        m_op = T6H_LOCKEXP::le_text;
        m_p[0] = StringClone("0");
        break;

    default:
        fprintf(stderr, "%d not recognized.\n", m_op);
        break;
    }
    return true;
}

const char *ConvertT5XValue(char *pValue)
{
    static char buffer[65536];
    char *p = buffer;
    while ('\0' != *pValue)
    {
        if (*pValue < 0x7F)
        {
            *p++ = *pValue;
        }
        else
        {
            *p++ = '?';
        }
        pValue++;
    }
    *p = '\0';
    return buffer;
}

const char *ConvertT5XAttrName(char *pName)
{
    static char buffer[65536];
    char *p = buffer;
    while (  '\0' != *pName
          && isdigit(*pName))
    {
        *p++ = *pName++;
    }
    if  (  '\0' != *pName
        && ':' == *pName)
    {
        *p++ = *pName++;
    }
    bool fFirst = true;
    while ('\0' != *pName)
    {
        if (  (  fFirst
              && !t6h_AttrNameInitialSet[(unsigned char)*pName])
           || (  !fFirst
              && !t6h_AttrNameSet[(unsigned char)*pName]))
        {
            *p++ = 'X';
        }
        else
        {
            *p++ = *pName;
        }
        fFirst = false;
        pName++;
    }
    *p = '\0';
    return buffer;
}

bool T6H_LOCKEXP::ConvertFromT5X(T5X_LOCKEXP *p)
{
    switch (p->m_op)
    {
    case T5X_LOCKEXP::le_is:
        m_op = T6H_LOCKEXP::le_is;
        m_le[0] = new T6H_LOCKEXP;
        if (!m_le[0]->ConvertFromT5X(p->m_le[0]))
        {
            delete m_le[0];
            m_le[0] = NULL;
            return false;
        }
        break;

    case T5X_LOCKEXP::le_carry:
        m_op = T6H_LOCKEXP::le_carry;
        m_le[0] = new T6H_LOCKEXP;
        if (!m_le[0]->ConvertFromT5X(p->m_le[0]))
        {
            delete m_le[0];
            m_le[0] = NULL;
            return false;
        }
        break;

    case T5X_LOCKEXP::le_indirect:
        m_op = T6H_LOCKEXP::le_indirect;
        m_le[0] = new T6H_LOCKEXP;
        if (!m_le[0]->ConvertFromT5X(p->m_le[0]))
        {
            delete m_le[0];
            m_le[0] = NULL;
            return false;
        }
        break;

    case T5X_LOCKEXP::le_owner:
        m_op = T6H_LOCKEXP::le_owner;
        m_le[0] = new T6H_LOCKEXP;
        if (!m_le[0]->ConvertFromT5X(p->m_le[0]))
        {
            delete m_le[0];
            m_le[0] = NULL;
            return false;
        }
        break;

    case T5X_LOCKEXP::le_or:
        m_op = T6H_LOCKEXP::le_or;
        m_le[0] = new T6H_LOCKEXP;
        m_le[1] = new T6H_LOCKEXP;
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
        m_op = T6H_LOCKEXP::le_not;
        m_le[0] = new T6H_LOCKEXP;
        if (!m_le[0]->ConvertFromT5X(p->m_le[0]))
        {
            delete m_le[0];
            m_le[0] = NULL;
            return false;
        }
        break;

    case T5X_LOCKEXP::le_attr:
        m_op = T6H_LOCKEXP::le_attr;
        m_le[0] = new T6H_LOCKEXP;
        m_le[1] = new T6H_LOCKEXP;
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
        m_op = T6H_LOCKEXP::le_eval;
        m_le[0] = new T6H_LOCKEXP;
        m_le[1] = new T6H_LOCKEXP;
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
        m_op = T6H_LOCKEXP::le_and;
        m_le[0] = new T6H_LOCKEXP;
        m_le[1] = new T6H_LOCKEXP;
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
        m_op = T6H_LOCKEXP::le_ref;
        m_dbRef = p->m_dbRef;
        break;

    case T5X_LOCKEXP::le_text:
        m_op = T6H_LOCKEXP::le_text;
        m_p[0] = StringClone(ConvertT5XValue(p->m_p[0]));
        break;

    default:
        fprintf(stderr, "%d not recognized.\n", m_op);
        break;
    }
    return true;
}

void T6H_ATTRNAMEINFO::SetNumFlagsAndName(int iNum, int iFlags, char *pName)
{
    free(m_pNameEncoded);
    m_fNumAndName = true;
    m_iNum = iNum;
    m_iFlags = iFlags;

    char buffer[65536];
    sprintf(buffer, "%d:", m_iFlags);
    size_t n = strlen(buffer);
    sprintf(buffer + n, "%s", pName);
    free(pName);

    m_pNameEncoded   = StringClone(buffer);
    m_pNameUnencoded = m_pNameEncoded + n;
}

void T6H_ATTRNAMEINFO::SetNumAndName(int iNum, char *pName)
{
    m_fNumAndName = true;
    m_iNum = iNum;

    m_pNameEncoded = pName;
    m_pNameUnencoded = NULL;
    m_iFlags = 0;

    // Get the flags.
    //
    char *cp = m_pNameEncoded;
    int tmp_flags = 0;
    unsigned char ch = *cp;
    while (isdigit(ch))
    {
        cp++;
        tmp_flags = 10*tmp_flags + (ch-'0');
        ch = *cp;
    }

    // If delimiter is not ':', just return attribute.
    //
    if (*cp++ != ':')
    {
        m_pNameUnencoded = m_pNameEncoded;
        fprintf(stderr, "WARNING, User attribute (%s) does not contain a flag sub-field.\n", m_pNameEncoded);
        return;
    }

    m_iFlags = tmp_flags;
    m_pNameUnencoded = cp;
}

static char *EncodeString(const char *str, bool fExtraEscapes)
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
            *p++ = *str++;
        }
        else if (fExtraEscapes && '\r' == *str)
        {
            *p++ = '\\';
            *p++ = 'r';
            str++;
        }
        else if (fExtraEscapes && '\n' == *str)
        {
            *p++ = '\\';
            *p++ = 'n';
            str++;
        }
        else if (fExtraEscapes && '\t' == *str)
        {
            *p++ = '\\';
            *p++ = 't';
            str++;
        }
        else if (fExtraEscapes && '\x1B' == *str)
        {
            *p++ = '\\';
            *p++ = 'e';
            str++;
        }
        else
        {
            *p++ = *str++;
        }
    }
    *p = '\0';
    return buf;
}

void T6H_ATTRNAMEINFO::Write(FILE *fp, bool fExtraEscapes)
{
    if (m_fNumAndName)
    {
        fprintf(fp, "+A%d\n\"%s\"\n", m_iNum, EncodeString(m_pNameEncoded, fExtraEscapes));
    }
}

void T6H_OBJECTINFO::SetName(char *pName)
{
    if (NULL != m_pName)
    {
        free(m_pName);
    }
    m_pName = pName;
}

static struct
{
    const char *pName;
    int         iNum;
} t6h_locks[] =
{
    { "chownlock",   T6H_A_LCHOWN    },
    { "controllock", T6H_A_LCONTROL  },
    { "defaultlock", T6H_A_LOCK      },
    { "darklock",    T6H_A_LDARK     },
    { "droplock",    T6H_A_LDROP     },
    { "enterlock",   T6H_A_LENTER    },
    { "givelock",    T6H_A_LGIVE     },
    { "heardlock",   T6H_A_LHEARD    },
    { "hearslock",   T6H_A_LHEARS    },
    { "knownlock",   T6H_A_LKNOWN    },
    { "knowslock",   T6H_A_LKNOWS    },
    { "leavelock",   T6H_A_LLEAVE    },
    { "linklock",    T6H_A_LLINK     },
    { "movedlock",   T6H_A_LMOVED    },
    { "moveslock",   T6H_A_LMOVES    },
    { "pagelock",    T6H_A_LPAGE     },
    { "parentlock",  T6H_A_LPARENT   },
    { "receivelock", T6H_A_LRECEIVE  },
    { "teloutlock",  T6H_A_LTELOUT   },
    { "tportlock",   T6H_A_LTPORT    },
    { "uselock",     T6H_A_LUSE      },
    { "userlock",    T6H_A_LUSER     },
    { "speechlock",  T6H_A_LSPEECH   },
};

void T6H_OBJECTINFO::SetAttrs(int nAttrs, vector<T6H_ATTRINFO *> *pvai)
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

    if (NULL != m_pvai)
    {
        for (vector<T6H_ATTRINFO *>::iterator it = m_pvai->begin(); it != m_pvai->end(); ++it)
        {
            (*it)->m_fIsLock = false;
            for (int i = 0; i < sizeof(t6h_locks)/sizeof(t6h_locks[0]); i++)
            {
                if (t6h_locks[i].iNum == (*it)->m_iNum)
                {
                    char *pValue = (NULL != (*it)->m_pValueUnencoded) ? (*it)->m_pValueUnencoded : (*it)->m_pValueEncoded;
                    (*it)->m_fIsLock = true;
                    (*it)->m_pKeyTree = t6hl_ParseKey(pValue);
                    if (NULL == (*it)->m_pKeyTree)
                    {
                       fprintf(stderr, "WARNING: Lock key '%s' is not valid.\n", pValue);
                    }
                    break;
                }
            }
        }
    }
}

void T6H_ATTRINFO::SetNumOwnerFlagsAndValue(int iNum, int dbAttrOwner, int iAttrFlags, char *pValue)
{
    m_fNumAndValue = true;
    free(m_pAllocated);
    m_pAllocated = pValue;

    m_iNum    = iNum;
    m_pValueUnencoded  = pValue;
    m_pValueEncoded = NULL;
    m_iFlags  = iAttrFlags;
    m_dbOwner = dbAttrOwner;

    m_kState  = kEncode;
}

void T6H_ATTRINFO::SetNumAndValue(int iNum, char *pValue)
{
    m_fNumAndValue = true;
    free(m_pAllocated);
    m_pAllocated = pValue;

    m_iNum    = iNum;
    m_pValueUnencoded  = NULL;
    m_pValueEncoded = pValue;
    m_iFlags  = 0;
    m_dbOwner = T6H_NOTHING;

    m_kState  = kDecode;
}

void T6H_ATTRINFO::EncodeDecode(int dbObjOwner)
{
    if (kEncode == m_kState)
    {
        // If using the default owner and flags (almost all attributes will),
        // just store the string.
        //
        if (  (  m_dbOwner == dbObjOwner
              || T6H_NOTHING == m_dbOwner)
           && 0 == m_iFlags)
        {
            m_pValueEncoded = m_pValueUnencoded;
        }
        else
        {
            // Encode owner and flags into the attribute text.
            //
            if (T6H_NOTHING == m_dbOwner)
            {
                m_dbOwner = dbObjOwner;
            }

            char buffer[65536];
            sprintf(buffer, "%c%d:%d:", ATR_INFO_CHAR, m_dbOwner, m_iFlags);
            size_t n = strlen(buffer);
            sprintf(buffer + n, "%s", m_pValueUnencoded);

            free(m_pAllocated);
            m_pAllocated = StringClone(buffer);

            m_pValueEncoded = m_pAllocated;
            m_pValueUnencoded = m_pAllocated + n;
        }
        m_kState = kNone;
    }
    else if (kDecode == m_kState)
    {
        // See if the first char of the attribute is the special character
        //
        m_iFlags = 0;
        if (ATR_INFO_CHAR != *m_pValueEncoded)
        {
            m_dbOwner = dbObjOwner;
            m_pValueUnencoded = m_pValueEncoded;
        }

        // It has the special character, crack the attr apart.
        //
        char *cp = m_pValueEncoded + 1;

        // Get the attribute owner
        //
        bool neg = false;
        if (*cp == '-')
        {
            neg = true;
            cp++;
        }
        int tmp_owner = 0;
        unsigned int ch = *cp;
        while (isdigit(ch))
        {
            cp++;
            tmp_owner = 10*tmp_owner + (ch-'0');
            ch = *cp;
        }
        if (neg)
        {
            tmp_owner = -tmp_owner;
        }

        // If delimiter is not ':', just return attribute
        //
        if (*cp++ != ':')
        {
            m_dbOwner = dbObjOwner;
            m_pValueUnencoded = m_pValueEncoded;
            return;
        }

        // Get the attribute flags.
        //
        int tmp_flags = 0;
        ch = *cp;
        while (isdigit(ch))
        {
            cp++;
            tmp_flags = 10*tmp_flags + (ch-'0');
            ch = *cp;
        }

        // If delimiter is not ':', just return attribute.
        //
        if (*cp++ != ':')
        {
            m_dbOwner = dbObjOwner;
            m_pValueUnencoded = m_pValueEncoded;
            return;
        }

        // Get the attribute text.
        //
        if (tmp_owner < 0)
        {
            m_dbOwner = dbObjOwner;
        }
        else
        {
            m_dbOwner = tmp_owner;
        }
        m_iFlags = tmp_flags;
        m_pValueUnencoded = cp;

        m_kState = kNone;
    }
}

void T6H_GAME::AddNumAndName(int iNum, char *pName)
{
    T6H_ATTRNAMEINFO *pani = new T6H_ATTRNAMEINFO;
    pani->SetNumAndName(iNum, pName);

    map<int, T6H_ATTRNAMEINFO *, lti>::iterator itName = m_mAttrNames.find(iNum);
    if (itName != m_mAttrNames.end())
    {
        fprintf(stderr, "WARNING: Duplicate attribute number %s(%d) conflicts with %s(%d)\n",
            pani->m_pNameUnencoded, iNum, itName->second->m_pNameUnencoded, itName->second->m_iNum);
        delete pani;
        return;
    }
    map<char *, T6H_ATTRNAMEINFO *, ltstr>::iterator itNum = m_mAttrNums.find(pani->m_pNameUnencoded);
    if (itNum != m_mAttrNums.end())
    {
        fprintf(stderr, "WARNING: Duplicate attribute name %s(%d) conflicts with %s(%d)\n",
            pani->m_pNameUnencoded, iNum, itNum->second->m_pNameUnencoded, itNum->second->m_iNum);
        delete pani;
        return;
    }
    m_mAttrNames[iNum] = pani;
    m_mAttrNamesInSitu[++m_iOrder] = iNum;
    m_mAttrNums[pani->m_pNameUnencoded] = pani;
}

void T6H_GAME::AddObject(T6H_OBJECTINFO *poi)
{
    m_mObjects[poi->m_dbRef] = poi;
}

void T6H_GAME::ValidateFlags() const
{
    int flags = m_flags;
    int ver = (m_flags & T6H_V_MASK);
    fprintf(stderr, "INFO: Flatfile version is %d\n", ver);
    if (ver < 1 || 1 < ver)
    {
        fprintf(stderr, "WARNING: Expecting version to be 1.\n");
    }
    flags &= ~T6H_V_MASK;
    int tflags = flags;

    const int Mask31p0 = T6H_V_TIMESTAMPS
                       | T6H_V_VISUALATTRS;
    const int Mask32   = Mask31p0
                       | T6H_V_CREATETIME;
    if ((m_flags & Mask32) == Mask32)
    {
        fprintf(stderr, "INFO: Flatfile produced by TinyMUSH 3.2 or later.\n");
    }
    else if ((m_flags & Mask31p0) == Mask31p0)
    {
        if (m_fExtraEscapes)
        {
            fprintf(stderr, "INFO: Flatfile produced by TinyMUSH 3.1p5 or 3.1p6\n");
        }
        else
        {
            fprintf(stderr, "INFO: Flatfile produced by TinyMUSH 3.1p0 to 3.1p4\n");
        }
    }
    else
    {
        fprintf(stderr, "INFO: Flatfile produced by TinyMUSH 3.0.\n");
    }

    fprintf(stderr, "INFO: Flatfile flags are ");
    for (int i = 0; i < T6H_NUM_GAMEFLAGNAMES; i++)
    {
        if (t6h_gameflagnames[i].mask & tflags)
        {
            fprintf(stderr, "%s ", t6h_gameflagnames[i].pName);
            tflags &= ~t6h_gameflagnames[i].mask;
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
    if (  1 == ver
       && (flags & T6H_MANDFLAGS_V1) != T6H_MANDFLAGS_V1)
    {
        fprintf(stderr, "WARNING: Not all mandatory flags for v1 are present.\n");
    }

    // Validate that this is a flatfile and not a structure file.
    //
    if (  (flags & T6H_V_GDBM) != 0
       || (flags & T6H_V_ATRNAME) != 0
       || (flags & T6H_V_ATRMONEY) != 0)
    {
        fprintf(stderr, "WARNING: Expected a flatfile (with strings) instead of a structure file (with only object anchors).\n");
    }
}

void T6H_GAME::Pass2()
{
    for (map<int, T6H_OBJECTINFO *, lti>::iterator itObj = m_mObjects.begin(); itObj != m_mObjects.end(); ++itObj)
    {
        if (NULL != itObj->second->m_pvai)
        {
            for (vector<T6H_ATTRINFO *>::iterator itAttr = itObj->second->m_pvai->begin(); itAttr != itObj->second->m_pvai->end(); ++itAttr)
            {
                (*itAttr)->EncodeDecode(itObj->second->m_dbOwner);
            }
        }
    }
}

void T6H_GAME::ValidateObjects() const
{
    int dbRefMax = 0;
    for (map<int, T6H_OBJECTINFO *, lti>::const_iterator it = m_mObjects.begin(); it != m_mObjects.end(); ++it)
    {
        it->second->Validate();
        if (dbRefMax < it->first)
        {
            dbRefMax = it->first;
        }
    }

    if (!m_fSizeHint)
    {
        fprintf(stderr, "WARNING: +S phrase for next object was missing.\n");
    }
    else
    {
        if (m_nSizeHint < dbRefMax+1)
        {
            fprintf(stderr, "WARNING: +S phrase does not leave room for the dbrefs.\n");
        }
        else if (m_nSizeHint != dbRefMax+1)
        {
            fprintf(stderr, "WARNING: +S phrase does not agree with last object.\n");
        }
    }
}

void T6H_ATTRNAMEINFO::Validate(int ver) const
{
    if (m_fNumAndName)
    {
        if (m_iNum < A_USER_START)
        {
            fprintf(stderr, "WARNING: User attribute (%s) uses an attribute number (%d) which is below %d.\n", m_pNameUnencoded, m_iNum, A_USER_START);
        }
        char *q = m_pNameUnencoded;

        bool fValid = true;
        if (!t6h_AttrNameInitialSet[(unsigned char)*q])
        {
            fValid = false;
        }
        else if ('\0' != *q)
        {
            q++;
            while ('\0' != *q)
            {
                if (!t6h_AttrNameSet[(unsigned char)*q])
                {
                    fValid = false;
                    break;
                }
                q++;
            }
        }
        if (!fValid)
        {
            fprintf(stderr, "WARNING, User attribute (%s) name is not valid.\n", m_pNameUnencoded);
        }
    }
    else
    {
        fprintf(stderr, "WARNING: Unexpected ATTRNAMEINFO -- internal error.\n");
    }
}

void T6H_GAME::ValidateAttrNames(int ver) const
{
    if (!m_fNextAttr)
    {
        fprintf(stderr, "WARNING: +N phrase for attribute count was missing.\n");
    }
    else
    {
        int n = 256;
        for (map<int, T6H_ATTRNAMEINFO *, lti>::const_iterator it = m_mAttrNames.begin(); it != m_mAttrNames.end(); ++it)
        {
            it->second->Validate(ver);
            if (it->second->m_fNumAndName)
            {
                int iNum = it->second->m_iNum;
                if (n <= iNum)
                {
                    n = iNum + 1;
                }
            }
        }
        if (m_nNextAttr != n)
        {
            fprintf(stderr, "WARNING: +N phrase (%d) does not agree with the maximum attribute number (%d).\n", m_nNextAttr, n);
        }
    }
}

void T6H_GAME::Validate() const
{
    fprintf(stderr, "TinyMUSH\n");

    int ver = (m_flags & T6H_V_MASK);
    ValidateFlags();
    ValidateAttrNames(ver);
    ValidateObjects();
}

void T6H_OBJECTINFO::Write(FILE *fp, bool bWriteLock, bool fExtraEscapes)
{
    fprintf(fp, "!%d\n", m_dbRef);
    if (NULL != m_pName)
    {
        fprintf(fp, "\"%s\"\n", EncodeString(m_pName, fExtraEscapes));
    }
    if (m_fLocation)
    {
        fprintf(fp, "%d\n", m_dbLocation);
    }
    if (m_fZone)
    {
        fprintf(fp, "%d\n", m_dbZone);
    }
    if (m_fContents)
    {
        fprintf(fp, "%d\n", m_dbContents);
    }
    if (m_fExits)
    {
        fprintf(fp, "%d\n", m_dbExits);
    }
    if (m_fLink)
    {
        fprintf(fp, "%d\n", m_dbLink);
    }
    if (m_fNext)
    {
        fprintf(fp, "%d\n", m_dbNext);
    }
    if (bWriteLock)
    {
        if (NULL == m_ple)
        {
            fprintf(fp, "\n");
        }
        else
        {
            m_ple->Write(fp);
            fprintf(fp, "\n");
        }
    }
    if (m_fOwner)
    {
        fprintf(fp, "%d\n", m_dbOwner);
    }
    if (m_fParent)
    {
        fprintf(fp, "%d\n", m_dbParent);
    }
    if (m_fPennies)
    {
        fprintf(fp, "%d\n", m_iPennies);
    }
    if (m_fFlags1)
    {
        fprintf(fp, "%d\n", m_iFlags1);
    }
    if (m_fFlags2)
    {
        fprintf(fp, "%d\n", m_iFlags2);
    }
    if (m_fFlags3)
    {
        fprintf(fp, "%d\n", m_iFlags3);
    }
    if (m_fPowers1)
    {
        fprintf(fp, "%d\n", m_iPowers1);
    }
    if (m_fPowers2)
    {
        fprintf(fp, "%d\n", m_iPowers2);
    }
    if (m_fAccessed)
    {
        fprintf(fp, "%d\n", m_iAccessed);
    }
    if (m_fModified)
    {
        fprintf(fp, "%d\n", m_iModified);
    }
    if (m_fCreated)
    {
        fprintf(fp, "%d\n", m_iCreated);
    }
    if (  m_fAttrCount
       && NULL != m_pvai)
    {
        for (vector<T6H_ATTRINFO *>::iterator it = m_pvai->begin(); it != m_pvai->end(); ++it)
        {
            (*it)->Write(fp, fExtraEscapes);
        }
    }
    fprintf(fp, "<\n");
}

void T6H_ATTRINFO::Validate() const
{
    if (  m_fNumAndValue
       && m_fIsLock
       && NULL != m_pKeyTree)
    {
        char buffer[65536];
        char *p = m_pKeyTree->Write(buffer);
        *p = '\0';
        if (strcmp(m_pValueUnencoded, buffer) != 0)
        {
            fprintf(stderr, "WARNING: Re-generated lock key '%s' does not agree with parsed key '%s'.\n", buffer, m_pValueUnencoded);
        }
    }
}

void T6H_OBJECTINFO::Validate() const
{
    int iType = -1;
    if (m_fFlags1)
    {
        iType = (m_iFlags1) & T6H_TYPE_MASK;
    }

    map<int, T6H_OBJECTINFO *, lti>::const_iterator itFound;
    if (m_fLocation)
    {
        if (m_dbLocation < 0)
        {
            if (  m_dbLocation != T6H_NOTHING
               && (  T6H_TYPE_ROOM != iType
                  || T6H_HOME      != m_dbLocation)
               && (  T6H_TYPE_EXIT != iType
                  || T6H_AMBIGUOUS != m_dbLocation))
            {
                fprintf(stderr, "WARNING: Location (#%d) of object #%d is unexpected.\n", m_dbLocation, m_dbRef);
            }
        }
        else
        {
            itFound = g_t6hgame.m_mObjects.find(m_dbLocation);
            if (itFound == g_t6hgame.m_mObjects.end())
            {
                fprintf(stderr, "WARNING: Location (#%d) of object #%d does not exist.\n", m_dbLocation, m_dbRef);
            }
        }
    }
    if (  m_fContents
       && T6H_NOTHING != m_dbContents)
    {
        itFound = g_t6hgame.m_mObjects.find(m_dbContents);
        if (itFound == g_t6hgame.m_mObjects.end())
        {
            fprintf(stderr, "WARNING: Contents (#%d) of object #%d does not exist.\n", m_dbContents, m_dbRef);
        }
    }
    if (  m_fExits
       && T6H_NOTHING != m_dbExits)
    {
        itFound = g_t6hgame.m_mObjects.find(m_dbExits);
        if (itFound == g_t6hgame.m_mObjects.end())
        {
            fprintf(stderr, "WARNING: Exits (#%d) of object #%d does not exist.\n", m_dbExits, m_dbRef);
        }
    }
    if (  m_fNext
       && T6H_NOTHING != m_dbNext)
    {
        itFound = g_t6hgame.m_mObjects.find(m_dbNext);
        if (itFound == g_t6hgame.m_mObjects.end())
        {
            fprintf(stderr, "WARNING: Next (#%d) of object #%d does not exist.\n", m_dbNext, m_dbRef);
        }
    }
    if (  m_fParent
       && T6H_NOTHING != m_dbParent)
    {
        itFound = g_t6hgame.m_mObjects.find(m_dbParent);
        if (itFound == g_t6hgame.m_mObjects.end())
        {
            fprintf(stderr, "WARNING: Parent (#%d) of object #%d does not exist.\n", m_dbParent, m_dbRef);
        }
    }
    if (  m_fOwner
       && T6H_NOTHING != m_dbOwner)
    {
        itFound = g_t6hgame.m_mObjects.find(m_dbOwner);
        if (itFound == g_t6hgame.m_mObjects.end())
        {
            fprintf(stderr, "WARNING: Owner (#%d) of object #%d does not exist.\n", m_dbOwner, m_dbRef);
        }
    }
    if (  m_fZone
       && T6H_NOTHING != m_dbZone)
    {
        itFound = g_t6hgame.m_mObjects.find(m_dbZone);
        if (itFound == g_t6hgame.m_mObjects.end())
        {
            fprintf(stderr, "WARNING: Zone (#%d) of object #%d does not exist.\n", m_dbZone, m_dbRef);
        }
    }
    if (  m_fLink
       && T6H_NOTHING != m_dbLink)
    {
        itFound = g_t6hgame.m_mObjects.find(m_dbLink);
        if (itFound == g_t6hgame.m_mObjects.end())
        {
            fprintf(stderr, "WARNING: Link (#%d) of object #%d does not exist.\n", m_dbLink, m_dbRef);
        }
    }

    if (  m_fAttrCount
       && NULL != m_pvai)
    {
        for (vector<T6H_ATTRINFO *>::iterator it = m_pvai->begin(); it != m_pvai->end(); ++it)
        {
            (*it)->Validate();
        }
    }
}

void T6H_ATTRINFO::Write(FILE *fp, bool fExtraEscapes) const
{
    if (m_fNumAndValue)
    {
        fprintf(fp, ">%d\n\"%s\"\n", m_iNum, EncodeString(m_pValueEncoded, fExtraEscapes));
    }
}

void T6H_GAME::Write(FILE *fp)
{
    fprintf(fp, "+T%d\n", m_flags);
    if (m_fSizeHint)
    {
        fprintf(fp, "+S%d\n", m_nSizeHint);
    }
    if (m_fNextAttr)
    {
        fprintf(fp, "+N%d\n", m_nNextAttr);
    }
    if (m_fRecordPlayers)
    {
        fprintf(fp, "-R%d\n", m_nRecordPlayers);
    }
    for (map<int, int, lti>::iterator it = m_mAttrNamesInSitu.begin(); it != m_mAttrNamesInSitu.end(); ++it)
    {
        T6H_ATTRNAMEINFO *pani = m_mAttrNames[it->second];
        pani->Write(fp, m_fExtraEscapes);
    }
    for (map<int, T6H_OBJECTINFO *, lti>::iterator it = m_mObjects.begin(); it != m_mObjects.end(); ++it)
    {
        it->second->Write(fp, (m_flags & T6H_V_ATRKEY) == 0, m_fExtraEscapes);
    }

    fprintf(fp, "***END OF DUMP***\n");
}

static int p6h_convert_type[] =
{
    T6H_NOTYPE,        //  0
    T6H_TYPE_ROOM,     //  1
    T6H_TYPE_THING,    //  2
    T6H_NOTYPE,        //  3
    T6H_TYPE_EXIT,     //  4
    T6H_NOTYPE,        //  5
    T6H_NOTYPE,        //  6
    T6H_NOTYPE,        //  7
    T6H_TYPE_PLAYER,   //  8
    T6H_NOTYPE,        //  9
    T6H_NOTYPE,        // 10
    T6H_NOTYPE,        // 11
    T6H_NOTYPE,        // 12
    T6H_NOTYPE,        // 13
    T6H_NOTYPE,        // 14
    T6H_NOTYPE,        // 15
    T6H_TYPE_GARBAGE,  // 16
};

static NameMask p6h_convert_obj_flags1[] =
{
    { "TRANSPARENT",    T6H_SEETHRU     },
    { "WIZARD",         T6H_WIZARD      },
    { "LINK_OK",        T6H_LINK_OK     },
    { "DARK",           T6H_DARK        },
    { "JUMP_OK",        T6H_JUMP_OK     },
    { "STICKY",         T6H_STICKY      },
    { "DESTROY_OK",     T6H_DESTROY_OK  },
    { "HAVEN",          T6H_HAVEN       },
    { "QUIET",          T6H_QUIET       },
    { "HALT",           T6H_HALT        },
    { "DEBUG",          T6H_TRACE       },
    { "GOING",          T6H_GOING       },
    { "MONITOR",        T6H_MONITOR     },
    { "MYOPIC",         T6H_MYOPIC      },
    { "PUPPET",         T6H_PUPPET      },
    { "CHOWN_OK",       T6H_CHOWN_OK    },
    { "ENTER_OK",       T6H_ENTER_OK    },
    { "VISUAL",         T6H_VISUAL      },
    { "OPAQUE",         T6H_OPAQUE      },
    { "VERBOSE",        T6H_VERBOSE     },
    { "NOSPOOF",        T6H_NOSPOOF     },
    { "SAFE",           T6H_SAFE        },
    { "ROYALTY",        T6H_ROYALTY     },
    { "AUDIBLE",        T6H_HEARTHRU    },
    { "TERSE",          T6H_TERSE       },
};

static NameMask p6h_convert_obj_flags2[] =
{
    { "ABODE",          T6H_ABODE       },
    { "FLOATING",       T6H_FLOATING    },
    { "UNFINDABLE",     T6H_UNFINDABLE  },
    { "LIGHT",          T6H_LIGHT       },
    { "ANSI",           T6H_ANSI        },
    { "COLOR",          T6H_ANSI        },
    { "FIXED",          T6H_FIXED       },
    { "UNINSPECTED",    T6H_UNINSPECTED },
    { "GAGGED",         T6H_GAGGED      },
    { "ON-VACATION",    T6H_VACATION    },
    { "SUSPECT",        T6H_SUSPECT     },
    { "SLAVE",          T6H_SLAVE       },
};

static NameMask p6h_convert_obj_powers1[] =
{
    { "Announce",       T6H_POW_ANNOUNCE    },
    { "Boot",           T6H_POW_BOOT        },
    { "Guest",          T6H_POW_GUEST       },
    { "Halt",           T6H_POW_HALT        },
    { "Hide",           T6H_POW_HIDE        },
    { "Idle",           T6H_POW_IDLE        },
    { "Long_Fingers",   T6H_POW_LONGFINGERS },
    { "No_Pay",         T6H_POW_FREE_MONEY  },
    { "No_Quota",       T6H_POW_FREE_QUOTA  },
    { "Poll",           T6H_POW_POLL        },
    { "Quotas",         T6H_POW_CHG_QUOTAS  },
    { "Search",         T6H_POW_SEARCH      },
    { "See_All",        T6H_POW_EXAM_ALL    },
    { "See_Queue",      T6H_POW_SEE_QUEUE   },
    { "Tport_Anything", T6H_POW_TEL_UNRST   },
    { "Tport_Anywhere", T6H_POW_TEL_ANYWHR  },
    { "Unkillable",     T6H_POW_UNKILLABLE  },
};

static NameMask p6h_convert_obj_powers2[] =
{
    { "Builder",        T6H_POW_BUILDER },
};

static struct
{
    const char *pName;
    int         iNum;
} t6h_known_attrs[] =
{
    { "AAHEAR",         T6H_A_AAHEAR      },
    { "ACLONE",         T6H_A_ACLONE      },
    { "ACONNECT",       T6H_A_ACONNECT    },
    { "ADESC",          -1                },  // rename ADESC to XADESC
    { "ADESCRIBE",      T6H_A_ADESC       },  // rename ADESCRIBE to ADESC
    { "ADFAIL",         -1                },  // rename ADFAIL to XADFAIL
    { "ADISCONNECT",    T6H_A_ADISCONNECT },
    { "ADROP",          T6H_A_ADROP       },
    { "AEFAIL",         T6H_A_AEFAIL      },
    { "AENTER",         T6H_A_AENTER      },
    { "AFAIL",          -1                }, // rename AFAIL to XAFAIL
    { "AFAILURE",       T6H_A_AFAIL       }, // rename AFAILURE to AFAIL
    { "AGFAIL",         -1                }, // rename AGFAIL to XAGFAIL
    { "AHEAR",          T6H_A_AHEAR       },
    { "AKILL",          -1                }, // rename AKILL to XAKILL
    { "ALEAVE",         T6H_A_ALEAVE      },
    { "ALFAIL",         T6H_A_ALFAIL      },
    { "ALIAS",          T6H_A_ALIAS       },
    { "ALLOWANCE",      -1                },
    { "AMAIL",          T6H_A_AMAIL       },
    { "AMHEAR",         T6H_A_AMHEAR      },
    { "AMOVE",          T6H_A_AMOVE       },
    { "APAY",           -1                }, // rename APAY to XAPAY
    { "APAYMENT",       T6H_A_APAY        }, // rename APAYMENT to APAY
    { "ARFAIL",         -1                },
    { "ASUCC",          -1                }, // rename ASUCC to XASUCC
    { "ASUCCESS",       T6H_A_ASUCC       }, // rename AUCCESS to ASUCC
    { "ATFAIL",         -1                }, // rename ATFAIL to XATFAIL
    { "ATPORT",         T6H_A_ATPORT      },
    { "ATOFAIL",        -1                }, // rename ATOFAIL to XATOFAIL
    { "AUFAIL",         T6H_A_AUFAIL      },
    { "AUSE",           T6H_A_AUSE        },
    { "AWAY",           T6H_A_AWAY        },
    { "CHARGES",        T6H_A_CHARGES     },
    { "CMDCHECK",       -1                }, // rename CMDCHECK to XCMDCHECK
    { "COMMENT",        T6H_A_COMMENT     },
    { "CONFORMAT",      T6H_A_LCON_FMT    },
    { "CONNINFO",       -1                },
    { "COST",           T6H_A_COST        },
    { "CREATED",        -1                }, // rename CREATED to XCREATED
    { "DAILY",          -1                }, // rename DAILY to XDAILY
    { "DESC",           -1                }, // rename DESC to XDESC
    { "DESCRIBE",       T6H_A_DESC        }, // rename DESCRIBE to DESC
    { "DEFAULTLOCK",    -1                }, // rename DEFAULTLOCK to XDEFAULTLOCK
    { "DESTINATION",    T6H_A_EXITVARDEST },
    { "DESTROYER",      -1                }, // rename DESTROYER to XDESTROYER
    { "DFAIL",          -1                }, // rename DFAIL to XDFAIL
    { "DROP",           T6H_A_DROP        },
    { "DROPLOCK",       -1                }, // rename DROPLOCK to XDROPLOCK
    { "EALIAS",         T6H_A_EALIAS      },
    { "EFAIL",          T6H_A_EFAIL       },
    { "ENTER",          T6H_A_ENTER       },
    { "ENTERLOCK",      -1                }, // rename ENTERLOCK to XENTERLOCK
    { "EXITFORMAT",     T6H_A_LEXITS_FMT  },
    { "EXITTO",         T6H_A_EXITVARDEST },
    { "FAIL",           -1                }, // rename FAIL to XFAIL
    { "FAILURE",        T6H_A_FAIL        }, // rename FAILURE to FAIL
    { "FILTER",         T6H_A_FILTER      },
    { "FORWARDLIST",    T6H_A_FORWARDLIST },
    { "GETFROMLOCK",    -1                }, // rename GETFROMLOCK to XGETFROMLOCK
    { "GFAIL",          -1                }, // rename GFAIL to XGFAIL
    { "GIVELOCK",       -1                }, // rename GIVELOCK to XGIVELOCK
    { "HTDESC",         -1                }, // rename HTDESC to XHTDESC
    { "IDESC",          -1                }, // rename IDESC to XIDESC
    { "IDESCRIBE",      T6H_A_IDESC       }, // rename IDESCRIBE to IDESC
    { "IDLE",           T6H_A_IDLE        },
    { "IDLETIMEOUT",    -1                }, // rename IDLETIMEOUT to XIDLETIMEOUT
    { "INFILTER",       T6H_A_INFILTER    },
    { "INPREFIX",       T6H_A_INPREFIX    },
    { "KILL",           -1                }, // rename KILL to XKILL
    { "LALIAS",         T6H_A_LALIAS      },
    { "LAST",           T6H_A_LAST        },
    { "LASTPAGE",       -1                }, // rename LASTPAGE to XLASTPAGE
    { "LASTSITE",       T6H_A_LASTSITE    },
    { "LASTIP",         T6H_A_LASTIP      },
    { "LEAVE",          T6H_A_LEAVE       },
    { "LEAVELOCK",      -1                }, // rename LEAVELOCK to XLEAVELOCK
    { "LFAIL",          T6H_A_LFAIL       },
    { "LINKLOCK",       -1                }, // rename LINKLOCK to XLINKLOCK
    { "LISTEN",         T6H_A_LISTEN      },
    { "LOGINDATA",      -1                }, // rename LOGINDATA to XLOGINDATA
    { "MAILCURF",       -1                }, // rename MAILCURF to XMAILCURF
    { "MAILFLAGS",      -1                }, // rename MAILFLAGS to XMAILFLAGS
    { "MAILFOLDERS",    -1                }, // rename MAILFOLDERS to XMAILFOLDERS
    { "MAILLOCK",       -1                }, // rename MAILLOCK to XMAILLOCK
    { "MAILMSG",        -1                }, // rename MAILMSG to XMAILMSG
    { "MAILSUB",        -1                }, // rename MAILSUB to XMAILSUB
    { "MAILSUCC",       -1                }, // rename MAILSUCC to XMAILSUCC
    { "MAILTO",         -1                }, // rename MAILTO to XMAILTO
    { "MFAIL",          -1                }, // rename MFAIL to XMFAIL
    { "MODIFIED",       -1                }, // rename MODIFIED to XMODIFIED
    { "MONIKER",        -1                }, // rename MONIKER to XMONIKER
    { "MOVE",           T6H_A_MOVE        },
    { "NAME",           -1                }, // rename NAME to XNAME
    { "NAMEFORMAT",     T6H_A_NAME_FMT    },
    { "ODESC",          -1                }, // rename ODESC to XODESC
    { "ODESCRIBE",      T6H_A_ODESC       }, // rename ODESCRIBE to ODESC
    { "ODFAIL",         -1                }, // rename ODFAIL to XODFAIL
    { "ODROP",          T6H_A_ODROP       },
    { "OEFAIL",         T6H_A_OEFAIL      },
    { "OENTER",         T6H_A_OENTER      },
    { "OFAIL",          -1                }, // rename OFAIL to XOFAIL
    { "OFAILURE",       T6H_A_OFAIL       }, // rename OFAILURE to OFAIL
    { "OGFAIL",         -1                }, // rename OGFAIL to XOGFAIL
    { "OKILL",          -1                }, // rename OKILL to XOKILL
    { "OLEAVE",         T6H_A_OLEAVE      },
    { "OLFAIL",         T6H_A_OLFAIL      },
    { "OMOVE",          T6H_A_OMOVE       },
    { "OPAY",           -1                }, // rename OPAY to XOPAY
    { "OPAYMENT",       T6H_A_OPAY        }, // rename OPAYMENT to OPAY
    { "OPENLOCK",       -1                }, // rename OPENLOCK to XOPENLOCK
    { "ORFAIL",         -1                }, // rename ORFAIL to XORFAIL
    { "OSUCC",          -1                }, // rename OSUCC to XSUCC
    { "OSUCCESS",       T6H_A_OSUCC       }, // rename OSUCCESS to OSUCC
    { "OTFAIL",         -1                }, // rename OTFAIL to XOTFAIL
    { "OTPORT",         T6H_A_OTPORT      },
    { "OTOFAIL",        -1                }, // rename OTOFAIL to XOTOFAIL
    { "OUFAIL",         T6H_A_OUFAIL      },
    { "OUSE",           T6H_A_OUSE        },
    { "OXENTER",        T6H_A_OXENTER     },
    { "OXLEAVE",        T6H_A_OXLEAVE     },
    { "OXTPORT",        T6H_A_OXTPORT     },
    { "PAGELOCK",       -1                }, // rename PAGELOCK to XPAGELOCK
    { "PARENTLOCK",     -1                }, // rename PARENTLOCK to XPARENTLOCK
    { "PAY",            -1                }, // rename PAY to XPAY
    { "PAYMENT",        T6H_A_PAY         }, // rename PAYMENT to PAY
    { "PREFIX",         T6H_A_PREFIX      },
    { "PROGCMD",        -1                }, // rename PROGCMD to XPROGCMD
    { "QUEUEMAX",       -1                }, // rename QUEUEMAX to XQUEUEMAX
    { "QUOTA",          -1                }, // rename QUOTA to XQUOTA
    { "RECEIVELOCK",    -1                },
    { "REJECT",         -1                }, // rename REJECT to XREJECT
    { "REASON",         -1                }, // rename REASON to XREASON
    { "RFAIL",          -1                }, // rename RFAIL to XRFAIL
    { "RQUOTA",         T6H_A_RQUOTA      },
    { "RUNOUT",         T6H_A_RUNOUT      },
    { "SAYSTRING",      -1                }, // rename SAYSTRING to XSAYSTRING
    { "SEMAPHORE",      T6H_A_SEMAPHORE   },
    { "SEX",            T6H_A_SEX         },
    { "SIGNATURE",      -1                }, // rename SIGNATURE to XSIGNATURE
    { "MAILSIGNATURE",  T6H_A_SIGNATURE   }, // rename MAILSIGNATURE to SIGNATURE
    { "SPEECHMOD",      -1                }, // rename SPEECHMOD to XSPEECHMOD
    { "SPEECHLOCK",     -1                }, // rename SPEECHLOCK to XSPEECHLOCK
    { "STARTUP",        T6H_A_STARTUP     },
    { "SUCC",           T6H_A_SUCC        },
    { "TELOUTLOCK",     -1                }, // rename TELOUTLOCK to XTELOUTLOCK
    { "TFAIL",          -1                }, // rename TFAIL to XTFAIL
    { "TIMEOUT",        -1                }, // rename TIMEOUT to XTIMEOUT
    { "TPORT",          T6H_A_TPORT       },
    { "TPORTLOCK",      -1                }, // rename TPORTLOCK to XTPORTLOCK
    { "TOFAIL",         -1                }, // rename TOFAIL to XTOFAIL
    { "UFAIL",          T6H_A_UFAIL       },
    { "USE",            T6H_A_USE         },
    { "USELOCK",        -1                },
    { "USERLOCK",       -1                },
    { "VA",             T6H_A_VA          },
    { "VB",             T6H_A_VA+1        },
    { "VC",             T6H_A_VA+2        },
    { "VD",             T6H_A_VA+3        },
    { "VE",             T6H_A_VA+4        },
    { "VF",             T6H_A_VA+5        },
    { "VG",             T6H_A_VA+6        },
    { "VH",             T6H_A_VA+7        },
    { "VI",             T6H_A_VA+8        },
    { "VJ",             T6H_A_VA+9        },
    { "VK",             T6H_A_VA+10       },
    { "VL",             T6H_A_VA+11       },
    { "VM",             T6H_A_VA+12       },
    { "VRML_URL",       T6H_A_VRML_URL    },
    { "VN",             T6H_A_VA+13       },
    { "VO",             T6H_A_VA+14       },
    { "VP",             T6H_A_VA+15       },
    { "VQ",             T6H_A_VA+16       },
    { "VR",             T6H_A_VA+17       },
    { "VS",             T6H_A_VA+18       },
    { "VT",             T6H_A_VA+19       },
    { "VU",             T6H_A_VA+20       },
    { "VV",             T6H_A_VA+21       },
    { "VW",             T6H_A_VA+22       },
    { "VX",             T6H_A_VA+23       },
    { "VY",             T6H_A_VA+24       },
    { "VZ",             T6H_A_VA+25       },
    { "XYXXY",          T6H_A_PASS        },   // *Password
};

static struct
{
    const char *pName;
    int         iNum;
} p6h_locknames[] =
{
    { "Basic",       T6H_A_LOCK     },
    { "Enter",       T6H_A_LENTER   },
    { "Use",         T6H_A_LUSE     },
    { "Zone",        -1             },
    { "Page",        T6H_A_LPAGE    },
    { "Teleport",    T6H_A_LTPORT   },
    { "Speech",      T6H_A_LSPEECH  },
    { "Parent",      T6H_A_LPARENT  },
    { "Link",        T6H_A_LLINK    },
    { "Leave",       T6H_A_LLEAVE   },
    { "Drop",        T6H_A_LDROP    },
    { "Give",        T6H_A_LGIVE    },
    { "Receive",     T6H_A_LRECEIVE },
};

static NameMask p6h_attr_flags[] =
{
    { "no_command",     T6H_AF_NOCMD    },
    { "private",        T6H_AF_PRIVATE  },
    { "no_clone",       T6H_AF_NOCLONE  },
    { "wizard",         T6H_AF_WIZARD   },
    { "visual",         T6H_AF_VISUAL   },
    { "mortal_dark",    T6H_AF_MDARK    },
    { "hidden",         T6H_AF_DARK     },
    { "regexp",         T6H_AF_REGEXP   },
    { "case",           T6H_AF_CASE     },
    { "locked",         T6H_AF_LOCK     },
    { "internal",       T6H_AF_INTERNAL },
    { "debug",          T6H_AF_TRACE    },
    { "noname",         T6H_AF_NONAME   },
};

void T6H_GAME::ConvertFromP6H()
{
    time_t tNow;
    time(&tNow);

    SetFlags(T6H_MANDFLAGS_V1 | T6H_V_TIMESTAMPS | T6H_V_VISUALATTRS | 1);
    m_fExtraEscapes = true;

    // Build internal attribute names.
    //
    map<const char *, int, ltstr> AttrNamesKnown;
    for (int i = 0; i < sizeof(t6h_known_attrs)/sizeof(t6h_known_attrs[0]); i++)
    {
        AttrNamesKnown[StringClone(t6h_known_attrs[i].pName)] = t6h_known_attrs[i].iNum;
    }

    // Build set of attribute names.
    //
    int iNextAttr = A_USER_START;
    map<const char *, int, ltstr> AttrNames;
    for (map<int, P6H_OBJECTINFO *, lti>::iterator itObj = g_p6hgame.m_mObjects.begin(); itObj != g_p6hgame.m_mObjects.end(); ++itObj)
    {
        if (NULL != itObj->second->m_pvai)
        {
            for (vector<P6H_ATTRINFO *>::iterator itAttr = itObj->second->m_pvai->begin(); itAttr != itObj->second->m_pvai->end(); ++itAttr)
            {
                if (NULL != (*itAttr)->m_pName)
                {
                    char *pAttrName = t6h_ConvertAttributeName((*itAttr)->m_pName);
                    map<const char *, int , ltstr>::iterator itFound = AttrNamesKnown.find(pAttrName);
                    if (itFound != AttrNamesKnown.end())
                    {
                        if (-1 == itFound->second)
                        {
                            // This is a known name, but it doesn't have the
                            // same meaning. Rename it.
                            //
                            char buffer[100];
                            sprintf(buffer, "X%s", pAttrName);
                            AttrNames[StringClone(buffer)] = iNextAttr;
                            AttrNamesKnown[StringClone(pAttrName)] = iNextAttr;
                            iNextAttr++;
                        }
                    }
                    else if (AttrNames.find(pAttrName) == AttrNames.end())
                    {
                        AttrNames[StringClone(pAttrName)] = iNextAttr++;
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
        AddNumAndName(it->second, StringClone(buffer));
    }
    SetNextAttr(iNextAttr);

    int dbRefMax = 0;
    for (map<int, P6H_OBJECTINFO *, lti>::iterator it = g_p6hgame.m_mObjects.begin(); it != g_p6hgame.m_mObjects.end(); ++it)
    {
        if (  !it->second->m_fType
           || it->second->m_iType < 0
           || 16 < it->second->m_iType)
        {
            continue;
        }

        T6H_OBJECTINFO *poi = new T6H_OBJECTINFO;

        int iType = p6h_convert_type[it->second->m_iType];

        poi->SetRef(it->first);
        poi->SetName(StringClone(it->second->m_pName));
        if (it->second->m_fLocation)
        {
            int iLocation = it->second->m_dbLocation;
            if (  T6H_TYPE_EXIT == iType
               && -2 == iLocation)
            {
                poi->SetLocation(T6H_NOTHING);
            }
            else
            {
                poi->SetLocation(iLocation);
            }
        }
        if (it->second->m_fContents)
        {
            poi->SetContents(it->second->m_dbContents);
        }
        if (it->second->m_fExits)
        {
            switch (iType)
            {
            case T6H_TYPE_PLAYER:
            case T6H_TYPE_THING:
                poi->SetExits(T6H_NOTHING);
                poi->SetLink(it->second->m_dbExits);
                break;

            default:
                poi->SetExits(it->second->m_dbExits);
                poi->SetLink(T6H_NOTHING);
                break;
            }
        }
        if (it->second->m_fNext)
        {
            poi->SetNext(it->second->m_dbNext);
        }
        if (it->second->m_fParent)
        {
            poi->SetParent(it->second->m_dbParent);
        }
        if (it->second->m_fOwner)
        {
            poi->SetOwner(it->second->m_dbOwner);
        }
        if (it->second->m_fZone)
        {
            poi->SetZone(it->second->m_dbZone);
        }
        if (it->second->m_fPennies)
        {
            poi->SetPennies(it->second->m_iPennies);
        }

        // Flagwords
        //
        int flags1 = iType;
        int flags2 = 0;
        int flags3 = 0;
        char *pFlags = it->second->m_pFlags;
        if (NULL != pFlags)
        {
            // First flagword
            //
            for (int i = 0; i < sizeof(p6h_convert_obj_flags1)/sizeof(p6h_convert_obj_flags1[0]); i++)
            {
                if (NULL != strcasestr(pFlags, p6h_convert_obj_flags1[i].pName))
                {
                    flags1 |= p6h_convert_obj_flags1[i].mask;
                }
            }

            // Second flagword
            //
            for (int i = 0; i < sizeof(p6h_convert_obj_flags2)/sizeof(p6h_convert_obj_flags2[0]); i++)
            {
                if (NULL != strcasestr(pFlags, p6h_convert_obj_flags2[i].pName))
                {
                    flags2 |= p6h_convert_obj_flags2[i].mask;
                }
            }
        }

        // Powers
        //
        int powers1 = 0;
        int powers2 = 0;
        char *pPowers = it->second->m_pPowers;
        if (NULL != pPowers)
        {
            // First powerword
            //
            for (int i = 0; i < sizeof(p6h_convert_obj_powers1)/sizeof(p6h_convert_obj_powers1[0]); i++)
            {
                if (NULL != strcasestr(pPowers, p6h_convert_obj_powers1[i].pName))
                {
                    powers1 |= p6h_convert_obj_powers1[i].mask;
                }
            }

            // Second powerword
            //
            for (int i = 0; i < sizeof(p6h_convert_obj_powers2)/sizeof(p6h_convert_obj_powers2[0]); i++)
            {
                if (NULL != strcasestr(pPowers, p6h_convert_obj_powers2[i].pName))
                {
                    powers2 |= p6h_convert_obj_powers2[i].mask;
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

        if (m_flags & T6H_V_TIMESTAMPS)
        {
            if (it->second->m_fModified)
            {
                poi->SetModified(it->second->m_iModified);
                poi->SetAccessed(it->second->m_iModified);
            }
            else
            {
                poi->SetModified(tNow);
                poi->SetAccessed(tNow);
            }
        }

        if (m_flags & T6H_V_CREATETIME)
        {
            if (it->second->m_fCreated)
            {
                poi->SetCreated(it->second->m_iCreated);
            }
            else
            {
                poi->SetCreated(tNow);
            }
        }

        if (NULL != it->second->m_pvai)
        {
            vector<T6H_ATTRINFO *> *pvai = new vector<T6H_ATTRINFO *>;
            for (vector<P6H_ATTRINFO *>::iterator itAttr = it->second->m_pvai->begin(); itAttr != it->second->m_pvai->end(); ++itAttr)
            {
                if (  NULL != (*itAttr)->m_pName
                   && NULL != (*itAttr)->m_pValue)
                {
                    char *pAttrFlags = (*itAttr)->m_pFlags;
                    int iAttrFlags = 0;
                    for (int i = 0; i < sizeof(p6h_attr_flags)/sizeof(p6h_attr_flags[0]); i++)
                    {
                        if (strcasecmp(p6h_attr_flags[i].pName, pAttrFlags) == 0)
                        {
                            iAttrFlags |= p6h_attr_flags[i].mask;
                        }
                    }
                    char *pAttrName = t6h_ConvertAttributeName((*itAttr)->m_pName);
                    map<const char *, int , ltstr>::iterator itFound = AttrNamesKnown.find(pAttrName);
                    if (itFound != AttrNamesKnown.end())
                    {
                        T6H_ATTRINFO *pai = new T6H_ATTRINFO;
                        int iNum = AttrNamesKnown[pAttrName];
                        if (T6H_A_PASS == iNum)
                        {
                            char buffer[200];
                            sprintf(buffer, "$P6H$$%s", (*itAttr)->m_pValue);
                            pai->SetNumOwnerFlagsAndValue(AttrNamesKnown[pAttrName], (*itAttr)->m_dbOwner, iAttrFlags, StringClone(buffer));
                        }
                        else
                        {
                            pai->SetNumOwnerFlagsAndValue(AttrNamesKnown[pAttrName], (*itAttr)->m_dbOwner, iAttrFlags, StringClone((*itAttr)->m_pValue));
                        }
                        pvai->push_back(pai);
                    }
                    else
                    {
                        itFound = AttrNames.find(pAttrName);
                        if (itFound != AttrNames.end())
                        {
                            T6H_ATTRINFO *pai = new T6H_ATTRINFO;
                            pai->SetNumOwnerFlagsAndValue(AttrNames[pAttrName], (*itAttr)->m_dbOwner, iAttrFlags, StringClone((*itAttr)->m_pValue));
                            pvai->push_back(pai);
                        }
                    }
                    free(pAttrName);
                }
            }
            if (0 < pvai->size())
            {
                poi->SetAttrs(pvai->size(), pvai);
                pvai = NULL;
            }
            delete pvai;
        }

        if (NULL != it->second->m_pvli)
        {
            for (vector<P6H_LOCKINFO *>::iterator itLock = it->second->m_pvli->begin(); itLock != it->second->m_pvli->end(); ++itLock)
            {
                if (NULL != (*itLock)->m_pKeyTree)
                {
                    bool fFound = false;
                    int iLock;
                    for (int i = 0; i < sizeof(p6h_locknames)/sizeof(p6h_locknames[0]); i++)
                    {
                        if (strcmp(p6h_locknames[i].pName, (*itLock)->m_pType) == 0)
                        {
                            iLock = p6h_locknames[i].iNum;
                            fFound = true;
                            break;
                        }
                    }

                    if (fFound)
                    {
                        T6H_LOCKEXP *pLock = new T6H_LOCKEXP;
                        if (pLock->ConvertFromP6H((*itLock)->m_pKeyTree))
                        {
                            if (T6H_A_LOCK == iLock)
                            {
                                poi->SetDefaultLock(pLock);
                            }
                            else
                            {
                                char buffer[65536];
                                char *p = pLock->Write(buffer);
                                *p = '\0';

                                // Add it.
                                //
                                T6H_ATTRINFO *pai = new T6H_ATTRINFO;
                                pai->SetNumAndValue(iLock, StringClone(buffer));

                                if (NULL == poi->m_pvai)
                                {
                                    vector<T6H_ATTRINFO *> *pvai = new vector<T6H_ATTRINFO *>;
                                    pvai->push_back(pai);
                                    poi->SetAttrs(pvai->size(), pvai);
                                }
                                else
                                {
                                    poi->m_pvai->push_back(pai);
                                    poi->m_fAttrCount = true;
                                    poi->m_nAttrCount = poi->m_pvai->size();
                                }
                            }
                        }
                        else
                        {
                            delete pLock;
                            fprintf(stderr, "WARNING: Could not convert '%s' lock on #%d containing '%s'.\n", (*itLock)->m_pType, it->first, (*itLock)->m_pKey);
                        }
                    }
                }
            }
        }

        AddObject(poi);

        if (dbRefMax < it->first)
        {
            dbRefMax = it->first;
        }
    }

    // Release memory that we allocated.
    //
    for (map<const char *, int, ltstr>::iterator it = AttrNames.begin(); it != AttrNames.end(); ++it)
    {
        delete it->first;
    }
    for (map<const char *, int, ltstr>::iterator it = AttrNamesKnown.begin(); it != AttrNamesKnown.end(); ++it)
    {
        delete it->first;
    }

    SetSizeHint(dbRefMax+1);
    SetRecordPlayers(0);
}

char *convert_t5x_quota(char *p)
{
    static char buffer[500];
    int quota = atoi(p);
    sprintf(buffer, "%d %d %d %d %d", quota, quota, quota, quota, quota);
    return buffer;
}

bool convert_t5x_attr_num(int iNum, int *piNum)
{
    if (A_USER_START <= iNum)
    {
        *piNum = iNum;
        return true;
    }

    // T5X attributes with no corresponding T6H attribute.
    //
    if (  T5X_A_PFAIL == iNum
       || T5X_A_PRIVS == iNum
       || T5X_A_LGET == iNum
       || T5X_A_MFAIL == iNum
       || T5X_A_COMJOIN == iNum
       || T5X_A_COMLEAVE == iNum
       || T5X_A_COMON == iNum
       || T5X_A_COMOFF == iNum
       || T5X_A_CMDCHECK == iNum
       || T5X_A_MONIKER == iNum
       || T5X_A_SAYSTRING == iNum
       || T5X_A_CREATED == iNum
       || T5X_A_MODIFIED == iNum
       || T5X_A_REASON == iNum
       || T5X_A_REGINFO == iNum
       || T5X_A_CONNINFO == iNum
       || T5X_A_LMAIL == iNum
       || T5X_A_LOPEN == iNum
       || T5X_A_LASTWHISPER == iNum
       || T5X_A_ADESTROY == iNum
       || T5X_A_APARENT == iNum
       || T5X_A_ACREATE == iNum
       || T5X_A_LVISIBLE == iNum
       || T5X_A_IDLETMOUT == iNum
       || T5X_A_DESCFORMAT == iNum
       || T5X_A_VLIST == iNum
       || T5X_A_STRUCT == iNum)
    {
        return false;
    }

    if (T5X_A_CONFORMAT == iNum)
    {
        iNum = T6H_A_LCON_FMT;
    }
    else if (T5X_A_EXITFORMAT == iNum)
    {
        iNum = T6H_A_LEXITS_FMT;
    }
    else if (T5X_A_LASTIP == iNum)
    {
        iNum = T6H_A_LASTIP;
    }
    else if (T5X_A_NAMEFORMAT == iNum)
    {
        iNum = T6H_A_NAME_FMT;
    }
    else if (T5X_A_SPEECHMOD == iNum)
    {
        iNum = T6H_A_SPEECHFMT;
    }

    // T6H attribute numbers with no corresponding T5X attribute.
    //
    if (  T6H_A_MAILCC == iNum
       || T6H_A_MAILBCC == iNum
       || T6H_A_NEWOBJS == iNum
       || T6H_A_LDARK == iNum
       || T6H_A_LKNOWN == iNum
       || T6H_A_LHEARD == iNum
       || T6H_A_LMOVED == iNum
       || T6H_A_LKNOWS == iNum
       || T6H_A_LHEARS == iNum
       || T6H_A_LMOVES == iNum
       || T6H_A_PAGEGROUP == iNum
       || T6H_A_PROPDIR == iNum)
    {
        return false;
    }

    *piNum = iNum;
    return true;
}

int convert_t5x_flags1(int f)
{
    f &= T6H_SEETHRU
       | T6H_WIZARD
       | T6H_LINK_OK
       | T6H_DARK
       | T6H_JUMP_OK
       | T6H_STICKY
       | T6H_DESTROY_OK
       | T6H_HAVEN
       | T6H_QUIET
       | T6H_HALT
       | T6H_TRACE
       | T6H_GOING
       | T6H_MONITOR
       | T6H_MYOPIC
       | T6H_PUPPET
       | T6H_CHOWN_OK
       | T6H_ENTER_OK
       | T6H_VISUAL
       | T6H_IMMORTAL
       | T6H_HAS_STARTUP
       | T6H_OPAQUE
       | T6H_VERBOSE
       | T6H_INHERIT
       | T6H_NOSPOOF
       | T6H_ROBOT
       | T6H_SAFE
       | T6H_ROYALTY
       | T6H_HEARTHRU
       | T6H_TERSE;
    return f;
}

int convert_t5x_flags2(int f)
{
    int g = f;
    g &= T6H_KEY
       | T6H_ABODE
       | T6H_FLOATING
       | T6H_UNFINDABLE
       | T6H_PARENT_OK
       | T6H_LIGHT
       | T6H_HAS_LISTEN
       | T6H_HAS_FWDLIST
       | T6H_AUDITORIUM
       | T6H_ANSI
       | T6H_HEAD_FLAG
       | T6H_FIXED
       | T6H_UNINSPECTED
       | T6H_NOBLEED
       | T6H_STAFF
       | T6H_HAS_DAILY
       | T6H_GAGGED
       | T6H_VACATION
       | T6H_PLAYER_MAILS
       | T6H_HTML
       | T6H_BLIND
       | T6H_SUSPECT
       | T6H_CONNECTED
       | T6H_SLAVE;

    if ((f & T5X_NO_COMMAND) == 0)
    {
        g |= T6H_HAS_COMMANDS;
    }

    return g;
}

int convert_t5x_flags3(int f)
{
    f &= T6H_MARK_0
       | T6H_MARK_1
       | T6H_MARK_2
       | T6H_MARK_3
       | T6H_MARK_4
       | T6H_MARK_5
       | T6H_MARK_6
       | T6H_MARK_7
       | T6H_MARK_8
       | T6H_MARK_9;
    return f;
}

int convert_t5x_attr_flags(int f)
{
    int g = f;
    g &= T6H_AF_ODARK
       | T6H_AF_DARK
       | T6H_AF_WIZARD
       | T6H_AF_MDARK
       | T6H_AF_INTERNAL
       | T6H_AF_NOCMD
       | T6H_AF_LOCK
       | T6H_AF_DELETED
       | T6H_AF_NOPROG
       | T6H_AF_GOD
       | T6H_AF_IS_LOCK
       | T6H_AF_VISUAL
       | T6H_AF_PRIVATE
       | T6H_AF_HTML
       | T6H_AF_NOPARSE
       | T6H_AF_REGEXP
       | T6H_AF_NOCLONE
       | T6H_AF_CONST
       | T6H_AF_CASE
       | T6H_AF_NONAME;

    if (f & T5X_AF_TRACE)
    {
        g |= T6H_AF_TRACE;
    }
    return g;
}

int convert_t5x_power1(int f)
{
    f &= T6H_POW_CHG_QUOTAS
       | T6H_POW_CHOWN_ANY
       | T6H_POW_ANNOUNCE
       | T6H_POW_BOOT
       | T6H_POW_HALT
       | T6H_POW_CONTROL_ALL
       | T6H_POW_WIZARD_WHO
       | T6H_POW_EXAM_ALL
       | T6H_POW_FIND_UNFIND
       | T6H_POW_FREE_MONEY
       | T6H_POW_FREE_QUOTA
       | T6H_POW_HIDE
       | T6H_POW_IDLE
       | T6H_POW_SEARCH
       | T6H_POW_LONGFINGERS
       | T6H_POW_PROG
       | T6H_POW_COMM_ALL
       | T6H_POW_SEE_QUEUE
       | T6H_POW_SEE_HIDDEN
       | T6H_POW_WATCH
       | T6H_POW_POLL
       | T6H_POW_NO_DESTROY
       | T6H_POW_GUEST
       | T6H_POW_PASS_LOCKS
       | T6H_POW_STAT_ANY
       | T6H_POW_STEAL
       | T6H_POW_TEL_ANYWHR
       | T6H_POW_TEL_UNRST
       | T6H_POW_UNKILLABLE;
    return f;
}

int convert_t5x_power2(int f)
{
    f &= T6H_POW_BUILDER;
    return f;
}

void T6H_GAME::ConvertFromT5X()
{
    int ver = (g_t5xgame.m_flags & T5X_V_MASK);

    time_t tNow;
    time(&tNow);

    SetFlags(T6H_MANDFLAGS_V1 | T6H_V_TIMESTAMPS | T6H_V_VISUALATTRS | 1);
    m_fExtraEscapes = true;

    // Attribute names
    //
    for (map<int, T5X_ATTRNAMEINFO *, lti>::iterator it = g_t5xgame.m_mAttrNames.begin(); it != g_t5xgame.m_mAttrNames.end(); ++it)
    {
        AddNumAndName(it->second->m_iNum, StringClone(ConvertT5XAttrName(it->second->m_pNameEncoded)));
    }
    if (!m_fNextAttr)
    {
        SetNextAttr(g_t5xgame.m_nNextAttr);
    }

    int dbRefMax = 0;
    for (map<int, T5X_OBJECTINFO *, lti>::iterator it = g_t5xgame.m_mObjects.begin(); it != g_t5xgame.m_mObjects.end(); ++it)
    {
        if (!it->second->m_fFlags1)
        {
            continue;
        }

        // ROOM, THING, EXIT, and PLAYER types are the same between T6H and
        // T5X.  No mapping is required.
        //
        int iType = (it->second->m_iFlags1) & T6H_TYPE_MASK;
        if (  T6H_TYPE_ROOM != iType
           && T6H_TYPE_THING != iType
           && T6H_TYPE_EXIT != iType
           && T6H_TYPE_PLAYER != iType)
        {
            continue;
        }

        T6H_OBJECTINFO *poi = new T6H_OBJECTINFO;

        poi->SetRef(it->first);
        poi->SetName(StringClone(ConvertT5XValue(it->second->m_pName)));
        if (it->second->m_fLocation)
        {
            int iLocation = it->second->m_dbLocation;
            poi->SetLocation(iLocation);
        }
        if (it->second->m_fContents)
        {
            poi->SetContents(it->second->m_dbContents);
        }
        if (it->second->m_fExits)
        {
            poi->SetExits(it->second->m_dbExits);
        }
        if (it->second->m_fLink)
        {
            poi->SetLink(it->second->m_dbLink);
        }
        if (it->second->m_fNext)
        {
            poi->SetNext(it->second->m_dbNext);
        }
        if (it->second->m_fParent)
        {
            poi->SetParent(it->second->m_dbParent);
        }
        if (it->second->m_fOwner)
        {
            poi->SetOwner(it->second->m_dbOwner);
        }
        if (it->second->m_fZone)
        {
            poi->SetZone(it->second->m_dbZone);
        }
        if (it->second->m_fPennies)
        {
            poi->SetPennies(it->second->m_iPennies);
        }

        // Flagwords
        //
        int flags1 = iType;
        int flags2 = 0;
        int flags3 = 0;
        if (it->second->m_fFlags1)
        {
            flags1 |= convert_t5x_flags1(it->second->m_iFlags1);
        }
        if (it->second->m_fFlags2)
        {
            flags2 = convert_t5x_flags2(it->second->m_iFlags2);
        }
        if (it->second->m_fFlags3)
        {
            flags3 = convert_t5x_flags3(it->second->m_iFlags3);
        }

        // Powers
        //
        int powers1 = 0;
        int powers2 = 0;
        if (it->second->m_fPowers1)
        {
            powers1 = convert_t5x_power1(it->second->m_iPowers1);
        }
        if (it->second->m_fPowers2)
        {
            powers2 = convert_t5x_power2(it->second->m_iPowers2);
        }

        poi->SetFlags1(flags1);
        poi->SetFlags2(flags2);
        poi->SetFlags3(flags3);
        poi->SetPowers1(powers1);
        poi->SetPowers2(powers2);

        if (NULL != it->second->m_pvai)
        {
            vector<T6H_ATTRINFO *> *pvai = new vector<T6H_ATTRINFO *>;
            for (vector<T5X_ATTRINFO *>::iterator itAttr = it->second->m_pvai->begin(); itAttr != it->second->m_pvai->end(); ++itAttr)
            {
                int iNum;
                if ((*itAttr)->m_fNumAndValue)
                {
                    if (T5X_A_QUOTA == (*itAttr)->m_iNum)
                    {
                        // Typed quota needs to be converted to single quota.
                        //
                        T6H_ATTRINFO *pai = new T6H_ATTRINFO;
                        pai->SetNumAndValue(T6H_A_QUOTA, StringClone(convert_t5x_quota((*itAttr)->m_pValueUnencoded)));
                        pvai->push_back(pai);
                    }
                    else if (T5X_A_MODIFIED == (*itAttr)->m_iNum)
                    {
                        time_t t;
                        if (ConvertTimeString((*itAttr)->m_pValueUnencoded, &t))
                        {
                            poi->SetModified(t);
                        }
                    }
                    else if (T5X_A_LOCK == (*itAttr)->m_iNum)
                    {
                        T6H_LOCKEXP *ple = new T6H_LOCKEXP;
                        if (ple->ConvertFromT5X((*itAttr)->m_pKeyTree))
                        {
                            poi->SetDefaultLock(ple);
                        }
                        else
                        {
                            delete ple;
                        }
                    }
                    else if (convert_t5x_attr_num((*itAttr)->m_iNum, &iNum))
                    {
                        T6H_ATTRINFO *pai = new T6H_ATTRINFO;
                        pai->SetNumOwnerFlagsAndValue(iNum, (*itAttr)->m_dbOwner,
                            convert_t5x_attr_flags((*itAttr)->m_iFlags),
                            StringClone(ConvertT5XValue((*itAttr)->m_pValueUnencoded)));
                        pvai->push_back(pai);
                    }
                }
            }
            if (0 < pvai->size())
            {
                poi->SetAttrs(pvai->size(), pvai);
                pvai = NULL;
            }
            delete pvai;
        }

        if (!poi->m_fModified)
        {
            poi->SetModified(tNow);
        }
        if (!poi->m_fAccessed)
        {
            poi->SetAccessed(tNow);
        }

        if (it->second->m_ple)
        {
            T6H_LOCKEXP *ple = new T6H_LOCKEXP;
            if (ple->ConvertFromT5X(it->second->m_ple))
            {
                poi->SetDefaultLock(ple);
            }
            else
            {
                delete ple;
            }
        }

        AddObject(poi);

        if (dbRefMax < it->first)
        {
            dbRefMax = it->first;
        }
    }
    SetSizeHint(dbRefMax+1);
    if (g_t6hgame.m_fRecordPlayers)
    {
        SetRecordPlayers(g_t6hgame.m_nRecordPlayers);
    }
    else
    {
        SetRecordPlayers(0);
    }
}


void T6H_GAME::ResetPassword()
{
    for (map<int, T6H_OBJECTINFO *, lti>::iterator itObj = m_mObjects.begin(); itObj != m_mObjects.end(); ++itObj)
    {
        if (1 == itObj->first)
        {
            bool fFound = false;
            if (NULL != itObj->second->m_pvai)
            {
                for (vector<T6H_ATTRINFO *>::iterator itAttr = itObj->second->m_pvai->begin(); itAttr != itObj->second->m_pvai->end(); ++itAttr)
                {
                    if (T6H_A_PASS == (*itAttr)->m_iNum)
                    {
                        // Change it to 'potrzebie'.
                        //
                        (*itAttr)->SetNumAndValue(T6H_A_PASS, StringClone("XXNHc95o0HhAc"));

                        fFound = true;
                    }
                }
            }

            if (!fFound)
            {
                // Add it.
                //
                T6H_ATTRINFO *pai = new T6H_ATTRINFO;
                pai->SetNumAndValue(T6H_A_PASS, StringClone("XXNHc95o0HhAc"));

                if (NULL == itObj->second->m_pvai)
                {
                    vector<T6H_ATTRINFO *> *pvai = new vector<T6H_ATTRINFO *>;
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

void T6H_GAME::Upgrade32()
{
    m_fExtraEscapes = true;

    time_t tNow;
    time(&tNow);

    m_flags |= T6H_V_TIMESTAMPS|T6H_V_VISUALATTRS|T6H_V_CREATETIME;
    for (map<int, T6H_OBJECTINFO *, lti>::iterator itObj = m_mObjects.begin(); itObj != m_mObjects.end(); ++itObj)
    {
        if (!itObj->second->m_fModified)
        {
            itObj->second->SetModified(tNow);
        }
        if (!itObj->second->m_fAccessed)
        {
            itObj->second->SetAccessed(tNow);
        }
        if (!itObj->second->m_fCreated)
        {
            itObj->second->SetCreated(min(itObj->second->m_iModified, itObj->second->m_iAccessed));
        }
    }
}

void T6H_GAME::Upgrade31b()
{
    m_fExtraEscapes = true;

    time_t tNow;
    time(&tNow);

    m_flags |= T6H_V_TIMESTAMPS|T6H_V_VISUALATTRS;
    m_flags &= ~T6H_V_CREATETIME;
    for (map<int, T6H_OBJECTINFO *, lti>::iterator itObj = m_mObjects.begin(); itObj != m_mObjects.end(); ++itObj)
    {
        itObj->second->m_fCreated = false;
        if (!itObj->second->m_fModified)
        {
            itObj->second->SetModified(tNow);
        }
        if (!itObj->second->m_fAccessed)
        {
            itObj->second->SetAccessed(tNow);
        }
    }
}

void T6H_GAME::Upgrade31a()
{
    m_fExtraEscapes = false;

    time_t tNow;
    time(&tNow);

    m_flags |= T6H_V_TIMESTAMPS|T6H_V_VISUALATTRS;
    m_flags &= ~T6H_V_CREATETIME;
    for (map<int, T6H_OBJECTINFO *, lti>::iterator itObj = m_mObjects.begin(); itObj != m_mObjects.end(); ++itObj)
    {
        itObj->second->m_fCreated = false;
        if (!itObj->second->m_fModified)
        {
            itObj->second->SetModified(tNow);
        }
        if (!itObj->second->m_fAccessed)
        {
            itObj->second->SetAccessed(tNow);
        }
    }
}

void T6H_GAME::Downgrade()
{
    m_flags |= T6H_MANDFLAGS_V1;
    m_flags &= ~(T6H_V_CREATETIME|T6H_V_TIMESTAMPS|T6H_V_VISUALATTRS);
    m_fExtraEscapes = false;
    for (map<int, T6H_OBJECTINFO *, lti>::iterator itObj = m_mObjects.begin(); itObj != m_mObjects.end(); ++itObj)
    {
        itObj->second->m_fCreated = false;
        itObj->second->m_fModified = false;
        itObj->second->m_fAccessed = false;
    }
}

void T6H_GAME::Extract(FILE *fp, int dbExtract) const
{
    map<int, T6H_OBJECTINFO *, lti>::const_iterator itFound;
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
    { "\x1B[0m",      4, true,  false, "%xn",  3, true  },
    { "\x1B[1m",      4, true,  false, "%xh",  3, true  },
    { "\x1B[4m",      4, true,  false, "%xu",  3, true  },
    { "\x1B[5m",      4, true,  false, "%xf",  3, true  },
    { "\x1B[7m",      4, true,  false, "%xi",  3, true  },
    { "\x1B[30m",     5, true,  false, "%xx",  3, true  },
    { "\x1B[31m",     5, true,  false, "%xr",  3, true  },
    { "\x1B[32m",     5, true,  false, "%xg",  3, true  },
    { "\x1B[33m",     5, true,  false, "%xy",  3, true  },
    { "\x1B[34m",     5, true,  false, "%xb",  3, true  },
    { "\x1B[35m",     5, true,  false, "%xm",  3, true  },
    { "\x1B[36m",     5, true,  false, "%xc",  3, true  },
    { "\x1B[37m",     5, true,  false, "%xw",  3, true  },
    { "\x1B[40m",     5, true,  false, "%xX",  3, true  },
    { "\x1B[41m",     5, true,  false, "%xR",  3, true  },
    { "\x1B[42m",     5, true,  false, "%xG",  3, true  },
    { "\x1B[43m",     5, true,  false, "%xY",  3, true  },
    { "\x1B[44m",     5, true,  false, "%xB",  3, true  },
    { "\x1B[45m",     5, true,  false, "%xM",  3, true  },
    { "\x1B[46m",     5, true,  false, "%xC",  3, true  },
    { "\x1B[47m",     5, true,  false, "%xW",  3, true  },
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

    while (  '\0' != *p
          && q < buffer + sizeof(buffer) - 1)
    {
        int iFragment;
        size_t nSkip;
        if (ScanForFragment(p, fEval, iFragment, nSkip))
        {
            if (  !fEval
               && fragments[iFragment].fNeedEval)
            {
                fEval = true;
                p = pValue;
                q = buffer;
            }
            else
            {
                size_t ncpy = fragments[iFragment].nSubstitution;
                size_t nskp = fragments[iFragment].nFragment;
                if (q + ncpy < buffer + sizeof(buffer) - 1)
                {
                    memcpy(q, fragments[iFragment].pSubstitution, ncpy);
                    q += ncpy;
                }
                p += nskp;
            }
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

static NameMask t6h_flags1[] =
{
    { "TRANSPARENT", T6H_SEETHRU      },
    { "WIZARD",      T6H_WIZARD       },
    { "LINK_OK",     T6H_LINK_OK      },
    { "DARK",        T6H_DARK         },
    { "JUMP_OK",     T6H_JUMP_OK      },
    { "STICKY",      T6H_STICKY       },
    { "DESTROY_OK",  T6H_DESTROY_OK   },
    { "HAVE",        T6H_HAVEN        },
    { "QUIET",       T6H_QUIET        },
    { "HALT",        T6H_HALT         },
    { "TRACE",       T6H_TRACE        },
    { "MONITOR",     T6H_MONITOR      },
    { "MYOPIC",      T6H_MYOPIC       },
    { "PUPPET",      T6H_PUPPET       },
    { "CHOWN_OK",    T6H_CHOWN_OK     },
    { "ENTER_OK",    T6H_ENTER_OK     },
    { "VISUAL",      T6H_VISUAL       },
    { "IMMORTAL",    T6H_IMMORTAL     },
    { "OPAQUE",      T6H_OPAQUE       },
    { "VERBOSE",     T6H_VERBOSE      },
    { "INHERIT",     T6H_INHERIT      },
    { "NOSPOOF",     T6H_NOSPOOF      },
    { "SAFE",        T6H_SAFE         },
    { "ROYALTY",     T6H_ROYALTY      },
    { "AUDIBLE",     T6H_HEARTHRU     },
    { "TERSE",       T6H_TERSE        },
};

static NameMask t6h_flags2[] =
{
    { "KEY",         T6H_KEY          },
    { "ABODE",       T6H_ABODE        },
    { "FLOATING",    T6H_FLOATING     },
    { "UNFINDABLE",  T6H_UNFINDABLE   },
    { "PARENT_OK",   T6H_PARENT_OK    },
    { "LIGHT",       T6H_LIGHT        },
    { "AUDITORIUM",  T6H_AUDITORIUM   },
    { "ANSI",        T6H_ANSI         },
    { "HEAD",        T6H_HEAD_FLAG    },
    { "FIXED",       T6H_FIXED        },
    { "UNINSPECTED", T6H_UNINSPECTED  },
    { "COMMANDS",    T6H_HAS_COMMANDS },
    { "NOBLEED",     T6H_NOBLEED      },
    { "STAFF",       T6H_STAFF        },
    { "GAGGED",      T6H_GAGGED       },
    { "VACATION",    T6H_VACATION     },
    { "HTML",        T6H_HTML         },
    { "BLIND",       T6H_BLIND        },
    { "SUSPECT",     T6H_SUSPECT      },
    { "SLAVE",       T6H_SLAVE        },
    { "ZONE",        T6H_ZONE_PARENT  },
    { "STOP",        T6H_STOP_MATCH   },
    { "BOUNCE",      T6H_BOUNCE       },
    { "CONTROL_OK",  T6H_CONTROL_OK   },
    { "WATCHER",     T6H_WATCHER      },
    { "CONSTANT",    T6H_CONSTANT_ATTRS },
};

static NameMask t6h_flags3[] =
{
    { "REDIR_OK", T6H_ORPHAN     },
    { "ORPHAN",   T6H_ORPHAN     },
    { "FREE",     T6H_NODEFAULT  },
    { "PRESENCE", T6H_PRESENCE   },
    { "MARKER0",  T6H_MARK_0     },
    { "MARKER1",  T6H_MARK_1     },
    { "MARKER2",  T6H_MARK_2     },
    { "MARKER3",  T6H_MARK_3     },
    { "MARKER4",  T6H_MARK_4     },
    { "MARKER5",  T6H_MARK_5     },
    { "MARKER6",  T6H_MARK_6     },
    { "MARKER7",  T6H_MARK_7     },
    { "MARKER8",  T6H_MARK_8     },
    { "MARKER9",  T6H_MARK_9     },
};

static NameMask t6h_powers1[] =
{
    { "quota",            T6H_POW_CHG_QUOTAS    },
    { "chown_anything",   T6H_POW_CHOWN_ANY     },
    { "announce",         T6H_POW_ANNOUNCE      },
    { "boot",             T6H_POW_BOOT          },
    { "halt",             T6H_POW_HALT          },
    { "control_all",      T6H_POW_CONTROL_ALL   },
    { "expanded_who",     T6H_POW_WIZARD_WHO    },
    { "see_all",          T6H_POW_EXAM_ALL      },
    { "find_unfindable",  T6H_POW_FIND_UNFIND   },
    { "free_money",       T6H_POW_FREE_MONEY    },
    { "free_quota",       T6H_POW_FREE_QUOTA    },
    { "hide",             T6H_POW_HIDE          },
    { "idle",             T6H_POW_IDLE          },
    { "search",           T6H_POW_SEARCH        },
    { "long_fingers",     T6H_POW_LONGFINGERS   },
    { "prog",             T6H_POW_PROG          },
    { "comm_all",         T6H_POW_COMM_ALL      },
    { "see_queue",        T6H_POW_SEE_QUEUE     },
    { "see_hidden",       T6H_POW_SEE_HIDDEN    },
    { "poll",             T6H_POW_POLL          },
    { "no_destroy",       T6H_POW_NO_DESTROY    },
    { "guest",            T6H_POW_GUEST         },
    { "pass_locks",       T6H_POW_PASS_LOCKS    },
    { "stat_any",         T6H_POW_STAT_ANY      },
    { "steal_money",      T6H_POW_STEAL         },
    { "tel_anywhere",     T6H_POW_TEL_ANYWHR    },
    { "tel_anything",     T6H_POW_TEL_UNRST     },
    { "unkillable",       T6H_POW_UNKILLABLE    },
    { "attr_read",        T6H_POW_MDARK_ATTR    },
    { "attr_write",       T6H_POW_WIZ_ATTR      },
    { "watch_logins",     T6H_POW_WATCH         },
};

static NameMask t6h_powers2[] =
{
    { "builder",          T6H_POW_BUILDER       },
    { "link_variable",    T6H_POW_LINKVAR       },
    { "link_to_anything", T6H_POW_LINKTOANY     },
    { "open_anywhere",    T6H_POW_OPENANYLOC    },
    { "use_sql",          T6H_POW_USE_SQL       },
    { "link_any_home",    T6H_POW_LINKHOME      },
    { "cloak",            T6H_POW_CLOAK         },
};

void T6H_OBJECTINFO::Extract(FILE *fp) const
{
    fprintf(fp, "@@ Extracting #%d\n", m_dbRef);
    fprintf(fp, "@@ encoding is ASCII\n");
    if (NULL != m_pName)
    {
        bool fNeedEval;
        fprintf(fp, "@@ %s\n", EncodeSubstitutions(m_pName, fNeedEval));
    }
    char *pStrippedObjName = StringClone(StripColor(m_pName));

    // Object flags.
    //
    if (m_fFlags1)
    {
        bool fFirst = true;
        for (int i = 0; i < sizeof(t6h_flags1)/sizeof(t6h_flags1[0]); i++)
        {
            if (m_iFlags1 & t6h_flags1[i].mask)
            {
                if (fFirst)
                {
                    fFirst = false;
                    fprintf(fp, "@set %s=", pStrippedObjName);
                }
                else
                {
                    fprintf(fp, " ");
                }
                fprintf(fp, "%s", t6h_flags1[i].pName);
            }
        }
        if (!fFirst)
        {
            fprintf(fp, "\n");
        }
    }

    if (m_fFlags2)
    {
        bool fFirst = true;
        for (int i = 0; i < sizeof(t6h_flags2)/sizeof(t6h_flags2[0]); i++)
        {
            if (m_iFlags2 & t6h_flags2[i].mask)
            {
                if (fFirst)
                {
                    fFirst = false;
                    fprintf(fp, "@set %s=", pStrippedObjName);
                }
                else
                {
                    fprintf(fp, " ");
                }
                fprintf(fp, "%s", t6h_flags2[i].pName);
            }
        }
        if (!fFirst)
        {
            fprintf(fp, "\n");
        }
    }

    if (m_fFlags3)
    {
        bool fFirst = true;
        for (int i = 0; i < sizeof(t6h_flags3)/sizeof(t6h_flags3[0]); i++)
        {
            if (m_iFlags3 & t6h_flags3[i].mask)
            {
                if (fFirst)
                {
                    fFirst = false;
                    fprintf(fp, "@set %s=", pStrippedObjName);
                }
                else
                {
                    fprintf(fp, " ");
                }
                fprintf(fp, "%s", t6h_flags3[i].pName);
            }
        }
        if (!fFirst)
        {
            fprintf(fp, "\n");
        }
    }

    if (m_fPowers1)
    {
        bool fFirst = true;
        for (int i = 0; i < sizeof(t6h_powers1)/sizeof(t6h_powers1[0]); i++)
        {
            if (m_iPowers1 & t6h_powers1[i].mask)
            {
                if (fFirst)
                {
                    fFirst = false;
                    fprintf(fp, "@power %s=", pStrippedObjName);
                }
                else
                {
                    fprintf(fp, " ");
                }
                fprintf(fp, "%s", t6h_powers1[i].pName);
            }
        }
        if (!fFirst)
        {
            fprintf(fp, "\n");
        }
    }

    if (m_fPowers2)
    {
        bool fFirst = true;
        for (int i = 0; i < sizeof(t6h_powers2)/sizeof(t6h_powers2[0]); i++)
        {
            if (m_iPowers2 & t6h_powers2[i].mask)
            {
                if (fFirst)
                {
                    fFirst = false;
                    fprintf(fp, "@power %s=", pStrippedObjName);
                }
                else
                {
                    fprintf(fp, " ");
                }
                fprintf(fp, "%s", t6h_powers2[i].pName);
            }
        }
        if (!fFirst)
        {
            fprintf(fp, "\n");
        }
    }

    if (NULL != m_ple)
    {
        char buffer[65536];
        char *p = m_ple->Write(buffer);
        *p = '\0';

        fprintf(fp, "@lock/DefaultLock %s=%s\n", pStrippedObjName, p);
    }

    // Extract attribute values.
    //
    if (NULL != m_pvai)
    {
        for (vector<T6H_ATTRINFO *>::iterator it = m_pvai->begin(); it != m_pvai->end(); ++it)
        {
            (*it)->Extract(fp, pStrippedObjName);
        }
    }
    free(pStrippedObjName);
}

static struct
{
    const char *pName;
    int         iNum;
} t6h_attr_names[] =
{
    { "Aahear",        T6H_A_AAHEAR        },
    { "Aclone",        T6H_A_ACLONE        },
    { "Aconnect",      T6H_A_ACONNECT      },
    { "Adesc",         T6H_A_ADESC         },
    { "Adfail",        T6H_A_ADFAIL        },
    { "Adisconnect",   T6H_A_ADISCONNECT   },
    { "Adrop",         T6H_A_ADROP         },
    { "Aefail",        T6H_A_AEFAIL        },
    { "Aenter",        T6H_A_AENTER        },
    { "Afail",         T6H_A_AFAIL         },
    { "Agfail",        T6H_A_AGFAIL        },
    { "Ahear",         T6H_A_AHEAR         },
    { "Akill",         T6H_A_AKILL         },
    { "Aleave",        T6H_A_ALEAVE        },
    { "Alfail",        T6H_A_ALFAIL        },
    { "Alias",         T6H_A_ALIAS         },
    { "Allowance",     T6H_A_ALLOWANCE     },
    { "Amail",         T6H_A_AMAIL         },
    { "Amhear",        T6H_A_AMHEAR        },
    { "Amove",         T6H_A_AMOVE         },
    { "Apay",          T6H_A_APAY          },
    { "Arfail",        T6H_A_ARFAIL        },
    { "Asucc",         T6H_A_ASUCC         },
    { "Atfail",        T6H_A_ATFAIL        },
    { "Atport",        T6H_A_ATPORT        },
    { "Atofail",       T6H_A_ATOFAIL       },
    { "Aufail",        T6H_A_AUFAIL        },
    { "Ause",          T6H_A_AUSE          },
    { "Away",          T6H_A_AWAY          },
    { "Charges",       T6H_A_CHARGES       },
    { "ChownLock",     T6H_A_LCHOWN        },
    { "Comment",       T6H_A_COMMENT       },
    { "Conformat",     T6H_A_LCON_FMT      },
    { "ControlLock",   T6H_A_LCONTROL      },
    { "Cost",          T6H_A_COST          },
    { "Daily",         T6H_A_DAILY         },
    { "DarkLock",      T6H_A_LDARK         },
    { "Desc",          T6H_A_DESC          },
    { "DefaultLock",   T6H_A_LOCK          },
    { "Destroyer",     T6H_A_DESTROYER     },
    { "Dfail",         T6H_A_DFAIL         },
    { "Drop",          T6H_A_DROP          },
    { "DropLock",      T6H_A_LDROP         },
    { "Ealias",        T6H_A_EALIAS        },
    { "Efail",         T6H_A_EFAIL         },
    { "Enter",         T6H_A_ENTER         },
    { "EnterLock",     T6H_A_LENTER        },
    { "Exitformat",    T6H_A_LEXITS_FMT    },
    { "Exitto",        T6H_A_EXITVARDEST   },
    { "Fail",          T6H_A_FAIL          },
    { "Filter",        T6H_A_FILTER        },
    { "Forwardlist",   T6H_A_FORWARDLIST   },
    { "Gfail",         T6H_A_GFAIL         },
    { "GiveLock",      T6H_A_LGIVE         },
    { "HeardLock",     T6H_A_LHEARD        },
    { "HearsLock",     T6H_A_LHEARS        },
    { "Idesc",         T6H_A_IDESC         },
    { "Idle",          T6H_A_IDLE          },
    { "Infilter",      T6H_A_INFILTER      },
    { "Inprefix",      T6H_A_INPREFIX      },
    { "Kill",          T6H_A_KILL          },
    { "KnownLock",     T6H_A_LKNOWN        },
    { "KnowsLock",     T6H_A_LKNOWS        },
    { "Lalias",        T6H_A_LALIAS        },
    { "Last",          T6H_A_LAST          },
    { "Lastip",        T6H_A_LASTIP        },
    { "Lastpage",      T6H_A_LASTPAGE      },
    { "Lastsite",      T6H_A_LASTSITE      },
    { "Leave",         T6H_A_LEAVE         },
    { "LeaveLock",     T6H_A_LLEAVE        },
    { "Lfail",         T6H_A_LFAIL         },
    { "LinkLock",      T6H_A_LLINK         },
    { "Listen",        T6H_A_LISTEN        },
    { "Logindata",     T6H_A_LOGINDATA     },
    { "Mailcurf",      T6H_A_MAILCURF      },
    { "Mailflags",     T6H_A_MAILFLAGS     },
    { "Mailfolders",   T6H_A_MAILFOLDERS   },
    { "Mailmsg",       T6H_A_MAILMSG       },
    { "Mailsub",       T6H_A_MAILSUB       },
    { "Mailsucc",      T6H_A_MAIL          },
    { "Mailto",        T6H_A_MAILTO        },
    { "MovedLock",     T6H_A_LMOVED        },
    { "MovesLock",     T6H_A_LMOVES        },
    { "Move",          T6H_A_MOVE          },
    { "Name",          T6H_A_NAME          },
    { "Nameformat",    T6H_A_NAME_FMT      },
    { "Newobjs",       T6H_A_NEWOBJS       },
    { "Odesc",         T6H_A_ODESC         },
    { "Odfail",        T6H_A_ODFAIL        },
    { "Odrop",         T6H_A_ODROP         },
    { "Oefail",        T6H_A_OEFAIL        },
    { "Oenter",        T6H_A_OENTER        },
    { "Ofail",         T6H_A_OFAIL         },
    { "Ogfail",        T6H_A_OGFAIL        },
    { "Okill",         T6H_A_OKILL         },
    { "Oleave",        T6H_A_OLEAVE        },
    { "Olfail",        T6H_A_OLFAIL        },
    { "Omove",         T6H_A_OMOVE         },
    { "Opay",          T6H_A_OPAY          },
    { "Orfail",        T6H_A_ORFAIL        },
    { "Osucc",         T6H_A_OSUCC         },
    { "Otfail",        T6H_A_OTFAIL        },
    { "Otport",        T6H_A_OTPORT        },
    { "Otofail",       T6H_A_OTOFAIL       },
    { "Oufail",        T6H_A_OUFAIL        },
    { "Ouse",          T6H_A_OUSE          },
    { "Oxenter",       T6H_A_OXENTER       },
    { "Oxleave",       T6H_A_OXLEAVE       },
    { "Oxtport",       T6H_A_OXTPORT       },
    { "Pagegroup",     T6H_A_PAGEGROUP     },
    { "PageLock",      T6H_A_LPAGE         },
    { "ParentLock",    T6H_A_LPARENT       },
    { "Pay",           T6H_A_PAY           },
    { "Prefix",        T6H_A_PREFIX        },
    { "Progcmd",       T6H_A_PROGCMD       },
    { "Propdir",       T6H_A_PROPDIR       },
    { "Queuemax",      T6H_A_QUEUEMAX      },
    { "Quota",         T6H_A_QUOTA         },
    { "ReceiveLock",   T6H_A_LRECEIVE      },
    { "Reject",        T6H_A_REJECT        },
    { "Rfail",         T6H_A_RFAIL         },
    { "Rquota",        T6H_A_RQUOTA        },
    { "Runout",        T6H_A_RUNOUT        },
    { "Semaphore",     T6H_A_SEMAPHORE     },
    { "Sex",           T6H_A_SEX           },
    { "Signature",     T6H_A_SIGNATURE     },
    { "Speechformat",  T6H_A_SPEECHFMT     },
    { "SpeechLock",    T6H_A_LSPEECH       },
    { "Startup",       T6H_A_STARTUP       },
    { "Succ",          T6H_A_SUCC          },
    { "TeloutLock",    T6H_A_LTELOUT       },
    { "Tfail",         T6H_A_TFAIL         },
    { "Timeout",       T6H_A_TIMEOUT       },
    { "Tport",         T6H_A_TPORT         },
    { "TportLock",     T6H_A_LTPORT        },
    { "Tofail",        T6H_A_TOFAIL        },
    { "Ufail",         T6H_A_UFAIL         },
    { "Use",           T6H_A_USE           },
    { "UseLock",       T6H_A_LUSE          },
    { "UserLock",      T6H_A_LUSER         },
    { "Va",            T6H_A_VA            },
    { "Vb",            T6H_A_VA+1          },
    { "Vc",            T6H_A_VA+2          },
    { "Vd",            T6H_A_VA+3          },
    { "Ve",            T6H_A_VA+4          },
    { "Vf",            T6H_A_VA+5          },
    { "Vg",            T6H_A_VA+6          },
    { "Vh",            T6H_A_VA+7          },
    { "Vi",            T6H_A_VA+8          },
    { "Vj",            T6H_A_VA+9          },
    { "Vk",            T6H_A_VA+10         },
    { "Vl",            T6H_A_VA+11         },
    { "Vm",            T6H_A_VA+12         },
    { "Vn",            T6H_A_VA+13         },
    { "Vo",            T6H_A_VA+14         },
    { "Vp",            T6H_A_VA+15         },
    { "Vq",            T6H_A_VA+16         },
    { "Vr",            T6H_A_VA+17         },
    { "Vs",            T6H_A_VA+18         },
    { "Vt",            T6H_A_VA+19         },
    { "Vu",            T6H_A_VA+20         },
    { "Vv",            T6H_A_VA+21         },
    { "Vw",            T6H_A_VA+22         },
    { "Vx",            T6H_A_VA+23         },
    { "Vy",            T6H_A_VA+24         },
    { "Vz",            T6H_A_VA+25         },
    { "Vrml_url",      T6H_A_VRML_URL      },
    { "Htdesc",        T6H_A_HTDESC        },
    { "*Atrlist",      T6H_A_LIST          },
    { "*Password",     T6H_A_PASS          },
    { "*Money",        T6H_A_MONEY         },
    { "*Invalid",      T6H_A_TEMP          },
};

// T6H_AF_DIRTY is not exposed.
// T6H_AF_LOCK is handled separately.
//
static NameMask t6h_attr_flags[] =
{
    { "case",        T6H_AF_CASE        },
    { "dark",        T6H_AF_DARK        },
    { "private",     T6H_AF_ODARK       },
    { "hidden",      T6H_AF_MDARK       },
    { "god",         T6H_AF_GOD         },
    { "html",        T6H_AF_HTML        },
    { "no_clone",    T6H_AF_NOCLONE     },
    { "no_command",  T6H_AF_NOPROG      },
    { "no_inherit",  T6H_AF_PRIVATE     },
    { "no_name",     T6H_AF_NONAME      },
    { "no_parse",    T6H_AF_NOPARSE     },
    { "regexp",      T6H_AF_REGEXP      },
    { "trace",       T6H_AF_TRACE       },
    { "visual",      T6H_AF_VISUAL      },
    { "wizard",      T6H_AF_WIZARD      },
    { "default",     T6H_AF_DEFAULT     },
    { "rmatch",      T6H_AF_RMATCH      },
    { "now",         T6H_AF_NOW         },
};

static NameMask t6h_attr_flags_comment[] =
{
    { "const",       T6H_AF_CONST       },
    { "deleted",     T6H_AF_DELETED     },
    { "ignore",      T6H_AF_NOCMD       },
    { "internal",    T6H_AF_INTERNAL    },
    { "is_lock",     T6H_AF_IS_LOCK     },
    { "structure",   T6H_AF_STRUCTURE   },
};

void T6H_ATTRINFO::Extract(FILE *fp, char *pObjName) const
{
    if (m_fNumAndValue)
    {
        if (m_iNum < A_USER_START)
        {
            if (m_fIsLock)
            {
                for (int i = 0; i < sizeof(t6h_locks)/sizeof(t6h_locks[0]); i++)
                {
                    if (t6h_locks[i].iNum == m_iNum)
                    {
                        bool fNeedEval;
                        const char *p = EncodeSubstitutions(m_pValueUnencoded, fNeedEval);
                        if (fNeedEval)
                        {
                            fprintf(fp, "@wait 0={@lock/%s %s=%s}\n", t6h_locks[i].pName, pObjName, p);
                        }
                        else
                        {
                            fprintf(fp, "@lock/%s %s=%s\n", t6h_locks[i].pName, pObjName, p);
                        }
                        break;
                    }
                }
            }
            else
            {
                for (int i = 0; i < sizeof(t6h_attr_names)/sizeof(t6h_attr_names[0]); i++)
                {
                    if (t6h_attr_names[i].iNum == m_iNum)
                    {
                        bool fFirst = true;
                        for (int j = 0; j < sizeof(t6h_attr_flags_comment)/sizeof(t6h_attr_flags_comment[0]); j++)
                        {
                            if (m_iFlags & t6h_attr_flags_comment[j].mask)
                            {
                                if (fFirst)
                                {
                                    fFirst = false;
                                    fprintf(fp, "@@ attribute is ");
                                }
                                else
                                {
                                    fprintf(fp, " ");
                                }
                                fprintf(fp, "%s", t6h_attr_flags_comment[j].pName);
                            }
                        }
                        if (!fFirst)
                        {
                            fprintf(fp, "\n");
                        }
                        bool fNeedEval;
                        const char *p = EncodeSubstitutions(m_pValueUnencoded, fNeedEval);
                        if (fNeedEval)
                        {
                            fprintf(fp, "@wait 0={@%s %s=%s}\n", t6h_attr_names[i].pName, pObjName, p);
                        }
                        else
                        {
                            fprintf(fp, "@%s %s=%s\n", t6h_attr_names[i].pName, pObjName, p);
                        }
                        fFirst = true;
                        for (int j = 0; j < sizeof(t6h_attr_flags)/sizeof(t6h_attr_flags[0]); j++)
                        {
                            if (m_iFlags & t6h_attr_flags[j].mask)
                            {
                                if (fFirst)
                                {
                                    fFirst = false;
                                    if (fNeedEval)
                                    {
                                        fprintf(fp, "@wait 0={@set %s/%s=", pObjName, t6h_attr_names[i].pName);
                                    }
                                    else
                                    {
                                        fprintf(fp, "@set %s/%s=", pObjName, t6h_attr_names[i].pName);
                                    }
                                }
                                else
                                {
                                    fprintf(fp, " ");
                                }
                                fprintf(fp, "%s", t6h_attr_flags[j].pName);
                            }
                        }
                        if (!fFirst)
                        {
                            if (fNeedEval)
                            {
                                fprintf(fp, "}\n");
                            }
                            else
                            {
                                fprintf(fp, "\n");
                            }
                        }
                        if (T6H_AF_LOCK & m_iFlags)
                        {
                            if (fNeedEval)
                            {
                                fprintf(fp, "@wait 0={@lock %s/%s}\n", pObjName, t6h_attr_names[i].pName);
                            }
                            else
                            {
                                fprintf(fp, "@lock %s/%s\n", pObjName, t6h_attr_names[i].pName);
                            }
                        }
                        break;
                    }
                }
            }
        }
        else
        {
            for (map<int, T6H_ATTRNAMEINFO *, lti>::iterator itName =  g_t6hgame.m_mAttrNames.begin(); itName != g_t6hgame.m_mAttrNames.end(); ++itName)
            {
                if (  itName->second->m_fNumAndName
                   && itName->second->m_iNum == m_iNum)
                {
                    bool fFirst = true;
                    for (int i = 0; i < sizeof(t6h_attr_flags_comment)/sizeof(t6h_attr_flags_comment[0]); i++)
                    {
                        if (m_iFlags & t6h_attr_flags_comment[i].mask)
                        {
                            if (fFirst)
                            {
                                fFirst = false;
                                fprintf(fp, "@@ attribute is ");
                            }
                            else
                            {
                                fprintf(fp, " ");
                            }
                            fprintf(fp, "%s", t6h_attr_flags_comment[i].pName);
                        }
                    }
                    if (!fFirst)
                    {
                        fprintf(fp, "\n");
                    }
                    bool fNeedEval;
                    const char *p = EncodeSubstitutions(m_pValueUnencoded, fNeedEval);
                    if (fNeedEval)
                    {
                        fprintf(fp, "@wait 0={&%s %s=%s}\n", itName->second->m_pNameUnencoded, pObjName, p);
                    }
                    else
                    {
                        fprintf(fp, "&%s %s=%s\n", itName->second->m_pNameUnencoded, pObjName, p);
                    }
                    fFirst = true;
                    for (int i = 0; i < sizeof(t6h_attr_flags)/sizeof(t6h_attr_flags[0]); i++)
                    {
                        if (m_iFlags & t6h_attr_flags[i].mask)
                        {
                            if (fFirst)
                            {
                                fFirst = false;
                                if (fNeedEval)
                                {
                                    fprintf(fp, "@wait 0={@set %s/%s=", pObjName, itName->second->m_pNameUnencoded);
                                }
                                else
                                {
                                    fprintf(fp, "@set %s/%s=", pObjName, itName->second->m_pNameUnencoded);
                                }
                            }
                            else
                            {
                                fprintf(fp, " ");
                            }
                            fprintf(fp, "%s", t6h_attr_flags[i].pName);
                        }
                    }
                    if (!fFirst)
                    {
                        if (fNeedEval)
                        {
                            fprintf(fp, "}\n");
                        }
                        else
                        {
                            fprintf(fp, "\n");
                        }
                    }
                    if (T6H_AF_LOCK & m_iFlags)
                    {
                        if (fNeedEval)
                        {
                            fprintf(fp, "@wait 0={@lock %s/%s}\n", pObjName, itName->second->m_pNameUnencoded);
                        }
                        else
                        {
                            fprintf(fp, "@lock %s/%s\n", pObjName, itName->second->m_pNameUnencoded);
                        }
                    }
                    break;
                }
            }
        }
    }
}
