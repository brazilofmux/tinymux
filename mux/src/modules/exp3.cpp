/*! \file exp3.cpp
 * \brief Experiment 3: Non-trivial module exercising netmux boundaries
 *
 * This module provides softcode functions that attempt to do things a
 * real module would need to do.  Each function documents the walls it
 * hits — places where the module needs to call back into netmux but
 * cannot because the required interface or function is not available.
 *
 * Functions provided:
 *
 *   mget(obj/attr)   — Read an attribute value.
 *                      WALL: No mux_IDatabase or attribute-access interface.
 *
 *   mname(obj)       — Return the name of an object.
 *                      WALL: No object-property interface.
 *
 *   mtell(obj, msg)  — Send a message to an object/player.
 *                      WALL: No notification interface.
 *
 *   mowner(obj)      — Return the dbref of the object's owner.
 *                      WALL: No object-property interface.
 *
 *   mloc(obj)        — Return the dbref of the object's location.
 *                      WALL: No object-property interface.
 *
 *   meval(obj, expr) — Evaluate a softcode expression as obj.
 *                      WALL: No evaluator interface.
 *
 *   mtype(obj)       — Return the type of an object (ROOM, PLAYER, etc.).
 *                      WALL: No object-property interface.
 *
 *   mset(obj/attr, value) — Set an attribute on an object.
 *                      WALL: No attribute-write interface.
 *
 * FINDINGS (updated as walls are hit):
 *
 * 1. ATTRIBUTE READ — A module receives (executor, fargs) but has no way
 *    to call atr_get_str() or atr_pget_str().  These are linked into
 *    netmux, not exported.  Needs: mux_IAttributeAccess with methods
 *    like GetAttribute(dbref obj, const UTF8 *attrname, ...).
 *
 * 2. OBJECT PROPERTIES — Name(), Location(), Owner(), Typeof() are all
 *    netmux-internal functions.  Even the db[] array is inaccessible.
 *    Needs: mux_IObjectInfo with methods like GetName(dbref),
 *    GetLocation(dbref), GetOwner(dbref), GetType(dbref).
 *
 * 3. PLAYER NOTIFICATION — notify() and raw_notify() are netmux
 *    functions.  No interface exists to send messages to players/objects.
 *    Needs: mux_INotify with Notify(dbref target, const UTF8 *msg).
 *
 * 4. EVALUATOR — mux_exec() is deeply embedded in netmux (accesses
 *    mudstate, db[], the parse cache, etc.).  A module cannot evaluate
 *    softcode.  Needs: mux_IEvaluator with Eval(dbref executor,
 *    const UTF8 *expr, UTF8 *result, size_t resultSize).
 *
 * 5. ATTRIBUTE WRITE — atr_add_raw_LEN() is netmux-internal.  Needs to
 *    be part of mux_IAttributeAccess with SetAttribute().
 *
 * 6. PERMISSION CHECKS — could_doit(), Examinable(), Controls() are all
 *    netmux-internal.  A module doing attribute reads would need to
 *    check permissions.  Could be methods on the attribute interface
 *    (GetAttribute does the permission check internally) or a separate
 *    mux_IPermissions interface.
 *
 * 7. DBREF PARSING — The module receives fargs as strings.  Converting
 *    "#123" to a dbref requires mux_atol() and Good_obj() validation.
 *    mux_atol() is in stringutil.cpp (netmux-internal).  Good_obj()
 *    needs db_top (netmux-internal).  Even basic argument validation
 *    requires netmux cooperation.
 *
 * 8. SAFE STRING OUTPUT — funcs.cpp had to duplicate safe_copy_str_lbuf,
 *    utf8_FirstByte[], trim_partial_sequence(), and LBUF_SIZE.  These
 *    are fundamental buffer utilities that every module needs.  They
 *    should be in libmux.so.
 *
 * 9. MATCH/LOOKUP — match_thing() resolves "me", "here", "#123", and
 *    player names to dbrefs.  Without it, the module can only accept
 *    raw dbrefs as arguments.  Would need mux_IMatch or be part of
 *    mux_IObjectInfo.
 *
 * SUMMARY OF INTERFACES NEEDED:
 *
 *   mux_IObjectInfo       — GetName, GetLocation, GetOwner, GetType,
 *                           IsValid (replaces Good_obj), GetDbTop
 *   mux_IAttributeAccess  — GetAttribute, SetAttribute (with permission
 *                           checks built in)
 *   mux_INotify           — Notify, RawNotify
 *   mux_IEvaluator        — Eval (probably too heavy/risky for Phase 3)
 *   mux_IMatch            — MatchThing (resolves name/ref to dbref)
 *
 * CANDIDATES FOR LIBMUX.SO MIGRATION:
 *
 *   safe_str / safe_copy_str_lbuf — Every module needs this
 *   LBUF_SIZE constant            — Every module needs this
 *   trim_partial_sequence          — Every module needs this
 *   mux_atol / mux_ltoa            — Basic conversions
 *   utf8_FirstByte table           — UTF-8 handling
 */

#include "../autoconf.h"
#include "../config.h"
#include "../libmux.h"
#include "../modules.h"
#include "exp3.h"

static int32_t g_cComponents  = 0;
static int32_t g_cServerLocks = 0;

static mux_IExp3SinkControl *g_pIExp3SinkControl = nullptr;

// IID for our private sink-control interface.
//
const MUX_IID IID_IExp3SinkControl = UINT64_C(0x00000002E3D2C1B0);

static MUX_CLASS_INFO exp3_classes[] =
{
    { CID_Exp3 }
};
#define NUM_CLASSES (sizeof(exp3_classes)/sizeof(exp3_classes[0]))

// ---------------------------------------------------------------------------
// Duplicated utilities.  These SHOULD be in libmux.so.
// ---------------------------------------------------------------------------

#define LBUF_SIZE   8000

static void safe_copy_str(const UTF8 *src, UTF8 *buff, UTF8 **bufp)
{
    if (nullptr == src) return;
    UTF8 *tp = *bufp;
    const UTF8 *maxtp = buff + LBUF_SIZE - 1;
    while (tp < maxtp && *src)
    {
        *tp++ = *src++;
    }
    *bufp = tp;
}

static void safe_ltoa(int val, UTF8 *buff, UTF8 **bufp)
{
    UTF8 tbuf[32];
    size_t n = snprintf(reinterpret_cast<char *>(tbuf), sizeof(tbuf), "%d", val);
    if (n > sizeof(tbuf) - 1) n = sizeof(tbuf) - 1;
    tbuf[n] = '\0';
    safe_copy_str(tbuf, buff, bufp);
}

// Parse a dbref from a string like "#123".  Returns -1 on failure.
// WALL: This is a crude approximation.  The real mux_atol() and Good_obj()
// are in netmux.  We can't validate that the dbref actually exists without
// db_top, which is inaccessible.
//
static int parse_dbref(const UTF8 *str)
{
    if (nullptr == str) return -1;
    const char *p = reinterpret_cast<const char *>(str);
    if ('#' == *p) p++;
    char *end;
    long val = strtol(p, &end, 10);
    if (end == p) return -1;
    if (*end != '\0' && *end != '/') return -1;
    if (val < 0) return -1;
    return static_cast<int>(val);
}

// ---------------------------------------------------------------------------
// Module entry points.
// ---------------------------------------------------------------------------

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_CanUnloadNow(void)
{
    if (  0 == g_cComponents
       && 0 == g_cServerLocks)
    {
        return MUX_S_OK;
    }
    return MUX_S_FALSE;
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_GetClassObject(MUX_CID cid, MUX_IID iid, void **ppv)
{
    MUX_RESULT mr = MUX_E_CLASSNOTAVAILABLE;

    if (CID_Exp3 == cid)
    {
        CExp3Factory *pFactory = nullptr;
        try
        {
            pFactory = new CExp3Factory;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (nullptr == pFactory)
        {
            return MUX_E_OUTOFMEMORY;
        }

        mr = pFactory->QueryInterface(iid, ppv);
        pFactory->Release();
    }
    return mr;
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_Register(void)
{
    MUX_RESULT mr = MUX_E_UNEXPECTED;

    if (nullptr == g_pIExp3SinkControl)
    {
        mr = mux_RegisterClassObjects(NUM_CLASSES, exp3_classes, nullptr);
        if (MUX_FAILED(mr))
        {
            return mr;
        }

        mux_IExp3SinkControl *pSink = nullptr;
        mr = mux_CreateInstance(CID_Exp3, nullptr, UseSameProcess,
                                IID_IExp3SinkControl,
                                reinterpret_cast<void **>(&pSink));
        if (MUX_SUCCEEDED(mr))
        {
            g_pIExp3SinkControl = pSink;
        }
        else
        {
            (void)mux_RevokeClassObjects(NUM_CLASSES, exp3_classes);
            mr = MUX_E_OUTOFMEMORY;
        }
    }
    return mr;
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_Unregister(void)
{
    if (nullptr != g_pIExp3SinkControl)
    {
        g_pIExp3SinkControl->Unregistering();
        g_pIExp3SinkControl->Release();
        g_pIExp3SinkControl = nullptr;
    }
    return mux_RevokeClassObjects(NUM_CLASSES, exp3_classes);
}

// ---------------------------------------------------------------------------
// CExp3 component.
// ---------------------------------------------------------------------------

CExp3::CExp3(void) : m_cRef(1), m_pILog(nullptr), m_pIFunctionsControl(nullptr)
{
    g_cComponents++;
}

MUX_RESULT CExp3::FinalConstruct(void)
{
    // Get logging interface.
    //
    MUX_RESULT mr = mux_CreateInstance(CID_Log, nullptr, UseSameProcess,
                                        IID_ILog,
                                        reinterpret_cast<void **>(&m_pILog));
    if (MUX_SUCCEEDED(mr))
    {
        bool fStarted;
        mr = m_pILog->start_log(&fStarted, LOG_ALWAYS, T("INI"), T("INFO"));
        if (MUX_SUCCEEDED(mr) && fStarted)
        {
            m_pILog->log_text(T("Experiment 3 module loaded."));
            m_pILog->end_log();
        }
    }

    // Register softcode functions.
    //
    mr = mux_CreateInstance(CID_Functions, nullptr, UseSameProcess,
                            IID_IFunctionsControl,
                            reinterpret_cast<void **>(&m_pIFunctionsControl));
    if (MUX_SUCCEEDED(mr))
    {
        mux_IFunction *pIFunction = nullptr;
        mr = QueryInterface(IID_IFunction, reinterpret_cast<void **>(&pIFunction));
        if (MUX_SUCCEEDED(mr))
        {
            // Register all experiment functions.
            // nKey 0..7 mapped in Call() below.
            //
            m_pIFunctionsControl->Add(0, T("MGET"),   pIFunction, 2, 1, 2, 0, 0);
            m_pIFunctionsControl->Add(1, T("MNAME"),  pIFunction, 1, 1, 1, 0, 0);
            m_pIFunctionsControl->Add(2, T("MTELL"),  pIFunction, 2, 2, 2, 0, 0);
            m_pIFunctionsControl->Add(3, T("MOWNER"), pIFunction, 1, 1, 1, 0, 0);
            m_pIFunctionsControl->Add(4, T("MLOC"),   pIFunction, 1, 1, 1, 0, 0);
            m_pIFunctionsControl->Add(5, T("MEVAL"),  pIFunction, 2, 2, 2, 0, 0);
            m_pIFunctionsControl->Add(6, T("MTYPE"),  pIFunction, 1, 1, 1, 0, 0);
            m_pIFunctionsControl->Add(7, T("MSET"),   pIFunction, 2, 2, 2, 0, 0);
            pIFunction->Release();
        }
    }

    return mr;
}

CExp3::~CExp3()
{
    if (nullptr != m_pILog)
    {
        bool fStarted;
        MUX_RESULT mr = m_pILog->start_log(&fStarted, LOG_ALWAYS, T("INI"), T("INFO"));
        if (MUX_SUCCEEDED(mr) && fStarted)
        {
            m_pILog->log_text(T("Experiment 3 module unloaded."));
            m_pILog->end_log();
        }
        m_pILog->Release();
        m_pILog = nullptr;
    }

    if (nullptr != m_pIFunctionsControl)
    {
        m_pIFunctionsControl->Release();
        m_pIFunctionsControl = nullptr;
    }

    g_cComponents--;
}

MUX_RESULT CExp3::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_IExp3SinkControl *>(this);
    }
    else if (IID_IExp3SinkControl == iid)
    {
        *ppv = static_cast<mux_IExp3SinkControl *>(this);
    }
    else if (IID_IFunction == iid)
    {
        *ppv = static_cast<mux_IFunction *>(this);
    }
    else
    {
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

uint32_t CExp3::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CExp3::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

void CExp3::Unregistering(void)
{
    if (nullptr != m_pIFunctionsControl)
    {
        m_pIFunctionsControl->Release();
        m_pIFunctionsControl = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Function implementations.  Each documents why it can't do what it needs to.
// ---------------------------------------------------------------------------

MUX_RESULT CExp3::Call(unsigned int nKey, UTF8 *buff, UTF8 **bufc,
                        dbref executor, dbref caller, dbref enactor,
                        int eval, UTF8 *fargs[], int nfargs,
                        const UTF8 *cargs[], int ncargs)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    switch (nKey)
    {
    case 0: // MGET(obj/attr) or MGET(obj, attr)
        {
            // Parse the argument.  We receive the obj/attr string but have
            // no way to resolve it.
            //
            // WALL 1: Cannot call atr_get_str() — it's in netmux.
            // WALL 6: Cannot call Examinable()/could_doit() for permission
            //          checks.
            // WALL 7: Cannot call match_thing() to resolve "me"/"here".
            // WALL 7: Cannot validate dbref with Good_obj() (needs db_top).
            // WALL 8: Had to duplicate safe_copy_str (should be in libmux).
            //
            // What we CAN do: parse the string, but we hit a dead end
            // because we can't read the database.
            //
            int obj = parse_dbref(fargs[0]);
            if (obj < 0)
            {
                safe_copy_str(T("#-1 INVALID DBREF"), buff, bufc);
            }
            else
            {
                // We have a dbref number but cannot:
                // - Verify it exists (need db_top)
                // - Read any attribute (need atr_get_str)
                // - Check read permissions (need Examinable)
                //
                safe_copy_str(T("#-1 NO ATTRIBUTE ACCESS INTERFACE"), buff, bufc);
            }
        }
        break;

    case 1: // MNAME(obj)
        {
            // WALL 2: Cannot call Name() — it's in netmux.
            // WALL 7: Cannot validate the dbref.
            // WALL 9: Cannot call match_thing() to resolve names.
            //
            int obj = parse_dbref(fargs[0]);
            if (obj < 0)
            {
                safe_copy_str(T("#-1 INVALID DBREF"), buff, bufc);
            }
            else
            {
                safe_copy_str(T("#-1 NO OBJECT INFO INTERFACE"), buff, bufc);
            }
        }
        break;

    case 2: // MTELL(obj, msg)
        {
            // WALL 3: Cannot call notify() or raw_notify() — in netmux.
            // WALL 7: Cannot validate the target dbref.
            // WALL 6: Cannot check if executor has permission to notify.
            //
            int obj = parse_dbref(fargs[0]);
            if (obj < 0)
            {
                safe_copy_str(T("#-1 INVALID DBREF"), buff, bufc);
            }
            else
            {
                // We have the target dbref and the message text in fargs[1],
                // but we have no way to deliver the message.
                //
                safe_copy_str(T("#-1 NO NOTIFY INTERFACE"), buff, bufc);
            }
        }
        break;

    case 3: // MOWNER(obj)
        {
            // WALL 2: Cannot call Owner() — it's in netmux.
            //
            int obj = parse_dbref(fargs[0]);
            if (obj < 0)
            {
                safe_copy_str(T("#-1 INVALID DBREF"), buff, bufc);
            }
            else
            {
                safe_copy_str(T("#-1 NO OBJECT INFO INTERFACE"), buff, bufc);
            }
        }
        break;

    case 4: // MLOC(obj)
        {
            // WALL 2: Cannot call Location() — it's in netmux (db[obj].location).
            //
            int obj = parse_dbref(fargs[0]);
            if (obj < 0)
            {
                safe_copy_str(T("#-1 INVALID DBREF"), buff, bufc);
            }
            else
            {
                safe_copy_str(T("#-1 NO OBJECT INFO INTERFACE"), buff, bufc);
            }
        }
        break;

    case 5: // MEVAL(obj, expr)
        {
            // WALL 4: Cannot call mux_exec() — deeply embedded in netmux.
            //         Accesses mudstate, db[], parse cache, register stacks,
            //         iteration context, etc.  This is the hardest wall.
            //
            safe_copy_str(T("#-1 NO EVALUATOR INTERFACE"), buff, bufc);
        }
        break;

    case 6: // MTYPE(obj)
        {
            // WALL 2: Cannot call Typeof() — needs db[obj].flags access.
            //
            int obj = parse_dbref(fargs[0]);
            if (obj < 0)
            {
                safe_copy_str(T("#-1 INVALID DBREF"), buff, bufc);
            }
            else
            {
                safe_copy_str(T("#-1 NO OBJECT INFO INTERFACE"), buff, bufc);
            }
        }
        break;

    case 7: // MSET(obj/attr, value)
        {
            // WALL 5: Cannot call atr_add_raw_LEN() — in netmux.
            // WALL 6: Cannot check write permissions.
            //
            int obj = parse_dbref(fargs[0]);
            if (obj < 0)
            {
                safe_copy_str(T("#-1 INVALID DBREF"), buff, bufc);
            }
            else
            {
                safe_copy_str(T("#-1 NO ATTRIBUTE WRITE INTERFACE"), buff, bufc);
            }
        }
        break;
    }

    return MUX_S_OK;
}

// ---------------------------------------------------------------------------
// CExp3Factory — boilerplate.
// ---------------------------------------------------------------------------

CExp3Factory::CExp3Factory(void) : m_cRef(1)
{
}

CExp3Factory::~CExp3Factory()
{
}

MUX_RESULT CExp3Factory::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_IClassFactory *>(this);
    }
    else if (mux_IID_IClassFactory == iid)
    {
        *ppv = static_cast<mux_IClassFactory *>(this);
    }
    else
    {
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

uint32_t CExp3Factory::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CExp3Factory::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CExp3Factory::CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv)
{
    if (nullptr != pUnknownOuter)
    {
        return MUX_E_NOAGGREGATION;
    }

    CExp3 *pExp3 = nullptr;
    try
    {
        pExp3 = new CExp3;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == pExp3)
    {
        return MUX_E_OUTOFMEMORY;
    }

    MUX_RESULT mr = pExp3->FinalConstruct();
    if (MUX_FAILED(mr))
    {
        pExp3->Release();
        return mr;
    }

    mr = pExp3->QueryInterface(iid, ppv);
    pExp3->Release();
    return mr;
}

MUX_RESULT CExp3Factory::LockServer(bool bLock)
{
    if (bLock)
    {
        g_cServerLocks++;
    }
    else
    {
        g_cServerLocks--;
    }
    return MUX_S_OK;
}
