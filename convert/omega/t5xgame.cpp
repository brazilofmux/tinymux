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
    map<int, T5X_OBJECTINFO *, lti>::const_iterator itFound;
    if (  m_fLocation
       && -1 != m_dbLocation)
    {
        itFound = g_t5xgame.m_mObjects.find(m_dbLocation);
        if (itFound == g_t5xgame.m_mObjects.end())
        {
            fprintf(stderr, "WARNING: Location (#%d) of object #%d does not exist.\n", m_dbLocation, m_dbRef);
        }
    }
    if (  m_fContents
       && -1 != m_dbContents)
    {
        itFound = g_t5xgame.m_mObjects.find(m_dbContents);
        if (itFound == g_t5xgame.m_mObjects.end())
        {
            fprintf(stderr, "WARNING: Contents (#%d) of object #%d does not exist.\n", m_dbContents, m_dbRef);
        }
    }
    if (  m_fExits
       && -1 != m_dbExits)
    {
        itFound = g_t5xgame.m_mObjects.find(m_dbExits);
        if (itFound == g_t5xgame.m_mObjects.end())
        {
            fprintf(stderr, "WARNING: Exits (#%d) of object #%d does not exist.\n", m_dbExits, m_dbRef);
        }
    }
    if (  m_fNext
       && -1 != m_dbNext)
    {
        itFound = g_t5xgame.m_mObjects.find(m_dbNext);
        if (itFound == g_t5xgame.m_mObjects.end())
        {
            fprintf(stderr, "WARNING: Next (#%d) of object #%d does not exist.\n", m_dbNext, m_dbRef);
        }
    }
    if (  m_fParent
       && -1 != m_dbParent)
    {
        itFound = g_t5xgame.m_mObjects.find(m_dbParent);
        if (itFound == g_t5xgame.m_mObjects.end())
        {
            fprintf(stderr, "WARNING: Parent (#%d) of object #%d does not exist.\n", m_dbParent, m_dbRef);
        }
    }
    if (  m_fOwner
       && -1 != m_dbOwner)
    {
        itFound = g_t5xgame.m_mObjects.find(m_dbOwner);
        if (itFound == g_t5xgame.m_mObjects.end())
        {
            fprintf(stderr, "WARNING: Owner (#%d) of object #%d does not exist.\n", m_dbOwner, m_dbRef);
        }
    }
    if (  m_fZone
       && -1 != m_dbZone)
    {
        itFound = g_t5xgame.m_mObjects.find(m_dbZone);
        if (itFound == g_t5xgame.m_mObjects.end())
        {
            fprintf(stderr, "WARNING: Zone (#%d) of object #%d does not exist.\n", m_dbZone, m_dbRef);
        }
    }
    if (  m_fLink
       && -1 != m_dbLink)
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
                poi->SetLocation(-1);
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
                poi->SetExits(-1);
                poi->SetLink(it->second->m_dbExits);
                break;

            default:
                poi->SetExits(it->second->m_dbExits);
                poi->SetLink(-1);
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
                poi->SetLocation(-1);
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
                poi->SetLocation(-1);
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
// 1596 code points.
// 74 states, 191 columns, 28524 bytes
//
#define TR_LATIN1_START_STATE (0)
#define TR_LATIN1_ACCEPTING_STATES_START (74)

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
       0,   0, 165, 166, 167, 168, 169, 170,  171, 172, 173, 174,   0,   0,   0,   0,
     175, 176, 177, 178,   0, 179, 180,   0,    0, 181,   0, 182,   0,   0,   0, 183,
     184, 185, 186, 187,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0, 188,
     189,   0,   0, 190,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0

};

const unsigned short tr_latin1_stt[74][191] =
{
    { 137,  81,  82,  83,  84,  87, 101, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135, 136, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175, 176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194, 195, 196, 197, 198, 199, 200, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137,   1,   2,   3,   4,   5,   6,   7,   8,   9,  10,  11,  12,  13,  14,  15,  16,  17,  18,  19,  20,  23,  35,  54,  56,  62,  70},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 234, 235, 236, 237, 238, 239, 240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255, 256, 257, 258, 259, 260, 261, 262, 263, 264, 265, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 266, 267, 268, 269, 270, 271, 272, 273, 274, 275, 276, 277, 278, 279, 280, 281, 282, 283, 284, 285, 286, 287, 288, 289, 290, 291, 292, 293, 294, 295, 296, 297, 298, 299, 300, 301, 302, 303, 304, 305, 306, 307, 308, 309, 310, 311, 312, 313, 314, 315, 316, 317, 318, 319, 320, 321, 322, 323, 324, 325, 326, 327, 328, 329, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 139, 171, 139, 171, 139, 171, 141, 173, 141, 173, 141, 173, 141, 173, 142, 174, 142, 174, 143, 175, 143, 175, 143, 175, 143, 175, 143, 175, 145, 177, 145, 177, 145, 177, 145, 177, 146, 178, 146, 178, 147, 179, 147, 179, 147, 179, 147, 179, 147, 137, 137, 137, 148, 180, 149, 181, 137, 150, 182, 150, 182, 150, 182, 150, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 182, 150, 182, 152, 184, 152, 184, 152, 184, 184, 137, 137, 153, 185, 153, 185, 153, 185, 137, 137, 156, 188, 156, 188, 156, 188, 157, 189, 157, 189, 157, 189, 157, 189, 158, 190, 158, 190, 158, 190, 159, 191, 159, 191, 159, 191, 159, 191, 159, 191, 159, 191, 161, 193, 163, 195, 329, 164, 196, 164, 196, 196, 196, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 172, 140, 140, 172, 137, 137, 137, 141, 173, 137, 142, 142, 174, 137, 137, 137, 137, 144, 205, 145, 137, 137, 137, 147, 149, 181, 182, 137, 137, 152, 184, 153, 153, 185, 137, 137, 154, 186, 137, 137, 137, 137, 137, 190, 158, 190, 158, 159, 191, 137, 160, 163, 195, 164, 196, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 107, 137, 137, 137, 137, 137, 137, 137, 137, 137, 139, 171, 147, 179, 153, 185, 159, 191, 159, 191, 159, 191, 159, 191, 159, 191, 137, 139, 171, 139, 171, 137, 304, 145, 177, 145, 177, 149, 181, 153, 185, 153, 185, 137, 137, 180, 137, 137, 137, 145, 177, 137, 137, 152, 184, 139, 171, 137, 304, 153, 185, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 139, 171, 139, 171, 143, 175, 143, 175, 147, 179, 147, 179, 153, 185, 153, 185, 156, 188, 156, 188, 159, 191, 159, 191, 157, 189, 158, 190, 137, 137, 146, 178, 152, 174, 137, 137, 164, 196, 139, 171, 143, 175, 153, 185, 153, 185, 153, 185, 153, 185, 163, 195, 182, 184, 190, 137, 137, 137, 139, 141, 173, 150, 158, 189, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 196, 137, 137, 140, 159, 137, 143, 175, 148, 180, 137, 187, 156, 188, 163, 195, 137, 137, 137, 172, 137, 173, 174, 174, 137, 137, 137, 137, 137, 137, 137, 137, 177, 137, 137, 137, 137, 137, 178, 137, 179, 137, 137, 182, 182, 182, 182, 137, 137, 183, 184, 184, 137, 137, 137, 137, 137, 137, 137, 137, 188, 188, 188, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 189, 137, 137, 137, 137, 137, 190, 191, 137, 192, 137, 137, 137, 137, 196, 196, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 180, 137, 137, 187, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 210, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 226, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 143, 137, 147, 137, 137, 137, 137, 137, 137, 147, 137, 137, 139, 137, 137, 137, 137, 137, 137, 137, 147, 137, 137, 137, 137, 137, 153, 137, 137, 137, 137, 159, 137, 137, 137, 137, 137, 137, 137, 137, 137, 143, 137, 137, 171, 137, 137, 137, 137, 137, 137, 137, 179, 137, 137, 137, 137, 137, 185, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 191, 137, 137, 137, 137, 137, 137, 137, 137, 137, 175, 137, 137, 137, 137, 137, 137, 175, 137, 179, 137, 137, 137, 137, 137, 137, 179, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 153, 185, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 146, 178, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 139, 171, 139, 171, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 147, 179, 147, 179, 153, 185, 137, 137, 137, 137, 143, 175, 159, 191, 159, 191, 159, 191, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 107, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 119, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 107, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137,  21, 137,  21, 137,  21, 137,  21, 137,  21, 137,  21, 137,  21, 137,  21, 137,  21, 137, 137, 137,  22, 137,  22,  17, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137,  24, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137,  25, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137,  17,  26, 137, 137, 137, 137,  27, 137,  22, 137, 137, 137, 137, 137,  22, 137, 137, 137, 137, 137, 137,  28,  29,  30, 137,  31,  32,  33,  34, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 123, 124, 125, 126, 127, 128, 129, 130, 131, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 247, 137, 137, 137, 137, 137, 137, 137, 137, 137, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 107, 137, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 139, 137, 137, 140, 141, 142, 282, 143, 143, 147, 148, 149, 150, 151, 152, 153, 153, 153, 153, 153, 137, 137, 153, 153, 154, 156, 156, 158, 159, 159, 159, 151, 160, 161, 164, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 179, 188, 191, 192, 137, 137, 137, 137, 137, 137, 172, 174, 176, 183, 184, 186, 188, 188, 189, 190, 196, 137, 137, 137, 137, 147, 137, 186, 159, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 172, 174, 176, 177, 181, 182, 183, 184, 186, 188, 189, 137, 192, 194, 196, 171, 137, 174, 175, 137, 137, 137, 179, 137, 137, 191, 137, 137, 173, 173, 314, 175, 176, 180, 177, 178, 179, 137, 179, 179, 180, 182, 182, 182, 183, 183, 184, 184, 184, 185, 137, 189, 137, 190, 191, 137, 191, 192, 192, 196, 196, 196, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 139, 171, 140, 172, 140, 172, 140, 172, 141, 173, 142, 174, 142, 174, 142, 174, 142, 174, 142, 174, 143, 175, 143, 175, 143, 175, 143, 175, 143, 175, 144, 176, 145, 177, 146, 178, 146, 178, 146, 178, 146, 178, 146, 178, 147, 179, 147, 179, 149, 181, 149, 181, 149, 181, 150, 182, 150, 182, 150, 182, 150, 182, 151, 183, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 151, 183, 151, 183, 152, 184, 152, 184, 152, 184, 152, 184, 153, 185, 153, 185, 153, 185, 153, 185, 154, 186, 154, 186, 156, 188, 156, 188, 156, 188, 156, 188, 157, 189, 157, 189, 157, 189, 157, 189, 157, 189, 158, 190, 158, 190, 158, 190, 158, 190, 159, 191, 159, 191, 159, 191, 159, 191, 159, 191, 160, 192, 160, 192, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 161, 193, 161, 193, 161, 193, 161, 193, 161, 193, 162, 194, 162, 194, 163, 195, 164, 196, 164, 196, 164, 196, 178, 190, 193, 195, 171, 137, 137, 137, 137, 137, 139, 171, 139, 171, 139, 171, 139, 171, 139, 171, 139, 171, 139, 171, 139, 171, 139, 171, 139, 171, 139, 171, 139, 171, 143, 175, 143, 175, 143, 175, 143, 175, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 143, 175, 143, 175, 143, 175, 143, 175, 147, 179, 147, 179, 153, 185, 153, 185, 153, 185, 153, 185, 153, 185, 153, 185, 153, 185, 153, 185, 153, 185, 153, 185, 153, 185, 153, 185, 159, 191, 159, 191, 159, 191, 159, 191, 159, 191, 159, 191, 159, 191, 163, 195, 163, 195, 163, 195, 163, 195, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137,  36,  37,  38, 137,  39, 137, 137, 137,  40, 137,  41, 137,  42, 137, 137, 137, 137,  43,  44,  45, 137, 137, 137, 137, 137, 137, 137, 137,  46,  47,  48, 137, 137, 137, 137, 137, 137, 137, 137, 137,  49, 137, 137,  50, 137, 137, 137, 137,  51,  52,  53, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 119, 119, 137, 224, 225, 137, 137, 137, 219, 220, 204, 204, 221, 222, 206, 206, 208, 209, 223, 137, 137, 137, 207, 137, 137, 137, 137, 137, 137, 137, 137, 137, 211, 137, 137, 137, 137, 137, 137, 137, 137, 213, 229, 137, 107, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 119, 137, 137, 137, 137, 107, 137, 137, 137, 137, 137, 137, 137, 137, 137, 119, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 122, 179, 137, 137, 126, 127, 128, 129, 130, 131, 137, 247, 137, 137, 137, 184, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 137, 247, 137, 137, 137, 137, 171, 175, 185, 194, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 202, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 227, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 119, 137, 137, 121, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 116, 137, 137, 137, 137, 137, 137, 137, 137, 119, 137, 121, 137, 137, 137, 137, 137, 137, 119, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 246, 137, 137, 137, 137, 137, 137, 137, 137, 246, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 123, 124, 125, 126, 127, 128, 129, 130, 131, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 123, 124, 125, 126, 127, 128, 129, 130, 131, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 123, 124, 125, 126, 127, 128, 129, 130, 131, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 171, 172, 173, 174, 175, 176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194, 195, 196, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 162, 163, 164, 171, 172, 173, 174, 175, 176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194, 195, 196, 122, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 123, 124, 125, 126, 127, 128, 129, 130, 131, 137, 122, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 116, 116, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 107, 107, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 213, 229, 137, 137, 137, 137, 137, 137, 123, 124, 125, 126, 127, 128, 129, 130, 131, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 123, 124, 125, 126, 127, 128, 129, 130, 131, 137, 123, 124, 125, 126, 127, 128, 129, 130, 131, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 116, 116, 137, 137, 116, 116, 116, 116, 121, 137, 137, 116, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 116, 116, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 246, 246, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 147, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 179, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 150, 182, 150, 154, 156, 171, 190, 146, 178, 149, 181, 164, 196, 137, 137, 137, 137, 137, 137, 137, 192, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 153, 185, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137,  55, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 106, 118, 120, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137,  57,  58, 137, 137,  59,  60, 137,  61, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 107, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 107, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 119, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 170, 171, 172, 173, 174, 175, 176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194, 195, 196, 197, 198, 199, 200, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 236, 237, 246, 137, 240, 239, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137,  63, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137,  67, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137,  64,  65,  17, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137,  66, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 146, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 282, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 178, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 314, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 123, 124, 125, 126, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137,  68, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137,  69, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 123, 124, 125, 126, 127, 128, 129, 130, 131, 123, 124, 125, 126, 127, 128, 129, 130, 131, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137,  71, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137,  72,  73, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 107, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 119, 137, 137, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137},
    { 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 162, 163, 164, 137, 137, 137, 137, 137, 137, 171, 172, 173, 174, 175, 176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194, 195, 196, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137}
};

char *ConvertToLatin(const UTF8 *pString)
{
    static char buffer[LBUF_SIZE];
    char *q = buffer;

    while (  '\0' != *pString
          && q < buffer + sizeof(buffer) - 1)
    {
        const UTF8 *p = pString;
        int iState = TR_LATIN1_START_STATE;
        do
        {
            unsigned char ch = *p++;
            iState = tr_latin1_stt[iState][tr_latin1_itt[(unsigned char)ch]];
        } while (iState < TR_LATIN1_ACCEPTING_STATES_START);
        *q++ = (char)(iState - TR_LATIN1_ACCEPTING_STATES_START);
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
    char *p = (char *)ConvertToLatin((UTF8 *)m_pName);
    free(m_pName);
    m_pName = StringClone(p);
}

void T5X_OBJECTINFO::Downgrade()
{
    // Convert name
    //
    char *p = (char *)convert_color((UTF8 *)m_pName);
    p = (char *)ConvertToLatin((UTF8 *)p);
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
    SetNumOwnerFlagsAndValue(m_iNum, m_dbOwner, m_iFlags, (char *)ConvertToLatin((UTF8 *)p));
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

bool scanpoints(const char *p, const char **pend, const char **q, size_t *qn)
{
    *q = NULL;
    if ('\xEF' == p[0])
    {
        *pend = p + 3;
        *qn = 3;
        if ('\x94' == p[1])
        {
            if ('\x80' == p[2])
            {
                *q = "%xn";
            }
            else if ('\x81' == p[2])
            {
                *q = "%xh";
            }
            else if ('\x84' == p[2])
            {
                *q = "%xu";
            }
            else if ('\x85' == p[2])
            {
                *q = "%xf";
            }
            else if ('\x87' == p[2])
            {
                *q = "%xi";
            }
        }
        else if ('\x98' == p[1])
        {
            if ('\x80' == p[2])
            {
                *q = "%xx";
            }
            else if ('\x81' == p[2])
            {
                *q = "%xr";
            }
            else if ('\x82' == p[2])
            {
                *q = "%xg";
            }
            else if ('\x83' == p[2])
            {
                *q = "%xy";
            }
            else if ('\x84' == p[2])
            {
                *q = "%xb";
            }
            else if ('\x85' == p[2])
            {
                *q = "%xm";
            }
            else if ('\x86' == p[2])
            {
                *q = "%xc";
            }
            else if ('\x87' == p[2])
            {
                *q = "%xw";
            }
        }
        else if ('\x9C' == p[1])
        {
            if ('\x80' == p[2])
            {
                *q = "%xX";
            }
            else if ('\x81' == p[2])
            {
                *q = "%xR";
            }
            else if ('\x82' == p[2])
            {
                *q = "%xG";
            }
            else if ('\x83' == p[2])
            {
                *q = "%xY";
            }
            else if ('\x84' == p[2])
            {
                *q = "%xB";
            }
            else if ('\x85' == p[2])
            {
                *q = "%xM";
            }
            else if ('\x86' == p[2])
            {
                *q = "%xC";
            }
            else if ('\x87' == p[2])
            {
                *q = "%xW";
            }
        }
    }

    if (NULL != *q)
    {
        return true;
    }
    else
    {
        *pend = strchr(p, '\xEF');
        if (NULL == *pend)
        {
            *pend = p + strlen(p);
        }
        return false;
    }
}

bool scanansi(const char *p, const char **pend, const char **q, size_t *qn)
{
    *q = NULL;
    if (ESC_CHAR == p[0])
    {
        *qn = 3;
        if (  '[' == p[1]
           && '\0' != p[2]
           && 'm' == p[3])
        {
            *pend = p + 4;
            if ('0' == p[2])
            {
                *q = "%xn";
            }
            else if ('1' == p[2])
            {
                *q = "%xh";
            }
            else if ('4' == p[2])
            {
                *q = "%xu";
            }
            else if ('5' == p[2])
            {
                *q = "%xf";
            }
            else if ('7' == p[2])
            {
                *q = "%xi";
            }
        }
        else if (  '[' == p[1]
                && '\0' != p[2]
                && '\0' != p[3]
                && 'm' == p[4])
        {
            *pend = p + 5;
            if ('3' == p[2])
            {
                if ('0' == p[3])
                {
                    *q = "%xx";
                }
                else if ('1' == p[3])
                {
                    *q = "%xr";
                }
                else if ('2' == p[3])
                {
                    *q = "%xg";
                }
                else if ('3' == p[3])
                {
                    *q = "%xy";
                }
                else if ('4' == p[3])
                {
                    *q = "%xb";
                }
                else if ('5' == p[3])
                {
                    *q = "%xm";
                }
                else if ('6' == p[3])
                {
                    *q = "%xc";
                }
                else if ('7' == p[3])
                {
                    *q = "%xw";
                }
            }
            else if ('4' == p[2])
            {
                if ('0' == p[3])
                {
                    *q = "%xX";
                }
                else if ('1' == p[3])
                {
                    *q = "%xR";
                }
                else if ('2' == p[3])
                {
                    *q = "%xG";
                }
                else if ('3' == p[3])
                {
                    *q = "%xY";
                }
                else if ('4' == p[3])
                {
                    *q = "%xB";
                }
                else if ('5' == p[3])
                {
                    *q = "%xM";
                }
                else if ('6' == p[3])
                {
                    *q = "%xC";
                }
                else if ('7' == p[3])
                {
                    *q = "%xW";
                }
            }
        }
    }

    if (NULL != *q)
    {
        return true;
    }
    else
    {
        *pend = strchr(p, ESC_CHAR);
        if (NULL == *pend)
        {
            *pend = p + strlen(p);
        }
        return false;
    }
}

char *EncodeSubstitutions(bool fUnicode, char *p)
{
    static char buffer[65536];
    char *q = buffer;

    if (fUnicode)
    {
        while (  '\0' != *p
              && q < buffer + sizeof(buffer) - 1)
        {
            const char *pSub;
            const char *pEnd;
            size_t nSub;
            size_t pn;
            if (scanpoints(p, &pEnd, &pSub, &nSub))
            {
                size_t ncpy = nSub;
                size_t nskp = pEnd - p;
                if (q + ncpy < buffer + sizeof(buffer) - 1)
                {
                    memcpy(q, pSub, ncpy);
                    q += ncpy;
                }
                p += nskp;
            }
            else
            {
                size_t ncpy = pEnd-p;
                size_t nskp = pEnd-p;
                if (q + ncpy < buffer + sizeof(buffer) - 1)
                {
                    memcpy(q, p, ncpy);
                    q += ncpy;
                }
                p += nskp;
            }
        }
    }
    else
    {
        while (  '\0' != *p
              && q < buffer + sizeof(buffer) - 1)
        {
            const char *pSub;
            const char *pEnd;
            size_t nSub;
            size_t pn;
            if (scanansi(p, &pEnd, &pSub, &nSub))
            {
                size_t ncpy = nSub;
                size_t nskp = pEnd - p;
                if (q + ncpy < buffer + sizeof(buffer) - 1)
                {
                    memcpy(q, pSub, ncpy);
                    q += ncpy;
                }
                p += nskp;
            }
            else
            {
                size_t ncpy = pEnd-p;
                size_t nskp = pEnd-p;
                if (q + ncpy < buffer + sizeof(buffer) - 1)
                {
                    memcpy(q, p, ncpy);
                    q += ncpy;
                }
                p += nskp;
            }
        }
    }
    *q = '\0';
    return buffer;
}

char *StripColor(bool fUnicode, char *p)
{
    static char buffer[65536];
    char *q = buffer;

    if (fUnicode)
    {
        while (  '\0' != *p
              && q < buffer + sizeof(buffer) - 1)
        {
            const char *pSub;
            const char *pEnd;
            size_t nSub;
            size_t pn;
            if (!scanpoints(p, &pEnd, &pSub, &nSub))
            {
                size_t ncpy = pEnd-p;
                size_t nskp = pEnd-p;
                if (q + ncpy < buffer + sizeof(buffer) - 1)
                {
                    memcpy(q, p, ncpy);
                    q += ncpy;
                }
                p += nskp;
            }
            else
            {
                size_t nskp = pEnd-p;
                p += nskp;
            }
        }
    }
    else
    {
        while (  '\0' != *p
              && q < buffer + sizeof(buffer) - 1)
        {
            const char *pSub;
            const char *pEnd;
            size_t nSub;
            size_t pn;
            if (!scanansi(p, &pEnd, &pSub, &nSub))
            {
                size_t ncpy = pEnd-p;
                size_t nskp = pEnd-p;
                if (q + ncpy < buffer + sizeof(buffer) - 1)
                {
                    memcpy(q, p, ncpy);
                    q += ncpy;
                }
                p += nskp;
            }
            else
            {
                size_t nskp = pEnd-p;
                p += nskp;
            }
        }
    }
    *q = '\0';
    return buffer;
}

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
        fprintf(fp, "@@ %s\n", EncodeSubstitutions(fUnicode, m_pName));
    }

    // Extract attribute values.
    //
    char *pStrippedObjName = StringClone(StripColor(fUnicode, m_pName));
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
    int         iNum;
    const char *pName;
} t5x_attr_names[] =
{
    {   1, "Osucc" },
    {   2, "Ofail" },
    {   3, "Fail" },
    {   4, "Succ" },
    {   5, "*Password" },
    {   6, "Desc" },
    {   7, "Sex" },
    {   8, "Odrop" },
    {   9, "Drop" },
    {  10, "Okill" },
    {  11, "Kill" },
    {  12, "Asucc" },
    {  13, "Afail" },
    {  14, "Adrop" },
    {  15, "Akill" },
    {  16, "Ause" },
    {  17, "Charges" },
    {  18, "Runout" },
    {  19, "Startup" },
    {  20, "Aclone" },
    {  21, "Apay" },
    {  22, "Opay" },
    {  23, "Pay" },
    {  24, "Cost" },
    {  25, "*Money" },
    {  26, "Listen" },
    {  27, "Aahear" },
    {  28, "Amhear" },
    {  29, "Ahear" },
    {  30, "Last" },
    {  31, "QueueMax" },
    {  32, "Idesc" },
    {  33, "Enter" },
    {  34, "Oxenter" },
    {  35, "Aenter" },
    {  36, "Adesc" },
    {  37, "Odesc" },
    {  38, "Rquota" },
    {  39, "Aconnect" },
    {  40, "Adisconnect" },
    {  41, "Allowance" },
    {  42, "DefaultLock" },
    {  43, "Name" },
    {  44, "Comment" },
    {  45, "Use" },
    {  46, "Ouse" },
    {  47, "Semaphore" },
    {  48, "Timeout" },
    {  49, "Quota" },
    {  50, "Leave" },
    {  51, "Oleave" },
    {  52, "Aleave" },
    {  53, "Oenter" },
    {  54, "Oxleave" },
    {  55, "Move" },
    {  56, "Omove" },
    {  57, "Amove" },
    {  58, "Alias" },
    {  59, "EnterLock" },
    {  60, "LeaveLock" },
    {  61, "PageLock" },
    {  62, "UseLock" },
    {  63, "GiveLock" },
    {  64, "Ealias" },
    {  65, "Lalias" },
    {  66, "Efail" },
    {  67, "Oefail" },
    {  68, "Aefail" },
    {  69, "Lfail" },
    {  70, "Olfail" },
    {  71, "Alfail" },
    {  72, "Reject" },
    {  73, "Away" },
    {  74, "Idle" },
    {  75, "Ufail" },
    {  76, "Oufail" },
    {  77, "Aufail" },
    {  78, "PFAIL" },
    {  79, "Tport" },
    {  80, "Otport" },
    {  81, "Oxtport" },
    {  82, "Atport" },
    {  83, "*Privileges" },
    {  84, "Logindata" },
    {  85, "TportLock" },
    {  86, "DropLock" },
    {  87, "ReceiveLock" },
    {  88, "Lastsite" },
    {  89, "Inprefix" },
    {  90, "Prefix" },
    {  91, "Infilter" },
    {  92, "Filter" },
    {  93, "LinkLock" },
    {  94, "TeloutLock" },
    {  95, "Forwardlist" },
    {  96, "Mailfolders" },
    {  97, "UserLock" },
    {  98, "ParentLock" },
    {  99, "LCONTROL" },
    { 100, "VA" },
    { 101, "VB" },
    { 102, "VC" },
    { 103, "VD" },
    { 104, "VE" },
    { 105, "VF" },
    { 106, "VG" },
    { 107, "VH" },
    { 108, "VI" },
    { 109, "VJ" },
    { 110, "VK" },
    { 111, "VL" },
    { 112, "VM" },
    { 113, "VN" },
    { 114, "VO" },
    { 115, "VP" },
    { 116, "VQ" },
    { 117, "VR" },
    { 118, "VS" },
    { 119, "VT" },
    { 120, "VU" },
    { 121, "VV" },
    { 122, "VW" },
    { 123, "VX" },
    { 124, "VY" },
    { 125, "VZ" },
    { 127, "GetFromLock" },
    { 128, "Mfail" },
    { 129, "Gfail" },
    { 130, "Ogfail" },
    { 131, "Agfail" },
    { 132, "Rfail" },
    { 133, "Orfail" },
    { 134, "Arfail" },
    { 135, "Dfail" },
    { 136, "Odfail" },
    { 137, "Adfail" },
    { 138, "Tfail" },
    { 139, "Otfail" },
    { 140, "Atfail" },
    { 141, "Tofail" },
    { 142, "Otofail" },
    { 143, "Atofail" },
    { 144, "LastIP" },
    { 145, "UMBRADESC" },
    { 146, "WRAITHDESC" },
    { 147, "FAEDESC" },
    { 148, "MATRIXDESC" },
    { 149, "ComJoin" },
    { 150, "ComLeave" },
    { 151, "ComOn" },
    { 152, "ComOff" },
    { 198, "CmdCheck" },
    { 199, "Moniker" },
    { 200, "Lastpage" },
    { 201, "Mailsucc" },
    { 202, "Amail" },
    { 203, "Signature" },
    { 204, "Daily" },
    { 205, "Mailto" },
    { 206, "Mailmsg" },
    { 207, "Mailsub" },
    { 208, "Mailcurf" },
    { 209, "SpeechLock" },
    { 210, "ProgCmd" },
    { 211, "Mailflags" },
    { 212, "Destroyer" },
    { 213, "NEWOBJS" },
    { 214, "SayString" },
    { 215, "SpeechMod" },
    { 216, "ExitTo" },
    { 217, "LCHOWN" },
    { 218, "Created" },
    { 219, "Modified" },
    { 220, "VRML_URL" },
    { 221, "HTDesc" },
    { 222, "Reason" },
    { 223, "RegInfo" },
    { 224, "ConnInfo" },
    { 225, "MailLock" },
    { 226, "OpenLock" },
    { 227, "LastWhisper" },
    { 229, "AParent" },
    { 230, "ACreate" },
    { 231, "LVisible" },
    { 236, "Color" },
    { 237, "Alead" },
    { 238, "Lead" },
    { 239, "Olead" },
    { 240, "IdleTimeout" },
    { 241, "ExitFormat" },
    { 242, "ConFormat" },
    { 243, "NameFormat" },
    { 244, "DescFormat" },
    { 250, "RLevel" },
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
                        fprintf(fp, "@lock/%s %s=%s\n", t5x_locks[i].pName, pObjName, EncodeSubstitutions(fUnicode, m_pValueUnencoded));
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
                        if ( m_iFlags
                           & ( T5X_AF_CONST
                             | T5X_AF_DARK
                             | T5X_AF_GOD
                             | T5X_AF_NOCMD
                             | T5X_AF_INTERNAL
                             | T5X_AF_ODARK))
                        {
                            fprintf(fp, "@@ attribute is ");
                            if (T5X_AF_CONST & m_iFlags)
                            {
                                fprintf(fp, "const ");
                            }
                            if (T5X_AF_DARK & m_iFlags)
                            {
                                fprintf(fp, "dark ");
                            }
                            if (T5X_AF_GOD & m_iFlags)
                            {
                                fprintf(fp, "god ");
                            }
                            if (T5X_AF_NOCMD & m_iFlags)
                            {
                                fprintf(fp, "ignore ");
                            }
                            if (T5X_AF_INTERNAL & m_iFlags)
                            {
                                fprintf(fp, "internal ");
                            }
                            if (T5X_AF_ODARK & m_iFlags)
                            {
                                fprintf(fp, "private ");
                            }
                            fprintf(fp, "\n");
                        }
                        fprintf(fp, "@%s %s=%s\n", t5x_attr_names[i].pName, pObjName, EncodeSubstitutions(fUnicode, m_pValueUnencoded));
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
                        fprintf(fp, "&%s %s=%s\n", pAttrName, pObjName, EncodeSubstitutions(fUnicode, m_pValueUnencoded));
                        if ( m_iFlags
                           & ( T5X_AF_CASE
                             | T5X_AF_DARK
                             | T5X_AF_MDARK
                             | T5X_AF_HTML
                             | T5X_AF_NOPROG
                             | T5X_AF_PRIVATE
                             | T5X_AF_NONAME
                             | T5X_AF_NOPARSE
                             | T5X_AF_REGEXP
                             | T5X_AF_TRACE
                             | T5X_AF_WIZARD))
                        {
                            fprintf(fp, "@set %s/%s=", pObjName, pAttrName);
                            if (T5X_AF_CASE & m_iFlags)
                            {
                                fprintf(fp, "case ");
                            }
                            if (T5X_AF_DARK & m_iFlags)
                            {
                                fprintf(fp, "dark ");
                            }
                            if (T5X_AF_MDARK & m_iFlags)
                            {
                                fprintf(fp, "hidden ");
                            }
                            if (T5X_AF_HTML & m_iFlags)
                            {
                                fprintf(fp, "html ");
                            }
                            if (T5X_AF_NOPROG & m_iFlags)
                            {
                                fprintf(fp, "no_command ");
                            }
                            if (T5X_AF_PRIVATE & m_iFlags)
                            {
                                fprintf(fp, "no_inherit ");
                            }
                            if (T5X_AF_NONAME & m_iFlags)
                            {
                                fprintf(fp, "no_name ");
                            }
                            if (T5X_AF_NOPARSE & m_iFlags)
                            {
                                fprintf(fp, "no_parse ");
                            }
                            if (T5X_AF_REGEXP & m_iFlags)
                            {
                                fprintf(fp, "regexp ");
                            }
                            if (T5X_AF_TRACE & m_iFlags)
                            {
                                fprintf(fp, "trace ");
                            }
                            if (T5X_AF_WIZARD & m_iFlags)
                            {
                                fprintf(fp, "wizard ");
                            }
                            fprintf(fp, "\n");
                        }
                        if (T5X_AF_IS_LOCK & m_iFlags)
                        {
                            fprintf(fp, "@lock %s/%s", pObjName, pAttrName);
                        }
                    }
                    break;
                }
            }
        }
    }
}
