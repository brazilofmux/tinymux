#include "omega.h"
#include "t5xgame.h"
#include "p6hgame.h"
#include "t6hgame.h"
#include "r7hgame.h"

typedef long long          INT64;
typedef unsigned long long UINT64;
#ifndef INT64_C
#define INT64_C(c)       (c ## ll)
#endif // INT64_C
#ifndef UINT64_C
#define UINT64_C(c)      (c ## ull)
#endif

typedef UINT64 ColorState;

typedef struct _t5x_gameflaginfo
{
    int         mask;
    const char *pName;
} t5x_gameflaginfo;

t5x_gameflaginfo t5x_gameflagnames[] =
{
    { T5X_V_ZONE,     "V_ZONE"     },
    { T5X_V_LINK,     "V_LINK"     },
    { T5X_V_DATABASE, "V_DATABASE" },
    { T5X_V_ATRNAME,  "V_ATRNAME"  },
    { T5X_V_ATRKEY,   "V_ATRKEY"   },
    { T5X_V_PARENT,   "V_PARENT"   },
    { T5X_V_ATRMONEY, "V_ATRMONEY" },
    { T5X_V_XFLAGS,   "V_XFLAGS"   },
    { T5X_V_POWERS,   "V_POWERS"   },
    { T5X_V_3FLAGS,   "V_3FLAGS"   },
    { T5X_V_QUOTED,   "V_QUOTED"   },
};
#define T5X_NUM_GAMEFLAGNAMES (sizeof(t5x_gameflagnames)/sizeof(t5x_gameflagnames[0]))

T5X_GAME g_t5xgame;
T5X_LOCKEXP *t5xl_ParseKey(char *pKey);

// The first character of an attribute name must be either alphabetic,
// '_', '#', '.', or '~'. It's handled by the following table.
//
// Characters thereafter may be letters, numbers, and characters from
// the set {'?!`/-_.@#$^&~=+<>()}. Lower-case letters are turned into
// uppercase before being used, but lower-case letters are valid input.
//
bool t5x_AttrNameInitialSet[256] =
{
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
//
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 1
    0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0,  // 2
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 3
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 4
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1,  // 5
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 6
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 0,  // 7

    0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 0,  // 8
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 1,  // 9
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,  // A
    0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,  // B
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // C
    1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1,  // D
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // E
    1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1   // F
};

bool t5x_AttrNameSet[256] =
{
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
//
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 1
    0, 1, 0, 1, 1, 0, 1, 1, 1, 1, 0, 1, 0, 1, 1, 1,  // 2
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1,  // 3
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 4
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1,  // 5
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 6
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 0,  // 7

    0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 0,  // 8
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 1,  // 9
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,  // A
    0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,  // B
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // C
    1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1,  // D
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // E
    1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1   // F
};

char *t5x_ConvertAttributeName(const char *pName)
{
    char aBuffer[256];
    char *pBuffer = aBuffer;
    if (  '\0' != *pName
       && pBuffer < aBuffer + sizeof(aBuffer) - 1)
    {
        if (t5x_AttrNameInitialSet[(unsigned char) *pName])
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
        if (t5x_AttrNameSet[(unsigned char) *pName])
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

void T5X_LOCKEXP::Write(FILE *fp)
{
    switch (m_op)
    {
    case T5X_LOCKEXP::le_is:
        fprintf(fp, "(=");
        m_le[0]->Write(fp);
        fprintf(fp, ")");
        break;

    case T5X_LOCKEXP::le_carry:
        fprintf(fp, "(+");
        m_le[0]->Write(fp);
        fprintf(fp, ")");
        break;

    case T5X_LOCKEXP::le_indirect:
        fprintf(fp, "(@");
        m_le[0]->Write(fp);
        fprintf(fp, ")");
        break;

    case T5X_LOCKEXP::le_owner:
        fprintf(fp, "($");
        m_le[0]->Write(fp);
        fprintf(fp, ")");
        break;

    case T5X_LOCKEXP::le_and:
        fprintf(fp, "(");
        m_le[0]->Write(fp);
        fprintf(fp, "&");
        m_le[1]->Write(fp);
        fprintf(fp, ")");
        break;

    case T5X_LOCKEXP::le_or:
        fprintf(fp, "(");
        m_le[0]->Write(fp);
        fprintf(fp, "|");
        m_le[1]->Write(fp);
        fprintf(fp, ")");
        break;

    case T5X_LOCKEXP::le_not:
        fprintf(fp, "(!");
        m_le[0]->Write(fp);
        fprintf(fp, ")");
        break;

    case T5X_LOCKEXP::le_attr:
        {
            bool fText = (m_le[0]->m_op == T5X_LOCKEXP::le_text);
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

            // The code in 2.6 and earlier does not always emit a NL.  It's really
            // a beneign typo, but we reproduce it to make regression testing
            // easier.
            //
            if (!fText)
            {
                fprintf(fp, "\n");
            }

            if (fWildcard) fprintf(fp, ")");
        }
        break;

    case T5X_LOCKEXP::le_eval:
        m_le[0]->Write(fp);
        fprintf(fp, "/");
        m_le[1]->Write(fp);
        fprintf(fp, "\n");
        break;

    case T5X_LOCKEXP::le_ref:
        fprintf(fp, "%d\n", m_dbRef);
        break;

    case T5X_LOCKEXP::le_text:
        fprintf(fp, "%s", m_p[0]);
        break;
    }
}

char *T5X_LOCKEXP::Write(char *p)
{
    switch (m_op)
    {
    case T5X_LOCKEXP::le_is:
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

    case T5X_LOCKEXP::le_carry:
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

    case T5X_LOCKEXP::le_indirect:
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

    case T5X_LOCKEXP::le_owner:
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

    case T5X_LOCKEXP::le_or:
        p = m_le[0]->Write(p);
        *p++ = '|';
        p = m_le[1]->Write(p);
        break;

    case T5X_LOCKEXP::le_not:
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

    case T5X_LOCKEXP::le_attr:
        p = m_le[0]->Write(p);
        *p++ = ':';
        p = m_le[1]->Write(p);
        break;

    case T5X_LOCKEXP::le_eval:
        p = m_le[0]->Write(p);
        *p++ = '/';
        p = m_le[1]->Write(p);
        break;

    case T5X_LOCKEXP::le_and:
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

    case T5X_LOCKEXP::le_ref:
        sprintf(p, "(#%d)", m_dbRef);
        p += strlen(p);
        break;

    case T5X_LOCKEXP::le_text:
        sprintf(p, "%s", m_p[0]);
        p += strlen(p);
        break;

    default:
        fprintf(stderr, "%d not recognized.\n", m_op);
        break;
    }
    return p;
}

bool T5X_LOCKEXP::ConvertFromP6H(P6H_LOCKEXP *p)
{
    switch (p->m_op)
    {
    case P6H_LOCKEXP::le_is:
        m_op = T5X_LOCKEXP::le_is;
        m_le[0] = new T5X_LOCKEXP;
        if (!m_le[0]->ConvertFromP6H(p->m_le[0]))
        {
            delete m_le[0];
            m_le[0] = NULL;
            return false;
        }
        break;

    case P6H_LOCKEXP::le_carry:
        m_op = T5X_LOCKEXP::le_carry;
        m_le[0] = new T5X_LOCKEXP;
        if (!m_le[0]->ConvertFromP6H(p->m_le[0]))
        {
            delete m_le[0];
            m_le[0] = NULL;
            return false;
        }
        break;

    case P6H_LOCKEXP::le_indirect:
        m_op = T5X_LOCKEXP::le_indirect;
        m_le[0] = new T5X_LOCKEXP;
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
        m_op = T5X_LOCKEXP::le_owner;
        m_le[0] = new T5X_LOCKEXP;
        if (!m_le[0]->ConvertFromP6H(p->m_le[0]))
        {
            delete m_le[0];
            m_le[0] = NULL;
            return false;
        }
        break;

    case P6H_LOCKEXP::le_or:
        m_op = T5X_LOCKEXP::le_or;
        m_le[0] = new T5X_LOCKEXP;
        m_le[1] = new T5X_LOCKEXP;
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
        m_op = T5X_LOCKEXP::le_not;
        m_le[0] = new T5X_LOCKEXP;
        if (!m_le[0]->ConvertFromP6H(p->m_le[0]))
        {
            delete m_le[0];
            m_le[0] = NULL;
            return false;
        }
        break;

    case P6H_LOCKEXP::le_attr:
        m_op = T5X_LOCKEXP::le_attr;
        m_le[0] = new T5X_LOCKEXP;
        m_le[1] = new T5X_LOCKEXP;
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
        m_op = T5X_LOCKEXP::le_eval;
        m_le[0] = new T5X_LOCKEXP;
        m_le[1] = new T5X_LOCKEXP;
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
        m_op = T5X_LOCKEXP::le_and;
        m_le[0] = new T5X_LOCKEXP;
        m_le[1] = new T5X_LOCKEXP;
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
        m_op = T5X_LOCKEXP::le_ref;
        m_dbRef = p->m_dbRef;
        break;

    case P6H_LOCKEXP::le_text:
        m_op = T5X_LOCKEXP::le_text;
        m_p[0] = StringClone(p->m_p[0]);
        break;

    case P6H_LOCKEXP::le_class:
        return false;
        break;

    case P6H_LOCKEXP::le_true:
        m_op = T5X_LOCKEXP::le_text;
        m_p[0] = StringClone("1");
        break;

    case P6H_LOCKEXP::le_false:
        m_op = T5X_LOCKEXP::le_text;
        m_p[0] = StringClone("0");
        break;

    default:
        fprintf(stderr, "%d not recognized.\n", m_op);
        break;
    }
    return true;
}

bool T5X_LOCKEXP::ConvertFromT6H(T6H_LOCKEXP *p)
{
    switch (p->m_op)
    {
    case T6H_LOCKEXP::le_is:
        m_op = T5X_LOCKEXP::le_is;
        m_le[0] = new T5X_LOCKEXP;
        if (!m_le[0]->ConvertFromT6H(p->m_le[0]))
        {
            delete m_le[0];
            m_le[0] = NULL;
            return false;
        }
        break;

    case T6H_LOCKEXP::le_carry:
        m_op = T5X_LOCKEXP::le_carry;
        m_le[0] = new T5X_LOCKEXP;
        if (!m_le[0]->ConvertFromT6H(p->m_le[0]))
        {
            delete m_le[0];
            m_le[0] = NULL;
            return false;
        }
        break;

    case T6H_LOCKEXP::le_indirect:
        m_op = T5X_LOCKEXP::le_indirect;
        m_le[0] = new T5X_LOCKEXP;
        if (!m_le[0]->ConvertFromT6H(p->m_le[0]))
        {
            delete m_le[0];
            m_le[0] = NULL;
            return false;
        }
        break;

    case T6H_LOCKEXP::le_owner:
        m_op = T5X_LOCKEXP::le_owner;
        m_le[0] = new T5X_LOCKEXP;
        if (!m_le[0]->ConvertFromT6H(p->m_le[0]))
        {
            delete m_le[0];
            m_le[0] = NULL;
            return false;
        }
        break;

    case T6H_LOCKEXP::le_or:
        m_op = T5X_LOCKEXP::le_or;
        m_le[0] = new T5X_LOCKEXP;
        m_le[1] = new T5X_LOCKEXP;
        if (  !m_le[0]->ConvertFromT6H(p->m_le[0])
           || !m_le[1]->ConvertFromT6H(p->m_le[1]))
        {
            delete m_le[0];
            delete m_le[1];
            m_le[0] = m_le[1] = NULL;
            return false;
        }
        break;

    case T6H_LOCKEXP::le_not:
        m_op = T5X_LOCKEXP::le_not;
        m_le[0] = new T5X_LOCKEXP;
        if (!m_le[0]->ConvertFromT6H(p->m_le[0]))
        {
            delete m_le[0];
            m_le[0] = NULL;
            return false;
        }
        break;

    case T6H_LOCKEXP::le_attr:
        m_op = T5X_LOCKEXP::le_attr;
        m_le[0] = new T5X_LOCKEXP;
        m_le[1] = new T5X_LOCKEXP;
        if (  !m_le[0]->ConvertFromT6H(p->m_le[0])
           || !m_le[1]->ConvertFromT6H(p->m_le[1]))
        {
            delete m_le[0];
            delete m_le[1];
            m_le[0] = m_le[1] = NULL;
            return false;
        }
        break;

    case T6H_LOCKEXP::le_eval:
        m_op = T5X_LOCKEXP::le_eval;
        m_le[0] = new T5X_LOCKEXP;
        m_le[1] = new T5X_LOCKEXP;
        if (  !m_le[0]->ConvertFromT6H(p->m_le[0])
           || !m_le[1]->ConvertFromT6H(p->m_le[1]))
        {
            delete m_le[0];
            delete m_le[1];
            m_le[0] = m_le[1] = NULL;
            return false;
        }
        break;

    case T6H_LOCKEXP::le_and:
        m_op = T5X_LOCKEXP::le_and;
        m_le[0] = new T5X_LOCKEXP;
        m_le[1] = new T5X_LOCKEXP;
        if (  !m_le[0]->ConvertFromT6H(p->m_le[0])
           || !m_le[1]->ConvertFromT6H(p->m_le[1]))
        {
            delete m_le[0];
            delete m_le[1];
            m_le[0] = m_le[1] = NULL;
            return false;
        }
        break;

    case T6H_LOCKEXP::le_ref:
        m_op = T5X_LOCKEXP::le_ref;
        m_dbRef = p->m_dbRef;
        break;

    case T6H_LOCKEXP::le_text:
        m_op = T5X_LOCKEXP::le_text;
        m_p[0] = StringClone(p->m_p[0]);
        break;

    default:
        fprintf(stderr, "%d not recognized.\n", m_op);
        break;
    }
    return true;
}

bool T5X_LOCKEXP::ConvertFromR7H(R7H_LOCKEXP *p)
{
    switch (p->m_op)
    {
    case R7H_LOCKEXP::le_is:
        m_op = T5X_LOCKEXP::le_is;
        m_le[0] = new T5X_LOCKEXP;
        if (!m_le[0]->ConvertFromR7H(p->m_le[0]))
        {
            delete m_le[0];
            m_le[0] = NULL;
            return false;
        }
        break;

    case R7H_LOCKEXP::le_carry:
        m_op = T5X_LOCKEXP::le_carry;
        m_le[0] = new T5X_LOCKEXP;
        if (!m_le[0]->ConvertFromR7H(p->m_le[0]))
        {
            delete m_le[0];
            m_le[0] = NULL;
            return false;
        }
        break;

    case R7H_LOCKEXP::le_indirect:
        m_op = T5X_LOCKEXP::le_indirect;
        m_le[0] = new T5X_LOCKEXP;
        if (!m_le[0]->ConvertFromR7H(p->m_le[0]))
        {
            delete m_le[0];
            m_le[0] = NULL;
            return false;
        }
        break;

    case R7H_LOCKEXP::le_owner:
        m_op = T5X_LOCKEXP::le_owner;
        m_le[0] = new T5X_LOCKEXP;
        if (!m_le[0]->ConvertFromR7H(p->m_le[0]))
        {
            delete m_le[0];
            m_le[0] = NULL;
            return false;
        }
        break;

    case R7H_LOCKEXP::le_or:
        m_op = T5X_LOCKEXP::le_or;
        m_le[0] = new T5X_LOCKEXP;
        m_le[1] = new T5X_LOCKEXP;
        if (  !m_le[0]->ConvertFromR7H(p->m_le[0])
           || !m_le[1]->ConvertFromR7H(p->m_le[1]))
        {
            delete m_le[0];
            delete m_le[1];
            m_le[0] = m_le[1] = NULL;
            return false;
        }
        break;

    case R7H_LOCKEXP::le_not:
        m_op = T5X_LOCKEXP::le_not;
        m_le[0] = new T5X_LOCKEXP;
        if (!m_le[0]->ConvertFromR7H(p->m_le[0]))
        {
            delete m_le[0];
            m_le[0] = NULL;
            return false;
        }
        break;

    case R7H_LOCKEXP::le_attr:
        m_op = T5X_LOCKEXP::le_attr;
        m_le[0] = new T5X_LOCKEXP;
        m_le[1] = new T5X_LOCKEXP;
        if (  !m_le[0]->ConvertFromR7H(p->m_le[0])
           || !m_le[1]->ConvertFromR7H(p->m_le[1]))
        {
            delete m_le[0];
            delete m_le[1];
            m_le[0] = m_le[1] = NULL;
            return false;
        }
        break;

    case R7H_LOCKEXP::le_eval:
        m_op = T5X_LOCKEXP::le_eval;
        m_le[0] = new T5X_LOCKEXP;
        m_le[1] = new T5X_LOCKEXP;
        if (  !m_le[0]->ConvertFromR7H(p->m_le[0])
           || !m_le[1]->ConvertFromR7H(p->m_le[1]))
        {
            delete m_le[0];
            delete m_le[1];
            m_le[0] = m_le[1] = NULL;
            return false;
        }
        break;

    case R7H_LOCKEXP::le_and:
        m_op = T5X_LOCKEXP::le_and;
        m_le[0] = new T5X_LOCKEXP;
        m_le[1] = new T5X_LOCKEXP;
        if (  !m_le[0]->ConvertFromR7H(p->m_le[0])
           || !m_le[1]->ConvertFromR7H(p->m_le[1]))
        {
            delete m_le[0];
            delete m_le[1];
            m_le[0] = m_le[1] = NULL;
            return false;
        }
        break;

    case R7H_LOCKEXP::le_ref:
        m_op = T5X_LOCKEXP::le_ref;
        m_dbRef = p->m_dbRef;
        break;

    case R7H_LOCKEXP::le_text:
        m_op = T5X_LOCKEXP::le_text;
        m_p[0] = StringClone(p->m_p[0]);
        break;

    default:
        fprintf(stderr, "%d not recognized.\n", m_op);
        break;
    }
    return true;
}

void T5X_ATTRNAMEINFO::SetNumFlagsAndName(int iNum, int iFlags, char *pName)
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

void T5X_ATTRNAMEINFO::SetNumAndName(int iNum, char *pName)
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

void T5X_ATTRNAMEINFO::Write(FILE *fp, bool fExtraEscapes)
{
    if (m_fNumAndName)
    {
        fprintf(fp, "+A%d\n\"%s\"\n", m_iNum, EncodeString(m_pNameEncoded, fExtraEscapes));
    }
}

void T5X_OBJECTINFO::SetName(char *pName)
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
} t5x_locks[] =
{
    { "defaultlock", T5X_A_LOCK     },
    { "enterlock",   T5X_A_LENTER   },
    { "leavelock",   T5X_A_LLEAVE   },
    { "pagelock",    T5X_A_LPAGE    },
    { "uselock",     T5X_A_LUSE     },
    { "givelock",    T5X_A_LGIVE    },
    { "tportlock",   T5X_A_LTPORT   },
    { "droplock",    T5X_A_LDROP    },
    { "receivelock", T5X_A_LRECEIVE },
    { "linklock",    T5X_A_LLINK    },
    { "teloutlock",  T5X_A_LTELOUT  },
    { "userlock",    T5X_A_LUSER    },
    { "parentlock",  T5X_A_LPARENT  },
    { "controllock", T5X_A_LCONTROL },
    { "getfromlock", T5X_A_LGET     },
    { "speechlock",  T5X_A_LSPEECH  },
    { "maillock",    T5X_A_LMAIL    },
    { "openlock",    T5X_A_LOPEN    },
    { "visiblelock", T5X_A_LVISIBLE },
};

void T5X_OBJECTINFO::SetAttrs(int nAttrs, vector<T5X_ATTRINFO *> *pvai)
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
        for (vector<T5X_ATTRINFO *>::iterator it = m_pvai->begin(); it != m_pvai->end(); ++it)
        {
            (*it)->m_fIsLock = false;
            for (int i = 0; i < sizeof(t5x_locks)/sizeof(t5x_locks[0]); i++)
            {
                if (t5x_locks[i].iNum == (*it)->m_iNum)
                {
                    char *pValue = (NULL != (*it)->m_pValueUnencoded) ? (*it)->m_pValueUnencoded : (*it)->m_pValueEncoded;
                    (*it)->m_fIsLock = true;
                    (*it)->m_pKeyTree = t5xl_ParseKey(pValue);
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

void T5X_ATTRINFO::SetNumOwnerFlagsAndValue(int iNum, int dbAttrOwner, int iAttrFlags, char *pValue)
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

void T5X_ATTRINFO::SetNumAndValue(int iNum, char *pValue)
{
    m_fNumAndValue = true;
    free(m_pAllocated);
    m_pAllocated = pValue;

    m_iNum    = iNum;
    m_pValueUnencoded  = NULL;
    m_pValueEncoded = pValue;
    m_iFlags  = 0;
    m_dbOwner = T5X_NOTHING;

    m_kState  = kDecode;
}

void T5X_ATTRINFO::EncodeDecode(int dbObjOwner)
{
    if (kEncode == m_kState)
    {
        // If using the default owner and flags (almost all attributes will),
        // just store the string.
        //
        if (  (  m_dbOwner == dbObjOwner
              || T5X_NOTHING == m_dbOwner)
           && 0 == m_iFlags)
        {
            m_pValueEncoded = m_pValueUnencoded;
        }
        else
        {
            // Encode owner and flags into the attribute text.
            //
            if (T5X_NOTHING == m_dbOwner)
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
            m_kState = kNone;
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
            m_kState = kNone;
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

void T5X_GAME::AddNumAndName(int iNum, char *pName)
{
    T5X_ATTRNAMEINFO *pani = new T5X_ATTRNAMEINFO;
    pani->SetNumAndName(iNum, pName);

    map<int, T5X_ATTRNAMEINFO *, lti>::iterator itName = m_mAttrNames.find(iNum);
    if (itName != m_mAttrNames.end())
    {
        fprintf(stderr, "WARNING: Duplicate attribute number %s(%d) conflicts with %s(%d)\n",
            pani->m_pNameUnencoded, iNum, itName->second->m_pNameUnencoded, itName->second->m_iNum);
        delete pani;
        return;
    }
    map<char *, T5X_ATTRNAMEINFO *, ltstr>::iterator itNum = m_mAttrNums.find(pani->m_pNameUnencoded);
    if (itNum != m_mAttrNums.end())
    {
        fprintf(stderr, "WARNING: Duplicate attribute name %s(%d) conflicts with %s(%d)\n",
            pani->m_pNameUnencoded, iNum, itNum->second->m_pNameUnencoded, itNum->second->m_iNum);
        delete pani;
        return;
    }
    m_mAttrNames[iNum] = pani;
    m_mAttrNums[pani->m_pNameUnencoded] = pani;
}

void T5X_GAME::AddObject(T5X_OBJECTINFO *poi)
{
    m_mObjects[poi->m_dbRef] = poi;
}

void T5X_GAME::ValidateFlags() const
{
    int flags = m_flags;

    int ver = (m_flags & T5X_V_MASK);
    fprintf(stderr, "INFO: Flatfile version is %d\n", ver);
    if (ver < 1 || 4 < ver)
    {
        fprintf(stderr, "WARNING: Expecting version to be between 1 and 4.\n");
    }
    flags &= ~T5X_V_MASK;
    int tflags = flags;

    fprintf(stderr, "INFO: Flatfile flags are ");
    for (int i = 0; i < T5X_NUM_GAMEFLAGNAMES; i++)
    {
        if (t5x_gameflagnames[i].mask & tflags)
        {
            fprintf(stderr, "%s ", t5x_gameflagnames[i].pName);
            tflags &= ~t5x_gameflagnames[i].mask;
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
    if (  2 == ver
       && (flags & T5X_MANDFLAGS_V2) != T5X_MANDFLAGS_V2)
    {
        fprintf(stderr, "WARNING: Not all mandatory flags for v2 are present.\n");
    }
    else if (  3 == ver
            && (flags & T5X_MANDFLAGS_V3) != T5X_MANDFLAGS_V3)
    {
        fprintf(stderr, "WARNING: Not all mandatory flags for v3 are present.\n");
    }
    else if (  4 == ver
            && (flags & T5X_MANDFLAGS_V4) != T5X_MANDFLAGS_V4)
    {
        fprintf(stderr, "WARNING: Not all mandatory flags for v4 are present.\n");
    }

    // Validate that this is a flatfile and not a structure file.
    //
    if (  (flags & T5X_V_DATABASE) != 0
       || (flags & T5X_V_ATRNAME) != 0
       || (flags & T5X_V_ATRMONEY) != 0)
    {
        fprintf(stderr, "WARNING: Expected a flatfile (with strings) instead of a structure file (with only object anchors).\n");
    }
}

void T5X_GAME::Pass2()
{
    for (map<int, T5X_OBJECTINFO *, lti>::iterator itObj = m_mObjects.begin(); itObj != m_mObjects.end(); ++itObj)
    {
        if (NULL != itObj->second->m_pvai)
        {
            for (vector<T5X_ATTRINFO *>::iterator itAttr = itObj->second->m_pvai->begin(); itAttr != itObj->second->m_pvai->end(); ++itAttr)
            {
                (*itAttr)->EncodeDecode(itObj->second->m_dbOwner);
            }
        }
    }
}

void T5X_GAME::ValidateObjects() const
{
    int dbRefMax = 0;
    for (map<int, T5X_OBJECTINFO *, lti>::const_iterator it = m_mObjects.begin(); it != m_mObjects.end(); ++it)
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

void T5X_ATTRNAMEINFO::Validate(int ver) const
{
    if (m_fNumAndName)
    {
        if (m_iNum < A_USER_START)
        {
            fprintf(stderr, "WARNING: User attribute (%s) uses an attribute number (%d) which is below %d.\n", m_pNameUnencoded, m_iNum, A_USER_START);
        }
        char *q = m_pNameUnencoded;

        if (ver <= 2)
        {
            bool fValid = true;
            if (!t5x_AttrNameInitialSet[(unsigned char)*q])
            {
                fValid = false;
            }
            else if ('\0' != *q)
            {
                q++;
                while ('\0' != *q)
                {
                    if (!t5x_AttrNameSet[(unsigned char)*q])
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
    }
    else
    {
        fprintf(stderr, "WARNING: Unexpected ATTRNAMEINFO -- internal error.\n");
    }
}

void T5X_GAME::ValidateAttrNames(int ver) const
{
    if (!m_fNextAttr)
    {
        fprintf(stderr, "WARNING: +N phrase for attribute count was missing.\n");
    }
    else
    {
        int n = 256;
        for (map<int, T5X_ATTRNAMEINFO *, lti>::const_iterator it = m_mAttrNames.begin(); it != m_mAttrNames.end(); ++it)
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

void T5X_GAME::Validate() const
{
    fprintf(stderr, "TinyMUX\n");

    int ver = (m_flags & T5X_V_MASK);
    ValidateFlags();
    ValidateAttrNames(ver);
    ValidateObjects();
}

void T5X_OBJECTINFO::Write(FILE *fp, bool bWriteLock, bool fExtraEscapes)
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
    if (  m_fAttrCount
       && NULL != m_pvai)
    {
        for (vector<T5X_ATTRINFO *>::iterator it = m_pvai->begin(); it != m_pvai->end(); ++it)
        {
            (*it)->Write(fp, fExtraEscapes);
        }
    }
    fprintf(fp, "<\n");
}

void T5X_ATTRINFO::Validate() const
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

void T5X_OBJECTINFO::Validate() const
{
    int iType = -1;
    if (m_fFlags1)
    {
        iType = (m_iFlags1) & T5X_TYPE_MASK;
    }

    map<int, T5X_OBJECTINFO *, lti>::const_iterator itFound;
    if (m_fLocation)
    {
        if (m_dbLocation < 0)
        {
            if (  m_dbLocation != T5X_NOTHING
               && (  T5X_TYPE_ROOM != iType
                  || T5X_HOME      != m_dbLocation))
            {
                fprintf(stderr, "WARNING: Location (#%d) of object #%d is unexpected.\n", m_dbLocation, m_dbRef);
            }
        }
        else
        {
            itFound = g_t5xgame.m_mObjects.find(m_dbLocation);
            if (itFound == g_t5xgame.m_mObjects.end())
            {
                fprintf(stderr, "WARNING: Location (#%d) of object #%d does not exist.\n", m_dbLocation, m_dbRef);
            }
        }
    }
    if (  m_fContents
       && T5X_NOTHING != m_dbContents)
    {
        itFound = g_t5xgame.m_mObjects.find(m_dbContents);
        if (itFound == g_t5xgame.m_mObjects.end())
        {
            fprintf(stderr, "WARNING: Contents (#%d) of object #%d does not exist.\n", m_dbContents, m_dbRef);
        }
    }
    if (  m_fExits
       && T5X_NOTHING != m_dbExits)
    {
        itFound = g_t5xgame.m_mObjects.find(m_dbExits);
        if (itFound == g_t5xgame.m_mObjects.end())
        {
            fprintf(stderr, "WARNING: Exits (#%d) of object #%d does not exist.\n", m_dbExits, m_dbRef);
        }
    }
    if (  m_fNext
       && T5X_NOTHING != m_dbNext)
    {
        itFound = g_t5xgame.m_mObjects.find(m_dbNext);
        if (itFound == g_t5xgame.m_mObjects.end())
        {
            fprintf(stderr, "WARNING: Next (#%d) of object #%d does not exist.\n", m_dbNext, m_dbRef);
        }
    }
    if (  m_fParent
       && T5X_NOTHING != m_dbParent)
    {
        itFound = g_t5xgame.m_mObjects.find(m_dbParent);
        if (itFound == g_t5xgame.m_mObjects.end())
        {
            fprintf(stderr, "WARNING: Parent (#%d) of object #%d does not exist.\n", m_dbParent, m_dbRef);
        }
    }
    if (  m_fOwner
       && T5X_NOTHING != m_dbOwner)
    {
        itFound = g_t5xgame.m_mObjects.find(m_dbOwner);
        if (itFound == g_t5xgame.m_mObjects.end())
        {
            fprintf(stderr, "WARNING: Owner (#%d) of object #%d does not exist.\n", m_dbOwner, m_dbRef);
        }
    }
    if (  m_fZone
       && T5X_NOTHING != m_dbZone)
    {
        itFound = g_t5xgame.m_mObjects.find(m_dbZone);
        if (itFound == g_t5xgame.m_mObjects.end())
        {
            fprintf(stderr, "WARNING: Zone (#%d) of object #%d does not exist.\n", m_dbZone, m_dbRef);
        }
    }
    if (  m_fLink
       && T5X_NOTHING != m_dbLink)
    {
        itFound = g_t5xgame.m_mObjects.find(m_dbLink);
        if (itFound == g_t5xgame.m_mObjects.end())
        {
            fprintf(stderr, "WARNING: Link (#%d) of object #%d does not exist.\n", m_dbLink, m_dbRef);
        }
    }

    if (  m_fAttrCount
       && NULL != m_pvai)
    {
        for (vector<T5X_ATTRINFO *>::iterator it = m_pvai->begin(); it != m_pvai->end(); ++it)
        {
            (*it)->Validate();
        }
    }
}

void T5X_ATTRINFO::Write(FILE *fp, bool fExtraEscapes) const
{
    if (m_fNumAndValue)
    {
        fprintf(fp, ">%d\n\"%s\"\n", m_iNum, EncodeString(m_pValueEncoded, fExtraEscapes));
    }
}

void T5X_GAME::Write(FILE *fp)
{
    int ver = (m_flags & T5X_V_MASK);
    bool fExtraEscapes = (2 <= ver);
    fprintf(fp, "+X%d\n", m_flags);
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
    for (map<int, T5X_ATTRNAMEINFO *, lti>::iterator it = m_mAttrNames.begin(); it != m_mAttrNames.end(); ++it)
    {
        it->second->Write(fp, fExtraEscapes);
    }
    for (map<int, T5X_OBJECTINFO *, lti>::iterator it = m_mObjects.begin(); it != m_mObjects.end(); ++it)
    {
        it->second->Write(fp, (m_flags & T5X_V_ATRKEY) == 0, fExtraEscapes);
    }

    fprintf(fp, "***END OF DUMP***\n");
}

static int p6h_convert_type[] =
{
    T5X_NOTYPE,        //  0
    T5X_TYPE_ROOM,     //  1
    T5X_TYPE_THING,    //  2
    T5X_NOTYPE,        //  3
    T5X_TYPE_EXIT,     //  4
    T5X_NOTYPE,        //  5
    T5X_NOTYPE,        //  6
    T5X_NOTYPE,        //  7
    T5X_TYPE_PLAYER,   //  8
    T5X_NOTYPE,        //  9
    T5X_NOTYPE,        // 10
    T5X_NOTYPE,        // 11
    T5X_NOTYPE,        // 12
    T5X_NOTYPE,        // 13
    T5X_NOTYPE,        // 14
    T5X_NOTYPE,        // 15
    T5X_TYPE_GARBAGE,  // 16
};

static NameMask p6h_convert_obj_flags1[] =
{
    { "TRANSPARENT",    T5X_SEETHRU     },
    { "WIZARD",         T5X_WIZARD      },
    { "LINK_OK",        T5X_LINK_OK     },
    { "DARK",           T5X_DARK        },
    { "JUMP_OK",        T5X_JUMP_OK     },
    { "STICKY",         T5X_STICKY      },
    { "DESTROY_OK",     T5X_DESTROY_OK  },
    { "HAVEN",          T5X_HAVEN       },
    { "QUIET",          T5X_QUIET       },
    { "HALT",           T5X_HALT        },
    { "DEBUG",          T5X_TRACE       },
    { "GOING",          T5X_GOING       },
    { "MONITOR",        T5X_MONITOR     },
    { "MYOPIC",         T5X_MYOPIC      },
    { "PUPPET",         T5X_PUPPET      },
    { "CHOWN_OK",       T5X_CHOWN_OK    },
    { "ENTER_OK",       T5X_ENTER_OK    },
    { "VISUAL",         T5X_VISUAL      },
    { "OPAQUE",         T5X_OPAQUE      },
    { "VERBOSE",        T5X_VERBOSE     },
    { "NOSPOOF",        T5X_NOSPOOF     },
    { "SAFE",           T5X_SAFE        },
    { "ROYALTY",        T5X_ROYALTY     },
    { "AUDIBLE",        T5X_HEARTHRU    },
    { "TERSE",          T5X_TERSE       },
};

static NameMask p6h_convert_obj_flags2[] =
{
    { "ABODE",          T5X_ABODE       },
    { "FLOATING",       T5X_FLOATING    },
    { "UNFINDABLE",     T5X_UNFINDABLE  },
    { "LIGHT",          T5X_LIGHT       },
    { "ANSI",           T5X_ANSI        },
    { "COLOR",          T5X_ANSI        },
    { "FIXED",          T5X_FIXED       },
    { "UNINSPECTED",    T5X_UNINSPECTED },
    { "NO_COMMAND",     T5X_NO_COMMAND  },
    { "KEEPALIVE",      T5X_KEEPALIVE   },
    { "GAGGED",         T5X_GAGGED      },
    { "ON-VACATION",    T5X_VACATION    },
    { "SUSPECT",        T5X_SUSPECT     },
    { "NOACCENTS",      T5X_ASCII       },
    { "SLAVE",          T5X_SLAVE       },
};

static NameMask p6h_convert_obj_powers1[] =
{
    { "Announce",       T5X_POW_ANNOUNCE    },
    { "Boot",           T5X_POW_BOOT        },
    { "Guest",          T5X_POW_GUEST       },
    { "Halt",           T5X_POW_HALT        },
    { "Hide",           T5X_POW_HIDE        },
    { "Idle",           T5X_POW_IDLE        },
    { "Long_Fingers",   T5X_POW_LONGFINGERS },
    { "No_Pay",         T5X_POW_FREE_MONEY  },
    { "No_Quota",       T5X_POW_FREE_QUOTA  },
    { "Poll",           T5X_POW_POLL        },
    { "Quotas",         T5X_POW_CHG_QUOTAS  },
    { "Search",         T5X_POW_SEARCH      },
    { "See_All",        T5X_POW_EXAM_ALL    },
    { "See_Queue",      T5X_POW_SEE_QUEUE   },
    { "Tport_Anything", T5X_POW_TEL_UNRST   },
    { "Tport_Anywhere", T5X_POW_TEL_ANYWHR  },
    { "Unkillable",     T5X_POW_UNKILLABLE  },
};

static NameMask p6h_convert_obj_powers2[] =
{
    { "Builder",        T5X_POW_BUILDER     },
};

static struct
{
    const char *pName;
    int         iNum;
} t5x_known_attrs[] =
{
    { "AAHEAR",         T5X_A_AAHEAR      },
    { "ACLONE",         T5X_A_ACLONE      },
    { "ACONNECT",       T5X_A_ACONNECT    },
    { "ADESC",          -1                },  // rename ADESC to XADESC
    { "ADESCRIBE",      T5X_A_ADESC       },  // rename ADESCRIBE to ADESC
    { "ADFAIL",         -1                },  // rename ADFAIL to XADFAIL
    { "ADISCONNECT",    T5X_A_ADISCONNECT },
    { "ADROP",          T5X_A_ADROP       },
    { "AEFAIL",         T5X_A_AEFAIL      },
    { "AENTER",         T5X_A_AENTER      },
    { "AFAIL",          -1                }, // rename AFAIL to XAFAIL
    { "AFAILURE",       T5X_A_AFAIL       }, // rename AFAILURE to AFAIL
    { "AGFAIL",         -1                }, // rename AGFAIL to XAGFAIL
    { "AHEAR",          T5X_A_AHEAR       },
    { "AKILL",          -1                }, // rename AKILL to XAKILL
    { "ALEAVE",         T5X_A_ALEAVE      },
    { "ALFAIL",         T5X_A_ALFAIL      },
    { "ALIAS",          T5X_A_ALIAS       },
    { "ALLOWANCE",      -1                },
    { "AMAIL",          T5X_A_AMAIL       },
    { "AMHEAR",         T5X_A_AMHEAR      },
    { "AMOVE",          T5X_A_AMOVE       },
    { "APAY",           -1                }, // rename APAY to XAPAY
    { "APAYMENT",       T5X_A_APAY        }, // rename APAYMENT to APAY
    { "ARFAIL",         -1                },
    { "ASUCC",          -1                }, // rename ASUCC to XASUCC
    { "ASUCCESS",       T5X_A_ASUCC       }, // rename AUCCESS to ASUCC
    { "ATFAIL",         -1                }, // rename ATFAIL to XATFAIL
    { "ATPORT",         T5X_A_ATPORT      },
    { "ATOFAIL",        -1                }, // rename ATOFAIL to XATOFAIL
    { "AUFAIL",         T5X_A_AUFAIL      },
    { "AUSE",           T5X_A_AUSE        },
    { "AWAY",           T5X_A_AWAY        },
    { "CHARGES",        T5X_A_CHARGES     },
    { "CMDCHECK",       -1                }, // rename CMDCHECK to XCMDCHECK
    { "COMMENT",        T5X_A_COMMENT     },
    { "CONFORMAT",      T5X_A_CONFORMAT   },
    { "CONNINFO",       -1                },
    { "COST",           T5X_A_COST        },
    { "CREATED",        -1                }, // rename CREATED to XCREATED
    { "DAILY",          -1                }, // rename DAILY to XDAILY
    { "DESC",           -1                }, // rename DESC to XDESC
    { "DESCRIBE",       T5X_A_DESC        }, // rename DESCRIBE to DESC
    { "DEFAULTLOCK",    -1                }, // rename DEFAULTLOCK to XDEFAULTLOCK
    { "DESCFORMAT",     T5X_A_DESCFORMAT  },
    { "DESTINATION",    T5X_A_EXITVARDEST },
    { "DESTROYER",      -1                }, // rename DESTROYER to XDESTROYER
    { "DFAIL",          -1                }, // rename DFAIL to XDFAIL
    { "DROP",           T5X_A_DROP        },
    { "DROPLOCK",       -1                }, // rename DROPLOCK to XDROPLOCK
    { "EALIAS",         T5X_A_EALIAS      },
    { "EFAIL",          T5X_A_EFAIL       },
    { "ENTER",          T5X_A_ENTER       },
    { "ENTERLOCK",      -1                }, // rename ENTERLOCK to XENTERLOCK
    { "EXITFORMAT",     T5X_A_EXITFORMAT  },
    { "EXITTO",         T5X_A_EXITVARDEST },
    { "FAIL",           -1                }, // rename FAIL to XFAIL
    { "FAILURE",        T5X_A_FAIL        }, // rename FAILURE to FAIL
    { "FILTER",         T5X_A_FILTER      },
    { "FORWARDLIST",    T5X_A_FORWARDLIST },
    { "GETFROMLOCK",    -1                }, // rename GETFROMLOCK to XGETFROMLOCK
    { "GFAIL",          -1                }, // rename GFAIL to XGFAIL
    { "GIVELOCK",       -1                }, // rename GIVELOCK to XGIVELOCK
    { "HTDESC",         -1                }, // rename HTDESC to XHTDESC
    { "IDESC",          -1                }, // rename IDESC to XIDESC
    { "IDESCRIBE",      T5X_A_IDESC       }, // rename IDESCRIBE to IDESC
    { "IDLE",           T5X_A_IDLE        },
    { "IDLETIMEOUT",    -1                }, // rename IDLETIMEOUT to XIDLETIMEOUT
    { "INFILTER",       T5X_A_INFILTER    },
    { "INPREFIX",       T5X_A_INPREFIX    },
    { "KILL",           -1                }, // rename KILL to XKILL
    { "LALIAS",         T5X_A_LALIAS      },
    { "LAST",           T5X_A_LAST        },
    { "LASTPAGE",       -1                }, // rename LASTPAGE to XLASTPAGE
    { "LASTSITE",       T5X_A_LASTSITE    },
    { "LASTIP",         T5X_A_LASTIP      },
    { "LEAVE",          T5X_A_LEAVE       },
    { "LEAVELOCK",      -1                }, // rename LEAVELOCK to XLEAVELOCK
    { "LFAIL",          T5X_A_LFAIL       },
    { "LINKLOCK",       -1                }, // rename LINKLOCK to XLINKLOCK
    { "LISTEN",         T5X_A_LISTEN      },
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
    { "MOVE",           T5X_A_MOVE        },
    { "NAME",           -1                }, // rename NAME to XNAME
    { "NAMEFORMAT",     T5X_A_NAMEFORMAT  },
    { "ODESC",          -1                }, // rename ODESC to XODESC
    { "ODESCRIBE",      T5X_A_ODESC       }, // rename ODESCRIBE to ODESC
    { "ODFAIL",         -1                }, // rename ODFAIL to XODFAIL
    { "ODROP",          T5X_A_ODROP       },
    { "OEFAIL",         T5X_A_OEFAIL      },
    { "OENTER",         T5X_A_OENTER      },
    { "OFAIL",          -1                }, // rename OFAIL to XOFAIL
    { "OFAILURE",       T5X_A_OFAIL       }, // rename OFAILURE to OFAIL
    { "OGFAIL",         -1                }, // rename OGFAIL to XOGFAIL
    { "OKILL",          -1                }, // rename OKILL to XOKILL
    { "OLEAVE",         T5X_A_OLEAVE      },
    { "OLFAIL",         T5X_A_OLFAIL      },
    { "OMOVE",          T5X_A_OMOVE       },
    { "OPAY",           -1                }, // rename OPAY to XOPAY
    { "OPAYMENT",       T5X_A_OPAY        }, // rename OPAYMENT to OPAY
    { "OPENLOCK",       -1                }, // rename OPENLOCK to XOPENLOCK
    { "ORFAIL",         -1                }, // rename ORFAIL to XORFAIL
    { "OSUCC",          -1                }, // rename OSUCC to XSUCC
    { "OSUCCESS",       T5X_A_OSUCC       }, // rename OSUCCESS to OSUCC
    { "OTFAIL",         -1                }, // rename OTFAIL to XOTFAIL
    { "OTPORT",         T5X_A_OTPORT      },
    { "OTOFAIL",        -1                }, // rename OTOFAIL to XOTOFAIL
    { "OUFAIL",         T5X_A_OUFAIL      },
    { "OUSE",           T5X_A_OUSE        },
    { "OXENTER",        T5X_A_OXENTER     },
    { "OXLEAVE",        T5X_A_OXLEAVE     },
    { "OXTPORT",        T5X_A_OXTPORT     },
    { "PAGELOCK",       -1                }, // rename PAGELOCK to XPAGELOCK
    { "PARENTLOCK",     -1                }, // rename PARENTLOCK to XPARENTLOCK
    { "PAY",            -1                }, // rename PAY to XPAY
    { "PAYMENT",        T5X_A_PAY         }, // rename PAYMENT to PAY
    { "PREFIX",         T5X_A_PREFIX      },
    { "PROGCMD",        -1                }, // rename PROGCMD to XPROGCMD
    { "QUEUEMAX",       -1                }, // rename QUEUEMAX to XQUEUEMAX
    { "QUOTA",          -1                }, // rename QUOTA to XQUOTA
    { "RECEIVELOCK",    -1                },
    { "REJECT",         -1                }, // rename REJECT to XREJECT
    { "REASON",         -1                }, // rename REASON to XREASON
    { "RFAIL",          -1                }, // rename RFAIL to XRFAIL
    { "RQUOTA",         T5X_A_RQUOTA      },
    { "RUNOUT",         T5X_A_RUNOUT      },
    { "SAYSTRING",      -1                }, // rename SAYSTRING to XSAYSTRING
    { "SEMAPHORE",      T5X_A_SEMAPHORE   },
    { "SEX",            T5X_A_SEX         },
    { "SIGNATURE",      -1                }, // rename SIGNATURE to XSIGNATURE
    { "MAILSIGNATURE",  T5X_A_SIGNATURE   }, // rename MAILSIGNATURE to SIGNATURE
    { "SPEECHMOD",      -1                }, // rename SPEECHMOD to XSPEECHMOD
    { "SPEECHLOCK",     -1                }, // rename SPEECHLOCK to XSPEECHLOCK
    { "STARTUP",        T5X_A_STARTUP     },
    { "SUCC",           T5X_A_SUCC        },
    { "TELOUTLOCK",     -1                }, // rename TELOUTLOCK to XTELOUTLOCK
    { "TFAIL",          -1                }, // rename TFAIL to XTFAIL
    { "TIMEOUT",        -1                }, // rename TIMEOUT to XTIMEOUT
    { "TPORT",          T5X_A_TPORT       },
    { "TPORTLOCK",      -1                }, // rename TPORTLOCK to XTPORTLOCK
    { "TOFAIL",         -1                }, // rename TOFAIL to XTOFAIL
    { "UFAIL",          T5X_A_UFAIL       },
    { "USE",            T5X_A_USE         },
    { "USELOCK",        -1                },
    { "USERLOCK",       -1                },
    { "VA",             T5X_A_VA          },
    { "VB",             T5X_A_VA+1        },
    { "VC",             T5X_A_VA+2        },
    { "VD",             T5X_A_VA+3        },
    { "VE",             T5X_A_VA+4        },
    { "VF",             T5X_A_VA+5        },
    { "VG",             T5X_A_VA+6        },
    { "VH",             T5X_A_VA+7        },
    { "VI",             T5X_A_VA+8        },
    { "VJ",             T5X_A_VA+9        },
    { "VK",             T5X_A_VA+10       },
    { "VL",             T5X_A_VA+11       },
    { "VM",             T5X_A_VA+12       },
    { "VRML_URL",       T5X_A_VRML_URL    },
    { "VN",             T5X_A_VA+13       },
    { "VO",             T5X_A_VA+14       },
    { "VP",             T5X_A_VA+15       },
    { "VQ",             T5X_A_VA+16       },
    { "VR",             T5X_A_VA+17       },
    { "VS",             T5X_A_VA+18       },
    { "VT",             T5X_A_VA+19       },
    { "VU",             T5X_A_VA+20       },
    { "VV",             T5X_A_VA+21       },
    { "VW",             T5X_A_VA+22       },
    { "VX",             T5X_A_VA+23       },
    { "VY",             T5X_A_VA+24       },
    { "VZ",             T5X_A_VA+25       },
    { "XYXXY",          T5X_A_PASS        },   // *Password
};

static struct
{
    const char *pName;
    int         iNum;
} p6h_locknames[] =
{
    { "Basic",       T5X_A_LOCK     },
    { "Enter",       T5X_A_LENTER   },
    { "Use",         T5X_A_LUSE     },
    { "Zone",        -1             },
    { "Page",        T5X_A_LPAGE    },
    { "Teleport",    T5X_A_LTPORT   },
    { "Speech",      T5X_A_LSPEECH  },
    { "Parent",      T5X_A_LPARENT  },
    { "Link",        T5X_A_LLINK    },
    { "Leave",       T5X_A_LLEAVE   },
    { "Drop",        T5X_A_LDROP    },
    { "Give",        T5X_A_LGIVE    },
    { "Receive",     T5X_A_LRECEIVE },
    { "Mail",        T5X_A_LMAIL    },
    { "Take",        T5X_A_LGET     },
    { "Open",        T5X_A_LOPEN    },
};

static NameMask p6h_attr_flags[] =
{
    { "no_command",     T5X_AF_NOCMD    },
    { "private",        T5X_AF_PRIVATE  },
    { "no_clone",       T5X_AF_NOCLONE  },
    { "wizard",         T5X_AF_WIZARD   },
    { "visual",         T5X_AF_VISUAL   },
    { "mortal_dark",    T5X_AF_MDARK    },
    { "hidden",         T5X_AF_DARK     },
    { "regexp",         T5X_AF_REGEXP   },
    { "case",           T5X_AF_CASE     },
    { "locked",         T5X_AF_LOCK     },
    { "internal",       T5X_AF_INTERNAL },
    { "debug",          T5X_AF_TRACE    },
    { "noname",         T5X_AF_NONAME   },
};

void T5X_GAME::ConvertFromP6H()
{
    SetFlags(T5X_MANDFLAGS_V2 | 2);

    // Build internal attribute names.
    //
    map<const char *, int, ltstr> AttrNamesKnown;
    for (int i = 0; i < sizeof(t5x_known_attrs)/sizeof(t5x_known_attrs[0]); i++)
    {
        AttrNamesKnown[StringClone(t5x_known_attrs[i].pName)] = t5x_known_attrs[i].iNum;
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
                    char *pAttrName = t5x_ConvertAttributeName((*itAttr)->m_pName);
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

        T5X_OBJECTINFO *poi = new T5X_OBJECTINFO;

        int iType = p6h_convert_type[it->second->m_iType];

        poi->SetRef(it->first);
        poi->SetName(StringClone(it->second->m_pName));
        if (it->second->m_fLocation)
        {
            int iLocation = it->second->m_dbLocation;
            if (  T5X_TYPE_EXIT == iType
               && -2 == iLocation)
            {
                poi->SetLocation(T5X_NOTHING);
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
            case T5X_TYPE_PLAYER:
            case T5X_TYPE_THING:
                poi->SetExits(T5X_NOTHING);
                poi->SetLink(it->second->m_dbExits);
                break;

            default:
                poi->SetExits(it->second->m_dbExits);
                poi->SetLink(T5X_NOTHING);
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
                flags1 |= T5X_IMMORTAL;
            }
        }
        poi->SetFlags1(flags1);
        poi->SetFlags2(flags2);
        poi->SetFlags3(flags3);
        poi->SetPowers1(powers1);
        poi->SetPowers2(powers2);

        if (it->second->m_fCreated)
        {
            time_t t = it->second->m_iCreated;
            char *pTime = ctime(&t);
            if (NULL != pTime)
            {
                char *p = strchr(pTime, '\n');
                if (NULL != p)
                {
                    size_t n = p - pTime;
                    pTime = StringCloneLen(pTime, n);

                    T5X_ATTRINFO *pai = new T5X_ATTRINFO;
                    pai->SetNumAndValue(T5X_A_CREATED, StringClone(pTime));

                    if (NULL == poi->m_pvai)
                    {
                        vector<T5X_ATTRINFO *> *pvai = new vector<T5X_ATTRINFO *>;
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
        }

        if (it->second->m_fModified)
        {
            time_t t = it->second->m_iModified;
            char *pTime = ctime(&t);
            if (NULL != pTime)
            {
                char *p = strchr(pTime, '\n');
                if (NULL != p)
                {
                    size_t n = p - pTime;
                    pTime = StringCloneLen(pTime, n);

                    // A_MODIFIED
                    //
                    T5X_ATTRINFO *pai = new T5X_ATTRINFO;
                    pai->SetNumAndValue(T5X_A_MODIFIED, StringClone(pTime));

                    if (NULL == poi->m_pvai)
                    {
                        vector<T5X_ATTRINFO *> *pvai = new vector<T5X_ATTRINFO *>;
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
        }

        if (NULL != it->second->m_pvai)
        {
            vector<T5X_ATTRINFO *> *pvai = new vector<T5X_ATTRINFO *>;
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
                    char *pAttrName = t5x_ConvertAttributeName((*itAttr)->m_pName);
                    map<const char *, int , ltstr>::iterator itFound = AttrNamesKnown.find(pAttrName);
                    if (itFound != AttrNamesKnown.end())
                    {
                        T5X_ATTRINFO *pai = new T5X_ATTRINFO;
                        int iNum = AttrNamesKnown[pAttrName];
                        if (T5X_A_PASS == iNum)
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
                            T5X_ATTRINFO *pai = new T5X_ATTRINFO;
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
                        T5X_LOCKEXP *pLock = new T5X_LOCKEXP;
                        if (pLock->ConvertFromP6H((*itLock)->m_pKeyTree))
                        {
                            if (T5X_A_LOCK == iLock)
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
                                T5X_ATTRINFO *pai = new T5X_ATTRINFO;
                                pai->SetNumAndValue(iLock, StringClone(buffer));

                                if (NULL == poi->m_pvai)
                                {
                                    vector<T5X_ATTRINFO *> *pvai = new vector<T5X_ATTRINFO *>;
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

char *convert_t6h_quota(char *p)
{
    int maxquota = 0;
    for (;;)
    {
        maxquota = max(maxquota, atoi(p));
        p = strchr(p, ' ');
        if (NULL != p)
        {
            while (isspace(*p))
            {
                p++;
            }
        }
        else
        {
            break;
        }
    }

    static char buffer[100];
    sprintf(buffer, "%d", maxquota);
    return buffer;
}

bool convert_t6h_attr_num(int iNum, int *piNum)
{
    if (A_USER_START <= iNum)
    {
        *piNum = iNum;
        return true;
    }

    // T6H attribute numbers with no corresponding T5X attribute.
    //
    if (  T6H_A_NULL == iNum
       || T6H_A_NEWOBJS == iNum
       || T6H_A_MAILCC == iNum
       || T6H_A_MAILBCC == iNum
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

    if (T6H_A_SPEECHFMT == iNum)
    {
        iNum = T5X_A_SPEECHMOD;
    }
    else if (T6H_A_LEXITS_FMT == iNum)
    {
        iNum = T5X_A_EXITFORMAT;
    }
    else if (T6H_A_LCON_FMT == iNum)
    {
        iNum = T5X_A_CONFORMAT;
    }
    else if (T6H_A_NAME_FMT == iNum)
    {
        iNum = T5X_A_NAMEFORMAT;
    }
    else if (T6H_A_LASTIP == iNum)
    {
        iNum = T5X_A_LASTIP;
    }

    // T5X attributes with no corresponding T6H attribute, and nothing
    // in T6H currently uses the number, but it might be assigned later.
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
    *piNum = iNum;
    return true;
}

int convert_t6h_flags1(int f)
{
    f &= T5X_SEETHRU
       | T5X_WIZARD
       | T5X_LINK_OK
       | T5X_DARK
       | T5X_JUMP_OK
       | T5X_STICKY
       | T5X_DESTROY_OK
       | T5X_HAVEN
       | T5X_QUIET
       | T5X_HALT
       | T5X_TRACE
       | T5X_GOING
       | T5X_MONITOR
       | T5X_MYOPIC
       | T5X_PUPPET
       | T5X_CHOWN_OK
       | T5X_ENTER_OK
       | T5X_VISUAL
       | T5X_IMMORTAL
       | T5X_HAS_STARTUP
       | T5X_OPAQUE
       | T5X_VERBOSE
       | T5X_INHERIT
       | T5X_NOSPOOF
       | T5X_ROBOT
       | T5X_SAFE
       | T5X_ROYALTY
       | T5X_HEARTHRU
       | T5X_TERSE;
    return f;
}

int convert_t6h_flags2(int f)
{
    int g = f;
    g &= T5X_KEY
       | T5X_ABODE
       | T5X_FLOATING
       | T5X_UNFINDABLE
       | T5X_PARENT_OK
       | T5X_LIGHT
       | T5X_HAS_LISTEN
       | T5X_HAS_FWDLIST
       | T5X_AUDITORIUM
       | T5X_ANSI
       | T5X_HEAD_FLAG
       | T5X_FIXED
       | T5X_UNINSPECTED
       | T5X_NOBLEED
       | T5X_STAFF
       | T5X_HAS_DAILY
       | T5X_GAGGED
       | T5X_VACATION
       | T5X_PLAYER_MAILS
       | T5X_HTML
       | T5X_BLIND
       | T5X_SUSPECT
       | T5X_CONNECTED
       | T5X_SLAVE;

    if ((f & T6H_HAS_COMMANDS) == 0)
    {
        g |= T5X_NO_COMMAND;
    }

    return g;
}

int convert_t6h_flags3(int f)
{
    f &= T5X_MARK_0
       | T5X_MARK_1
       | T5X_MARK_2
       | T5X_MARK_3
       | T5X_MARK_4
       | T5X_MARK_5
       | T5X_MARK_6
       | T5X_MARK_7
       | T5X_MARK_8
       | T5X_MARK_9;
    return f;
}

int convert_t6h_attr_flags(int f)
{
    int g = f;
    g &= T5X_AF_ODARK
       | T5X_AF_DARK
       | T5X_AF_WIZARD
       | T5X_AF_MDARK
       | T5X_AF_INTERNAL
       | T5X_AF_NOCMD
       | T5X_AF_LOCK
       | T5X_AF_DELETED
       | T5X_AF_NOPROG
       | T5X_AF_GOD
       | T5X_AF_IS_LOCK
       | T5X_AF_VISUAL
       | T5X_AF_PRIVATE
       | T5X_AF_HTML
       | T5X_AF_NOPARSE
       | T5X_AF_REGEXP
       | T5X_AF_NOCLONE
       | T5X_AF_CONST
       | T5X_AF_CASE
       | T5X_AF_NONAME;

    if (f & T6H_AF_TRACE)
    {
        g |= T5X_AF_TRACE;
    }
    return g;
}

int convert_t6h_power1(int f)
{
    f &= T5X_POW_CHG_QUOTAS
       | T5X_POW_CHOWN_ANY
       | T5X_POW_ANNOUNCE
       | T5X_POW_BOOT
       | T5X_POW_HALT
       | T5X_POW_CONTROL_ALL
       | T5X_POW_WIZARD_WHO
       | T5X_POW_EXAM_ALL
       | T5X_POW_FIND_UNFIND
       | T5X_POW_FREE_MONEY
       | T5X_POW_FREE_QUOTA
       | T5X_POW_HIDE
       | T5X_POW_IDLE
       | T5X_POW_SEARCH
       | T5X_POW_LONGFINGERS
       | T5X_POW_PROG
       | T5X_POW_COMM_ALL
       | T5X_POW_SEE_QUEUE
       | T5X_POW_SEE_HIDDEN
       | T5X_POW_MONITOR
       | T5X_POW_POLL
       | T5X_POW_NO_DESTROY
       | T5X_POW_GUEST
       | T5X_POW_PASS_LOCKS
       | T5X_POW_STAT_ANY
       | T5X_POW_STEAL
       | T5X_POW_TEL_ANYWHR
       | T5X_POW_TEL_UNRST
       | T5X_POW_UNKILLABLE;
    return f;
}

int convert_t6h_power2(int f)
{
    f &= T5X_POW_BUILDER;
    return f;
}

void T5X_GAME::ConvertFromT6H()
{
    SetFlags(T5X_MANDFLAGS_V2 | 2);

    // Attribute names
    //
    for (map<int, T6H_ATTRNAMEINFO *, lti>::iterator it =  g_t6hgame.m_mAttrNames.begin(); it != g_t6hgame.m_mAttrNames.end(); ++it)
    {
        AddNumAndName(it->second->m_iNum, StringClone(it->second->m_pNameEncoded));
    }
    if (!m_fNextAttr)
    {
        SetNextAttr(g_t6hgame.m_nNextAttr);
    }

    int dbRefMax = 0;
    for (map<int, T6H_OBJECTINFO *, lti>::iterator it = g_t6hgame.m_mObjects.begin(); it != g_t6hgame.m_mObjects.end(); ++it)
    {
        if (!it->second->m_fFlags1)
        {
            continue;
        }

        // ROOM, THING, EXIT, and PLAYER types are the same between T6H and
        // T5X.  No mapping is required.
        //
        int iType = (it->second->m_iFlags1) & T5X_TYPE_MASK;
        if (  T5X_TYPE_ROOM != iType
           && T5X_TYPE_THING != iType
           && T5X_TYPE_EXIT != iType
           && T5X_TYPE_PLAYER != iType)
        {
            continue;
        }

        T5X_OBJECTINFO *poi = new T5X_OBJECTINFO;

        poi->SetRef(it->first);
        poi->SetName(StringClone(it->second->m_pName));
        if (it->second->m_fLocation)
        {
            int iLocation = it->second->m_dbLocation;
            if (  T5X_TYPE_EXIT == iType
               && -2 == iLocation)
            {
                poi->SetLocation(T5X_NOTHING);
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
            flags1 |= convert_t6h_flags1(it->second->m_iFlags1);
        }
        if (it->second->m_fFlags2)
        {
            flags2 = convert_t6h_flags2(it->second->m_iFlags2);
        }
        if (it->second->m_fFlags3)
        {
            flags3 = convert_t6h_flags3(it->second->m_iFlags3);
        }

        // Powers
        //
        int powers1 = 0;
        int powers2 = 0;
        if (it->second->m_fPowers1)
        {
            powers1 = convert_t6h_power1(it->second->m_iPowers1);
        }
        if (it->second->m_fPowers2)
        {
            powers2 = convert_t6h_power2(it->second->m_iPowers2);
        }

        poi->SetFlags1(flags1);
        poi->SetFlags2(flags2);
        poi->SetFlags3(flags3);
        poi->SetPowers1(powers1);
        poi->SetPowers2(powers2);

        if (it->second->m_fCreated)
        {
            time_t t = it->second->m_iCreated;
            char *pTime = ctime(&t);
            if (NULL != pTime)
            {
                char *p = strchr(pTime, '\n');
                if (NULL != p)
                {
                    size_t n = p - pTime;
                    pTime = StringCloneLen(pTime, n);

                    T5X_ATTRINFO *pai = new T5X_ATTRINFO;
                    pai->SetNumAndValue(T5X_A_CREATED, StringClone(pTime));

                    if (NULL == poi->m_pvai)
                    {
                        vector<T5X_ATTRINFO *> *pvai = new vector<T5X_ATTRINFO *>;
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
        }

        if (it->second->m_fModified)
        {
            time_t t = it->second->m_iModified;
            char *pTime = ctime(&t);
            if (NULL != pTime)
            {
                char *p = strchr(pTime, '\n');
                if (NULL != p)
                {
                    size_t n = p - pTime;
                    pTime = StringCloneLen(pTime, n);

                    T5X_ATTRINFO *pai = new T5X_ATTRINFO;
                    pai->SetNumAndValue(T5X_A_MODIFIED, StringClone(pTime));

                    if (NULL == poi->m_pvai)
                    {
                        vector<T5X_ATTRINFO *> *pvai = new vector<T5X_ATTRINFO *>;
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
        }

        if (NULL != it->second->m_pvai)
        {
            vector<T5X_ATTRINFO *> *pvai = new vector<T5X_ATTRINFO *>;
            for (vector<T6H_ATTRINFO *>::iterator itAttr = it->second->m_pvai->begin(); itAttr != it->second->m_pvai->end(); ++itAttr)
            {
                int iNum;
                if ((*itAttr)->m_fNumAndValue)
                {
                    if (T6H_A_QUOTA == (*itAttr)->m_iNum)
                    {
                        // Typed quota needs to be converted to single quota.
                        //
                        T5X_ATTRINFO *pai = new T5X_ATTRINFO;
                        pai->SetNumAndValue(T5X_A_QUOTA, StringClone(convert_t6h_quota((*itAttr)->m_pValueUnencoded)));
                        pvai->push_back(pai);
                    }
                    else if (convert_t6h_attr_num((*itAttr)->m_iNum, &iNum))
                    {
                        T5X_ATTRINFO *pai = new T5X_ATTRINFO;
                        pai->SetNumOwnerFlagsAndValue(iNum, (*itAttr)->m_dbOwner, convert_t6h_attr_flags((*itAttr)->m_iFlags), StringClone((*itAttr)->m_pValueUnencoded));
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

        if (it->second->m_ple)
        {
            T5X_LOCKEXP *ple = new T5X_LOCKEXP;
            if (ple->ConvertFromT6H(it->second->m_ple))
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

bool convert_r7h_attr_num(int iNum, int *piNum)
{
    if (A_USER_START <= iNum)
    {
        *piNum = iNum;
        return true;
    }

    // R7H attribute numbers with no corresponding T5X attribute.
    //
    if (  R7H_A_SPAMPROTECT == iNum
       || R7H_A_RLEVEL == iNum
       || R7H_A_DESTVATTRMAX == iNum
       || R7H_A_TEMPBUFFER == iNum
       || R7H_A_PROGPROMPTBUF == iNum
       || R7H_A_PROGPROMPT == iNum
       || R7H_A_PROGBUFFER == iNum
       || R7H_A_SAVESENDMAIL == iNum
       || R7H_A_LAMBDA == iNum
       || R7H_A_CHANNEL == iNum
       || R7H_A_GUILD == iNum
       || (R7H_A_ZA <= iNum && iNum <= R7H_A_ZA + 25)
       || R7H_A_BCCMAIL == iNum
       || R7H_A_EMAIL == iNum
       || R7H_A_LSHARE == iNum
       || R7H_A_AOTFAIL == iNum
       || R7H_A_MPASS == iNum
       || R7H_A_MPSET == iNum
       || R7H_A_LASTPAGE == iNum
       || R7H_A_RETPAGE == iNum
       || R7H_A_RECTIME == iNum
       || R7H_A_MCURR == iNum
       || R7H_A_MQUOTA == iNum
       || R7H_A_LQUOTA == iNum
       || R7H_A_TQUOTA == iNum
       || R7H_A_MTIME == iNum
       || R7H_A_MSAVEMAX == iNum
       || R7H_A_MSAVECUR == iNum
       || R7H_A_IDENT == iNum
       || R7H_A_LZONEWIZ == iNum
       || R7H_A_LZONETO == iNum
       || R7H_A_LTWINK == iNum
       || R7H_A_SITEGOOD == iNum
       || R7H_A_SITEBAD == iNum
       || R7H_A_ADESC2 == iNum
       || R7H_A_PAYLIM == iNum
       || R7H_A_DESC2 == iNum
       || R7H_A_RACE == iNum
       || R7H_A_SFAIL == iNum
       || R7H_A_ASFAIL == iNum
       || R7H_A_AUTOREG == iNum
       || R7H_A_LDARK == iNum
       || R7H_A_STOUCH == iNum
       || R7H_A_SATOUCH == iNum
       || R7H_A_SOTOUCH == iNum
       || R7H_A_SLISTEN == iNum
       || R7H_A_SALISTEN == iNum
       || R7H_A_SOLISTEN == iNum
       || R7H_A_STASTE == iNum
       || R7H_A_SATASTE == iNum
       || R7H_A_SOTASTE == iNum
       || R7H_A_SSMELL == iNum
       || R7H_A_SASMELL == iNum
       || R7H_A_SOSMELL == iNum
       || R7H_A_LDROPTO == iNum
       || R7H_A_CAPTION == iNum
       || R7H_A_TOTCMDS == iNum
       || R7H_A_LSTCMDS == iNum
       || R7H_A_RECEIVELIM == iNum
       || R7H_A_LDEXIT_FMT == iNum
       || R7H_A_ALTNAME == iNum
       || R7H_A_LALTNAME == iNum
       || R7H_A_INVTYPE == iNum
       || R7H_A_TOTCHARIN == iNum
       || R7H_A_TOTCHAROUT == iNum
       || R7H_A_LGIVETO == iNum
       || R7H_A_LASTCREATE == iNum)
    {
        return false;
    }

    if (R7H_A_NAME_FMT == iNum)
    {
        iNum = T5X_A_NAMEFORMAT;
    }
    else if (R7H_A_LASTIP == iNum)
    {
        iNum = T5X_A_LASTIP;
    }
    else if (R7H_A_LCON_FMT == iNum)
    {
        iNum = T5X_A_CONFORMAT;
    }
    else if (R7H_A_EXITTO == iNum)
    {
        iNum = T5X_A_EXITVARDEST;
    }
    else if (R7H_A_LCONTROL == iNum)
    {
        iNum = T5X_A_LCONTROL;
    }
    else if (R7H_A_LMAIL == iNum)
    {
        iNum = T5X_A_LMAIL;
    }
    else if (R7H_A_LGETFROM == iNum)
    {
        iNum = T5X_A_LGET;
    }
    else if (R7H_A_GFAIL == iNum)
    {
        iNum = T5X_A_GFAIL;
    }
    else if (R7H_A_OGFAIL == iNum)
    {
        iNum = T5X_A_OGFAIL;
    }
    else if (R7H_A_AGFAIL == iNum)
    {
        iNum = T5X_A_AGFAIL;
    }
    else if (R7H_A_RFAIL == iNum)
    {
        iNum = T5X_A_RFAIL;
    }
    else if (R7H_A_ORFAIL == iNum)
    {
        iNum = T5X_A_ORFAIL;
    }
    else if (R7H_A_ARFAIL == iNum)
    {
        iNum = T5X_A_ARFAIL;
    }
    else if (R7H_A_DFAIL == iNum)
    {
        iNum = T5X_A_DFAIL;
    }
    else if (R7H_A_ODFAIL == iNum)
    {
        iNum = T5X_A_ODFAIL;
    }
    else if (R7H_A_ADFAIL == iNum)
    {
        iNum = T5X_A_ADFAIL;
    }
    else if (R7H_A_TFAIL == iNum)
    {
        iNum = T5X_A_TFAIL;
    }
    else if (R7H_A_OTFAIL == iNum)
    {
        iNum = T5X_A_OTFAIL;
    }
    else if (R7H_A_ATFAIL == iNum)
    {
        iNum = T5X_A_ATFAIL;
    }
    else if (R7H_A_TOFAIL == iNum)
    {
        iNum = T5X_A_TOFAIL;
    }
    else if (R7H_A_OTOFAIL == iNum)
    {
        iNum = T5X_A_OTOFAIL;
    }
    else if (R7H_A_ATOFAIL == iNum)
    {
        iNum = T5X_A_ATOFAIL;
    }
    else if (R7H_A_MAILSIG == iNum)
    {
        iNum = T5X_A_SIGNATURE;
    }
    else if (R7H_A_LSPEECH == iNum)
    {
        iNum = T5X_A_LSPEECH;
    }
    else if (R7H_A_ANSINAME == iNum)
    {
        iNum = T5X_A_MONIKER;
    }
    else if (R7H_A_LOPEN == iNum)
    {
        iNum = T5X_A_LOPEN;
    }
    else if (R7H_A_LCHOWN == iNum)
    {
        iNum = T5X_A_LCHOWN;
    }
    else if (R7H_A_LCON_FMT == iNum)
    {
        iNum = T5X_A_CONFORMAT;
    }
    else if (R7H_A_LEXIT_FMT == iNum)
    {
        iNum = T5X_A_EXITFORMAT;
    }
    else if (R7H_A_MODIFY_TIME == iNum)
    {
        iNum = T5X_A_MODIFIED;
    }
    else if (R7H_A_CREATED_TIME == iNum)
    {
        iNum = T5X_A_CREATED;
    }
    else if (R7H_A_SAYSTRING == iNum)
    {
        iNum = T5X_A_SAYSTRING;
    }

    // T5X attributes with no corresponding R7H attribute, and nothing
    // in R7H currently uses the number, but it might be assigned later.
    //
    if (  T5X_A_MFAIL == iNum
       || T5X_A_COMJOIN == iNum
       || T5X_A_COMLEAVE == iNum
       || T5X_A_COMON == iNum
       || T5X_A_COMOFF == iNum
       || T5X_A_CMDCHECK == iNum
       || T5X_A_MONIKER == iNum
       || T5X_A_CONNINFO == iNum
       || T5X_A_IDLETMOUT == iNum
       || T5X_A_ADESTROY == iNum
       || T5X_A_APARENT == iNum
       || T5X_A_ACREATE == iNum
       || T5X_A_LMAIL == iNum
       || T5X_A_LOPEN == iNum
       || T5X_A_LASTWHISPER == iNum
       || T5X_A_LVISIBLE == iNum
       || T5X_A_LASTPAGE == iNum
       || T5X_A_MAIL == iNum
       || T5X_A_AMAIL == iNum
       || T5X_A_DAILY == iNum
       || T5X_A_MAILTO == iNum
       || T5X_A_MAILMSG == iNum
       || T5X_A_MAILSUB == iNum
       || T5X_A_MAILCURF == iNum
       || T5X_A_PROGCMD == iNum
       || T5X_A_MAILFLAGS == iNum
       || T5X_A_DESTROYER == iNum
       || T5X_A_NEWOBJS == iNum
       || T5X_A_SPEECHMOD == iNum
       || T5X_A_VRML_URL == iNum
       || T5X_A_HTDESC == iNum
       || T5X_A_REASON == iNum
       || T5X_A_REGINFO == iNum
       || T5X_A_CONNINFO == iNum
       || T5X_A_LVISIBLE == iNum
       || T5X_A_IDLETMOUT == iNum
       || T5X_A_NAMEFORMAT == iNum
       || T5X_A_DESCFORMAT == iNum)
    {
        return false;
    }
    *piNum = iNum;
    return true;
}

int convert_r7h_flags1(int f)
{
    f &= T5X_SEETHRU
       | T5X_WIZARD
       | T5X_LINK_OK
       | T5X_DARK
       | T5X_JUMP_OK
       | T5X_STICKY
       | T5X_DESTROY_OK
       | T5X_HAVEN
       | T5X_QUIET
       | T5X_HALT
       | T5X_TRACE
       | T5X_GOING
       | T5X_MONITOR
       | T5X_MYOPIC
       | T5X_PUPPET
       | T5X_CHOWN_OK
       | T5X_ENTER_OK
       | T5X_VISUAL
       | T5X_IMMORTAL
       | T5X_HAS_STARTUP
       | T5X_OPAQUE
       | T5X_VERBOSE
       | T5X_INHERIT
       | T5X_NOSPOOF
       | T5X_ROBOT
       | T5X_SAFE
       | T5X_HEARTHRU
       | T5X_TERSE;
    return f;
}

int convert_r7h_flags2(int f2, int f3, int f4)
{
    int g = f2;
    g &= T5X_KEY
       | T5X_ABODE
       | T5X_FLOATING
       | T5X_UNFINDABLE
       | T5X_PARENT_OK
       | T5X_LIGHT
       | T5X_HAS_LISTEN
       | T5X_HAS_FWDLIST
       | T5X_SUSPECT
       | T5X_CONNECTED
       | T5X_SLAVE;

    if (f2 & R7H_ADMIN)
    {
        g |= T5X_STAFF;
    }
    if (f2 & (R7H_ANSI|R7H_ANSICOLOR))
    {
        g |= T5X_ANSI;
    }
    if (f3 & R7H_NOCOMMAND)
    {
        g |= T5X_NO_COMMAND;
    }
    if (f4 & R7H_BLIND)
    {
        g |= T5X_BLIND;
    }

    return g;
}

int convert_r7h_flags3(int f3, int f4)
{
    int g = 0;
    if (f3 & R7H_MARKER0)
    {
        g |= T5X_MARK_0;
    }
    if (f3 & R7H_MARKER1)
    {
        g |= T5X_MARK_1;
    }
    if (f3 & R7H_MARKER2)
    {
        g |= T5X_MARK_2;
    }
    if (f3 & R7H_MARKER3)
    {
        g |= T5X_MARK_3;
    }
    if (f3 & R7H_MARKER4)
    {
        g |= T5X_MARK_4;
    }
    if (f3 & R7H_MARKER5)
    {
        g |= T5X_MARK_5;
    }
    if (f3 & R7H_MARKER6)
    {
        g |= T5X_MARK_6;
    }
    if (f3 & R7H_MARKER7)
    {
        g |= T5X_MARK_7;
    }
    if (f3 & R7H_MARKER8)
    {
        g |= T5X_MARK_8;
    }
    return g;
}

int convert_r7h_attr_flags(int f)
{
    int g = f;
    g &= T5X_AF_ODARK
       | T5X_AF_DARK
       | T5X_AF_WIZARD
       | T5X_AF_MDARK
       | T5X_AF_INTERNAL
       | T5X_AF_NOCMD
       | T5X_AF_LOCK
       | T5X_AF_DELETED
       | T5X_AF_NOPROG
       | T5X_AF_GOD;

    if (f & R7H_AF_IS_LOCK)
    {
        g |= T5X_AF_IS_LOCK;
    }
    if (f & R7H_AF_PRIVATE)
    {
        g |= T5X_AF_PRIVATE;
    }
    if (f & R7H_AF_VISUAL)
    {
        g |= T5X_AF_VISUAL;
    }
    if (f & R7H_AF_NOCLONE)
    {
        g |= T5X_AF_NOCLONE;
    }
    if (f & R7H_AF_NOPARSE)
    {
        g |= T5X_AF_NOPARSE;
    }

    return g;
}

bool AnyLevel(int tog, int i)
{
    return (tog & (3 << i)) != 0;
}

int convert_r7h_power1(int t3, int t4, int t5)
{
    int g = 0;
    if (AnyLevel(t3, R7H_POWER_CHANGE_QUOTAS))
    {
        g |= T5X_POW_CHG_QUOTAS;
    }
    if (AnyLevel(t3, R7H_POWER_CHOWN_OTHER))
    {
        g |= T5X_POW_CHOWN_ANY;
    }
    if (AnyLevel(t4, R7H_POWER_FREE_WALL))
    {
        g |= T5X_POW_ANNOUNCE;
    }
    if (AnyLevel(t3, R7H_POWER_BOOT))
    {
        g |= T5X_POW_BOOT;
    }
    if (  AnyLevel(t4, R7H_POWER_HALT_QUEUE)
       || AnyLevel(t4, R7H_POWER_HALT_QUEUE_ALL))
    {
        g |= T5X_POW_HALT;
    }
    if (AnyLevel(t3, R7H_POWER_WIZ_WHO))
    {
        g |= T5X_POW_WIZARD_WHO;
    }
    if (AnyLevel(t5, R7H_POWER_EX_FULL))
    {
        g |= T5X_POW_EXAM_ALL;
    }
    if (AnyLevel(t4, R7H_POWER_WHO_UNFIND))
    {
        g |= T5X_POW_FIND_UNFIND;
    }
    if (AnyLevel(t3, R7H_POWER_FREE_QUOTA))
    {
        g |= T5X_POW_FREE_QUOTA;
    }
    if (AnyLevel(t5, R7H_POWER_HIDEBIT))
    {
        g |= T5X_POW_HIDE;
    }
    if (AnyLevel(t4, R7H_POWER_SEARCH_ANY))
    {
        g |= T5X_POW_SEARCH;
    }
    if (AnyLevel(t3, R7H_POWER_LONG_FINGERS))
    {
        g |= T5X_POW_LONGFINGERS;
    }
    if (AnyLevel(t3, R7H_POWER_SEE_QUEUE))
    {
        g |= T5X_POW_SEE_QUEUE;
    }
    if (AnyLevel(t4, R7H_POWER_STAT_ANY))
    {
        g |= T5X_POW_STAT_ANY;
    }
    if (AnyLevel(t3, R7H_POWER_STEAL))
    {
        g |= T5X_POW_STEAL;
    }
    if (AnyLevel(t4, R7H_POWER_TEL_ANYWHERE))
    {
        g |= T5X_POW_TEL_ANYWHR;
    }
    if (AnyLevel(t4, R7H_POWER_TEL_ANYTHING))
    {
        g |= T5X_POW_TEL_UNRST;
    }
    if (AnyLevel(t4, R7H_POWER_NOKILL))
    {
        g |= T5X_POW_UNKILLABLE;
    }
    return g;
}

int convert_r7h_power2(int f2)
{
    int g = 0;
    if (f2 & R7H_BUILDER)
    {
         g |= T5X_POW_BUILDER;
    }
    return g;
}

void T5X_GAME::ConvertFromR7H()
{
    SetFlags(T5X_MANDFLAGS_V2 | 2);

    // Attribute names
    //
    for (vector<R7H_ATTRNAMEINFO *>::iterator it =  g_r7hgame.m_vAttrNames.begin(); it != g_r7hgame.m_vAttrNames.end(); ++it)
    {
        AddNumAndName((*it)->m_iNum, StringClone((*it)->m_pName));
    }
    if (!m_fNextAttr)
    {
        SetNextAttr(g_r7hgame.m_nNextAttr);
    }

    int dbRefMax = 0;
    for (map<int, R7H_OBJECTINFO *, lti>::iterator it = g_r7hgame.m_mObjects.begin(); it != g_r7hgame.m_mObjects.end(); ++it)
    {
        if (!it->second->m_fFlags1)
        {
            continue;
        }

        // ROOM, THING, EXIT, and PLAYER types are the same between R7H and
        // T5X.  No mapping is required.
        //
        int iType = (it->second->m_iFlags1) & T5X_TYPE_MASK;
        if (  T5X_TYPE_ROOM != iType
           && T5X_TYPE_THING != iType
           && T5X_TYPE_EXIT != iType
           && T5X_TYPE_PLAYER != iType)
        {
            continue;
        }

        T5X_OBJECTINFO *poi = new T5X_OBJECTINFO;

        poi->SetRef(it->first);
        poi->SetName(StringClone(it->second->m_pName));
        if (it->second->m_fLocation)
        {
            int iLocation = it->second->m_dbLocation;
            if (  T5X_TYPE_EXIT == iType
               && -2 == iLocation)
            {
                poi->SetLocation(T5X_NOTHING);
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
            flags1 |= convert_r7h_flags1(it->second->m_iFlags1);
        }
        if (it->second->m_fFlags2)
        {
            flags2 = convert_r7h_flags2(it->second->m_iFlags2, it->second->m_iFlags3, it->second->m_iFlags4);
        }
        if (it->second->m_fFlags3)
        {
            flags3 = convert_r7h_flags3(it->second->m_iFlags3, it->second->m_iFlags4);
        }

        // Powers
        //
        int powers1 = 0;
        int powers2 = 0;
        if (it->second->m_fToggles1)
        {
            powers1 = convert_r7h_power1(it->second->m_iToggles3, it->second->m_iToggles4, it->second->m_iToggles5);
        }
        if (it->second->m_fToggles2)
        {
            powers2 = convert_r7h_power2(it->second->m_iFlags2);
        }

        poi->SetFlags1(flags1);
        poi->SetFlags2(flags2);
        poi->SetFlags3(flags3);
        poi->SetPowers1(powers1);
        poi->SetPowers2(powers2);

        if (NULL != it->second->m_pvai)
        {
            vector<T5X_ATTRINFO *> *pvai = new vector<T5X_ATTRINFO *>;
            for (vector<R7H_ATTRINFO *>::iterator itAttr = it->second->m_pvai->begin(); itAttr != it->second->m_pvai->end(); ++itAttr)
            {
                int iNum;
                if (  (*itAttr)->m_fNumAndValue
                   && convert_r7h_attr_num((*itAttr)->m_iNum, &iNum))
                {
                    T5X_ATTRINFO *pai = new T5X_ATTRINFO;
                    pai->SetNumOwnerFlagsAndValue(iNum, (*itAttr)->m_dbOwner, convert_r7h_attr_flags((*itAttr)->m_iFlags), StringClone((*itAttr)->m_pValueUnencoded));
                    pvai->push_back(pai);
                }
            }
            if (0 < pvai->size())
            {
                poi->SetAttrs(pvai->size(), pvai);
                pvai = NULL;
            }
            delete pvai;
        }

        if (it->second->m_ple)
        {
            T5X_LOCKEXP *ple = new T5X_LOCKEXP;
            if (ple->ConvertFromR7H(it->second->m_ple))
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
    if (g_r7hgame.m_fRecordPlayers)
    {
        SetRecordPlayers(g_r7hgame.m_nRecordPlayers);
    }
    else
    {
        SetRecordPlayers(0);
    }
}

void T5X_GAME::ResetPassword()
{
    int ver = (m_flags & T5X_V_MASK);
    bool fSHA1 = (2 <= ver);
    for (map<int, T5X_OBJECTINFO *, lti>::iterator itObj = m_mObjects.begin(); itObj != m_mObjects.end(); ++itObj)
    {
        if (1 == itObj->first)
        {
            bool fFound = false;
            if (NULL != itObj->second->m_pvai)
            {
                for (vector<T5X_ATTRINFO *>::iterator itAttr = itObj->second->m_pvai->begin(); itAttr != itObj->second->m_pvai->end(); ++itAttr)
                {
                    if (T5X_A_PASS == (*itAttr)->m_iNum)
                    {
                        // Change it to 'potrzebie'.
                        //
                        if (fSHA1)
                        {
                            (*itAttr)->SetNumAndValue(T5X_A_PASS, StringClone("$SHA1$X0PG0reTn66s$FxO7KKs/CJ+an2rDWgGO4zpo1co="));
                        }
                        else
                        {
                            (*itAttr)->SetNumAndValue(T5X_A_PASS, StringClone("XXNHc95o0HhAc"));
                        }

                        fFound = true;
                    }
                }
            }

            if (!fFound)
            {
                // Add it.
                //
                T5X_ATTRINFO *pai = new T5X_ATTRINFO;
                if (fSHA1)
                {
                    pai->SetNumAndValue(T5X_A_PASS, StringClone("$SHA1$X0PG0reTn66s$FxO7KKs/CJ+an2rDWgGO4zpo1co="));
                }
                else
                {
                    pai->SetNumAndValue(T5X_A_PASS, StringClone("XXNHc95o0HhAc"));
                }

                if (NULL == itObj->second->m_pvai)
                {
                    vector<T5X_ATTRINFO *> *pvai = new vector<T5X_ATTRINFO *>;
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

#define T(x)    ((const UTF8 *)x)

#define UTF8_SIZE1     1
#define UTF8_SIZE2     2
#define UTF8_SIZE3     3
#define UTF8_SIZE4     4
#define UTF8_CONTINUE  5
#define UTF8_ILLEGAL   6

// This will help decode UTF-8 sequences.
//
// 0xxxxxxx ==> 00000000-01111111 ==> 00-7F 1 byte sequence.
// 10xxxxxx ==> 10000000-10111111 ==> 80-BF continue
// 110xxxxx ==> 11000000-11011111 ==> C0-DF 2 byte sequence.
// 1110xxxx ==> 11100000-11101111 ==> E0-EF 3 byte sequence.
// 11110xxx ==> 11110000-11110111 ==> F0-F7 4 byte sequence.
//              11111000-11111111 illegal
//
// Also, RFC 3629 specifies that 0xC0, 0xC1, and 0xF5-0xFF never
// appear in a valid sequence.
//
// The first byte gives the length of a sequence (UTF8_SIZE1 - UTF8_SIZE4).
// Bytes in the middle of a sequence map to UTF8_CONTINUE.  Bytes which should
// not appear map to UTF8_ILLEGAL.
//
const unsigned char utf8_FirstByte[256] =
{
//  0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
//
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 0
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 1
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 2
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 3
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 4
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 5
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 6
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 7

    5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  // 8
    5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  // 9
    5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  // A
    5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  // B
    6,  6,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  // C
    2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  // D
    3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  // E
    4,  4,  4,  4,  4,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6   // F
};

const bool ANSI_TokenTerminatorTable[256] =
{
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
//
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 1
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 2
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 3
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 4
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,  // 5
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

// The following table maps existing 8-bit characters to their corresponding
// UTF8 sequences.
//
const UTF8 *latin1_utf8[256] =
{
   T(""),             T("\x01"),         T("\x02"),         T("\x03"),
   T("\x04"),         T("\x05"),         T("\x06"),         T("\x07"),
   T("\x08"),         T("\x09"),         T("\x0A"),         T("\x0B"),
   T("\x0C"),         T("\x0D"),         T("\x0E"),         T("\x0F"),
   T("\x10"),         T("\x11"),         T("\x12"),         T("\x13"),
   T("\x14"),         T("\x15"),         T("\x16"),         T("\x17"),
   T("\x18"),         T("\x19"),         T("\x1A"),         T("\x1B"),
   T("\x1C"),         T("\x1D"),         T("\x1E"),         T("\x1F"),
   T("\x20"),         T("\x21"),         T("\x22"),         T("\x23"),
   T("\x24"),         T("\x25"),         T("\x26"),         T("\x27"),
   T("\x28"),         T("\x29"),         T("\x2A"),         T("\x2B"),
   T("\x2C"),         T("\x2D"),         T("\x2E"),         T("\x2F"),
   T("\x30"),         T("\x31"),         T("\x32"),         T("\x33"),
   T("\x34"),         T("\x35"),         T("\x36"),         T("\x37"),
   T("\x38"),         T("\x39"),         T("\x3A"),         T("\x3B"),
   T("\x3C"),         T("\x3D"),         T("\x3E"),         T("\x3F"),
   T("\x40"),         T("\x41"),         T("\x42"),         T("\x43"),
   T("\x44"),         T("\x45"),         T("\x46"),         T("\x47"),
   T("\x48"),         T("\x49"),         T("\x4A"),         T("\x4B"),
   T("\x4C"),         T("\x4D"),         T("\x4E"),         T("\x4F"),
   T("\x50"),         T("\x51"),         T("\x52"),         T("\x53"),
   T("\x54"),         T("\x55"),         T("\x56"),         T("\x57"),
   T("\x58"),         T("\x59"),         T("\x5A"),         T("\x5B"),
   T("\x5C"),         T("\x5D"),         T("\x5E"),         T("\x5F"),
   T("\x60"),         T("\x61"),         T("\x62"),         T("\x63"),
   T("\x64"),         T("\x65"),         T("\x66"),         T("\x67"),
   T("\x68"),         T("\x69"),         T("\x6A"),         T("\x6B"),
   T("\x6C"),         T("\x6D"),         T("\x6E"),         T("\x6F"),
   T("\x70"),         T("\x71"),         T("\x72"),         T("\x73"),
   T("\x74"),         T("\x75"),         T("\x76"),         T("\x77"),
   T("\x78"),         T("\x79"),         T("\x7A"),         T("\x7B"),
   T("\x7C"),         T("\x7D"),         T("\x7E"),         T("\x7F"),
   T("\xE2\x82\xAC"), T("\xEF\xBF\xBD"), T("\xE2\x80\x9A"), T("\xC6\x92"),
   T("\xE2\x80\x9E"), T("\xE2\x80\xA6"), T("\xE2\x80\xA0"), T("\xE2\x80\xA1"),
   T("\xCB\x86"),     T("\xE2\x80\xB0"), T("\xC5\xA0"),     T("\xE2\x80\xB9"),
   T("\xC5\x92"),     T("\xEF\xBF\xBD"), T("\xC5\xBD"),     T("\xEF\xBF\xBD"),
   T("\xEF\xBF\xBD"), T("\xE2\x80\x98"), T("\xE2\x80\x99"), T("\xE2\x80\x9C"),
   T("\xE2\x80\x9D"), T("\xE2\x80\xA2"), T("\xE2\x80\x93"), T("\xE2\x80\x94"),
   T("\xCB\x9C"),     T("\xE2\x84\xA2"), T("\xC5\xA1"),     T("\xE2\x80\xBA"),
   T("\xC5\x93"),     T("\xEF\xBF\xBD"), T("\xC5\xBE"),     T("\xC5\xB8"),
   T("\xC2\xA0"),     T("\xC2\xA1"),     T("\xC2\xA2"),     T("\xC2\xA3"),
   T("\xC2\xA4"),     T("\xC2\xA5"),     T("\xC2\xA6"),     T("\xC2\xA7"),
   T("\xC2\xA8"),     T("\xC2\xA9"),     T("\xC2\xAA"),     T("\xC2\xAB"),
   T("\xC2\xAC"),     T("\xC2\xAD"),     T("\xC2\xAE"),     T("\xC2\xAF"),
   T("\xC2\xB0"),     T("\xC2\xB1"),     T("\xC2\xB2"),     T("\xC2\xB3"),
   T("\xC2\xB4"),     T("\xC2\xB5"),     T("\xC2\xB6"),     T("\xC2\xB7"),
   T("\xC2\xB8"),     T("\xC2\xB9"),     T("\xC2\xBA"),     T("\xC2\xBB"),
   T("\xC2\xBC"),     T("\xC2\xBD"),     T("\xC2\xBE"),     T("\xC2\xBF"),
   T("\xC3\x80"),     T("\xC3\x81"),     T("\xC3\x82"),     T("\xC3\x83"),
   T("\xC3\x84"),     T("\xC3\x85"),     T("\xC3\x86"),     T("\xC3\x87"),
   T("\xC3\x88"),     T("\xC3\x89"),     T("\xC3\x8A"),     T("\xC3\x8B"),
   T("\xC3\x8C"),     T("\xC3\x8D"),     T("\xC3\x8E"),     T("\xC3\x8F"),
   T("\xC3\x90"),     T("\xC3\x91"),     T("\xC3\x92"),     T("\xC3\x93"),
   T("\xC3\x94"),     T("\xC3\x95"),     T("\xC3\x96"),     T("\xC3\x97"),
   T("\xC3\x98"),     T("\xC3\x99"),     T("\xC3\x9A"),     T("\xC3\x9B"),
   T("\xC3\x9C"),     T("\xC3\x9D"),     T("\xC3\x9E"),     T("\xC3\x9F"),
   T("\xC3\xA0"),     T("\xC3\xA1"),     T("\xC3\xA2"),     T("\xC3\xA3"),
   T("\xC3\xA4"),     T("\xC3\xA5"),     T("\xC3\xA6"),     T("\xC3\xA7"),
   T("\xC3\xA8"),     T("\xC3\xA9"),     T("\xC3\xAA"),     T("\xC3\xAB"),
   T("\xC3\xAC"),     T("\xC3\xAD"),     T("\xC3\xAE"),     T("\xC3\xAF"),
   T("\xC3\xB0"),     T("\xC3\xB1"),     T("\xC3\xB2"),     T("\xC3\xB3"),
   T("\xC3\xB4"),     T("\xC3\xB5"),     T("\xC3\xB6"),     T("\xC3\xB7"),
   T("\xC3\xB8"),     T("\xC3\xB9"),     T("\xC3\xBA"),     T("\xC3\xBB"),
   T("\xC3\xBC"),     T("\xC3\xBD"),     T("\xC3\xBE"),     T("\xC3\xBF"),
};

void utf8_safe_chr(const UTF8 *src, UTF8 *buff, UTF8 **bufc)
{
    size_t nLen;
    size_t nLeft;
    if (  NULL == src
       || UTF8_CONTINUE <= (nLen = utf8_FirstByte[*src])
       || (nLeft = LBUF_SIZE - (*bufc - buff) - 1) < nLen)
    {
        return;
    }
    memcpy(*bufc, src, nLen);
    *bufc += nLen;
}

#define NUM_OTHER               2
#define NUM_ATTR                4
#define NUM_FG                  256
#define NUM_BG                  256

#define COLOR_INDEX_ATTR        (NUM_OTHER)
#define COLOR_INDEX_FG          (COLOR_INDEX_ATTR + NUM_ATTR)
#define COLOR_INDEX_BG          (COLOR_INDEX_FG + NUM_FG)
#define COLOR_INDEX_FG_24       (COLOR_INDEX_BG + NUM_BG)
#define COLOR_INDEX_FG_24_RED   (COLOR_INDEX_FG_24)
#define COLOR_INDEX_FG_24_GREEN (COLOR_INDEX_FG_24_RED   + 256)
#define COLOR_INDEX_FG_24_BLUE  (COLOR_INDEX_FG_24_GREEN + 256)
#define COLOR_INDEX_BG_24       (COLOR_INDEX_FG_24_BLUE  + 256)
#define COLOR_INDEX_BG_24_RED   (COLOR_INDEX_BG_24)
#define COLOR_INDEX_BG_24_GREEN (COLOR_INDEX_BG_24_RED   + 256)
#define COLOR_INDEX_BG_24_BLUE  (COLOR_INDEX_BG_24_GREEN + 256)

#define COLOR_INDEX_RESET       1
#define COLOR_INDEX_INTENSE     (COLOR_INDEX_ATTR + 0)
#define COLOR_INDEX_UNDERLINE   (COLOR_INDEX_ATTR + 1)
#define COLOR_INDEX_BLINK       (COLOR_INDEX_ATTR + 2)
#define COLOR_INDEX_INVERSE     (COLOR_INDEX_ATTR + 3)

#define COLOR_INDEX_BLACK       0
#define COLOR_INDEX_RED         1
#define COLOR_INDEX_GREEN       2
#define COLOR_INDEX_YELLOW      3
#define COLOR_INDEX_BLUE        4
#define COLOR_INDEX_MAGENTA     5
#define COLOR_INDEX_CYAN        6
#define COLOR_INDEX_WHITE       7
#define COLOR_INDEX_DEFAULT     (NUM_FG)

#define COLOR_NOTCOLOR   0
#define COLOR_RESET      "\xEF\x94\x80"    // 1
#define COLOR_INTENSE    "\xEF\x94\x81"    // 2
#define COLOR_UNDERLINE  "\xEF\x94\x84"    // 3
#define COLOR_BLINK      "\xEF\x94\x85"    // 4
#define COLOR_INVERSE    "\xEF\x94\x87"    // 5
#define COLOR_FG_BLACK   "\xEF\x98\x80"    // 6
#define COLOR_FG_RED     "\xEF\x98\x81"    // 7
#define COLOR_FG_GREEN   "\xEF\x98\x82"    // 8
#define COLOR_FG_YELLOW  "\xEF\x98\x83"    // 9
#define COLOR_FG_BLUE    "\xEF\x98\x84"    // 10
#define COLOR_FG_MAGENTA "\xEF\x98\x85"    // 11
#define COLOR_FG_CYAN    "\xEF\x98\x86"    // 12
#define COLOR_FG_WHITE   "\xEF\x98\x87"    // 13
#define COLOR_FG_555555  "\xEF\x98\x88"    // 14
#define COLOR_FG_FF5555  "\xEF\x98\x89"    // 15
#define COLOR_FG_55FF55  "\xEF\x98\x8A"    // 16
#define COLOR_FG_FFFF55  "\xEF\x98\x8B"    // 17
#define COLOR_FG_5555FF  "\xEF\x98\x8C"    // 18
#define COLOR_FG_FF55FF  "\xEF\x98\x8D"    // 19
#define COLOR_FG_55FFFF  "\xEF\x98\x8E"    // 20
#define COLOR_FG_FFFFFF_1 "\xEF\x98\x8F"    // 21
#define COLOR_FG_000000  "\xEF\x98\x90"    // 22
#define COLOR_FG_00005F  "\xEF\x98\x91"    // 23
#define COLOR_FG_000087  "\xEF\x98\x92"    // 24
#define COLOR_FG_0000AF  "\xEF\x98\x93"    // 25
#define COLOR_FG_0000D7  "\xEF\x98\x94"    // 26
#define COLOR_FG_0000FF  "\xEF\x98\x95"    // 27
#define COLOR_FG_005F00  "\xEF\x98\x96"    // 28
#define COLOR_FG_005F5F  "\xEF\x98\x97"    // 29
#define COLOR_FG_005F87  "\xEF\x98\x98"    // 30
#define COLOR_FG_005FAF  "\xEF\x98\x99"    // 31
#define COLOR_FG_005FD7  "\xEF\x98\x9A"    // 32
#define COLOR_FG_005FFF  "\xEF\x98\x9B"    // 33
#define COLOR_FG_008700  "\xEF\x98\x9C"    // 34
#define COLOR_FG_00875F  "\xEF\x98\x9D"    // 35
#define COLOR_FG_008785  "\xEF\x98\x9E"    // 36
#define COLOR_FG_0087AF  "\xEF\x98\x9F"    // 37
#define COLOR_FG_0087D7  "\xEF\x98\xA0"    // 38
#define COLOR_FG_0087FF  "\xEF\x98\xA1"    // 39
#define COLOR_FG_00AF00  "\xEF\x98\xA2"    // 40
#define COLOR_FG_00AF5F  "\xEF\x98\xA3"    // 41
#define COLOR_FG_00AF87  "\xEF\x98\xA4"    // 42
#define COLOR_FG_00AFAF  "\xEF\x98\xA5"    // 43
#define COLOR_FG_00AFD7  "\xEF\x98\xA6"    // 44
#define COLOR_FG_00AFFF  "\xEF\x98\xA7"    // 45
#define COLOR_FG_00D700  "\xEF\x98\xA8"    // 46
#define COLOR_FG_00D75F  "\xEF\x98\xA9"    // 47
#define COLOR_FG_00D787  "\xEF\x98\xAA"    // 48
#define COLOR_FG_00D7AF  "\xEF\x98\xAB"    // 49
#define COLOR_FG_00D7D7  "\xEF\x98\xAC"    // 50
#define COLOR_FG_00D7FF  "\xEF\x98\xAD"    // 51
#define COLOR_FG_00FF00  "\xEF\x98\xAE"    // 52
#define COLOR_FG_00FF5A  "\xEF\x98\xAF"    // 53
#define COLOR_FG_00FF87  "\xEF\x98\xB0"    // 54
#define COLOR_FG_00FFAF  "\xEF\x98\xB1"    // 55
#define COLOR_FG_00FFD7  "\xEF\x98\xB2"    // 56
#define COLOR_FG_00FFFF  "\xEF\x98\xB3"    // 57
#define COLOR_FG_5F0000  "\xEF\x98\xB4"    // 58
#define COLOR_FG_5F005F  "\xEF\x98\xB5"    // 59
#define COLOR_FG_5F0087  "\xEF\x98\xB6"    // 60
#define COLOR_FG_5F00AF  "\xEF\x98\xB7"    // 61
#define COLOR_FG_5F00D7  "\xEF\x98\xB8"    // 62
#define COLOR_FG_5F00FF  "\xEF\x98\xB9"    // 63
#define COLOR_FG_5F5F00  "\xEF\x98\xBA"    // 64
#define COLOR_FG_5F5F5F  "\xEF\x98\xBB"    // 65
#define COLOR_FG_5F5F87  "\xEF\x98\xBC"    // 66
#define COLOR_FG_5F5FAF  "\xEF\x98\xBD"    // 67
#define COLOR_FG_5F5FD7  "\xEF\x98\xBE"    // 68
#define COLOR_FG_5F5FFF  "\xEF\x98\xBF"    // 69
#define COLOR_FG_5F8700  "\xEF\x99\x80"    // 70
#define COLOR_FG_5F875F  "\xEF\x99\x81"    // 71
#define COLOR_FG_5F8787  "\xEF\x99\x82"    // 72
#define COLOR_FG_5F87AF  "\xEF\x99\x83"    // 73
#define COLOR_FG_5F87D7  "\xEF\x99\x84"    // 74
#define COLOR_FG_5F87FF  "\xEF\x99\x85"    // 75
#define COLOR_FG_5FAF00  "\xEF\x99\x86"    // 76
#define COLOR_FG_5FAF5F  "\xEF\x99\x87"    // 77
#define COLOR_FG_5FAF87  "\xEF\x99\x88"    // 78
#define COLOR_FG_5FAFAF  "\xEF\x99\x89"    // 79
#define COLOR_FG_5FAFD7  "\xEF\x99\x8A"    // 80
#define COLOR_FG_5FAFFF  "\xEF\x99\x8B"    // 81
#define COLOR_FG_5FD700  "\xEF\x99\x8C"    // 82
#define COLOR_FG_5FD75F  "\xEF\x99\x8D"    // 83
#define COLOR_FG_5FD787  "\xEF\x99\x8E"    // 84
#define COLOR_FG_5FD7AF  "\xEF\x99\x8F"    // 85
#define COLOR_FG_5FD7D7  "\xEF\x99\x90"    // 86
#define COLOR_FG_5FD7FF  "\xEF\x99\x91"    // 87
#define COLOR_FG_5FFF00  "\xEF\x99\x92"    // 88
#define COLOR_FG_5FFF5F  "\xEF\x99\x93"    // 89
#define COLOR_FG_5FFF87  "\xEF\x99\x94"    // 90
#define COLOR_FG_5FFFAF  "\xEF\x99\x95"    // 91
#define COLOR_FG_5FFFD7  "\xEF\x99\x96"    // 92
#define COLOR_FG_5FFFFF  "\xEF\x99\x97"    // 93
#define COLOR_FG_870000  "\xEF\x99\x98"    // 94
#define COLOR_FG_87005F  "\xEF\x99\x99"    // 95
#define COLOR_FG_870087  "\xEF\x99\x9A"    // 96
#define COLOR_FG_8700AF  "\xEF\x99\x9B"    // 97
#define COLOR_FG_8700D7  "\xEF\x99\x9C"    // 98
#define COLOR_FG_8700FF  "\xEF\x99\x9D"    // 99
#define COLOR_FG_875F00  "\xEF\x99\x9E"    // 100
#define COLOR_FG_875F5F  "\xEF\x99\x9F"    // 101
#define COLOR_FG_875F87  "\xEF\x99\xA0"    // 102
#define COLOR_FG_875FAF  "\xEF\x99\xA1"    // 103
#define COLOR_FG_875FD7  "\xEF\x99\xA2"    // 104
#define COLOR_FG_875FFF  "\xEF\x99\xA3"    // 105
#define COLOR_FG_878700  "\xEF\x99\xA4"    // 106
#define COLOR_FG_87875F  "\xEF\x99\xA5"    // 107
#define COLOR_FG_878787  "\xEF\x99\xA6"    // 108
#define COLOR_FG_8787AF  "\xEF\x99\xA7"    // 109
#define COLOR_FG_8787D7  "\xEF\x99\xA8"    // 110
#define COLOR_FG_8787FF  "\xEF\x99\xA9"    // 111
#define COLOR_FG_87AF00  "\xEF\x99\xAA"    // 112
#define COLOR_FG_87AF5F  "\xEF\x99\xAB"    // 113
#define COLOR_FG_87AF87  "\xEF\x99\xAC"    // 114
#define COLOR_FG_87AFAF  "\xEF\x99\xAD"    // 115
#define COLOR_FG_87AFD7  "\xEF\x99\xAE"    // 116
#define COLOR_FG_87AFFF  "\xEF\x99\xAF"    // 117
#define COLOR_FG_87D700  "\xEF\x99\xB0"    // 118
#define COLOR_FG_87D75A  "\xEF\x99\xB1"    // 119
#define COLOR_FG_87D787  "\xEF\x99\xB2"    // 120
#define COLOR_FG_87D7AF  "\xEF\x99\xB3"    // 121
#define COLOR_FG_87D7D7  "\xEF\x99\xB4"    // 122
#define COLOR_FG_87D7FF  "\xEF\x99\xB5"    // 123
#define COLOR_FG_87FF00  "\xEF\x99\xB6"    // 124
#define COLOR_FG_87FF5F  "\xEF\x99\xB7"    // 125
#define COLOR_FG_87FF87  "\xEF\x99\xB8"    // 126
#define COLOR_FG_87FFAF  "\xEF\x99\xB9"    // 127
#define COLOR_FG_87FFD7  "\xEF\x99\xBA"    // 128
#define COLOR_FG_87FFFF  "\xEF\x99\xBB"    // 129
#define COLOR_FG_AF0000  "\xEF\x99\xBC"    // 130
#define COLOR_FG_AF005F  "\xEF\x99\xBD"    // 131
#define COLOR_FG_AF0087  "\xEF\x99\xBE"    // 132
#define COLOR_FG_AF00AF  "\xEF\x99\xBF"    // 133
#define COLOR_FG_AF00D7  "\xEF\x9A\x80"    // 134
#define COLOR_FG_AF00FF  "\xEF\x9A\x81"    // 135
#define COLOR_FG_AF5F00  "\xEF\x9A\x82"    // 136
#define COLOR_FG_AF5F5F  "\xEF\x9A\x83"    // 137
#define COLOR_FG_AF5F87  "\xEF\x9A\x84"    // 138
#define COLOR_FG_AF5FAF  "\xEF\x9A\x85"    // 139
#define COLOR_FG_AF5FD7  "\xEF\x9A\x86"    // 140
#define COLOR_FG_AF5FFF  "\xEF\x9A\x87"    // 141
#define COLOR_FG_AF8700  "\xEF\x9A\x88"    // 142
#define COLOR_FG_AF875F  "\xEF\x9A\x89"    // 143
#define COLOR_FG_AF8787  "\xEF\x9A\x8A"    // 144
#define COLOR_FG_AF87AF  "\xEF\x9A\x8B"    // 145
#define COLOR_FG_AF87D7  "\xEF\x9A\x8C"    // 146
#define COLOR_FG_AF87FF  "\xEF\x9A\x8D"    // 147
#define COLOR_FG_AFAF00  "\xEF\x9A\x8E"    // 148
#define COLOR_FG_AFAF5F  "\xEF\x9A\x8F"    // 149
#define COLOR_FG_AFAF87  "\xEF\x9A\x90"    // 150
#define COLOR_FG_AFAFAF  "\xEF\x9A\x91"    // 151
#define COLOR_FG_AFAFD7  "\xEF\x9A\x92"    // 152
#define COLOR_FG_AFAFFF  "\xEF\x9A\x93"    // 153
#define COLOR_FG_AFD700  "\xEF\x9A\x94"    // 154
#define COLOR_FG_AFD75F  "\xEF\x9A\x95"    // 155
#define COLOR_FG_AFD787  "\xEF\x9A\x96"    // 156
#define COLOR_FG_AFD7AF  "\xEF\x9A\x97"    // 157
#define COLOR_FG_AFD7D7  "\xEF\x9A\x98"    // 158
#define COLOR_FG_AFD7FF  "\xEF\x9A\x99"    // 159
#define COLOR_FG_AFFF00  "\xEF\x9A\x9A"    // 160
#define COLOR_FG_AFFF5F  "\xEF\x9A\x9B"    // 161
#define COLOR_FG_AFFF87  "\xEF\x9A\x9C"    // 162
#define COLOR_FG_AFFFAF  "\xEF\x9A\x9D"    // 163
#define COLOR_FG_AFFFD7  "\xEF\x9A\x9E"    // 164
#define COLOR_FG_AFFFFF  "\xEF\x9A\x9F"    // 165
#define COLOR_FG_D70000  "\xEF\x9A\xA0"    // 166
#define COLOR_FG_D7005F  "\xEF\x9A\xA1"    // 167
#define COLOR_FG_D70087  "\xEF\x9A\xA2"    // 168
#define COLOR_FG_D700AF  "\xEF\x9A\xA3"    // 169
#define COLOR_FG_D700D7  "\xEF\x9A\xA4"    // 170
#define COLOR_FG_D700FF  "\xEF\x9A\xA5"    // 171
#define COLOR_FG_D75F00  "\xEF\x9A\xA6"    // 172
#define COLOR_FG_D75F5F  "\xEF\x9A\xA7"    // 173
#define COLOR_FG_D75F87  "\xEF\x9A\xA8"    // 174
#define COLOR_FG_D75FAF  "\xEF\x9A\xA9"    // 175
#define COLOR_FG_D75FD7  "\xEF\x9A\xAA"    // 176
#define COLOR_FG_D75FFF  "\xEF\x9A\xAB"    // 177
#define COLOR_FG_D78700  "\xEF\x9A\xAC"    // 178
#define COLOR_FG_D7875A  "\xEF\x9A\xAD"    // 179
#define COLOR_FG_D78787  "\xEF\x9A\xAE"    // 180
#define COLOR_FG_D787AF  "\xEF\x9A\xAF"    // 181
#define COLOR_FG_D787D7  "\xEF\x9A\xB0"    // 182
#define COLOR_FG_D787FF  "\xEF\x9A\xB1"    // 183
#define COLOR_FG_D7AF00  "\xEF\x9A\xB2"    // 184
#define COLOR_FG_D7AF5A  "\xEF\x9A\xB3"    // 185
#define COLOR_FG_D7AF87  "\xEF\x9A\xB4"    // 186
#define COLOR_FG_D7AFAF  "\xEF\x9A\xB5"    // 187
#define COLOR_FG_D7AFD7  "\xEF\x9A\xB6"    // 188
#define COLOR_FG_D7AFFF  "\xEF\x9A\xB7"    // 189
#define COLOR_FG_D7D700  "\xEF\x9A\xB8"    // 190
#define COLOR_FG_D7D75F  "\xEF\x9A\xB9"    // 191
#define COLOR_FG_D7D787  "\xEF\x9A\xBA"    // 192
#define COLOR_FG_D7D7AF  "\xEF\x9A\xBB"    // 193
#define COLOR_FG_D7D7D7  "\xEF\x9A\xBC"    // 194
#define COLOR_FG_D7D7FF  "\xEF\x9A\xBD"    // 195
#define COLOR_FG_D7FF00  "\xEF\x9A\xBE"    // 196
#define COLOR_FG_D7FF5F  "\xEF\x9A\xBF"    // 197
#define COLOR_FG_D7FF87  "\xEF\x9B\x80"    // 198
#define COLOR_FG_D7FFAF  "\xEF\x9B\x81"    // 199
#define COLOR_FG_D7FFD7  "\xEF\x9B\x82"    // 200
#define COLOR_FG_D7FFFF  "\xEF\x9B\x83"    // 201
#define COLOR_FG_FF0000  "\xEF\x9B\x84"    // 202
#define COLOR_FG_FF005F  "\xEF\x9B\x85"    // 203
#define COLOR_FG_FF0087  "\xEF\x9B\x86"    // 204
#define COLOR_FG_FF00AF  "\xEF\x9B\x87"    // 205
#define COLOR_FG_FF00D7  "\xEF\x9B\x88"    // 206
#define COLOR_FG_FF00FF  "\xEF\x9B\x89"    // 207
#define COLOR_FG_FF5F00  "\xEF\x9B\x8A"    // 208
#define COLOR_FG_FF5F5F  "\xEF\x9B\x8B"    // 209
#define COLOR_FG_FF5F87  "\xEF\x9B\x8C"    // 210
#define COLOR_FG_FF5FAF  "\xEF\x9B\x8D"    // 211
#define COLOR_FG_FF5FD7  "\xEF\x9B\x8E"    // 212
#define COLOR_FG_FF5FFF  "\xEF\x9B\x8F"    // 213
#define COLOR_FG_FF8700  "\xEF\x9B\x90"    // 214
#define COLOR_FG_FF875F  "\xEF\x9B\x91"    // 215
#define COLOR_FG_FF8787  "\xEF\x9B\x92"    // 216
#define COLOR_FG_FF87AF  "\xEF\x9B\x93"    // 217
#define COLOR_FG_FF87D7  "\xEF\x9B\x94"    // 218
#define COLOR_FG_FF87FF  "\xEF\x9B\x95"    // 219
#define COLOR_FG_FFAF00  "\xEF\x9B\x96"    // 220
#define COLOR_FG_FFAF5F  "\xEF\x9B\x97"    // 221
#define COLOR_FG_FFAF87  "\xEF\x9B\x98"    // 222
#define COLOR_FG_FFAFAF  "\xEF\x9B\x99"    // 223
#define COLOR_FG_FFAFD7  "\xEF\x9B\x9A"    // 224
#define COLOR_FG_FFAFFF  "\xEF\x9B\x9B"    // 225
#define COLOR_FG_FFD700  "\xEF\x9B\x9C"    // 226
#define COLOR_FG_FFD75F  "\xEF\x9B\x9D"    // 227
#define COLOR_FG_FFD787  "\xEF\x9B\x9E"    // 228
#define COLOR_FG_FFD7AF  "\xEF\x9B\x9F"    // 229
#define COLOR_FG_FFD7D7  "\xEF\x9B\xA0"    // 230
#define COLOR_FG_FFD7FF  "\xEF\x9B\xA1"    // 231
#define COLOR_FG_FFFF00  "\xEF\x9B\xA2"    // 232
#define COLOR_FG_FFFF5F  "\xEF\x9B\xA3"    // 233
#define COLOR_FG_FFFF87  "\xEF\x9B\xA4"    // 234
#define COLOR_FG_FFFFAF  "\xEF\x9B\xA5"    // 235
#define COLOR_FG_FFFFD7  "\xEF\x9B\xA6"    // 236
#define COLOR_FG_FFFFFF_2 "\xEF\x9B\xA7"   // 237
#define COLOR_FG_080808  "\xEF\x9B\xA8"    // 238
#define COLOR_FG_121212  "\xEF\x9B\xA9"    // 239
#define COLOR_FG_1C1C1C  "\xEF\x9B\xAA"    // 240
#define COLOR_FG_262626  "\xEF\x9B\xAB"    // 241
#define COLOR_FG_303030  "\xEF\x9B\xAC"    // 242
#define COLOR_FG_3A3A3A  "\xEF\x9B\xAD"    // 243
#define COLOR_FG_444444  "\xEF\x9B\xAE"    // 244
#define COLOR_FG_4E4E4E  "\xEF\x9B\xAF"    // 245
#define COLOR_FG_585858  "\xEF\x9B\xB0"    // 246
#define COLOR_FG_626262  "\xEF\x9B\xB1"    // 247
#define COLOR_FG_6C6C6C  "\xEF\x9B\xB2"    // 248
#define COLOR_FG_767676  "\xEF\x9B\xB3"    // 249
#define COLOR_FG_808080  "\xEF\x9B\xB4"    // 250
#define COLOR_FG_8A8A8A  "\xEF\x9B\xB5"    // 251
#define COLOR_FG_949494  "\xEF\x9B\xB6"    // 252
#define COLOR_FG_9E9E9E  "\xEF\x9B\xB7"    // 253
#define COLOR_FG_A8A8A8  "\xEF\x9B\xB8"    // 254
#define COLOR_FG_B2B2B2  "\xEF\x9B\xB9"    // 255
#define COLOR_FG_BCBCBC  "\xEF\x9B\xBA"    // 256
#define COLOR_FG_C6C6C6  "\xEF\x9B\xBB"    // 257
#define COLOR_FG_D0D0D0  "\xEF\x9B\xBC"    // 258
#define COLOR_FG_DADADA  "\xEF\x9B\xBD"    // 259
#define COLOR_FG_E4E4E4  "\xEF\x9B\xBE"    // 260
#define COLOR_FG_EEEEEE  "\xEF\x9B\xBF"    // 261

#define COLOR_BG_BLACK   "\xEF\x9C\x80"    // 262
#define COLOR_BG_RED     "\xEF\x9C\x81"    // 263
#define COLOR_BG_GREEN   "\xEF\x9C\x82"    // 264
#define COLOR_BG_YELLOW  "\xEF\x9C\x83"    // 265
#define COLOR_BG_BLUE    "\xEF\x9C\x84"    // 266
#define COLOR_BG_MAGENTA "\xEF\x9C\x85"    // 267
#define COLOR_BG_CYAN    "\xEF\x9C\x86"    // 268
#define COLOR_BG_WHITE   "\xEF\x9C\x87"    // 269
#define COLOR_BG_555555  "\xEF\x9C\x88"    // 270
#define COLOR_BG_FF5555  "\xEF\x9C\x89"    // 271
#define COLOR_BG_55FF55  "\xEF\x9C\x8A"    // 272
#define COLOR_BG_FFFF55  "\xEF\x9C\x8B"    // 273
#define COLOR_BG_5555FF  "\xEF\x9C\x8C"    // 274
#define COLOR_BG_FF55FF  "\xEF\x9C\x8D"    // 275
#define COLOR_BG_55FFFF  "\xEF\x9C\x8E"    // 276
#define COLOR_BG_FFFFFF_1 "\xEF\x9C\x8F"   // 277
#define COLOR_BG_000000  "\xEF\x9C\x90"    // 278
#define COLOR_BG_00005F  "\xEF\x9C\x91"    // 279
#define COLOR_BG_000087  "\xEF\x9C\x92"    // 280
#define COLOR_BG_0000AF  "\xEF\x9C\x93"    // 281
#define COLOR_BG_0000D7  "\xEF\x9C\x94"    // 282
#define COLOR_BG_0000FF  "\xEF\x9C\x95"    // 283
#define COLOR_BG_005F00  "\xEF\x9C\x96"    // 284
#define COLOR_BG_005F5F  "\xEF\x9C\x97"    // 285
#define COLOR_BG_005F87  "\xEF\x9C\x98"    // 286
#define COLOR_BG_005FAF  "\xEF\x9C\x99"    // 287
#define COLOR_BG_005FD7  "\xEF\x9C\x9A"    // 288
#define COLOR_BG_005FFF  "\xEF\x9C\x9B"    // 289
#define COLOR_BG_008700  "\xEF\x9C\x9C"    // 290
#define COLOR_BG_00875F  "\xEF\x9C\x9D"    // 291
#define COLOR_BG_008785  "\xEF\x9C\x9E"    // 292
#define COLOR_BG_0087AF  "\xEF\x9C\x9F"    // 293
#define COLOR_BG_0087D7  "\xEF\x9C\xA0"    // 294
#define COLOR_BG_0087FF  "\xEF\x9C\xA1"    // 295
#define COLOR_BG_00AF00  "\xEF\x9C\xA2"    // 296
#define COLOR_BG_00AF5F  "\xEF\x9C\xA3"    // 297
#define COLOR_BG_00AF87  "\xEF\x9C\xA4"    // 298
#define COLOR_BG_00AFAF  "\xEF\x9C\xA5"    // 299
#define COLOR_BG_00AFD7  "\xEF\x9C\xA6"    // 300
#define COLOR_BG_00AFFF  "\xEF\x9C\xA7"    // 301
#define COLOR_BG_00D700  "\xEF\x9C\xA8"    // 302
#define COLOR_BG_00D75F  "\xEF\x9C\xA9"    // 303
#define COLOR_BG_00D787  "\xEF\x9C\xAA"    // 304
#define COLOR_BG_00D7AF  "\xEF\x9C\xAB"    // 305
#define COLOR_BG_00D7D7  "\xEF\x9C\xAC"    // 306
#define COLOR_BG_00D7FF  "\xEF\x9C\xAD"    // 307
#define COLOR_BG_00FF00  "\xEF\x9C\xAE"    // 308
#define COLOR_BG_00FF5A  "\xEF\x9C\xAF"    // 309
#define COLOR_BG_00FF87  "\xEF\x9C\xB0"    // 310
#define COLOR_BG_00FFAF  "\xEF\x9C\xB1"    // 311
#define COLOR_BG_00FFD7  "\xEF\x9C\xB2"    // 312
#define COLOR_BG_00FFFF  "\xEF\x9C\xB3"    // 313
#define COLOR_BG_5F0000  "\xEF\x9C\xB4"    // 314
#define COLOR_BG_5F005F  "\xEF\x9C\xB5"    // 315
#define COLOR_BG_5F0087  "\xEF\x9C\xB6"    // 316
#define COLOR_BG_5F00AF  "\xEF\x9C\xB7"    // 317
#define COLOR_BG_5F00D7  "\xEF\x9C\xB8"    // 318
#define COLOR_BG_5F00FF  "\xEF\x9C\xB9"    // 319
#define COLOR_BG_5F5F00  "\xEF\x9C\xBA"    // 320
#define COLOR_BG_5F5F5F  "\xEF\x9C\xBB"    // 321
#define COLOR_BG_5F5F87  "\xEF\x9C\xBC"    // 322
#define COLOR_BG_5F5FAF  "\xEF\x9C\xBD"    // 323
#define COLOR_BG_5F5FD7  "\xEF\x9C\xBE"    // 324
#define COLOR_BG_5F5FFF  "\xEF\x9C\xBF"    // 325
#define COLOR_BG_5F8700  "\xEF\x9D\x80"    // 326
#define COLOR_BG_5F875F  "\xEF\x9D\x81"    // 327
#define COLOR_BG_5F8787  "\xEF\x9D\x82"    // 328
#define COLOR_BG_5F87AF  "\xEF\x9D\x83"    // 329
#define COLOR_BG_5F87D7  "\xEF\x9D\x84"    // 330
#define COLOR_BG_5F87FF  "\xEF\x9D\x85"    // 331
#define COLOR_BG_5FAF00  "\xEF\x9D\x86"    // 332
#define COLOR_BG_5FAF5F  "\xEF\x9D\x87"    // 333
#define COLOR_BG_5FAF87  "\xEF\x9D\x88"    // 334
#define COLOR_BG_5FAFAF  "\xEF\x9D\x89"    // 335
#define COLOR_BG_5FAFD7  "\xEF\x9D\x8A"    // 336
#define COLOR_BG_5FAFFF  "\xEF\x9D\x8B"    // 337
#define COLOR_BG_5FD700  "\xEF\x9D\x8C"    // 338
#define COLOR_BG_5FD75F  "\xEF\x9D\x8D"    // 339
#define COLOR_BG_5FD787  "\xEF\x9D\x8E"    // 340
#define COLOR_BG_5FD7AF  "\xEF\x9D\x8F"    // 341
#define COLOR_BG_5FD7D7  "\xEF\x9D\x90"    // 342
#define COLOR_BG_5FD7FF  "\xEF\x9D\x91"    // 343
#define COLOR_BG_5FFF00  "\xEF\x9D\x92"    // 344
#define COLOR_BG_5FFF5F  "\xEF\x9D\x93"    // 345
#define COLOR_BG_5FFF87  "\xEF\x9D\x94"    // 346
#define COLOR_BG_5FFFAF  "\xEF\x9D\x95"    // 347
#define COLOR_BG_5FFFD7  "\xEF\x9D\x96"    // 348
#define COLOR_BG_5FFFFF  "\xEF\x9D\x97"    // 349
#define COLOR_BG_870000  "\xEF\x9D\x98"    // 350
#define COLOR_BG_87005F  "\xEF\x9D\x99"    // 351
#define COLOR_BG_870087  "\xEF\x9D\x9A"    // 352
#define COLOR_BG_8700AF  "\xEF\x9D\x9B"    // 353
#define COLOR_BG_8700D7  "\xEF\x9D\x9C"    // 354
#define COLOR_BG_8700FF  "\xEF\x9D\x9D"    // 355
#define COLOR_BG_875F00  "\xEF\x9D\x9E"    // 356
#define COLOR_BG_875F5F  "\xEF\x9D\x9F"    // 357
#define COLOR_BG_875F87  "\xEF\x9D\xA0"    // 358
#define COLOR_BG_875FAF  "\xEF\x9D\xA1"    // 359
#define COLOR_BG_875FD7  "\xEF\x9D\xA2"    // 360
#define COLOR_BG_875FFF  "\xEF\x9D\xA3"    // 361
#define COLOR_BG_878700  "\xEF\x9D\xA4"    // 362
#define COLOR_BG_87875F  "\xEF\x9D\xA5"    // 363
#define COLOR_BG_878787  "\xEF\x9D\xA6"    // 364
#define COLOR_BG_8787AF  "\xEF\x9D\xA7"    // 365
#define COLOR_BG_8787D7  "\xEF\x9D\xA8"    // 366
#define COLOR_BG_8787FF  "\xEF\x9D\xA9"    // 367
#define COLOR_BG_87AF00  "\xEF\x9D\xAA"    // 368
#define COLOR_BG_87AF5F  "\xEF\x9D\xAB"    // 369
#define COLOR_BG_87AF87  "\xEF\x9D\xAC"    // 370
#define COLOR_BG_87AFAF  "\xEF\x9D\xAD"    // 371
#define COLOR_BG_87AFD7  "\xEF\x9D\xAE"    // 372
#define COLOR_BG_87AFFF  "\xEF\x9D\xAF"    // 373
#define COLOR_BG_87D700  "\xEF\x9D\xB0"    // 374
#define COLOR_BG_87D75A  "\xEF\x9D\xB1"    // 375
#define COLOR_BG_87D787  "\xEF\x9D\xB2"    // 376
#define COLOR_BG_87D7AF  "\xEF\x9D\xB3"    // 377
#define COLOR_BG_87D7D7  "\xEF\x9D\xB4"    // 378
#define COLOR_BG_87D7FF  "\xEF\x9D\xB5"    // 379
#define COLOR_BG_87FF00  "\xEF\x9D\xB6"    // 380
#define COLOR_BG_87FF5F  "\xEF\x9D\xB7"    // 381
#define COLOR_BG_87FF87  "\xEF\x9D\xB8"    // 382
#define COLOR_BG_87FFAF  "\xEF\x9D\xB9"    // 383
#define COLOR_BG_87FFD7  "\xEF\x9D\xBA"    // 384
#define COLOR_BG_87FFFF  "\xEF\x9D\xBB"    // 385
#define COLOR_BG_AF0000  "\xEF\x9D\xBC"    // 386
#define COLOR_BG_AF005F  "\xEF\x9D\xBD"    // 387
#define COLOR_BG_AF0087  "\xEF\x9D\xBE"    // 388
#define COLOR_BG_AF00AF  "\xEF\x9D\xBF"    // 389
#define COLOR_BG_AF00D7  "\xEF\x9E\x80"    // 390
#define COLOR_BG_AF00FF  "\xEF\x9E\x81"    // 391
#define COLOR_BG_AF5F00  "\xEF\x9E\x82"    // 392
#define COLOR_BG_AF5F5F  "\xEF\x9E\x83"    // 393
#define COLOR_BG_AF5F87  "\xEF\x9E\x84"    // 394
#define COLOR_BG_AF5FAF  "\xEF\x9E\x85"    // 395
#define COLOR_BG_AF5FD7  "\xEF\x9E\x86"    // 396
#define COLOR_BG_AF5FFF  "\xEF\x9E\x87"    // 397
#define COLOR_BG_AF8700  "\xEF\x9E\x88"    // 398
#define COLOR_BG_AF875F  "\xEF\x9E\x89"    // 399
#define COLOR_BG_AF8787  "\xEF\x9E\x8A"    // 400
#define COLOR_BG_AF87AF  "\xEF\x9E\x8B"    // 401
#define COLOR_BG_AF87D7  "\xEF\x9E\x8C"    // 402
#define COLOR_BG_AF87FF  "\xEF\x9E\x8D"    // 403
#define COLOR_BG_AFAF00  "\xEF\x9E\x8E"    // 404
#define COLOR_BG_AFAF5F  "\xEF\x9E\x8F"    // 405
#define COLOR_BG_AFAF87  "\xEF\x9E\x90"    // 406
#define COLOR_BG_AFAFAF  "\xEF\x9E\x91"    // 407
#define COLOR_BG_AFAFD7  "\xEF\x9E\x92"    // 408
#define COLOR_BG_AFAFFF  "\xEF\x9E\x93"    // 409
#define COLOR_BG_AFD700  "\xEF\x9E\x94"    // 410
#define COLOR_BG_AFD75F  "\xEF\x9E\x95"    // 411
#define COLOR_BG_AFD787  "\xEF\x9E\x96"    // 412
#define COLOR_BG_AFD7AF  "\xEF\x9E\x97"    // 413
#define COLOR_BG_AFD7D7  "\xEF\x9E\x98"    // 414
#define COLOR_BG_AFD7FF  "\xEF\x9E\x99"    // 415
#define COLOR_BG_AFFF00  "\xEF\x9E\x9A"    // 416
#define COLOR_BG_AFFF5F  "\xEF\x9E\x9B"    // 417
#define COLOR_BG_AFFF87  "\xEF\x9E\x9C"    // 418
#define COLOR_BG_AFFFAF  "\xEF\x9E\x9D"    // 419
#define COLOR_BG_AFFFD7  "\xEF\x9E\x9E"    // 420
#define COLOR_BG_AFFFFF  "\xEF\x9E\x9F"    // 421
#define COLOR_BG_D70000  "\xEF\x9E\xA0"    // 422
#define COLOR_BG_D7005F  "\xEF\x9E\xA1"    // 423
#define COLOR_BG_D70087  "\xEF\x9E\xA2"    // 424
#define COLOR_BG_D700AF  "\xEF\x9E\xA3"    // 425
#define COLOR_BG_D700D7  "\xEF\x9E\xA4"    // 426
#define COLOR_BG_D700FF  "\xEF\x9E\xA5"    // 427
#define COLOR_BG_D75F00  "\xEF\x9E\xA6"    // 428
#define COLOR_BG_D75F5F  "\xEF\x9E\xA7"    // 429
#define COLOR_BG_D75F87  "\xEF\x9E\xA8"    // 430
#define COLOR_BG_D75FAF  "\xEF\x9E\xA9"    // 431
#define COLOR_BG_D75FD7  "\xEF\x9E\xAA"    // 432
#define COLOR_BG_D75FFF  "\xEF\x9E\xAB"    // 433
#define COLOR_BG_D78700  "\xEF\x9E\xAC"    // 434
#define COLOR_BG_D7875A  "\xEF\x9E\xAD"    // 435
#define COLOR_BG_D78787  "\xEF\x9E\xAE"    // 436
#define COLOR_BG_D787AF  "\xEF\x9E\xAF"    // 437
#define COLOR_BG_D787D7  "\xEF\x9E\xB0"    // 438
#define COLOR_BG_D787FF  "\xEF\x9E\xB1"    // 439
#define COLOR_BG_D7AF00  "\xEF\x9E\xB2"    // 440
#define COLOR_BG_D7AF5A  "\xEF\x9E\xB3"    // 441
#define COLOR_BG_D7AF87  "\xEF\x9E\xB4"    // 442
#define COLOR_BG_D7AFAF  "\xEF\x9E\xB5"    // 443
#define COLOR_BG_D7AFD7  "\xEF\x9E\xB6"    // 444
#define COLOR_BG_D7AFFF  "\xEF\x9E\xB7"    // 445
#define COLOR_BG_D7D700  "\xEF\x9E\xB8"    // 446
#define COLOR_BG_D7D75F  "\xEF\x9E\xB9"    // 447
#define COLOR_BG_D7D787  "\xEF\x9E\xBA"    // 448
#define COLOR_BG_D7D7AF  "\xEF\x9E\xBB"    // 449
#define COLOR_BG_D7D7D7  "\xEF\x9E\xBC"    // 450
#define COLOR_BG_D7D7FF  "\xEF\x9E\xBD"    // 451
#define COLOR_BG_D7FF00  "\xEF\x9E\xBE"    // 452
#define COLOR_BG_D7FF5F  "\xEF\x9E\xBF"    // 453
#define COLOR_BG_D7FF87  "\xEF\x9F\x80"    // 454
#define COLOR_BG_D7FFAF  "\xEF\x9F\x81"    // 455
#define COLOR_BG_D7FFD7  "\xEF\x9F\x82"    // 456
#define COLOR_BG_D7FFFF  "\xEF\x9F\x83"    // 457
#define COLOR_BG_FF0000  "\xEF\x9F\x84"    // 458
#define COLOR_BG_FF005F  "\xEF\x9F\x85"    // 459
#define COLOR_BG_FF0087  "\xEF\x9F\x86"    // 460
#define COLOR_BG_FF00AF  "\xEF\x9F\x87"    // 461
#define COLOR_BG_FF00D7  "\xEF\x9F\x88"    // 462
#define COLOR_BG_FF00FF  "\xEF\x9F\x89"    // 463
#define COLOR_BG_FF5F00  "\xEF\x9F\x8A"    // 464
#define COLOR_BG_FF5F5F  "\xEF\x9F\x8B"    // 465
#define COLOR_BG_FF5F87  "\xEF\x9F\x8C"    // 466
#define COLOR_BG_FF5FAF  "\xEF\x9F\x8D"    // 467
#define COLOR_BG_FF5FD7  "\xEF\x9F\x8E"    // 468
#define COLOR_BG_FF5FFF  "\xEF\x9F\x8F"    // 469
#define COLOR_BG_FF8700  "\xEF\x9F\x90"    // 470
#define COLOR_BG_FF875F  "\xEF\x9F\x91"    // 471
#define COLOR_BG_FF8787  "\xEF\x9F\x92"    // 472
#define COLOR_BG_FF87AF  "\xEF\x9F\x93"    // 473
#define COLOR_BG_FF87D7  "\xEF\x9F\x94"    // 474
#define COLOR_BG_FF87FF  "\xEF\x9F\x95"    // 475
#define COLOR_BG_FFAF00  "\xEF\x9F\x96"    // 476
#define COLOR_BG_FFAF5F  "\xEF\x9F\x97"    // 477
#define COLOR_BG_FFAF87  "\xEF\x9F\x98"    // 478
#define COLOR_BG_FFAFAF  "\xEF\x9F\x99"    // 479
#define COLOR_BG_FFAFD7  "\xEF\x9F\x9A"    // 480
#define COLOR_BG_FFAFFF  "\xEF\x9F\x9B"    // 481
#define COLOR_BG_FFD700  "\xEF\x9F\x9C"    // 482
#define COLOR_BG_FFD75F  "\xEF\x9F\x9D"    // 483
#define COLOR_BG_FFD787  "\xEF\x9F\x9E"    // 484
#define COLOR_BG_FFD7AF  "\xEF\x9F\x9F"    // 485
#define COLOR_BG_FFD7D7  "\xEF\x9F\xA0"    // 486
#define COLOR_BG_FFD7FF  "\xEF\x9F\xA1"    // 487
#define COLOR_BG_FFFF00  "\xEF\x9F\xA2"    // 488
#define COLOR_BG_FFFF5F  "\xEF\x9F\xA3"    // 489
#define COLOR_BG_FFFF87  "\xEF\x9F\xA4"    // 490
#define COLOR_BG_FFFFAF  "\xEF\x9F\xA5"    // 491
#define COLOR_BG_FFFFD7  "\xEF\x9F\xA6"    // 492
#define COLOR_BG_FFFFFF_2 "\xEF\x9F\xA7"   // 493
#define COLOR_BG_080808  "\xEF\x9F\xA8"    // 494
#define COLOR_BG_121212  "\xEF\x9F\xA9"    // 495
#define COLOR_BG_1C1C1C  "\xEF\x9F\xAA"    // 496
#define COLOR_BG_262626  "\xEF\x9F\xAB"    // 497
#define COLOR_BG_303030  "\xEF\x9F\xAC"    // 498
#define COLOR_BG_3A3A3A  "\xEF\x9F\xAD"    // 499
#define COLOR_BG_444444  "\xEF\x9F\xAE"    // 500
#define COLOR_BG_4E4E4E  "\xEF\x9F\xAF"    // 501
#define COLOR_BG_585858  "\xEF\x9F\xB0"    // 502
#define COLOR_BG_626262  "\xEF\x9F\xB1"    // 503
#define COLOR_BG_6C6C6C  "\xEF\x9F\xB2"    // 504
#define COLOR_BG_767676  "\xEF\x9F\xB3"    // 505
#define COLOR_BG_808080  "\xEF\x9F\xB4"    // 506
#define COLOR_BG_8A8A8A  "\xEF\x9F\xB5"    // 507
#define COLOR_BG_949494  "\xEF\x9F\xB6"    // 508
#define COLOR_BG_9E9E9E  "\xEF\x9F\xB7"    // 509
#define COLOR_BG_A8A8A8  "\xEF\x9F\xB8"    // 510
#define COLOR_BG_B2B2B2  "\xEF\x9F\xB9"    // 511
#define COLOR_BG_BCBCBC  "\xEF\x9F\xBA"    // 512
#define COLOR_BG_C6C6C6  "\xEF\x9F\xBB"    // 513
#define COLOR_BG_D0D0D0  "\xEF\x9F\xBC"    // 514
#define COLOR_BG_DADADA  "\xEF\x9F\xBD"    // 515
#define COLOR_BG_E4E4E4  "\xEF\x9F\xBE"    // 516
#define COLOR_BG_EEEEEE  "\xEF\x9F\xBF"    // 517

#define COLOR_INDEX_FG_WHITE    (COLOR_INDEX_FG + COLOR_INDEX_WHITE)

#define CS_FOREGROUND UINT64_C(0x0000000001FFFFFF)
#define CS_FOREGROUND_RED     UINT64_C(0x0000000000FF0000)
#define CS_FOREGROUND_GREEN   UINT64_C(0x000000000000FF00)
#define CS_FOREGROUND_BLUE    UINT64_C(0x00000000000000FF)
#define CS_FG_BLACK   UINT64_C(0x0000000001000000)    // FOREGROUND BLACK (0,0,0)
#define CS_FG_RED     UINT64_C(0x0000000001000001)    // FOREGROUND RED (187,0,0)
#define CS_FG_GREEN   UINT64_C(0x0000000001000002)    // FOREGROUND GREEN (0,187,0)
#define CS_FG_YELLOW  UINT64_C(0x0000000001000003)    // FOREGROUND YELLOW (187,187,0)
#define CS_FG_BLUE    UINT64_C(0x0000000001000004)    // FOREGROUND BLUE (0,0,187)
#define CS_FG_MAGENTA UINT64_C(0x0000000001000005)    // FOREGROUND MAGENTA (187,0,187)
#define CS_FG_CYAN    UINT64_C(0x0000000001000006)    // FOREGROUND CYAN (0,187,187)
#define CS_FG_WHITE   UINT64_C(0x0000000001000007)    // FOREGROUND WHITE (187,187,187)
#define CS_FG_INDEXED UINT64_C(0x0000000001000000)
#define CS_FG(x)      (CS_FG_INDEXED | static_cast<UINT64>(x))
#define CS_FG_FIELD(x) ((x) & UINT64_C(0x0000000000FFFFFF))
#define CS_FG_DEFAULT CS_FG(NUM_FG)
#define CS_BACKGROUND UINT64_C(0x01FFFFFF00000000)
#define CS_BACKGROUND_RED     UINT64_C(0x00FF000000000000)
#define CS_BACKGROUND_GREEN   UINT64_C(0x0000FF0000000000)
#define CS_BACKGROUND_BLUE    UINT64_C(0x000000FF00000000)
#define CS_BG_BLACK   UINT64_C(0x0100000000000000)    // BACKGROUND BLACK (0,0,0)
#define CS_BG_RED     UINT64_C(0x0100000100000000)    // BACKGROUND RED (187,0,0)
#define CS_BG_GREEN   UINT64_C(0x0100000200000000)    // BACKGROUND GREEN (0,187,0)
#define CS_BG_YELLOW  UINT64_C(0x0100000300000000)    // BACKGROUND YELLOW (187,187,0)
#define CS_BG_BLUE    UINT64_C(0x0100000400000000)    // BACKGROUND BLUE (0,0,187)
#define CS_BG_MAGENTA UINT64_C(0x0100000500000000)    // BACKGROUND MAGENTA (187,0,187)
#define CS_BG_CYAN    UINT64_C(0x0100000600000000)    // BACKGROUND CYAN (0,187,187)
#define CS_BG_WHITE   UINT64_C(0x0100000700000000)    // BACKGROUND WHITE (187,187,187)
#define CS_BG_INDEXED UINT64_C(0x0100000000000000)
#define CS_BG(x)      (CS_BG_INDEXED | (static_cast<UINT64>(x) << 32))
#define CS_BG_FIELD(x) (((x) & UINT64_C(0x00FFFFFF00000000)) >> 32)
#define CS_BG_DEFAULT CS_BG(NUM_BG)
#define CS_INTENSE    UINT64_C(0x0000000010000000)
#define CS_INVERSE    UINT64_C(0x0000000020000000)
#define CS_UNDERLINE  UINT64_C(0x0000000040000000)
#define CS_BLINK      UINT64_C(0x0000000080000000)
#define CS_ATTRS      UINT64_C(0x00000000F0000000)
#define CS_ALLBITS    UINT64_C(0x01FFFFFFF1FFFFFF)

#define CS_NORMAL     (CS_FG_DEFAULT|CS_BG_DEFAULT)
#define CS_NOBLEED    (CS_FG_WHITE|CS_BG_DEFAULT)

#define ANSI_ATTR_CMD 'm'

#define ANSI_NORMAL   "\033[0m"

#define ANSI_HILITE   "\033[1m"
#define ANSI_UNDER    "\033[4m"
#define ANSI_BLINK    "\033[5m"
#define ANSI_INVERSE  "\033[7m"

// Foreground colors.
//
#define ANSI_BLACK      "\033[30m"
#define ANSI_RED        "\033[31m"
#define ANSI_GREEN      "\033[32m"
#define ANSI_YELLOW     "\033[33m"
#define ANSI_BLUE       "\033[34m"
#define ANSI_MAGENTA    "\033[35m"
#define ANSI_CYAN       "\033[36m"
#define ANSI_WHITE      "\033[37m"
#define XTERM_FG(x)     "\033[38;5;" #x "m"

// Background colors.
//
#define ANSI_BBLACK     "\033[40m"
#define ANSI_BRED       "\033[41m"
#define ANSI_BGREEN     "\033[42m"
#define ANSI_BYELLOW    "\033[43m"
#define ANSI_BBLUE      "\033[44m"
#define ANSI_BMAGENTA   "\033[45m"
#define ANSI_BCYAN      "\033[46m"
#define ANSI_BWHITE     "\033[47m"
#define XTERM_BG(x)     "\033[48;5;" #x "m"


typedef struct
{
    ColorState  cs;
    ColorState  csMask;
    const char  pAnsi[12];
    size_t      nAnsi;
    const UTF8 *pUTF;
    size_t      nUTF;
    const UTF8 *pEscape;
    size_t      nEscape;
} MUX_COLOR_SET;

// XTERM_FG(0) through XTERM_FG(7) is equivalent to ANSI_BLACK...ANSI_WHITE.
// Even for 256-color-capable clients, the latter are used instead of the former.
// Similiarly for XTERM_BG(0) through XTERM_BG(0).
//
const MUX_COLOR_SET aColors[] =
{
    { 0,             0,             "",            0,                       T(""),               0, T(""),    0}, // COLOR_NOTCOLOR
    { CS_NORMAL,     CS_ALLBITS,    ANSI_NORMAL,   sizeof(ANSI_NORMAL)-1,   T(COLOR_RESET),      3, T("%xn"), 3}, // COLOR_INDEX_RESET
    { CS_INTENSE,    CS_INTENSE,    ANSI_HILITE,   sizeof(ANSI_HILITE)-1,   T(COLOR_INTENSE),    3, T("%xh"), 3}, // COLOR_INDEX_ATTR, COLOR_INDEX_INTENSE
    { CS_UNDERLINE,  CS_UNDERLINE,  ANSI_UNDER,    sizeof(ANSI_UNDER)-1,    T(COLOR_UNDERLINE),  3, T("%xu"), 3}, // COLOR_INDEX_UNDERLINE
    { CS_BLINK,      CS_BLINK,      ANSI_BLINK,    sizeof(ANSI_BLINK)-1,    T(COLOR_BLINK),      3, T("%xf"), 3}, // COLOR_INDEX_BLINK
    { CS_INVERSE,    CS_INVERSE,    ANSI_INVERSE,  sizeof(ANSI_INVERSE)-1,  T(COLOR_INVERSE),    3, T("%xi"), 3}, // COLOR_INDEX_INVERSE
    { CS_FG_BLACK,   CS_FOREGROUND, ANSI_BLACK,    sizeof(ANSI_BLACK)-1,    T(COLOR_FG_BLACK),   3, T("%xx"), 3}, // COLOR_INDEX_FG
    { CS_FG_RED,     CS_FOREGROUND, ANSI_RED,      sizeof(ANSI_RED)-1,      T(COLOR_FG_RED),     3, T("%xr"), 3},
    { CS_FG_GREEN,   CS_FOREGROUND, ANSI_GREEN,    sizeof(ANSI_GREEN)-1,    T(COLOR_FG_GREEN),   3, T("%xg"), 3},
    { CS_FG_YELLOW,  CS_FOREGROUND, ANSI_YELLOW,   sizeof(ANSI_YELLOW)-1,   T(COLOR_FG_YELLOW),  3, T("%xy"), 3},
    { CS_FG_BLUE,    CS_FOREGROUND, ANSI_BLUE,     sizeof(ANSI_BLUE)-1,     T(COLOR_FG_BLUE),    3, T("%xb"), 3},
    { CS_FG_MAGENTA, CS_FOREGROUND, ANSI_MAGENTA,  sizeof(ANSI_MAGENTA)-1,  T(COLOR_FG_MAGENTA), 3, T("%xm"), 3},
    { CS_FG_CYAN,    CS_FOREGROUND, ANSI_CYAN,     sizeof(ANSI_CYAN)-1,     T(COLOR_FG_CYAN),    3, T("%xc"), 3},
    { CS_FG_WHITE,   CS_FOREGROUND, ANSI_WHITE,    sizeof(ANSI_WHITE)-1,    T(COLOR_FG_WHITE),   3, T("%xw"), 3}, // COLOR_INDEX_FG_WHITE
    { CS_FG(  8),    CS_FOREGROUND, XTERM_FG(  8), sizeof(XTERM_FG(  8))-1, T(COLOR_FG_555555),  3, T("NU8"), 6},  // These eight are converted into something else.
    { CS_FG(  9),    CS_FOREGROUND, XTERM_FG(  9), sizeof(XTERM_FG(  9))-1, T(COLOR_FG_FF5555),  3, T("NU9"), 6},  // .
    { CS_FG( 10),    CS_FOREGROUND, XTERM_FG( 10), sizeof(XTERM_FG( 10))-1, T(COLOR_FG_55FF55),  3, T("NU10"), 6},  // .
    { CS_FG( 11),    CS_FOREGROUND, XTERM_FG( 11), sizeof(XTERM_FG( 11))-1, T(COLOR_FG_FFFF55),  3, T("NU11"), 6},  // .
    { CS_FG( 12),    CS_FOREGROUND, XTERM_FG( 12), sizeof(XTERM_FG( 12))-1, T(COLOR_FG_5555FF),  3, T("NU12"), 6},  // .
    { CS_FG( 13),    CS_FOREGROUND, XTERM_FG( 13), sizeof(XTERM_FG( 13))-1, T(COLOR_FG_FF55FF),  3, T("NU13"), 6},  // .
    { CS_FG( 14),    CS_FOREGROUND, XTERM_FG( 14), sizeof(XTERM_FG( 14))-1, T(COLOR_FG_55FFFF),  3, T("NU14"), 6},  // .
    { CS_FG( 15),    CS_FOREGROUND, XTERM_FG( 15), sizeof(XTERM_FG( 15))-1, T(COLOR_FG_FFFFFF_1),3, T("NU15"), 6},  // -
    { CS_FG( 16),    CS_FOREGROUND, XTERM_FG( 16), sizeof(XTERM_FG( 16))-1, T(COLOR_FG_000000),  3, T("%x<#000000>"), 11},
    { CS_FG( 17),    CS_FOREGROUND, XTERM_FG( 17), sizeof(XTERM_FG( 17))-1, T(COLOR_FG_00005F),  3, T("%x<#00005F>"), 11},
    { CS_FG( 18),    CS_FOREGROUND, XTERM_FG( 18), sizeof(XTERM_FG( 18))-1, T(COLOR_FG_000087),  3, T("%x<#000087>"), 11},
    { CS_FG( 19),    CS_FOREGROUND, XTERM_FG( 19), sizeof(XTERM_FG( 19))-1, T(COLOR_FG_0000AF),  3, T("%x<#0000AF>"), 11},
    { CS_FG( 20),    CS_FOREGROUND, XTERM_FG( 20), sizeof(XTERM_FG( 20))-1, T(COLOR_FG_0000D7),  3, T("%x<#0000D7>"), 11},
    { CS_FG( 21),    CS_FOREGROUND, XTERM_FG( 21), sizeof(XTERM_FG( 21))-1, T(COLOR_FG_0000FF),  3, T("%x<#0000FF>"), 11},
    { CS_FG( 22),    CS_FOREGROUND, XTERM_FG( 22), sizeof(XTERM_FG( 22))-1, T(COLOR_FG_005F00),  3, T("%x<#005F00>"), 11},
    { CS_FG( 23),    CS_FOREGROUND, XTERM_FG( 23), sizeof(XTERM_FG( 23))-1, T(COLOR_FG_005F5F),  3, T("%x<#005F5F>"), 11},
    { CS_FG( 24),    CS_FOREGROUND, XTERM_FG( 24), sizeof(XTERM_FG( 24))-1, T(COLOR_FG_005F87),  3, T("%x<#005F87>"), 11},
    { CS_FG( 25),    CS_FOREGROUND, XTERM_FG( 25), sizeof(XTERM_FG( 25))-1, T(COLOR_FG_005FAF),  3, T("%x<#005FAF>"), 11},
    { CS_FG( 26),    CS_FOREGROUND, XTERM_FG( 26), sizeof(XTERM_FG( 26))-1, T(COLOR_FG_005FD7),  3, T("%x<#005FD7>"), 11},
    { CS_FG( 27),    CS_FOREGROUND, XTERM_FG( 27), sizeof(XTERM_FG( 27))-1, T(COLOR_FG_005FFF),  3, T("%x<#005FFF>"), 11},
    { CS_FG( 28),    CS_FOREGROUND, XTERM_FG( 28), sizeof(XTERM_FG( 28))-1, T(COLOR_FG_008700),  3, T("%x<#008700>"), 11},
    { CS_FG( 29),    CS_FOREGROUND, XTERM_FG( 29), sizeof(XTERM_FG( 29))-1, T(COLOR_FG_00875F),  3, T("%x<#00875F>"), 11},
    { CS_FG( 30),    CS_FOREGROUND, XTERM_FG( 30), sizeof(XTERM_FG( 30))-1, T(COLOR_FG_008785),  3, T("%x<#008785>"), 11},
    { CS_FG( 31),    CS_FOREGROUND, XTERM_FG( 31), sizeof(XTERM_FG( 31))-1, T(COLOR_FG_0087AF),  3, T("%x<#0087AF>"), 11},
    { CS_FG( 32),    CS_FOREGROUND, XTERM_FG( 32), sizeof(XTERM_FG( 32))-1, T(COLOR_FG_0087D7),  3, T("%x<#0087D7>"), 11},
    { CS_FG( 33),    CS_FOREGROUND, XTERM_FG( 33), sizeof(XTERM_FG( 33))-1, T(COLOR_FG_0087FF),  3, T("%x<#0087FF>"), 11},
    { CS_FG( 34),    CS_FOREGROUND, XTERM_FG( 34), sizeof(XTERM_FG( 34))-1, T(COLOR_FG_00AF00),  3, T("%x<#00AF00>"), 11},
    { CS_FG( 35),    CS_FOREGROUND, XTERM_FG( 35), sizeof(XTERM_FG( 35))-1, T(COLOR_FG_00AF5F),  3, T("%x<#00AF5F>"), 11},
    { CS_FG( 36),    CS_FOREGROUND, XTERM_FG( 36), sizeof(XTERM_FG( 36))-1, T(COLOR_FG_00AF87),  3, T("%x<#00AF87>"), 11},
    { CS_FG( 37),    CS_FOREGROUND, XTERM_FG( 37), sizeof(XTERM_FG( 37))-1, T(COLOR_FG_00AFAF),  3, T("%x<#00AFAF>"), 11},
    { CS_FG( 38),    CS_FOREGROUND, XTERM_FG( 38), sizeof(XTERM_FG( 38))-1, T(COLOR_FG_00AFD7),  3, T("%x<#00AFD7>"), 11},
    { CS_FG( 39),    CS_FOREGROUND, XTERM_FG( 39), sizeof(XTERM_FG( 39))-1, T(COLOR_FG_00AFFF),  3, T("%x<#00AFFF>"), 11},
    { CS_FG( 40),    CS_FOREGROUND, XTERM_FG( 40), sizeof(XTERM_FG( 40))-1, T(COLOR_FG_00D700),  3, T("%x<#00D700>"), 11},
    { CS_FG( 41),    CS_FOREGROUND, XTERM_FG( 41), sizeof(XTERM_FG( 41))-1, T(COLOR_FG_00D75F),  3, T("%x<#00D75F>"), 11},
    { CS_FG( 42),    CS_FOREGROUND, XTERM_FG( 42), sizeof(XTERM_FG( 42))-1, T(COLOR_FG_00D787),  3, T("%x<#00D787>"), 11},
    { CS_FG( 43),    CS_FOREGROUND, XTERM_FG( 43), sizeof(XTERM_FG( 43))-1, T(COLOR_FG_00D7AF),  3, T("%x<#00D7AF>"), 11},
    { CS_FG( 44),    CS_FOREGROUND, XTERM_FG( 44), sizeof(XTERM_FG( 44))-1, T(COLOR_FG_00D7D7),  3, T("%x<#00D7D7>"), 11},
    { CS_FG( 45),    CS_FOREGROUND, XTERM_FG( 45), sizeof(XTERM_FG( 45))-1, T(COLOR_FG_00D7FF),  3, T("%x<#00D7FF>"), 11},
    { CS_FG( 46),    CS_FOREGROUND, XTERM_FG( 46), sizeof(XTERM_FG( 46))-1, T(COLOR_FG_00FF00),  3, T("%x<#00FF00>"), 11},
    { CS_FG( 47),    CS_FOREGROUND, XTERM_FG( 47), sizeof(XTERM_FG( 47))-1, T(COLOR_FG_00FF5A),  3, T("%x<#00FF5A>"), 11},
    { CS_FG( 48),    CS_FOREGROUND, XTERM_FG( 48), sizeof(XTERM_FG( 48))-1, T(COLOR_FG_00FF87),  3, T("%x<#00FF87>"), 11},
    { CS_FG( 49),    CS_FOREGROUND, XTERM_FG( 49), sizeof(XTERM_FG( 49))-1, T(COLOR_FG_00FFAF),  3, T("%x<#00FFAF>"), 11},
    { CS_FG( 50),    CS_FOREGROUND, XTERM_FG( 50), sizeof(XTERM_FG( 50))-1, T(COLOR_FG_00FFD7),  3, T("%x<#00FFD7>"), 11},
    { CS_FG( 51),    CS_FOREGROUND, XTERM_FG( 51), sizeof(XTERM_FG( 51))-1, T(COLOR_FG_00FFFF),  3, T("%x<#00FFFF>"), 11},
    { CS_FG( 52),    CS_FOREGROUND, XTERM_FG( 52), sizeof(XTERM_FG( 52))-1, T(COLOR_FG_5F0000),  3, T("%x<#5F0000>"), 11},
    { CS_FG( 53),    CS_FOREGROUND, XTERM_FG( 53), sizeof(XTERM_FG( 53))-1, T(COLOR_FG_5F005F),  3, T("%x<#5F005F>"), 11},
    { CS_FG( 54),    CS_FOREGROUND, XTERM_FG( 54), sizeof(XTERM_FG( 54))-1, T(COLOR_FG_5F0087),  3, T("%x<#5F0087>"), 11},
    { CS_FG( 55),    CS_FOREGROUND, XTERM_FG( 55), sizeof(XTERM_FG( 55))-1, T(COLOR_FG_5F00AF),  3, T("%x<#5F00AF>"), 11},
    { CS_FG( 56),    CS_FOREGROUND, XTERM_FG( 56), sizeof(XTERM_FG( 56))-1, T(COLOR_FG_5F00D7),  3, T("%x<#5F00D7>"), 11},
    { CS_FG( 57),    CS_FOREGROUND, XTERM_FG( 57), sizeof(XTERM_FG( 57))-1, T(COLOR_FG_5F00FF),  3, T("%x<#5F00FF>"), 11},
    { CS_FG( 58),    CS_FOREGROUND, XTERM_FG( 58), sizeof(XTERM_FG( 58))-1, T(COLOR_FG_5F5F00),  3, T("%x<#5F5F00>"), 11},
    { CS_FG( 59),    CS_FOREGROUND, XTERM_FG( 59), sizeof(XTERM_FG( 59))-1, T(COLOR_FG_5F5F5F),  3, T("%x<#5F5F5F>"), 11},
    { CS_FG( 60),    CS_FOREGROUND, XTERM_FG( 60), sizeof(XTERM_FG( 60))-1, T(COLOR_FG_5F5F87),  3, T("%x<#5F5F87>"), 11},
    { CS_FG( 61),    CS_FOREGROUND, XTERM_FG( 61), sizeof(XTERM_FG( 61))-1, T(COLOR_FG_5F5FAF),  3, T("%x<#5F5FAF>"), 11},
    { CS_FG( 62),    CS_FOREGROUND, XTERM_FG( 62), sizeof(XTERM_FG( 62))-1, T(COLOR_FG_5F5FD7),  3, T("%x<#5F5FD7>"), 11},
    { CS_FG( 63),    CS_FOREGROUND, XTERM_FG( 63), sizeof(XTERM_FG( 63))-1, T(COLOR_FG_5F5FFF),  3, T("%x<#5F5FFF>"), 11},
    { CS_FG( 64),    CS_FOREGROUND, XTERM_FG( 64), sizeof(XTERM_FG( 64))-1, T(COLOR_FG_5F8700),  3, T("%x<#5F8700>"), 11},
    { CS_FG( 65),    CS_FOREGROUND, XTERM_FG( 65), sizeof(XTERM_FG( 65))-1, T(COLOR_FG_5F875F),  3, T("%x<#5F875F>"), 11},
    { CS_FG( 66),    CS_FOREGROUND, XTERM_FG( 66), sizeof(XTERM_FG( 66))-1, T(COLOR_FG_5F8787),  3, T("%x<#5F8787>"), 11},
    { CS_FG( 67),    CS_FOREGROUND, XTERM_FG( 67), sizeof(XTERM_FG( 67))-1, T(COLOR_FG_5F87AF),  3, T("%x<#5F87AF>"), 11},
    { CS_FG( 68),    CS_FOREGROUND, XTERM_FG( 68), sizeof(XTERM_FG( 68))-1, T(COLOR_FG_5F87D7),  3, T("%x<#5F87D7>"), 11},
    { CS_FG( 69),    CS_FOREGROUND, XTERM_FG( 69), sizeof(XTERM_FG( 69))-1, T(COLOR_FG_5F87FF),  3, T("%x<#5F87FF>"), 11},
    { CS_FG( 70),    CS_FOREGROUND, XTERM_FG( 70), sizeof(XTERM_FG( 70))-1, T(COLOR_FG_5FAF00),  3, T("%x<#5FAF00>"), 11},
    { CS_FG( 71),    CS_FOREGROUND, XTERM_FG( 71), sizeof(XTERM_FG( 71))-1, T(COLOR_FG_5FAF5F),  3, T("%x<#5FAF5F>"), 11},
    { CS_FG( 72),    CS_FOREGROUND, XTERM_FG( 72), sizeof(XTERM_FG( 72))-1, T(COLOR_FG_5FAF87),  3, T("%x<#5FAF87>"), 11},
    { CS_FG( 73),    CS_FOREGROUND, XTERM_FG( 73), sizeof(XTERM_FG( 73))-1, T(COLOR_FG_5FAFAF),  3, T("%x<#5FAFAF>"), 11},
    { CS_FG( 74),    CS_FOREGROUND, XTERM_FG( 74), sizeof(XTERM_FG( 74))-1, T(COLOR_FG_5FAFD7),  3, T("%x<#5FAFD7>"), 11},
    { CS_FG( 75),    CS_FOREGROUND, XTERM_FG( 75), sizeof(XTERM_FG( 75))-1, T(COLOR_FG_5FAFFF),  3, T("%x<#5FAFFF>"), 11},
    { CS_FG( 76),    CS_FOREGROUND, XTERM_FG( 76), sizeof(XTERM_FG( 76))-1, T(COLOR_FG_5FD700),  3, T("%x<#5FD700>"), 11},
    { CS_FG( 77),    CS_FOREGROUND, XTERM_FG( 77), sizeof(XTERM_FG( 77))-1, T(COLOR_FG_5FD75F),  3, T("%x<#5FD75F>"), 11},
    { CS_FG( 78),    CS_FOREGROUND, XTERM_FG( 78), sizeof(XTERM_FG( 78))-1, T(COLOR_FG_5FD787),  3, T("%x<#5FD787>"), 11},
    { CS_FG( 79),    CS_FOREGROUND, XTERM_FG( 79), sizeof(XTERM_FG( 79))-1, T(COLOR_FG_5FD7AF),  3, T("%x<#5FD7AF>"), 11},
    { CS_FG( 80),    CS_FOREGROUND, XTERM_FG( 80), sizeof(XTERM_FG( 80))-1, T(COLOR_FG_5FD7D7),  3, T("%x<#5FD7D7>"), 11},
    { CS_FG( 81),    CS_FOREGROUND, XTERM_FG( 81), sizeof(XTERM_FG( 81))-1, T(COLOR_FG_5FD7FF),  3, T("%x<#5FD7FF>"), 11},
    { CS_FG( 82),    CS_FOREGROUND, XTERM_FG( 82), sizeof(XTERM_FG( 82))-1, T(COLOR_FG_5FFF00),  3, T("%x<#5FFF00>"), 11},
    { CS_FG( 83),    CS_FOREGROUND, XTERM_FG( 83), sizeof(XTERM_FG( 83))-1, T(COLOR_FG_5FFF5F),  3, T("%x<#5FFF5F>"), 11},
    { CS_FG( 84),    CS_FOREGROUND, XTERM_FG( 84), sizeof(XTERM_FG( 84))-1, T(COLOR_FG_5FFF87),  3, T("%x<#5FFF87>"), 11},
    { CS_FG( 85),    CS_FOREGROUND, XTERM_FG( 85), sizeof(XTERM_FG( 85))-1, T(COLOR_FG_5FFFAF),  3, T("%x<#5FFFAF>"), 11},
    { CS_FG( 86),    CS_FOREGROUND, XTERM_FG( 86), sizeof(XTERM_FG( 86))-1, T(COLOR_FG_5FFFD7),  3, T("%x<#5FFFD7>"), 11},
    { CS_FG( 87),    CS_FOREGROUND, XTERM_FG( 87), sizeof(XTERM_FG( 87))-1, T(COLOR_FG_5FFFFF),  3, T("%x<#5FFFFF>"), 11},
    { CS_FG( 88),    CS_FOREGROUND, XTERM_FG( 88), sizeof(XTERM_FG( 88))-1, T(COLOR_FG_870000),  3, T("%x<#870000>"), 11},
    { CS_FG( 89),    CS_FOREGROUND, XTERM_FG( 89), sizeof(XTERM_FG( 89))-1, T(COLOR_FG_87005F),  3, T("%x<#87005F>"), 11},
    { CS_FG( 90),    CS_FOREGROUND, XTERM_FG( 90), sizeof(XTERM_FG( 90))-1, T(COLOR_FG_870087),  3, T("%x<#870087>"), 11},
    { CS_FG( 91),    CS_FOREGROUND, XTERM_FG( 91), sizeof(XTERM_FG( 91))-1, T(COLOR_FG_8700AF),  3, T("%x<#8700AF>"), 11},
    { CS_FG( 92),    CS_FOREGROUND, XTERM_FG( 92), sizeof(XTERM_FG( 92))-1, T(COLOR_FG_8700D7),  3, T("%x<#8700D7>"), 11},
    { CS_FG( 93),    CS_FOREGROUND, XTERM_FG( 93), sizeof(XTERM_FG( 93))-1, T(COLOR_FG_8700FF),  3, T("%x<#8700FF>"), 11},
    { CS_FG( 94),    CS_FOREGROUND, XTERM_FG( 94), sizeof(XTERM_FG( 94))-1, T(COLOR_FG_875F00),  3, T("%x<#875F00>"), 11},
    { CS_FG( 95),    CS_FOREGROUND, XTERM_FG( 95), sizeof(XTERM_FG( 95))-1, T(COLOR_FG_875F5F),  3, T("%x<#875F5F>"), 11},
    { CS_FG( 96),    CS_FOREGROUND, XTERM_FG( 96), sizeof(XTERM_FG( 96))-1, T(COLOR_FG_875F87),  3, T("%x<#875F87>"), 11},
    { CS_FG( 97),    CS_FOREGROUND, XTERM_FG( 97), sizeof(XTERM_FG( 97))-1, T(COLOR_FG_875FAF),  3, T("%x<#875FAF>"), 11},
    { CS_FG( 98),    CS_FOREGROUND, XTERM_FG( 98), sizeof(XTERM_FG( 98))-1, T(COLOR_FG_875FD7),  3, T("%x<#875FD7>"), 11},
    { CS_FG( 99),    CS_FOREGROUND, XTERM_FG( 99), sizeof(XTERM_FG( 99))-1, T(COLOR_FG_875FFF),  3, T("%x<#875FFF>"), 11},
    { CS_FG(100),    CS_FOREGROUND, XTERM_FG(100), sizeof(XTERM_FG(100))-1, T(COLOR_FG_878700),  3, T("%x<#878700>"), 11},
    { CS_FG(101),    CS_FOREGROUND, XTERM_FG(101), sizeof(XTERM_FG(101))-1, T(COLOR_FG_87875F),  3, T("%x<#87875F>"), 11},
    { CS_FG(102),    CS_FOREGROUND, XTERM_FG(102), sizeof(XTERM_FG(102))-1, T(COLOR_FG_878787),  3, T("%x<#878787>"), 11},
    { CS_FG(103),    CS_FOREGROUND, XTERM_FG(103), sizeof(XTERM_FG(103))-1, T(COLOR_FG_8787AF),  3, T("%x<#8787AF>"), 11},
    { CS_FG(104),    CS_FOREGROUND, XTERM_FG(104), sizeof(XTERM_FG(104))-1, T(COLOR_FG_8787D7),  3, T("%x<#8787D7>"), 11},
    { CS_FG(105),    CS_FOREGROUND, XTERM_FG(105), sizeof(XTERM_FG(105))-1, T(COLOR_FG_8787FF),  3, T("%x<#8787FF>"), 11},
    { CS_FG(106),    CS_FOREGROUND, XTERM_FG(106), sizeof(XTERM_FG(106))-1, T(COLOR_FG_87AF00),  3, T("%x<#87AF00>"), 11},
    { CS_FG(107),    CS_FOREGROUND, XTERM_FG(107), sizeof(XTERM_FG(107))-1, T(COLOR_FG_87AF5F),  3, T("%x<#87AF5F>"), 11},
    { CS_FG(108),    CS_FOREGROUND, XTERM_FG(108), sizeof(XTERM_FG(108))-1, T(COLOR_FG_87AF87),  3, T("%x<#87AF87>"), 11},
    { CS_FG(109),    CS_FOREGROUND, XTERM_FG(109), sizeof(XTERM_FG(109))-1, T(COLOR_FG_87AFAF),  3, T("%x<#87AFAF>"), 11},
    { CS_FG(110),    CS_FOREGROUND, XTERM_FG(110), sizeof(XTERM_FG(110))-1, T(COLOR_FG_87AFD7),  3, T("%x<#87AFD7>"), 11},
    { CS_FG(111),    CS_FOREGROUND, XTERM_FG(111), sizeof(XTERM_FG(111))-1, T(COLOR_FG_87AFFF),  3, T("%x<#87AFFF>"), 11},
    { CS_FG(112),    CS_FOREGROUND, XTERM_FG(112), sizeof(XTERM_FG(112))-1, T(COLOR_FG_87D700),  3, T("%x<#87D700>"), 11},
    { CS_FG(113),    CS_FOREGROUND, XTERM_FG(113), sizeof(XTERM_FG(113))-1, T(COLOR_FG_87D75A),  3, T("%x<#87D75A>"), 11},
    { CS_FG(114),    CS_FOREGROUND, XTERM_FG(114), sizeof(XTERM_FG(114))-1, T(COLOR_FG_87D787),  3, T("%x<#87D787>"), 11},
    { CS_FG(115),    CS_FOREGROUND, XTERM_FG(115), sizeof(XTERM_FG(115))-1, T(COLOR_FG_87D7AF),  3, T("%x<#87D7AF>"), 11},
    { CS_FG(116),    CS_FOREGROUND, XTERM_FG(116), sizeof(XTERM_FG(116))-1, T(COLOR_FG_87D7D7),  3, T("%x<#87D7D7>"), 11},
    { CS_FG(117),    CS_FOREGROUND, XTERM_FG(117), sizeof(XTERM_FG(117))-1, T(COLOR_FG_87D7FF),  3, T("%x<#87D7FF>"), 11},
    { CS_FG(118),    CS_FOREGROUND, XTERM_FG(118), sizeof(XTERM_FG(118))-1, T(COLOR_FG_87FF00),  3, T("%x<#87FF00>"), 11},
    { CS_FG(119),    CS_FOREGROUND, XTERM_FG(119), sizeof(XTERM_FG(119))-1, T(COLOR_FG_87FF5F),  3, T("%x<#87FF5F>"), 11},
    { CS_FG(120),    CS_FOREGROUND, XTERM_FG(120), sizeof(XTERM_FG(120))-1, T(COLOR_FG_87FF87),  3, T("%x<#87FF87>"), 11},
    { CS_FG(121),    CS_FOREGROUND, XTERM_FG(121), sizeof(XTERM_FG(121))-1, T(COLOR_FG_87FFAF),  3, T("%x<#87FFAF>"), 11},
    { CS_FG(122),    CS_FOREGROUND, XTERM_FG(122), sizeof(XTERM_FG(122))-1, T(COLOR_FG_87FFD7),  3, T("%x<#87FFD7>"), 11},
    { CS_FG(123),    CS_FOREGROUND, XTERM_FG(123), sizeof(XTERM_FG(123))-1, T(COLOR_FG_87FFFF),  3, T("%x<#87FFFF>"), 11},
    { CS_FG(124),    CS_FOREGROUND, XTERM_FG(124), sizeof(XTERM_FG(124))-1, T(COLOR_FG_AF0000),  3, T("%x<#AF0000>"), 11},
    { CS_FG(125),    CS_FOREGROUND, XTERM_FG(125), sizeof(XTERM_FG(125))-1, T(COLOR_FG_AF005F),  3, T("%x<#AF005F>"), 11},
    { CS_FG(126),    CS_FOREGROUND, XTERM_FG(126), sizeof(XTERM_FG(126))-1, T(COLOR_FG_AF0087),  3, T("%x<#AF0087>"), 11},
    { CS_FG(127),    CS_FOREGROUND, XTERM_FG(127), sizeof(XTERM_FG(127))-1, T(COLOR_FG_AF00AF),  3, T("%x<#AF00AF>"), 11},
    { CS_FG(128),    CS_FOREGROUND, XTERM_FG(128), sizeof(XTERM_FG(128))-1, T(COLOR_FG_AF00D7),  3, T("%x<#AF00D7>"), 11},
    { CS_FG(129),    CS_FOREGROUND, XTERM_FG(129), sizeof(XTERM_FG(129))-1, T(COLOR_FG_AF00FF),  3, T("%x<#AF00FF>"), 11},
    { CS_FG(130),    CS_FOREGROUND, XTERM_FG(130), sizeof(XTERM_FG(130))-1, T(COLOR_FG_AF5F00),  3, T("%x<#AF5F00>"), 11},
    { CS_FG(131),    CS_FOREGROUND, XTERM_FG(131), sizeof(XTERM_FG(131))-1, T(COLOR_FG_AF5F5F),  3, T("%x<#AF5F5F>"), 11},
    { CS_FG(132),    CS_FOREGROUND, XTERM_FG(132), sizeof(XTERM_FG(132))-1, T(COLOR_FG_AF5F87),  3, T("%x<#AF5F87>"), 11},
    { CS_FG(133),    CS_FOREGROUND, XTERM_FG(133), sizeof(XTERM_FG(133))-1, T(COLOR_FG_AF5FAF),  3, T("%x<#AF5FAF>"), 11},
    { CS_FG(134),    CS_FOREGROUND, XTERM_FG(134), sizeof(XTERM_FG(134))-1, T(COLOR_FG_AF5FD7),  3, T("%x<#AF5FD7>"), 11},
    { CS_FG(135),    CS_FOREGROUND, XTERM_FG(135), sizeof(XTERM_FG(135))-1, T(COLOR_FG_AF5FFF),  3, T("%x<#AF5FFF>"), 11},
    { CS_FG(136),    CS_FOREGROUND, XTERM_FG(136), sizeof(XTERM_FG(136))-1, T(COLOR_FG_AF8700),  3, T("%x<#AF8700>"), 11},
    { CS_FG(137),    CS_FOREGROUND, XTERM_FG(137), sizeof(XTERM_FG(137))-1, T(COLOR_FG_AF875F),  3, T("%x<#AF875F>"), 11},
    { CS_FG(138),    CS_FOREGROUND, XTERM_FG(138), sizeof(XTERM_FG(138))-1, T(COLOR_FG_AF8787),  3, T("%x<#AF8787>"), 11},
    { CS_FG(139),    CS_FOREGROUND, XTERM_FG(139), sizeof(XTERM_FG(139))-1, T(COLOR_FG_AF87AF),  3, T("%x<#AF87AF>"), 11},
    { CS_FG(140),    CS_FOREGROUND, XTERM_FG(140), sizeof(XTERM_FG(140))-1, T(COLOR_FG_AF87D7),  3, T("%x<#AF87D7>"), 11},
    { CS_FG(141),    CS_FOREGROUND, XTERM_FG(141), sizeof(XTERM_FG(141))-1, T(COLOR_FG_AF87FF),  3, T("%x<#AF87FF>"), 11},
    { CS_FG(142),    CS_FOREGROUND, XTERM_FG(142), sizeof(XTERM_FG(142))-1, T(COLOR_FG_AFAF00),  3, T("%x<#AFAF00>"), 11},
    { CS_FG(143),    CS_FOREGROUND, XTERM_FG(143), sizeof(XTERM_FG(143))-1, T(COLOR_FG_AFAF5F),  3, T("%x<#AFAF5F>"), 11},
    { CS_FG(144),    CS_FOREGROUND, XTERM_FG(144), sizeof(XTERM_FG(144))-1, T(COLOR_FG_AFAF87),  3, T("%x<#AFAF87>"), 11},
    { CS_FG(145),    CS_FOREGROUND, XTERM_FG(145), sizeof(XTERM_FG(145))-1, T(COLOR_FG_AFAFAF),  3, T("%x<#AFAFAF>"), 11},
    { CS_FG(146),    CS_FOREGROUND, XTERM_FG(146), sizeof(XTERM_FG(146))-1, T(COLOR_FG_AFAFD7),  3, T("%x<#AFAFD7>"), 11},
    { CS_FG(147),    CS_FOREGROUND, XTERM_FG(147), sizeof(XTERM_FG(147))-1, T(COLOR_FG_AFAFFF),  3, T("%x<#AFAFFF>"), 11},
    { CS_FG(148),    CS_FOREGROUND, XTERM_FG(148), sizeof(XTERM_FG(148))-1, T(COLOR_FG_AFD700),  3, T("%x<#AFD700>"), 11},
    { CS_FG(149),    CS_FOREGROUND, XTERM_FG(149), sizeof(XTERM_FG(149))-1, T(COLOR_FG_AFD75F),  3, T("%x<#AFD75F>"), 11},
    { CS_FG(150),    CS_FOREGROUND, XTERM_FG(150), sizeof(XTERM_FG(150))-1, T(COLOR_FG_AFD787),  3, T("%x<#AFD787>"), 11},
    { CS_FG(151),    CS_FOREGROUND, XTERM_FG(151), sizeof(XTERM_FG(151))-1, T(COLOR_FG_AFD7AF),  3, T("%x<#AFD7AF>"), 11},
    { CS_FG(152),    CS_FOREGROUND, XTERM_FG(152), sizeof(XTERM_FG(152))-1, T(COLOR_FG_AFD7D7),  3, T("%x<#AFD7D7>"), 11},
    { CS_FG(153),    CS_FOREGROUND, XTERM_FG(153), sizeof(XTERM_FG(153))-1, T(COLOR_FG_AFD7FF),  3, T("%x<#AFD7FF>"), 11},
    { CS_FG(154),    CS_FOREGROUND, XTERM_FG(154), sizeof(XTERM_FG(154))-1, T(COLOR_FG_AFFF00),  3, T("%x<#AFFF00>"), 11},
    { CS_FG(155),    CS_FOREGROUND, XTERM_FG(155), sizeof(XTERM_FG(155))-1, T(COLOR_FG_AFFF5F),  3, T("%x<#AFFF5F>"), 11},
    { CS_FG(156),    CS_FOREGROUND, XTERM_FG(156), sizeof(XTERM_FG(156))-1, T(COLOR_FG_AFFF87),  3, T("%x<#AFFF87>"), 11},
    { CS_FG(157),    CS_FOREGROUND, XTERM_FG(157), sizeof(XTERM_FG(157))-1, T(COLOR_FG_AFFFAF),  3, T("%x<#AFFFAF>"), 11},
    { CS_FG(158),    CS_FOREGROUND, XTERM_FG(158), sizeof(XTERM_FG(158))-1, T(COLOR_FG_AFFFD7),  3, T("%x<#AFFFD7>"), 11},
    { CS_FG(159),    CS_FOREGROUND, XTERM_FG(159), sizeof(XTERM_FG(159))-1, T(COLOR_FG_AFFFFF),  3, T("%x<#AFFFFF>"), 11},
    { CS_FG(160),    CS_FOREGROUND, XTERM_FG(160), sizeof(XTERM_FG(160))-1, T(COLOR_FG_D70000),  3, T("%x<#D70000>"), 11},
    { CS_FG(161),    CS_FOREGROUND, XTERM_FG(161), sizeof(XTERM_FG(161))-1, T(COLOR_FG_D7005F),  3, T("%x<#D7005F>"), 11},
    { CS_FG(162),    CS_FOREGROUND, XTERM_FG(162), sizeof(XTERM_FG(162))-1, T(COLOR_FG_D70087),  3, T("%x<#D70087>"), 11},
    { CS_FG(163),    CS_FOREGROUND, XTERM_FG(163), sizeof(XTERM_FG(163))-1, T(COLOR_FG_D700AF),  3, T("%x<#D700AF>"), 11},
    { CS_FG(164),    CS_FOREGROUND, XTERM_FG(164), sizeof(XTERM_FG(164))-1, T(COLOR_FG_D700D7),  3, T("%x<#D700D7>"), 11},
    { CS_FG(165),    CS_FOREGROUND, XTERM_FG(165), sizeof(XTERM_FG(165))-1, T(COLOR_FG_D700FF),  3, T("%x<#D700FF>"), 11},
    { CS_FG(166),    CS_FOREGROUND, XTERM_FG(166), sizeof(XTERM_FG(166))-1, T(COLOR_FG_D75F00),  3, T("%x<#D75F00>"), 11},
    { CS_FG(167),    CS_FOREGROUND, XTERM_FG(167), sizeof(XTERM_FG(167))-1, T(COLOR_FG_D75F5F),  3, T("%x<#D75F5F>"), 11},
    { CS_FG(168),    CS_FOREGROUND, XTERM_FG(168), sizeof(XTERM_FG(168))-1, T(COLOR_FG_D75F87),  3, T("%x<#D75F87>"), 11},
    { CS_FG(169),    CS_FOREGROUND, XTERM_FG(169), sizeof(XTERM_FG(169))-1, T(COLOR_FG_D75FAF),  3, T("%x<#D75FAF>"), 11},
    { CS_FG(170),    CS_FOREGROUND, XTERM_FG(170), sizeof(XTERM_FG(170))-1, T(COLOR_FG_D75FD7),  3, T("%x<#D75FD7>"), 11},
    { CS_FG(171),    CS_FOREGROUND, XTERM_FG(171), sizeof(XTERM_FG(171))-1, T(COLOR_FG_D75FFF),  3, T("%x<#D75FFF>"), 11},
    { CS_FG(172),    CS_FOREGROUND, XTERM_FG(172), sizeof(XTERM_FG(172))-1, T(COLOR_FG_D78700),  3, T("%x<#D78700>"), 11},
    { CS_FG(173),    CS_FOREGROUND, XTERM_FG(173), sizeof(XTERM_FG(173))-1, T(COLOR_FG_D7875A),  3, T("%x<#D7875A>"), 11},
    { CS_FG(174),    CS_FOREGROUND, XTERM_FG(174), sizeof(XTERM_FG(174))-1, T(COLOR_FG_D78787),  3, T("%x<#D78787>"), 11},
    { CS_FG(175),    CS_FOREGROUND, XTERM_FG(175), sizeof(XTERM_FG(175))-1, T(COLOR_FG_D787AF),  3, T("%x<#D787AF>"), 11},
    { CS_FG(176),    CS_FOREGROUND, XTERM_FG(176), sizeof(XTERM_FG(176))-1, T(COLOR_FG_D787D7),  3, T("%x<#D787D7>"), 11},
    { CS_FG(177),    CS_FOREGROUND, XTERM_FG(177), sizeof(XTERM_FG(177))-1, T(COLOR_FG_D787FF),  3, T("%x<#D787FF>"), 11},
    { CS_FG(178),    CS_FOREGROUND, XTERM_FG(178), sizeof(XTERM_FG(178))-1, T(COLOR_FG_D7AF00),  3, T("%x<#D7AF00>"), 11},
    { CS_FG(179),    CS_FOREGROUND, XTERM_FG(179), sizeof(XTERM_FG(179))-1, T(COLOR_FG_D7AF5A),  3, T("%x<#D7AF5A>"), 11},
    { CS_FG(180),    CS_FOREGROUND, XTERM_FG(180), sizeof(XTERM_FG(180))-1, T(COLOR_FG_D7AF87),  3, T("%x<#D7AF87>"), 11},
    { CS_FG(181),    CS_FOREGROUND, XTERM_FG(181), sizeof(XTERM_FG(181))-1, T(COLOR_FG_D7AFAF),  3, T("%x<#D7AFAF>"), 11},
    { CS_FG(182),    CS_FOREGROUND, XTERM_FG(182), sizeof(XTERM_FG(182))-1, T(COLOR_FG_D7AFD7),  3, T("%x<#D7AFD7>"), 11},
    { CS_FG(183),    CS_FOREGROUND, XTERM_FG(183), sizeof(XTERM_FG(183))-1, T(COLOR_FG_D7AFFF),  3, T("%x<#D7AFFF>"), 11},
    { CS_FG(184),    CS_FOREGROUND, XTERM_FG(184), sizeof(XTERM_FG(184))-1, T(COLOR_FG_D7D700),  3, T("%x<#D7D700>"), 11},
    { CS_FG(185),    CS_FOREGROUND, XTERM_FG(185), sizeof(XTERM_FG(185))-1, T(COLOR_FG_D7D75F),  3, T("%x<#D7D75F>"), 11},
    { CS_FG(186),    CS_FOREGROUND, XTERM_FG(186), sizeof(XTERM_FG(186))-1, T(COLOR_FG_D7D787),  3, T("%x<#D7D787>"), 11},
    { CS_FG(187),    CS_FOREGROUND, XTERM_FG(187), sizeof(XTERM_FG(187))-1, T(COLOR_FG_D7D7AF),  3, T("%x<#D7D7AF>"), 11},
    { CS_FG(188),    CS_FOREGROUND, XTERM_FG(188), sizeof(XTERM_FG(188))-1, T(COLOR_FG_D7D7D7),  3, T("%x<#D7D7D7>"), 11},
    { CS_FG(189),    CS_FOREGROUND, XTERM_FG(189), sizeof(XTERM_FG(189))-1, T(COLOR_FG_D7D7FF),  3, T("%x<#D7D7FF>"), 11},
    { CS_FG(190),    CS_FOREGROUND, XTERM_FG(190), sizeof(XTERM_FG(190))-1, T(COLOR_FG_D7FF00),  3, T("%x<#D7FF00>"), 11},
    { CS_FG(191),    CS_FOREGROUND, XTERM_FG(191), sizeof(XTERM_FG(191))-1, T(COLOR_FG_D7FF5F),  3, T("%x<#D7FF5F>"), 11},
    { CS_FG(192),    CS_FOREGROUND, XTERM_FG(192), sizeof(XTERM_FG(192))-1, T(COLOR_FG_D7FF87),  3, T("%x<#D7FF87>"), 11},
    { CS_FG(193),    CS_FOREGROUND, XTERM_FG(193), sizeof(XTERM_FG(193))-1, T(COLOR_FG_D7FFAF),  3, T("%x<#D7FFAF>"), 11},
    { CS_FG(194),    CS_FOREGROUND, XTERM_FG(194), sizeof(XTERM_FG(194))-1, T(COLOR_FG_D7FFD7),  3, T("%x<#D7FFD7>"), 11},
    { CS_FG(195),    CS_FOREGROUND, XTERM_FG(195), sizeof(XTERM_FG(195))-1, T(COLOR_FG_D7FFFF),  3, T("%x<#D7FFFF>"), 11},
    { CS_FG(196),    CS_FOREGROUND, XTERM_FG(196), sizeof(XTERM_FG(196))-1, T(COLOR_FG_FF0000),  3, T("%x<#FF0000>"), 11},
    { CS_FG(197),    CS_FOREGROUND, XTERM_FG(197), sizeof(XTERM_FG(197))-1, T(COLOR_FG_FF005F),  3, T("%x<#FF005F>"), 11},
    { CS_FG(198),    CS_FOREGROUND, XTERM_FG(198), sizeof(XTERM_FG(198))-1, T(COLOR_FG_FF0087),  3, T("%x<#FF0087>"), 11},
    { CS_FG(199),    CS_FOREGROUND, XTERM_FG(199), sizeof(XTERM_FG(199))-1, T(COLOR_FG_FF00AF),  3, T("%x<#FF00AF>"), 11},
    { CS_FG(200),    CS_FOREGROUND, XTERM_FG(200), sizeof(XTERM_FG(200))-1, T(COLOR_FG_FF00D7),  3, T("%x<#FF00D7>"), 11},
    { CS_FG(201),    CS_FOREGROUND, XTERM_FG(201), sizeof(XTERM_FG(201))-1, T(COLOR_FG_FF00FF),  3, T("%x<#FF00FF>"), 11},
    { CS_FG(202),    CS_FOREGROUND, XTERM_FG(202), sizeof(XTERM_FG(202))-1, T(COLOR_FG_FF5F00),  3, T("%x<#FF5F00>"), 11},
    { CS_FG(203),    CS_FOREGROUND, XTERM_FG(203), sizeof(XTERM_FG(203))-1, T(COLOR_FG_FF5F5F),  3, T("%x<#FF5F5F>"), 11},
    { CS_FG(204),    CS_FOREGROUND, XTERM_FG(204), sizeof(XTERM_FG(204))-1, T(COLOR_FG_FF5F87),  3, T("%x<#FF5F87>"), 11},
    { CS_FG(205),    CS_FOREGROUND, XTERM_FG(205), sizeof(XTERM_FG(205))-1, T(COLOR_FG_FF5FAF),  3, T("%x<#FF5FAF>"), 11},
    { CS_FG(206),    CS_FOREGROUND, XTERM_FG(206), sizeof(XTERM_FG(206))-1, T(COLOR_FG_FF5FD7),  3, T("%x<#FF5FD7>"), 11},
    { CS_FG(207),    CS_FOREGROUND, XTERM_FG(207), sizeof(XTERM_FG(207))-1, T(COLOR_FG_FF5FFF),  3, T("%x<#FF5FFF>"), 11},
    { CS_FG(208),    CS_FOREGROUND, XTERM_FG(208), sizeof(XTERM_FG(208))-1, T(COLOR_FG_FF8700),  3, T("%x<#FF8700>"), 11},
    { CS_FG(209),    CS_FOREGROUND, XTERM_FG(209), sizeof(XTERM_FG(209))-1, T(COLOR_FG_FF875F),  3, T("%x<#FF875F>"), 11},
    { CS_FG(210),    CS_FOREGROUND, XTERM_FG(210), sizeof(XTERM_FG(210))-1, T(COLOR_FG_FF8787),  3, T("%x<#FF8787>"), 11},
    { CS_FG(211),    CS_FOREGROUND, XTERM_FG(211), sizeof(XTERM_FG(211))-1, T(COLOR_FG_FF87AF),  3, T("%x<#FF87AF>"), 11},
    { CS_FG(212),    CS_FOREGROUND, XTERM_FG(212), sizeof(XTERM_FG(212))-1, T(COLOR_FG_FF87D7),  3, T("%x<#FF87D7>"), 11},
    { CS_FG(213),    CS_FOREGROUND, XTERM_FG(213), sizeof(XTERM_FG(213))-1, T(COLOR_FG_FF87FF),  3, T("%x<#FF87FF>"), 11},
    { CS_FG(214),    CS_FOREGROUND, XTERM_FG(214), sizeof(XTERM_FG(214))-1, T(COLOR_FG_FFAF00),  3, T("%x<#FFAF00>"), 11},
    { CS_FG(215),    CS_FOREGROUND, XTERM_FG(215), sizeof(XTERM_FG(215))-1, T(COLOR_FG_FFAF5F),  3, T("%x<#FFAF5F>"), 11},
    { CS_FG(216),    CS_FOREGROUND, XTERM_FG(216), sizeof(XTERM_FG(216))-1, T(COLOR_FG_FFAF87),  3, T("%x<#FFAF87>"), 11},
    { CS_FG(217),    CS_FOREGROUND, XTERM_FG(217), sizeof(XTERM_FG(217))-1, T(COLOR_FG_FFAFAF),  3, T("%x<#FFAFAF>"), 11},
    { CS_FG(218),    CS_FOREGROUND, XTERM_FG(218), sizeof(XTERM_FG(218))-1, T(COLOR_FG_FFAFD7),  3, T("%x<#FFAFD7>"), 11},
    { CS_FG(219),    CS_FOREGROUND, XTERM_FG(219), sizeof(XTERM_FG(219))-1, T(COLOR_FG_FFAFFF),  3, T("%x<#FFAFFF>"), 11},
    { CS_FG(220),    CS_FOREGROUND, XTERM_FG(220), sizeof(XTERM_FG(220))-1, T(COLOR_FG_FFD700),  3, T("%x<#FFD700>"), 11},
    { CS_FG(221),    CS_FOREGROUND, XTERM_FG(221), sizeof(XTERM_FG(221))-1, T(COLOR_FG_FFD75F),  3, T("%x<#FFD75F>"), 11},
    { CS_FG(222),    CS_FOREGROUND, XTERM_FG(222), sizeof(XTERM_FG(222))-1, T(COLOR_FG_FFD787),  3, T("%x<#FFD787>"), 11},
    { CS_FG(223),    CS_FOREGROUND, XTERM_FG(223), sizeof(XTERM_FG(223))-1, T(COLOR_FG_FFD7AF),  3, T("%x<#FFD7AF>"), 11},
    { CS_FG(224),    CS_FOREGROUND, XTERM_FG(224), sizeof(XTERM_FG(224))-1, T(COLOR_FG_FFD7D7),  3, T("%x<#FFD7D7>"), 11},
    { CS_FG(225),    CS_FOREGROUND, XTERM_FG(225), sizeof(XTERM_FG(225))-1, T(COLOR_FG_FFD7FF),  3, T("%x<#FFD7FF>"), 11},
    { CS_FG(226),    CS_FOREGROUND, XTERM_FG(226), sizeof(XTERM_FG(226))-1, T(COLOR_FG_FFFF00),  3, T("%x<#FFFF00>"), 11},
    { CS_FG(227),    CS_FOREGROUND, XTERM_FG(227), sizeof(XTERM_FG(227))-1, T(COLOR_FG_FFFF5F),  3, T("%x<#FFFF5F>"), 11},
    { CS_FG(228),    CS_FOREGROUND, XTERM_FG(228), sizeof(XTERM_FG(228))-1, T(COLOR_FG_FFFF87),  3, T("%x<#FFFF87>"), 11},
    { CS_FG(229),    CS_FOREGROUND, XTERM_FG(229), sizeof(XTERM_FG(229))-1, T(COLOR_FG_FFFFAF),  3, T("%x<#FFFFAF>"), 11},
    { CS_FG(230),    CS_FOREGROUND, XTERM_FG(230), sizeof(XTERM_FG(230))-1, T(COLOR_FG_FFFFD7),  3, T("%x<#FFFFD7>"), 11},
    { CS_FG(231),    CS_FOREGROUND, XTERM_FG(231), sizeof(XTERM_FG(231))-1, T(COLOR_FG_FFFFFF_2),3, T("%x<#FFFFFF>"), 11},
    { CS_FG(232),    CS_FOREGROUND, XTERM_FG(232), sizeof(XTERM_FG(232))-1, T(COLOR_FG_080808),  3, T("%x<#080808>"), 11},
    { CS_FG(233),    CS_FOREGROUND, XTERM_FG(233), sizeof(XTERM_FG(233))-1, T(COLOR_FG_121212),  3, T("%x<#121212>"), 11},
    { CS_FG(234),    CS_FOREGROUND, XTERM_FG(234), sizeof(XTERM_FG(234))-1, T(COLOR_FG_1C1C1C),  3, T("%x<#1C1C1C>"), 11},
    { CS_FG(235),    CS_FOREGROUND, XTERM_FG(235), sizeof(XTERM_FG(235))-1, T(COLOR_FG_262626),  3, T("%x<#262626>"), 11},
    { CS_FG(236),    CS_FOREGROUND, XTERM_FG(236), sizeof(XTERM_FG(236))-1, T(COLOR_FG_303030),  3, T("%x<#303030>"), 11},
    { CS_FG(237),    CS_FOREGROUND, XTERM_FG(237), sizeof(XTERM_FG(237))-1, T(COLOR_FG_3A3A3A),  3, T("%x<#3A3A3A>"), 11},
    { CS_FG(238),    CS_FOREGROUND, XTERM_FG(238), sizeof(XTERM_FG(238))-1, T(COLOR_FG_444444),  3, T("%x<#444444>"), 11},
    { CS_FG(239),    CS_FOREGROUND, XTERM_FG(239), sizeof(XTERM_FG(239))-1, T(COLOR_FG_4E4E4E),  3, T("%x<#4E4E4E>"), 11},
    { CS_FG(240),    CS_FOREGROUND, XTERM_FG(240), sizeof(XTERM_FG(240))-1, T(COLOR_FG_585858),  3, T("%x<#585858>"), 11},
    { CS_FG(241),    CS_FOREGROUND, XTERM_FG(241), sizeof(XTERM_FG(241))-1, T(COLOR_FG_626262),  3, T("%x<#626262>"), 11},
    { CS_FG(242),    CS_FOREGROUND, XTERM_FG(242), sizeof(XTERM_FG(242))-1, T(COLOR_FG_6C6C6C),  3, T("%x<#6C6C6C>"), 11},
    { CS_FG(243),    CS_FOREGROUND, XTERM_FG(243), sizeof(XTERM_FG(243))-1, T(COLOR_FG_767676),  3, T("%x<#767676>"), 11},
    { CS_FG(244),    CS_FOREGROUND, XTERM_FG(244), sizeof(XTERM_FG(244))-1, T(COLOR_FG_808080),  3, T("%x<#808080>"), 11},
    { CS_FG(245),    CS_FOREGROUND, XTERM_FG(245), sizeof(XTERM_FG(245))-1, T(COLOR_FG_8A8A8A),  3, T("%x<#8A8A8A>"), 11},
    { CS_FG(246),    CS_FOREGROUND, XTERM_FG(246), sizeof(XTERM_FG(246))-1, T(COLOR_FG_949494),  3, T("%x<#949494>"), 11},
    { CS_FG(247),    CS_FOREGROUND, XTERM_FG(247), sizeof(XTERM_FG(247))-1, T(COLOR_FG_9E9E9E),  3, T("%x<#9E9E9E>"), 11},
    { CS_FG(248),    CS_FOREGROUND, XTERM_FG(248), sizeof(XTERM_FG(248))-1, T(COLOR_FG_A8A8A8),  3, T("%x<#A8A8A8>"), 11},
    { CS_FG(249),    CS_FOREGROUND, XTERM_FG(249), sizeof(XTERM_FG(249))-1, T(COLOR_FG_B2B2B2),  3, T("%x<#B2B2B2>"), 11},
    { CS_FG(250),    CS_FOREGROUND, XTERM_FG(250), sizeof(XTERM_FG(250))-1, T(COLOR_FG_BCBCBC),  3, T("%x<#BCBCBC>"), 11},
    { CS_FG(251),    CS_FOREGROUND, XTERM_FG(251), sizeof(XTERM_FG(251))-1, T(COLOR_FG_C6C6C6),  3, T("%x<#C6C6C6>"), 11},
    { CS_FG(252),    CS_FOREGROUND, XTERM_FG(252), sizeof(XTERM_FG(252))-1, T(COLOR_FG_D0D0D0),  3, T("%x<#D0D0D0>"), 11},
    { CS_FG(253),    CS_FOREGROUND, XTERM_FG(253), sizeof(XTERM_FG(253))-1, T(COLOR_FG_DADADA),  3, T("%x<#DADADA>"), 11},
    { CS_FG(254),    CS_FOREGROUND, XTERM_FG(254), sizeof(XTERM_FG(254))-1, T(COLOR_FG_E4E4E4),  3, T("%x<#E4E4E4>"), 11},
    { CS_FG(255),    CS_FOREGROUND, XTERM_FG(255), sizeof(XTERM_FG(255))-1, T(COLOR_FG_EEEEEE),  3, T("%x<#EEEEEE>"), 11},
    { CS_BG_BLACK,   CS_BACKGROUND, ANSI_BBLACK,   sizeof(ANSI_BBLACK)-1,   T(COLOR_BG_BLACK),   3, T("%xX"), 3}, // COLOR_INDEX_BG
    { CS_BG_RED,     CS_BACKGROUND, ANSI_BRED,     sizeof(ANSI_BRED)-1,     T(COLOR_BG_RED),     3, T("%xR"), 3},
    { CS_BG_GREEN,   CS_BACKGROUND, ANSI_BGREEN,   sizeof(ANSI_BGREEN)-1,   T(COLOR_BG_GREEN),   3, T("%xG"), 3},
    { CS_BG_YELLOW,  CS_BACKGROUND, ANSI_BYELLOW,  sizeof(ANSI_BYELLOW)-1,  T(COLOR_BG_YELLOW),  3, T("%xY"), 3},
    { CS_BG_BLUE,    CS_BACKGROUND, ANSI_BBLUE,    sizeof(ANSI_BBLUE)-1,    T(COLOR_BG_BLUE),    3, T("%xB"), 3},
    { CS_BG_MAGENTA, CS_BACKGROUND, ANSI_BMAGENTA, sizeof(ANSI_BMAGENTA)-1, T(COLOR_BG_MAGENTA), 3, T("%xM"), 3},
    { CS_BG_CYAN,    CS_BACKGROUND, ANSI_BCYAN,    sizeof(ANSI_BCYAN)-1,    T(COLOR_BG_CYAN),    3, T("%xC"), 3},
    { CS_BG_WHITE,   CS_BACKGROUND, ANSI_BWHITE,   sizeof(ANSI_BWHITE)-1,   T(COLOR_BG_WHITE),   3, T("%xW"), 3},
    { CS_BG(  8),    CS_BACKGROUND, ANSI_BBLACK,   sizeof(ANSI_BBLACK)-1,   T(COLOR_BG_555555),  3, T("%xX"), 3}, // These eight are never used.
    { CS_BG(  9),    CS_BACKGROUND, ANSI_BRED,     sizeof(ANSI_BRED)-1,     T(COLOR_BG_FF5555),  3, T("%xR"), 3}, // .
    { CS_BG( 10),    CS_BACKGROUND, ANSI_BGREEN,   sizeof(ANSI_BGREEN)-1,   T(COLOR_BG_55FF55),  3, T("%xG"), 3}, // .
    { CS_BG( 11),    CS_BACKGROUND, ANSI_BYELLOW,  sizeof(ANSI_BYELLOW)-1,  T(COLOR_BG_FFFF55),  3, T("%xY"), 3}, // .
    { CS_BG( 12),    CS_BACKGROUND, ANSI_BBLUE,    sizeof(ANSI_BBLUE)-1,    T(COLOR_BG_5555FF),  3, T("%xB"), 3}, // .
    { CS_BG( 13),    CS_BACKGROUND, ANSI_BMAGENTA, sizeof(ANSI_BMAGENTA)-1, T(COLOR_BG_FF55FF),  3, T("%xM"), 3}, // .
    { CS_BG( 14),    CS_BACKGROUND, ANSI_BCYAN,    sizeof(ANSI_BCYAN)-1,    T(COLOR_BG_55FFFF),  3, T("%xC"), 3}, // .
    { CS_BG( 15),    CS_BACKGROUND, ANSI_BWHITE,   sizeof(ANSI_BWHITE)-1,   T(COLOR_BG_FFFFFF_1),3, T("%xW"), 3}, // -
    { CS_BG( 16),    CS_BACKGROUND, XTERM_BG( 16), sizeof(XTERM_BG( 16))-1, T(COLOR_BG_000000),  3, T("%X<#000000>"), 11},
    { CS_BG( 17),    CS_BACKGROUND, XTERM_BG( 17), sizeof(XTERM_BG( 17))-1, T(COLOR_BG_00005F),  3, T("%X<#00005F>"), 11},
    { CS_BG( 18),    CS_BACKGROUND, XTERM_BG( 18), sizeof(XTERM_BG( 18))-1, T(COLOR_BG_000087),  3, T("%X<#000087>"), 11},
    { CS_BG( 19),    CS_BACKGROUND, XTERM_BG( 19), sizeof(XTERM_BG( 19))-1, T(COLOR_BG_0000AF),  3, T("%X<#0000AF>"), 11},
    { CS_BG( 20),    CS_BACKGROUND, XTERM_BG( 20), sizeof(XTERM_BG( 20))-1, T(COLOR_BG_0000D7),  3, T("%X<#0000D7>"), 11},
    { CS_BG( 21),    CS_BACKGROUND, XTERM_BG( 21), sizeof(XTERM_BG( 21))-1, T(COLOR_BG_0000FF),  3, T("%X<#0000FF>"), 11},
    { CS_BG( 22),    CS_BACKGROUND, XTERM_BG( 22), sizeof(XTERM_BG( 22))-1, T(COLOR_BG_005F00),  3, T("%X<#005F00>"), 11},
    { CS_BG( 23),    CS_BACKGROUND, XTERM_BG( 23), sizeof(XTERM_BG( 23))-1, T(COLOR_BG_005F5F),  3, T("%X<#005F5F>"), 11},
    { CS_BG( 24),    CS_BACKGROUND, XTERM_BG( 24), sizeof(XTERM_BG( 24))-1, T(COLOR_BG_005F87),  3, T("%X<#005F87>"), 11},
    { CS_BG( 25),    CS_BACKGROUND, XTERM_BG( 25), sizeof(XTERM_BG( 25))-1, T(COLOR_BG_005FAF),  3, T("%X<#005FAF>"), 11},
    { CS_BG( 26),    CS_BACKGROUND, XTERM_BG( 26), sizeof(XTERM_BG( 26))-1, T(COLOR_BG_005FD7),  3, T("%X<#005FD7>"), 11},
    { CS_BG( 27),    CS_BACKGROUND, XTERM_BG( 27), sizeof(XTERM_BG( 27))-1, T(COLOR_BG_005FFF),  3, T("%X<#005FFF>"), 11},
    { CS_BG( 28),    CS_BACKGROUND, XTERM_BG( 28), sizeof(XTERM_BG( 28))-1, T(COLOR_BG_008700),  3, T("%X<#008700>"), 11},
    { CS_BG( 29),    CS_BACKGROUND, XTERM_BG( 29), sizeof(XTERM_BG( 29))-1, T(COLOR_BG_00875F),  3, T("%X<#00875F>"), 11},
    { CS_BG( 30),    CS_BACKGROUND, XTERM_BG( 30), sizeof(XTERM_BG( 30))-1, T(COLOR_BG_008785),  3, T("%X<#008785>"), 11},
    { CS_BG( 31),    CS_BACKGROUND, XTERM_BG( 31), sizeof(XTERM_BG( 31))-1, T(COLOR_BG_0087AF),  3, T("%X<#0087AF>"), 11},
    { CS_BG( 32),    CS_BACKGROUND, XTERM_BG( 32), sizeof(XTERM_BG( 32))-1, T(COLOR_BG_0087D7),  3, T("%X<#0087D7>"), 11},
    { CS_BG( 33),    CS_BACKGROUND, XTERM_BG( 33), sizeof(XTERM_BG( 33))-1, T(COLOR_BG_0087FF),  3, T("%X<#0087FF>"), 11},
    { CS_BG( 34),    CS_BACKGROUND, XTERM_BG( 34), sizeof(XTERM_BG( 34))-1, T(COLOR_BG_00AF00),  3, T("%X<#00AF00>"), 11},
    { CS_BG( 35),    CS_BACKGROUND, XTERM_BG( 35), sizeof(XTERM_BG( 35))-1, T(COLOR_BG_00AF5F),  3, T("%X<#00AF5F>"), 11},
    { CS_BG( 36),    CS_BACKGROUND, XTERM_BG( 36), sizeof(XTERM_BG( 36))-1, T(COLOR_BG_00AF87),  3, T("%X<#00AF87>"), 11},
    { CS_BG( 37),    CS_BACKGROUND, XTERM_BG( 37), sizeof(XTERM_BG( 37))-1, T(COLOR_BG_00AFAF),  3, T("%X<#00AFAF>"), 11},
    { CS_BG( 38),    CS_BACKGROUND, XTERM_BG( 38), sizeof(XTERM_BG( 38))-1, T(COLOR_BG_00AFD7),  3, T("%X<#00AFD7>"), 11},
    { CS_BG( 39),    CS_BACKGROUND, XTERM_BG( 39), sizeof(XTERM_BG( 39))-1, T(COLOR_BG_00AFFF),  3, T("%X<#00AFFF>"), 11},
    { CS_BG( 40),    CS_BACKGROUND, XTERM_BG( 40), sizeof(XTERM_BG( 40))-1, T(COLOR_BG_00D700),  3, T("%X<#00D700>"), 11},
    { CS_BG( 41),    CS_BACKGROUND, XTERM_BG( 41), sizeof(XTERM_BG( 41))-1, T(COLOR_BG_00D75F),  3, T("%X<#00D75F>"), 11},
    { CS_BG( 42),    CS_BACKGROUND, XTERM_BG( 42), sizeof(XTERM_BG( 42))-1, T(COLOR_BG_00D787),  3, T("%X<#00D787>"), 11},
    { CS_BG( 43),    CS_BACKGROUND, XTERM_BG( 43), sizeof(XTERM_BG( 43))-1, T(COLOR_BG_00D7AF),  3, T("%X<#00D7AF>"), 11},
    { CS_BG( 44),    CS_BACKGROUND, XTERM_BG( 44), sizeof(XTERM_BG( 44))-1, T(COLOR_BG_00D7D7),  3, T("%X<#00D7D7>"), 11},
    { CS_BG( 45),    CS_BACKGROUND, XTERM_BG( 45), sizeof(XTERM_BG( 45))-1, T(COLOR_BG_00D7FF),  3, T("%X<#00D7FF>"), 11},
    { CS_BG( 46),    CS_BACKGROUND, XTERM_BG( 46), sizeof(XTERM_BG( 46))-1, T(COLOR_BG_00FF00),  3, T("%X<#00FF00>"), 11},
    { CS_BG( 47),    CS_BACKGROUND, XTERM_BG( 47), sizeof(XTERM_BG( 47))-1, T(COLOR_BG_00FF5A),  3, T("%X<#00FF5A>"), 11},
    { CS_BG( 48),    CS_BACKGROUND, XTERM_BG( 48), sizeof(XTERM_BG( 48))-1, T(COLOR_BG_00FF87),  3, T("%X<#00FF87>"), 11},
    { CS_BG( 49),    CS_BACKGROUND, XTERM_BG( 49), sizeof(XTERM_BG( 49))-1, T(COLOR_BG_00FFAF),  3, T("%X<#00FFAF>"), 11},
    { CS_BG( 50),    CS_BACKGROUND, XTERM_BG( 50), sizeof(XTERM_BG( 50))-1, T(COLOR_BG_00FFD7),  3, T("%X<#00FFD7>"), 11},
    { CS_BG( 51),    CS_BACKGROUND, XTERM_BG( 51), sizeof(XTERM_BG( 51))-1, T(COLOR_BG_00FFFF),  3, T("%X<#00FFFF>"), 11},
    { CS_BG( 52),    CS_BACKGROUND, XTERM_BG( 52), sizeof(XTERM_BG( 52))-1, T(COLOR_BG_5F0000),  3, T("%X<#5F0000>"), 11},
    { CS_BG( 53),    CS_BACKGROUND, XTERM_BG( 53), sizeof(XTERM_BG( 53))-1, T(COLOR_BG_5F005F),  3, T("%X<#5F005F>"), 11},
    { CS_BG( 54),    CS_BACKGROUND, XTERM_BG( 54), sizeof(XTERM_BG( 54))-1, T(COLOR_BG_5F0087),  3, T("%X<#5F0087>"), 11},
    { CS_BG( 55),    CS_BACKGROUND, XTERM_BG( 55), sizeof(XTERM_BG( 55))-1, T(COLOR_BG_5F00AF),  3, T("%X<#5F00AF>"), 11},
    { CS_BG( 56),    CS_BACKGROUND, XTERM_BG( 56), sizeof(XTERM_BG( 56))-1, T(COLOR_BG_5F00D7),  3, T("%X<#5F00D7>"), 11},
    { CS_BG( 57),    CS_BACKGROUND, XTERM_BG( 57), sizeof(XTERM_BG( 57))-1, T(COLOR_BG_5F00FF),  3, T("%X<#5F00FF>"), 11},
    { CS_BG( 58),    CS_BACKGROUND, XTERM_BG( 58), sizeof(XTERM_BG( 58))-1, T(COLOR_BG_5F5F00),  3, T("%X<#5F5F00>"), 11},
    { CS_BG( 59),    CS_BACKGROUND, XTERM_BG( 59), sizeof(XTERM_BG( 59))-1, T(COLOR_BG_5F5F5F),  3, T("%X<#5F5F5F>"), 11},
    { CS_BG( 60),    CS_BACKGROUND, XTERM_BG( 60), sizeof(XTERM_BG( 60))-1, T(COLOR_BG_5F5F87),  3, T("%X<#5F5F87>"), 11},
    { CS_BG( 61),    CS_BACKGROUND, XTERM_BG( 61), sizeof(XTERM_BG( 61))-1, T(COLOR_BG_5F5FAF),  3, T("%X<#5F5FAF>"), 11},
    { CS_BG( 62),    CS_BACKGROUND, XTERM_BG( 62), sizeof(XTERM_BG( 62))-1, T(COLOR_BG_5F5FD7),  3, T("%X<#5F5FD7>"), 11},
    { CS_BG( 63),    CS_BACKGROUND, XTERM_BG( 63), sizeof(XTERM_BG( 63))-1, T(COLOR_BG_5F5FFF),  3, T("%X<#5F5FFF>"), 11},
    { CS_BG( 64),    CS_BACKGROUND, XTERM_BG( 64), sizeof(XTERM_BG( 64))-1, T(COLOR_BG_5F8700),  3, T("%X<#5F8700>"), 11},
    { CS_BG( 65),    CS_BACKGROUND, XTERM_BG( 65), sizeof(XTERM_BG( 65))-1, T(COLOR_BG_5F875F),  3, T("%X<#5F875F>"), 11},
    { CS_BG( 66),    CS_BACKGROUND, XTERM_BG( 66), sizeof(XTERM_BG( 66))-1, T(COLOR_BG_5F8787),  3, T("%X<#5F8787>"), 11},
    { CS_BG( 67),    CS_BACKGROUND, XTERM_BG( 67), sizeof(XTERM_BG( 67))-1, T(COLOR_BG_5F87AF),  3, T("%X<#5F87AF>"), 11},
    { CS_BG( 68),    CS_BACKGROUND, XTERM_BG( 68), sizeof(XTERM_BG( 68))-1, T(COLOR_BG_5F87D7),  3, T("%X<#5F87D7>"), 11},
    { CS_BG( 69),    CS_BACKGROUND, XTERM_BG( 69), sizeof(XTERM_BG( 69))-1, T(COLOR_BG_5F87FF),  3, T("%X<#5F87FF>"), 11},
    { CS_BG( 70),    CS_BACKGROUND, XTERM_BG( 70), sizeof(XTERM_BG( 70))-1, T(COLOR_BG_5FAF00),  3, T("%X<#5FAF00>"), 11},
    { CS_BG( 71),    CS_BACKGROUND, XTERM_BG( 71), sizeof(XTERM_BG( 71))-1, T(COLOR_BG_5FAF5F),  3, T("%X<#5FAF5F>"), 11},
    { CS_BG( 72),    CS_BACKGROUND, XTERM_BG( 72), sizeof(XTERM_BG( 72))-1, T(COLOR_BG_5FAF87),  3, T("%X<#5FAF87>"), 11},
    { CS_BG( 73),    CS_BACKGROUND, XTERM_BG( 73), sizeof(XTERM_BG( 73))-1, T(COLOR_BG_5FAFAF),  3, T("%X<#5FAFAF>"), 11},
    { CS_BG( 74),    CS_BACKGROUND, XTERM_BG( 74), sizeof(XTERM_BG( 74))-1, T(COLOR_BG_5FAFD7),  3, T("%X<#5FAFD7>"), 11},
    { CS_BG( 75),    CS_BACKGROUND, XTERM_BG( 75), sizeof(XTERM_BG( 75))-1, T(COLOR_BG_5FAFFF),  3, T("%X<#5FAFFF>"), 11},
    { CS_BG( 76),    CS_BACKGROUND, XTERM_BG( 76), sizeof(XTERM_BG( 76))-1, T(COLOR_BG_5FD700),  3, T("%X<#5FD700>"), 11},
    { CS_BG( 77),    CS_BACKGROUND, XTERM_BG( 77), sizeof(XTERM_BG( 77))-1, T(COLOR_BG_5FD75F),  3, T("%X<#5FD75F>"), 11},
    { CS_BG( 78),    CS_BACKGROUND, XTERM_BG( 78), sizeof(XTERM_BG( 78))-1, T(COLOR_BG_5FD787),  3, T("%X<#5FD787>"), 11},
    { CS_BG( 79),    CS_BACKGROUND, XTERM_BG( 79), sizeof(XTERM_BG( 79))-1, T(COLOR_BG_5FD7AF),  3, T("%X<#5FD7AF>"), 11},
    { CS_BG( 80),    CS_BACKGROUND, XTERM_BG( 80), sizeof(XTERM_BG( 80))-1, T(COLOR_BG_5FD7D7),  3, T("%X<#5FD7D7>"), 11},
    { CS_BG( 81),    CS_BACKGROUND, XTERM_BG( 81), sizeof(XTERM_BG( 81))-1, T(COLOR_BG_5FD7FF),  3, T("%X<#5FD7FF>"), 11},
    { CS_BG( 82),    CS_BACKGROUND, XTERM_BG( 82), sizeof(XTERM_BG( 82))-1, T(COLOR_BG_5FFF00),  3, T("%X<#5FFF00>"), 11},
    { CS_BG( 83),    CS_BACKGROUND, XTERM_BG( 83), sizeof(XTERM_BG( 83))-1, T(COLOR_BG_5FFF5F),  3, T("%X<#5FFF5F>"), 11},
    { CS_BG( 84),    CS_BACKGROUND, XTERM_BG( 84), sizeof(XTERM_BG( 84))-1, T(COLOR_BG_5FFF87),  3, T("%X<#5FFF87>"), 11},
    { CS_BG( 85),    CS_BACKGROUND, XTERM_BG( 85), sizeof(XTERM_BG( 85))-1, T(COLOR_BG_5FFFAF),  3, T("%X<#5FFFAF>"), 11},
    { CS_BG( 86),    CS_BACKGROUND, XTERM_BG( 86), sizeof(XTERM_BG( 86))-1, T(COLOR_BG_5FFFD7),  3, T("%X<#5FFFD7>"), 11},
    { CS_BG( 87),    CS_BACKGROUND, XTERM_BG( 87), sizeof(XTERM_BG( 87))-1, T(COLOR_BG_5FFFFF),  3, T("%X<#5FFFFF>"), 11},
    { CS_BG( 88),    CS_BACKGROUND, XTERM_BG( 88), sizeof(XTERM_BG( 88))-1, T(COLOR_BG_870000),  3, T("%X<#870000>"), 11},
    { CS_BG( 89),    CS_BACKGROUND, XTERM_BG( 89), sizeof(XTERM_BG( 89))-1, T(COLOR_BG_87005F),  3, T("%X<#87005F>"), 11},
    { CS_BG( 90),    CS_BACKGROUND, XTERM_BG( 90), sizeof(XTERM_BG( 90))-1, T(COLOR_BG_870087),  3, T("%X<#870087>"), 11},
    { CS_BG( 91),    CS_BACKGROUND, XTERM_BG( 91), sizeof(XTERM_BG( 91))-1, T(COLOR_BG_8700AF),  3, T("%X<#8700AF>"), 11},
    { CS_BG( 92),    CS_BACKGROUND, XTERM_BG( 92), sizeof(XTERM_BG( 92))-1, T(COLOR_BG_8700D7),  3, T("%X<#8700D7>"), 11},
    { CS_BG( 93),    CS_BACKGROUND, XTERM_BG( 93), sizeof(XTERM_BG( 93))-1, T(COLOR_BG_8700FF),  3, T("%X<#8700FF>"), 11},
    { CS_BG( 94),    CS_BACKGROUND, XTERM_BG( 94), sizeof(XTERM_BG( 94))-1, T(COLOR_BG_875F00),  3, T("%X<#875F00>"), 11},
    { CS_BG( 95),    CS_BACKGROUND, XTERM_BG( 95), sizeof(XTERM_BG( 95))-1, T(COLOR_BG_875F5F),  3, T("%X<#875F5F>"), 11},
    { CS_BG( 96),    CS_BACKGROUND, XTERM_BG( 96), sizeof(XTERM_BG( 96))-1, T(COLOR_BG_875F87),  3, T("%X<#875F87>"), 11},
    { CS_BG( 97),    CS_BACKGROUND, XTERM_BG( 97), sizeof(XTERM_BG( 97))-1, T(COLOR_BG_875FAF),  3, T("%X<#875FAF>"), 11},
    { CS_BG( 98),    CS_BACKGROUND, XTERM_BG( 98), sizeof(XTERM_BG( 98))-1, T(COLOR_BG_875FD7),  3, T("%X<#875FD7>"), 11},
    { CS_BG( 99),    CS_BACKGROUND, XTERM_BG( 99), sizeof(XTERM_BG( 99))-1, T(COLOR_BG_875FFF),  3, T("%X<#875FFF>"), 11},
    { CS_BG(100),    CS_BACKGROUND, XTERM_BG(100), sizeof(XTERM_BG(100))-1, T(COLOR_BG_878700),  3, T("%X<#878700>"), 11},
    { CS_BG(101),    CS_BACKGROUND, XTERM_BG(101), sizeof(XTERM_BG(101))-1, T(COLOR_BG_87875F),  3, T("%X<#87875F>"), 11},
    { CS_BG(102),    CS_BACKGROUND, XTERM_BG(102), sizeof(XTERM_BG(102))-1, T(COLOR_BG_878787),  3, T("%X<#878787>"), 11},
    { CS_BG(103),    CS_BACKGROUND, XTERM_BG(103), sizeof(XTERM_BG(103))-1, T(COLOR_BG_8787AF),  3, T("%X<#8787AF>"), 11},
    { CS_BG(104),    CS_BACKGROUND, XTERM_BG(104), sizeof(XTERM_BG(104))-1, T(COLOR_BG_8787D7),  3, T("%X<#8787D7>"), 11},
    { CS_BG(105),    CS_BACKGROUND, XTERM_BG(105), sizeof(XTERM_BG(105))-1, T(COLOR_BG_8787FF),  3, T("%X<#8787FF>"), 11},
    { CS_BG(106),    CS_BACKGROUND, XTERM_BG(106), sizeof(XTERM_BG(106))-1, T(COLOR_BG_87AF00),  3, T("%X<#87AF00>"), 11},
    { CS_BG(107),    CS_BACKGROUND, XTERM_BG(107), sizeof(XTERM_BG(107))-1, T(COLOR_BG_87AF5F),  3, T("%X<#87AF5F>"), 11},
    { CS_BG(108),    CS_BACKGROUND, XTERM_BG(108), sizeof(XTERM_BG(108))-1, T(COLOR_BG_87AF87),  3, T("%X<#87AF87>"), 11},
    { CS_BG(109),    CS_BACKGROUND, XTERM_BG(109), sizeof(XTERM_BG(109))-1, T(COLOR_BG_87AFAF),  3, T("%X<#87AFAF>"), 11},
    { CS_BG(110),    CS_BACKGROUND, XTERM_BG(110), sizeof(XTERM_BG(110))-1, T(COLOR_BG_87AFD7),  3, T("%X<#87AFD7>"), 11},
    { CS_BG(111),    CS_BACKGROUND, XTERM_BG(111), sizeof(XTERM_BG(111))-1, T(COLOR_BG_87AFFF),  3, T("%X<#87AFFF>"), 11},
    { CS_BG(112),    CS_BACKGROUND, XTERM_BG(112), sizeof(XTERM_BG(112))-1, T(COLOR_BG_87D700),  3, T("%X<#87D700>"), 11},
    { CS_BG(113),    CS_BACKGROUND, XTERM_BG(113), sizeof(XTERM_BG(113))-1, T(COLOR_BG_87D75A),  3, T("%X<#87D75A>"), 11},
    { CS_BG(114),    CS_BACKGROUND, XTERM_BG(114), sizeof(XTERM_BG(114))-1, T(COLOR_BG_87D787),  3, T("%X<#87D787>"), 11},
    { CS_BG(115),    CS_BACKGROUND, XTERM_BG(115), sizeof(XTERM_BG(115))-1, T(COLOR_BG_87D7AF),  3, T("%X<#87D7AF>"), 11},
    { CS_BG(116),    CS_BACKGROUND, XTERM_BG(116), sizeof(XTERM_BG(116))-1, T(COLOR_BG_87D7D7),  3, T("%X<#87D7D7>"), 11},
    { CS_BG(117),    CS_BACKGROUND, XTERM_BG(117), sizeof(XTERM_BG(117))-1, T(COLOR_BG_87D7FF),  3, T("%X<#87D7FF>"), 11},
    { CS_BG(118),    CS_BACKGROUND, XTERM_BG(118), sizeof(XTERM_BG(118))-1, T(COLOR_BG_87FF00),  3, T("%X<#87FF00>"), 11},
    { CS_BG(119),    CS_BACKGROUND, XTERM_BG(119), sizeof(XTERM_BG(119))-1, T(COLOR_BG_87FF5F),  3, T("%X<#87FF5F>"), 11},
    { CS_BG(120),    CS_BACKGROUND, XTERM_BG(120), sizeof(XTERM_BG(120))-1, T(COLOR_BG_87FF87),  3, T("%X<#87FF87>"), 11},
    { CS_BG(121),    CS_BACKGROUND, XTERM_BG(121), sizeof(XTERM_BG(121))-1, T(COLOR_BG_87FFAF),  3, T("%X<#87FFAF>"), 11},
    { CS_BG(122),    CS_BACKGROUND, XTERM_BG(122), sizeof(XTERM_BG(122))-1, T(COLOR_BG_87FFD7),  3, T("%X<#87FFD7>"), 11},
    { CS_BG(123),    CS_BACKGROUND, XTERM_BG(123), sizeof(XTERM_BG(123))-1, T(COLOR_BG_87FFFF),  3, T("%X<#87FFFF>"), 11},
    { CS_BG(124),    CS_BACKGROUND, XTERM_BG(124), sizeof(XTERM_BG(124))-1, T(COLOR_BG_AF0000),  3, T("%X<#AF0000>"), 11},
    { CS_BG(125),    CS_BACKGROUND, XTERM_BG(125), sizeof(XTERM_BG(125))-1, T(COLOR_BG_AF005F),  3, T("%X<#AF005F>"), 11},
    { CS_BG(126),    CS_BACKGROUND, XTERM_BG(126), sizeof(XTERM_BG(126))-1, T(COLOR_BG_AF0087),  3, T("%X<#AF0087>"), 11},
    { CS_BG(127),    CS_BACKGROUND, XTERM_BG(127), sizeof(XTERM_BG(127))-1, T(COLOR_BG_AF00AF),  3, T("%X<#AF00AF>"), 11},
    { CS_BG(128),    CS_BACKGROUND, XTERM_BG(128), sizeof(XTERM_BG(128))-1, T(COLOR_BG_AF00D7),  3, T("%X<#AF00D7>"), 11},
    { CS_BG(129),    CS_BACKGROUND, XTERM_BG(129), sizeof(XTERM_BG(129))-1, T(COLOR_BG_AF00FF),  3, T("%X<#AF00FF>"), 11},
    { CS_BG(130),    CS_BACKGROUND, XTERM_BG(130), sizeof(XTERM_BG(130))-1, T(COLOR_BG_AF5F00),  3, T("%X<#AF5F00>"), 11},
    { CS_BG(131),    CS_BACKGROUND, XTERM_BG(131), sizeof(XTERM_BG(131))-1, T(COLOR_BG_AF5F5F),  3, T("%X<#AF5F5F>"), 11},
    { CS_BG(132),    CS_BACKGROUND, XTERM_BG(132), sizeof(XTERM_BG(132))-1, T(COLOR_BG_AF5F87),  3, T("%X<#AF5F87>"), 11},
    { CS_BG(133),    CS_BACKGROUND, XTERM_BG(133), sizeof(XTERM_BG(133))-1, T(COLOR_BG_AF5FAF),  3, T("%X<#AF5FAF>"), 11},
    { CS_BG(134),    CS_BACKGROUND, XTERM_BG(134), sizeof(XTERM_BG(134))-1, T(COLOR_BG_AF5FD7),  3, T("%X<#AF5FD7>"), 11},
    { CS_BG(135),    CS_BACKGROUND, XTERM_BG(135), sizeof(XTERM_BG(135))-1, T(COLOR_BG_AF5FFF),  3, T("%X<#AF5FFF>"), 11},
    { CS_BG(136),    CS_BACKGROUND, XTERM_BG(136), sizeof(XTERM_BG(136))-1, T(COLOR_BG_AF8700),  3, T("%X<#AF8700>"), 11},
    { CS_BG(137),    CS_BACKGROUND, XTERM_BG(137), sizeof(XTERM_BG(137))-1, T(COLOR_BG_AF875F),  3, T("%X<#AF875F>"), 11},
    { CS_BG(138),    CS_BACKGROUND, XTERM_BG(138), sizeof(XTERM_BG(138))-1, T(COLOR_BG_AF8787),  3, T("%X<#AF8787>"), 11},
    { CS_BG(139),    CS_BACKGROUND, XTERM_BG(139), sizeof(XTERM_BG(139))-1, T(COLOR_BG_AF87AF),  3, T("%X<#AF87AF>"), 11},
    { CS_BG(140),    CS_BACKGROUND, XTERM_BG(140), sizeof(XTERM_BG(140))-1, T(COLOR_BG_AF87D7),  3, T("%X<#AF87D7>"), 11},
    { CS_BG(141),    CS_BACKGROUND, XTERM_BG(141), sizeof(XTERM_BG(141))-1, T(COLOR_BG_AF87FF),  3, T("%X<#AF87FF>"), 11},
    { CS_BG(142),    CS_BACKGROUND, XTERM_BG(142), sizeof(XTERM_BG(142))-1, T(COLOR_BG_AFAF00),  3, T("%X<#AFAF00>"), 11},
    { CS_BG(143),    CS_BACKGROUND, XTERM_BG(143), sizeof(XTERM_BG(143))-1, T(COLOR_BG_AFAF5F),  3, T("%X<#AFAF5F>"), 11},
    { CS_BG(144),    CS_BACKGROUND, XTERM_BG(144), sizeof(XTERM_BG(144))-1, T(COLOR_BG_AFAF87),  3, T("%X<#AFAF87>"), 11},
    { CS_BG(145),    CS_BACKGROUND, XTERM_BG(145), sizeof(XTERM_BG(145))-1, T(COLOR_BG_AFAFAF),  3, T("%X<#AFAFAF>"), 11},
    { CS_BG(146),    CS_BACKGROUND, XTERM_BG(146), sizeof(XTERM_BG(146))-1, T(COLOR_BG_AFAFD7),  3, T("%X<#AFAFD7>"), 11},
    { CS_BG(147),    CS_BACKGROUND, XTERM_BG(147), sizeof(XTERM_BG(147))-1, T(COLOR_BG_AFAFFF),  3, T("%X<#AFAFFF>"), 11},
    { CS_BG(148),    CS_BACKGROUND, XTERM_BG(148), sizeof(XTERM_BG(148))-1, T(COLOR_BG_AFD700),  3, T("%X<#AFD700>"), 11},
    { CS_BG(149),    CS_BACKGROUND, XTERM_BG(149), sizeof(XTERM_BG(149))-1, T(COLOR_BG_AFD75F),  3, T("%X<#AFD75F>"), 11},
    { CS_BG(150),    CS_BACKGROUND, XTERM_BG(150), sizeof(XTERM_BG(150))-1, T(COLOR_BG_AFD787),  3, T("%X<#AFD787>"), 11},
    { CS_BG(151),    CS_BACKGROUND, XTERM_BG(151), sizeof(XTERM_BG(151))-1, T(COLOR_BG_AFD7AF),  3, T("%X<#AFD7AF>"), 11},
    { CS_BG(152),    CS_BACKGROUND, XTERM_BG(152), sizeof(XTERM_BG(152))-1, T(COLOR_BG_AFD7D7),  3, T("%X<#AFD7D7>"), 11},
    { CS_BG(153),    CS_BACKGROUND, XTERM_BG(153), sizeof(XTERM_BG(153))-1, T(COLOR_BG_AFD7FF),  3, T("%X<#AFD7FF>"), 11},
    { CS_BG(154),    CS_BACKGROUND, XTERM_BG(154), sizeof(XTERM_BG(154))-1, T(COLOR_BG_AFFF00),  3, T("%X<#AFFF00>"), 11},
    { CS_BG(155),    CS_BACKGROUND, XTERM_BG(155), sizeof(XTERM_BG(155))-1, T(COLOR_BG_AFFF5F),  3, T("%X<#AFFF5F>"), 11},
    { CS_BG(156),    CS_BACKGROUND, XTERM_BG(156), sizeof(XTERM_BG(156))-1, T(COLOR_BG_AFFF87),  3, T("%X<#AFFF87>"), 11},
    { CS_BG(157),    CS_BACKGROUND, XTERM_BG(157), sizeof(XTERM_BG(157))-1, T(COLOR_BG_AFFFAF),  3, T("%X<#AFFFAF>"), 11},
    { CS_BG(158),    CS_BACKGROUND, XTERM_BG(158), sizeof(XTERM_BG(158))-1, T(COLOR_BG_AFFFD7),  3, T("%X<#AFFFD7>"), 11},
    { CS_BG(159),    CS_BACKGROUND, XTERM_BG(159), sizeof(XTERM_BG(159))-1, T(COLOR_BG_AFFFFF),  3, T("%X<#AFFFFF>"), 11},
    { CS_BG(160),    CS_BACKGROUND, XTERM_BG(160), sizeof(XTERM_BG(160))-1, T(COLOR_BG_D70000),  3, T("%X<#D70000>"), 11},
    { CS_BG(161),    CS_BACKGROUND, XTERM_BG(161), sizeof(XTERM_BG(161))-1, T(COLOR_BG_D7005F),  3, T("%X<#D7005F>"), 11},
    { CS_BG(162),    CS_BACKGROUND, XTERM_BG(162), sizeof(XTERM_BG(162))-1, T(COLOR_BG_D70087),  3, T("%X<#D70087>"), 11},
    { CS_BG(163),    CS_BACKGROUND, XTERM_BG(163), sizeof(XTERM_BG(163))-1, T(COLOR_BG_D700AF),  3, T("%X<#D700AF>"), 11},
    { CS_BG(164),    CS_BACKGROUND, XTERM_BG(164), sizeof(XTERM_BG(164))-1, T(COLOR_BG_D700D7),  3, T("%X<#D700D7>"), 11},
    { CS_BG(165),    CS_BACKGROUND, XTERM_BG(165), sizeof(XTERM_BG(165))-1, T(COLOR_BG_D700FF),  3, T("%X<#D700FF>"), 11},
    { CS_BG(166),    CS_BACKGROUND, XTERM_BG(166), sizeof(XTERM_BG(166))-1, T(COLOR_BG_D75F00),  3, T("%X<#D75F00>"), 11},
    { CS_BG(167),    CS_BACKGROUND, XTERM_BG(167), sizeof(XTERM_BG(167))-1, T(COLOR_BG_D75F5F),  3, T("%X<#D75F5F>"), 11},
    { CS_BG(168),    CS_BACKGROUND, XTERM_BG(168), sizeof(XTERM_BG(168))-1, T(COLOR_BG_D75F87),  3, T("%X<#D75F87>"), 11},
    { CS_BG(169),    CS_BACKGROUND, XTERM_BG(169), sizeof(XTERM_BG(169))-1, T(COLOR_BG_D75FAF),  3, T("%X<#D75FAF>"), 11},
    { CS_BG(170),    CS_BACKGROUND, XTERM_BG(170), sizeof(XTERM_BG(170))-1, T(COLOR_BG_D75FD7),  3, T("%X<#D75FD7>"), 11},
    { CS_BG(171),    CS_BACKGROUND, XTERM_BG(171), sizeof(XTERM_BG(171))-1, T(COLOR_BG_D75FFF),  3, T("%X<#D75FFF>"), 11},
    { CS_BG(172),    CS_BACKGROUND, XTERM_BG(172), sizeof(XTERM_BG(172))-1, T(COLOR_BG_D78700),  3, T("%X<#D78700>"), 11},
    { CS_BG(173),    CS_BACKGROUND, XTERM_BG(173), sizeof(XTERM_BG(173))-1, T(COLOR_BG_D7875A),  3, T("%X<#D7875A>"), 11},
    { CS_BG(174),    CS_BACKGROUND, XTERM_BG(174), sizeof(XTERM_BG(174))-1, T(COLOR_BG_D78787),  3, T("%X<#D78787>"), 11},
    { CS_BG(175),    CS_BACKGROUND, XTERM_BG(175), sizeof(XTERM_BG(175))-1, T(COLOR_BG_D787AF),  3, T("%X<#D787AF>"), 11},
    { CS_BG(176),    CS_BACKGROUND, XTERM_BG(176), sizeof(XTERM_BG(176))-1, T(COLOR_BG_D787D7),  3, T("%X<#D787D7>"), 11},
    { CS_BG(177),    CS_BACKGROUND, XTERM_BG(177), sizeof(XTERM_BG(177))-1, T(COLOR_BG_D787FF),  3, T("%X<#D787FF>"), 11},
    { CS_BG(178),    CS_BACKGROUND, XTERM_BG(178), sizeof(XTERM_BG(178))-1, T(COLOR_BG_D7AF00),  3, T("%X<#D7AF00>"), 11},
    { CS_BG(179),    CS_BACKGROUND, XTERM_BG(179), sizeof(XTERM_BG(179))-1, T(COLOR_BG_D7AF5A),  3, T("%X<#D7AF5A>"), 11},
    { CS_BG(180),    CS_BACKGROUND, XTERM_BG(180), sizeof(XTERM_BG(180))-1, T(COLOR_BG_D7AF87),  3, T("%X<#D7AF87>"), 11},
    { CS_BG(181),    CS_BACKGROUND, XTERM_BG(181), sizeof(XTERM_BG(181))-1, T(COLOR_BG_D7AFAF),  3, T("%X<#D7AFAF>"), 11},
    { CS_BG(182),    CS_BACKGROUND, XTERM_BG(182), sizeof(XTERM_BG(182))-1, T(COLOR_BG_D7AFD7),  3, T("%X<#D7AFD7>"), 11},
    { CS_BG(183),    CS_BACKGROUND, XTERM_BG(183), sizeof(XTERM_BG(183))-1, T(COLOR_BG_D7AFFF),  3, T("%X<#D7AFFF>"), 11},
    { CS_BG(184),    CS_BACKGROUND, XTERM_BG(184), sizeof(XTERM_BG(184))-1, T(COLOR_BG_D7D700),  3, T("%X<#D7D700>"), 11},
    { CS_BG(185),    CS_BACKGROUND, XTERM_BG(185), sizeof(XTERM_BG(185))-1, T(COLOR_BG_D7D75F),  3, T("%X<#D7D75F>"), 11},
    { CS_BG(186),    CS_BACKGROUND, XTERM_BG(186), sizeof(XTERM_BG(186))-1, T(COLOR_BG_D7D787),  3, T("%X<#D7D787>"), 11},
    { CS_BG(187),    CS_BACKGROUND, XTERM_BG(187), sizeof(XTERM_BG(187))-1, T(COLOR_BG_D7D7AF),  3, T("%X<#D7D7AF>"), 11},
    { CS_BG(188),    CS_BACKGROUND, XTERM_BG(188), sizeof(XTERM_BG(188))-1, T(COLOR_BG_D7D7D7),  3, T("%X<#D7D7D7>"), 11},
    { CS_BG(189),    CS_BACKGROUND, XTERM_BG(189), sizeof(XTERM_BG(189))-1, T(COLOR_BG_D7D7FF),  3, T("%X<#D7D7FF>"), 11},
    { CS_BG(190),    CS_BACKGROUND, XTERM_BG(190), sizeof(XTERM_BG(190))-1, T(COLOR_BG_D7FF00),  3, T("%X<#D7FF00>"), 11},
    { CS_BG(191),    CS_BACKGROUND, XTERM_BG(191), sizeof(XTERM_BG(191))-1, T(COLOR_BG_D7FF5F),  3, T("%X<#D7FF5F>"), 11},
    { CS_BG(192),    CS_BACKGROUND, XTERM_BG(192), sizeof(XTERM_BG(192))-1, T(COLOR_BG_D7FF87),  3, T("%X<#D7FF87>"), 11},
    { CS_BG(193),    CS_BACKGROUND, XTERM_BG(193), sizeof(XTERM_BG(193))-1, T(COLOR_BG_D7FFAF),  3, T("%X<#D7FFAF>"), 11},
    { CS_BG(194),    CS_BACKGROUND, XTERM_BG(194), sizeof(XTERM_BG(194))-1, T(COLOR_BG_D7FFD7),  3, T("%X<#D7FFD7>"), 11},
    { CS_BG(195),    CS_BACKGROUND, XTERM_BG(195), sizeof(XTERM_BG(195))-1, T(COLOR_BG_D7FFFF),  3, T("%X<#D7FFFF>"), 11},
    { CS_BG(196),    CS_BACKGROUND, XTERM_BG(196), sizeof(XTERM_BG(196))-1, T(COLOR_BG_FF0000),  3, T("%X<#FF0000>"), 11},
    { CS_BG(197),    CS_BACKGROUND, XTERM_BG(197), sizeof(XTERM_BG(197))-1, T(COLOR_BG_FF005F),  3, T("%X<#FF005F>"), 11},
    { CS_BG(198),    CS_BACKGROUND, XTERM_BG(198), sizeof(XTERM_BG(198))-1, T(COLOR_BG_FF0087),  3, T("%X<#FF0087>"), 11},
    { CS_BG(199),    CS_BACKGROUND, XTERM_BG(199), sizeof(XTERM_BG(199))-1, T(COLOR_BG_FF00AF),  3, T("%X<#FF00AF>"), 11},
    { CS_BG(200),    CS_BACKGROUND, XTERM_BG(200), sizeof(XTERM_BG(200))-1, T(COLOR_BG_FF00D7),  3, T("%X<#FF00D7>"), 11},
    { CS_BG(201),    CS_BACKGROUND, XTERM_BG(201), sizeof(XTERM_BG(201))-1, T(COLOR_BG_FF00FF),  3, T("%X<#FF00FF>"), 11},
    { CS_BG(202),    CS_BACKGROUND, XTERM_BG(202), sizeof(XTERM_BG(202))-1, T(COLOR_BG_FF5F00),  3, T("%X<#FF5F00>"), 11},
    { CS_BG(203),    CS_BACKGROUND, XTERM_BG(203), sizeof(XTERM_BG(203))-1, T(COLOR_BG_FF5F5F),  3, T("%X<#FF5F5F>"), 11},
    { CS_BG(204),    CS_BACKGROUND, XTERM_BG(204), sizeof(XTERM_BG(204))-1, T(COLOR_BG_FF5F87),  3, T("%X<#FF5F87>"), 11},
    { CS_BG(205),    CS_BACKGROUND, XTERM_BG(205), sizeof(XTERM_BG(205))-1, T(COLOR_BG_FF5FAF),  3, T("%X<#FF5FAF>"), 11},
    { CS_BG(206),    CS_BACKGROUND, XTERM_BG(206), sizeof(XTERM_BG(206))-1, T(COLOR_BG_FF5FD7),  3, T("%X<#FF5FD7>"), 11},
    { CS_BG(207),    CS_BACKGROUND, XTERM_BG(207), sizeof(XTERM_BG(207))-1, T(COLOR_BG_FF5FFF),  3, T("%X<#FF5FFF>"), 11},
    { CS_BG(208),    CS_BACKGROUND, XTERM_BG(208), sizeof(XTERM_BG(208))-1, T(COLOR_BG_FF8700),  3, T("%X<#FF8700>"), 11},
    { CS_BG(209),    CS_BACKGROUND, XTERM_BG(209), sizeof(XTERM_BG(209))-1, T(COLOR_BG_FF875F),  3, T("%X<#FF875F>"), 11},
    { CS_BG(210),    CS_BACKGROUND, XTERM_BG(210), sizeof(XTERM_BG(210))-1, T(COLOR_BG_FF8787),  3, T("%X<#FF8787>"), 11},
    { CS_BG(211),    CS_BACKGROUND, XTERM_BG(211), sizeof(XTERM_BG(211))-1, T(COLOR_BG_FF87AF),  3, T("%X<#FF87AF>"), 11},
    { CS_BG(212),    CS_BACKGROUND, XTERM_BG(212), sizeof(XTERM_BG(212))-1, T(COLOR_BG_FF87D7),  3, T("%X<#FF87D7>"), 11},
    { CS_BG(213),    CS_BACKGROUND, XTERM_BG(213), sizeof(XTERM_BG(213))-1, T(COLOR_BG_FF87FF),  3, T("%X<#FF87FF>"), 11},
    { CS_BG(214),    CS_BACKGROUND, XTERM_BG(214), sizeof(XTERM_BG(214))-1, T(COLOR_BG_FFAF00),  3, T("%X<#FFAF00>"), 11},
    { CS_BG(215),    CS_BACKGROUND, XTERM_BG(215), sizeof(XTERM_BG(215))-1, T(COLOR_BG_FFAF5F),  3, T("%X<#FFAF5F>"), 11},
    { CS_BG(216),    CS_BACKGROUND, XTERM_BG(216), sizeof(XTERM_BG(216))-1, T(COLOR_BG_FFAF87),  3, T("%X<#FFAF87>"), 11},
    { CS_BG(217),    CS_BACKGROUND, XTERM_BG(217), sizeof(XTERM_BG(217))-1, T(COLOR_BG_FFAFAF),  3, T("%X<#FFAFAF>"), 11},
    { CS_BG(218),    CS_BACKGROUND, XTERM_BG(218), sizeof(XTERM_BG(218))-1, T(COLOR_BG_FFAFD7),  3, T("%X<#FFAFD7>"), 11},
    { CS_BG(219),    CS_BACKGROUND, XTERM_BG(219), sizeof(XTERM_BG(219))-1, T(COLOR_BG_FFAFFF),  3, T("%X<#FFAFFF>"), 11},
    { CS_BG(220),    CS_BACKGROUND, XTERM_BG(220), sizeof(XTERM_BG(220))-1, T(COLOR_BG_FFD700),  3, T("%X<#FFD700>"), 11},
    { CS_BG(221),    CS_BACKGROUND, XTERM_BG(221), sizeof(XTERM_BG(221))-1, T(COLOR_BG_FFD75F),  3, T("%X<#FFD75F>"), 11},
    { CS_BG(222),    CS_BACKGROUND, XTERM_BG(222), sizeof(XTERM_BG(222))-1, T(COLOR_BG_FFD787),  3, T("%X<#FFD787>"), 11},
    { CS_BG(223),    CS_BACKGROUND, XTERM_BG(223), sizeof(XTERM_BG(223))-1, T(COLOR_BG_FFD7AF),  3, T("%X<#FFD7AF>"), 11},
    { CS_BG(224),    CS_BACKGROUND, XTERM_BG(224), sizeof(XTERM_BG(224))-1, T(COLOR_BG_FFD7D7),  3, T("%X<#FFD7D7>"), 11},
    { CS_BG(225),    CS_BACKGROUND, XTERM_BG(225), sizeof(XTERM_BG(225))-1, T(COLOR_BG_FFD7FF),  3, T("%X<#FFD7FF>"), 11},
    { CS_BG(226),    CS_BACKGROUND, XTERM_BG(226), sizeof(XTERM_BG(226))-1, T(COLOR_BG_FFFF00),  3, T("%X<#FFFF00>"), 11},
    { CS_BG(227),    CS_BACKGROUND, XTERM_BG(227), sizeof(XTERM_BG(227))-1, T(COLOR_BG_FFFF5F),  3, T("%X<#FFFF5F>"), 11},
    { CS_BG(228),    CS_BACKGROUND, XTERM_BG(228), sizeof(XTERM_BG(228))-1, T(COLOR_BG_FFFF87),  3, T("%X<#FFFF87>"), 11},
    { CS_BG(229),    CS_BACKGROUND, XTERM_BG(229), sizeof(XTERM_BG(229))-1, T(COLOR_BG_FFFFAF),  3, T("%X<#FFFFAF>"), 11},
    { CS_BG(230),    CS_BACKGROUND, XTERM_BG(230), sizeof(XTERM_BG(230))-1, T(COLOR_BG_FFFFD7),  3, T("%X<#FFFFD7>"), 11},
    { CS_BG(231),    CS_BACKGROUND, XTERM_BG(231), sizeof(XTERM_BG(231))-1, T(COLOR_BG_FFFFFF_2),3, T("%X<#FFFFFF>"), 11},
    { CS_BG(232),    CS_BACKGROUND, XTERM_BG(232), sizeof(XTERM_BG(232))-1, T(COLOR_BG_080808),  3, T("%X<#080808>"), 11},
    { CS_BG(233),    CS_BACKGROUND, XTERM_BG(233), sizeof(XTERM_BG(233))-1, T(COLOR_BG_121212),  3, T("%X<#121212>"), 11},
    { CS_BG(234),    CS_BACKGROUND, XTERM_BG(234), sizeof(XTERM_BG(234))-1, T(COLOR_BG_1C1C1C),  3, T("%X<#1C1C1C>"), 11},
    { CS_BG(235),    CS_BACKGROUND, XTERM_BG(235), sizeof(XTERM_BG(235))-1, T(COLOR_BG_262626),  3, T("%X<#262626>"), 11},
    { CS_BG(236),    CS_BACKGROUND, XTERM_BG(236), sizeof(XTERM_BG(236))-1, T(COLOR_BG_303030),  3, T("%X<#303030>"), 11},
    { CS_BG(237),    CS_BACKGROUND, XTERM_BG(237), sizeof(XTERM_BG(237))-1, T(COLOR_BG_3A3A3A),  3, T("%X<#3A3A3A>"), 11},
    { CS_BG(238),    CS_BACKGROUND, XTERM_BG(238), sizeof(XTERM_BG(238))-1, T(COLOR_BG_444444),  3, T("%X<#444444>"), 11},
    { CS_BG(239),    CS_BACKGROUND, XTERM_BG(239), sizeof(XTERM_BG(239))-1, T(COLOR_BG_4E4E4E),  3, T("%X<#4E4E4E>"), 11},
    { CS_BG(240),    CS_BACKGROUND, XTERM_BG(240), sizeof(XTERM_BG(240))-1, T(COLOR_BG_585858),  3, T("%X<#585858>"), 11},
    { CS_BG(241),    CS_BACKGROUND, XTERM_BG(241), sizeof(XTERM_BG(241))-1, T(COLOR_BG_626262),  3, T("%X<#626262>"), 11},
    { CS_BG(242),    CS_BACKGROUND, XTERM_BG(242), sizeof(XTERM_BG(242))-1, T(COLOR_BG_6C6C6C),  3, T("%X<#6C6C6C>"), 11},
    { CS_BG(243),    CS_BACKGROUND, XTERM_BG(243), sizeof(XTERM_BG(243))-1, T(COLOR_BG_767676),  3, T("%X<#767676>"), 11},
    { CS_BG(244),    CS_BACKGROUND, XTERM_BG(244), sizeof(XTERM_BG(244))-1, T(COLOR_BG_808080),  3, T("%X<#808080>"), 11},
    { CS_BG(245),    CS_BACKGROUND, XTERM_BG(245), sizeof(XTERM_BG(245))-1, T(COLOR_BG_8A8A8A),  3, T("%X<#8A8A8A>"), 11},
    { CS_BG(246),    CS_BACKGROUND, XTERM_BG(246), sizeof(XTERM_BG(246))-1, T(COLOR_BG_949494),  3, T("%X<#949494>"), 11},
    { CS_BG(247),    CS_BACKGROUND, XTERM_BG(247), sizeof(XTERM_BG(247))-1, T(COLOR_BG_9E9E9E),  3, T("%X<#9E9E9E>"), 11},
    { CS_BG(248),    CS_BACKGROUND, XTERM_BG(248), sizeof(XTERM_BG(248))-1, T(COLOR_BG_A8A8A8),  3, T("%X<#A8A8A8>"), 11},
    { CS_BG(249),    CS_BACKGROUND, XTERM_BG(249), sizeof(XTERM_BG(249))-1, T(COLOR_BG_B2B2B2),  3, T("%X<#B2B2B2>"), 11},
    { CS_BG(250),    CS_BACKGROUND, XTERM_BG(250), sizeof(XTERM_BG(250))-1, T(COLOR_BG_BCBCBC),  3, T("%X<#BCBCBC>"), 11},
    { CS_BG(251),    CS_BACKGROUND, XTERM_BG(251), sizeof(XTERM_BG(251))-1, T(COLOR_BG_C6C6C6),  3, T("%X<#C6C6C6>"), 11},
    { CS_BG(252),    CS_BACKGROUND, XTERM_BG(252), sizeof(XTERM_BG(252))-1, T(COLOR_BG_D0D0D0),  3, T("%X<#D0D0D0>"), 11},
    { CS_BG(253),    CS_BACKGROUND, XTERM_BG(253), sizeof(XTERM_BG(253))-1, T(COLOR_BG_DADADA),  3, T("%X<#DADADA>"), 11},
    { CS_BG(254),    CS_BACKGROUND, XTERM_BG(254), sizeof(XTERM_BG(254))-1, T(COLOR_BG_E4E4E4),  3, T("%X<#E4E4E4>"), 11},
    { CS_BG(255),    CS_BACKGROUND, XTERM_BG(255), sizeof(XTERM_BG(255))-1, T(COLOR_BG_EEEEEE),  3, T("%X<#EEEEEE>"), 11},
};

// We want to remove mal-formed ESC sequences completely and convert the
// well-formed ones.
//
UTF8 *ConvertToUTF8(const char *p)
{
    static UTF8 aBuffer[LBUF_SIZE];
    UTF8 *pBuffer = aBuffer;

    while ('\0' != *p)
    {
        if (ESC_CHAR != *p)
        {
            const UTF8 *q = latin1_utf8[(unsigned char)*p];
            utf8_safe_chr(q, aBuffer, &pBuffer);
            p++;
        }
        else
        {
            // We have an ANSI sequence.
            //
            p++;
            if ('[' == *p)
            {
                p++;
                const char *q = p;
                while (ANSI_TokenTerminatorTable[(unsigned char)*q] == 0)
                {
                    q++;
                }

                if ('\0' != q[0])
                {
                    // The segment [p,q) should contain a list of semi-color delimited codes.
                    //
                    const char *r = p;
                    while (r != q)
                    {
                        while (  r != q
                              && ';' != r[0])
                        {
                            r++;
                        }

                        // The segment [p,r) should contain one code.
                        //
                        size_t n = r - p;
                        const UTF8 *s = NULL;
                        switch (n)
                        {
                        case 1:
                            if ('0' == *p)
                            {
                                s = aColors[COLOR_INDEX_RESET].pUTF;
                            }
                            else if ('1' == *p)
                            {
                                s = aColors[COLOR_INDEX_INTENSE].pUTF;
                            }
                            else if ('4' == *p)
                            {
                                s = aColors[COLOR_INDEX_UNDERLINE].pUTF;
                            }
                            else if ('5' == *p)
                            {
                                s = aColors[COLOR_INDEX_BLINK].pUTF;
                            }
                            else if ('7' == *p)
                            {
                                s = aColors[COLOR_INDEX_INVERSE].pUTF;
                            }
                            break;

                        case 2:
                            if ('3' == *p)
                            {
                                unsigned int iCode = COLOR_INDEX_FG + (p[1] - '0');
                                if (  COLOR_INDEX_FG <= iCode
                                   && iCode < COLOR_INDEX_BG)
                                {
                                    s = aColors[iCode].pUTF;
                                }
                            }
                            else if ('4' == *p)
                            {
                                unsigned int iCode = COLOR_INDEX_BG + (p[1] - '0');
                                if (  COLOR_INDEX_BG <= iCode
                                   && iCode < sizeof(aColors)/sizeof(aColors[0]))
                                {
                                    s = aColors[iCode].pUTF;
                                }
                            }
                            break;
                        }

                        if (NULL != s)
                        {
                            utf8_safe_chr(s, aBuffer, &pBuffer);
                        }

                        while (  r != q
                              && ';' == r[0])
                        {
                            r++;
                        }
                        p = r;
                    }

                    // Eat trailing terminator.
                    //
                    p = q + 1;
                }
                else
                {
                    // Skip to end of mal-formed ANSI sequence.
                    //
                    p = q;
                }
            }
        }
    }
    *pBuffer = '\0';
    return aBuffer;
}

bool T5X_GAME::Upgrade4()
{
    Upgrade3();
    int ver = (m_flags & T5X_V_MASK);
    if (4 == ver)
    {
        return false;
    }
    m_flags &= ~T5X_V_MASK;

    // Additional flatfile flags.
    //
    m_flags |= 4;

    return true;
}

bool T5X_GAME::Upgrade3()
{
    int ver = (m_flags & T5X_V_MASK);
    if (3 == ver)
    {
        return false;
    }
    m_flags &= ~T5X_V_MASK;

    // Additional flatfile flags.
    //
    m_flags |= T5X_V_ATRKEY | 3;

    // Upgrade attribute names.
    //
    map<int, T5X_ATTRNAMEINFO *, lti> new_mAttrNames;
    m_mAttrNums.clear();
    for (map<int, T5X_ATTRNAMEINFO *, lti>::iterator it =  m_mAttrNames.begin(); it != m_mAttrNames.end(); ++it)
    {
        T5X_ATTRNAMEINFO *pani = it->second;
        it->second = NULL;
        pani->ConvertToUTF8();
        new_mAttrNames[pani->m_iNum] = pani;

        map<char *, T5X_ATTRNAMEINFO *, ltstr>::iterator itNum = m_mAttrNums.find(pani->m_pNameUnencoded);
        if (itNum != m_mAttrNums.end())
        {
            fprintf(stderr, "WARNING: Duplicate attribute name %s(%d) conflicts with %s(%d)\n",
                pani->m_pNameUnencoded, pani->m_iNum, itNum->second->m_pNameUnencoded, itNum->second->m_iNum);
        }
        else
        {
            m_mAttrNums[pani->m_pNameUnencoded] = pani;
        }
    }
    m_mAttrNames = new_mAttrNames;

    // Upgrade objects.
    //
    for (map<int, T5X_OBJECTINFO *, lti>::iterator it = m_mObjects.begin(); it != m_mObjects.end(); ++it)
    {
        it->second->UpgradeDefaultLock();
        it->second->ConvertToUTF8();
    }
    return true;
}

void T5X_ATTRNAMEINFO::ConvertToUTF8()
{
    char *p = (char *)::ConvertToUTF8(m_pNameUnencoded);
    SetNumFlagsAndName(m_iNum, m_iFlags, StringClone(p));
}

void T5X_OBJECTINFO::UpgradeDefaultLock()
{
    // Convert default Lock to attribute value
    //
    if (NULL != m_ple)
    {
        char buffer[65536];
        char *p = m_ple->Write(buffer);
        *p = '\0';

        // Add it.
        //
        T5X_ATTRINFO *pai = new T5X_ATTRINFO;
        pai->SetNumAndValue(T5X_A_LOCK, StringClone(buffer));

        if (NULL == m_pvai)
        {
            vector<T5X_ATTRINFO *> *pvai = new vector<T5X_ATTRINFO *>;
            pvai->push_back(pai);
            SetAttrs(pvai->size(), pvai);
        }
        else
        {
            m_pvai->push_back(pai);
            m_fAttrCount = true;
            m_nAttrCount = m_pvai->size();
        }

        delete m_ple;
        m_ple = NULL;
    }
}

void T5X_OBJECTINFO::ConvertToUTF8()
{
    // Convert name
    //
    char *p = (char *)::ConvertToUTF8(m_pName);
    free(m_pName);
    m_pName = StringClone(p);

    // Convert attribute values.
    //
    if (NULL != m_pvai)
    {
        for (vector<T5X_ATTRINFO *>::iterator it = m_pvai->begin(); it != m_pvai->end(); ++it)
        {
            (*it)->ConvertToUTF8();
        }
    }
}

void T5X_ATTRINFO::ConvertToUTF8()
{
    if (kEncode == m_kState)
    {
        char *p = (char *)::ConvertToUTF8(m_pValueUnencoded);
        SetNumOwnerFlagsAndValue(m_iNum, m_dbOwner, m_iFlags, StringClone(p));
    }
    else
    {
        char *p = (char *)::ConvertToUTF8(m_pValueEncoded);
        SetNumAndValue(m_iNum, StringClone(p));
    }
}

bool T5X_GAME::Upgrade2()
{
    int ver = (m_flags & T5X_V_MASK);
    if (2 <= ver)
    {
        return false;
    }
    m_flags &= ~T5X_V_MASK;

    // Additional flatfile flags.
    //
    m_flags |= T5X_V_ATRKEY | 2;
    return true;
}

// utf/tr_Color.txt
//
// 2053 code points.
// 37 states, 67 columns, 4820 bytes
//
#define TR_COLOR_START_STATE (0)
#define TR_COLOR_ACCEPTING_STATES_START (37)
extern const unsigned char tr_color_itt[256];
extern const unsigned short tr_color_sot[37];
extern const unsigned short tr_color_sbt[2245];

inline int mux_color(const unsigned char *p)
{
    unsigned short iState = TR_COLOR_START_STATE;
    do
    {
        unsigned char ch = *p++;
        unsigned char iColumn = tr_color_itt[(unsigned char)ch];
        unsigned short iOffset = tr_color_sot[iState];
        for (;;)
        {
            int y = tr_color_sbt[iOffset];
            if (y < 128)
            {
                // RUN phrase.
                //
                if (iColumn < y)
                {
                    iState = tr_color_sbt[iOffset+1];
                    break;
                }
                else
                {
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset += 2;
                }
            }
            else
            {
                // COPY phrase.
                //
                y = 256-y;
                if (iColumn < y)
                {
                    iState = tr_color_sbt[iOffset+iColumn+1];
                    break;
                }
                else
                {
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset = static_cast<unsigned short>(iOffset + y + 1);
                }
            }
        }
    } while (iState < TR_COLOR_ACCEPTING_STATES_START);
    return iState - TR_COLOR_ACCEPTING_STATES_START;
}

#define COLOR_NOTCOLOR   0

#define utf8_NextCodePoint(x)      (x + utf8_FirstByte[(unsigned char)*x])

// utf/tr_Color.txt
//
// 2053 code points.
// 37 states, 67 columns, 4820 bytes
//
const unsigned char tr_color_itt[256] =
{
       0,   0,   0,   0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0,

       1,   2,   3,   4,   5,   6,   7,   8,    9,  10,  11,  12,  13,  14,  15,  16,
      17,  18,  19,  20,  21,  22,  23,  24,   25,  26,  27,  28,  29,  30,  31,  32,
      33,  34,  35,  36,  37,  38,  39,  40,   41,  42,  43,  44,  45,  46,  47,  48,
      49,  50,  51,  52,  53,  54,  55,  56,   57,  58,  59,  60,  61,  62,  63,  64,
       0,   0,   0,   0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0,  65,
       0,   0,   0,  66,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0

};

const unsigned short tr_color_sot[37] =
{
        0,    5,   22,   35,  103,  171,  239,  307,   375,  443,  511,  579,  585,  613,  681,  749,
      817,  885,  953, 1021, 1089, 1157, 1225, 1293,  1361, 1429, 1497, 1565, 1633, 1701, 1769, 1837,
     1905, 1973, 2041, 2109, 2177
};

const unsigned short tr_color_sbt[2245] =
{
      65,  37, 254,   1,  11,  21,  37, 255,    2,   3,  37, 248,   3,   4,   5,   6,
       7,   8,   9,  10,  34,  37, 253,  37,   38,  39,   2,  37, 252,  40,  41,  37,
      42,  58,  37, 191,  37,  43,  44,  45,   46,  47,  48,  49,  50,  51,  52,  53,
      54,  55,  56,  57,  58,  59,  60,  61,   62,  63,  64,  65,  66,  67,  68,  69,
      70,  71,  72,  73,  74,  75,  76,  77,   78,  79,  80,  81,  82,  83,  84,  85,
      86,  87,  88,  89,  90,  91,  92,  93,   94,  95,  96,  97,  98,  99, 100, 101,
     102, 103, 104, 105, 106,   2,  37, 191,   37, 107, 108, 109, 110, 111, 112, 113,
     114, 115, 116, 117, 118, 119, 120, 121,  122, 123, 124, 125, 126, 127, 128, 129,
     130, 131, 132, 133, 134, 135, 136, 137,  138, 139, 140, 141, 142, 143, 144, 145,
     146, 147, 148, 149, 150, 151, 152, 153,  154, 155, 156, 157, 158, 159, 160, 161,
     162, 163, 164, 165, 166, 167, 168, 169,  170,   2,  37, 191,  37, 171, 172, 173,
     174, 175, 176, 177, 178, 179, 180, 181,  182, 183, 184, 185, 186, 187, 188, 189,
     190, 191, 192, 193, 194, 195, 196, 197,  198, 199, 200, 201, 202, 203, 204, 205,
     206, 207, 208, 209, 210, 211, 212, 213,  214, 215, 216, 217, 218, 219, 220, 221,
     222, 223, 224, 225, 226, 227, 228, 229,  230, 231, 232, 233, 234,   2,  37, 191,
      37, 235, 236, 237, 238, 239, 240, 241,  242, 243, 244, 245, 246, 247, 248, 249,
     250, 251, 252, 253, 254, 255, 256, 257,  258, 259, 260, 261, 262, 263, 264, 265,
     266, 267, 268, 269, 270, 271, 272, 273,  274, 275, 276, 277, 278, 279, 280, 281,
     282, 283, 284, 285, 286, 287, 288, 289,  290, 291, 292, 293, 294, 295, 296, 297,
     298,   2,  37, 191,  37, 299, 300, 301,  302, 303, 304, 305, 306, 307, 308, 309,
     310, 311, 312, 313, 314, 315, 316, 317,  318, 319, 320, 321, 322, 323, 324, 325,
     326, 327, 328, 329, 330, 331, 332, 333,  334, 335, 336, 337, 338, 339, 340, 341,
     342, 343, 344, 345, 346, 347, 348, 349,  350, 351, 352, 353, 354, 355, 356, 357,
     358, 359, 360, 361, 362,   2,  37, 191,   37, 363, 364, 365, 366, 367, 368, 369,
     370, 371, 372, 373, 374, 375, 376, 377,  378, 379, 380, 381, 382, 383, 384, 385,
     386, 387, 388, 389, 390, 391, 392, 393,  394, 395, 396, 397, 398, 399, 400, 401,
     402, 403, 404, 405, 406, 407, 408, 409,  410, 411, 412, 413, 414, 415, 416, 417,
     418, 419, 420, 421, 422, 423, 424, 425,  426,   2,  37, 191,  37, 427, 428, 429,
     430, 431, 432, 433, 434, 435, 436, 437,  438, 439, 440, 441, 442, 443, 444, 445,
     446, 447, 448, 449, 450, 451, 452, 453,  454, 455, 456, 457, 458, 459, 460, 461,
     462, 463, 464, 465, 466, 467, 468, 469,  470, 471, 472, 473, 474, 475, 476, 477,
     478, 479, 480, 481, 482, 483, 484, 485,  486, 487, 488, 489, 490,   2,  37, 191,
      37, 491, 492, 493, 494, 495, 496, 497,  498, 499, 500, 501, 502, 503, 504, 505,
     506, 507, 508, 509, 510, 511, 512, 513,  514, 515, 516, 517, 518, 519, 520, 521,
     522, 523, 524, 525, 526, 527, 528, 529,  530, 531, 532, 533, 534, 535, 536, 537,
     538, 539, 540, 541, 542, 543, 544, 545,  546, 547, 548, 549, 550, 551, 552, 553,
     554,   2,  37,  49,  37, 255,  12,  17,   37, 231,  37,  13,  14,  15,  16,  17,
      18,  19,  20,  21,  22,  23,  24,  25,   26,  27,  28,  29,  30,  31,  32,  33,
      34,  35,  36,  42,  37, 191,  37, 555,  556, 557, 558, 559, 560, 561, 562, 563,
     564, 565, 566, 567, 568, 569, 570, 571,  572, 573, 574, 575, 576, 577, 578, 579,
     580, 581, 582, 583, 584, 585, 586, 587,  588, 589, 590, 591, 592, 593, 594, 595,
     596, 597, 598, 599, 600, 601, 602, 603,  604, 605, 606, 607, 608, 609, 610, 611,
     612, 613, 614, 615, 616, 617, 618,   2,   37, 191,  37, 619, 620, 621, 622, 623,
     624, 625, 626, 627, 628, 629, 630, 631,  632, 633, 634, 635, 636, 637, 638, 639,
     640, 641, 642, 643, 644, 645, 646, 647,  648, 649, 650, 651, 652, 653, 654, 655,
     656, 657, 658, 659, 660, 661, 662, 663,  664, 665, 666, 667, 668, 669, 670, 671,
     672, 673, 674, 675, 676, 677, 678, 679,  680, 681, 682,   2,  37, 191,  37, 683,
     684, 685, 686, 687, 688, 689, 690, 691,  692, 693, 694, 695, 696, 697, 698, 699,
     700, 701, 702, 703, 704, 705, 706, 707,  708, 709, 710, 711, 712, 713, 714, 715,
     716, 717, 718, 719, 720, 721, 722, 723,  724, 725, 726, 727, 728, 729, 730, 731,
     732, 733, 734, 735, 736, 737, 738, 739,  740, 741, 742, 743, 744, 745, 746,   2,
      37, 191,  37, 747, 748, 749, 750, 751,  752, 753, 754, 755, 756, 757, 758, 759,
     760, 761, 762, 763, 764, 765, 766, 767,  768, 769, 770, 771, 772, 773, 774, 775,
     776, 777, 778, 779, 780, 781, 782, 783,  784, 785, 786, 787, 788, 789, 790, 791,
     792, 793, 794, 795, 796, 797, 798, 799,  800, 801, 802, 803, 804, 805, 806, 807,
     808, 809, 810,   2,  37, 191,  37, 811,  812, 813, 814, 815, 816, 817, 818, 819,
     820, 821, 822, 823, 824, 825, 826, 827,  828, 829, 830, 831, 832, 833, 834, 835,
     836, 837, 838, 839, 840, 841, 842, 843,  844, 845, 846, 847, 848, 849, 850, 851,
     852, 853, 854, 855, 856, 857, 858, 859,  860, 861, 862, 863, 864, 865, 866, 867,
     868, 869, 870, 871, 872, 873, 874,   2,   37, 191,  37, 875, 876, 877, 878, 879,
     880, 881, 882, 883, 884, 885, 886, 887,  888, 889, 890, 891, 892, 893, 894, 895,
     896, 897, 898, 899, 900, 901, 902, 903,  904, 905, 906, 907, 908, 909, 910, 911,
     912, 913, 914, 915, 916, 917, 918, 919,  920, 921, 922, 923, 924, 925, 926, 927,
     928, 929, 930, 931, 932, 933, 934, 935,  936, 937, 938,   2,  37, 191,  37, 939,
     940, 941, 942, 943, 944, 945, 946, 947,  948, 949, 950, 951, 952, 953, 954, 955,
     956, 957, 958, 959, 960, 961, 962, 963,  964, 965, 966, 967, 968, 969, 970, 971,
     972, 973, 974, 975, 976, 977, 978, 979,  980, 981, 982, 983, 984, 985, 986, 987,
     988, 989, 990, 991, 992, 993, 994, 995,  996, 997, 998, 999, 1000, 1001, 1002,   2,
      37, 191,  37, 1003, 1004, 1005, 1006, 1007,  1008, 1009, 1010, 1011, 1012, 1013, 1014, 1015,
     1016, 1017, 1018, 1019, 1020, 1021, 1022, 1023,  1024, 1025, 1026, 1027, 1028, 1029, 1030, 1031,
     1032, 1033, 1034, 1035, 1036, 1037, 1038, 1039,  1040, 1041, 1042, 1043, 1044, 1045, 1046, 1047,
     1048, 1049, 1050, 1051, 1052, 1053, 1054, 1055,  1056, 1057, 1058, 1059, 1060, 1061, 1062, 1063,
     1064, 1065, 1066,   2,  37, 191,  37, 1067,  1068, 1069, 1070, 1071, 1072, 1073, 1074, 1075,
     1076, 1077, 1078, 1079, 1080, 1081, 1082, 1083,  1084, 1085, 1086, 1087, 1088, 1089, 1090, 1091,
     1092, 1093, 1094, 1095, 1096, 1097, 1098, 1099,  1100, 1101, 1102, 1103, 1104, 1105, 1106, 1107,
     1108, 1109, 1110, 1111, 1112, 1113, 1114, 1115,  1116, 1117, 1118, 1119, 1120, 1121, 1122, 1123,
     1124, 1125, 1126, 1127, 1128, 1129, 1130,   2,   37, 191,  37, 1131, 1132, 1133, 1134, 1135,
     1136, 1137, 1138, 1139, 1140, 1141, 1142, 1143,  1144, 1145, 1146, 1147, 1148, 1149, 1150, 1151,
     1152, 1153, 1154, 1155, 1156, 1157, 1158, 1159,  1160, 1161, 1162, 1163, 1164, 1165, 1166, 1167,
     1168, 1169, 1170, 1171, 1172, 1173, 1174, 1175,  1176, 1177, 1178, 1179, 1180, 1181, 1182, 1183,
     1184, 1185, 1186, 1187, 1188, 1189, 1190, 1191,  1192, 1193, 1194,   2,  37, 191,  37, 1195,
     1196, 1197, 1198, 1199, 1200, 1201, 1202, 1203,  1204, 1205, 1206, 1207, 1208, 1209, 1210, 1211,
     1212, 1213, 1214, 1215, 1216, 1217, 1218, 1219,  1220, 1221, 1222, 1223, 1224, 1225, 1226, 1227,
     1228, 1229, 1230, 1231, 1232, 1233, 1234, 1235,  1236, 1237, 1238, 1239, 1240, 1241, 1242, 1243,
     1244, 1245, 1246, 1247, 1248, 1249, 1250, 1251,  1252, 1253, 1254, 1255, 1256, 1257, 1258,   2,
      37, 191,  37, 1259, 1260, 1261, 1262, 1263,  1264, 1265, 1266, 1267, 1268, 1269, 1270, 1271,
     1272, 1273, 1274, 1275, 1276, 1277, 1278, 1279,  1280, 1281, 1282, 1283, 1284, 1285, 1286, 1287,
     1288, 1289, 1290, 1291, 1292, 1293, 1294, 1295,  1296, 1297, 1298, 1299, 1300, 1301, 1302, 1303,
     1304, 1305, 1306, 1307, 1308, 1309, 1310, 1311,  1312, 1313, 1314, 1315, 1316, 1317, 1318, 1319,
     1320, 1321, 1322,   2,  37, 191,  37, 1323,  1324, 1325, 1326, 1327, 1328, 1329, 1330, 1331,
     1332, 1333, 1334, 1335, 1336, 1337, 1338, 1339,  1340, 1341, 1342, 1343, 1344, 1345, 1346, 1347,
     1348, 1349, 1350, 1351, 1352, 1353, 1354, 1355,  1356, 1357, 1358, 1359, 1360, 1361, 1362, 1363,
     1364, 1365, 1366, 1367, 1368, 1369, 1370, 1371,  1372, 1373, 1374, 1375, 1376, 1377, 1378, 1379,
     1380, 1381, 1382, 1383, 1384, 1385, 1386,   2,   37, 191,  37, 1387, 1388, 1389, 1390, 1391,
     1392, 1393, 1394, 1395, 1396, 1397, 1398, 1399,  1400, 1401, 1402, 1403, 1404, 1405, 1406, 1407,
     1408, 1409, 1410, 1411, 1412, 1413, 1414, 1415,  1416, 1417, 1418, 1419, 1420, 1421, 1422, 1423,
     1424, 1425, 1426, 1427, 1428, 1429, 1430, 1431,  1432, 1433, 1434, 1435, 1436, 1437, 1438, 1439,
     1440, 1441, 1442, 1443, 1444, 1445, 1446, 1447,  1448, 1449, 1450,   2,  37, 191,  37, 1451,
     1452, 1453, 1454, 1455, 1456, 1457, 1458, 1459,  1460, 1461, 1462, 1463, 1464, 1465, 1466, 1467,
     1468, 1469, 1470, 1471, 1472, 1473, 1474, 1475,  1476, 1477, 1478, 1479, 1480, 1481, 1482, 1483,
     1484, 1485, 1486, 1487, 1488, 1489, 1490, 1491,  1492, 1493, 1494, 1495, 1496, 1497, 1498, 1499,
     1500, 1501, 1502, 1503, 1504, 1505, 1506, 1507,  1508, 1509, 1510, 1511, 1512, 1513, 1514,   2,
      37, 191,  37, 1515, 1516, 1517, 1518, 1519,  1520, 1521, 1522, 1523, 1524, 1525, 1526, 1527,
     1528, 1529, 1530, 1531, 1532, 1533, 1534, 1535,  1536, 1537, 1538, 1539, 1540, 1541, 1542, 1543,
     1544, 1545, 1546, 1547, 1548, 1549, 1550, 1551,  1552, 1553, 1554, 1555, 1556, 1557, 1558, 1559,
     1560, 1561, 1562, 1563, 1564, 1565, 1566, 1567,  1568, 1569, 1570, 1571, 1572, 1573, 1574, 1575,
     1576, 1577, 1578,   2,  37, 191,  37, 1579,  1580, 1581, 1582, 1583, 1584, 1585, 1586, 1587,
     1588, 1589, 1590, 1591, 1592, 1593, 1594, 1595,  1596, 1597, 1598, 1599, 1600, 1601, 1602, 1603,
     1604, 1605, 1606, 1607, 1608, 1609, 1610, 1611,  1612, 1613, 1614, 1615, 1616, 1617, 1618, 1619,
     1620, 1621, 1622, 1623, 1624, 1625, 1626, 1627,  1628, 1629, 1630, 1631, 1632, 1633, 1634, 1635,
     1636, 1637, 1638, 1639, 1640, 1641, 1642,   2,   37, 191,  37, 1643, 1644, 1645, 1646, 1647,
     1648, 1649, 1650, 1651, 1652, 1653, 1654, 1655,  1656, 1657, 1658, 1659, 1660, 1661, 1662, 1663,
     1664, 1665, 1666, 1667, 1668, 1669, 1670, 1671,  1672, 1673, 1674, 1675, 1676, 1677, 1678, 1679,
     1680, 1681, 1682, 1683, 1684, 1685, 1686, 1687,  1688, 1689, 1690, 1691, 1692, 1693, 1694, 1695,
     1696, 1697, 1698, 1699, 1700, 1701, 1702, 1703,  1704, 1705, 1706,   2,  37, 191,  37, 1707,
     1708, 1709, 1710, 1711, 1712, 1713, 1714, 1715,  1716, 1717, 1718, 1719, 1720, 1721, 1722, 1723,
     1724, 1725, 1726, 1727, 1728, 1729, 1730, 1731,  1732, 1733, 1734, 1735, 1736, 1737, 1738, 1739,
     1740, 1741, 1742, 1743, 1744, 1745, 1746, 1747,  1748, 1749, 1750, 1751, 1752, 1753, 1754, 1755,
     1756, 1757, 1758, 1759, 1760, 1761, 1762, 1763,  1764, 1765, 1766, 1767, 1768, 1769, 1770,   2,
      37, 191,  37, 1771, 1772, 1773, 1774, 1775,  1776, 1777, 1778, 1779, 1780, 1781, 1782, 1783,
     1784, 1785, 1786, 1787, 1788, 1789, 1790, 1791,  1792, 1793, 1794, 1795, 1796, 1797, 1798, 1799,
     1800, 1801, 1802, 1803, 1804, 1805, 1806, 1807,  1808, 1809, 1810, 1811, 1812, 1813, 1814, 1815,
     1816, 1817, 1818, 1819, 1820, 1821, 1822, 1823,  1824, 1825, 1826, 1827, 1828, 1829, 1830, 1831,
     1832, 1833, 1834,   2,  37, 191,  37, 1835,  1836, 1837, 1838, 1839, 1840, 1841, 1842, 1843,
     1844, 1845, 1846, 1847, 1848, 1849, 1850, 1851,  1852, 1853, 1854, 1855, 1856, 1857, 1858, 1859,
     1860, 1861, 1862, 1863, 1864, 1865, 1866, 1867,  1868, 1869, 1870, 1871, 1872, 1873, 1874, 1875,
     1876, 1877, 1878, 1879, 1880, 1881, 1882, 1883,  1884, 1885, 1886, 1887, 1888, 1889, 1890, 1891,
     1892, 1893, 1894, 1895, 1896, 1897, 1898,   2,   37, 191,  37, 1899, 1900, 1901, 1902, 1903,
     1904, 1905, 1906, 1907, 1908, 1909, 1910, 1911,  1912, 1913, 1914, 1915, 1916, 1917, 1918, 1919,
     1920, 1921, 1922, 1923, 1924, 1925, 1926, 1927,  1928, 1929, 1930, 1931, 1932, 1933, 1934, 1935,
     1936, 1937, 1938, 1939, 1940, 1941, 1942, 1943,  1944, 1945, 1946, 1947, 1948, 1949, 1950, 1951,
     1952, 1953, 1954, 1955, 1956, 1957, 1958, 1959,  1960, 1961, 1962,   2,  37, 191,  37, 1963,
     1964, 1965, 1966, 1967, 1968, 1969, 1970, 1971,  1972, 1973, 1974, 1975, 1976, 1977, 1978, 1979,
     1980, 1981, 1982, 1983, 1984, 1985, 1986, 1987,  1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995,
     1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003,  2004, 2005, 2006, 2007, 2008, 2009, 2010, 2011,
     2012, 2013, 2014, 2015, 2016, 2017, 2018, 2019,  2020, 2021, 2022, 2023, 2024, 2025, 2026,   2,
      37, 191,  37, 2027, 2028, 2029, 2030, 2031,  2032, 2033, 2034, 2035, 2036, 2037, 2038, 2039,
     2040, 2041, 2042, 2043, 2044, 2045, 2046, 2047,  2048, 2049, 2050, 2051, 2052, 2053, 2054, 2055,
     2056, 2057, 2058, 2059, 2060, 2061, 2062, 2063,  2064, 2065, 2066, 2067, 2068, 2069, 2070, 2071,
     2072, 2073, 2074, 2075, 2076, 2077, 2078, 2079,  2080, 2081, 2082, 2083, 2084, 2085, 2086, 2087,
     2088, 2089, 2090,   2,  37
};

// This function assumes that color depth has already been restricted to ANSI (highlight + 8 colors).
//
UTF8 *ConvertColorToANSI(const UTF8 *pString)
{
    static UTF8 aBuffer[2*LBUF_SIZE];
    UTF8 *pBuffer = aBuffer;
    while (  '\0' != *pString
          && pBuffer < aBuffer + sizeof(aBuffer) - sizeof(ANSI_NORMAL) - 1)
    {
        unsigned int iCode = mux_color(pString);
        if (COLOR_NOTCOLOR == iCode)
        {
            utf8_safe_chr(pString, aBuffer, &pBuffer);
        }
        else
        {
            memcpy(pBuffer, aColors[iCode].pAnsi, aColors[iCode].nAnsi);
            pBuffer += aColors[iCode].nAnsi;
        }
        pString = utf8_NextCodePoint(pString);
    }
    *pBuffer = '\0';
    return aBuffer;
}

// utf/tr_utf8_latin1.txt
//
// 2396 code points.
// 93 states, 192 columns, 7060 bytes
//
#define TR_LATIN1_START_STATE (0)
#define TR_LATIN1_ACCEPTING_STATES_START (93)

const unsigned char tr_latin1_itt[256] =
{
       0,   0,   0,   0,   0,   0,   0,   1,    2,   3,   4,   0,   0,   5,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,    0,   0,   0,   6,   0,   0,   0,   0,
       7,   8,   9,  10,  11,  12,  13,  14,   15,  16,  17,  18,  19,  20,  21,  22,
      23,  24,  25,  26,  27,  28,  29,  30,   31,  32,  33,  34,  35,  36,  37,   0,
      38,  39,  40,  41,  42,  43,  44,  45,   46,  47,  48,  49,  50,  51,  52,  53,
      54,  55,  56,  57,  58,  59,  60,  61,   62,  63,  64,  65,  66,  67,  68,  69,
      70,  71,  72,  73,  74,  75,  76,  77,   78,  79,  80,  81,  82,  83,  84,  85,
      86,  87,  88,  89,  90,  91,  92,  93,   94,  95,  96,  97,  98,  99, 100,   0,

     101, 102, 103, 104, 105, 106, 107, 108,  109, 110, 111, 112, 113, 114, 115, 116,
     117, 118, 119, 120, 121, 122, 123, 124,  125, 126, 127, 128, 129, 130, 131, 132,
     133, 134, 135, 136, 137, 138, 139, 140,  141, 142, 143, 144, 145, 146, 147, 148,
     149, 150, 151, 152, 153, 154, 155, 156,  157, 158, 159, 160, 161, 162, 163, 164,
       0,   0, 165, 166, 167, 168, 169, 170,  171, 172, 173, 174,   0, 175, 176,   0,
     177, 178, 179, 180,   0, 181, 182,   0,    0, 183,   0, 184,   0,   0,   0, 185,
     186, 187, 188, 189,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0, 190,
     191,   0,   0,   0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0

};

const unsigned short tr_latin1_sot[93] =
{
        0,  132,  171,  240,  309,  380,  442,  504,   575,  629,  666,  681,  693,  699,  743,  763,
      777,  806,  814,  820,  837,  854,  873,  904,   921,  936,  977,  992, 1008, 1027, 1044, 1108,
     1171, 1239, 1308, 1377, 1445, 1508, 1518, 1542,  1587, 1623, 1661, 1686, 1739, 1785, 1795, 1808,
     1824, 1834, 1862, 1915, 1977, 1983, 2003, 2027,  2045, 2055, 2065, 2094, 2103, 2119, 2127, 2135,
     2149, 2164, 2183, 2207, 2215, 2223, 2244, 2303,  2371, 2407, 2420, 2430, 2442, 2456, 2462, 2471,
     2495, 2520, 2589, 2658, 2730, 2799, 2868, 2937,  3006, 3075, 3144, 3213, 3254
};

const unsigned short tr_latin1_sbt[3309] =
{
     155, 156, 100, 101, 102, 103, 106, 120,  125, 126, 127, 128, 129, 130, 131, 132,
     133, 134, 135, 136, 137, 138, 139, 140,  141, 142, 143, 144, 145, 146, 147, 148,
     149, 150, 151, 152, 153, 154, 155, 157,  158, 159, 160, 161, 162, 163, 164, 165,
     166, 167, 168, 169, 170, 171, 172, 173,  174, 175, 176, 177, 178, 179, 180, 181,
     182, 183, 184, 185, 186, 187, 188, 189,  190, 191, 192, 193, 194, 195, 196, 197,
     198, 199, 200, 201, 202, 203, 204, 205,  206, 207, 208, 209, 210, 211, 212, 213,
     214, 215, 216, 217, 218, 219,  64, 156,  229,   1,   2,   3,   4,   5,   6,   7,
       8,   9,  10,  11,  12,  13,  14,  15,   16,  17,  18,  19,  20,  21,  22,  25,
      39,  60,  66,  74, 127, 156,   6, 156,  224, 253, 254, 255, 256, 257, 258, 259,
     260, 261, 262, 263, 264, 265, 266, 267,  268, 269, 270, 271, 272, 273, 274, 275,
     276, 277, 278, 279, 280, 281, 282, 283,  284,  27, 156, 101, 156, 192, 285, 286,
     287, 288, 289, 290, 291, 292, 293, 294,  295, 296, 297, 298, 299, 300, 301, 302,
     303, 304, 305, 306, 307, 308, 309, 310,  311, 312, 313, 314, 315, 316, 317, 318,
     319, 320, 321, 322, 323, 324, 325, 326,  327, 328, 329, 330, 331, 332, 333, 334,
     335, 336, 337, 338, 339, 340, 341, 342,  343, 344, 345, 346, 347, 348,  27, 156,
     101, 156, 207, 158, 190, 158, 190, 158,  190, 160, 192, 160, 192, 160, 192, 160,
     192, 161, 193, 161, 193, 162, 194, 162,  194, 162, 194, 162, 194, 162, 194, 164,
     196, 164, 196, 164, 196, 164, 196, 165,  197, 165, 197, 166, 198, 166, 198, 166,
     198, 166, 198, 166,   3, 156, 244, 167,  199, 168, 200, 156, 169, 201, 169, 201,
     169, 201, 169,  27, 156, 101, 156, 248,  201, 169, 201, 171, 203, 171, 203, 171,
       2, 203,   2, 156, 250, 172, 204, 172,  204, 172, 204,   2, 156, 216, 175, 207,
     175, 207, 175, 207, 176, 208, 176, 208,  176, 208, 176, 208, 177, 209, 177, 209,
     177, 209, 178, 210, 178, 210, 178, 210,  178, 210, 178, 210, 178, 210, 180, 212,
     182, 214, 348, 183, 215, 183,   3, 215,  255, 208,  27, 156, 101, 156, 255, 191,
       2, 159, 255, 191,   3, 156, 253, 160,  192, 156,   2, 161, 255, 193,   4, 156,
     253, 163, 195, 164,   3, 156, 252, 166,  168, 200, 201,   2, 156, 254, 171, 203,
       2, 172, 255, 204,   2, 156, 254, 173,  205,   5, 156, 244, 209, 177, 209, 177,
     178, 210, 156, 179, 182, 214, 183, 215,   36, 156, 104, 156, 255, 126,   9, 156,
     223, 158, 190, 166, 198, 172, 204, 178,  210, 178, 210, 178, 210, 178, 210, 178,
     210, 156, 158, 190, 158, 190, 156, 323,  164, 196, 164, 196, 168, 200, 172, 204,
     172, 204,   2, 156, 255, 199,   3, 156,  254, 164, 196,   2, 156, 248, 171, 203,
     158, 190, 156, 323, 172, 204,  27, 156,  101, 156, 228, 158, 190, 158, 190, 162,
     194, 162, 194, 166, 198, 166, 198, 172,  204, 172, 204, 175, 207, 175, 207, 178,
     210, 178, 210, 176, 208, 177, 209,   2,  156, 252, 165, 197, 171, 193,   2, 156,
     237, 183, 215, 158, 190, 162, 194, 172,  204, 172, 204, 172, 204, 172, 204, 182,
     214, 201, 203, 209,   3, 156, 250, 158,  160, 192, 169, 177, 208,  27, 156, 101,
     156, 255, 215,   2, 156, 243, 159, 178,  156, 162, 194, 167, 199, 156, 206, 175,
     207, 182, 214,   3, 156, 253, 191, 156,  192,   2, 193,   8, 156, 255, 196,   5,
     156, 253, 197, 156, 198,   2, 156,   4,  201,   2, 156, 255, 202,   2, 203,   8,
     156,   3, 207,  28, 156, 103, 156, 255,  208,   5, 156, 252, 209, 210, 156, 211,
       4, 156,   2, 215,  11, 156, 255, 199,    2, 156, 255, 206,  15, 156, 252, 197,
     156, 199, 207,   3, 156, 254, 212, 214,   34, 156, 125, 156,   4, 125, 254, 219,
     125,   3, 156, 253, 201, 208, 213,  55,  156, 127, 156,  32, 156, 255, 125,   3,
     156, 255, 152,  28, 156, 105, 156,   2,  125,  85, 156, 105, 156, 253, 162, 156,
     166,   6, 156, 255, 166,   2, 156, 255,  158,   7, 156, 255, 166,   5, 156, 255,
     172,   4, 156, 255, 178,   9, 156, 255,  162,   2, 156, 255, 190,   7, 156, 255,
     198,   5, 156, 255, 204,  28, 156, 104,  156, 255, 210,   9, 156, 255, 194,   6,
     156, 253, 194, 156, 198,   6, 156, 255,  198,  61, 156, 127, 156,  14, 156, 254,
     172, 204,  16, 156, 254, 165, 197,  31,  156, 117, 156, 252, 158, 190, 158, 190,
      14, 156, 250, 166, 198, 166, 198, 172,  204,   4, 156, 248, 162, 194, 178, 210,
     178, 210, 178, 210,  39, 156, 127, 156,    2, 156, 255, 126,  62, 156, 111, 156,
     255, 138,  80, 156, 127, 156,   6, 156,  246, 141, 142, 143, 144, 145, 146, 147,
     148, 149, 150,  49, 156, 127, 156,  22,  156, 246, 141, 142, 143, 144, 145, 146,
     147, 148, 149, 150,  33, 156, 101, 156,  246, 141, 142, 143, 144, 145, 146, 147,
     148, 149, 150,  47, 156, 255, 126,  33,  156, 127, 156,  11, 156, 239,  23, 156,
      23, 156,  23, 156,  23, 156,  23, 156,   23, 156,  23, 156,  23, 156,  23,   3,
     156, 252,  24, 156,  24,  19,  30, 156,  127, 156,  12, 156, 246, 141, 142, 143,
     144, 145, 146, 147, 148, 149, 150,  43,  156, 117, 156, 246, 141, 142, 143, 144,
     145, 146, 147, 148, 149, 150,  65, 156,  102, 156, 255,  26,  11, 156, 255,  27,
      17, 156, 254,  19,  28,   4, 156, 253,   29, 156,  24,   5, 156, 255,  24,   6,
     156, 248,  30,  31,  32, 156,  33,  34,   35,  36,   2, 156, 254,  37,  38,  27,
     156, 101, 156, 246, 141, 142, 143, 144,  145, 146, 147, 148, 149, 150,  81, 156,
     127, 156,  15, 156, 247, 142, 143, 144,  145, 146, 147, 148, 149, 150,  41, 156,
     107, 156, 255, 266,   9, 156, 246, 141,  142, 143, 144, 145, 146, 147, 148, 149,
     150,  65, 156, 105, 156, 244, 126, 156,  141, 142, 143, 144, 145, 146, 147, 148,
     149, 150,  75, 156, 101, 156, 255, 158,    2, 156, 252, 159, 160, 161, 193,   2,
     162, 250, 166, 167, 168, 169, 170, 171,    5, 172,   2, 156,   2, 172, 255, 173,
       2, 175, 255, 177,   3, 178, 252, 170,  179, 180, 183,   9, 156, 236, 158, 156,
     159, 156, 161, 162, 156, 164, 165, 166,  167, 168, 169, 170, 171, 156, 172, 156,
     173, 175,  27, 156, 101, 156, 252, 177,  178, 180, 190,   3, 156, 253, 191, 193,
     194,   3, 156, 250, 196, 156, 200, 202,  156, 204,   3, 156, 253, 205, 209, 210,
       2, 156, 255, 211,   6, 156, 252, 198,  207, 210, 211,   6, 156, 250, 191, 193,
     195, 202, 203, 205,   2, 207, 253, 208,  209, 215,   4, 156, 252, 166, 156, 205,
     178,  28, 156, 101, 156, 237, 191, 193,  195, 196, 200, 201, 202, 203, 205, 207,
     208, 156, 211, 213, 215, 190, 156, 193,  194,   3, 156, 255, 198,   2, 156, 255,
     210,   2, 156,   2, 192, 248, 193, 194,  195, 199, 196, 197, 198, 156,   2, 198,
     255, 199,   3, 201,   2, 202,   3, 203,  248, 204, 156, 208, 156, 209, 210, 156,
     210,   2, 211,   3, 215,  29, 156, 101,  156, 192, 158, 190, 159, 191, 159, 191,
     159, 191, 160, 192, 161, 193, 161, 193,  161, 193, 161, 193, 161, 193, 162, 194,
     162, 194, 162, 194, 162, 194, 162, 194,  163, 195, 164, 196, 165, 197, 165, 197,
     165, 197, 165, 197, 165, 197, 166, 198,  166, 198, 168, 200, 168, 200, 168, 200,
     169, 201, 169, 201, 169, 201, 169, 201,  170, 202,  27, 156, 101, 156, 192, 170,
     202, 170, 202, 171, 203, 171, 203, 171,  203, 171, 203, 172, 204, 172, 204, 172,
     204, 172, 204, 173, 205, 173, 205, 175,  207, 175, 207, 175, 207, 175, 207, 176,
     208, 176, 208, 176, 208, 176, 208, 176,  208, 177, 209, 177, 209, 177, 209, 177,
     209, 178, 210, 178, 210, 178, 210, 178,  210, 178, 210, 179, 211, 179, 211,  27,
     156, 101, 156, 228, 180, 212, 180, 212,  180, 212, 180, 212, 180, 212, 181, 213,
     181, 213, 182, 214, 183, 215, 183, 215,  183, 215, 197, 209, 212, 214, 190, 208,
       4, 156, 224, 158, 190, 158, 190, 158,  190, 158, 190, 158, 190, 158, 190, 158,
     190, 158, 190, 158, 190, 158, 190, 158,  190, 158, 190, 162, 194, 162, 194, 162,
     194, 162, 194,  27, 156, 101, 156, 198,  162, 194, 162, 194, 162, 194, 162, 194,
     166, 198, 166, 198, 172, 204, 172, 204,  172, 204, 172, 204, 172, 204, 172, 204,
     172, 204, 172, 204, 172, 204, 172, 204,  172, 204, 172, 204, 178, 210, 178, 210,
     178, 210, 178, 210, 178, 210, 178, 210,  178, 210, 182, 214, 182, 214, 182, 214,
     182, 214,  33, 156, 127, 156,  35, 156,  253, 125, 156, 125,  27, 156, 101, 156,
       2, 125,  11, 156,   3, 125,  13, 156,    3, 125,  13, 156,   2, 125, 255, 189,
      13, 156,   2, 125,  28, 156, 101, 156,  250,  40,  41,  42, 156,  43,  44,   2,
     156, 251,  45,  46,  47, 156,  48,   4,  156, 253,  49,  50,  51,   8, 156, 253,
      52,  53,  54,   9, 156, 255,  55,   2,  156, 255,  56,   4, 156, 253,  57,  58,
      59,  40, 156, 101, 156,  11, 125,   5,  156,   2, 138, 255, 156,   2, 138,   2,
     156, 255, 125,   4, 132,   4, 127,   4,  156, 255, 139,  10, 156, 255, 125,  12,
     156, 253, 126, 156, 125,  28, 156, 104,  156, 255, 138,   4, 156, 255, 126,   9,
     156, 255, 138,  12, 156, 255, 125,  16,  156, 254, 141, 198,   2, 156, 244, 145,
     146, 147, 148, 149, 150, 136, 266, 154,  133, 134, 203,  27, 156, 101, 156, 236,
     141, 142, 143, 144, 145, 146, 147, 148,  149, 150, 136, 266, 154, 133, 134, 156,
     190, 194, 204, 213,  71, 156, 103, 156,  255, 160,   7, 156, 255, 196,   3, 165,
     254, 197, 156,   2, 166, 252, 169, 201,  156, 171,   3, 156, 254, 173, 174,   3,
     175,   6, 156, 255, 183,   3, 156, 243,  183, 156, 168, 158, 159, 160, 156, 194,
     162, 163, 156, 170, 204,   4, 156, 255,  198,  33, 156, 106, 156, 251, 161, 193,
     194, 198, 199,  21, 156, 254, 142, 166,    3, 156, 255, 179,   4, 156, 255, 181,
       2, 156, 251, 169, 160, 161, 170, 198,    3, 156, 255, 211,   4, 156, 255, 213,
       2, 156, 252, 201, 192, 193, 202,  27,  156, 119, 156, 255, 138,   2, 156, 255,
     140,  69, 156, 127, 156,   6, 156, 255,  154,  13, 156, 254, 153, 155,  43, 156,
     114, 156, 255, 135,   8, 156, 253, 138,  156, 140,   6, 156, 255, 138,  59, 156,
     117, 156, 255, 265,   8, 156, 255, 265,   65, 156, 127, 156,   6, 156, 247, 142,
     143, 144, 145, 146, 147, 148, 149, 150,   11, 156, 247, 142, 143, 144, 145, 146,
     147, 148, 149, 150,  30, 156, 109, 156,  247, 142, 143, 144, 145, 146, 147, 148,
     149, 150,  11, 156, 220, 190, 191, 192,  193, 194, 195, 196, 197, 198, 199, 200,
     201, 202, 203, 204, 205, 206, 207, 208,  209, 210, 211, 212, 213, 214, 215, 158,
     159, 160, 161, 162, 163, 164, 165, 166,  167,  27, 156, 101, 156, 213, 168, 169,
     170, 171, 172, 173, 174, 175, 176, 177,  178, 179, 180, 181, 182, 183, 190, 191,
     192, 193, 194, 195, 196, 197, 198, 199,  200, 201, 202, 203, 204, 205, 206, 207,
     208, 209, 210, 211, 212, 213, 214, 215,  141,  10, 156, 245, 142, 143, 144, 145,
     146, 147, 148, 149, 150, 156, 141,  27,  156, 122, 156,   2, 135,  68, 156, 127,
     156,   8, 156,   2, 126,  18, 156, 247,  142, 143, 144, 145, 146, 147, 148, 149,
     150,  28, 156, 101, 156, 237, 142, 143,  144, 145, 146, 147, 148, 149, 150, 156,
     142, 143, 144, 145, 146, 147, 148, 149,  150,  72, 156, 127, 156,  22, 156,   2,
     135,   2, 156,   4, 135, 255, 140,   2,  156, 255, 135,  31, 156, 102, 156,   2,
     135,  41, 156,   2, 265,  45, 156, 112,  156, 255, 166,  47, 156, 255, 198,  31,
     156, 127, 156,   6, 156, 243, 169, 201,  169, 173, 175, 190, 209, 165, 197, 168,
     200, 183, 215,   7, 156, 255, 211,   7,  156, 254, 199, 179,  29, 156, 127, 156,
       4, 156, 254, 172, 204,  59, 156, 101,  156, 253,  61, 156,  62,   8, 156, 251,
      63, 156,  64, 156,  65,  75, 156, 101,  156, 253, 125, 137, 139,  88, 156, 127,
     156, 255, 156,   2, 125,  62, 156, 101,  156, 247, 142, 143, 144, 145, 146, 147,
     148, 149, 150,  82, 156, 125, 156, 246,  141, 142, 143, 144, 145, 146, 147, 148,
     149, 150,  57, 156, 101, 156, 254, 200,  170,  30, 156, 247, 142, 143, 144, 145,
     146, 147, 148, 149, 150,  50, 156, 127,  156,  18, 156, 255,  67,   4, 156, 255,
      68,   6, 156, 254,  69,  70,   2, 156,  252,  71,  72, 156,  73,  27, 156, 127,
     156,  15, 156, 255, 136,  49, 156, 127,  156,   4, 156,   6, 125,  55, 156, 117,
     156, 255, 137,   2, 156, 253, 151, 152,  126,  29, 156,   2, 188, 252, 133, 134,
     216, 218,  34, 156, 108, 156, 254, 184,  186,   4, 125,   3, 188, 243, 137, 156,
     139, 156, 152, 151, 156, 126, 156, 133,  134, 216, 218,   2, 156, 243, 128, 131,
     135, 136, 138, 153, 155, 154, 156, 185,  129, 130, 157,   4, 156, 241, 125, 156,
     125, 156, 125, 156, 125, 156, 125, 156,  125, 156, 125, 156, 125,  28, 156, 102,
     156, 193, 126, 127, 128, 129, 130, 131,  132, 133, 134, 135, 136, 137, 138, 139,
     140, 141, 142, 143, 144, 145, 146, 147,  148, 149, 150, 151, 152, 153, 154, 155,
     156, 157, 158, 159, 160, 161, 162, 163,  164, 165, 166, 167, 168, 169, 170, 171,
     172, 173, 174, 175, 176, 177, 178, 179,  180, 181, 182, 183, 184, 185, 186, 187,
     188,  27, 156, 101, 156, 225, 189, 190,  191, 192, 193, 194, 195, 196, 197, 198,
     199, 200, 201, 202, 203, 204, 205, 206,  207, 208, 209, 210, 211, 212, 213, 214,
     215, 216, 217, 218, 219,  60, 156, 127,  156,   6, 156, 250, 255, 256, 265, 125,
     259, 258,  53, 156, 117, 156, 255,  75,   12, 156, 255,  79,  61, 156, 117, 156,
     253,  76,  77,  19,  22, 156, 255,  78,   49, 156, 117, 156, 255, 165,  10, 156,
     255, 301,  28, 156, 255, 197,  34, 156,  104, 156, 255, 333,  87, 156, 101, 156,
     252, 142, 143, 144, 145,  87, 156, 114,  156, 255,  80,   2, 156, 245,  81,  82,
      83,  84,  85,  86,  87,  88,  89,  90,   91,   4, 156, 255,  92,  59, 156, 127,
     156,   6, 156, 238, 142, 143, 144, 145,  146, 147, 148, 149, 150, 142, 143, 144,
     145, 146, 147, 148, 149, 150,  41, 156,  101, 156, 192, 158, 159, 160, 161, 162,
     163, 164, 165, 166, 167, 168, 169, 170,  171, 172, 173, 174, 175, 176, 177, 178,
     179, 180, 181, 182, 183, 190, 191, 192,  193, 194, 195, 196, 197, 198, 199, 200,
     201, 202, 203, 204, 205, 206, 207, 208,  209, 210, 211, 212, 213, 214, 215, 158,
     159, 160, 161, 162, 163, 164, 165, 166,  167, 168, 169,  27, 156, 101, 156, 192,
     170, 171, 172, 173, 174, 175, 176, 177,  178, 179, 180, 181, 182, 183, 190, 191,
     192, 193, 194, 195, 196, 156, 198, 199,  200, 201, 202, 203, 204, 205, 206, 207,
     208, 209, 210, 211, 212, 213, 214, 215,  158, 159, 160, 161, 162, 163, 164, 165,
     166, 167, 168, 169, 170, 171, 172, 173,  174, 175, 176, 177, 178, 179, 180, 181,
      27, 156, 101, 156, 224, 182, 183, 190,  191, 192, 193, 194, 195, 196, 197, 198,
     199, 200, 201, 202, 203, 204, 205, 206,  207, 208, 209, 210, 211, 212, 213, 214,
     215, 158, 156, 160, 161,   2, 156, 255,  164,   2, 156, 254, 167, 168,   2, 156,
     233, 171, 172, 173, 174, 156, 176, 177,  178, 179, 180, 181, 182, 183, 190, 191,
     192, 193, 156, 195, 156, 197, 198, 199,   27, 156, 101, 156, 192, 200, 201, 202,
     203, 156, 205, 206, 207, 208, 209, 210,  211, 212, 213, 214, 215, 158, 159, 160,
     161, 162, 163, 164, 165, 166, 167, 168,  169, 170, 171, 172, 173, 174, 175, 176,
     177, 178, 179, 180, 181, 182, 183, 190,  191, 192, 193, 194, 195, 196, 197, 198,
     199, 200, 201, 202, 203, 204, 205, 206,  207, 208, 209, 210, 211,  27, 156, 101,
     156, 245, 212, 213, 214, 215, 158, 159,  156, 161, 162, 163, 164,   2, 156, 206,
     167, 168, 169, 170, 171, 172, 173, 174,  156, 176, 177, 178, 179, 180, 181, 182,
     156, 190, 191, 192, 193, 194, 195, 196,  197, 198, 199, 200, 201, 202, 203, 204,
     205, 206, 207, 208, 209, 210, 211, 212,  213, 214, 215, 158, 159, 156, 161, 162,
     163, 164,  28, 156, 101, 156, 249, 166,  167, 168, 169, 170, 156, 172,   3, 156,
     202, 176, 177, 178, 179, 180, 181, 182,  156, 190, 191, 192, 193, 194, 195, 196,
     197, 198, 199, 200, 201, 202, 203, 204,  205, 206, 207, 208, 209, 210, 211, 212,
     213, 214, 215, 158, 159, 160, 161, 162,  163, 164, 165, 166, 167, 168, 169, 170,
     171, 172, 173, 174, 175, 176, 177,  27,  156, 101, 156, 192, 178, 179, 180, 181,
     182, 183, 190, 191, 192, 193, 194, 195,  196, 197, 198, 199, 200, 201, 202, 203,
     204, 205, 206, 207, 208, 209, 210, 211,  212, 213, 214, 215, 158, 159, 160, 161,
     162, 163, 164, 165, 166, 167, 168, 169,  170, 171, 172, 173, 174, 175, 176, 177,
     178, 179, 180, 181, 182, 183, 190, 191,  192, 193, 194, 195,  27, 156, 101, 156,
     192, 196, 197, 198, 199, 200, 201, 202,  203, 204, 205, 206, 207, 208, 209, 210,
     211, 212, 213, 214, 215, 158, 159, 160,  161, 162, 163, 164, 165, 166, 167, 168,
     169, 170, 171, 172, 173, 174, 175, 176,  177, 178, 179, 180, 181, 182, 183, 190,
     191, 192, 193, 194, 195, 196, 197, 198,  199, 200, 201, 202, 203, 204, 205, 206,
     207,  27, 156, 101, 156, 192, 208, 209,  210, 211, 212, 213, 214, 215, 158, 159,
     160, 161, 162, 163, 164, 165, 166, 167,  168, 169, 170, 171, 172, 173, 174, 175,
     176, 177, 178, 179, 180, 181, 182, 183,  190, 191, 192, 193, 194, 195, 196, 197,
     198, 199, 200, 201, 202, 203, 204, 205,  206, 207, 208, 209, 210, 211, 212, 213,
     214, 215, 158, 159, 160, 161,  27, 156,  101, 156, 192, 162, 163, 164, 165, 166,
     167, 168, 169, 170, 171, 172, 173, 174,  175, 176, 177, 178, 179, 180, 181, 182,
     183, 190, 191, 192, 193, 194, 195, 196,  197, 198, 199, 200, 201, 202, 203, 204,
     205, 206, 207, 208, 209, 210, 211, 212,  213, 214, 215, 158, 159, 160, 161, 162,
     163, 164, 165, 166, 167, 168, 169, 170,  171, 172, 173,  27, 156, 101, 156, 220,
     174, 175, 176, 177, 178, 179, 180, 181,  182, 183, 190, 191, 192, 193, 194, 195,
     196, 197, 198, 199, 200, 201, 202, 203,  204, 205, 206, 207, 208, 209, 210, 211,
     212, 213, 214, 215,  55, 156, 115, 156,  206, 141, 142, 143, 144, 145, 146, 147,
     148, 149, 150, 141, 142, 143, 144, 145,  146, 147, 148, 149, 150, 141, 142, 143,
     144, 145, 146, 147, 148, 149, 150, 141,  142, 143, 144, 145, 146, 147, 148, 149,
     150, 141, 142, 143, 144, 145, 146, 147,  148, 149, 150,  27, 156
};

// utf/tr_utf8_ascii.txt
//
// 2359 code points.
// 91 states, 192 columns, 3693 bytes
//
#define TR_ASCII_START_STATE (0)
#define TR_ASCII_ACCEPTING_STATES_START (91)

const unsigned char tr_ascii_itt[256] =
{
       0,   0,   0,   0,   0,   0,   0,   1,    2,   3,   4,   0,   0,   5,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,    0,   0,   0,   6,   0,   0,   0,   0,
       7,   8,   9,  10,  11,  12,  13,  14,   15,  16,  17,  18,  19,  20,  21,  22,
      23,  24,  25,  26,  27,  28,  29,  30,   31,  32,  33,  34,  35,  36,  37,   0,
      38,  39,  40,  41,  42,  43,  44,  45,   46,  47,  48,  49,  50,  51,  52,  53,
      54,  55,  56,  57,  58,  59,  60,  61,   62,  63,  64,  65,  66,  67,  68,  69,
      70,  71,  72,  73,  74,  75,  76,  77,   78,  79,  80,  81,  82,  83,  84,  85,
      86,  87,  88,  89,  90,  91,  92,  93,   94,  95,  96,  97,  98,  99, 100,   0,

     101, 102, 103, 104, 105, 106, 107, 108,  109, 110, 111, 112, 113, 114, 115, 116,
     117, 118, 119, 120, 121, 122, 123, 124,  125, 126, 127, 128, 129, 130, 131, 132,
     133, 134, 135, 136, 137, 138, 139, 140,  141, 142, 143, 144, 145, 146, 147, 148,
     149, 150, 151, 152, 153, 154, 155, 156,  157, 158, 159, 160, 161, 162, 163, 164,
       0,   0, 165, 166, 167, 168, 169, 170,  171, 172, 173, 174,   0, 175, 176,   0,
     177, 178, 179, 180,   0, 181, 182,   0,    0, 183,   0, 184,   0,   0,   0, 185,
     186, 187, 188, 189,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0, 190,
     191,   0,   0,   0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0

};

const unsigned short tr_ascii_sot[91] =
{
        0,  132,  165,  215,  284,  355,  417,  481,   552,  606,  643,  658,  670,  676,  720,  740,
      754,  783,  791,  797,  814,  831,  850,  881,   898,  913,  954,  969,  985, 1004, 1021, 1085,
     1148, 1216, 1285, 1354, 1422, 1485, 1495, 1519,  1562, 1598, 1636, 1661, 1714, 1760, 1770, 1783,
     1799, 1827, 1880, 1942, 1948, 1968, 1992, 2010,  2016, 2026, 2055, 2064, 2080, 2088, 2096, 2110,
     2125, 2144, 2168, 2176, 2184, 2205, 2264, 2332,  2368, 2376, 2386, 2398, 2408, 2417, 2441, 2466,
     2535, 2604, 2676, 2745, 2814, 2883, 2952, 3021,  3090, 3159, 3200
};

const unsigned char tr_ascii_sbt[3255] =
{
     155, 154,  98,  99, 100, 101, 104, 118,  123, 124, 125, 126, 127, 128, 129, 130,
     131, 132, 133, 134, 135, 136, 137, 138,  139, 140, 141, 142, 143, 144, 145, 146,
     147, 148, 149, 150, 151, 152, 153, 155,  156, 157, 158, 159, 160, 161, 162, 163,
     164, 165, 166, 167, 168, 169, 170, 171,  172, 173, 174, 175, 176, 177, 178, 179,
     180, 181, 182, 183, 184, 185, 186, 187,  188, 189, 190, 191, 192, 193, 194, 195,
     196, 197, 198, 199, 200, 201, 202, 203,  204, 205, 206, 207, 208, 209, 210, 211,
     212, 213, 214, 215, 216, 217,  64, 154,  229,   1,   2,   3,   4,   5,   6,   7,
       8,   9,  10,  11,  12,  13,  14,  15,   16,  17,  18,  19,  20,  21,  22,  25,
      39,  59,  65,  73, 127, 154,   6, 154,  254, 123, 124,   6, 154, 253, 123, 154,
     188,   2, 154, 253, 136, 154, 123,   2,  154, 253, 141, 142, 123,   3, 154, 253,
     123, 140, 202,  32, 154, 101, 154,   6,  156, 254, 154, 158,   4, 160,   4, 164,
     254, 159, 169,   5, 170, 254, 133, 170,    4, 176, 255, 180,   2, 154,   6, 188,
     254, 154, 190,   4, 192,   4, 196, 254,  191, 201,   5, 202, 254, 138, 202,   4,
     208, 253, 212, 154, 212,  27, 154, 101,  154, 207, 156, 188, 156, 188, 156, 188,
     158, 190, 158, 190, 158, 190, 158, 190,  159, 191, 159, 191, 160, 192, 160, 192,
     160, 192, 160, 192, 160, 192, 162, 194,  162, 194, 162, 194, 162, 194, 163, 195,
     163, 195, 164, 196, 164, 196, 164, 196,  164, 196, 164,   3, 154, 244, 165, 197,
     166, 198, 154, 167, 199, 167, 199, 167,  199, 167,  27, 154, 101, 154, 248, 199,
     167, 199, 169, 201, 169, 201, 169,   2,  201,   2, 154, 250, 170, 202, 170, 202,
     170, 202,   2, 154, 212, 173, 205, 173,  205, 173, 205, 174, 206, 174, 206, 174,
     206, 174, 206, 175, 207, 175, 207, 175,  207, 176, 208, 176, 208, 176, 208, 176,
     208, 176, 208, 176, 208, 178, 210, 180,  212, 180, 181, 213, 181, 213, 181, 213,
     206,  27, 154, 101, 154, 255, 189,   2,  157, 255, 189,   3, 154, 253, 158, 190,
     154,   2, 159, 255, 191,   4, 154, 253,  161, 193, 162,   3, 154, 252, 164, 166,
     198, 199,   2, 154, 254, 169, 201,   2,  170, 255, 202,   2, 154, 254, 171, 203,
       5, 154, 244, 207, 175, 207, 175, 176,  208, 154, 177, 180, 212, 181, 213,  36,
     154, 104, 154, 255, 124,   9, 154, 235,  156, 188, 164, 196, 170, 202, 176, 208,
     176, 208, 176, 208, 176, 208, 176, 208,  154, 156, 188, 156, 188,   2, 154, 246,
     162, 194, 162, 194, 166, 198, 170, 202,  170, 202,   2, 154, 255, 197,   3, 154,
     254, 162, 194,   2, 154, 252, 169, 201,  156, 188,   2, 154, 254, 170, 202,  27,
     154, 101, 154, 228, 156, 188, 156, 188,  160, 192, 160, 192, 164, 196, 164, 196,
     170, 202, 170, 202, 173, 205, 173, 205,  176, 208, 176, 208, 174, 206, 175, 207,
       2, 154, 252, 163, 195, 169, 191,   2,  154, 237, 181, 213, 156, 188, 160, 192,
     170, 202, 170, 202, 170, 202, 170, 202,  180, 212, 199, 201, 207,   3, 154, 250,
     156, 158, 190, 167, 175, 206,  27, 154,  101, 154, 255, 213,   2, 154, 243, 157,
     176, 154, 160, 192, 165, 197, 154, 204,  173, 205, 180, 212,   3, 154, 253, 189,
     154, 190,   2, 191,   8, 154, 255, 194,    5, 154, 253, 195, 154, 196,   2, 154,
       4, 199,   2, 154, 255, 200,   2, 201,    8, 154,   3, 205,  28, 154, 103, 154,
     255, 206,   5, 154, 252, 207, 208, 154,  209,   4, 154,   2, 213,  11, 154, 255,
     197,   2, 154, 255, 204,  15, 154, 252,  195, 154, 197, 205,   3, 154, 254, 210,
     212,  34, 154, 125, 154,   4, 123, 254,  217, 123,   3, 154, 253, 199, 206, 211,
      55, 154, 127, 154,  32, 154, 255, 123,    3, 154, 255, 150,  28, 154, 105, 154,
       2, 123,  85, 154, 105, 154, 253, 160,  154, 164,   6, 154, 255, 164,   2, 154,
     255, 156,   7, 154, 255, 164,   5, 154,  255, 170,   4, 154, 255, 176,   9, 154,
     255, 160,   2, 154, 255, 188,   7, 154,  255, 196,   5, 154, 255, 202,  28, 154,
     104, 154, 255, 208,   9, 154, 255, 192,    6, 154, 253, 192, 154, 196,   6, 154,
     255, 196,  61, 154, 127, 154,  14, 154,  254, 170, 202,  16, 154, 254, 163, 195,
      31, 154, 117, 154, 252, 156, 188, 156,  188,  14, 154, 250, 164, 196, 164, 196,
     170, 202,   4, 154, 248, 160, 192, 176,  208, 176, 208, 176, 208,  39, 154, 127,
     154,   2, 154, 255, 124,  62, 154, 111,  154, 255, 136,  80, 154, 127, 154,   6,
     154, 246, 139, 140, 141, 142, 143, 144,  145, 146, 147, 148,  49, 154, 127, 154,
      22, 154, 246, 139, 140, 141, 142, 143,  144, 145, 146, 147, 148,  33, 154, 101,
     154, 246, 139, 140, 141, 142, 143, 144,  145, 146, 147, 148,  47, 154, 255, 124,
      33, 154, 127, 154,  11, 154, 239,  23,  154,  23, 154,  23, 154,  23, 154,  23,
     154,  23, 154,  23, 154,  23, 154,  23,    3, 154, 252,  24, 154,  24,  19,  30,
     154, 127, 154,  12, 154, 246, 139, 140,  141, 142, 143, 144, 145, 146, 147, 148,
      43, 154, 117, 154, 246, 139, 140, 141,  142, 143, 144, 145, 146, 147, 148,  65,
     154, 102, 154, 255,  26,  11, 154, 255,   27,  17, 154, 254,  19,  28,   4, 154,
     253,  29, 154,  24,   5, 154, 255,  24,    6, 154, 248,  30,  31,  32, 154,  33,
      34,  35,  36,   2, 154, 254,  37,  38,   27, 154, 101, 154, 246, 139, 140, 141,
     142, 143, 144, 145, 146, 147, 148,  81,  154, 127, 154,  15, 154, 247, 140, 141,
     142, 143, 144, 145, 146, 147, 148,  41,  154, 107, 154, 255, 136,   9, 154, 246,
     139, 140, 141, 142, 143, 144, 145, 146,  147, 148,  65, 154, 105, 154, 244, 124,
     154, 139, 140, 141, 142, 143, 144, 145,  146, 147, 148,  75, 154, 101, 154, 255,
     156,   2, 154, 252, 157, 158, 159, 191,    2, 160, 250, 164, 165, 166, 167, 168,
     169,   5, 170,   2, 154,   2, 170, 255,  171,   2, 173, 255, 175,   3, 176, 252,
     168, 177, 178, 181,   9, 154, 236, 156,  154, 157, 154, 159, 160, 154, 162, 163,
     164, 165, 166, 167, 168, 169, 154, 170,  154, 171, 173,  27, 154, 101, 154, 252,
     175, 176, 178, 188,   3, 154, 253, 189,  191, 192,   3, 154, 250, 194, 154, 198,
     200, 154, 202,   3, 154, 253, 203, 207,  208,   2, 154, 255, 209,   6, 154, 252,
     196, 205, 208, 209,   6, 154, 250, 189,  191, 193, 200, 201, 203,   2, 205, 253,
     206, 207, 213,   4, 154, 252, 164, 154,  203, 176,  28, 154, 101, 154, 237, 189,
     191, 193, 194, 198, 199, 200, 201, 203,  205, 206, 154, 209, 211, 213, 188, 154,
     191, 192,   3, 154, 255, 196,   2, 154,  255, 208,   2, 154,   2, 190, 248, 191,
     192, 193, 197, 194, 195, 196, 154,   2,  196, 255, 197,   3, 199,   2, 200,   3,
     201, 248, 202, 154, 206, 154, 207, 208,  154, 208,   2, 209,   3, 213,  29, 154,
     101, 154, 192, 156, 188, 157, 189, 157,  189, 157, 189, 158, 190, 159, 191, 159,
     191, 159, 191, 159, 191, 159, 191, 160,  192, 160, 192, 160, 192, 160, 192, 160,
     192, 161, 193, 162, 194, 163, 195, 163,  195, 163, 195, 163, 195, 163, 195, 164,
     196, 164, 196, 166, 198, 166, 198, 166,  198, 167, 199, 167, 199, 167, 199, 167,
     199, 168, 200,  27, 154, 101, 154, 192,  168, 200, 168, 200, 169, 201, 169, 201,
     169, 201, 169, 201, 170, 202, 170, 202,  170, 202, 170, 202, 171, 203, 171, 203,
     173, 205, 173, 205, 173, 205, 173, 205,  174, 206, 174, 206, 174, 206, 174, 206,
     174, 206, 175, 207, 175, 207, 175, 207,  175, 207, 176, 208, 176, 208, 176, 208,
     176, 208, 176, 208, 177, 209, 177, 209,   27, 154, 101, 154, 228, 178, 210, 178,
     210, 178, 210, 178, 210, 178, 210, 179,  211, 179, 211, 180, 212, 181, 213, 181,
     213, 181, 213, 195, 207, 210, 212, 188,  206,   4, 154, 224, 156, 188, 156, 188,
     156, 188, 156, 188, 156, 188, 156, 188,  156, 188, 156, 188, 156, 188, 156, 188,
     156, 188, 156, 188, 160, 192, 160, 192,  160, 192, 160, 192,  27, 154, 101, 154,
     198, 160, 192, 160, 192, 160, 192, 160,  192, 164, 196, 164, 196, 170, 202, 170,
     202, 170, 202, 170, 202, 170, 202, 170,  202, 170, 202, 170, 202, 170, 202, 170,
     202, 170, 202, 170, 202, 176, 208, 176,  208, 176, 208, 176, 208, 176, 208, 176,
     208, 176, 208, 180, 212, 180, 212, 180,  212, 180, 212,  33, 154, 127, 154,  35,
     154, 253, 123, 154, 123,  27, 154, 101,  154,   2, 123,  11, 154,   3, 123,  13,
     154,   3, 123,  13, 154,   2, 123, 255,  187,  13, 154,   2, 123,  28, 154, 101,
     154, 250,  40,  41,  42, 154,  43,  44,    2, 154, 253,  45,  46,  47,   6, 154,
     253,  48,  49,  50,   8, 154, 253,  51,   52,  53,   9, 154, 255,  54,   2, 154,
     255,  55,   4, 154, 253,  56,  57,  58,   40, 154, 101, 154,  11, 123,   5, 154,
       2, 136, 255, 154,   2, 136,   2, 154,  255, 123,   4, 130,   4, 125,   4, 154,
     255, 137,  10, 154, 255, 123,  12, 154,  253, 124, 154, 123,  28, 154, 104, 154,
     255, 136,   4, 154, 255, 124,   9, 154,  255, 136,  12, 154, 255, 123,  16, 154,
     254, 139, 196,   2, 154, 244, 143, 144,  145, 146, 147, 148, 134, 136, 152, 131,
     132, 201,  27, 154, 101, 154, 236, 139,  140, 141, 142, 143, 144, 145, 146, 147,
     148, 134, 136, 152, 131, 132, 154, 188,  192, 202, 211,  71, 154, 103, 154, 255,
     158,   7, 154, 255, 194,   3, 163, 254,  195, 154,   2, 164, 252, 167, 199, 154,
     169,   3, 154, 254, 171, 172,   3, 173,    6, 154, 255, 181,   3, 154, 243, 181,
     154, 166, 156, 157, 158, 154, 192, 160,  161, 154, 168, 202,   4, 154, 255, 196,
      33, 154, 106, 154, 251, 159, 191, 192,  196, 197,  21, 154, 254, 140, 164,   3,
     154, 255, 177,   4, 154, 255, 179,   2,  154, 251, 167, 158, 159, 168, 196,   3,
     154, 255, 209,   4, 154, 255, 211,   2,  154, 252, 199, 190, 191, 200,  27, 154,
     119, 154, 255, 136,   2, 154, 255, 138,   69, 154, 127, 154,   6, 154, 255, 152,
      13, 154, 254, 151, 153,  43, 154, 114,  154, 255, 133,   8, 154, 253, 136, 154,
     138,   6, 154, 255, 136,  59, 154, 127,  154,   6, 154, 247, 140, 141, 142, 143,
     144, 145, 146, 147, 148,  11, 154, 247,  140, 141, 142, 143, 144, 145, 146, 147,
     148,  30, 154, 109, 154, 247, 140, 141,  142, 143, 144, 145, 146, 147, 148,  11,
     154, 220, 188, 189, 190, 191, 192, 193,  194, 195, 196, 197, 198, 199, 200, 201,
     202, 203, 204, 205, 206, 207, 208, 209,  210, 211, 212, 213, 156, 157, 158, 159,
     160, 161, 162, 163, 164, 165,  27, 154,  101, 154, 213, 166, 167, 168, 169, 170,
     171, 172, 173, 174, 175, 176, 177, 178,  179, 180, 181, 188, 189, 190, 191, 192,
     193, 194, 195, 196, 197, 198, 199, 200,  201, 202, 203, 204, 205, 206, 207, 208,
     209, 210, 211, 212, 213, 139,  10, 154,  245, 140, 141, 142, 143, 144, 145, 146,
     147, 148, 154, 139,  27, 154, 122, 154,    2, 133,  68, 154, 127, 154,   8, 154,
       2, 124,  18, 154, 247, 140, 141, 142,  143, 144, 145, 146, 147, 148,  28, 154,
     101, 154, 237, 140, 141, 142, 143, 144,  145, 146, 147, 148, 154, 140, 141, 142,
     143, 144, 145, 146, 147, 148,  72, 154,  127, 154,  22, 154,   2, 133,   2, 154,
       4, 133, 255, 138,   2, 154, 255, 133,   31, 154, 102, 154,   2, 133,  88, 154,
     112, 154, 255, 164,  47, 154, 255, 196,   31, 154, 127, 154,   6, 154, 243, 167,
     199, 167, 171, 173, 188, 207, 163, 195,  166, 198, 181, 213,   7, 154, 255, 209,
       7, 154, 254, 197, 177,  29, 154, 127,  154,   4, 154, 254, 170, 202,  59, 154,
     101, 154, 253,  60, 154,  61,   8, 154,  251,  62, 154,  63, 154,  64,  75, 154,
     101, 154, 253, 123, 135, 137,  88, 154,  127, 154, 255, 154,   2, 123,  62, 154,
     101, 154, 247, 140, 141, 142, 143, 144,  145, 146, 147, 148,  82, 154, 125, 154,
     246, 139, 140, 141, 142, 143, 144, 145,  146, 147, 148,  57, 154, 101, 154, 254,
     198, 168,  30, 154, 247, 140, 141, 142,  143, 144, 145, 146, 147, 148,  50, 154,
     127, 154,  18, 154, 255,  66,   4, 154,  255,  67,   6, 154, 254,  68,  69,   2,
     154, 252,  70,  71, 154,  72,  27, 154,  127, 154,  15, 154, 255, 134,  49, 154,
     127, 154,   4, 154,   6, 123,  55, 154,  117, 154, 255, 135,   2, 154, 253, 149,
     150, 124,  29, 154,   2, 186, 252, 131,  132, 214, 216,  34, 154, 108, 154, 254,
     182, 184,   4, 123,   3, 186, 243, 135,  154, 137, 154, 150, 149, 154, 124, 154,
     131, 132, 214, 216,   2, 154, 243, 126,  129, 133, 134, 136, 151, 153, 152, 154,
     183, 127, 128, 155,   4, 154, 241, 123,  154, 123, 154, 123, 154, 123, 154, 123,
     154, 123, 154, 123, 154, 123,  28, 154,  102, 154, 193, 124, 125, 126, 127, 128,
     129, 130, 131, 132, 133, 134, 135, 136,  137, 138, 139, 140, 141, 142, 143, 144,
     145, 146, 147, 148, 149, 150, 151, 152,  153, 154, 155, 156, 157, 158, 159, 160,
     161, 162, 163, 164, 165, 166, 167, 168,  169, 170, 171, 172, 173, 174, 175, 176,
     177, 178, 179, 180, 181, 182, 183, 184,  185, 186,  27, 154, 101, 154, 225, 187,
     188, 189, 190, 191, 192, 193, 194, 195,  196, 197, 198, 199, 200, 201, 202, 203,
     204, 205, 206, 207, 208, 209, 210, 211,  212, 213, 214, 215, 216, 217,  60, 154,
     127, 154,   9, 154, 255, 123,  55, 154,  117, 154, 255,  74,  12, 154, 255,  77,
      61, 154, 117, 154, 253,  75, 154,  19,   22, 154, 255,  76,  49, 154, 117, 154,
     255, 163,  39, 154, 255, 195,  34, 154,  101, 154, 252, 140, 141, 142, 143,  87,
     154, 114, 154, 255,  78,   2, 154, 245,   79,  80,  81,  82,  83,  84,  85,  86,
      87,  88,  89,   4, 154, 255,  90,  59,  154, 127, 154,   6, 154, 238, 140, 141,
     142, 143, 144, 145, 146, 147, 148, 140,  141, 142, 143, 144, 145, 146, 147, 148,
      41, 154, 101, 154, 192, 156, 157, 158,  159, 160, 161, 162, 163, 164, 165, 166,
     167, 168, 169, 170, 171, 172, 173, 174,  175, 176, 177, 178, 179, 180, 181, 188,
     189, 190, 191, 192, 193, 194, 195, 196,  197, 198, 199, 200, 201, 202, 203, 204,
     205, 206, 207, 208, 209, 210, 211, 212,  213, 156, 157, 158, 159, 160, 161, 162,
     163, 164, 165, 166, 167,  27, 154, 101,  154, 192, 168, 169, 170, 171, 172, 173,
     174, 175, 176, 177, 178, 179, 180, 181,  188, 189, 190, 191, 192, 193, 194, 154,
     196, 197, 198, 199, 200, 201, 202, 203,  204, 205, 206, 207, 208, 209, 210, 211,
     212, 213, 156, 157, 158, 159, 160, 161,  162, 163, 164, 165, 166, 167, 168, 169,
     170, 171, 172, 173, 174, 175, 176, 177,  178, 179,  27, 154, 101, 154, 224, 180,
     181, 188, 189, 190, 191, 192, 193, 194,  195, 196, 197, 198, 199, 200, 201, 202,
     203, 204, 205, 206, 207, 208, 209, 210,  211, 212, 213, 156, 154, 158, 159,   2,
     154, 255, 162,   2, 154, 254, 165, 166,    2, 154, 233, 169, 170, 171, 172, 154,
     174, 175, 176, 177, 178, 179, 180, 181,  188, 189, 190, 191, 154, 193, 154, 195,
     196, 197,  27, 154, 101, 154, 192, 198,  199, 200, 201, 154, 203, 204, 205, 206,
     207, 208, 209, 210, 211, 212, 213, 156,  157, 158, 159, 160, 161, 162, 163, 164,
     165, 166, 167, 168, 169, 170, 171, 172,  173, 174, 175, 176, 177, 178, 179, 180,
     181, 188, 189, 190, 191, 192, 193, 194,  195, 196, 197, 198, 199, 200, 201, 202,
     203, 204, 205, 206, 207, 208, 209,  27,  154, 101, 154, 245, 210, 211, 212, 213,
     156, 157, 154, 159, 160, 161, 162,   2,  154, 206, 165, 166, 167, 168, 169, 170,
     171, 172, 154, 174, 175, 176, 177, 178,  179, 180, 154, 188, 189, 190, 191, 192,
     193, 194, 195, 196, 197, 198, 199, 200,  201, 202, 203, 204, 205, 206, 207, 208,
     209, 210, 211, 212, 213, 156, 157, 154,  159, 160, 161, 162,  28, 154, 101, 154,
     249, 164, 165, 166, 167, 168, 154, 170,    3, 154, 202, 174, 175, 176, 177, 178,
     179, 180, 154, 188, 189, 190, 191, 192,  193, 194, 195, 196, 197, 198, 199, 200,
     201, 202, 203, 204, 205, 206, 207, 208,  209, 210, 211, 212, 213, 156, 157, 158,
     159, 160, 161, 162, 163, 164, 165, 166,  167, 168, 169, 170, 171, 172, 173, 174,
     175,  27, 154, 101, 154, 192, 176, 177,  178, 179, 180, 181, 188, 189, 190, 191,
     192, 193, 194, 195, 196, 197, 198, 199,  200, 201, 202, 203, 204, 205, 206, 207,
     208, 209, 210, 211, 212, 213, 156, 157,  158, 159, 160, 161, 162, 163, 164, 165,
     166, 167, 168, 169, 170, 171, 172, 173,  174, 175, 176, 177, 178, 179, 180, 181,
     188, 189, 190, 191, 192, 193,  27, 154,  101, 154, 192, 194, 195, 196, 197, 198,
     199, 200, 201, 202, 203, 204, 205, 206,  207, 208, 209, 210, 211, 212, 213, 156,
     157, 158, 159, 160, 161, 162, 163, 164,  165, 166, 167, 168, 169, 170, 171, 172,
     173, 174, 175, 176, 177, 178, 179, 180,  181, 188, 189, 190, 191, 192, 193, 194,
     195, 196, 197, 198, 199, 200, 201, 202,  203, 204, 205,  27, 154, 101, 154, 192,
     206, 207, 208, 209, 210, 211, 212, 213,  156, 157, 158, 159, 160, 161, 162, 163,
     164, 165, 166, 167, 168, 169, 170, 171,  172, 173, 174, 175, 176, 177, 178, 179,
     180, 181, 188, 189, 190, 191, 192, 193,  194, 195, 196, 197, 198, 199, 200, 201,
     202, 203, 204, 205, 206, 207, 208, 209,  210, 211, 212, 213, 156, 157, 158, 159,
      27, 154, 101, 154, 192, 160, 161, 162,  163, 164, 165, 166, 167, 168, 169, 170,
     171, 172, 173, 174, 175, 176, 177, 178,  179, 180, 181, 188, 189, 190, 191, 192,
     193, 194, 195, 196, 197, 198, 199, 200,  201, 202, 203, 204, 205, 206, 207, 208,
     209, 210, 211, 212, 213, 156, 157, 158,  159, 160, 161, 162, 163, 164, 165, 166,
     167, 168, 169, 170, 171,  27, 154, 101,  154, 220, 172, 173, 174, 175, 176, 177,
     178, 179, 180, 181, 188, 189, 190, 191,  192, 193, 194, 195, 196, 197, 198, 199,
     200, 201, 202, 203, 204, 205, 206, 207,  208, 209, 210, 211, 212, 213,  55, 154,
     115, 154, 206, 139, 140, 141, 142, 143,  144, 145, 146, 147, 148, 139, 140, 141,
     142, 143, 144, 145, 146, 147, 148, 139,  140, 141, 142, 143, 144, 145, 146, 147,
     148, 139, 140, 141, 142, 143, 144, 145,  146, 147, 148, 139, 140, 141, 142, 143,
     144, 145, 146, 147, 148,  27, 154
};

/*! \brief Convert UTF8 to latin1 with '?' for all unsupported characters.
 *
 * \param pString   UTF8 string.
 * \return          Equivalent string in latin1 codeset.
 */

const char *ConvertToLatin1(const UTF8 *pString)
{
    static char buffer[2*LBUF_SIZE];
    char *q = buffer;

    while (  '\0' != *pString
          && q < buffer + sizeof(buffer) - 1)
    {
        const UTF8 *p = pString;
        int iState = TR_LATIN1_START_STATE;
        do
        {
            unsigned char ch = *p++;
            unsigned char iColumn = tr_latin1_itt[(unsigned char)ch];
            unsigned short iOffset = tr_latin1_sot[iState];
            for (;;)
            {
                int y = tr_latin1_sbt[iOffset];
                if (y < 128)
                {
                    // RUN phrase.
                    //
                    if (iColumn < y)
                    {
                        iState = tr_latin1_sbt[iOffset+1];
                        break;
                    }
                    else
                    {
                        iColumn = static_cast<unsigned char>(iColumn - y);
                        iOffset += 2;
                    }
                }
                else
                {
                    // COPY phrase.
                    //
                    y = 256-y;
                    if (iColumn < y)
                    {
                        iState = tr_latin1_sbt[iOffset+iColumn+1];
                        break;
                    }
                    else
                    {
                        iColumn = static_cast<unsigned char>(iColumn - y);
                        iOffset = static_cast<unsigned short>(iOffset + y + 1);
                    }
                }
            }
        } while (iState < TR_LATIN1_ACCEPTING_STATES_START);
        *q++ = (char)(iState - TR_LATIN1_ACCEPTING_STATES_START);
        pString = utf8_NextCodePoint(pString);
    }
    *q = '\0';
    return buffer;
}

/*! \brief Convert UTF8 to ASCII with '?' for all unsupported characters.
 *
 * \param pString   UTF8 string.
 * \return          Equivalent string in ASCII codeset.
 */

const char *ConvertToAscii(const UTF8 *pString)
{
    static char buffer[LBUF_SIZE];
    char *q = buffer;

    while ('\0' != *pString)
    {
        const UTF8 *p = pString;
        int iState = TR_ASCII_START_STATE;
        do
        {
            unsigned char ch = *p++;
            unsigned char iColumn = tr_ascii_itt[(unsigned char)ch];
            unsigned short iOffset = tr_ascii_sot[iState];
            for (;;)
            {
                int y = tr_ascii_sbt[iOffset];
                if (y < 128)
                {
                    // RUN phrase.
                    //
                    if (iColumn < y)
                    {
                        iState = tr_ascii_sbt[iOffset+1];
                        break;
                    }
                    else
                    {
                        iColumn = static_cast<unsigned char>(iColumn - y);
                        iOffset += 2;
                    }
                }
                else
                {
                    // COPY phrase.
                    //
                    y = 256-y;
                    if (iColumn < y)
                    {
                        iState = tr_ascii_sbt[iOffset+iColumn+1];
                        break;
                    }
                    else
                    {
                        iColumn = static_cast<unsigned char>(iColumn - y);
                        iOffset = static_cast<unsigned short>(iOffset + y + 1);
                    }
                }
            }
        } while (iState < TR_ASCII_ACCEPTING_STATES_START);
        *q++ = (char)(iState - TR_ASCII_ACCEPTING_STATES_START);
        pString = utf8_NextCodePoint(pString);
    }
    *q = '\0';
    return buffer;
}

bool T5X_GAME::Downgrade3()
{
    int ver = (m_flags & T5X_V_MASK);
    if (ver <= 3)
    {
        return false;
    }
    m_flags &= ~T5X_V_MASK;

    // Additional flatfile flags.
    //
    m_flags |= 3;

    // Downgrade objects by reducing color depth from 24-bit and 256-color down to ANSI-level -- highlight with 8 colors.
    //
    for (map<int, T5X_OBJECTINFO *, lti>::iterator it = m_mObjects.begin(); it != m_mObjects.end(); ++it)
    {
        it->second->RestrictToColor16();
    }
    return true;
}

bool T5X_GAME::Downgrade2()
{
    Downgrade3();
    int ver = (m_flags & T5X_V_MASK);
    if (ver <= 2)
    {
        return false;
    }
    m_flags &= ~(T5X_V_MASK|T5X_V_ATRKEY);

    // Additional flatfile flags.
    //
    m_flags |= 2;

    // Downgrade attribute names.
    //
    for (map<int, T5X_ATTRNAMEINFO *, lti>::iterator it =  m_mAttrNames.begin(); it != m_mAttrNames.end(); ++it)
    {
        m_mAttrNums.erase(it->second->m_pNameUnencoded);
        it->second->ConvertToLatin1();
        map<char *, T5X_ATTRNAMEINFO *, ltstr>::iterator itNum = m_mAttrNums.find(it->second->m_pNameUnencoded);
        if (itNum != m_mAttrNums.end())
        {
            fprintf(stderr, "WARNING: Duplicate attribute name %s(%d) conflicts with %s(%d)\n",
                it->second->m_pNameUnencoded, it->second->m_iNum, itNum->second->m_pNameUnencoded, itNum->second->m_iNum);
        }
        else
        {
            m_mAttrNums[it->second->m_pNameUnencoded] = it->second;
        }
    }

    // Downgrade objects.
    //
    for (map<int, T5X_OBJECTINFO *, lti>::iterator it = m_mObjects.begin(); it != m_mObjects.end(); ++it)
    {
        it->second->ConvertToLatin1();
        it->second->DowngradeDefaultLock();
    }
    return true;
}

bool T5X_GAME::Downgrade1()
{
    Downgrade2();
    int ver = (m_flags & T5X_V_MASK);
    if (1 == ver)
    {
        return false;
    }
    m_flags &= ~(T5X_V_MASK|T5X_V_ATRKEY);

    // Additional flatfile flags.
    //
    m_flags |= 1;
    return true;
}

void T5X_ATTRNAMEINFO::ConvertToLatin1()
{
    char *p = (char *)::ConvertToLatin1((UTF8 *)m_pNameUnencoded);
    SetNumFlagsAndName(m_iNum, m_iFlags, StringClone(p));
}

typedef struct
{
    int r;
    int g;
    int b;
} RGB;

typedef struct
{
    int y;
    int u;
    int v;
    int y2;
} YUV;

typedef struct
{
    RGB  rgb;
    YUV  yuv;
    int  child[2];
    int  color8;
    int  color16;
} PALETTE_ENTRY;
extern PALETTE_ENTRY palette[];

// 16-bit RGB --> Y'UV
//
// Y' = min(abs( 2104R + 4310G +  802B + 4096 +  131072) >> 13, 235)
// U  = min(abs(-1214R - 2384G + 3598B + 4096 + 1048576) >> 13, 240)
// V  = min(abs( 3598R - 3013G -  585B + 4096 + 1048576) >> 13, 240)

inline int mux_abs(int x)
{
    if (0 < x) return x;
    else return -x;
}

inline int mux_min(int x, int y)
{
    if (x < y) return x;
    else return y;
}

inline void rgb2yuv16(RGB *rgb, YUV *yuv)
{
    yuv->y = mux_min(mux_abs( 2104*rgb->r + 4310*rgb->g +  802*rgb->b + 4096 +  131072) >> 13, 235);
    yuv->u = mux_min(mux_abs(-1214*rgb->r - 2384*rgb->g + 3598*rgb->b + 4096 + 1048576) >> 13, 240);
    yuv->v = mux_min(mux_abs( 3598*rgb->r - 3013*rgb->g -  585*rgb->b + 4096 + 1048576) >> 13, 240);
    yuv->y2 = yuv->y + (yuv->y/2);
}


inline void cs2rgb(ColorState cs, RGB *rgb)
{
    rgb->r = static_cast<int>((cs & 0xFF0000) >> 16);
    rgb->g = static_cast<int>((cs & 0x00FF00) >> 8);
    rgb->b = static_cast<int>((cs & 0x0000FF));
}

inline ColorState rgb2cs(RGB *rgb)
{
    ColorState cs;
    cs = (static_cast<ColorState>(rgb->r) << 16)
       | (static_cast<ColorState>(rgb->g) << 8)
       | (static_cast<ColorState>(rgb->b));
    return cs;
}


// All 256 entries of the palette are included in this table, but the palette
// is divided into two non-overlapping trees -- one for when xterm is support,
// and one for when it is not.
//
// Since elements 0 through 15 do not have dependable RGB vales, we usually
// avoid using them when we have xterm support. xterm uses the values below,
// but other clients do not, and these values are usually user-configurable.
// However, if we are forced to map to the 16-color palette, the 16-color tree
// is used.
//
#define PALETTE16_ROOT 9
#define PALETTE256_ROOT 139
#define PALETTE_SIZE (sizeof(palette)/sizeof(palette[0]))
PALETTE_ENTRY palette[] =
{
    { {   0,   0,   0 }, {  16, 128, 128,  24 }, {  -1,  -1 },  0,  0},
    { { 187,   0,   0 }, {  64, 100, 210,  96 }, {  -1,  -1 },  1,  1},
    { {   0, 187,   0 }, { 114,  74,  59, 171 }, {   0,  -1 },  2,  2},
    { { 187, 187,   0 }, { 162,  46, 141, 243 }, {  -1,  -1 },  3,  3},
    { {   0,   0, 187 }, {  34, 210, 115,  51 }, {  -1,  -1 },  4,  4},
    { { 187,   0, 187 }, {  82, 182, 197, 123 }, {  -1,  -1 },  5,  5},
    { {   0, 187, 187 }, { 133, 156,  46, 199 }, {   8,  12 },  6,  6},
    { { 187, 187, 187 }, { 181, 128, 128, 271 }, {  11,  15 },  7,  7},
    { {  85,  85,  85 }, {  91, 128, 128, 136 }, {   2,   1 },  5,  8},
    { { 255,  85,  85 }, { 135, 103, 203, 202 }, {   6,   7 },  3,  9},
    { {  85, 255,  85 }, { 180,  79,  65, 270 }, {  -1,  -1 },  7, 10},
    { { 255, 255,  85 }, { 224,  53, 140, 336 }, {  10,   3 },  3, 11},
    { {  85,  85, 255 }, { 108, 203, 116, 162 }, {   4,   5 },  6, 12},
    { { 255,  85, 255 }, { 151, 177, 191, 226 }, {  -1,  -1 },  7, 13},
    { {  85, 255, 255 }, { 197, 153,  53, 295 }, {  -1,  -1 },  7, 14},
    { { 255, 255, 255 }, { 235, 128, 128, 352 }, {  14,  13 },  7, 15},
    { {   0,   0,   0 }, {  16, 128, 128,  24 }, {  -1,  -1 },  0,  0},
    { {   0,   0,  95 }, {  25, 170, 121,  37 }, {  -1,  -1 },  4,  4},
    { {   0,   0, 135 }, {  29, 187, 118,  43 }, {  -1,  -1 },  4,  4},
    { {   0,   0, 175 }, {  33, 205, 116,  49 }, {  18,  -1 },  4,  4},
    { {   0,   0, 215 }, {  37, 222, 113,  55 }, {  26,  19 },  4,  4},
    { {   0,   0, 255 }, {  41, 240, 110,  61 }, {  -1,  -1 },  4,  4},
    { {   0,  95,   0 }, {  66, 100,  93,  99 }, {  16,  28 },  2,  8},
    { {   0,  95,  95 }, {  75, 142,  86, 112 }, { 244,  97 },  2,  8},
    { {   0,  95, 135 }, {  79, 160,  83, 118 }, {  30,  60 },  6,  8},
    { {   0,  95, 175 }, {  83, 177,  81, 124 }, {  -1,  -1 },  6, 12},
    { {   0,  95, 215 }, {  87, 195,  78, 130 }, {  21,  27 },  6, 12},
    { {   0,  95, 255 }, {  91, 212,  75, 136 }, {  -1,  -1 },  6, 12},
    { {   0, 135,   0 }, {  87,  89,  78, 130 }, {  -1,  -1 },  2,  2},
    { {   0, 135,  95 }, {  96, 130,  72, 144 }, {  -1,  -1 },  2,  8},
    { {   0, 135, 133 }, { 100, 147,  69, 150 }, {  25,  31 },  6,  6},
    { {   0, 135, 175 }, { 104, 166,  66, 156 }, {  -1,  -1 },  6,  6},
    { {   0, 135, 215 }, { 108, 183,  63, 162 }, {  24,  20 },  6,  6},
    { {   0, 135, 255 }, { 112, 201,  60, 168 }, {  -1,  -1 },  6, 12},
    { {   0, 175,   0 }, { 108,  77,  64, 162 }, { 236,  35 },  2,  2},
    { {   0, 175,  95 }, { 117, 119,  57, 175 }, {  70,  66 },  2,  2},
    { {   0, 175, 135 }, { 121, 136,  54, 181 }, {  -1,  -1 },  6,  6},
    { {   0, 175, 175 }, { 125, 154,  51, 187 }, {  -1,  -1 },  6,  6},
    { {   0, 175, 215 }, { 129, 172,  48, 193 }, {  37,  43 },  6,  6},
    { {   0, 175, 255 }, { 133, 189,  45, 199 }, {  33,  69 },  6,  6},
    { {   0, 215,   0 }, { 129,  65,  49, 193 }, {  -1,  -1 },  2,  2},
    { {   0, 215,  95 }, { 138, 107,  42, 207 }, {  40,  71 },  6,  6},
    { {   0, 215, 135 }, { 142, 125,  39, 213 }, {  36,  72 },  6,  6},
    { {   0, 215, 175 }, { 146, 142,  36, 219 }, {  -1,  -1 },  6,  6},
    { {   0, 215, 215 }, { 150, 160,  34, 225 }, {  -1,  -1 },  6,  6},
    { {   0, 215, 255 }, { 154, 177,  31, 231 }, {  44,  51 },  6,  6},
    { {   0, 255,   0 }, { 150,  54,  34, 225 }, {  -1,  -1 },  2, 10},
    { {   0, 255,  90 }, { 159,  93,  28, 238 }, {  76, 114 },  2, 10},
    { {   0, 255, 135 }, { 163, 113,  25, 244 }, {  -1,  -1 },  6, 10},
    { {   0, 255, 175 }, { 167, 131,  22, 250 }, {  -1,  -1 },  6, 14},
    { {   0, 255, 215 }, { 171, 148,  19, 256 }, {  49,  80 },  6, 14},
    { {   0, 255, 255 }, { 175, 166,  16, 262 }, {  -1,  -1 },  6, 14},
    { {  95,   0,   0 }, {  40, 114, 170,  60 }, {  96, 197 },  1,  1},
    { {  95,   0,  95 }, {  50, 156, 163,  75 }, {  -1,  -1 },  5,  5},
    { {  95,   0, 135 }, {  54, 173, 160,  81 }, {  53,  90 },  5,  5},
    { {  95,   0, 175 }, {  58, 191, 157,  87 }, {  -1,  -1 },  5,  5},
    { {  95,   0, 215 }, {  61, 208, 154,  91 }, {  55,  57 },  4,  4},
    { {  95,   0, 255 }, {  65, 226, 152,  97 }, {  -1,  -1 },  4,  4},
    { {  95,  95,   0 }, {  90,  86, 135, 135 }, {  -1,  -1 },  2,  8},
    { {  95,  95,  95 }, { 100, 128, 128, 150 }, { 240, 241 },  2,  8},
    { {  95,  95, 135 }, { 104, 146, 125, 156 }, {  17,  61 },  5,  8},
    { {  95,  95, 175 }, { 108, 163, 122, 162 }, {  -1,  -1 },  6, 12},
    { {  95,  95, 215 }, { 111, 181, 119, 166 }, {  32,  98 },  6, 12},
    { {  95,  95, 255 }, { 115, 198, 117, 172 }, {  39, 105 },  6, 12},
    { {  95, 135,   0 }, { 111,  75, 120, 166 }, {  -1,  -1 },  2,  2},
    { {  95, 135,  95 }, { 121, 116, 113, 181 }, {  64, 106 },  2,  8},
    { {  95, 135, 135 }, { 125, 134, 110, 187 }, {  42, 243 },  6,  8},
    { {  95, 135, 175 }, { 129, 151, 108, 193 }, {  -1,  -1 },  6, 12},
    { {  95, 135, 215 }, { 132, 169, 105, 198 }, {  38, 103 },  6, 12},
    { {  95, 135, 255 }, { 136, 187, 102, 204 }, {  -1,  -1 },  6, 12},
    { {  95, 175,   0 }, { 132,  63, 105, 198 }, {  41,  65 },  2,  2},
    { {  95, 175,  95 }, { 142, 105,  99, 213 }, {  -1,  -1 },  2,  2},
    { {  95, 175, 135 }, { 146, 122,  96, 219 }, {  -1,  -1 },  7,  7},
    { {  95, 175, 175 }, { 150, 140,  93, 225 }, {  -1,  -1 },  6,  6},
    { {  95, 175, 215 }, { 154, 157,  90, 231 }, {  -1,  -1 },  6,  6},
    { {  95, 175, 255 }, { 157, 175,  87, 235 }, {  45, 111 },  6,  6},
    { {  95, 215,   0 }, { 154,  51,  91, 231 }, {  82, 113 },  3,  3},
    { {  95, 215,  95 }, { 163,  93,  84, 244 }, {  48,  78 },  7, 10},
    { {  95, 215, 135 }, { 167, 111,  81, 250 }, {  -1,  -1 },  7, 10},
    { {  95, 215, 175 }, { 171, 128,  78, 256 }, {  50, 109 },  7,  7},
    { {  95, 215, 215 }, { 175, 146,  75, 262 }, {  -1,  -1 },  7, 14},
    { {  95, 215, 255 }, { 178, 163,  72, 267 }, { 110, 159 },  7, 14},
    { {  95, 255,   0 }, { 175,  40,  76, 262 }, {  46,  83 },  3, 10},
    { {  95, 255,  95 }, { 184,  81,  69, 276 }, {  -1,  -1 },  7, 10},
    { {  95, 255, 135 }, { 188,  99,  66, 282 }, {  -1,  -1 },  7, 10},
    { {  95, 255, 175 }, { 192, 117,  63, 288 }, { 148, 247 },  7, 14},
    { {  95, 255, 215 }, { 196, 134,  61, 294 }, { 116, 122 },  7, 14},
    { {  95, 255, 255 }, { 200, 152,  58, 300 }, { 117, 123 },  7, 14},
    { { 135,   0,   0 }, {  51, 108, 187,  76 }, { 166,  52 },  1,  1},
    { { 135,   0,  95 }, {  60, 150, 181,  90 }, {  54, 126 },  5,  5},
    { { 135,   0, 135 }, {  64, 167, 178,  96 }, {  -1,  -1 },  5,  5},
    { { 135,   0, 175 }, {  68, 185, 175, 102 }, {  89,  93 },  5,  5},
    { { 135,   0, 215 }, {  72, 202, 172, 108 }, {  -1,  -1 },  5,  5},
    { { 135,   0, 255 }, {  76, 220, 169, 114 }, {  56, 128 },  5,  5},
    { { 135,  95,   0 }, { 101,  80, 152, 151 }, {  58, 130 },  1,  8},
    { { 135,  95,  95 }, { 110, 122, 146, 165 }, {  -1,  -1 },  5,  8},
    { { 135,  95, 135 }, { 114, 140, 143, 171 }, {  95, 131 },  5,  8},
    { { 135,  95, 175 }, { 118, 157, 140, 177 }, {  62, 162 },  5,  8},
    { { 135,  95, 215 }, { 122, 175, 137, 183 }, {  68,  63 },  5, 12},
    { { 135,  95, 255 }, { 126, 192, 134, 189 }, {  -1,  -1 },  5, 12},
    { { 135, 135,   0 }, { 122,  69, 138, 183 }, {  88, 138 },  3,  3},
    { { 135, 135,  95 }, { 131, 110, 131, 196 }, {  -1,  -1 },  7,  8},
    { { 135, 135, 135 }, { 135, 128, 128, 202 }, {  -1,  -1 },  7,  8},
    { { 135, 135, 175 }, { 139, 146, 125, 208 }, {  67, 104 },  7,  7},
    { { 135, 135, 215 }, { 143, 163, 122, 214 }, {  -1,  -1 },  7, 12},
    { { 135, 135, 255 }, { 147, 181, 119, 220 }, {  99,  -1 },  7, 12},
    { { 135, 175,   0 }, { 143,  57, 123, 214 }, {  -1,  -1 },  3,  3},
    { { 135, 175,  95 }, { 152,  99, 116, 228 }, {  -1,  -1 },  7,  7},
    { { 135, 175, 135 }, { 156, 116, 113, 234 }, { 107,  -1 },  7,  7},
    { { 135, 175, 175 }, { 160, 134, 110, 240 }, {  73, 146 },  7,  7},
    { { 135, 175, 215 }, { 164, 151, 108, 246 }, {  79,  75 },  7,  7},
    { { 135, 175, 255 }, { 168, 169, 105, 252 }, {  74,  -1 },  7,  7},
    { { 135, 215,   0 }, { 164,  45, 108, 246 }, {  -1,  -1 },  3,  3},
    { { 135, 215,  90 }, { 173,  85, 102, 259 }, { 112, 149 },  7, 10},
    { { 135, 215, 135 }, { 177, 105,  99, 265 }, {  77, 108 },  7,  7},
    { { 135, 215, 175 }, { 181, 122,  96, 271 }, {  86, 158 },  7,  7},
    { { 135, 215, 215 }, { 185, 140,  93, 277 }, {  -1,  -1 },  7,  7},
    { { 135, 215, 255 }, { 189, 157,  90, 283 }, {  -1,  -1 },  7, 14},
    { { 135, 255,   0 }, { 185,  34,  94, 277 }, {  47, 150 },  3, 10},
    { { 135, 255,  95 }, { 194,  76,  87, 291 }, {  -1,  -1 },  7, 10},
    { { 135, 255, 135 }, { 198,  93,  84, 297 }, { 119, 156 },  7, 10},
    { { 135, 255, 175 }, { 202, 111,  81, 303 }, {  84, 157 },  7, 10},
    { { 135, 255, 215 }, { 206, 128,  78, 309 }, {  -1,  -1 },  7, 14},
    { { 135, 255, 255 }, { 210, 146,  75, 315 }, {  -1,  -1 },  7, 14},
    { { 175,   0,   0 }, {  61, 102, 205,  91 }, {  -1,  -1 },  1,  1},
    { { 175,   0,  95 }, {  70, 144, 198, 105 }, {  -1,  -1 },  5,  5},
    { { 175,   0, 135 }, {  74, 161, 195, 111 }, { 125, 127 },  5,  5},
    { { 175,   0, 175 }, {  78, 179, 192, 117 }, {  -1,  -1 },  5,  5},
    { { 175,   0, 215 }, {  82, 196, 190, 123 }, {  92,  -1 },  5,  5},
    { { 175,   0, 255 }, {  86, 214, 187, 129 }, {  -1,  -1 },  5,  5},
    { { 175,  95,   0 }, { 111,  74, 170, 166 }, {  -1,  -1 },  1,  9},
    { { 175,  95,  95 }, { 120, 116, 163, 180 }, {  -1,  -1 },  5,  9},
    { { 175,  95, 135 }, { 124, 134, 160, 186 }, { 245, 204 },  5,  9},
    { { 175,  95, 175 }, { 128, 151, 157, 192 }, {  -1,  -1 },  5, 13},
    { { 175,  95, 215 }, { 132, 169, 155, 198 }, { 133, 170 },  5, 13},
    { { 175,  95, 255 }, { 136, 186, 152, 204 }, {  -1,  -1 },  7, 13},
    { { 175, 135,   0 }, { 132,  63, 155, 198 }, { 101, 137 },  3,  3},
    { { 175, 135,  95 }, { 141, 105, 148, 211 }, {  -1,  -1 },  7,  9},
    { { 175, 135, 135 }, { 145, 122, 146, 217 }, { 172, 132 },  7,  7},
    { { 175, 135, 175 }, { 149, 140, 143, 223 }, {  23,  85 },  7,  7},
    { { 175, 135, 215 }, { 153, 157, 140, 229 }, {  -1,  -1 },  7,  7},
    { { 175, 135, 255 }, { 157, 175, 137, 235 }, { 140, 177 },  7, 13},
    { { 175, 175,   0 }, { 153,  51, 140, 229 }, {  -1,  -1 },  3,  3},
    { { 175, 175,  95 }, { 162,  93, 134, 243 }, { 178, 173 },  7,  7},
    { { 175, 175, 135 }, { 166, 110, 131, 249 }, { 174, 180 },  7,  7},
    { { 175, 175, 175 }, { 170, 128, 128, 255 }, { 248, 249 },  7,  7},
    { { 175, 175, 215 }, { 174, 146, 125, 261 }, {  -1,  -1 },  7,  7},
    { { 175, 175, 255 }, { 178, 163, 122, 267 }, {  -1,  -1 },  7,  7},
    { { 175, 215,   0 }, { 174,  39, 126, 261 }, { 118, 216 },  3,  3},
    { { 175, 215,  95 }, { 183,  81, 119, 274 }, {  -1,  -1 },  7,  7},
    { { 175, 215, 135 }, { 187,  99, 116, 280 }, { 155, 151 },  7,  7},
    { { 175, 215, 175 }, { 191, 116, 113, 286 }, { 121, 194 },  7,  7},
    { { 175, 215, 215 }, { 195, 134, 110, 292 }, {  -1,  -1 },  7,  7},
    { { 175, 215, 255 }, { 199, 151, 108, 298 }, {  87, 189 },  7,  7},
    { { 175, 255,   0 }, { 195,  28, 111, 292 }, {  -1,  -1 },  3, 11},
    { { 175, 255,  95 }, { 204,  70, 104, 306 }, { 120, 191 },  7, 11},
    { { 175, 255, 135 }, { 208,  87, 101, 312 }, {  -1,  -1 },  7, 10},
    { { 175, 255, 175 }, { 212, 105,  99, 318 }, {  -1,  -1 },  7, 15},
    { { 175, 255, 215 }, { 216, 122,  96, 324 }, { 152, 195 },  7, 15},
    { { 175, 255, 255 }, { 220, 140,  93, 330 }, { 115, 153 },  7, 15},
    { { 215,   0,   0 }, {  71,  96, 222, 106 }, { 124, 196 },  1,  1},
    { { 215,   0,  95 }, {  81, 138, 216, 121 }, {  -1,  -1 },  1,  1},
    { { 215,   0, 135 }, {  84, 155, 213, 126 }, {  91, 171 },  5,  5},
    { { 215,   0, 175 }, {  88, 173, 210, 132 }, {  -1,  -1 },  5,  5},
    { { 215,   0, 215 }, {  92, 191, 207, 138 }, { 165, 201 },  5,  5},
    { { 215,   0, 255 }, {  96, 208, 204, 144 }, { 129, 135 },  5,  5},
    { { 215,  95,   0 }, { 121,  68, 187, 181 }, {  94, 160 },  3,  9},
    { { 215,  95,  95 }, { 131, 110, 181, 196 }, {  -1,  -1 },  3,  9},
    { { 215,  95, 135 }, { 134, 128, 178, 201 }, {  -1,  -1 },  7,  9},
    { { 215,  95, 175 }, { 138, 145, 175, 207 }, { 134, 198 },  7, 13},
    { { 215,  95, 215 }, { 142, 163, 172, 213 }, {  -1,  -1 },  7, 13},
    { { 215,  95, 255 }, { 146, 180, 169, 219 }, { 169, 164 },  7, 13},
    { { 215, 135,   0 }, { 142,  57, 173, 213 }, { 136, 202 },  3,  3},
    { { 215, 135,  90 }, { 151,  96, 166, 226 }, { 144, 210 },  3,  9},
    { { 215, 135, 135 }, { 155, 116, 163, 232 }, {  -1,  -1 },  7,  9},
    { { 215, 135, 175 }, { 159, 134, 160, 238 }, { 145, 211 },  7,  7},
    { { 215, 135, 215 }, { 163, 151, 157, 244 }, { 175, 213 },  7,  7},
    { { 215, 135, 255 }, { 167, 169, 155, 250 }, {  -1,  -1 },  7, 13},
    { { 215, 175,   0 }, { 163,  45, 158, 244 }, { 179, 214 },  3,  3},
    { { 215, 175,  90 }, { 172,  85, 152, 258 }, { 142, 184 },  3,  3},
    { { 215, 175, 135 }, { 177, 105, 148, 265 }, {  -1,  -1 },  7,  7},
    { { 215, 175, 175 }, { 180, 122, 146, 270 }, { 176, 255 },  7,  7},
    { { 215, 175, 215 }, { 184, 140, 143, 276 }, {  -1,  -1 },  7,  7},
    { { 215, 175, 255 }, { 188, 157, 140, 282 }, { 182, 231 },  7,  7},
    { { 215, 215,   0 }, { 184,  34, 143, 276 }, {  -1,  -1 },  3,  3},
    { { 215, 215,  95 }, { 194,  75, 137, 291 }, {  -1,  -1 },  3, 11},
    { { 215, 215, 135 }, { 198,  93, 134, 297 }, {  -1,  -1 },  7,  7},
    { { 215, 215, 175 }, { 201, 110, 131, 301 }, { 186, 230 },  7,  7},
    { { 215, 215, 215 }, { 205, 128, 128, 307 }, { 251, 224 },  7,  7},
    { { 215, 215, 255 }, { 209, 146, 125, 313 }, { 147,  -1 },  7, 15},
    { { 215, 255,   0 }, { 205,  22, 129, 307 }, { 185, 228 },  3, 11},
    { { 215, 255,  95 }, { 215,  64, 122, 322 }, { 154, 192 },  7, 11},
    { { 215, 255, 135 }, { 219,  81, 119, 328 }, {  -1,  -1 },  7, 11},
    { { 215, 255, 175 }, { 223,  99, 116, 334 }, {  -1,  -1 },  7, 15},
    { { 215, 255, 215 }, { 226, 116, 113, 339 }, { 193,  -1 },  7, 15},
    { { 215, 255, 255 }, { 230, 134, 110, 345 }, {  -1,  -1 },  7, 15},
    { { 255,   0,   0 }, {  81,  90, 240, 121 }, {  -1,  -1 },  1,  1},
    { { 255,   0,  95 }, {  91, 132, 233, 136 }, { 161,  -1 },  1,  1},
    { { 255,   0, 135 }, {  95, 150, 230, 142 }, { 163, 199 },  5,  5},
    { { 255,   0, 175 }, {  99, 167, 228, 148 }, {  -1,  -1 },  5,  5},
    { { 255,   0, 215 }, { 103, 185, 225, 154 }, {  -1,  -1 },  5,  5},
    { { 255,   0, 255 }, { 106, 202, 222, 159 }, { 200,  -1 },  5,  5},
    { { 255,  95,   0 }, { 131,  63, 205, 196 }, { 167, 203 },  3,  9},
    { { 255,  95,  95 }, { 141, 104, 198, 211 }, {  -1,  -1 },  3,  9},
    { { 255,  95, 135 }, { 145, 122, 195, 217 }, { 168,  -1 },  7,  9},
    { { 255,  95, 175 }, { 149, 139, 193, 223 }, {  -1,  -1 },  7, 13},
    { { 255,  95, 215 }, { 153, 157, 190, 229 }, {  -1,  -1 },  7, 13},
    { { 255,  95, 255 }, { 156, 175, 187, 234 }, { 206,  -1 },  7, 13},
    { { 255, 135,   0 }, { 153,  51, 190, 229 }, {  -1,  -1 },  3,  3},
    { { 255, 135,  95 }, { 162,  93, 184, 243 }, {  -1,  -1 },  3,  9},
    { { 255, 135, 135 }, { 166, 110, 181, 249 }, { 209,  -1 },  7,  9},
    { { 255, 135, 175 }, { 170, 128, 178, 255 }, { 205, 212 },  7,  7},
    { { 255, 135, 215 }, { 174, 145, 175, 261 }, {  -1,  -1 },  7, 13},
    { { 255, 135, 255 }, { 177, 163, 172, 265 }, { 141, 207 },  7, 13},
    { { 255, 175,   0 }, { 174,  39, 176, 261 }, { 208, 215 },  3,  3},
    { { 255, 175,  95 }, { 183,  81, 169, 274 }, {  -1,  -1 },  3,  3},
    { { 255, 175, 135 }, { 187,  99, 166, 280 }, { 143, 222 },  7,  7},
    { { 255, 175, 175 }, { 191, 116, 163, 286 }, {  -1,  -1 },  7,  7},
    { { 255, 175, 215 }, { 195, 134, 160, 292 }, {  -1,  -1 },  7,  7},
    { { 255, 175, 255 }, { 199, 151, 157, 298 }, { 218,  -1 },  7,  7},
    { { 255, 215,   0 }, { 195,  28, 161, 292 }, {  -1,  -1 },  3, 11},
    { { 255, 215,  95 }, { 204,  69, 154, 306 }, { 220, 226 },  3, 11},
    { { 255, 215, 135 }, { 208,  87, 151, 312 }, { 227, 229 },  7, 11},
    { { 255, 215, 175 }, { 212, 105, 148, 318 }, { 217,  -1 },  7, 15},
    { { 255, 215, 215 }, { 216, 122, 146, 324 }, { 253, 254 },  7, 15},
    { { 255, 215, 255 }, { 220, 140, 143, 330 }, { 183, 219 },  7, 15},
    { { 255, 255,   0 }, { 216,  16, 146, 324 }, {  -1,  -1 },  3, 11},
    { { 255, 255,  95 }, { 225,  58, 139, 337 }, { 190, 221 },  3, 11},
    { { 255, 255, 135 }, { 229,  75, 137, 343 }, {  -1,  -1 },  7, 11},
    { { 255, 255, 175 }, { 233,  93, 134, 349 }, { 187, 223 },  7, 15},
    { { 255, 255, 215 }, { 235, 110, 131, 352 }, {  -1,  -1 },  7, 15},
    { { 255, 255, 255 }, { 235, 128, 128, 352 }, {  -1,  -1 },  7, 15},
    { {   8,   8,   8 }, {  23, 128, 128,  34 }, {  22, 234 },  0,  0},
    { {  18,  18,  18 }, {  32, 128, 128,  48 }, {  -1,  -1 },  0,  0},
    { {  28,  28,  28 }, {  41, 128, 128,  61 }, { 233, 235 },  0,  0},
    { {  38,  38,  38 }, {  49, 128, 128,  73 }, {  -1,  -1 },  0,  0},
    { {  48,  48,  48 }, {  58, 128, 128,  87 }, { 232, 239 },  0,  8},
    { {  58,  58,  58 }, {  67, 128, 128, 100 }, {  -1,  -1 },  0,  8},
    { {  68,  68,  68 }, {  76, 128, 128, 114 }, { 237,  29 },  5,  8},
    { {  78,  78,  78 }, {  85, 128, 128, 127 }, { 238,  59 },  5,  8},
    { {  88,  88,  88 }, {  94, 128, 128, 141 }, {  -1,  -1 },  5,  8},
    { {  98,  98,  98 }, { 102, 128, 128, 153 }, {  -1,  -1 },  2,  8},
    { { 108, 108, 108 }, { 111, 128, 128, 166 }, {  -1,  -1 },  2,  8},
    { { 118, 118, 118 }, { 120, 128, 128, 180 }, { 242,  -1 },  2,  8},
    { { 128, 128, 128 }, { 129, 128, 128, 193 }, {  34, 100 },  7,  8},
    { { 138, 138, 138 }, { 138, 128, 128, 207 }, { 102, 246 },  7,  7},
    { { 148, 148, 148 }, { 146, 128, 128, 219 }, {  -1,  -1 },  7,  7},
    { { 158, 158, 158 }, { 155, 128, 128, 232 }, {  81, 181 },  7,  7},
    { { 168, 168, 168 }, { 164, 128, 128, 246 }, {  -1,  -1 },  7,  7},
    { { 178, 178, 178 }, { 173, 128, 128, 259 }, {  -1,  -1 },  7,  7},
    { { 188, 188, 188 }, { 182, 128, 128, 273 }, {  -1,  -1 },  7,  7},
    { { 198, 198, 198 }, { 190, 128, 128, 285 }, { 250, 252 },  7,  7},
    { { 208, 208, 208 }, { 199, 128, 128, 298 }, {  -1,  -1 },  7,  7},
    { { 218, 218, 218 }, { 208, 128, 128, 312 }, {  -1,  -1 },  7, 15},
    { { 228, 228, 228 }, { 217, 128, 128, 325 }, {  -1,  -1 },  7, 15},
    { { 238, 238, 238 }, { 226, 128, 128, 339 }, { 188, 225 },  7, 15},
};

INT64 diff(const YUV &yuv1, const YUV &yuv2)
{
    // The human eye is twice as sensitive to changes in Y.  We use 1.5 times.
    //
    INT64 dy = yuv1.y2-yuv2.y2;
    INT64 du = yuv1.u-yuv2.u;
    INT64 dv = yuv1.v-yuv2.v;

    INT64 r = dy*dy + du*du + dv*dv;
    return r;
}

void NearestIndex_tree_u(int iHere, const YUV &yuv, int &iBest, INT64 &rBest);
void NearestIndex_tree_v(int iHere, const YUV &yuv, int &iBest, INT64 &rBest);

void NearestIndex_tree_y(int iHere, const YUV &yuv, int &iBest, INT64 &rBest)
{
    if (-1 == iHere)
    {
        return;
    }

    if (-1 == iBest)
    {
        iBest = iHere;
        rBest = diff(yuv, palette[iBest].yuv);
    }

    INT64 rHere = diff(yuv, palette[iHere].yuv);
    if (rHere < rBest)
    {
        iBest = iHere;
        rBest = rHere;
    }

    INT64 d = yuv.y2 - palette[iHere].yuv.y2;
    int iNearChild = (d < 0)?0:1;
    NearestIndex_tree_u(palette[iHere].child[iNearChild], yuv, iBest, rBest);

    INT64 rAxis = d*d;
    if (rAxis < rBest)
    {
        NearestIndex_tree_u(palette[iHere].child[1-iNearChild], yuv, iBest, rBest);
    }
}

void NearestIndex_tree_u(int iHere, const YUV &yuv, int &iBest, INT64 &rBest)
{
    if (-1 == iHere)
    {
        return;
    }

    if (-1 == iBest)
    {
        iBest = iHere;
        rBest = diff(yuv, palette[iBest].yuv);
    }

    INT64 rHere = diff(yuv, palette[iHere].yuv);
    if (rHere < rBest)
    {
        iBest = iHere;
        rBest = rHere;
    }

    INT64 d = yuv.u - palette[iHere].yuv.u;
    int iNearChild = (d < 0)?0:1;
    NearestIndex_tree_v(palette[iHere].child[iNearChild], yuv, iBest, rBest);

    INT64 rAxis = d*d;
    if (rAxis < rBest)
    {
        NearestIndex_tree_v(palette[iHere].child[1-iNearChild], yuv, iBest, rBest);
    }
}

void NearestIndex_tree_v(int iHere, const YUV &yuv, int &iBest, INT64 &rBest)
{
    if (-1 == iHere)
    {
        return;
    }

    if (-1 == iBest)
    {
        iBest = iHere;
        rBest = diff(yuv, palette[iBest].yuv);
    }

    INT64 rHere = diff(yuv, palette[iHere].yuv);
    if (rHere < rBest)
    {
        iBest = iHere;
        rBest = rHere;
    }

    INT64 d = yuv.v - palette[iHere].yuv.v;
    int iNearChild = (d < 0)?0:1;
    NearestIndex_tree_y(palette[iHere].child[iNearChild], yuv, iBest, rBest);

    INT64 rAxis = d*d;
    if (rAxis < rBest)
    {
        NearestIndex_tree_y(palette[iHere].child[1-iNearChild], yuv, iBest, rBest);
    }
}

int FindNearestPaletteEntry(RGB &rgb)
{
    YUV yuv16;
    rgb2yuv16(&rgb, &yuv16);

    INT64 d;
    int j = -1;
    NearestIndex_tree_y(PALETTE16_ROOT, yuv16, j, d);
    return j;
}

int FindNearestPalette8Entry(RGB &rgb)
{
    YUV yuv16;
    rgb2yuv16(&rgb, &yuv16);

    int iNearest = 0;
    INT64 rNearest = diff(yuv16, palette[0].yuv);

    for (int i = 1; i < 8; i++)
    {
        INT64 r = diff(yuv16, palette[i].yuv);
        if (r < rNearest)
        {
            rNearest = r;
            iNearest = i;
        }
    }
    return iNearest;
}

ColorState UpdateColorState(ColorState cs, int iColorCode)
{
    if (COLOR_INDEX_FG_24 <= iColorCode)
    {
        // In order to apply an RGB 24-bit modification, we need to translate
        // any indexed color to a value color.
        //
        if (iColorCode < COLOR_INDEX_BG_24)
        {
            if (CS_FG_INDEXED & cs)
            {
                cs = (cs & ~CS_FOREGROUND) | rgb2cs(&palette[CS_FG_FIELD(cs)].rgb);
            }

            if (iColorCode < COLOR_INDEX_FG_24_GREEN)
            {
                cs = (cs & ~CS_FOREGROUND_RED) | (static_cast<ColorState>(iColorCode - COLOR_INDEX_FG_24_RED) << 16);
            }
            else if (iColorCode < COLOR_INDEX_FG_24_BLUE)
            {
                cs = (cs & ~CS_FOREGROUND_GREEN) | (static_cast<ColorState>(iColorCode - COLOR_INDEX_FG_24_GREEN) << 8);
            }
            else
            {
                cs = (cs & ~CS_FOREGROUND_BLUE) | static_cast<ColorState>(iColorCode - COLOR_INDEX_FG_24_BLUE);
            }
        }
        else
        {
            if (CS_BG_INDEXED & cs)
            {
                cs = (cs & ~CS_BACKGROUND) | (rgb2cs(&palette[CS_BG_FIELD(cs)].rgb) << 32);
            }

            if (iColorCode < COLOR_INDEX_BG_24_GREEN)
            {
                cs = (cs & ~CS_BACKGROUND_RED) | (static_cast<ColorState>(iColorCode - COLOR_INDEX_BG_24_RED) << 48);
            }
            else if (iColorCode < COLOR_INDEX_BG_24_BLUE)
            {
                cs = (cs & ~CS_BACKGROUND_GREEN) | (static_cast<ColorState>(iColorCode - COLOR_INDEX_BG_24_GREEN) << 40);
            }
            else
            {
                cs = (cs & ~CS_BACKGROUND_BLUE) | (static_cast<ColorState>(iColorCode - COLOR_INDEX_BG_24_BLUE) << 32);
            }
        }
        return cs;
    }
    return (cs & ~aColors[iColorCode].csMask) | aColors[iColorCode].cs;
}

// Maximum binary transition length is:
//
//   COLOR_RESET      "\xEF\x94\x80"
// + COLOR_INTENSE    "\xEF\x94\x81"
// + COLOR_UNDERLINE  "\xEF\x94\x84"
// + COLOR_BLINK      "\xEF\x94\x85"
// + COLOR_INVERSE    "\xEF\x94\x87"
// + COLOR_FG_RED     "\xEF\x98\x81"
// + COLOR_BG_WHITE   "\xEF\x9C\x87"
//
// Each of the seven codes is 3 bytes or 21 bytes total. Plus six 24-bit modifiers (each 4 bytes).
//
#define COLOR_MAXIMUM_BINARY_TRANSITION_LENGTH (21+4*6)

// Generate the minimal color sequence that will transition from one color state
// to another.
//
static UTF8 *ColorTransition
(
    ColorState &csClient,
    ColorState csNext,
    size_t *nTransition
)
{
    static UTF8 Buffer[COLOR_MAXIMUM_BINARY_TRANSITION_LENGTH+1];

    RGB rgb;
    unsigned int iColor;

    // Approximate Foreground Color
    //
    if (CS_FG_INDEXED & csNext)
    {
        if (CS_FG_FIELD(csNext) < COLOR_INDEX_DEFAULT)
        {
            iColor = COLOR_INDEX_FG + palette[CS_FG_FIELD(csNext)].color16;
        }
        else
        {
            iColor = COLOR_INDEX_FG + COLOR_INDEX_DEFAULT;
        }
    }
    else
    {
        cs2rgb(CS_FG_FIELD(csNext), &rgb);
        iColor = COLOR_INDEX_FG + FindNearestPaletteEntry(rgb);
    }

    // For foreground 256-to-16-color down-conversion, we 'borrow' the
    // highlite capability.  We don't need or want it if the client supports
    // 256-color, and there is no highlite capability we can borrow for
    // background color.  This decision is a prerequisite to the 'return
    // to normal' decision below.
    //
    if (COLOR_INDEX_FG + 8 <= iColor && iColor <= COLOR_INDEX_FG + 15)
    {
        csNext |= CS_INTENSE;
        iColor -= 8;
    }

    if (iColor < COLOR_INDEX_FG + COLOR_INDEX_DEFAULT)
    {
        csNext = UpdateColorState(csNext, iColor);
    }

    // Aproximate Background Color
    //
    if (CS_BG_INDEXED & csNext)
    {
        if (CS_BG_FIELD(csNext) < COLOR_INDEX_DEFAULT)
        {
            iColor = COLOR_INDEX_BG + palette[CS_BG_FIELD(csNext)].color8;
        }
        else
        {
            iColor = COLOR_INDEX_BG + COLOR_INDEX_DEFAULT;
        }
    }
    else
    {
        cs2rgb(CS_BG_FIELD(csNext), &rgb);
        iColor = COLOR_INDEX_BG + FindNearestPalette8Entry(rgb);
    }

    if (iColor < COLOR_INDEX_BG + COLOR_INDEX_DEFAULT)
    {
        csNext = UpdateColorState(csNext, iColor);
    }

    if (csClient == csNext)
    {
        *nTransition = 0;
        Buffer[0] = '\0';
        return Buffer;
    }

    size_t i = 0;

    // Do we need to go through the normal state?
    //
    if (  ((csClient & ~csNext) & CS_ATTRS)
       || (  (csNext & CS_BACKGROUND) == CS_BG_DEFAULT
          && (csClient & CS_BACKGROUND) != CS_BG_DEFAULT)
       || (  (csNext & CS_FOREGROUND) == CS_FG_DEFAULT
          && (csClient & CS_FOREGROUND) != CS_FG_DEFAULT))
    {
        memcpy(Buffer + i, COLOR_RESET, sizeof(COLOR_RESET)-1);
        i += sizeof(COLOR_RESET)-1;
        csClient = CS_NORMAL;
    }

    ColorState tmp = csClient ^ csNext;
    if (CS_ATTRS & tmp)
    {
        for (unsigned int iAttr = COLOR_INDEX_ATTR; iAttr < COLOR_INDEX_FG; iAttr++)
        {
            if (aColors[iAttr].cs == (aColors[iAttr].csMask & tmp))
            {
                memcpy(Buffer + i, aColors[iAttr].pUTF, aColors[iAttr].nUTF);
                i += aColors[iAttr].nUTF;
            }
        }
    }

    // At this point, all colors are indexed.
    //
    if (CS_FOREGROUND & tmp)
    {
        iColor = COLOR_INDEX_FG + static_cast<unsigned int>(CS_FG_FIELD(csNext));
        if (iColor < COLOR_INDEX_FG + COLOR_INDEX_DEFAULT)
        {
            memcpy(Buffer + i, aColors[iColor].pUTF, aColors[iColor].nUTF);
            i += aColors[iColor].nUTF;
        }
    }

    if (CS_BACKGROUND & tmp)
    {
        iColor = COLOR_INDEX_BG + static_cast<unsigned int>(CS_BG_FIELD(csNext));
        if (iColor < COLOR_INDEX_BG + COLOR_INDEX_DEFAULT)
        {
            memcpy(Buffer + i, aColors[iColor].pUTF, aColors[iColor].nUTF);
            i += aColors[iColor].nUTF;
        }
    }
    Buffer[i] = '\0';
    *nTransition = i;
    csClient = csNext;
    return Buffer;
}

const UTF8 *RestrictToColor16(const UTF8 *pString)
{
    static UTF8 aBuffer[2*LBUF_SIZE];
    ColorState csClient = CS_NORMAL;
    ColorState csPrev = CS_NORMAL;
    ColorState csNext = CS_NORMAL;
    UTF8 *pBuffer = aBuffer;
    while (  '\0' != *pString
          && pBuffer < aBuffer + sizeof(aBuffer) - sizeof(COLOR_RESET) - 1)
    {
        unsigned int iCode = mux_color(pString);
        if (COLOR_NOTCOLOR == iCode)
        {
            UTF8  *pTransition = NULL;
            size_t nTransition = 0;
            if (csPrev != csNext)
            {
                pTransition = ColorTransition(csClient, csNext, &nTransition);
                if (nTransition)
                {
                    memcpy(pBuffer, pTransition, nTransition);
                    pBuffer += nTransition;
                }
                csPrev = csNext;
            }
            utf8_safe_chr(pString, aBuffer, &pBuffer);
        }
        else
        {
            csNext = UpdateColorState(csNext, iCode);
        }
        pString = utf8_NextCodePoint(pString);
    }

    if (csNext != CS_NORMAL)
    {
        memcpy(pBuffer, COLOR_RESET, sizeof(COLOR_RESET));
        pBuffer += sizeof(COLOR_RESET);
    }
    *pBuffer = '\0';
    return aBuffer;
}

void T5X_OBJECTINFO::RestrictToColor16()
{
    // Restrict name color
    //
    char *p = (char *)::RestrictToColor16((UTF8 *)m_pName);
    free(m_pName);
    m_pName = StringClone(p);

    // Restrict attribute color.
    //
    if (NULL != m_pvai)
    {
        for (vector<T5X_ATTRINFO *>::iterator it = m_pvai->begin(); it != m_pvai->end(); ++it)
        {
            (*it)->RestrictToColor16();
        }
    }
}

void T5X_OBJECTINFO::DowngradeDefaultLock()
{
    if (NULL != m_pvai)
    {
        // Convert A_LOCK if it exists.
        //
        for (vector<T5X_ATTRINFO *>::iterator it = m_pvai->begin(); it != m_pvai->end(); ++it)
        {
            if (T5X_A_LOCK == (*it)->m_iNum)
            {
                delete m_ple;
                m_ple = (*it)->m_pKeyTree;
                (*it)->m_pKeyTree = NULL;
                delete (*it);
                m_pvai->erase(it);
                break;
            }
        }
        if (0 == m_pvai->size())
        {
            delete m_pvai;
            m_pvai = NULL;
        }
    }
}

void T5X_OBJECTINFO::ConvertToLatin1()
{
    // Convert name
    //
    char *p = (char *)ConvertColorToANSI((UTF8 *)m_pName);
    p = (char *)::ConvertToLatin1((UTF8 *)p);
    free(m_pName);
    m_pName = StringClone(p);

    // Convert attribute values.
    //
    if (NULL != m_pvai)
    {
        for (vector<T5X_ATTRINFO *>::iterator it = m_pvai->begin(); it != m_pvai->end(); ++it)
        {
            (*it)->ConvertToLatin1();
        }
    }
}

void T5X_ATTRINFO::ConvertToLatin1()
{
    char *p = (char *)ConvertColorToANSI((UTF8 *)m_pValueUnencoded);
    SetNumOwnerFlagsAndValue(m_iNum, m_dbOwner, m_iFlags, StringClone((char *)::ConvertToLatin1((UTF8 *)p)));
}

void T5X_ATTRINFO::RestrictToColor16()
{
    SetNumOwnerFlagsAndValue(m_iNum, m_dbOwner, m_iFlags, StringClone((char *)::RestrictToColor16((UTF8 *)m_pValueUnencoded)));
}

void T5X_GAME::Extract(FILE *fp, int dbExtract) const
{
    int ver = (m_flags & T5X_V_MASK);
    bool fUnicode = (3 == ver);

    map<int, T5X_OBJECTINFO *, lti>::const_iterator itFound;
    itFound = m_mObjects.find(dbExtract);
    if (itFound == m_mObjects.end())
    {
        fprintf(stderr, "WARNING: Object #%d does not exist in the database.\n", dbExtract);
    }
    else
    {
        itFound->second->Extract(fp, fUnicode);
    }
}

static struct
{
    const char *pFragment;
    size_t      nFragment;
    bool        fColor;
    bool        fUnicode;
    bool        fEvalOnly;
    const char *pSubstitution;
    size_t      nSubstitution;
    bool        fNeedEval;
} fragments[] =
{
    { "\xEF\x94\x80", 3, true,   true, false, "%xn",  3, true  },
    { "\xEF\x94\x81", 3, true,   true, false, "%xh",  3, true  },
    { "\xEF\x94\x84", 3, true,   true, false, "%xu",  3, true  },
    { "\xEF\x94\x85", 3, true,   true, false, "%xf",  3, true  },
    { "\xEF\x94\x87", 3, true,   true, false, "%xi",  3, true  },
    { "\xEF\x98\x80", 3, true,   true, false, "%xx",  3, true  },
    { "\xEF\x98\x81", 3, true,   true, false, "%xr",  3, true  },
    { "\xEF\x98\x82", 3, true,   true, false, "%xg",  3, true  },
    { "\xEF\x98\x83", 3, true,   true, false, "%xy",  3, true  },
    { "\xEF\x98\x84", 3, true,   true, false, "%xb",  3, true  },
    { "\xEF\x98\x85", 3, true,   true, false, "%xm",  3, true  },
    { "\xEF\x98\x86", 3, true,   true, false, "%xc",  3, true  },
    { "\xEF\x98\x87", 3, true,   true, false, "%xw",  3, true  },
    { "\xEF\x9C\x80", 3, true,   true, false, "%xX",  3, true  },
    { "\xEF\x9C\x81", 3, true,   true, false, "%xR",  3, true  },
    { "\xEF\x9C\x82", 3, true,   true, false, "%xG",  3, true  },
    { "\xEF\x9C\x83", 3, true,   true, false, "%xY",  3, true  },
    { "\xEF\x9C\x84", 3, true,   true, false, "%xB",  3, true  },
    { "\xEF\x9C\x85", 3, true,   true, false, "%xM",  3, true  },
    { "\xEF\x9C\x86", 3, true,   true, false, "%xC",  3, true  },
    { "\xEF\x9C\x87", 3, true,   true, false, "%xW",  3, true  },
    { "\x1B[0m",      4, true,  false, false, "%xn",  3, true  },
    { "\x1B[1m",      4, true,  false, false, "%xh",  3, true  },
    { "\x1B[4m",      4, true,  false, false, "%xu",  3, true  },
    { "\x1B[5m",      4, true,  false, false, "%xf",  3, true  },
    { "\x1B[7m",      4, true,  false, false, "%xi",  3, true  },
    { "\x1B[30m",     5, true,  false, false, "%xx",  3, true  },
    { "\x1B[31m",     5, true,  false, false, "%xr",  3, true  },
    { "\x1B[32m",     5, true,  false, false, "%xg",  3, true  },
    { "\x1B[33m",     5, true,  false, false, "%xy",  3, true  },
    { "\x1B[34m",     5, true,  false, false, "%xb",  3, true  },
    { "\x1B[35m",     5, true,  false, false, "%xm",  3, true  },
    { "\x1B[36m",     5, true,  false, false, "%xc",  3, true  },
    { "\x1B[37m",     5, true,  false, false, "%xw",  3, true  },
    { "\x1B[40m",     5, true,  false, false, "%xX",  3, true  },
    { "\x1B[41m",     5, true,  false, false, "%xR",  3, true  },
    { "\x1B[42m",     5, true,  false, false, "%xG",  3, true  },
    { "\x1B[43m",     5, true,  false, false, "%xY",  3, true  },
    { "\x1B[44m",     5, true,  false, false, "%xB",  3, true  },
    { "\x1B[45m",     5, true,  false, false, "%xM",  3, true  },
    { "\x1B[46m",     5, true,  false, false, "%xC",  3, true  },
    { "\x1B[47m",     5, true,  false, false, "%xW",  3, true  },
    { "\t",           1, false, false, false, "%t",   2, true  },
    { "\r\n",         2, false, false, false, "%r",   2, true  },
    { "\r",           1, false, false, false, "",     0, false },
    { "\n",           1, false, false, false, "",     0, false },
    { "  ",           2, false, false, false, "%b ",  3, true  },
    { "%",            1, false, false,  true, "\\%",  2, true  },
    { "\\",           1, false, false,  true, "\\\\", 2, true  },
    { "[",            1, false, false,  true, "\\[",  2, true  },
    { "]",            1, false, false,  true, "\\]",  2, true  },
    { "{",            1, false, false,  true, "\\{",  2, true  },
    { "}",            1, false, false,  true, "\\}",  2, true  },
    { ",",            1, false, false,  true, "\\,",  2, true  },
    { "(",            1, false, false,  true, "\\(",  2, true  },
    { "$",            1, false, false,  true, "\\$",  2, true  },
};

static bool ScanForFragment(const char *p, bool fUnicode, bool fEval, int &iFragment, size_t &nSkip)
{
    if (  NULL == p
       && '\0' == *p)
    {
        nSkip = 0;
        return false;
    }

    for (int i = 0; i < sizeof(fragments)/sizeof(fragments[0]); i++)
    {
        if (  (  !fragments[i].fColor
              || fUnicode == fragments[i].fUnicode)
           && (  !fragments[i].fEvalOnly
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

static char *EncodeSubstitutions(bool fUnicode, char *pValue, bool &fNeedEval)
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
        if (ScanForFragment(p, fUnicode, fEval, iFragment, nSkip))
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

static char *StripColor(bool fUnicode, char *pValue)
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
        if (ScanForFragment(p, fUnicode, fEval, iFragment, nSkip))
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

static NameMask t5x_flags1[] =
{
    { "TRANSPARENT", T5X_SEETHRU      },
    { "WIZARD",      T5X_WIZARD       },
    { "LINK_OK",     T5X_LINK_OK      },
    { "DARK",        T5X_DARK         },
    { "JUMP_OK",     T5X_JUMP_OK      },
    { "STICKY",      T5X_STICKY       },
    { "DESTROY_OK",  T5X_DESTROY_OK   },
    { "HAVE",        T5X_HAVEN        },
    { "QUIET",       T5X_QUIET        },
    { "HALT",        T5X_HALT         },
    { "TRACE",       T5X_TRACE        },
    { "MONITOR",     T5X_MONITOR      },
    { "MYOPIC",      T5X_MYOPIC       },
    { "PUPPET",      T5X_PUPPET       },
    { "CHOWN_OK",    T5X_CHOWN_OK     },
    { "ENTER_OK",    T5X_ENTER_OK     },
    { "VISUAL",      T5X_VISUAL       },
    { "IMMORTAL",    T5X_IMMORTAL     },
    { "OPAQUE",      T5X_OPAQUE       },
    { "VERBOSE",     T5X_VERBOSE      },
    { "INHERIT",     T5X_INHERIT      },
    { "NOSPOOF",     T5X_NOSPOOF      },
    { "SAFE",        T5X_SAFE         },
    { "ROYALTY",     T5X_ROYALTY      },
    { "AUDIBLE",     T5X_HEARTHRU     },
    { "TERSE",       T5X_TERSE        },
};

static NameMask t5x_flags2[] =
{
    { "KEY",         T5X_KEY          },
    { "ABODE",       T5X_ABODE        },
    { "FLOATING",    T5X_FLOATING     },
    { "UNFINDABLE",  T5X_UNFINDABLE   },
    { "PARENT_OK",   T5X_PARENT_OK    },
    { "LIGHT",       T5X_LIGHT        },
    { "AUDITORIUM",  T5X_AUDITORIUM   },
    { "ANSI",        T5X_ANSI         },
    { "HEAD",        T5X_HEAD_FLAG    },
    { "FIXED",       T5X_FIXED        },
    { "UNINSPECTED", T5X_UNINSPECTED  },
    { "NO_COMMAND",  T5X_NO_COMMAND   },
    { "KEEPALIVE",   T5X_KEEPALIVE    },
    { "NOBLEED",     T5X_NOBLEED      },
    { "STAFF",       T5X_STAFF        },
    { "GAGGED",      T5X_GAGGED       },
    { "OPEN_OK",     T5X_OPEN_OK      },
    { "VACATION",    T5X_VACATION     },
    { "HTML",        T5X_HTML         },
    { "BLIND",       T5X_BLIND        },
    { "SUSPECT",     T5X_SUSPECT      },
    { "ASCII",       T5X_ASCII        },
    { "SLAVE",       T5X_SLAVE        },
};

static NameMask t5x_flags3[] =
{
    { "SITEMON", T5X_SITEMON          },
    { "UNICODE", T5X_UNICODE          },
    { "MARKER0", T5X_MARK_0           },
    { "MARKER1", T5X_MARK_1           },
    { "MARKER2", T5X_MARK_2           },
    { "MARKER3", T5X_MARK_3           },
    { "MARKER4", T5X_MARK_4           },
    { "MARKER5", T5X_MARK_5           },
    { "MARKER6", T5X_MARK_6           },
    { "MARKER7", T5X_MARK_7           },
    { "MARKER8", T5X_MARK_8           },
    { "MARKER9", T5X_MARK_9           },
};

static NameMask t5x_powers1[] =
{
    { "quota",            T5X_POW_CHG_QUOTAS    },
    { "chown_anything",   T5X_POW_CHOWN_ANY     },
    { "announce",         T5X_POW_ANNOUNCE      },
    { "boot",             T5X_POW_BOOT          },
    { "halt",             T5X_POW_HALT          },
    { "control_all",      T5X_POW_CONTROL_ALL   },
    { "expanded_who",     T5X_POW_WIZARD_WHO    },
    { "see_all",          T5X_POW_EXAM_ALL      },
    { "find_unfindable",  T5X_POW_FIND_UNFIND   },
    { "free_money",       T5X_POW_FREE_MONEY    },
    { "free_quota",       T5X_POW_FREE_QUOTA    },
    { "hide",             T5X_POW_HIDE          },
    { "idle",             T5X_POW_IDLE          },
    { "search",           T5X_POW_SEARCH        },
    { "long_fingers",     T5X_POW_LONGFINGERS   },
    { "prog",             T5X_POW_PROG          },
    { "siteadmin",        T5X_POW_SITEADMIN     },
    { "comm_all",         T5X_POW_COMM_ALL      },
    { "see_queue",        T5X_POW_SEE_QUEUE     },
    { "see_hidden",       T5X_POW_SEE_HIDDEN    },
    { "monitor",          T5X_POW_MONITOR       },
    { "poll",             T5X_POW_POLL          },
    { "no_destroy",       T5X_POW_NO_DESTROY    },
    { "guest",            T5X_POW_GUEST         },
    { "pass_locks",       T5X_POW_PASS_LOCKS    },
    { "stat_any",         T5X_POW_STAT_ANY      },
    { "steal_money",      T5X_POW_STEAL         },
    { "tel_anywhere",     T5X_POW_TEL_ANYWHR    },
    { "tel_anything",     T5X_POW_TEL_UNRST     },
    { "unkillable",       T5X_POW_UNKILLABLE    },
};

static NameMask t5x_powers2[] =
{
    { "builder",          T5X_POW_BUILDER       },
};

void T5X_OBJECTINFO::Extract(FILE *fp, bool fUnicode) const
{
    fprintf(fp, "@@ Extracting #%d\n", m_dbRef);
    if (fUnicode)
    {
        fprintf(fp, "@@ encoding is UTF-8\n");
    }
    else
    {
        fprintf(fp, "@@ encoding is latin-1\n");
    }
    if (NULL != m_pName)
    {
        bool fNeedEval;
        fprintf(fp, "@@ %s\n", EncodeSubstitutions(fUnicode, m_pName, fNeedEval));
    }
    char *pStrippedObjName = StringClone(StripColor(fUnicode, m_pName));

    // Object flags.
    //
    if (m_fFlags1)
    {
        bool fFirst = true;
        for (int i = 0; i < sizeof(t5x_flags1)/sizeof(t5x_flags1[0]); i++)
        {
            if (m_iFlags1 & t5x_flags1[i].mask)
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
                fprintf(fp, "%s", t5x_flags1[i].pName);
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
        for (int i = 0; i < sizeof(t5x_flags2)/sizeof(t5x_flags2[0]); i++)
        {
            if (m_iFlags2 & t5x_flags2[i].mask)
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
                fprintf(fp, "%s", t5x_flags2[i].pName);
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
        for (int i = 0; i < sizeof(t5x_flags3)/sizeof(t5x_flags3[0]); i++)
        {
            if (m_iFlags3 & t5x_flags3[i].mask)
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
                fprintf(fp, "%s", t5x_flags3[i].pName);
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
        for (int i = 0; i < sizeof(t5x_powers1)/sizeof(t5x_powers1[0]); i++)
        {
            if (m_iPowers1 & t5x_powers1[i].mask)
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
                fprintf(fp, "%s", t5x_powers1[i].pName);
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
        for (int i = 0; i < sizeof(t5x_powers2)/sizeof(t5x_powers2[0]); i++)
        {
            if (m_iPowers2 & t5x_powers2[i].mask)
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
                fprintf(fp, "%s", t5x_powers2[i].pName);
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
        for (vector<T5X_ATTRINFO *>::iterator it = m_pvai->begin(); it != m_pvai->end(); ++it)
        {
            (*it)->Extract(fp, fUnicode, pStrippedObjName);
        }
    }
    free(pStrippedObjName);
}

static struct
{
    const char *pName;
    int         iNum;
} t5x_attr_names[] =
{
    { "Aahear",      T5X_A_AAHEAR       },
    { "Aclone",      T5X_A_ACLONE       },
    { "Aconnect",    T5X_A_ACONNECT     },
    { "ACreate",     T5X_A_ACREATE      },
    { "Adesc",       T5X_A_ADESC        },
    { "ADestroy",    T5X_A_ADESTROY     },
    { "Adfail",      T5X_A_ADFAIL       },
    { "Adisconnect", T5X_A_ADISCONNECT  },
    { "Adrop",       T5X_A_ADROP        },
    { "Aefail",      T5X_A_AEFAIL       },
    { "Aenter",      T5X_A_AENTER       },
    { "Afail",       T5X_A_AFAIL        },
    { "Agfail",      T5X_A_AGFAIL       },
    { "Ahear",       T5X_A_AHEAR        },
    { "Akill",       T5X_A_AKILL        },
    { "Aleave",      T5X_A_ALEAVE       },
    { "Alfail",      T5X_A_ALFAIL       },
    { "Alias",       T5X_A_ALIAS        },
    { "Allowance",   T5X_A_ALLOWANCE    },
    { "Amail",       T5X_A_AMAIL        },
    { "Amhear",      T5X_A_AMHEAR       },
    { "Amove",       T5X_A_AMOVE        },
    { "Aparent",     T5X_A_APARENT      },
    { "Apay",        T5X_A_APAY         },
    { "Arfail",      T5X_A_ARFAIL       },
    { "Asucc",       T5X_A_ASUCC        },
    { "Atfail",      T5X_A_ATFAIL       },
    { "Atport",      T5X_A_ATPORT       },
    { "Atofail",     T5X_A_ATOFAIL      },
    { "Aufail",      T5X_A_AUFAIL       },
    { "Ause",        T5X_A_AUSE         },
    { "Away",        T5X_A_AWAY         },
    { "Charges",     T5X_A_CHARGES      },
    { "CmdCheck",    T5X_A_CMDCHECK     },
    { "Comjoin",     T5X_A_COMJOIN      },
    { "Comleave",    T5X_A_COMLEAVE     },
    { "Comment",     T5X_A_COMMENT      },
    { "Comoff",      T5X_A_COMOFF       },
    { "Comon",       T5X_A_COMON        },
    { "ConFormat",   T5X_A_CONFORMAT    },
    { "Cost",        T5X_A_COST         },
    { "Created",     T5X_A_CREATED      },
    { "Daily",       T5X_A_DAILY        },
    { "Desc",        T5X_A_DESC         },
    { "DefaultLock", T5X_A_LOCK         },
    { "DescFormat",  T5X_A_DESCFORMAT   },
    { "Destroyer",   T5X_A_DESTROYER    },
    { "Dfail",       T5X_A_DFAIL        },
    { "Drop",        T5X_A_DROP         },
    { "DropLock",    T5X_A_LDROP        },
    { "Ealias",      T5X_A_EALIAS       },
    { "Efail",       T5X_A_EFAIL        },
    { "Enter",       T5X_A_ENTER        },
    { "EnterLock",   T5X_A_LENTER       },
    { "ExitFormat",  T5X_A_EXITFORMAT   },
    { "ExitTo",      T5X_A_EXITVARDEST  },
    { "Fail",        T5X_A_FAIL         },
    { "Filter",      T5X_A_FILTER       },
    { "Forwardlist", T5X_A_FORWARDLIST  },
    { "GetFromLock", T5X_A_LGET         },
    { "Gfail",       T5X_A_GFAIL        },
    { "GiveLock",    T5X_A_LGIVE        },
    { "Idesc",       T5X_A_IDESC        },
    { "Idle",        T5X_A_IDLE         },
    { "IdleTimeout", T5X_A_IDLETMOUT    },
    { "Infilter",    T5X_A_INFILTER     },
    { "Inprefix",    T5X_A_INPREFIX     },
    { "Kill",        T5X_A_KILL         },
    { "Lalias",      T5X_A_LALIAS       },
    { "Last",        T5X_A_LAST         },
    { "Lastpage",    T5X_A_LASTPAGE     },
    { "Lastsite",    T5X_A_LASTSITE     },
    { "Lastip",      T5X_A_LASTIP       },
    { "Leave",       T5X_A_LEAVE        },
    { "LeaveLock",   T5X_A_LLEAVE       },
    { "Lfail",       T5X_A_LFAIL        },
    { "LinkLock",    T5X_A_LLINK        },
    { "Listen",      T5X_A_LISTEN       },
    { "Logindata",   T5X_A_LOGINDATA    },
    { "Mailcurf",    T5X_A_MAILCURF     },
    { "Mailflags",   T5X_A_MAILFLAGS    },
    { "Mailfolders", T5X_A_MAILFOLDERS  },
    { "MailLock",    T5X_A_LMAIL        },
    { "Mailmsg",     T5X_A_MAILMSG      },
    { "Mailsub",     T5X_A_MAILSUB      },
    { "Mailsucc",    T5X_A_MAIL         },
    { "Mailto",      T5X_A_MAILTO       },
    { "Mfail",       T5X_A_MFAIL        },
    { "Modified",    T5X_A_MODIFIED     },
    { "Moniker",     T5X_A_MONIKER      },
    { "Move",        T5X_A_MOVE         },
    { "Name",        T5X_A_NAME         },
    { "NameFormat",  T5X_A_NAMEFORMAT   },
    { "Newobjs",     T5X_A_NEWOBJS      },
    { "Odesc",       T5X_A_ODESC        },
    { "Odfail",      T5X_A_ODFAIL       },
    { "Odrop",       T5X_A_ODROP        },
    { "Oefail",      T5X_A_OEFAIL       },
    { "Oenter",      T5X_A_OENTER       },
    { "Ofail",       T5X_A_OFAIL        },
    { "Ogfail",      T5X_A_OGFAIL       },
    { "Okill",       T5X_A_OKILL        },
    { "Oleave",      T5X_A_OLEAVE       },
    { "Olfail",      T5X_A_OLFAIL       },
    { "Omove",       T5X_A_OMOVE        },
    { "Opay",        T5X_A_OPAY         },
    { "OpenLock",    T5X_A_LOPEN        },
    { "Orfail",      T5X_A_ORFAIL       },
    { "Osucc",       T5X_A_OSUCC        },
    { "Otfail",      T5X_A_OTFAIL       },
    { "Otport",      T5X_A_OTPORT       },
    { "Otofail",     T5X_A_OTOFAIL      },
    { "Oufail",      T5X_A_OUFAIL       },
    { "Ouse",        T5X_A_OUSE         },
    { "Oxenter",     T5X_A_OXENTER      },
    { "Oxleave",     T5X_A_OXLEAVE      },
    { "Oxtport",     T5X_A_OXTPORT      },
    { "PageLock",    T5X_A_LPAGE        },
    { "ParentLock",  T5X_A_LPARENT      },
    { "Pay",         T5X_A_PAY          },
    { "Prefix",      T5X_A_PREFIX       },
    { "ProgCmd",     T5X_A_PROGCMD      },
    { "QueueMax",    T5X_A_QUEUEMAX     },
    { "Quota",       T5X_A_QUOTA        },
    { "ReceiveLock", T5X_A_LRECEIVE     },
    { "Reject",      T5X_A_REJECT       },
    { "Rfail",       T5X_A_RFAIL        },
    { "Rquota",      T5X_A_RQUOTA       },
    { "Runout",      T5X_A_RUNOUT       },
    { "SayString",   T5X_A_SAYSTRING    },
    { "Semaphore",   T5X_A_SEMAPHORE    },
    { "Sex",         T5X_A_SEX          },
    { "Signature",   T5X_A_SIGNATURE    },
    { "SpeechMod",   T5X_A_SPEECHMOD    },
    { "SpeechLock",  T5X_A_LSPEECH      },
    { "Startup",     T5X_A_STARTUP      },
    { "Succ",        T5X_A_SUCC         },
    { "TeloutLock",  T5X_A_LTELOUT      },
    { "Tfail",       T5X_A_TFAIL        },
    { "Timeout",     T5X_A_TIMEOUT      },
    { "Tport",       T5X_A_TPORT        },
    { "TportLock",   T5X_A_LTPORT       },
    { "Tofail",      T5X_A_TOFAIL       },
    { "Ufail",       T5X_A_UFAIL        },
    { "Use",         T5X_A_USE          },
    { "UseLock",     T5X_A_LUSE         },
    { "UserLock",    T5X_A_LUSER        },
    { "VisibleLock", T5X_A_LVISIBLE     },
    { "VA",          T5X_A_VA           },
    { "VB",          T5X_A_VA + 1       },
    { "VC",          T5X_A_VA + 2       },
    { "VD",          T5X_A_VA + 3       },
    { "VE",          T5X_A_VA + 4       },
    { "VF",          T5X_A_VA + 5       },
    { "VG",          T5X_A_VA + 6       },
    { "VH",          T5X_A_VA + 7       },
    { "VI",          T5X_A_VA + 8       },
    { "VJ",          T5X_A_VA + 9       },
    { "VK",          T5X_A_VA + 10      },
    { "VL",          T5X_A_VA + 11      },
    { "VM",          T5X_A_VA + 12      },
    { "VN",          T5X_A_VA + 13      },
    { "VO",          T5X_A_VA + 14      },
    { "VP",          T5X_A_VA + 15      },
    { "VQ",          T5X_A_VA + 16      },
    { "VR",          T5X_A_VA + 17      },
    { "VS",          T5X_A_VA + 18      },
    { "VT",          T5X_A_VA + 19      },
    { "VU",          T5X_A_VA + 20      },
    { "VV",          T5X_A_VA + 21      },
    { "VW",          T5X_A_VA + 22      },
    { "VX",          T5X_A_VA + 23      },
    { "VY",          T5X_A_VA + 24      },
    { "VZ",          T5X_A_VA + 25      },
    { "VRML_URL",    T5X_A_VRML_URL     },
    { "HTDesc",      T5X_A_HTDESC       },
    { "Reason",      T5X_A_REASON       },
    { "ConnInfo",    T5X_A_CONNINFO     },
    { "*Password",   T5X_A_PASS         },
    { "*Money",      T5X_A_MONEY        },
    { "*Invalid",    T5X_A_TEMP         },
};

// T5X_AF_ISUSED is not exposed.
// T5X_AF_LOCK is handled separately.
//
static NameMask t5x_attr_flags[] =
{
    { "case",        T5X_AF_CASE        },
    { "dark",        T5X_AF_DARK        },
    { "private",     T5X_AF_ODARK       },
    { "hidden",      T5X_AF_MDARK       },
    { "god",         T5X_AF_GOD         },
    { "html",        T5X_AF_HTML        },
    { "no_clone",    T5X_AF_NOCLONE     },
    { "no_command",  T5X_AF_NOPROG      },
    { "no_inherit",  T5X_AF_PRIVATE     },
    { "no_name",     T5X_AF_NONAME      },
    { "no_parse",    T5X_AF_NOPARSE     },
    { "regexp",      T5X_AF_REGEXP      },
    { "trace",       T5X_AF_TRACE       },
    { "visual",      T5X_AF_VISUAL      },
    { "wizard",      T5X_AF_WIZARD      },
};

static NameMask t5x_attr_flags_comment[] =
{
    { "const",       T5X_AF_CONST       },
    { "deleted",     T5X_AF_DELETED     },
    { "ignore",      T5X_AF_NOCMD       },
    { "internal",    T5X_AF_INTERNAL    },
    { "is_lock",     T5X_AF_IS_LOCK     },
};

void T5X_ATTRINFO::Extract(FILE *fp, bool fUnicode, char *pObjName) const
{
    if (m_fNumAndValue)
    {
        if (m_iNum < A_USER_START)
        {
            if (m_fIsLock)
            {
                for (int i = 0; i < sizeof(t5x_locks)/sizeof(t5x_locks[0]); i++)
                {
                    if (t5x_locks[i].iNum == m_iNum)
                    {
                        bool fNeedEval;
                        const char *p = EncodeSubstitutions(fUnicode, m_pValueUnencoded, fNeedEval);
                        if (fNeedEval)
                        {
                            fprintf(fp, "@wait 0={@lock/%s %s=%s}\n", t5x_locks[i].pName, pObjName, p);
                        }
                        else
                        {
                            fprintf(fp, "@lock/%s %s=%s\n", t5x_locks[i].pName, pObjName, p);
                        }
                        break;
                    }
                }
            }
            else
            {
                for (int i = 0; i < sizeof(t5x_attr_names)/sizeof(t5x_attr_names[0]); i++)
                {
                    if (t5x_attr_names[i].iNum == m_iNum)
                    {
                        bool fFirst = true;
                        for (int j = 0; j < sizeof(t5x_attr_flags_comment)/sizeof(t5x_attr_flags_comment[0]); j++)
                        {
                            if (m_iFlags & t5x_attr_flags_comment[j].mask)
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
                                fprintf(fp, "%s", t5x_attr_flags_comment[j].pName);
                            }
                        }
                        if (!fFirst)
                        {
                            fprintf(fp, "\n");
                        }
                        bool fNeedEval;
                        const char *p = EncodeSubstitutions(fUnicode, m_pValueUnencoded, fNeedEval);
                        if (  fNeedEval
                           && m_iNum != T5X_A_MONIKER)
                        {
                            fprintf(fp, "@wait 0={@%s %s=%s}\n", t5x_attr_names[i].pName, pObjName, p);
                        }
                        else
                        {
                            fprintf(fp, "@%s %s=%s\n", t5x_attr_names[i].pName, pObjName, p);
                        }
                        fFirst = true;
                        for (int j = 0; j < sizeof(t5x_attr_flags)/sizeof(t5x_attr_flags[0]); j++)
                        {
                            if (m_iFlags & t5x_attr_flags[j].mask)
                            {
                                if (fFirst)
                                {
                                    fFirst = false;
                                    if (  fNeedEval
                                       && m_iNum != T5X_A_MONIKER)
                                    {
                                        fprintf(fp, "@wait 0={@set %s/%s=", pObjName, t5x_attr_names[i].pName);
                                    }
                                    else
                                    {
                                        fprintf(fp, "@set %s/%s=", pObjName, t5x_attr_names[i].pName);
                                    }
                                }
                                else
                                {
                                    fprintf(fp, " ");
                                }
                                fprintf(fp, "%s", t5x_attr_flags[j].pName);
                            }
                        }
                        if (!fFirst)
                        {
                            if (  fNeedEval
                               && m_iNum != T5X_A_MONIKER)
                            {
                                fprintf(fp, "}\n");
                            }
                            else
                            {
                                fprintf(fp, "\n");
                            }
                        }
                        if (T5X_AF_LOCK & m_iFlags)
                        {
                            if (  fNeedEval
                               && m_iNum != T5X_A_MONIKER)
                            {
                                fprintf(fp, "@wait 0={@lock %s/%s}\n", pObjName, t5x_attr_names[i].pName);
                            }
                            else
                            {
                                fprintf(fp, "@lock %s/%s\n", pObjName, t5x_attr_names[i].pName);
                            }
                        }
                        break;
                    }
                }
            }
        }
        else
        {
            for (map<int, T5X_ATTRNAMEINFO *, lti>::iterator itName =  g_t5xgame.m_mAttrNames.begin(); itName != g_t5xgame.m_mAttrNames.end(); ++itName)
            {
                if (  itName->second->m_fNumAndName
                   && itName->second->m_iNum == m_iNum)
                {
                    bool fFirst = true;
                    for (int i = 0; i < sizeof(t5x_attr_flags_comment)/sizeof(t5x_attr_flags_comment[0]); i++)
                    {
                        if (m_iFlags & t5x_attr_flags_comment[i].mask)
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
                            fprintf(fp, "%s", t5x_attr_flags_comment[i].pName);
                        }
                    }
                    if (!fFirst)
                    {
                        fprintf(fp, "\n");
                    }
                    bool fNeedEval;
                    const char *p = EncodeSubstitutions(fUnicode, m_pValueUnencoded, fNeedEval);
                    if (fNeedEval)
                    {
                        fprintf(fp, "@wait 0={&%s %s=%s}\n", itName->second->m_pNameUnencoded, pObjName, p);
                    }
                    else
                    {
                        fprintf(fp, "&%s %s=%s\n", itName->second->m_pNameUnencoded, pObjName, p);
                    }
                    fFirst = true;
                    for (int i = 0; i < sizeof(t5x_attr_flags)/sizeof(t5x_attr_flags[0]); i++)
                    {
                        if (m_iFlags & t5x_attr_flags[i].mask)
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
                            fprintf(fp, "%s", t5x_attr_flags[i].pName);
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
                    if (T5X_AF_LOCK & m_iFlags)
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
