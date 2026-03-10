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
 *                      RESOLVED: Uses mux_IAttributeAccess::GetAttribute().
 *
 *   mname(obj)       — Return the name of an object.
 *                      RESOLVED: Uses mux_IObjectInfo::GetName().
 *
 *   mtell(obj, msg)  — Send a message to an object/player.
 *                      RESOLVED: Uses mux_INotify::Notify().
 *
 *   mowner(obj)      — Return the dbref of the object's owner.
 *                      RESOLVED: Uses mux_IObjectInfo::GetOwner().
 *
 *   mloc(obj)        — Return the dbref of the object's location.
 *                      RESOLVED: Uses mux_IObjectInfo::GetLocation().
 *
 *   meval(obj, expr) — Evaluate a softcode expression as obj.
 *                      RESOLVED: Uses mux_IEvaluator::Eval().
 *
 *   mtype(obj)       — Return the type of an object (ROOM, PLAYER, etc.).
 *                      RESOLVED: Uses mux_IObjectInfo::GetType().
 *
 *   mset(obj/attr, value) — Set an attribute on an object.
 *                      RESOLVED: Uses mux_IAttributeAccess::SetAttribute().
 *
 *   mhelp(topic) or mhelp(helpfile, topic) — Look up a help topic.
 *                      RESOLVED: Uses mux_IHelpSystem::LookupTopic().
 *
 * FINDINGS (updated as walls are hit):
 *
 * 1. ATTRIBUTE READ — RESOLVED.  mux_IAttributeAccess (CID_AttributeAccess)
 *    provides GetAttribute(executor, obj, attrname, ...) with built-in
 *    permission checks via bCanReadAttr().  Implemented in modules.cpp.
 *
 * 2. OBJECT PROPERTIES — RESOLVED.  mux_IObjectInfo (CID_ObjectInfo)
 *    provides IsValid, GetName, GetOwner, GetLocation, GetType.
 *    Implemented in modules.cpp as CObjectInfo.
 *
 * 3. PLAYER NOTIFICATION — RESOLVED.  mux_INotify (CID_Notify)
 *    provides Notify and RawNotify.
 *    Implemented in modules.cpp as CNotify.
 *
 * 4. EVALUATOR — RESOLVED.  mux_IEvaluator (CID_Evaluator) provides
 *    Eval(executor, caller, enactor, expr, result, ...).  Wraps
 *    mux_exec() in modules.cpp as CEvaluator.
 *
 * 5. ATTRIBUTE WRITE — RESOLVED.  mux_IAttributeAccess::SetAttribute()
 *    wraps atr_add() with bCanSetAttr() permission checks.
 *
 * 6. PERMISSION CHECKS — RESOLVED for attributes.  GetAttribute() uses
 *    bCanReadAttr() internally; SetAttribute() uses bCanSetAttr().
 *    General permission checks (Controls, Examinable) are not yet
 *    exposed as a separate interface.
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

#include "autoconf.h"
#include "config.h"
#include "libmux.h"
#include "modules.h"
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

CExp3::CExp3(void) : m_cRef(1), m_pILog(nullptr), m_pIFunctionsControl(nullptr),
    m_pINotify(nullptr), m_pIObjectInfo(nullptr),
    m_pIAttributeAccess(nullptr), m_pIEvaluator(nullptr),
    m_pIHelpSystem(nullptr)
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
            m_pIFunctionsControl->Add(8, T("MHELP"),  pIFunction, 1, 1, 2, 0, 0);
            pIFunction->Release();
        }
    }

    // Acquire notification interface.
    //
    mux_CreateInstance(CID_Notify, nullptr, UseSameProcess,
                       IID_INotify,
                       reinterpret_cast<void **>(&m_pINotify));

    // Acquire object info interface.
    //
    mux_CreateInstance(CID_ObjectInfo, nullptr, UseSameProcess,
                       IID_IObjectInfo,
                       reinterpret_cast<void **>(&m_pIObjectInfo));

    mux_CreateInstance(CID_AttributeAccess, nullptr, UseSameProcess,
                       IID_IAttributeAccess,
                       reinterpret_cast<void **>(&m_pIAttributeAccess));

    mux_CreateInstance(CID_Evaluator, nullptr, UseSameProcess,
                       IID_IEvaluator,
                       reinterpret_cast<void **>(&m_pIEvaluator));

    mux_CreateInstance(CID_HelpSystem, nullptr, UseSameProcess,
                       IID_IHelpSystem,
                       reinterpret_cast<void **>(&m_pIHelpSystem));

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

    if (nullptr != m_pINotify)
    {
        m_pINotify->Release();
        m_pINotify = nullptr;
    }

    if (nullptr != m_pIObjectInfo)
    {
        m_pIObjectInfo->Release();
        m_pIObjectInfo = nullptr;
    }

    if (nullptr != m_pIAttributeAccess)
    {
        m_pIAttributeAccess->Release();
        m_pIAttributeAccess = nullptr;
    }

    if (nullptr != m_pIEvaluator)
    {
        m_pIEvaluator->Release();
        m_pIEvaluator = nullptr;
    }

    if (nullptr != m_pIHelpSystem)
    {
        m_pIHelpSystem->Release();
        m_pIHelpSystem = nullptr;
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
            int obj;
            const UTF8 *pAttrName;
            if (2 == nfargs)
            {
                // Two-arg form: mget(#obj, attrname)
                //
                obj = parse_dbref(fargs[0]);
                pAttrName = fargs[1];
            }
            else
            {
                // One-arg form: mget(#obj/attrname)
                //
                UTF8 *pSlash = (UTF8 *)strchr((const char *)fargs[0], '/');
                if (nullptr == pSlash)
                {
                    safe_copy_str(T("#-1 NO ATTR NAME"), buff, bufc);
                    break;
                }
                *pSlash = '\0';
                obj = parse_dbref(fargs[0]);
                pAttrName = pSlash + 1;
            }

            if (obj < 0)
            {
                safe_copy_str(T("#-1 INVALID DBREF"), buff, bufc);
            }
            else if (nullptr == m_pIAttributeAccess)
            {
                safe_copy_str(T("#-1 NO ATTRIBUTE ACCESS INTERFACE"), buff, bufc);
            }
            else
            {
                UTF8 value[LBUF_SIZE];
                size_t nLen;
                MUX_RESULT mr = m_pIAttributeAccess->GetAttribute(
                    executor, obj, pAttrName, value, sizeof(value), &nLen);
                if (MUX_E_INVALIDARG == mr)
                {
                    safe_copy_str(T("#-1 NO SUCH OBJECT"), buff, bufc);
                }
                else if (MUX_E_NOTFOUND == mr)
                {
                    safe_copy_str(T("#-1 NO SUCH ATTRIBUTE"), buff, bufc);
                }
                else if (MUX_E_PERMISSION == mr)
                {
                    safe_copy_str(T("#-1 PERMISSION DENIED"), buff, bufc);
                }
                else if (MUX_SUCCEEDED(mr))
                {
                    safe_copy_str(value, buff, bufc);
                }
            }
        }
        break;

    case 1: // MNAME(obj)
        {
            int obj = parse_dbref(fargs[0]);
            if (obj < 0)
            {
                safe_copy_str(T("#-1 INVALID DBREF"), buff, bufc);
            }
            else if (nullptr == m_pIObjectInfo)
            {
                safe_copy_str(T("#-1 NO OBJECT INFO INTERFACE"), buff, bufc);
            }
            else
            {
                bool bValid;
                m_pIObjectInfo->IsValid(obj, &bValid);
                if (!bValid)
                {
                    safe_copy_str(T("#-1 NO SUCH OBJECT"), buff, bufc);
                }
                else
                {
                    const UTF8 *pName = nullptr;
                    m_pIObjectInfo->GetName(obj, &pName);
                    if (nullptr != pName)
                    {
                        safe_copy_str(pName, buff, bufc);
                    }
                }
            }
        }
        break;

    case 2: // MTELL(obj, msg)
        {
            int obj = parse_dbref(fargs[0]);
            if (obj < 0)
            {
                safe_copy_str(T("#-1 INVALID DBREF"), buff, bufc);
            }
            else if (nullptr == m_pINotify)
            {
                safe_copy_str(T("#-1 NO NOTIFY INTERFACE"), buff, bufc);
            }
            else
            {
                MUX_RESULT mr = m_pINotify->Notify(obj, fargs[1]);
                if (MUX_FAILED(mr))
                {
                    safe_copy_str(T("#-1 NO SUCH OBJECT"), buff, bufc);
                }
            }
        }
        break;

    case 3: // MOWNER(obj)
        {
            int obj = parse_dbref(fargs[0]);
            if (obj < 0)
            {
                safe_copy_str(T("#-1 INVALID DBREF"), buff, bufc);
            }
            else if (nullptr == m_pIObjectInfo)
            {
                safe_copy_str(T("#-1 NO OBJECT INFO INTERFACE"), buff, bufc);
            }
            else
            {
                bool bValid;
                m_pIObjectInfo->IsValid(obj, &bValid);
                if (!bValid)
                {
                    safe_copy_str(T("#-1 NO SUCH OBJECT"), buff, bufc);
                }
                else
                {
                    dbref owner;
                    m_pIObjectInfo->GetOwner(obj, &owner);
                    safe_copy_str(T("#"), buff, bufc);
                    safe_ltoa(owner, buff, bufc);
                }
            }
        }
        break;

    case 4: // MLOC(obj)
        {
            int obj = parse_dbref(fargs[0]);
            if (obj < 0)
            {
                safe_copy_str(T("#-1 INVALID DBREF"), buff, bufc);
            }
            else if (nullptr == m_pIObjectInfo)
            {
                safe_copy_str(T("#-1 NO OBJECT INFO INTERFACE"), buff, bufc);
            }
            else
            {
                bool bValid;
                m_pIObjectInfo->IsValid(obj, &bValid);
                if (!bValid)
                {
                    safe_copy_str(T("#-1 NO SUCH OBJECT"), buff, bufc);
                }
                else
                {
                    dbref loc;
                    m_pIObjectInfo->GetLocation(obj, &loc);
                    safe_copy_str(T("#"), buff, bufc);
                    safe_ltoa(loc, buff, bufc);
                }
            }
        }
        break;

    case 5: // MEVAL(obj, expr)
        {
            int obj = parse_dbref(fargs[0]);
            if (obj < 0)
            {
                safe_copy_str(T("#-1 INVALID DBREF"), buff, bufc);
            }
            else if (nullptr == m_pIEvaluator)
            {
                safe_copy_str(T("#-1 NO EVALUATOR INTERFACE"), buff, bufc);
            }
            else
            {
                UTF8 result[LBUF_SIZE];
                size_t nLen;
                MUX_RESULT mr = m_pIEvaluator->Eval(
                    obj, executor, executor,
                    fargs[1], result, sizeof(result), &nLen);
                if (MUX_SUCCEEDED(mr))
                {
                    safe_copy_str(result, buff, bufc);
                }
                else
                {
                    safe_copy_str(T("#-1 EVAL FAILED"), buff, bufc);
                }
            }
        }
        break;

    case 6: // MTYPE(obj)
        {
            int obj = parse_dbref(fargs[0]);
            if (obj < 0)
            {
                safe_copy_str(T("#-1 INVALID DBREF"), buff, bufc);
            }
            else if (nullptr == m_pIObjectInfo)
            {
                safe_copy_str(T("#-1 NO OBJECT INFO INTERFACE"), buff, bufc);
            }
            else
            {
                bool bValid;
                m_pIObjectInfo->IsValid(obj, &bValid);
                if (!bValid)
                {
                    safe_copy_str(T("#-1 NO SUCH OBJECT"), buff, bufc);
                }
                else
                {
                    int nType;
                    m_pIObjectInfo->GetType(obj, &nType);
                    switch (nType)
                    {
                    case 0: safe_copy_str(T("ROOM"), buff, bufc);   break;
                    case 1: safe_copy_str(T("THING"), buff, bufc);  break;
                    case 2: safe_copy_str(T("EXIT"), buff, bufc);   break;
                    case 3: safe_copy_str(T("PLAYER"), buff, bufc); break;
                    default: safe_copy_str(T("GARBAGE"), buff, bufc); break;
                    }
                }
            }
        }
        break;

    case 7: // MSET(obj/attr, value)
        {
            UTF8 *pSlash = (UTF8 *)strchr((const char *)fargs[0], '/');
            if (nullptr == pSlash)
            {
                safe_copy_str(T("#-1 NO ATTR NAME"), buff, bufc);
                break;
            }
            *pSlash = '\0';
            int obj = parse_dbref(fargs[0]);
            const UTF8 *pAttrName = pSlash + 1;

            if (obj < 0)
            {
                safe_copy_str(T("#-1 INVALID DBREF"), buff, bufc);
            }
            else if (nullptr == m_pIAttributeAccess)
            {
                safe_copy_str(T("#-1 NO ATTRIBUTE WRITE INTERFACE"), buff, bufc);
            }
            else
            {
                MUX_RESULT mr = m_pIAttributeAccess->SetAttribute(
                    executor, obj, pAttrName, fargs[1]);
                if (MUX_E_INVALIDARG == mr)
                {
                    safe_copy_str(T("#-1 NO SUCH OBJECT"), buff, bufc);
                }
                else if (MUX_E_NOTFOUND == mr)
                {
                    safe_copy_str(T("#-1 NO SUCH ATTRIBUTE"), buff, bufc);
                }
                else if (MUX_E_PERMISSION == mr)
                {
                    safe_copy_str(T("#-1 PERMISSION DENIED"), buff, bufc);
                }
                // On success, return empty (like @set).
            }
        }
        break;

    case 8: // MHELP(topic) or MHELP(helpfile, topic)
        {
            if (nullptr == m_pIHelpSystem)
            {
                safe_copy_str(T("#-1 NO HELP SYSTEM INTERFACE"), buff, bufc);
            }
            else
            {
                int iHelpfile = 0; // Default to first help file.
                const UTF8 *pTopic;

                if (2 == nfargs)
                {
                    // Two-arg form: mhelp(helpfile, topic)
                    //
                    MUX_RESULT mr = m_pIHelpSystem->FindHelpFile(
                        fargs[0], &iHelpfile);
                    if (MUX_FAILED(mr))
                    {
                        safe_copy_str(T("#-1 NO SUCH HELPFILE"), buff, bufc);
                        break;
                    }
                    pTopic = fargs[1];
                }
                else
                {
                    pTopic = fargs[0];
                }

                UTF8 result[LBUF_SIZE];
                size_t nLen;
                MUX_RESULT mr = m_pIHelpSystem->LookupTopic(
                    executor, iHelpfile, pTopic, result, sizeof(result) - 1,
                    &nLen);
                if (MUX_SUCCEEDED(mr))
                {
                    safe_copy_str(result, buff, bufc);
                }
                else
                {
                    safe_copy_str(T("#-1 TOPIC NOT FOUND"), buff, bufc);
                }
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
