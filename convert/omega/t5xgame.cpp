#include "omega.h"
#include "t5xgame.h"
#include "p6hgame.h"
#include "t6hgame.h"
#include "r7hgame.h"

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
        m_le[0]->Write(fp);
        fprintf(fp, ":");
        m_le[1]->Write(fp);

        // The code in 2.6 an earlier does not always emit a NL.  It's really
        // a beneign typo, but we reproduce it to make regression testing
        // easier.
        //
        if (m_le[0]->m_op != T5X_LOCKEXP::le_text)
        {
            fprintf(fp, "\n");
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


void T5X_ATTRNAMEINFO::SetNumAndName(int iNum, char *pName)
{
    m_fNumAndName = true;
    m_iNum = iNum;
    free(m_pName);
    m_pName = pName;
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
        fprintf(fp, "+A%d\n\"%s\"\n", m_iNum, EncodeString(m_pName, fExtraEscapes));
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

            delete m_pAllocated;
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

void T5X_GAME::AddNumAndName(int iNum, char *pName)
{
    T5X_ATTRNAMEINFO *pani = new T5X_ATTRNAMEINFO;
    pani->SetNumAndName(iNum, pName);
    m_vAttrNames.push_back(pani);
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
    if (ver < 1 || 3 < ver)
    {
        fprintf(stderr, "WARNING: Expecting version to be between 1 and 3.\n");
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
            fprintf(stderr, "WARNING: User attribute (%s) uses an attribute number (%d) which is below %d.\n", m_pName, m_iNum, A_USER_START);
        }
        char *p = strchr(m_pName, ':');
        if (NULL == p)
        {
            fprintf(stderr, "WARNING, User attribute (%s) does not contain a flag sub-field.\n", m_pName);
        }
        else
        {
            char *q = m_pName;
            while (q != p)
            {
                if (!isdigit(*q))
                {
                    fprintf(stderr, "WARNING, User attribute (%s) flag sub-field is not numeric.\n", m_pName);
                    break;
                }
                q++;
            }

            if (ver <= 2)
            {
                q = p + 1;
                bool fValid = true;
                if (!t5x_AttrNameInitialSet[*q])
                {
                    fValid = false;
                }
                else if ('\0' != *q)
                {
                    q++;
                    while ('\0' != *q)
                    {
                        if (!t5x_AttrNameSet[*q])
                        {
                            fValid = false;
                            break;
                        }
                        q++;
                    }
                }
                if (!fValid)
                {
                    fprintf(stderr, "WARNING, User attribute (%s) name is not valid.\n", m_pName);
                }
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
        for (vector<T5X_ATTRNAMEINFO *>::const_iterator it = m_vAttrNames.begin(); it != m_vAttrNames.end(); ++it)
        {
            (*it)->Validate(ver);
            if ((*it)->m_fNumAndName)
            {
                int iNum = (*it)->m_iNum;
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
    for (vector<T5X_ATTRNAMEINFO *>::iterator it = m_vAttrNames.begin(); it != m_vAttrNames.end(); ++it)
    {
        (*it)->Write(fp, fExtraEscapes);
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
    if (  T6H_A_NEWOBJS == iNum
       || T6H_A_MAILCC == iNum
       || T6H_A_MAILBCC == iNum
       || T6H_A_LKNOWN == iNum
       || T6H_A_LHEARD == iNum)
    {
        return false;
    }

    if (T6H_A_LEXITS_FMT == iNum)
    {
        iNum = T5X_A_EXITFORMAT;
    }
    else if (T6H_A_NAME_FMT == iNum)
    {
        iNum = T5X_A_NAMEFORMAT;
    }
    else if (T6H_A_LASTIP == iNum)
    {
        iNum = T5X_A_LASTIP;
    }
    else if (T6H_A_SPEECHFMT == iNum)
    {
        iNum = T5X_A_SPEECHMOD;
    }
    else if (T6H_A_LCON_FMT == iNum)
    {
        iNum = T5X_A_CONFORMAT;
    }

    // T5X attributes with no corresponding T6H attribute, and nothing
    // in T6H currently uses the number, but it might be assigned later.
    //
    if (  T5X_A_LGET == iNum
       || T5X_A_MFAIL == iNum
       || T5X_A_LASTIP == iNum
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
       || T5X_A_LVISIBLE == iNum)
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
    for (vector<T6H_ATTRNAMEINFO *>::iterator it =  g_t6hgame.m_vAttrNames.begin(); it != g_t6hgame.m_vAttrNames.end(); ++it)
    {
        AddNumAndName((*it)->m_iNum, StringClone((*it)->m_pName));
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
                if (  (*itAttr)->m_fNumAndValue
                   && convert_t6h_attr_num((*itAttr)->m_iNum, &iNum))
                {
                    if (T5X_A_QUOTA == iNum)
                    {
                        // Typed quota needs to be converted to single quota.
                        //
                        T5X_ATTRINFO *pai = new T5X_ATTRINFO;
                        pai->SetNumAndValue(T5X_A_QUOTA, StringClone(convert_t6h_quota((*itAttr)->m_pValueUnencoded)));
                        pvai->push_back(pai);
                    }
                    else
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
#define COLOR_BG_BLACK   "\xEF\x9C\x80"    // 14
#define COLOR_BG_RED     "\xEF\x9C\x81"    // 15
#define COLOR_BG_GREEN   "\xEF\x9C\x82"    // 16
#define COLOR_BG_YELLOW  "\xEF\x9C\x83"    // 17
#define COLOR_BG_BLUE    "\xEF\x9C\x84"    // 18
#define COLOR_BG_MAGENTA "\xEF\x9C\x85"    // 19
#define COLOR_BG_CYAN    "\xEF\x9C\x86"    // 20
#define COLOR_BG_WHITE   "\xEF\x9C\x87"    // 21
#define COLOR_LAST_CODE  21

#define COLOR_INDEX_RESET       1
#define COLOR_INDEX_INTENSE     2
#define COLOR_INDEX_UNDERLINE   3
#define COLOR_INDEX_BLINK       4
#define COLOR_INDEX_INVERSE     5

#define COLOR_INDEX_ATTR        2
#define COLOR_INDEX_FG          6
#define COLOR_INDEX_BG          14

#define COLOR_INDEX_BLACK       0
#define COLOR_INDEX_RED         1
#define COLOR_INDEX_GREEN       2
#define COLOR_INDEX_YELLOW      3
#define COLOR_INDEX_BLUE        4
#define COLOR_INDEX_MAGENTA     5
#define COLOR_INDEX_CYAN        6
#define COLOR_INDEX_WHITE       7
#define COLOR_INDEX_DEFAULT     8

#define COLOR_INDEX_FG_WHITE    (COLOR_INDEX_FG + COLOR_INDEX_WHITE)

typedef struct
{
    const UTF8 *pUTF;
    size_t      nUTF;
} MUX_COLOR_SET;

const MUX_COLOR_SET aColors[COLOR_LAST_CODE+1] =
{
    { T(""),               0 },
    { T(COLOR_RESET),      3 },
    { T(COLOR_INTENSE),    3 },
    { T(COLOR_UNDERLINE),  3 },
    { T(COLOR_BLINK),      3 },
    { T(COLOR_INVERSE),    3 },
    { T(COLOR_FG_BLACK),   3 },
    { T(COLOR_FG_RED),     3 },
    { T(COLOR_FG_GREEN),   3 },
    { T(COLOR_FG_YELLOW),  3 },
    { T(COLOR_FG_BLUE),    3 },
    { T(COLOR_FG_MAGENTA), 3 },
    { T(COLOR_FG_CYAN),    3 },
    { T(COLOR_FG_WHITE),   3 },
    { T(COLOR_BG_BLACK),   3 },
    { T(COLOR_BG_RED),     3 },
    { T(COLOR_BG_GREEN),   3 },
    { T(COLOR_BG_YELLOW),  3 },
    { T(COLOR_BG_BLUE),    3 },
    { T(COLOR_BG_MAGENTA), 3 },
    { T(COLOR_BG_CYAN),    3 },
    { T(COLOR_BG_WHITE),   3 },
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
                                   && iCode <= COLOR_LAST_CODE)
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
    for (vector<T5X_ATTRNAMEINFO *>::iterator it =  m_vAttrNames.begin(); it != m_vAttrNames.end(); ++it)
    {
        (*it)->Upgrade();
    }

    // Upgrade objects.
    //
    for (map<int, T5X_OBJECTINFO *, lti>::iterator it = m_mObjects.begin(); it != m_mObjects.end(); ++it)
    {
        it->second->Upgrade();
    }
    return true;
}

void T5X_ATTRNAMEINFO::Upgrade()
{
    char *p = (char *)ConvertToUTF8(m_pName);
    free(m_pName);
    m_pName = StringClone(p);
}

void T5X_OBJECTINFO::Upgrade()
{
    // Convert name
    //
    char *p = (char *)ConvertToUTF8(m_pName);
    free(m_pName);
    m_pName = StringClone(p);

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
        pai->SetNumAndValue(42, StringClone(buffer));

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

    // Convert attribute values.
    //
    if (NULL != m_pvai)
    {
        for (vector<T5X_ATTRINFO *>::iterator it = m_pvai->begin(); it != m_pvai->end(); ++it)
        {
            (*it)->Upgrade();
        }
    }
}

void T5X_ATTRINFO::Upgrade()
{
    char *p = (char *)ConvertToUTF8(m_pValueUnencoded);
    SetNumOwnerFlagsAndValue(m_iNum, m_dbOwner, m_iFlags, StringClone(p));
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

#define ANSI_NORMAL   "\033[0m"

#define ANSI_HILITE   "\033[1m"
#define ANSI_UNDER    "\033[4m"
#define ANSI_BLINK    "\033[5m"
#define ANSI_INVERSE  "\033[7m"

// Foreground colors.
//
#define ANSI_FOREGROUND "\033[3"
#define ANSI_BLACK      "\033[30m"
#define ANSI_RED        "\033[31m"
#define ANSI_GREEN      "\033[32m"
#define ANSI_YELLOW     "\033[33m"
#define ANSI_BLUE       "\033[34m"
#define ANSI_MAGENTA    "\033[35m"
#define ANSI_CYAN       "\033[36m"
#define ANSI_WHITE      "\033[37m"

// Background colors.
//
#define ANSI_BACKGROUND "\033[4"
#define ANSI_BBLACK     "\033[40m"
#define ANSI_BRED       "\033[41m"
#define ANSI_BGREEN     "\033[42m"
#define ANSI_BYELLOW    "\033[43m"
#define ANSI_BBLUE      "\033[44m"
#define ANSI_BMAGENTA   "\033[45m"
#define ANSI_BCYAN      "\033[46m"
#define ANSI_BWHITE     "\033[47m"

typedef struct
{
    const char *pAnsi;
    size_t      nAnsi;
} MUX_COLOR_SET_ANSI;

#define COLOR_LAST_CODE  21

const MUX_COLOR_SET_ANSI aColorsAnsi[COLOR_LAST_CODE+1] =
{
    { "",            0                       }, // COLOR_NOTCOLOR
    { ANSI_NORMAL,   sizeof(ANSI_NORMAL)-1   }, // COLOR_INDEX_RESET
    { ANSI_HILITE,   sizeof(ANSI_HILITE)-1   }, // COLOR_INDEX_ATTR, COLOR_INDEX_INTENSE
    { ANSI_UNDER,    sizeof(ANSI_UNDER)-1    }, // COLOR_INDEX_UNDERLINE
    { ANSI_BLINK,    sizeof(ANSI_BLINK)-1    }, // COLOR_INDEX_BLINK
    { ANSI_INVERSE,  sizeof(ANSI_INVERSE)-1  }, // COLOR_INDEX_INVERSE
    { ANSI_BLACK,    sizeof(ANSI_BLACK)-1    }, // COLOR_INDEX_FG
    { ANSI_RED,      sizeof(ANSI_RED)-1      },
    { ANSI_GREEN,    sizeof(ANSI_GREEN)-1    },
    { ANSI_YELLOW,   sizeof(ANSI_YELLOW)-1   },
    { ANSI_BLUE,     sizeof(ANSI_BLUE)-1     },
    { ANSI_MAGENTA,  sizeof(ANSI_MAGENTA)-1  },
    { ANSI_CYAN,     sizeof(ANSI_CYAN)-1     },
    { ANSI_WHITE,    sizeof(ANSI_WHITE)-1    }, // COLOR_INDEX_FG_WHITE
    { ANSI_BBLACK,   sizeof(ANSI_BBLACK)-1   }, // COLOR_INDEX_BG
    { ANSI_BRED,     sizeof(ANSI_BRED)-1     },
    { ANSI_BGREEN,   sizeof(ANSI_BGREEN)-1   },
    { ANSI_BYELLOW,  sizeof(ANSI_BYELLOW)-1  },
    { ANSI_BBLUE,    sizeof(ANSI_BBLUE)-1    },
    { ANSI_BMAGENTA, sizeof(ANSI_BMAGENTA)-1 },
    { ANSI_BCYAN,    sizeof(ANSI_BCYAN)-1    },
    { ANSI_BWHITE,   sizeof(ANSI_BWHITE)-1   }  // COLOR_LAST_CODE
};

// utf/tr_Color.txt
//
// 517 code points.
// 5 states, 13 columns, 321 bytes
//
#define TR_COLOR_START_STATE (0)
#define TR_COLOR_ACCEPTING_STATES_START (5)
extern const unsigned char tr_color_itt[256];
extern const unsigned char tr_color_stt[5][13];

inline int mux_color(const unsigned char *p)
{
    int iState = TR_COLOR_START_STATE;
    do
    {
        unsigned char ch = *p++;
        iState = tr_color_stt[iState][tr_color_itt[(unsigned char)ch]];
    } while (iState < TR_COLOR_ACCEPTING_STATES_START);
    return iState - TR_COLOR_ACCEPTING_STATES_START;
}

#define COLOR_NOTCOLOR   0

#define utf8_NextCodePoint(x)      (x + utf8_FirstByte[(unsigned char)*x])

// utf/tr_Color.txt
//
// 517 code points.
// 5 states, 13 columns, 321 bytes
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

       1,   2,   3,   4,   5,   6,   7,   8,    0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   9,   0,   0,   0,   10,   0,   0,   0,  11,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0,  12,
       0,   0,   0,   0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0

};

const unsigned char tr_color_stt[5][13] =
{
    {   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   1},
    {   5,   5,   5,   5,   5,   5,   5,   5,   5,   2,   3,   4,   5},
    {   5,   6,   7,   5,   5,   8,   9,   5,  10,   5,   5,   5,   5},
    {   5,  11,  12,  13,  14,  15,  16,  17,  18,   5,   5,   5,   5},
    {   5,  19,  20,  21,  22,  23,  24,  25,  26,   5,   5,   5,   5}
};

UTF8 *convert_color(const UTF8 *pString)
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
            memcpy(pBuffer, aColorsAnsi[iCode].pAnsi, aColorsAnsi[iCode].nAnsi);
            pBuffer += aColorsAnsi[iCode].nAnsi;
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

bool T5X_GAME::Downgrade2()
{
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
    for (vector<T5X_ATTRNAMEINFO *>::iterator it =  m_vAttrNames.begin(); it != m_vAttrNames.end(); ++it)
    {
        (*it)->Downgrade();
    }

    // Downgrade objects.
    //
    for (map<int, T5X_OBJECTINFO *, lti>::iterator it = m_mObjects.begin(); it != m_mObjects.end(); ++it)
    {
        it->second->Downgrade();
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

void T5X_ATTRNAMEINFO::Downgrade()
{
    char *p = (char *)ConvertToLatin1((UTF8 *)m_pName);
    free(m_pName);
    m_pName = StringClone(p);
}

void T5X_OBJECTINFO::Downgrade()
{
    // Convert name
    //
    char *p = (char *)convert_color((UTF8 *)m_pName);
    p = (char *)ConvertToLatin1((UTF8 *)p);
    free(m_pName);
    m_pName = StringClone(p);

    if (NULL != m_pvai)
    {
        // Convert A_LOCK if it exists.
        //
        for (vector<T5X_ATTRINFO *>::iterator it = m_pvai->begin(); it != m_pvai->end(); ++it)
        {
            if (42 == (*it)->m_iNum)
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

    // Convert attribute values.
    //
    if (NULL != m_pvai)
    {
        for (vector<T5X_ATTRINFO *>::iterator it = m_pvai->begin(); it != m_pvai->end(); ++it)
        {
            (*it)->Downgrade();
        }
    }
}

void T5X_ATTRINFO::Downgrade()
{
    char *p = (char *)convert_color((UTF8 *)m_pValueUnencoded);
    SetNumOwnerFlagsAndValue(m_iNum, m_dbOwner, m_iFlags, (char *)ConvertToLatin1((UTF8 *)p));
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
        fprintf(fp, "@@ encoding is UTF-8\n", m_dbRef);
    }
    else
    {
        fprintf(fp, "@@ encoding is latin-1\n", m_dbRef);
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
    { "LastIP",      T5X_A_LASTIP       },
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
                            fprintf(fp, "@wait 0={@lock/%s %s=%s\n}", t5x_locks[i].pName, pObjName, p);
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
                                        fprintf(fp, "@wait 0={@set %s/%s=", pObjName, t5x_attr_names[j]);
                                    }
                                    else
                                    {
                                        fprintf(fp, "@set %s/%s=", pObjName, t5x_attr_names[j]);
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
            for (vector<T5X_ATTRNAMEINFO *>::iterator itName =  g_t5xgame.m_vAttrNames.begin(); itName != g_t5xgame.m_vAttrNames.end(); ++itName)
            {
                if (  (*itName)->m_fNumAndName
                   && (*itName)->m_iNum == m_iNum)
                {
                    char *pAttrName = strchr((*itName)->m_pName, ':');
                    if (NULL != pAttrName)
                    {
                        pAttrName++;
                        bool fFirst = true;
                        for (int i = 0; i < sizeof(t5x_attr_flags_comment)/sizeof(t5x_attr_flags_comment[0]); i++)
                        {
                            if (m_iFlags & t5x_attr_flags_comment[i].mask)
                            {
                                if (fFirst)
                                {
                                    fFirst = false;
                                    fprintf(fp, "@@ attribute is ", pObjName);
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
                            fprintf(fp, "@wait 0={&%s %s=%s}\n", pAttrName, pObjName, p);
                        }
                        else
                        {
                            fprintf(fp, "&%s %s=%s\n", pAttrName, pObjName, p);
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
                                        fprintf(fp, "@wait 0={@set %s/%s=", pObjName, pAttrName);
                                    }
                                    else
                                    {
                                        fprintf(fp, "@set %s/%s=", pObjName, pAttrName);
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
                                fprintf(fp, "@wait 0={@lock %s/%s\n}", pObjName, pAttrName);
                            }
                            else
                            {
                                fprintf(fp, "@lock %s/%s\n", pObjName, pAttrName);
                            }
                        }
                    }
                    break;
                }
            }
        }
    }
}
