/*! \file cron.cpp
 * \brief @cron facility — Vixie-style scheduled attribute triggers.
 *
 * In-memory cron entries with computed next-fire-time scheduling.
 * Entries are not persisted — use @startup to recreate on boot.
 * Syntax matches Unix cron (5-field: minute hour dom month dow).
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "core.h"
#include "externs.h"

// Bitmap field limits.
//
#define CRON_MINUTE_MIN  0
#define CRON_MINUTE_MAX  59
#define CRON_HOUR_MIN    0
#define CRON_HOUR_MAX    23
#define CRON_DOM_MIN     1
#define CRON_DOM_MAX     31
#define CRON_MONTH_MIN   1
#define CRON_MONTH_MAX   12
#define CRON_DOW_MIN     0
#define CRON_DOW_MAX     7    // 0 and 7 both mean Sunday

// Flags for DOM/DOW star handling (Vixie cron semantics).
// When at least one of DOM/DOW is '*', use AND (the '*' field is
// all-ones so it's transparent).  When both are specified (neither
// is '*'), use OR — the entry fires if either matches.
//
#define CRON_DOM_STAR  0x01
#define CRON_DOW_STAR  0x02

typedef struct cron_entry CRONTAB;
struct cron_entry
{
    dbref    obj;
    int      atr;
    UTF8    *cronstr;       // Original spec string for @crontab display.

    uint64_t minute;        // Bits 0..59
    uint32_t hour;          // Bits 0..23
    uint32_t dom;           // Bits 0..30  (bit 0 = day 1)
    uint16_t month;         // Bits 0..11  (bit 0 = January)
    uint8_t  dow;           // Bits 0..6   (bit 0 = Sunday)
    int      flags;

    CLinearTimeAbsolute ltaNext;
    CRONTAB *next;
};

static CRONTAB *cron_head = nullptr;

// Forward declarations.
//
static void dispatch_CronEntry(void *pArg, int iUnused);
static bool cron_next_time(CRONTAB *crp, const CLinearTimeAbsolute &ltaAfter,
                           CLinearTimeAbsolute &ltaResult);
static void cron_schedule(CRONTAB *crp);
static void cron_remove(CRONTAB *crp);

// -------------------------------------------------------------------------
// Bitmap helpers.
// -------------------------------------------------------------------------

// Find the lowest set bit at position >= pos in a 64-bit bitmap.
// Returns -1 if no such bit.
//
static int next_set_bit(uint64_t bitmap, int pos)
{
    if (pos > 63)
    {
        return -1;
    }
    uint64_t mask = bitmap & ~((1ULL << pos) - 1);
    if (0 == mask)
    {
        return -1;
    }
    return __builtin_ctzll(mask);
}

// Return the lowest set bit position, or -1.
//
static int first_set_bit(uint64_t bitmap)
{
    if (0 == bitmap)
    {
        return -1;
    }
    return __builtin_ctzll(bitmap);
}

// -------------------------------------------------------------------------
// Date helpers.
// -------------------------------------------------------------------------

static int cron_days_in_month(int year, int month)
{
    static const int dim[] = {31, 28, 31, 30, 31, 30, 31,
                              31, 30, 31, 30, 31};
    if (month < 1 || month > 12)
    {
        return 31;
    }
    int d = dim[month - 1];
    if (2 == month && isLeapYear(year))
    {
        d = 29;
    }
    return d;
}

// Tomohiko Sakamoto's day-of-week algorithm.
// Returns 0=Sunday, 1=Monday, ..., 6=Saturday.
//
static int cron_day_of_week(int y, int m, int d)
{
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3)
    {
        y--;
    }
    return (y + y/4 - y/100 + y/400 + t[m - 1] + d) % 7;
}

// -------------------------------------------------------------------------
// Cron field parser.
// -------------------------------------------------------------------------

// Month and day-of-week name tables (case-insensitive, 3 chars).
//
struct cron_name_entry
{
    const char *name;
    int         value;
};

static const cron_name_entry month_names[] =
{
    {"jan", 1},  {"feb", 2},  {"mar", 3},  {"apr", 4},
    {"may", 5},  {"jun", 6},  {"jul", 7},  {"aug", 8},
    {"sep", 9},  {"oct", 10}, {"nov", 11}, {"dec", 12},
    {nullptr, 0}
};

static const cron_name_entry dow_names[] =
{
    {"sun", 0}, {"mon", 1}, {"tue", 2}, {"wed", 3},
    {"thu", 4}, {"fri", 5}, {"sat", 6},
    {nullptr, 0}
};

// Try to parse a name from the name table.  Returns -1 if no match.
//
static int parse_cron_name(const UTF8 *&p, const cron_name_entry *table)
{
    for (int i = 0; nullptr != table[i].name; i++)
    {
        const char *n = table[i].name;
        if (  (p[0] | 0x20) == n[0]
           && (p[1] | 0x20) == n[1]
           && (p[2] | 0x20) == n[2])
        {
            p += 3;
            return table[i].value;
        }
    }
    return -1;
}

// Parse one number or name from the cron field.
// Returns the value, or -1 on error.
//
static int parse_cron_value(const UTF8 *&p, int low, int high,
                            const cron_name_entry *names)
{
    if (mux_isdigit(*p))
    {
        int val = 0;
        while (mux_isdigit(*p))
        {
            val = val * 10 + (*p - '0');
            p++;
        }
        if (val < low || val > high)
        {
            return -1;
        }
        return val;
    }
    else if (nullptr != names && mux_isalpha(*p))
    {
        return parse_cron_name(p, names);
    }
    return -1;
}

// Parse one cron field (e.g. "*/15" or "1,3,5" or "1-5/2") into a
// bitmap.  Returns pointer to next whitespace-delimited field, or
// nullptr on error.  The bitmap uses bit 0 = low value.
//
static const UTF8 *parse_cron_field(uint64_t &bitmap, int low, int high,
                                    bool &bStar, const UTF8 *p,
                                    const cron_name_entry *names)
{
    bitmap = 0;
    bStar = false;

    if (nullptr == p)
    {
        return nullptr;
    }

    while (mux_isspace(*p))
    {
        p++;
    }
    if ('\0' == *p)
    {
        return nullptr;
    }

    for (;;)
    {
        int begin, end, step;

        if ('*' == *p)
        {
            bStar = true;
            begin = low;
            end = high;
            p++;
        }
        else
        {
            begin = parse_cron_value(p, low, high, names);
            if (begin < 0)
            {
                return nullptr;
            }
            end = begin;

            if ('-' == *p)
            {
                p++;
                end = parse_cron_value(p, low, high, names);
                if (end < 0 || end < begin)
                {
                    return nullptr;
                }
            }
        }

        step = 1;
        if ('/' == *p)
        {
            p++;
            step = 0;
            if (!mux_isdigit(*p))
            {
                return nullptr;
            }
            while (mux_isdigit(*p))
            {
                step = step * 10 + (*p - '0');
                p++;
            }
            if (step <= 0)
            {
                return nullptr;
            }
        }

        for (int i = begin; i <= end; i += step)
        {
            bitmap |= (1ULL << (i - low));
        }

        if (',' != *p)
        {
            break;
        }
        p++;
    }

    while (mux_isspace(*p))
    {
        p++;
    }
    return p;
}

// -------------------------------------------------------------------------
// Next-fire-time computation (Vixie-style).
// -------------------------------------------------------------------------

// Given a cron spec and a reference time, compute the earliest future
// local time that matches.  Works with local time fields throughout.
//
// Returns true on success, false if no match within 4 years.
//
static bool cron_next_time(CRONTAB *crp,
                           const CLinearTimeAbsolute &ltaAfter,
                           CLinearTimeAbsolute &ltaResult)
{
    // Convert to local time fields.
    //
    CLinearTimeAbsolute ltaLocal = ltaAfter;
    ltaLocal.UTC2Local();

    FIELDEDTIME ft;
    if (!ltaLocal.ReturnFields(&ft))
    {
        return false;
    }

    // Advance by one minute and clear sub-minute fields.
    //
    ft.iMinute++;
    ft.iSecond = 0;
    ft.iMillisecond = 0;
    ft.iMicrosecond = 0;
    ft.iNanosecond = 0;

    if (ft.iMinute >= 60)
    {
        ft.iMinute = 0;
        ft.iHour++;
        if (ft.iHour >= 24)
        {
            ft.iHour = 0;
            ft.iDayOfMonth++;
        }
    }

    // Search loop — bounded by 4 years to handle all edge cases.
    //
    int iStartYear = ft.iYear;
    while (ft.iYear < iStartYear + 5)
    {
        // --- Month ---
        //
        int m = next_set_bit(crp->month, ft.iMonth - 1);
        if (-1 == m)
        {
            // Wrap to next year.
            //
            ft.iYear++;
            ft.iMonth = first_set_bit(crp->month) + 1;
            if (ft.iMonth < 1)
            {
                return false;
            }
            ft.iDayOfMonth = 1;
            ft.iHour = 0;
            ft.iMinute = 0;
            continue;
        }
        m += 1;  // Back to 1-based.
        if (m != ft.iMonth)
        {
            ft.iMonth = m;
            ft.iDayOfMonth = 1;
            ft.iHour = 0;
            ft.iMinute = 0;
            continue;
        }

        // --- Day of month overflow ---
        //
        int dim = cron_days_in_month(ft.iYear, ft.iMonth);
        if (ft.iDayOfMonth > dim)
        {
            ft.iMonth++;
            if (ft.iMonth > 12)
            {
                ft.iMonth = 1;
                ft.iYear++;
            }
            ft.iDayOfMonth = 1;
            ft.iHour = 0;
            ft.iMinute = 0;
            continue;
        }

        // --- Day (DOM/DOW combined) ---
        //
        {
            int iDow = cron_day_of_week(ft.iYear, ft.iMonth,
                                         ft.iDayOfMonth);
            bool bDomMatch = (crp->dom & (1u << (ft.iDayOfMonth - 1))) != 0;
            bool bDowMatch = (crp->dow & (1u << iDow)) != 0;
            bool bDayMatch;

            if (crp->flags & (CRON_DOM_STAR | CRON_DOW_STAR))
            {
                bDayMatch = bDomMatch && bDowMatch;
            }
            else
            {
                bDayMatch = bDomMatch || bDowMatch;
            }

            if (!bDayMatch)
            {
                ft.iDayOfMonth++;
                ft.iHour = 0;
                ft.iMinute = 0;
                continue;
            }
        }

        // --- Hour ---
        //
        int h = next_set_bit(crp->hour, ft.iHour);
        if (-1 == h)
        {
            ft.iDayOfMonth++;
            ft.iHour = 0;
            ft.iMinute = 0;
            continue;
        }
        if (h != ft.iHour)
        {
            ft.iHour = h;
            ft.iMinute = 0;
        }

        // --- Minute ---
        //
        int mn = next_set_bit(crp->minute, ft.iMinute);
        if (-1 == mn)
        {
            ft.iHour++;
            ft.iMinute = 0;
            if (ft.iHour >= 24)
            {
                ft.iHour = 0;
                ft.iDayOfMonth++;
            }
            continue;
        }
        ft.iMinute = mn;

        // All fields match.  Convert local fields back to UTC.
        //
        ft.iSecond = 0;
        ft.iMillisecond = 0;
        ft.iMicrosecond = 0;
        ft.iNanosecond = 0;
        ft.iDayOfWeek = cron_day_of_week(ft.iYear, ft.iMonth,
                                          ft.iDayOfMonth);

        CLinearTimeAbsolute ltaLocalTarget;
        ltaLocalTarget.SetFields(&ft);

        // Convert from local-time interpretation to UTC.
        //
        ltaLocalTarget.Local2UTC();
        ltaResult = ltaLocalTarget;
        return true;
    }

    return false;
}

// -------------------------------------------------------------------------
// Scheduler integration.
// -------------------------------------------------------------------------

static void cron_schedule(CRONTAB *crp)
{
    CLinearTimeAbsolute ltaNow;
    ltaNow.GetUTC();

    CLinearTimeAbsolute ltaNext;
    if (cron_next_time(crp, ltaNow, ltaNext))
    {
        crp->ltaNext = ltaNext;
        scheduler.DeferTask(ltaNext, PRIORITY_SYSTEM,
                            dispatch_CronEntry, crp, 0);
    }
}

static void dispatch_CronEntry(void *pArg, int iUnused)
{
    UNUSED_PARAMETER(iUnused);
    CRONTAB *crp = static_cast<CRONTAB *>(pArg);

    if (!Good_obj(crp->obj) || Going(crp->obj))
    {
        cron_remove(crp);
        return;
    }

    // Trigger the attribute.
    //
    did_it(Owner(crp->obj), crp->obj, 0, nullptr, 0, nullptr,
           crp->atr, 0, nullptr, 0);

    // Compute next fire time and reschedule.
    //
    CLinearTimeAbsolute ltaNow;
    ltaNow.GetUTC();

    CLinearTimeAbsolute ltaNext;
    if (cron_next_time(crp, ltaNow, ltaNext))
    {
        crp->ltaNext = ltaNext;
        scheduler.DeferTask(ltaNext, PRIORITY_SYSTEM,
                            dispatch_CronEntry, crp, 0);
    }
    else
    {
        cron_remove(crp);
    }
}

// -------------------------------------------------------------------------
// Entry management.
// -------------------------------------------------------------------------

static void cron_remove(CRONTAB *crp)
{
    scheduler.CancelTask(dispatch_CronEntry, crp, 0);

    CRONTAB **pp = &cron_head;
    while (nullptr != *pp && *pp != crp)
    {
        pp = &(*pp)->next;
    }
    if (nullptr != *pp)
    {
        *pp = crp->next;
    }

    free_sbuf(crp->cronstr);
    delete crp;
}

// Remove all cron entries for a given object, optionally filtered
// by attribute.  Returns the number of entries removed.
//
static int cron_clr(dbref thing, int atr)
{
    int count = 0;
    CRONTAB **pp = &cron_head;

    while (nullptr != *pp)
    {
        CRONTAB *crp = *pp;
        if (  crp->obj == thing
           && (NOTHING == atr || crp->atr == atr))
        {
            scheduler.CancelTask(dispatch_CronEntry, crp, 0);
            *pp = crp->next;
            free_sbuf(crp->cronstr);
            delete crp;
            count++;
        }
        else
        {
            pp = &crp->next;
        }
    }
    return count;
}

// Parse the 5-field timestring and create a new cron entry.
// Returns 1 on success, 0 on syntax error, -1 on duplicate.
//
static int cron_add(dbref player, dbref thing, int atr,
                    const UTF8 *timestr)
{
    // Reject duplicates.
    //
    for (CRONTAB *crp = cron_head; nullptr != crp; crp = crp->next)
    {
        if (  crp->obj == thing
           && crp->atr == atr
           && 0 == strcmp((const char *)crp->cronstr,
                          (const char *)timestr))
        {
            return -1;
        }
    }

    CRONTAB *crp = new (std::nothrow) CRONTAB;
    if (nullptr == crp)
    {
        return 0;
    }
    memset(crp, 0, sizeof(CRONTAB));
    crp->obj = thing;
    crp->atr = atr;

    UTF8 *pCronStr = alloc_sbuf("cron_add");
    mux_strncpy(pCronStr, timestr, SBUF_SIZE - 1);
    crp->cronstr = pCronStr;

    // Parse five fields: minute hour dom month dow.
    //
    const UTF8 *p = timestr;
    bool bStar;

    uint64_t bits;
    bool bDomStar, bDowStar;

    p = parse_cron_field(bits, CRON_MINUTE_MIN, CRON_MINUTE_MAX,
                         bStar, p, nullptr);
    if (nullptr == p)
    {
        free_sbuf(crp->cronstr);
        delete crp;
        return 0;
    }
    crp->minute = bits;

    p = parse_cron_field(bits, CRON_HOUR_MIN, CRON_HOUR_MAX,
                         bStar, p, nullptr);
    if (nullptr == p)
    {
        free_sbuf(crp->cronstr);
        delete crp;
        return 0;
    }
    crp->hour = (uint32_t)bits;

    p = parse_cron_field(bits, CRON_DOM_MIN, CRON_DOM_MAX,
                         bDomStar, p, nullptr);
    if (nullptr == p)
    {
        free_sbuf(crp->cronstr);
        delete crp;
        return 0;
    }
    crp->dom = (uint32_t)bits;
    if (bDomStar)
    {
        crp->flags |= CRON_DOM_STAR;
    }

    p = parse_cron_field(bits, CRON_MONTH_MIN, CRON_MONTH_MAX,
                         bStar, p, month_names);
    if (nullptr == p)
    {
        free_sbuf(crp->cronstr);
        delete crp;
        return 0;
    }
    crp->month = (uint16_t)bits;

    p = parse_cron_field(bits, CRON_DOW_MIN, CRON_DOW_MAX,
                         bDowStar, p, dow_names);
    if (nullptr == p)
    {
        free_sbuf(crp->cronstr);
        delete crp;
        return 0;
    }
    crp->dow = (uint8_t)bits;
    if (bDowStar)
    {
        crp->flags |= CRON_DOW_STAR;
    }

    // Sunday can be 0 or 7.  Map bit 7 to bit 0.
    //
    if (crp->dow & (1u << 7))
    {
        crp->dow |= 1u;
        crp->dow &= 0x7F;
    }

    // Link into list.
    //
    crp->next = cron_head;
    cron_head = crp;

    // Compute first fire time and schedule.
    //
    cron_schedule(crp);
    return 1;
}

// -------------------------------------------------------------------------
// Command handlers.
// -------------------------------------------------------------------------

void do_cron(dbref executor, dbref caller, dbref enactor, int eval,
             int key, int nargs, UTF8 *objattr, UTF8 *timestr,
             const UTF8 *cargs[], int ncargs)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(key);
    UNUSED_PARAMETER(nargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (!timestr || !*timestr)
    {
        notify(executor, T("Usage: @cron <object>/<attribute> = <timestring>"));
        return;
    }

    dbref thing;
    ATTR *pattr;
    if (!parse_attrib(executor, objattr, &thing, &pattr))
    {
        notify(executor, T("No match."));
        return;
    }
    if (NOTHING == thing || nullptr == pattr)
    {
        notify(executor, T("No match."));
        return;
    }
    if (!Controls(executor, thing))
    {
        notify(executor, NOPERM_MESSAGE);
        return;
    }

    int retcode = cron_add(executor, thing, pattr->number, timestr);
    if (0 == retcode)
    {
        notify(executor, T("Syntax errors. No cron entry made."));
    }
    else if (-1 == retcode)
    {
        notify(executor, T("That cron entry already exists."));
    }
    else
    {
        notify(executor, T("Cron entry added."));
    }
}

void do_crondel(dbref executor, dbref caller, dbref enactor, int eval,
                int key, UTF8 *objattr,
                const UTF8 *cargs[], int ncargs)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(key);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (!objattr || !*objattr)
    {
        notify(executor, T("Usage: @crondel <object>[/<attribute>]"));
        return;
    }

    // Check if there's a slash (attribute specified).
    //
    UTF8 *pSlash = (UTF8 *)strchr((const char *)objattr, '/');
    int atr = NOTHING;
    dbref thing;

    if (nullptr != pSlash)
    {
        // Has attribute.
        //
        ATTR *pattr;
        if (!parse_attrib(executor, objattr, &thing, &pattr))
        {
            notify(executor, T("No match."));
            return;
        }
        if (NOTHING == thing || nullptr == pattr)
        {
            notify(executor, T("No match."));
            return;
        }
        atr = pattr->number;
    }
    else
    {
        // Object only.
        //
        init_match(executor, objattr, NOTYPE);
        match_everything(MAT_EXIT_PARENTS);
        thing = match_result();
        if (!Good_obj(thing))
        {
            notify(executor, T("No match."));
            return;
        }
    }

    if (!Controls(executor, thing))
    {
        notify(executor, NOPERM_MESSAGE);
        return;
    }

    int count = cron_clr(thing, atr);
    notify(executor, tprintf(T("Removed %d cron %s."), count,
                             (1 == count) ? "entry" : "entries"));
}

void do_crontab(dbref executor, dbref caller, dbref enactor, int eval,
                int key, UTF8 *objarg,
                const UTF8 *cargs[], int ncargs)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(key);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref thing = NOTHING;

    if (nullptr != objarg && '\0' != *objarg)
    {
        init_match(executor, objarg, NOTYPE);
        match_everything(MAT_EXIT_PARENTS);
        thing = match_result();
        if (!Good_obj(thing))
        {
            notify(executor, T("No match."));
            return;
        }
        if (!Controls(executor, thing))
        {
            notify(executor, NOPERM_MESSAGE);
            return;
        }
    }

    int count = 0;
    for (CRONTAB *crp = cron_head; nullptr != crp; crp = crp->next)
    {
        if (  NOTHING != thing && crp->obj != thing)
        {
            continue;
        }
        if (  NOTHING == thing
           && Owner(crp->obj) != executor
           && !See_All(executor))
        {
            continue;
        }

        count++;
        ATTR *ap = atr_num(crp->atr);
        UTF8 *pObj = unparse_object(executor, crp->obj, false);

        if (nullptr == ap)
        {
            notify(executor, tprintf(T("%s has a cron entry with bad "
                                       "attribute number %d."),
                                     pObj, crp->atr));
        }
        else
        {
            // Show next fire time in local time.
            //
            FIELDEDTIME ft;
            CLinearTimeAbsolute ltaLocal = crp->ltaNext;
            ltaLocal.UTC2Local();
            ltaLocal.ReturnFields(&ft);

            notify(executor, tprintf(
                T("%s/%s: %s  [next: %04d-%02d-%02d %02d:%02d]"),
                pObj, ap->name, crp->cronstr,
                ft.iYear, ft.iMonth, ft.iDayOfMonth,
                ft.iHour, ft.iMinute));
        }
        free_lbuf(pObj);
    }

    notify(executor, tprintf(T("Matched %d cron %s."), count,
                             (1 == count) ? "entry" : "entries"));
}

// Called during object destruction to clean up cron entries.
//
void cron_clear_object(dbref thing)
{
    cron_clr(thing, NOTHING);
}
