/*! \file htab.h
 * \brief Structures and declarations needed for table hashing.
 *
 */

#include "copyright.h"

#ifndef HTAB_H
#define HTAB_H

#include <vector>

typedef struct name_table NAMETAB;
struct name_table
{
    const UTF8 *name;
    int minlen;
    int perm;
    unsigned int flag;
};

/* BQUE - Command queue */

typedef struct bque BQUE;
struct bque
{
    CLinearTimeAbsolute waittime;   // time to run command
    dbref   executor;               // executor who will do command
    dbref   caller;                 // caller.
    dbref   enactor;                // enactor causing command (for %N)
    int     eval;
    union
    {
        struct
        {
            dbref   sem;            // blocking semaphore
            int     attr;           // blocking attribute
        } s;
        uint32_t hQuery;              // blocking query
    } u;
    int     nargs;                  // How many args I have
    UTF8    *text;                  // buffer for comm, env, and scr text
    UTF8    *comm;                  // command
    UTF8    *env[NUM_ENV_VARS];     // environment vars
    reg_ref *scr[MAX_GLOBAL_REGS];  // temp vars
#if defined(STUB_SLAVE)
    CResultsSet *pResultsSet;       // Results Set
    int     iRow;                   // Current Row
#endif // STUB_SLAVE
    bool    IsTimed;                // Is there a waittime time on this entry?
};

class CBitField
{
    std::vector<bool> m_bits;

public:
    CBitField(unsigned int max = 0) : m_bits(max ? max + 1 : 0, false) {}
    ~CBitField() = default;

    void Resize(unsigned int max)
    {
        if (max + 1 > m_bits.size())
        {
            m_bits.resize(max + 1, false);
        }
    }

    void ClearAll(void)
    {
        m_bits.assign(m_bits.size(), false);
    }

    void Set(unsigned int i)
    {
        if (i < m_bits.size())
        {
            m_bits[i] = true;
        }
    }

    void Clear(unsigned int i)
    {
        if (i < m_bits.size())
        {
            m_bits[i] = false;
        }
    }

    bool IsSet(unsigned int i)
    {
        return i < m_bits.size() && m_bits[i];
    }
};

extern NAMETAB powers_nametab[];

extern bool search_nametab(dbref, NAMETAB *, const UTF8 *, int *);
extern NAMETAB *find_nametab_ent(dbref, NAMETAB *, const UTF8 *);
extern void display_nametab(dbref, NAMETAB *, const UTF8 *, bool);
extern void interp_nametab(dbref, NAMETAB *, int, const UTF8 *, const UTF8 *, const UTF8 *);
extern void listset_nametab(dbref, NAMETAB *, int, const UTF8 *, bool);

#endif // !HTAB_H
