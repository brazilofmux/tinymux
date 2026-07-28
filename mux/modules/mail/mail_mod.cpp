/*! \file mail_mod.cpp
 * \brief Mail Module — @mail system as a loadable module
 *
 * This module implements the MUX @mail system as a dynamically loaded
 * module.  It hooks into server events for player connect/disconnect
 * and provides mux_IMailControl for command dispatch.
 *
 * Core dependencies are accessed exclusively through COM interfaces:
 *   mux_INotify           — player notification
 *   mux_IObjectInfo       — object property queries
 *   mux_IAttributeAccess  — attribute read/write
 *   mux_IPermissions      — permission checks
 *   mux_ILog              — logging
 *
 * Mail data is loaded from and persisted to the game's SQLite database
 * via the mux_IMailStorage COM interface provided by the engine.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "libmux.h"
#include "modules.h"
#include "mail_mod.h"
#include "mux_nls.h"
#include "mux_format.h"
#include "color_ops.h"
#include "mux_table.h"

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <cctype>

// Windows compatibility for POSIX functions.
//
#ifdef _MSC_VER
#define strcasecmp  _stricmp
#define strtok_r    strtok_s
#endif

// Column layout: libmux mux_table_* on co_copy_field (#1667 Phase 3).
//
#define append_ljust_field  mux_table_append_ljust
#define append_trunc_field  mux_table_append_trunc
#define append_bytes        mux_table_append_bytes
#define pos_after           mux_table_pos_after

// mux_sprintf is void; call sites that need the written length (bp += …,
// truncation checks against remain) go through this.  Returns bytes written
// excluding the trailing NUL — the same contract as mux_vsnprintf.
//
static size_t mail_sprintf(UTF8 *buf, size_t nBuf, const UTF8 *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    size_t n = mux_vsnprintf(buf, nBuf, fmt, ap);
    va_end(ap);
    return n;
}

// Folder list / review summary line:
//   [flags] iii (ssss) From: name............ Sub: subject...
//
static void format_mail_list_line(
    UTF8 *line, size_t nLine,
    const UTF8 *status, int i, size_t nSize,
    const UTF8 *from, const UTF8 *subject)
{
    size_t pos = 0;
    pos = append_bytes(line, nLine, pos, "[");
    pos = append_bytes(line, nLine, pos,
        reinterpret_cast<const char *>(status));
    pos = append_bytes(line, nLine, pos, "] ");
    if (pos < nLine)
    {
        mux_sprintf(line + pos, nLine - pos, T("%-3d (%4zu) From: "),
            i, nSize);
        pos = pos_after(line, nLine, pos);
    }
    pos = append_ljust_field(line, nLine, pos, from, 16);
    pos = append_bytes(line, nLine, pos, " Sub: ");
    append_trunc_field(line, nLine, pos, subject, 25);
}

// Read / review detail header.  subject_cols 0 means no visual truncate
// (full subject, still buffer-capped by co_copy_field / mux assembly).
//
static void format_mail_detail_header(
    UTF8 *hdr, size_t nHdr,
    int i, const UTF8 *from, const UTF8 *time_str, bool bConn,
    int folder,
    const UTF8 *status_lead,
    const char *extra1, const char *extra2, const char *extra3,
    const char *extra4, const char *extra5, const char *extra6,
    const UTF8 *subject, size_t subject_cols)
{
    size_t pos = 0;
    if (pos < nHdr)
    {
        mux_sprintf(hdr + pos, nHdr - pos, T("%-3d         From:  "), i);
        pos = pos_after(hdr, nHdr, pos);
    }
    pos = append_ljust_field(hdr, nHdr, pos, from, 16);
    pos = append_bytes(hdr, nHdr, pos, "  At: ");
    pos = append_ljust_field(hdr, nHdr, pos, time_str, 25);
    // Format was "At: %-25s  %s" with %s = " (Conn)" / "      ": two
    // literal spaces between the padded date and the status arg.  Dropping
    // them shortens the gap from 4 columns to 2 and fails conformance.
    //
    pos = append_bytes(hdr, nHdr, pos, "  ");
    pos = append_bytes(hdr, nHdr, pos, bConn ? " (Conn)" : "      ");
    pos = append_bytes(hdr, nHdr, pos, "\r\n");
    if (pos < nHdr)
    {
        mux_sprintf(hdr + pos, nHdr - pos,
            T("Fldr   : %-2d Status: %s%s%s%s%s%s%s\r\nSubject: "),
            folder,
            status_lead,
            (nullptr != extra1) ? extra1 : "",
            (nullptr != extra2) ? extra2 : "",
            (nullptr != extra3) ? extra3 : "",
            (nullptr != extra4) ? extra4 : "",
            (nullptr != extra5) ? extra5 : "",
            (nullptr != extra6) ? extra6 : "");
        pos = pos_after(hdr, nHdr, pos);
    }
    if (0 == subject_cols)
    {
        // Full subject into the remainder; codepoint-safe via mux_sprintf.
        //
        if (pos < nHdr)
        {
            mux_sprintf(hdr + pos, nHdr - pos, T("%s"),
                (nullptr != subject) ? subject : T(""));
        }
    }
    else
    {
        append_trunc_field(hdr, nHdr, pos, subject, subject_cols);
    }
}

// "To: name" banner used by @mail/review.
//
static void format_mail_to_banner(UTF8 *hdr, size_t nHdr, const UTF8 *name)
{
    size_t pos = 0;
    pos = append_bytes(hdr, nHdr, pos,
        "–––––"
        "–––––"
        "–––––"
        "–––––"
        "–––"
        "   To: ");
    pos = append_ljust_field(hdr, nHdr, pos, name, 25);
    append_bytes(hdr, nHdr, pos,
        "   "
        "–––––"
        "–––––"
        "–––––"
        "–––––"
        "–––");
}

// En-dash line for mail display borders.
//
static const UTF8 *MOD_DASH_LINE =
    T("––––––"
      "––––––"
      "––––––"
      "––––––"
      "––––––"
      "––––––"
      "––––––"
      "––––––"
      "––––––"
      "––––––"
      "––––––"
      "––––––"
      "–––");

// Module bookkeeping.
//
static std::atomic<uint32_t> g_cComponents{0};
static std::atomic<uint32_t> g_cServerLocks{0};

// Module entry points.
//
static MUX_CLASS_INFO mail_classes[] =
{
    { CID_MailMod }
};
#define NUM_CLASSES (sizeof(mail_classes)/sizeof(mail_classes[0]))

// Required by libmux ModuleLoad — without this, bLoaded stays false and
// CreateInstance never finds the class (#1191 root cause).
//
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_CanUnloadNow(void)
{
    if (  0 == g_cComponents
       && 0 == g_cServerLocks)
    {
        return MUX_S_OK;
    }
    return MUX_S_FALSE;
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_Register(void)
{
    MUX_RESULT mr = MUX_E_UNEXPECTED;

    if (  0 == g_cComponents
       && 0 == g_cServerLocks)
    {
        mr = mux_RegisterClassObjects(NUM_CLASSES, mail_classes, nullptr);
    }
    return mr;
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_Unregister(void)
{
    return mux_RevokeClassObjects(NUM_CLASSES, mail_classes);
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_GetClassObject(MUX_CID cid, MUX_IID iid, void **ppv)
{
    MUX_RESULT mr = MUX_E_CLASSNOTAVAILABLE;

    if (CID_MailMod == cid)
    {
        CMailModFactory *pFactory = nullptr;
        try
        {
            pFactory = new CMailModFactory;
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

// ---------------------------------------------------------------------------
// CMailMod — main module class.
// ---------------------------------------------------------------------------

CMailMod::CMailMod(void) : m_cRef(1),
    m_pILog(nullptr),
    m_pIServerEventsControl(nullptr),
    m_pINotify(nullptr),
    m_pIObjectInfo(nullptr),
    m_pIAttributeAccess(nullptr),
    m_pIPermissions(nullptr),
    m_pIMailDelivery(nullptr),
    m_pIStorage(nullptr),
    m_revision(0),
    m_mail_expiration(14),
    m_mail_per_player(250),
    m_bLoading(false)
{
    g_cComponents++;
}

MUX_RESULT CMailMod::FinalConstruct(void)
{
    MUX_RESULT mr;

    // Acquire logging interface.
    //
    mr = mux_CreateInstance(CID_Log, nullptr, UseSameProcess,
                            IID_ILog,
                            reinterpret_cast<void **>(&m_pILog));
    if (MUX_FAILED(mr))
    {
        return mr;
    }

    // Register for server events.
    //
    mux_IServerEventsSink *pIServerEventsSink = nullptr;
    mr = QueryInterface(IID_IServerEventsSink,
                        reinterpret_cast<void **>(&pIServerEventsSink));
    if (MUX_SUCCEEDED(mr))
    {
        mr = mux_CreateInstance(CID_ServerEventsSource, nullptr,
                                UseSameProcess, IID_IServerEventsControl,
                                reinterpret_cast<void **>(&m_pIServerEventsControl));
        if (MUX_SUCCEEDED(mr))
        {
            m_pIServerEventsControl->Advise(pIServerEventsSink);
        }
        pIServerEventsSink->Release();
    }

    // Acquire core interfaces.
    //
    mr = mux_CreateInstance(CID_Notify, nullptr, UseSameProcess,
                            IID_INotify,
                            reinterpret_cast<void **>(&m_pINotify));
    if (MUX_FAILED(mr))
    {
        return mr;
    }

    mr = mux_CreateInstance(CID_ObjectInfo, nullptr, UseSameProcess,
                            IID_IObjectInfo,
                            reinterpret_cast<void **>(&m_pIObjectInfo));
    if (MUX_FAILED(mr))
    {
        return mr;
    }

    mr = mux_CreateInstance(CID_AttributeAccess, nullptr, UseSameProcess,
                            IID_IAttributeAccess,
                            reinterpret_cast<void **>(&m_pIAttributeAccess));
    if (MUX_FAILED(mr))
    {
        return mr;
    }

    mr = mux_CreateInstance(CID_Permissions, nullptr, UseSameProcess,
                            IID_IPermissions,
                            reinterpret_cast<void **>(&m_pIPermissions));
    if (MUX_FAILED(mr))
    {
        return mr;
    }

    mr = mux_CreateInstance(CID_MailDelivery, nullptr, UseSameProcess,
                            IID_IMailDelivery,
                            reinterpret_cast<void **>(&m_pIMailDelivery));
    if (MUX_FAILED(mr))
    {
        return mr;
    }

    // Log that we are alive.
    //
    if (nullptr != m_pILog)
    {
        bool fStarted;
        MUX_RESULT mr2 = m_pILog->start_log(&fStarted, LOG_ALWAYS,
                                             T("INI"), T("INFO"));
        if (MUX_SUCCEEDED(mr2) && fStarted)
        {
            m_pILog->log_text(T("Mail module loaded."));
            m_pILog->end_log();
        }
    }

    return mr;
}

CMailMod::~CMailMod()
{
    // Release storage interface.
    //
    if (nullptr != m_pIStorage)
    {
        m_pIStorage->Release();
        m_pIStorage = nullptr;
    }

    // Free mail data.
    //
    ClearRuntimeData();

    if (nullptr != m_pILog)
    {
        bool fStarted;
        MUX_RESULT mr = m_pILog->start_log(&fStarted, LOG_ALWAYS,
                                            T("INI"), T("INFO"));
        if (MUX_SUCCEEDED(mr) && fStarted)
        {
            m_pILog->log_text(T("Mail module unloading."));
            m_pILog->end_log();
        }
        m_pILog->Release();
        m_pILog = nullptr;
    }

    if (nullptr != m_pIServerEventsControl)
    {
        m_pIServerEventsControl->Release();
        m_pIServerEventsControl = nullptr;
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

    if (nullptr != m_pIPermissions)
    {
        m_pIPermissions->Release();
        m_pIPermissions = nullptr;
    }

    if (nullptr != m_pIMailDelivery)
    {
        m_pIMailDelivery->Release();
        m_pIMailDelivery = nullptr;
    }

    g_cComponents--;
}

void CMailMod::ClearRuntimeData(void)
{
    m_mail_htab.clear();
    m_mail_list.clear();
    m_malias.clear();
}

MUX_RESULT CMailMod::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_IMailControl *>(this);
    }
    else if (IID_IMailControl == iid)
    {
        *ppv = static_cast<mux_IMailControl *>(this);
    }
    else if (IID_IServerEventsSink == iid)
    {
        *ppv = static_cast<mux_IServerEventsSink *>(this);
    }
    else
    {
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

uint32_t CMailMod::AddRef(void)
{
    return m_cRef.fetch_add(1, std::memory_order_relaxed) + 1;
}

uint32_t CMailMod::Release(void)
{
    uint32_t prev = m_cRef.fetch_sub(1, std::memory_order_acq_rel);
    if (1 == prev)
    {
        delete this;
        return 0;
    }
    return prev - 1;
}

// ---------------------------------------------------------------------------
// Message body management.
// ---------------------------------------------------------------------------

// Largest message index we will honor (#1065 / engine #843 parity).
// Message numbers can come from a corrupt mail database; an absurd value
// would drive a huge resize or a wild index.
//
static constexpr int MAIL_DB_LIMIT = 0x04000000;   // 67,108,864

void CMailMod::mail_db_grow(int newtop)
{
    if (newtop <= static_cast<int>(m_mail_list.size()))
    {
        return;
    }
    if (newtop > MAIL_DB_LIMIT)
    {
        if (nullptr != m_pILog)
        {
            bool fStarted;
            m_pILog->start_log(&fStarted, LOG_BUGS, T("BUG"), T("MAIL"));
            if (fStarted)
            {
                UTF8 buf[128];
                mux_sprintf(buf, sizeof(buf), T("mail_db_grow: refusing absurd mail size %d."), newtop);
                m_pILog->log_text(buf);
                m_pILog->end_log();
            }
        }
        return;
    }
    m_mail_list.resize(newtop);
}

int CMailMod::new_mail_message(const UTF8 *message, int number)
{
    bool bTruncated = false;
    size_t nLen = strlen(reinterpret_cast<const char *>(message));
    if (nLen > MOD_LBUF_SIZE - 1)
    {
        bTruncated = true;
        nLen = MOD_LBUF_SIZE - 1;
    }

    // Allocating a new body, or loading an existing one back from storage?
    // The engine splits these into two functions -- MessageAdd() allocates
    // and takes a reference, new_mail_message() loads and does not -- and
    // this one does both, so the distinction has to be captured here (#1587).
    //
    const bool bAllocating = (NOTHING == number);

    // If number is NOTHING (-1), find a free slot.
    //
    if (NOTHING == number)
    {
        int top = static_cast<int>(m_mail_list.size());
        for (int i = 0; i < top; i++)
        {
            if (m_mail_list[i].m_nRefs <= 0 && m_mail_list[i].m_pMessage.empty())
            {
                number = i;
                break;
            }
        }
        if (NOTHING == number)
        {
            number = top;
        }
    }

    // Reject negative / absurd indices before grow (#1065).
    //
    if (number < 0 || number > MAIL_DB_LIMIT)
    {
        return NOTHING;
    }

    mail_db_grow(number + 1);

    // Grow may have refused an absurd size; do not index past the vector.
    //
    if (number >= static_cast<int>(m_mail_list.size()))
    {
        return NOTHING;
    }

    struct mail_body &pm = m_mail_list[number];
    pm.m_pMessage.assign(reinterpret_cast<const char *>(message), nLen);

    // Take the creation reference (#1587).  The engine does this in
    // MessageAdd and says so above it: "This function returns a reference to
    // the message and the reference count is increased to reflect that."
    // Without it the arithmetic on a send is 0 -> Inc per recipient -> Dec
    // once at the end of mail_to_list, so a single-recipient message returns
    // to zero the instant it is sent and the body is freed.  Two recipients
    // survived undercounted by one, and died when the first of them cleared
    // their copy.
    //
    // Only when allocating.  On load the per-header MessageReferenceInc in
    // the storage callback already counts every reference there is, and
    // taking another here would leak every body for the life of the process.
    //
    if (bAllocating)
    {
        pm.m_nRefs = 0;
        MessageReferenceInc(number);
    }

    // #1192: Persist body + mail_db_top on live creates.  Skipped while
    // m_bLoading so SQLite load does not write back through itself.
    // Also feeds the #1191 softcode gen-sync reload.
    //
    sqlite_wt_mail_body(number,
        reinterpret_cast<const UTF8 *>(pm.m_pMessage.c_str()));

    if (bTruncated && nullptr != m_pILog)
    {
        bool fStarted;
        m_pILog->start_log(&fStarted, LOG_BUGS, T("BUG"), T("MAIL"));
        if (fStarted)
        {
            UTF8 buf[128];
            mux_sprintf(buf, sizeof(buf), T("new_mail_message: Mail message %d truncated."), number);
            m_pILog->log_text(buf);
            m_pILog->end_log();
        }
    }
    return number;
}

const UTF8 *CMailMod::MessageFetch(int number)
{
    if (number < 0 || number >= static_cast<int>(m_mail_list.size()))
    {
        return T("MAIL: This mail message does not exist in the database. Please alert your admin.");
    }
    if (!m_mail_list[number].m_pMessage.empty())
    {
        return reinterpret_cast<const UTF8 *>(m_mail_list[number].m_pMessage.c_str());
    }
    return T("MAIL: This mail message does not exist in the database. Please alert your admin.");
}

size_t CMailMod::MessageFetchSize(int number)
{
    if (number < 0 || number >= static_cast<int>(m_mail_list.size()))
    {
        return 0;
    }
    return m_mail_list[number].m_pMessage.size();
}

void CMailMod::MessageReferenceInc(int number)
{
    if (0 <= number && number < static_cast<int>(m_mail_list.size()))
    {
        m_mail_list[number].m_nRefs++;
    }
}

void CMailMod::MessageReferenceDec(int number)
{
    if (number < 0 || number >= static_cast<int>(m_mail_list.size()))
    {
        return;
    }

    m_mail_list[number].m_nRefs--;
    if (m_mail_list[number].m_nRefs <= 0)
    {
        sqlite_wt_delete_mail_body(number);
        m_mail_list[number].m_pMessage.clear();
        m_mail_list[number].m_nRefs = 0;
    }
}

// ---------------------------------------------------------------------------
// Mail list management — per-player std::list<mail>.
// ---------------------------------------------------------------------------

std::list<mail> *CMailMod::MailList(dbref player)
{
    auto it = m_mail_htab.find(player);
    if (it != m_mail_htab.end() && !it->second.empty())
    {
        return &it->second;
    }
    return nullptr;
}

void CMailMod::MailListRemoveAll(dbref player)
{
    auto it = m_mail_htab.find(player);
    if (it == m_mail_htab.end())
    {
        return;
    }

    for (auto &m : it->second)
    {
        MessageReferenceDec(m.number);
    }
    m_mail_htab.erase(it);
}

// ---------------------------------------------------------------------------
// SQLite write-through helpers.
// ---------------------------------------------------------------------------

void CMailMod::bump_revision(void)
{
    m_revision++;
    if (m_revision < 0)
    {
        m_revision = 1;
    }
}

// Report a refused storage write (#1630).  Silent on success, so it can wrap
// a call directly:  log_storage_failure(m_pIStorage->Foo(k), "Foo(k=%d)", k);
//
// The message carries the operation, the key that identifies the row, and the
// result code, because "a write failed" without the key is not actionable --
// #1587 needed to know *which* body could not be deleted.
//
void CMailMod::log_storage_failure(MUX_RESULT mr, const char *fmt, ...)
{
    if (  MUX_SUCCEEDED(mr)
       || nullptr == m_pILog)
    {
        return;
    }

    char detail[192];
    va_list ap;
    va_start(ap, fmt);
    mux_vsnprintf(reinterpret_cast<UTF8 *>(detail), sizeof(detail),
        reinterpret_cast<const UTF8 *>(fmt), ap);
    va_end(ap);

    bool fStarted;
    m_pILog->start_log(&fStarted, LOG_ALWAYS, T("MAI"), T("DB"));
    if (fStarted)
    {
        UTF8 buf[256];
        mux_sprintf(buf, sizeof(buf), T("Mail module: storage write refused: %s; result %d."),
            detail, mr);
        m_pILog->log_text(buf);
        m_pILog->end_log();
    }
}

void CMailMod::sqlite_wt_insert_mail(struct mail *mp)
{
    if (m_bLoading || nullptr == m_pIStorage) return;

    int64_t rowid = -1;
    MUX_RESULT mr = m_pIStorage->InsertMailHeader(
        mp->to, mp->from, mp->number,
        reinterpret_cast<const UTF8 *>(mp->tolist.c_str()),
        reinterpret_cast<const UTF8 *>(mp->time.c_str()),
        reinterpret_cast<const UTF8 *>(mp->subject.c_str()),
        mp->read, &rowid);

    // The result was already consumed for the rowid, but a failure here
    // means the message is in memory and not in the database -- it survives
    // the session and vanishes on restart.  Worth saying so (#1630).
    //
    log_storage_failure(mr, "InsertMailHeader(to=#%d, from=#%d, number=%d)",
        static_cast<int>(mp->to), static_cast<int>(mp->from), mp->number);

    mp->sqlite_id = MUX_SUCCEEDED(mr) ? rowid : -1;
    bump_revision();
}

void CMailMod::sqlite_wt_update_mail_flags(struct mail *mp)
{
    if (m_bLoading || nullptr == m_pIStorage || mp->sqlite_id < 0) return;

    log_storage_failure(
        m_pIStorage->UpdateMailReadFlags(mp->sqlite_id, mp->read),
        "UpdateMailReadFlags(id=%lld, read=%d)",
        static_cast<long long>(mp->sqlite_id), mp->read);
    bump_revision();
}

void CMailMod::sqlite_wt_delete_mail(struct mail *mp)
{
    if (m_bLoading || nullptr == m_pIStorage || mp->sqlite_id < 0) return;

    log_storage_failure(m_pIStorage->DeleteMailHeader(mp->sqlite_id),
        "DeleteMailHeader(id=%lld)",
        static_cast<long long>(mp->sqlite_id));
    bump_revision();
}

void CMailMod::sqlite_wt_delete_all_mail(int to_player)
{
    if (m_bLoading || nullptr == m_pIStorage) return;

    log_storage_failure(m_pIStorage->DeleteAllMailHeaders(to_player),
        "DeleteAllMailHeaders(to=#%d)", to_player);
    bump_revision();
}

void CMailMod::sqlite_wt_mail_body(int number, const UTF8 *message)
{
    if (m_bLoading || nullptr == m_pIStorage) return;

    log_storage_failure(m_pIStorage->SyncMailBody(number, message),
        "SyncMailBody(number=%d)", number);

    // Keep the loader gate current (#783 / engine MessageAdd parity):
    // sqlite_load_mail / module Initialize only size bodies from
    // mail_db_top meta.  Without this, in-game module mail is lost on reboot.
    //
    log_storage_failure(m_pIStorage->PutMeta(T("mail_db_top"),
            static_cast<int>(m_mail_list.size())),
        "PutMeta(mail_db_top=%d)", static_cast<int>(m_mail_list.size()));
    bump_revision();
}

void CMailMod::sqlite_wt_delete_mail_body(int number)
{
    if (m_bLoading || nullptr == m_pIStorage) return;

    // Check the result (#1587).  mail_headers.body_number REFERENCES
    // mail_bodies(number), so deleting a body a header still points at is
    // refused with a foreign-key violation -- and this discarded that,
    // which is the only reason the refcount bug above did not destroy mail
    // outright.  A delete that cannot happen is a fact worth knowing rather
    // than an accident worth relying on.  The engine's equivalent logs it.
    //
    if (MUX_FAILED(m_pIStorage->DeleteMailBody(number)) && nullptr != m_pILog)
    {
        bool fStarted;
        m_pILog->start_log(&fStarted, LOG_ALWAYS, T("MAI"), T("BODY"));
        if (fStarted)
        {
            UTF8 buf[160];
            mux_sprintf(buf, sizeof(buf), T("Mail module: body %d could not be deleted; a header still "
                "references it."), number);
            m_pILog->log_text(buf);
            m_pILog->end_log();
        }
    }
    bump_revision();
}

void CMailMod::sqlite_wt_sync_all_aliases(void)
{
    if (m_bLoading || nullptr == m_pIStorage) return;

    // Clear existing aliases.
    //
    log_storage_failure(m_pIStorage->ClearMailAliases(),
        "ClearMailAliases()");

    for (size_t i = 0; i < m_malias.size(); i++)
    {
        malias_t *m = m_malias[i].get();
        if (nullptr == m) continue;

        // Build space-separated member list.  #1197: clamp snprintf return
        // (it can exceed remain) and refuse to write a separator when the
        // buffer is nearly full — same class as #1066 make_numlist.
        //
        char members_buf[MOD_LBUF_SIZE];
        char *bp = members_buf;
        size_t remain = sizeof(members_buf);
        for (size_t j = 0; j < m->list.size() && remain > 1; j++)
        {
            if (j > 0)
            {
                if (remain <= 1)
                {
                    break;
                }
                *bp++ = ' ';
                remain--;
            }
            size_t n = mail_sprintf(reinterpret_cast<UTF8 *>(bp), remain,
                T("%d"), m->list[j]);
            if (n + 1 >= remain)
            {
                // Truncated — leave the partial write and stop.
                //
                bp += remain - 1;
                remain = 1;
                break;
            }
            bp += n;
            remain -= n;
        }
        *bp = '\0';

        log_storage_failure(m_pIStorage->SyncMailAlias(
                m->owner,
                reinterpret_cast<const UTF8 *>(m->name.c_str()),
                reinterpret_cast<const UTF8 *>(m->desc.c_str()),
                static_cast<int>(m->desc_width),
                reinterpret_cast<const UTF8 *>(members_buf)),
            "SyncMailAlias(owner=#%d, name=%s)",
            static_cast<int>(m->owner), m->name.c_str());
    }
    bump_revision();
}

// ---------------------------------------------------------------------------
// Data loading from SQLite.
// ---------------------------------------------------------------------------

bool CMailMod::LoadMailBodies(void)
{
    if (nullptr == m_pIStorage) return false;

    MUX_RESULT mr = m_pIStorage->LoadAllMailBodies(
        [](void *ctx, int number, const UTF8 *message)
        {
            CMailMod *pThis = static_cast<CMailMod *>(ctx);
            if (nullptr != message)
            {
                pThis->new_mail_message(message, number);
            }
        }, this);

    return MUX_SUCCEEDED(mr);
}

bool CMailMod::LoadMailHeaders(void)
{
    if (nullptr == m_pIStorage) return false;

    MUX_RESULT mr = m_pIStorage->LoadAllMailHeaders(
        [](void *ctx, int64_t rowid, int to_player, int from_player,
            int body_number, const UTF8 *tolist, const UTF8 *time_str,
            const UTF8 *subject, int read_flags)
        {
            CMailMod *pThis = static_cast<CMailMod *>(ctx);

            mail m;
            m.sqlite_id = rowid;
            m.to        = to_player;
            m.from      = from_player;
            m.number    = body_number;

            pThis->MessageReferenceInc(m.number);

            m.tolist  = tolist   ? reinterpret_cast<const char *>(tolist)   : "";
            m.time    = time_str ? reinterpret_cast<const char *>(time_str) : "";
            m.subject = subject  ? reinterpret_cast<const char *>(subject)  : "";
            m.read    = read_flags;

            pThis->m_mail_htab[to_player].push_back(std::move(m));
        }, this);

    return MUX_SUCCEEDED(mr);
}

bool CMailMod::LoadMailAliases(void)
{
    if (nullptr == m_pIStorage) return false;

    MUX_RESULT mr = m_pIStorage->LoadAllMailAliases(
        [](void *ctx, int owner, const UTF8 *name, const UTF8 *desc,
            int desc_width, const UTF8 *members)
        {
            CMailMod *pThis = static_cast<CMailMod *>(ctx);

            auto m = std::make_unique<malias_t>();

            m->owner = owner;

            const char *pName = reinterpret_cast<const char *>(name);
            const char *pDesc = reinterpret_cast<const char *>(desc);

            m->name = pName ? pName : "";
            m->desc = pDesc ? pDesc : "";
            m->desc_width = desc_width;

            // Parse space-separated member list.
            //
            m->list.clear();
            const char *pMembers = reinterpret_cast<const char *>(members);
            if (nullptr != pMembers && pMembers[0] != '\0')
            {
                char buf[MOD_LBUF_SIZE];
                strncpy(buf, pMembers, MOD_LBUF_SIZE - 1);
                buf[MOD_LBUF_SIZE - 1] = '\0';
                char *p = buf;
                while (*p)
                {
                    while (*p == ' ') p++;
                    if (*p == '\0') break;
                    m->list.push_back(atoi(p));
                    while (*p && *p != ' ') p++;
                }
            }

            pThis->m_malias.push_back(std::move(m));
        }, this);

    return MUX_SUCCEEDED(mr);
}

// ---------------------------------------------------------------------------
// Composition attributes — AF_INTERNAL / AF_DARK; must use raw attr APIs.
// ---------------------------------------------------------------------------

bool CMailMod::set_comp_attr(dbref player, int attrnum, const UTF8 *value)
{
    if (nullptr == m_pIObjectInfo)
    {
        return false;
    }
    return MUX_SUCCEEDED(m_pIObjectInfo->AtrAddRaw(player, attrnum,
        value ? value : T("")));
}

bool CMailMod::get_comp_attr(dbref player, int attrnum, UTF8 *buf, size_t nBuf)
{
    if (nullptr == buf || 0 == nBuf)
    {
        return false;
    }
    buf[0] = '\0';
    if (nullptr == m_pIObjectInfo)
    {
        return false;
    }
    dbref aowner = NOTHING;
    int aflags = 0;
    MUX_RESULT mr = m_pIObjectInfo->AtrGet(player, attrnum, buf, nBuf,
        &aowner, &aflags);
    return MUX_SUCCEEDED(mr);
}

bool CMailMod::clr_comp_attr(dbref player, int attrnum)
{
    if (nullptr == m_pIObjectInfo)
    {
        return false;
    }
    return MUX_SUCCEEDED(m_pIObjectInfo->AtrClr(player, attrnum));
}

// ---------------------------------------------------------------------------
// mux_IMailControl implementation.
// ---------------------------------------------------------------------------

MUX_RESULT CMailMod::Initialize(mux_IMailStorage *pStorage,
    int mail_expiration, int mail_per_player)
{
    if (nullptr == pStorage)
    {
        return MUX_E_INVALIDARG;
    }

    // Store and AddRef the storage interface.
    //
    m_pIStorage = pStorage;
    m_pIStorage->AddRef();

    m_mail_expiration = mail_expiration;
    m_mail_per_player = mail_per_player;

    // Check for mail_db_top metadata to know if mail data exists.
    //
    int mail_top = 0;
    m_pIStorage->GetMeta(T("mail_db_top"), &mail_top);

    m_bLoading = true;

    ClearRuntimeData();
    mail_db_grow(mail_top + 1);

    if (!LoadMailBodies())
    {
        if (nullptr != m_pILog)
        {
            bool fStarted;
            m_pILog->start_log(&fStarted, LOG_ALWAYS, T("MAIL"), T("LOAD"));
            if (fStarted)
            {
                m_pILog->log_text(T("Mail module: LoadMailBodies failed."));
                m_pILog->end_log();
            }
        }
    }

    if (!LoadMailHeaders())
    {
        if (nullptr != m_pILog)
        {
            bool fStarted;
            m_pILog->start_log(&fStarted, LOG_ALWAYS, T("MAIL"), T("LOAD"));
            if (fStarted)
            {
                m_pILog->log_text(T("Mail module: LoadMailHeaders failed."));
                m_pILog->end_log();
            }
        }
    }

    if (!LoadMailAliases())
    {
        if (nullptr != m_pILog)
        {
            bool fStarted;
            m_pILog->start_log(&fStarted, LOG_ALWAYS, T("MAIL"), T("LOAD"));
            if (fStarted)
            {
                m_pILog->log_text(T("Mail module: LoadMailAliases failed."));
                m_pILog->end_log();
            }
        }
    }

    m_bLoading = false;

    if (nullptr != m_pILog)
    {
        bool fStarted;
        m_pILog->start_log(&fStarted, LOG_ALWAYS, T("MAIL"), T("LOAD"));
        if (fStarted)
        {
            m_pILog->log_text(T("Mail module: data loaded from SQLite."));
            m_pILog->end_log();
        }
    }

    return MUX_S_OK;
}

// ---------------------------------------------------------------------------
// Selector parsing and matching — ported from mail.cpp.
// ---------------------------------------------------------------------------

static const UTF8 *mailmsg[] =
{
    T("MAIL: Invalid message range"),
    T("MAIL: Invalid message number"),
    T("MAIL: Invalid age"),
    T("MAIL: Invalid dbref #"),
    T("MAIL: Invalid player"),
    T("MAIL: Invalid message specification"),
    T("MAIL: Invalid player or trying to send @mail to a @malias without a subject"),
};

#define MAIL_INVALID_RANGE  0
#define MAIL_INVALID_NUMBER 1
#define MAIL_INVALID_AGE    2
#define MAIL_INVALID_DBREF  3
#define MAIL_INVALID_PLAYER 4
#define MAIL_INVALID_SPEC   5
#define MAIL_INVALID_PLAYER_OR_USING_MALIAS 6

bool CMailMod::parse_msglist(const UTF8 *msglist, struct mail_selector *ms,
    dbref player)
{
    ms->low = 0;
    ms->high = 0;
    ms->flags = 0x0FFF | M_MSUNREAD;
    ms->player = 0;
    ms->days = -1;
    ms->day_comp = 0;

    if (nullptr == msglist || '\0' == *msglist)
    {
        return true;
    }

    const UTF8 *p = msglist;
    while (isspace(*p))
    {
        p++;
    }

    if ('\0' == *p)
    {
        return true;
    }

    if (isdigit(*p))
    {
        const char *q = strchr(reinterpret_cast<const char *>(p), '-');
        if (q)
        {
            q++;
            ms->low = atol(reinterpret_cast<const char *>(p));
            if (ms->low <= 0)
            {
                m_pINotify->RawNotify(player, mailmsg[MAIL_INVALID_RANGE]);
                return false;
            }
            if ('\0' == *q)
            {
                ms->high = 0;
            }
            else
            {
                ms->high = atol(q);
                if (ms->low > ms->high)
                {
                    m_pINotify->RawNotify(player, mailmsg[MAIL_INVALID_RANGE]);
                    return false;
                }
            }
        }
        else
        {
            ms->low = ms->high = atol(reinterpret_cast<const char *>(p));
            if (ms->low <= 0)
            {
                m_pINotify->RawNotify(player, mailmsg[MAIL_INVALID_NUMBER]);
                return false;
            }
        }
    }
    else
    {
        switch (*p)
        {
        case '-':
            p++;
            if ('\0' == *p)
            {
                m_pINotify->RawNotify(player, mailmsg[MAIL_INVALID_RANGE]);
                return false;
            }
            ms->high = atol(reinterpret_cast<const char *>(p));
            if (ms->high <= 0)
            {
                m_pINotify->RawNotify(player, mailmsg[MAIL_INVALID_RANGE]);
                return false;
            }
            break;

        case '~':
            p++;
            if ('\0' == *p)
            {
                m_pINotify->RawNotify(player, mailmsg[MAIL_INVALID_AGE]);
                return false;
            }
            ms->day_comp = 0;
            ms->days = atol(reinterpret_cast<const char *>(p));
            if (ms->days < 0)
            {
                m_pINotify->RawNotify(player, mailmsg[MAIL_INVALID_AGE]);
                return false;
            }
            break;

        case '<':
            p++;
            if ('\0' == *p)
            {
                m_pINotify->RawNotify(player, mailmsg[MAIL_INVALID_AGE]);
                return false;
            }
            ms->day_comp = -1;
            ms->days = atol(reinterpret_cast<const char *>(p));
            if (ms->days < 0)
            {
                m_pINotify->RawNotify(player, mailmsg[MAIL_INVALID_AGE]);
                return false;
            }
            break;

        case '>':
            p++;
            if ('\0' == *p)
            {
                m_pINotify->RawNotify(player, mailmsg[MAIL_INVALID_AGE]);
                return false;
            }
            ms->day_comp = 1;
            ms->days = atol(reinterpret_cast<const char *>(p));
            if (ms->days < 0)
            {
                m_pINotify->RawNotify(player, mailmsg[MAIL_INVALID_AGE]);
                return false;
            }
            break;

        case '#':
            p++;
            if ('\0' == *p)
            {
                m_pINotify->RawNotify(player, mailmsg[MAIL_INVALID_DBREF]);
                return false;
            }
            ms->player = atol(reinterpret_cast<const char *>(p));
            break;

        case '*':
            // From player name — use IObjectInfo::MatchThing.
            //
            p++;
            if ('\0' == *p)
            {
                m_pINotify->RawNotify(player, mailmsg[MAIL_INVALID_PLAYER]);
                return false;
            }
            {
                UTF8 namebuf[MOD_LBUF_SIZE];
                namebuf[0] = '*';
                strncpy(reinterpret_cast<char *>(namebuf + 1),
                        reinterpret_cast<const char *>(p),
                        MOD_LBUF_SIZE - 2);
                namebuf[MOD_LBUF_SIZE - 1] = '\0';
                dbref result = NOTHING;
                if (nullptr != m_pIObjectInfo)
                {
                    m_pIObjectInfo->MatchThing(player, namebuf, &result);
                }
                if (NOTHING == result)
                {
                    m_pINotify->RawNotify(player,
                        mailmsg[MAIL_INVALID_PLAYER_OR_USING_MALIAS]);
                    return false;
                }
                ms->player = result;
            }
            break;

        case 'a':
        case 'A':
            p++;
            if ('\0' == *p || ((*p != 'l' && *p != 'L')))
            {
                m_pINotify->RawNotify(player, mailmsg[MAIL_INVALID_SPEC]);
                return false;
            }
            p++;
            if ('\0' == *p || ((*p != 'l' && *p != 'L')))
            {
                m_pINotify->RawNotify(player, mailmsg[MAIL_INVALID_SPEC]);
                return false;
            }
            p++;
            if ('\0' == *p)
            {
                ms->flags = M_ALL;
            }
            else
            {
                m_pINotify->RawNotify(player, mailmsg[MAIL_INVALID_SPEC]);
                return false;
            }
            break;

        case 'u':
        case 'U':
            p++;
            if ('\0' == *p)
            {
                m_pINotify->RawNotify(player,
                    T("MAIL: U is ambiguous (urgent or unread?)"));
                return false;
            }
            if ('r' == *p || 'R' == *p)
            {
                ms->flags = M_URGENT;
            }
            else if ('n' == *p || 'N' == *p)
            {
                ms->flags = M_MSUNREAD;
            }
            else
            {
                m_pINotify->RawNotify(player, mailmsg[MAIL_INVALID_SPEC]);
                return false;
            }
            break;

        case 'r':
        case 'R':
            ms->flags = M_ISREAD;
            break;

        case 'c':
        case 'C':
            ms->flags = M_CLEARED;
            break;

        case 't':
        case 'T':
            ms->flags = M_TAG;
            break;

        case 'm':
        case 'M':
            ms->flags = M_MASS;
            break;

        case 'f':
        case 'F':
            ms->flags = M_FORWARD;
            break;

        default:
            m_pINotify->RawNotify(player, mailmsg[MAIL_INVALID_SPEC]);
            return false;
        }
    }
    return true;
}

static int sign(int x)
{
    if (x == 0) return 0;
    return (x < 0) ? -1 : 1;
}

bool CMailMod::mail_match(struct mail *mp, struct mail_selector &ms, int num)
{
    if (ms.low && num < ms.low)
    {
        return false;
    }
    if (ms.high && ms.high < num)
    {
        return false;
    }
    if (ms.player && mp->from != ms.player)
    {
        return false;
    }

    mail_flag mpflag = Read(mp)
        ? (mp->read | M_ALL)
        : (mp->read | M_ALL | M_MSUNREAD);

    if ((ms.flags & mpflag) == 0)
    {
        return false;
    }

    if (ms.days == -1)
    {
        return true;
    }

    CLinearTimeAbsolute ltaNow;
    ltaNow.GetLocal();

    CLinearTimeAbsolute ltaMail;
    if (ltaMail.SetString(reinterpret_cast<const UTF8 *>(mp->time.c_str())))
    {
        CLinearTimeDelta ltd(ltaMail, ltaNow);
        int iDiffDays = ltd.ReturnDays();
        if (sign(iDiffDays - ms.days) == ms.day_comp)
        {
            return true;
        }
    }
    return false;
}

int CMailMod::player_folder(dbref player)
{
    if (nullptr == m_pIAttributeAccess)
    {
        return 0;
    }

    UTF8 buf[64];
    size_t nLen = 0;
    MUX_RESULT mr = m_pIAttributeAccess->GetAttribute(player, player,
        T("Mailcurf"), buf, sizeof(buf) - 1, &nLen);
    if (MUX_FAILED(mr) || 0 == nLen)
    {
        return 0;
    }
    buf[nLen] = '\0';
    return atoi(reinterpret_cast<const char *>(buf));
}

void CMailMod::set_player_folder(dbref player, int fnum)
{
    if (nullptr == m_pIAttributeAccess)
    {
        return;
    }
    UTF8 buf[16];
    mux_sprintf(buf, sizeof(buf), T("%d"), fnum);
    set_attr_checked(player, T("Mailcurf"), buf, T("current folder"));
}

// Write an attribute and say so when the write is refused (#1620).
//
// SetAttribute is permission-checked.  The engine writes some of the same
// attributes as GOD with AF_CONST, which bCanSetAttr denies in every branch
// -- so on a game that has run both implementations these writes can fail,
// and every call site here used to discard the result.  A refused folder
// write means the player's folder list silently reverts, which looks like
// the command never ran.
//
// This does not make the write land; it makes the loss visible.
//
void CMailMod::set_attr_checked(dbref player, const UTF8 *pAttr,
    const UTF8 *pValue, const UTF8 *pWhere)
{
    MUX_RESULT mr = m_pIAttributeAccess->SetAttribute(1, player, pAttr,
        pValue);
    if (MUX_FAILED(mr) && nullptr != m_pILog)
    {
        bool fStarted;
        m_pILog->start_log(&fStarted, LOG_ALWAYS, T("MAI"), T("ATTR"));
        if (fStarted)
        {
            UTF8 logbuf[256];
            mux_sprintf(logbuf, sizeof(logbuf), T("Mail module: %s write refused (%s on #%d, result %d); "
                "change not saved."),
                reinterpret_cast<const char *>(pWhere),
                reinterpret_cast<const char *>(pAttr),
                static_cast<int>(player), mr);
            m_pILog->log_text(logbuf);
            m_pILog->end_log();
        }
    }
}

const UTF8 *CMailMod::get_folder_name(dbref player, int fld)
{
    if (nullptr == m_pIAttributeAccess)
    {
        return T("unnamed");
    }

    UTF8 aFolders[MOD_LBUF_SIZE];
    size_t nFolders = 0;
    MUX_RESULT mr = m_pIAttributeAccess->GetAttribute(player, player,
        T("Mailfolders"), aFolders, sizeof(aFolders) - 1, &nFolders);
    if (MUX_FAILED(mr) || 0 == nFolders)
    {
        return T("unnamed");
    }
    aFolders[nFolders] = '\0';

    // Build pattern "N:" where N is the folder number.
    //
    char pattern[16];
    mux_sprintf(reinterpret_cast<UTF8 *>(pattern), sizeof(pattern),
        T("%d:"), fld);
    size_t nPattern = strlen(pattern);

    const char *found = strstr(reinterpret_cast<const char *>(aFolders), pattern);
    if (nullptr == found)
    {
        return T("unnamed");
    }

    thread_local UTF8 result[FOLDER_NAME_LEN + 1];
    const char *p = found + nPattern;
    char *q = reinterpret_cast<char *>(result);
    while (*p && *p != ':' && q < reinterpret_cast<char *>(result) + FOLDER_NAME_LEN)
    {
        *q++ = *p++;
    }
    *q = '\0';
    return result;
}

int CMailMod::get_folder_number(dbref player, const UTF8 *name)
{
    if (nullptr == m_pIAttributeAccess || nullptr == name || '\0' == *name)
    {
        return -1;
    }

    UTF8 aFolders[MOD_LBUF_SIZE];
    size_t nFolders = 0;
    MUX_RESULT mr = m_pIAttributeAccess->GetAttribute(player, player,
        T("Mailfolders"), aFolders, sizeof(aFolders) - 1, &nFolders);
    if (MUX_FAILED(mr) || 0 == nFolders)
    {
        return -1;
    }
    aFolders[nFolders] = '\0';

    // Build upper-case pattern ":NAME:".
    //
    char pattern[FOLDER_NAME_LEN + 4];
    pattern[0] = ':';
    size_t i = 1;
    const char *s = reinterpret_cast<const char *>(name);
    while (*s && i < sizeof(pattern) - 2)
    {
        pattern[i++] = toupper(*s);
        s++;
    }
    pattern[i++] = ':';
    pattern[i] = '\0';

    // Need upper-case version of aFolders for case-insensitive match.
    //
    char upper[MOD_LBUF_SIZE];
    for (size_t k = 0; k <= nFolders; k++)
    {
        upper[k] = toupper(aFolders[k]);
    }

    const char *found = strstr(upper, pattern);
    if (nullptr == found)
    {
        return -1;
    }

    // The folder number follows the closing ':'.
    //
    const char *p = found + strlen(pattern);
    while (*p && isspace(*p))
    {
        p++;
    }
    if ('#' == *p)
    {
        p++;
    }
    return atoi(p);
}

int CMailMod::parse_folder(dbref player, const UTF8 *folder_string)
{
    if (nullptr == folder_string || '\0' == *folder_string)
    {
        return -1;
    }
    if (isdigit(*folder_string))
    {
        int fnum = atoi(reinterpret_cast<const char *>(folder_string));
        if (fnum < 0 || fnum > MAX_FOLDERS)
        {
            return -1;
        }
        return fnum;
    }
    return get_folder_number(player, folder_string);
}

void CMailMod::add_folder_name(dbref player, int fld, const UTF8 *name)
{
    if (nullptr == m_pIAttributeAccess)
    {
        return;
    }

    UTF8 aFolders[MOD_LBUF_SIZE];
    size_t nFolders = 0;
    MUX_RESULT mr = m_pIAttributeAccess->GetAttribute(player, player,
        T("Mailfolders"), aFolders, sizeof(aFolders) - 1, &nFolders);
    if (MUX_FAILED(mr))
    {
        nFolders = 0;
    }
    aFolders[nFolders] = '\0';

    // Build the new record "N:NAME:N" upper-cased.
    //
    char record[FOLDER_NAME_LEN + 16];
    mux_sprintf(reinterpret_cast<UTF8 *>(record), sizeof(record),
        T("%d:"), fld);
    size_t rlen = strlen(record);
    const char *s = reinterpret_cast<const char *>(name);
    while (*s && rlen < sizeof(record) - 8)
    {
        record[rlen++] = toupper(*s);
        s++;
    }
    mux_sprintf(reinterpret_cast<UTF8 *>(record + rlen),
        sizeof(record) - rlen, T(":%d"), fld);

    if (0 == nFolders)
    {
        // No existing folders — just set the new record.
        //
        set_attr_checked(player, T("Mailfolders"),
            reinterpret_cast<const UTF8 *>(record), T("folder list"));
        return;
    }

    // Build pattern "N:" to find existing entry.
    //
    char pattern[16];
    mux_sprintf(reinterpret_cast<UTF8 *>(pattern), sizeof(pattern),
        T("%d:"), fld);

    char *found = strstr(reinterpret_cast<char *>(aFolders), pattern);
    if (nullptr != found)
    {
        // Replace existing entry: skip to end of old record.
        //
        UTF8 aNew[MOD_LBUF_SIZE];
        size_t before = found - reinterpret_cast<char *>(aFolders);
        memcpy(aNew, aFolders, before);
        size_t nNew = before;

        // Skip old record (format "N:name:N").
        //
        char *end = found + strlen(pattern);
        while (*end && *end != ' ')
        {
            end++;
        }
        if (*end == ' ')
        {
            end++;
        }

        // Append new record.
        //
        size_t rsize = strlen(record);
        memcpy(aNew + nNew, record, rsize);
        nNew += rsize;

        // Append remainder.
        //
        size_t remain = nFolders - (end - reinterpret_cast<char *>(aFolders));
        if (remain > 0)
        {
            aNew[nNew++] = ' ';
            memcpy(aNew + nNew, end, remain);
            nNew += remain;
        }
        aNew[nNew] = '\0';

        set_attr_checked(player, T("Mailfolders"), aNew, T("folder list"));
    }
    else
    {
        // Append new record.
        //
        size_t nOld = strlen(reinterpret_cast<const char *>(aFolders));
        size_t nRec = strlen(record);
        if (nOld + 1 + nRec < MOD_LBUF_SIZE)
        {
            UTF8 aNew[MOD_LBUF_SIZE];
            memcpy(aNew, aFolders, nOld);
            aNew[nOld] = ' ';
            memcpy(aNew + nOld + 1, record, nRec + 1);
            set_attr_checked(player, T("Mailfolders"), aNew,
                T("folder list"));
        }
    }
}

bool CMailMod::mail_to_player(dbref player, struct mail *mp)
{
    if (mp->to != player)
    {
        return false;
    }
    // Check that the mail postdates the player's creation (recycled dbref guard).
    //
    if (nullptr == m_pIAttributeAccess)
    {
        return true;
    }
    UTF8 buf[128];
    size_t nLen = 0;
    MUX_RESULT mr = m_pIAttributeAccess->GetAttribute(player, player,
        T("Created"), buf, sizeof(buf) - 1, &nLen);
    if (MUX_FAILED(mr) || 0 == nLen)
    {
        return false;
    }
    buf[nLen] = '\0';
    CLinearTimeAbsolute ltaCreated, ltaMail;
    if (  ltaCreated.SetString(buf)
       && ltaMail.SetString(reinterpret_cast<const UTF8 *>(mp->time.c_str())))
    {
        return ltaCreated <= ltaMail;
    }
    return false;
}

bool CMailMod::mail_from_player(dbref player, struct mail *mp)
{
    if (mp->from != player)
    {
        return false;
    }
    if (nullptr == m_pIAttributeAccess)
    {
        return true;
    }
    UTF8 buf[128];
    size_t nLen = 0;
    MUX_RESULT mr = m_pIAttributeAccess->GetAttribute(player, player,
        T("Created"), buf, sizeof(buf) - 1, &nLen);
    if (MUX_FAILED(mr) || 0 == nLen)
    {
        return false;
    }
    buf[nLen] = '\0';
    CLinearTimeAbsolute ltaCreated, ltaMail;
    if (  ltaCreated.SetString(buf)
       && ltaMail.SetString(reinterpret_cast<const UTF8 *>(mp->time.c_str())))
    {
        return ltaCreated <= ltaMail;
    }
    return false;
}

void CMailMod::count_mail_internal(dbref player, int folder,
    int *rcount, int *ucount, int *ccount)
{
    int rc = 0, uc = 0, cc = 0;

    std::list<mail> *pList = MailList(player);
    if (pList)
    {
        for (auto &m : *pList)
        {
            struct mail *mp = &m;
            if (  Folder(mp) == folder
               && mail_to_player(player, mp))
            {
                if (Read(mp))
                {
                    rc++;
                }
                else
                {
                    uc++;
                }
                if (Cleared(mp))
                {
                    cc++;
                }
            }
        }
    }
    *rcount = rc;
    *ucount = uc;
    *ccount = cc;
}

void CMailMod::urgent_mail_internal(dbref player, int folder, int *ucount)
{
    int uc = 0;
    std::list<mail> *pList = MailList(player);
    if (pList)
    {
        for (auto &m : *pList)
        {
            struct mail *mp = &m;
            if (  Folder(mp) == folder
               && mail_to_player(player, mp))
            {
                if (Unread(mp) && Urgent(mp))
                {
                    uc++;
                }
            }
        }
    }
    *ucount = uc;
}

// ---------------------------------------------------------------------------
// Folder commands.
// ---------------------------------------------------------------------------

void CMailMod::do_mail_change_folder(dbref player, const UTF8 *fld,
    const UTF8 *newname)
{
    if (nullptr == fld || '\0' == *fld)
    {
        DoListMailBrief(player);
        return;
    }

    int pfld = parse_folder(player, fld);
    if (pfld < 0)
    {
        m_pINotify->RawNotify(player, T("MAIL: What folder is that?"));
        return;
    }

    if (nullptr != newname && '\0' != *newname)
    {
        // Rename folder.
        //
        if (strlen(reinterpret_cast<const char *>(newname)) > FOLDER_NAME_LEN)
        {
            m_pINotify->RawNotify(player, T("MAIL: Folder name too long"));
            return;
        }
        const UTF8 *p;
        for (p = newname; isalnum(*p); p++)
            ;
        if (*p != '\0')
        {
            m_pINotify->RawNotify(player, T("MAIL: Illegal folder name"));
            return;
        }

        add_folder_name(player, pfld, newname);

        UTF8 msg[128];
        mux_sprintf(msg, sizeof(msg), T("MAIL: Folder %d now named ‘%s’"),
                 pfld, reinterpret_cast<const char *>(newname));
        m_pINotify->RawNotify(player, msg);
    }
    else
    {
        // Switch to folder.
        //
        set_player_folder(player, pfld);

        UTF8 msg[128];
        mux_sprintf(msg, sizeof(msg), T("MAIL: Current folder set to %d [%s]."),
                 pfld,
                 reinterpret_cast<const char *>(get_folder_name(player, pfld)));
        m_pINotify->RawNotify(player, msg);
    }
}

void CMailMod::DoListMailBrief(dbref player)
{
    for (int folder = 0; folder < MAX_FOLDERS; folder++)
    {
        int rc, uc, cc, gc;
        count_mail_internal(player, folder, &rc, &uc, &cc);
        urgent_mail_internal(player, folder, &gc);

        if (rc + uc > 0)
        {
            UTF8 msg[256];
            mux_sprintf(msg, sizeof(msg), T("MAIL: %d messages in folder %d [%s] (%d unread, %d cleared)."),
                     rc + uc, folder,
                     reinterpret_cast<const char *>(get_folder_name(player, folder)),
                     uc, cc);
            m_pINotify->RawNotify(player, msg);
        }
        else
        {
            const UTF8 *fname = get_folder_name(player, folder);
            if (0 != strcmp(reinterpret_cast<const char *>(fname), "unnamed"))
            {
                UTF8 msg[256];
                mux_sprintf(msg, sizeof(msg), T("MAIL: 0 messages in folder %d [%s]."),
                         folder, reinterpret_cast<const char *>(fname));
                m_pINotify->RawNotify(player, msg);
            }
        }

        if (gc > 0)
        {
            UTF8 msg[256];
            mux_sprintf(msg, sizeof(msg), T("URGENT MAIL: You have %d urgent messages in folder %d [%s]."),
                     gc, folder,
                     reinterpret_cast<const char *>(get_folder_name(player, folder)));
            m_pINotify->RawNotify(player, msg);
        }
    }

    int current_folder = player_folder(player);

    UTF8 msg[256];
    mux_sprintf(msg, sizeof(msg), T("MAIL: Current folder is %d [%s]."),
             current_folder,
             reinterpret_cast<const char *>(get_folder_name(player, current_folder)));
    m_pINotify->RawNotify(player, msg);
}

void CMailMod::ListMailInFolderNumber(dbref player, int folder_num,
    const UTF8 *msglist)
{
    int original_folder = player_folder(player);
    set_player_folder(player, folder_num);

    struct mail_selector ms;
    if (!parse_msglist(msglist, &ms, player))
    {
        set_player_folder(player, original_folder);
        return;
    }

    UTF8 hdr[MOD_LBUF_SIZE];
    mux_sprintf(hdr, sizeof(hdr), T("––––––"
             "––––––"
             "––––––"
             "––––––"
             "–––"
             "   MAIL: Folder %d   "
             "––––––"
             "––––––"
             "––––––"
             "––––––"
             "––––"),
             folder_num);
    m_pINotify->RawNotify(player, hdr);

    int i = 0;
    std::list<mail> *pList = MailList(player);
    if (pList)
    {
        for (auto &m : *pList)
        {
            struct mail *mp = &m;
            if (Folder(mp) == folder_num)
            {
                i++;
                if (mail_match(mp, ms, i))
                {
                    UTF8 *time = mail_list_time(
                        reinterpret_cast<const UTF8 *>(mp->time.c_str()));
                    size_t nSize = MessageFetchSize(mp->number);

                    UTF8 szFromName[64];
                    get_player_name(mp->from, szFromName, sizeof(szFromName));

                    UTF8 line[MOD_LBUF_SIZE];
                    format_mail_list_line(line, sizeof(line),
                        status_chars(mp), i, nSize, szFromName,
                        reinterpret_cast<const UTF8 *>(mp->subject.c_str()));
                    m_pINotify->RawNotify(player, line);
                }
            }
        }
    }
    m_pINotify->RawNotify(player, MOD_DASH_LINE);

    set_player_folder(player, original_folder);
}

void CMailMod::ListMailInFolder(dbref player, const UTF8 *folder_name,
    const UTF8 *msglist)
{
    int folder = 0;

    if (nullptr == folder_name || '\0' == *folder_name)
    {
        folder = player_folder(player);
    }
    else
    {
        folder = parse_folder(player, folder_name);
    }

    if (-1 == folder)
    {
        m_pINotify->RawNotify(player, T("MAIL: No such folder."));
        return;
    }
    ListMailInFolderNumber(player, folder, msglist);
}

// ---------------------------------------------------------------------------
// @mail/review — review sent mail.
// ---------------------------------------------------------------------------

void CMailMod::do_mail_review_all(dbref player, const UTF8 *msglist)
{
    int i = 0, j = 0;

    if (nullptr == msglist || '\0' == *msglist)
    {
        // Summary mode: list all sent messages grouped by recipient.
        //
        for (auto &kv : m_mail_htab)
        {
            dbref target = kv.first;
            bool bHeader = false;

            for (auto &m : kv.second)
            {
                struct mail *mp = &m;
                if (mail_from_player(player, mp))
                {
                    i++;

                    if (!bHeader)
                    {
                        UTF8 szName[64];
                        get_player_name(target, szName, sizeof(szName));

                        UTF8 hdr[MOD_LBUF_SIZE];
                        format_mail_to_banner(hdr, sizeof(hdr), szName);
                        m_pINotify->RawNotify(player, hdr);
                        bHeader = true;
                    }

                    UTF8 szFromName[64];
                    get_player_name(mp->from, szFromName, sizeof(szFromName));

                    size_t nSize = MessageFetchSize(mp->number);
                    UTF8 line[MOD_LBUF_SIZE];
                    format_mail_list_line(line, sizeof(line),
                        status_chars(mp), i, nSize, szFromName,
                        reinterpret_cast<const UTF8 *>(mp->subject.c_str()));
                    m_pINotify->RawNotify(player, line);
                }
            }
        }

        if (0 == i)
        {
            m_pINotify->RawNotify(player,
                T("MAIL: You have no matching messages."));
        }
        else
        {
            m_pINotify->RawNotify(player, MOD_DASH_LINE);
        }
    }
    else
    {
        // Detail mode: show full messages matching msglist.
        //
        struct mail_selector ms;
        if (!parse_msglist(msglist, &ms, player))
        {
            return;
        }

        for (auto &kv : m_mail_htab)
        {
            for (auto &m : kv.second)
            {
                struct mail *mp = &m;
                if (mail_from_player(player, mp))
                {
                    i++;
                    if (mail_match(mp, ms, i))
                    {
                        j++;
                        const UTF8 *body = MessageFetch(mp->number);

                        UTF8 szFromName[64];
                        get_player_name(mp->from, szFromName, sizeof(szFromName));

                        m_pINotify->RawNotify(player, MOD_DASH_LINE);

                        UTF8 hdr[MOD_LBUF_SIZE];
                        format_mail_detail_header(hdr, sizeof(hdr),
                            i, szFromName,
                            reinterpret_cast<const UTF8 *>(mp->time.c_str()),
                            is_connected_visible(mp->from, player),
                            0,
                            Read(mp) ? T("Read") : T("Unread"),
                            Cleared(mp)  ? " Cleared" : "",
                            Urgent(mp)   ? " Urgent"  : "",
                            Mass(mp)     ? " Mass"    : "",
                            Forward(mp)  ? " Forward" : "",
                            M_Safe(mp)   ? " Safe"    : "",
                            Tagged(mp)   ? " Tagged"  : "",
                            reinterpret_cast<const UTF8 *>(mp->subject.c_str()),
                            65);
                        m_pINotify->RawNotify(player, hdr);
                        m_pINotify->RawNotify(player, MOD_DASH_LINE);
                        m_pINotify->RawNotify(player, body);
                        m_pINotify->RawNotify(player, MOD_DASH_LINE);
                    }
                }
            }
        }

        if (!j)
        {
            m_pINotify->RawNotify(player,
                T("MAIL: You don’t have that many matching messages!"));
        }
    }
}

void CMailMod::do_mail_review(dbref player, const UTF8 *name,
    const UTF8 *msglist)
{
    if (  nullptr != name
       && '\0' != *name
       && 0 == strcasecmp(reinterpret_cast<const char *>(name), "all"))
    {
        do_mail_review_all(player, msglist);
        return;
    }

    // Look up the target player.
    //
    if (nullptr == name || '\0' == *name)
    {
        m_pINotify->RawNotify(player, T("MAIL: No such player."));
        return;
    }

    dbref target = NOTHING;
    if (nullptr != m_pIObjectInfo)
    {
        // Prefix with '*' for player lookup.
        //
        UTF8 namebuf[256];
        mux_sprintf(namebuf, sizeof(namebuf), T("*%s"), reinterpret_cast<const char *>(name));
        m_pIObjectInfo->MatchThing(player, namebuf, &target);
    }

    if (NOTHING == target)
    {
        m_pINotify->RawNotify(player, T("MAIL: No such player."));
        return;
    }

    int i = 0, j = 0;

    if (nullptr == msglist || '\0' == *msglist)
    {
        // Summary: list mail sent to target.
        //
        UTF8 szName[64];
        get_player_name(target, szName, sizeof(szName));

        UTF8 hdr[MOD_LBUF_SIZE];
        format_mail_to_banner(hdr, sizeof(hdr), szName);
        m_pINotify->RawNotify(player, hdr);

        std::list<mail> *pList = MailList(target);
        if (pList)
        {
            for (auto &m : *pList)
            {
                struct mail *mp = &m;
                if (mail_from_player(player, mp))
                {
                    i++;

                    UTF8 szFromName[64];
                    get_player_name(mp->from, szFromName, sizeof(szFromName));

                    size_t nSize = MessageFetchSize(mp->number);
                    UTF8 line[MOD_LBUF_SIZE];
                    format_mail_list_line(line, sizeof(line),
                        status_chars(mp), i, nSize, szFromName,
                        reinterpret_cast<const UTF8 *>(mp->subject.c_str()));
                    m_pINotify->RawNotify(player, line);
                }
            }
        }
        m_pINotify->RawNotify(player, MOD_DASH_LINE);
    }
    else
    {
        // Detail mode: show full messages.
        //
        struct mail_selector ms;
        if (!parse_msglist(msglist, &ms, target))
        {
            return;
        }

        std::list<mail> *pList = MailList(target);
        if (pList)
        {
            for (auto &m : *pList)
            {
                struct mail *mp = &m;
                if (mail_from_player(player, mp))
                {
                    i++;
                    if (mail_match(mp, ms, i))
                    {
                        j++;
                        const UTF8 *body = MessageFetch(mp->number);

                        UTF8 szFromName[64];
                        get_player_name(mp->from, szFromName, sizeof(szFromName));

                        m_pINotify->RawNotify(player, MOD_DASH_LINE);

                        UTF8 hdr2[MOD_LBUF_SIZE];
                        format_mail_detail_header(hdr2, sizeof(hdr2),
                            i, szFromName,
                            reinterpret_cast<const UTF8 *>(mp->time.c_str()),
                            is_connected_visible(mp->from, player),
                            0,
                            Read(mp) ? T("Read") : T("Unread"),
                            Cleared(mp)  ? " Cleared" : "",
                            Urgent(mp)   ? " Urgent"  : "",
                            Mass(mp)     ? " Mass"    : "",
                            Forward(mp)  ? " Forward" : "",
                            M_Safe(mp)   ? " Safe"    : "",
                            Tagged(mp)   ? " Tagged"  : "",
                            reinterpret_cast<const UTF8 *>(mp->subject.c_str()),
                            65);
                        m_pINotify->RawNotify(player, hdr2);
                        m_pINotify->RawNotify(player, MOD_DASH_LINE);
                        m_pINotify->RawNotify(player, body);
                        m_pINotify->RawNotify(player, MOD_DASH_LINE);
                    }
                }
            }
        }

        if (!j)
        {
            m_pINotify->RawNotify(player,
                T("MAIL: You don’t have that many matching messages!"));
        }
    }
}

// ---------------------------------------------------------------------------
// @mail/retract — retract unread sent mail.
// ---------------------------------------------------------------------------

void CMailMod::do_mail_retract(dbref player, const UTF8 *name,
    const UTF8 *msglist)
{
    if (nullptr == name || '\0' == *name)
    {
        m_pINotify->RawNotify(player, T("MAIL: No such player."));
        return;
    }

    // Handle malias target.
    //
    if (*name == '*')
    {
        int nResult;
        malias_t *m = get_malias(player, name, &nResult);
        if (nResult == GMA_NOTFOUND)
        {
            UTF8 msg[256];
            mux_sprintf(msg, sizeof(msg), T("MAIL: Mail alias %s not found."),
                     reinterpret_cast<const char *>(name));
            m_pINotify->RawNotify(player, msg);
            return;
        }
        if (nResult == GMA_FOUND)
        {
            for (size_t k = 0; k < m->list.size(); k++)
            {
                UTF8 dbrefbuf[32];
                mux_sprintf(dbrefbuf, sizeof(dbrefbuf), T("#%d"), m->list[k]);
                do_mail_retract(player, dbrefbuf, msglist);
            }
        }
        return;
    }

    // Look up target player.
    //
    dbref target = NOTHING;
    if (nullptr != m_pIObjectInfo)
    {
        UTF8 namebuf[256];
        mux_sprintf(namebuf, sizeof(namebuf), T("*%s"), reinterpret_cast<const char *>(name));
        m_pIObjectInfo->MatchThing(player, namebuf, &target);
    }

    if (NOTHING == target)
    {
        m_pINotify->RawNotify(player, T("MAIL: No such player."));
        return;
    }

    struct mail_selector ms;
    if (!parse_msglist(msglist, &ms, target))
    {
        return;
    }

    int i = 0, j = 0;
    std::list<mail> *pList = MailList(target);
    if (pList)
    {
        for (auto it = pList->begin(); it != pList->end(); )
        {
            struct mail *mp = &*it;
            if (mail_from_player(player, mp))
            {
                i++;
                if (mail_match(mp, ms, i))
                {
                    j++;
                    if (Unread(mp))
                    {
                        sqlite_wt_delete_mail(mp);
                        MessageReferenceDec(mp->number);
                        it = pList->erase(it);
                        m_pINotify->RawNotify(player, T("MAIL: Mail retracted."));
                        continue;
                    }
                    else
                    {
                        m_pINotify->RawNotify(player,
                            T("MAIL: That message has been read."));
                    }
                }
            }
            ++it;
        }
        if (pList->empty()) m_mail_htab.erase(target);
    }

    if (!j)
    {
        m_pINotify->RawNotify(player, T("MAIL: No matching messages."));
    }
}

// ---------------------------------------------------------------------------
// @mail/nuke — admin: clear all mail in the database.
// ---------------------------------------------------------------------------

void CMailMod::do_mail_nuke(dbref player)
{
    bool bGod = false;
    if (nullptr != m_pIPermissions)
    {
        m_pIPermissions->IsGod(player, &bGod);
    }
    if (!bGod)
    {
        m_pINotify->RawNotify(player,
            T("The postal service issues a warrant for your arrest."));
        return;
    }

    // Remove all mail from all players.
    //
    for (auto it = m_mail_htab.begin(); it != m_mail_htab.end(); )
    {
        dbref target = it->first;
        // Decrement refs before erasing.
        //
        for (auto &m : it->second)
        {
            MessageReferenceDec(m.number);
        }
        sqlite_wt_delete_all_mail(target);
        it = m_mail_htab.erase(it);
    }

    UTF8 msg[128];
    mux_sprintf(msg, sizeof(msg), T("MAIL: ** MAIL PURGE ** done by #%d."), player);
    if (nullptr != m_pILog)
    {
        m_pILog->log_text(msg);
    }
    m_pINotify->RawNotify(player,
        T("You annihilate the post office. All messages cleared."));
}

// ---------------------------------------------------------------------------
// Delivery pipeline — send_mail, mail_to_list, make_numlist, make_namelist.
// ---------------------------------------------------------------------------

struct mail *CMailMod::mail_fetch(dbref player, int num)
{
    int i = 0;
    int fld = player_folder(player);
    std::list<mail> *pList = MailList(player);
    if (!pList)
    {
        return nullptr;
    }
    for (auto &m : *pList)
    {
        struct mail *mp = &m;
        if (  Folder(mp) == fld
           && mail_to_player(player, mp))
        {
            i++;
            if (i == num)
            {
                return mp;
            }
        }
    }
    return nullptr;
}

void CMailMod::send_mail
(
    dbref player,
    dbref target,
    const UTF8 *tolist,
    const UTF8 *subject,
    int number,
    mail_flag flags,
    bool silent
)
{
    // Verify target is a player.
    //
    bool bIsPlayer = false;
    if (nullptr != m_pIObjectInfo)
    {
        m_pIObjectInfo->IsPlayer(target, &bIsPlayer);
    }
    if (!bIsPlayer)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player,
                T("MAIL: You cannot send mail to non-existent people."));
        }
        return;
    }

    // Check mail lock via IMailDelivery.
    //
    if (nullptr != m_pIMailDelivery)
    {
        bool bOk = false;
        m_pIMailDelivery->MailCheck(player, target, &bOk);
        if (!bOk)
        {
            return;
        }
    }

    CLinearTimeAbsolute ltaNow;
    ltaNow.GetLocal();
    const UTF8 *pTimeStr = ltaNow.ReturnDateString(0);

    // Determine sender: if player is not a player, use owner if owner
    // is a wizard.
    //
    dbref from_player;
    {
        // Check if player itself is a player.
        //
        bool bSenderIsPlayer = false;
        if (nullptr != m_pIObjectInfo)
        {
            m_pIObjectInfo->IsPlayer(player, &bSenderIsPlayer);
        }
        if (bSenderIsPlayer)
        {
            from_player = player;
        }
        else
        {
            dbref mailbag = -1;
            if (nullptr != m_pIObjectInfo)
            {
                m_pIObjectInfo->GetOwner(player, &mailbag);
            }
            bool bWiz = false;
            if (nullptr != m_pIPermissions && mailbag >= 0)
            {
                m_pIPermissions->IsWizard(mailbag, &bWiz);
            }
            from_player = bWiz ? player : mailbag;
        }
    }

    // Build the mail struct and append first (#1196: engine counts after
    // emplace so the limit is total > max, not total >= max before insert).
    //
    mail newm;
    newm.to = target;
    newm.from = from_player;
    newm.sqlite_id = -1;

    if (!tolist || tolist[0] == '\0')
    {
        newm.tolist = "*HIDDEN*";
    }
    else
    {
        newm.tolist = reinterpret_cast<const char *>(tolist);
    }

    newm.number = number;
    MessageReferenceInc(number);
    newm.time = reinterpret_cast<const char *>(pTimeStr);
    newm.subject = reinterpret_cast<const char *>(subject);

    // Send to folder 0.
    //
    newm.read = flags & M_FMASK;

    m_mail_htab[target].push_back(std::move(newm));
    struct mail *mp = &m_mail_htab[target].back();

    // Reject if the target's mailbox is full.  Engine uses No_Mail_Expire
    // (wizard or POW_NO_MAIL_EXPIRE); COM only exposes IsWizard today.
    //
    if (0 < m_mail_per_player)
    {
        bool bExempt = false;
        if (nullptr != m_pIPermissions)
        {
            m_pIPermissions->IsWizard(target, &bExempt);
        }
        if (!bExempt)
        {
            int total = static_cast<int>(m_mail_htab[target].size());
            if (total > m_mail_per_player)
            {
                UTF8 targetname[MOD_LBUF_SIZE];
                get_player_name(target, targetname, sizeof(targetname));

                UTF8 msg[2 * MOD_LBUF_SIZE];
                mux_sprintf(msg, sizeof(msg), T("MAIL: %s’s mailbox is full (%d messages)."),
                    targetname, total - 1);
                if (nullptr != m_pINotify)
                {
                    m_pINotify->RawNotify(player, msg);
                }
                MessageReferenceDec(number);
                m_mail_htab[target].pop_back();
                return;
            }
        }
    }

    // Persist and notify.
    //
    sqlite_wt_insert_mail(mp);

    if (nullptr != m_pIMailDelivery)
    {
        m_pIMailDelivery->NotifyDelivery(mp->from, target, subject, silent);
    }
}

// make_numlist — resolve space-separated player names/aliases to dbref numbers.
//
// Returns a std::string of space-separated dbref numbers, or empty on error.
//
std::string CMailMod::make_numlist(dbref player, const UTF8 *arg, bool bBlind)
{
    if (!arg || !*arg)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player, T("MAIL: No players specified."));
        }
        return std::string();
    }

    dbref aRecip[4000];
    int nRecip = 0;

    // Work on a mutable copy.
    //
    UTF8 buf[MOD_LBUF_SIZE];
    strncpy(reinterpret_cast<char *>(buf),
            reinterpret_cast<const char *>(arg), MOD_LBUF_SIZE - 1);
    buf[MOD_LBUF_SIZE - 1] = '\0';

    UTF8 *head = buf;
    while (head && *head)
    {
        while (*head == ' ')
        {
            head++;
        }
        if (!*head)
        {
            break;
        }

        UTF8 *tail = head;
        while (*tail && *tail != ' ')
        {
            if (*tail == '"')
            {
                head++;
                tail++;
                while (*tail && *tail != '"')
                {
                    tail++;
                }
            }
            if (*tail)
            {
                tail++;
            }
        }
        // Back up to strip a trailing closing quote if present, but only
        // when there is content between head and tail; a lone '"' leaves
        // tail == head and must not be decremented (would underflow).
        if (tail > head)
        {
            tail--;
            if (*tail != '"')
            {
                tail++;
            }
        }
        UTF8 spot = *tail;
        *tail = '\0';

        if (*head == '*')
        {
            // Malias expansion.
            //
            int nResult;
            malias_t *m = get_malias(player, head, &nResult);
            if (nResult == GMA_NOTFOUND)
            {
                UTF8 msg[2 * MOD_LBUF_SIZE];
                mux_sprintf(msg, sizeof(msg), T("MAIL: Alias ‘%s’ does not exist."),
                    reinterpret_cast<const char *>(head));
                if (nullptr != m_pINotify)
                {
                    m_pINotify->RawNotify(player, msg);
                }
                return std::string();
            }
            else if (nResult == GMA_INVALIDFORM)
            {
                UTF8 msg[2 * MOD_LBUF_SIZE];
                mux_sprintf(msg, sizeof(msg), T("MAIL: ‘%s’ is a badly-formed alias."),
                    reinterpret_cast<const char *>(head));
                if (nullptr != m_pINotify)
                {
                    m_pINotify->RawNotify(player, msg);
                }
                return std::string();
            }
            for (size_t i = 0; i < m->list.size() && nRecip < 4000; i++)
            {
                aRecip[nRecip++] = m->list[i];
            }
        }
        else
        {
            // Player lookup via IObjectInfo::MatchThing.
            //
            UTF8 lookup[MOD_LBUF_SIZE + 1];
            if (*head != '*')
            {
                mux_sprintf(lookup, sizeof(lookup), T("*%s"), reinterpret_cast<const char *>(head));
            }
            else
            {
                strncpy(reinterpret_cast<char *>(lookup),
                        reinterpret_cast<const char *>(head), sizeof(lookup) - 1);
                lookup[sizeof(lookup) - 1] = '\0';
            }

            dbref target = NOTHING;
            if (nullptr != m_pIObjectInfo)
            {
                m_pIObjectInfo->MatchThing(player, lookup, &target);
            }
            if (target >= 0)
            {
                if (nRecip < 4000)
                {
                    aRecip[nRecip++] = target;
                }
            }
            else
            {
                UTF8 msg[2 * MOD_LBUF_SIZE];
                mux_sprintf(msg, sizeof(msg), T("MAIL: ‘%s’ does not exist."),
                    reinterpret_cast<const char *>(head));
                if (nullptr != m_pINotify)
                {
                    m_pINotify->RawNotify(player, msg);
                }
                return std::string();
            }
        }

        *tail = spot;
        head = tail;
        if (*head == '"')
        {
            head++;
        }
    }

    if (nRecip <= 0)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player, T("MAIL: No players specified."));
        }
        return std::string();
    }

    // De-duplicate and build result string.
    // #1066: never advance by snprintf's untruncated return value — that
    // walks past the stack buffer when the list is truncated.
    //
    UTF8 result[MOD_LBUF_SIZE];
    UTF8 *rp = result;
    for (int i = 0; i < nRecip; i++)
    {
        if (aRecip[i] != NOTHING)
        {
            // Remove duplicates.
            //
            for (int j = i + 1; j < nRecip; j++)
            {
                if (aRecip[i] == aRecip[j])
                {
                    aRecip[j] = NOTHING;
                }
            }

            size_t remain = sizeof(result) - static_cast<size_t>(rp - result);
            if (remain <= 1)
            {
                break;
            }
            if (rp != result)
            {
                *rp++ = ' ';
                remain--;
            }
            if (bBlind)
            {
                if (remain <= 1)
                {
                    break;
                }
                *rp++ = '!';
                remain--;
            }
            size_t n = mail_sprintf(rp, remain, T("%d"), aRecip[i]);
            if (n + 1 >= remain)
            {
                // Truncated incomplete number — discard it.
                *rp = '\0';
                break;
            }
            rp += n;
        }
    }
    *rp = '\0';

    return std::string(reinterpret_cast<const char *>(result));
}

// make_namelist — convert space-separated dbref numbers to player names.
//
std::string CMailMod::make_namelist(dbref player, const UTF8 *arg)
{
    UNUSED_PARAMETER(player);

    if (!arg || !*arg)
    {
        return std::string();
    }

    UTF8 buf[MOD_LBUF_SIZE];
    strncpy(reinterpret_cast<char *>(buf),
            reinterpret_cast<const char *>(arg), MOD_LBUF_SIZE - 1);
    buf[MOD_LBUF_SIZE - 1] = '\0';

    UTF8 result[MOD_LBUF_SIZE];
    UTF8 *rp = result;
    bool bFirst = true;

    char *saveptr = nullptr;
    char *token = strtok_r(reinterpret_cast<char *>(buf), " ", &saveptr);
    while (token)
    {
        if (!bFirst)
        {
            size_t remain = sizeof(result) - (rp - result);
            if (remain > 2)
            {
                *rp++ = ',';
                *rp++ = ' ';
            }
        }
        bFirst = false;

        char *p = token;
        bool bBCC = false;
        if (*p == '!')
        {
            bBCC = true;
            p++;
        }

        if (isdigit(static_cast<unsigned char>(*p)))
        {
            dbref target = atoi(p);
            UTF8 name[MOD_LBUF_SIZE];
            get_player_name(target, name, sizeof(name));

            if (bBCC)
            {
                size_t remain = sizeof(result) - (rp - result);
                if (remain > 1)
                {
                    *rp++ = '!';
                }
            }
            size_t nlen = strlen(reinterpret_cast<const char *>(name));
            size_t remain = sizeof(result) - (rp - result);
            if (nlen < remain)
            {
                memcpy(rp, name, nlen);
                rp += nlen;
            }
        }
        else
        {
            size_t tlen = strlen(token);
            size_t remain = sizeof(result) - (rp - result);
            if (tlen < remain)
            {
                memcpy(rp, token, tlen);
                rp += tlen;
            }
        }

        token = strtok_r(nullptr, " ", &saveptr);
    }
    *rp = '\0';

    return std::string(reinterpret_cast<const char *>(result));
}

// mail_to_list — send mail to all recipients in a numlist.
//
void CMailMod::mail_to_list(dbref player, UTF8 *list, const UTF8 *subject,
    const UTF8 *message, mail_flag flags, bool silent)
{
    if (!list)
    {
        return;
    }
    if (!*list)
    {
        free(list);
        return;
    }

    // Build tolist (excluding BCC) and senderlist (including BCC).
    //
    UTF8 tolist[MOD_LBUF_SIZE];
    UTF8 *p = tolist;
    UTF8 senderlist[MOD_LBUF_SIZE];
    UTF8 *sp = senderlist;
    UTF8 *head = list;

    while (*head)
    {
        while (*head == ' ')
        {
            head++;
        }
        if (!*head)
        {
            break;
        }

        UTF8 *tail = head;
        while (*tail && *tail != ' ')
        {
            if (*tail == '"')
            {
                head++;
                tail++;
                while (*tail && *tail != '"')
                {
                    tail++;
                }
            }
            if (*tail)
            {
                tail++;
            }
        }
        // Back up to strip a trailing closing quote if present, but only
        // when there is content between head and tail; a lone '"' leaves
        // tail == head and must not be decremented (would underflow).
        if (tail > head)
        {
            tail--;
            if (*tail != '"')
            {
                tail++;
            }
        }

        // Append to senderlist.
        //
        size_t seglen = tail - head;
        if (sp != senderlist)
        {
            size_t remain = sizeof(senderlist) - (sp - senderlist);
            if (remain > 1)
            {
                *sp++ = ' ';
            }
        }
        {
            size_t remain = sizeof(senderlist) - (sp - senderlist);
            if (seglen < remain)
            {
                memcpy(sp, head, seglen);
                sp += seglen;
            }
        }

        // Append to tolist if not BCC (!-prefixed).
        //
        if (*head != '!')
        {
            if (p != tolist)
            {
                size_t remain = sizeof(tolist) - (p - tolist);
                if (remain > 1)
                {
                    *p++ = ' ';
                }
            }
            size_t remain = sizeof(tolist) - (p - tolist);
            if (seglen < remain)
            {
                memcpy(p, head, seglen);
                p += seglen;
            }
        }

        head = tail;
        if (*head == '"')
        {
            head++;
        }
    }
    *p = '\0';
    *sp = '\0';

    // Add the message body (with signature evaluation).
    //
    int number = add_mail_message(player, message);
    if (number == NOTHING)
    {
        free(list);
        return;
    }

    // Iterate recipients and send.
    //
    head = list;
    while (*head)
    {
        while (*head == ' ')
        {
            head++;
        }
        if (!*head)
        {
            break;
        }

        UTF8 *tail = head;
        while (*tail && *tail != ' ')
        {
            if (*tail == '"')
            {
                head++;
                tail++;
                while (*tail && *tail != '"')
                {
                    tail++;
                }
            }
            if (*tail)
            {
                tail++;
            }
        }
        // Back up to strip a trailing closing quote if present, but only
        // when there is content between head and tail; a lone '"' leaves
        // tail == head and must not be decremented (would underflow).
        if (tail > head)
        {
            tail--;
            if (*tail != '"')
            {
                tail++;
            }
        }
        UTF8 spot = *tail;
        *tail = '\0';

        if (*head == '!')
        {
            head++;
        }

        if (*head == '*')
        {
            // Malias — expand and send to each member.
            //
            int nResult;
            malias_t *m = get_malias(player, head, &nResult);
            if (nResult == GMA_FOUND && nullptr != m)
            {
                for (size_t i = 0; i < m->list.size(); i++)
                {
                    bool bTargetIsPlayer = false;
                    if (nullptr != m_pIObjectInfo)
                    {
                        m_pIObjectInfo->IsPlayer(m->list[i], &bTargetIsPlayer);
                    }
                    if (bTargetIsPlayer)
                    {
                        send_mail(player, m->list[i],
                            (m->list[i] == player) ? senderlist : tolist,
                            subject, number, flags, silent);
                    }
                }
            }
        }
        else
        {
            dbref target = atoi(reinterpret_cast<const char *>(head));
            bool bTargetIsPlayer = false;
            if (nullptr != m_pIObjectInfo)
            {
                m_pIObjectInfo->IsPlayer(target, &bTargetIsPlayer);
            }
            if (bTargetIsPlayer)
            {
                send_mail(player, target,
                    (target == player) ? senderlist : tolist,
                    subject, number, flags, silent);
            }
        }

        *tail = spot;
        head = tail;
        if (*head == '"')
        {
            head++;
        }
    }
    MessageReferenceDec(number);
    free(list);
}

// add_mail_message — store a message body, optionally appending signature.
//
// This is the module equivalent of the server's add_mail_message(). It reads
// A_SIGNATURE via IAttributeAccess and evaluates it via IEvaluator.
//
int CMailMod::add_mail_message(dbref player, const UTF8 *message)
{
    // Reject if message says "clear" (common user error).
    //
    if (0 == strcasecmp(reinterpret_cast<const char *>(message), "clear"))
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player,
                T("MAIL: You probably did not intend to send a @mail saying "
                  "‘" "clear’."));
        }
        return NOTHING;
    }

    // Read and evaluate signature.
    //
    UTF8 sigraw[MOD_LBUF_SIZE];
    sigraw[0] = '\0';
    if (nullptr != m_pIAttributeAccess)
    {
        size_t nLen = 0;
        MUX_RESULT mr = m_pIAttributeAccess->GetAttribute(player, player,
            T("Signature"), sigraw, sizeof(sigraw) - 1, &nLen);
        if (MUX_SUCCEEDED(mr))
        {
            sigraw[nLen] = '\0';
        }
        else
        {
            sigraw[0] = '\0';
        }
    }

    // Evaluate the signature if non-empty.
    //
    UTF8 sigeval[MOD_LBUF_SIZE];
    sigeval[0] = '\0';
    if (sigraw[0] != '\0' && nullptr != m_pIMailDelivery)
    {
        // Use IEvaluator if available; otherwise use raw signature.
        //
        mux_IEvaluator *pIEval = nullptr;
        MUX_RESULT mr = mux_CreateInstance(CID_Evaluator, nullptr,
            UseSameProcess, IID_IEvaluator,
            reinterpret_cast<void **>(&pIEval));
        if (MUX_SUCCEEDED(mr) && nullptr != pIEval)
        {
            size_t nResultLen = 0;
            pIEval->Eval(player, player, player, sigraw,
                sigeval, sizeof(sigeval) - 1, &nResultLen);
            sigeval[nResultLen] = '\0';
            pIEval->Release();
        }
        else
        {
            strncpy(reinterpret_cast<char *>(sigeval),
                    reinterpret_cast<const char *>(sigraw),
                    sizeof(sigeval) - 1);
            sigeval[sizeof(sigeval) - 1] = '\0';
        }
    }

    // Combine message + signature.
    //
    UTF8 combined[MOD_LBUF_SIZE];
    if (sigeval[0] != '\0')
    {
        mux_sprintf(combined, sizeof(combined), T("%s %s"),
            reinterpret_cast<const char *>(message),
            reinterpret_cast<const char *>(sigeval));
    }
    else
    {
        strncpy(reinterpret_cast<char *>(combined),
                reinterpret_cast<const char *>(message),
                sizeof(combined) - 1);
        combined[sizeof(combined) - 1] = '\0';
    }

    return new_mail_message(combined, NOTHING);
}

// ---------------------------------------------------------------------------
// Composition commands.
// ---------------------------------------------------------------------------

void CMailMod::do_expmail_start(dbref player, const UTF8 *arg,
    const UTF8 *subject)
{
    if (!arg || !*arg)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player,
                T("MAIL: I do not know whom you want to mail."));
        }
        return;
    }
    if (!subject || !*subject)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player, T("MAIL: No subject."));
        }
        return;
    }

    // Check if already composing.
    //
    if (nullptr != m_pIMailDelivery)
    {
        bool bComposing = false;
        m_pIMailDelivery->IsComposing(player, &bComposing);
        if (bComposing)
        {
            if (nullptr != m_pINotify)
            {
                m_pINotify->RawNotify(player,
                    T("MAIL: Mail message already in progress."));
            }
            return;
        }
    }

    // Throttle check.
    //
    if (nullptr != m_pIMailDelivery)
    {
        bool bWiz = false;
        if (nullptr != m_pIPermissions)
        {
            m_pIPermissions->IsWizard(player, &bWiz);
        }
        if (!bWiz)
        {
            bool bThrottled = false;
            m_pIMailDelivery->ThrottleCheck(player, &bThrottled);
            if (bThrottled)
            {
                if (nullptr != m_pINotify)
                {
                    m_pINotify->RawNotify(player,
                        T("MAIL: Too much @mail sent recently."));
                }
                return;
            }
        }
    }

    // Resolve recipients.
    //
    std::string tolist = make_numlist(player, arg, false);
    if (tolist.empty())
    {
        return;
    }

    // Store composition state.  #1229: Mailto/Mailsub/Mailmsg/Mailflags are
    // AF_INTERNAL or AF_DARK — IAttributeAccess permission checks always
    // reject player-as-executor writes; use raw attr APIs like the engine.
    //
    set_comp_attr(player, MOD_A_MAILTO,
        reinterpret_cast<const UTF8 *>(tolist.c_str()));
    set_comp_attr(player, MOD_A_MAILSUB, subject);
    set_comp_attr(player, MOD_A_MAILFLAGS, T("0"));
    clr_comp_attr(player, MOD_A_MAILMSG);

    // Set composing flag.
    //
    if (nullptr != m_pIMailDelivery)
    {
        m_pIMailDelivery->SetComposing(player, true);
    }

    // Notify with recipient names.
    //
    std::string names = make_namelist(player,
        reinterpret_cast<const UTF8 *>(tolist.c_str()));
    if (nullptr != m_pINotify)
    {
        UTF8 msg[MOD_LBUF_SIZE];
        mux_sprintf(msg, sizeof(msg), T("MAIL: You are sending mail to ‘%s’."),
            names.c_str());
        m_pINotify->RawNotify(player, msg);
    }
}

void CMailMod::do_expmail_stop(dbref player, int flags)
{
    // Check composing state.
    //
    bool bComposing = false;
    if (nullptr != m_pIMailDelivery)
    {
        m_pIMailDelivery->IsComposing(player, &bComposing);
    }
    if (!bComposing)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player, T("MAIL: No message started."));
        }
        return;
    }

    // Read composition attributes.
    //
    UTF8 aTolist[MOD_LBUF_SIZE];
    aTolist[0] = '\0';
    UTF8 aMailMsg[MOD_LBUF_SIZE];
    aMailMsg[0] = '\0';
    UTF8 aMailSub[MOD_LBUF_SIZE];
    aMailSub[0] = '\0';
    UTF8 aMailFlags[MOD_LBUF_SIZE];
    aMailFlags[0] = '\0';

    get_comp_attr(player, MOD_A_MAILTO, aTolist, sizeof(aTolist));
    get_comp_attr(player, MOD_A_MAILMSG, aMailMsg, sizeof(aMailMsg));
    get_comp_attr(player, MOD_A_MAILSUB, aMailSub, sizeof(aMailSub));
    get_comp_attr(player, MOD_A_MAILFLAGS, aMailFlags, sizeof(aMailFlags));

    if (aTolist[0] == '\0')
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player, T("MAIL: No recipients."));
        }
        return;
    }

    if (aMailMsg[0] == '\0')
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player,
                T("MAIL: The body of this message is empty.  Use - to add to the message."));
        }
        return;
    }

    // Build numlist copy for mail_to_list (which takes ownership).
    //
    UTF8 *tolist_copy = reinterpret_cast<UTF8 *>(
        strdup(reinterpret_cast<const char *>(aTolist)));

    int combinedFlags = flags | atoi(reinterpret_cast<const char *>(aMailFlags));
    mail_to_list(player, tolist_copy, aMailSub, aMailMsg, combinedFlags, false);

    // Clear composing flag.
    //
    if (nullptr != m_pIMailDelivery)
    {
        m_pIMailDelivery->SetComposing(player, false);
    }
}

void CMailMod::do_expmail_abort(dbref player)
{
    if (nullptr != m_pIMailDelivery)
    {
        m_pIMailDelivery->SetComposing(player, false);
    }
    if (nullptr != m_pINotify)
    {
        m_pINotify->RawNotify(player, T("MAIL: Message aborted."));
    }
}

// #1196: append recipients to an in-progress composition (CC/BCC).
// bBlind prefixes each resolved dbref with '!' so make_namelist/send hide them.
//
void CMailMod::do_mail_cc(dbref player, const UTF8 *arg, bool bBlind)
{
    bool bComposing = false;
    if (nullptr != m_pIMailDelivery)
    {
        m_pIMailDelivery->IsComposing(player, &bComposing);
    }
    if (!bComposing)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player,
                T("MAIL: No mail message in progress."));
        }
        return;
    }
    if (nullptr == arg || '\0' == *arg)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player,
                T("MAIL: I do not know whom you want to mail."));
        }
        return;
    }

    std::string tolist = make_numlist(player, arg, bBlind);
    if (tolist.empty())
    {
        return;
    }

    UTF8 existing[MOD_LBUF_SIZE];
    existing[0] = '\0';
    get_comp_attr(player, MOD_A_MAILTO, existing, sizeof(existing));

    std::string fulllist = tolist;
    if ('\0' != existing[0])
    {
        fulllist.push_back(' ');
        fulllist.append(reinterpret_cast<const char *>(existing));
    }

    set_comp_attr(player, MOD_A_MAILTO,
        reinterpret_cast<const UTF8 *>(fulllist.c_str()));

    std::string names = make_namelist(player,
        reinterpret_cast<const UTF8 *>(fulllist.c_str()));
    if (nullptr != m_pINotify)
    {
        UTF8 msg[MOD_LBUF_SIZE];
        mux_sprintf(msg, sizeof(msg), T("MAIL: You are sending mail to ‘%s’."),
            names.c_str());
        m_pINotify->RawNotify(player, msg);
    }
}

void CMailMod::do_mail_quick(dbref player, const UTF8 *arg1,
    const UTF8 *arg2)
{
    if (!arg1 || !*arg1)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player,
                T("MAIL: I don’t know who you want to mail."));
        }
        return;
    }
    if (!arg2 || !*arg2)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player, T("MAIL: No message."));
        }
        return;
    }

    // Check composing state.
    //
    bool bComposing = false;
    if (nullptr != m_pIMailDelivery)
    {
        m_pIMailDelivery->IsComposing(player, &bComposing);
    }
    if (bComposing)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player,
                T("MAIL: Mail message already in progress."));
        }
        return;
    }

    // Throttle check.
    //
    bool bWiz = false;
    if (nullptr != m_pIPermissions)
    {
        m_pIPermissions->IsWizard(player, &bWiz);
    }
    if (!bWiz && nullptr != m_pIMailDelivery)
    {
        bool bThrottled = false;
        m_pIMailDelivery->ThrottleCheck(player, &bThrottled);
        if (bThrottled)
        {
            if (nullptr != m_pINotify)
            {
                m_pINotify->RawNotify(player,
                    T("MAIL: Too much @mail sent recently."));
            }
            return;
        }
    }

    // Parse "recipients/subject" from arg1.
    //
    UTF8 bufDest[MOD_LBUF_SIZE];
    strncpy(reinterpret_cast<char *>(bufDest),
            reinterpret_cast<const char *>(arg1),
            sizeof(bufDest) - 1);
    bufDest[sizeof(bufDest) - 1] = '\0';

    UTF8 *pSubject = reinterpret_cast<UTF8 *>(
        strchr(reinterpret_cast<char *>(bufDest), '/'));
    if (!pSubject)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player, T("MAIL: No subject."));
        }
        return;
    }
    *pSubject++ = '\0';

    std::string numlist = make_numlist(player, bufDest, false);
    if (numlist.empty())
    {
        return;
    }
    // mail_to_list takes ownership of a malloc'd buffer.
    //
    UTF8 *numlist_copy = reinterpret_cast<UTF8 *>(strdup(numlist.c_str()));
    if (nullptr == numlist_copy)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player,
                T("MAIL: Out of memory, message not sent."));
        }
        return;
    }
    mail_to_list(player, numlist_copy, pSubject, arg2, 0, false);
}

void CMailMod::do_mail_fwd(dbref player, const UTF8 *msg,
    const UTF8 *tolist)
{
    // Check composing state.
    //
    bool bComposing = false;
    if (nullptr != m_pIMailDelivery)
    {
        m_pIMailDelivery->IsComposing(player, &bComposing);
    }
    if (bComposing)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player,
                T("MAIL: Mail message already in progress."));
        }
        return;
    }
    if (!msg || !*msg)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player, T("MAIL: No message list."));
        }
        return;
    }
    if (!tolist || !*tolist)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player,
                T("MAIL: To whom should I forward?"));
        }
        return;
    }

    // Throttle check.
    //
    bool bWiz = false;
    if (nullptr != m_pIPermissions)
    {
        m_pIPermissions->IsWizard(player, &bWiz);
    }
    if (!bWiz && nullptr != m_pIMailDelivery)
    {
        bool bThrottled = false;
        m_pIMailDelivery->ThrottleCheck(player, &bThrottled);
        if (bThrottled)
        {
            if (nullptr != m_pINotify)
            {
                m_pINotify->RawNotify(player,
                    T("MAIL: Too much @mail sent recently."));
            }
            return;
        }
    }

    int num = atoi(reinterpret_cast<const char *>(msg));
    if (!num)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player,
                T("MAIL: I don’t understand that message number."));
        }
        return;
    }

    struct mail *mp = mail_fetch(player, num);
    if (!mp)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player,
                T("MAIL: You can’t forward non-existent messages."));
        }
        return;
    }

    // Build subject: "Original Subject (fwd from SenderName)"
    //
    UTF8 fromname[MOD_LBUF_SIZE];
    get_player_name(mp->from, fromname, sizeof(fromname));
    UTF8 subj[2 * MOD_LBUF_SIZE];
    mux_sprintf(subj, sizeof(subj), T("%s (fwd from %s)"),
        mp->subject.c_str(),
        reinterpret_cast<const char *>(fromname));

    do_expmail_start(player, tolist, subj);

    // Set message body to the forwarded message.
    //
    const UTF8 *body = MessageFetch(mp->number);
    if (body)
    {
        set_comp_attr(player, MOD_A_MAILMSG, body);
    }

    // Set M_FORWARD flag.
    //
    UTF8 aFlags[32];
    aFlags[0] = '\0';
    get_comp_attr(player, MOD_A_MAILFLAGS, aFlags, sizeof(aFlags));
    int iFlag = M_FORWARD;
    if (aFlags[0])
    {
        iFlag |= atoi(reinterpret_cast<const char *>(aFlags));
    }
    UTF8 flagbuf[16];
    mux_sprintf(flagbuf, sizeof(flagbuf), T("%d"), iFlag);
    set_comp_attr(player, MOD_A_MAILFLAGS, flagbuf);
}

void CMailMod::do_mail_reply(dbref player, const UTF8 *msg, bool all,
    int key)
{
    // Check composing state.
    //
    bool bComposing = false;
    if (nullptr != m_pIMailDelivery)
    {
        m_pIMailDelivery->IsComposing(player, &bComposing);
    }
    if (bComposing)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player,
                T("MAIL: Mail message already in progress."));
        }
        return;
    }
    if (!msg || !*msg)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player, T("MAIL: No message list."));
        }
        return;
    }

    // Throttle check.
    //
    bool bWiz = false;
    if (nullptr != m_pIPermissions)
    {
        m_pIPermissions->IsWizard(player, &bWiz);
    }
    if (!bWiz && nullptr != m_pIMailDelivery)
    {
        bool bThrottled = false;
        m_pIMailDelivery->ThrottleCheck(player, &bThrottled);
        if (bThrottled)
        {
            if (nullptr != m_pINotify)
            {
                m_pINotify->RawNotify(player,
                    T("MAIL: Too much @mail sent recently."));
            }
            return;
        }
    }

    int num = atoi(reinterpret_cast<const char *>(msg));
    if (!num)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player,
                T("MAIL: I don’t understand that message number."));
        }
        return;
    }

    struct mail *mp = mail_fetch(player, num);
    if (!mp)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player,
                T("MAIL: You can’t reply to non-existent messages."));
        }
        return;
    }

    if (!mail_from_player(mp->from, mp))
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player,
                T("MAIL: The original sender no longer exists."));
        }
        return;
    }

    // Build recipient list.
    //
    UTF8 tolist[MOD_LBUF_SIZE];
    UTF8 *bp = tolist;

    if (all)
    {
        // Include all original recipients except the original sender.
        //
        UTF8 oldlist[MOD_LBUF_SIZE];
        strncpy(reinterpret_cast<char *>(oldlist),
                mp->tolist.c_str(),
                sizeof(oldlist) - 1);
        oldlist[sizeof(oldlist) - 1] = '\0';

        char *saveptr = nullptr;
        char *token = strtok_r(reinterpret_cast<char *>(oldlist),
            " ", &saveptr);
        while (token)
        {
            if (atoi(token) != mp->from)
            {
                if (bp != tolist)
                {
                    *bp++ = ' ';
                }
                *bp++ = '#';
                size_t tlen = strlen(token);
                size_t remain = sizeof(tolist) - (bp - tolist);
                if (tlen < remain)
                {
                    memcpy(bp, token, tlen);
                    bp += tlen;
                }
            }
            token = strtok_r(nullptr, " ", &saveptr);
        }

        // Add original sender.
        //
        if (bp != tolist)
        {
            *bp++ = ' ';
        }
        bp += mail_sprintf(bp, sizeof(tolist) - (bp - tolist),
            T("#%d"), mp->from);
    }
    else
    {
        bp += mail_sprintf(bp, sizeof(tolist) - (bp - tolist),
            T("#%d"), mp->from);
    }
    *bp = '\0';

    // Build subject.
    //
    const char *pSubject = mp->subject.c_str();
    UTF8 subj[MOD_LBUF_SIZE];
    if (strncmp(pSubject, "Re:", 3))
    {
        mux_sprintf(subj, sizeof(subj), T("Re: %s"), pSubject);
    }
    else
    {
        strncpy(reinterpret_cast<char *>(subj),
                pSubject,
                sizeof(subj) - 1);
        subj[sizeof(subj) - 1] = '\0';
    }

    do_expmail_start(player, tolist, subj);

    // Quote original message if requested.
    //
    if (key & MAIL_QUOTE)
    {
        UTF8 fromname[MOD_LBUF_SIZE];
        get_player_name(mp->from, fromname, sizeof(fromname));
        const UTF8 *pMessage = MessageFetch(mp->number);
        const char *pTime = mp->time.c_str();

        UTF8 body[4 * MOD_LBUF_SIZE];
        mux_sprintf(body, sizeof(body), T("On %s, %s wrote:\r\n\r\n%s\r\n\r\n********** End of included message from %s\r\n"),
            pTime,
            reinterpret_cast<const char *>(fromname),
            pMessage ? reinterpret_cast<const char *>(pMessage) : "",
            reinterpret_cast<const char *>(fromname));

        set_comp_attr(player, MOD_A_MAILMSG, body);
    }

    // Set M_REPLY flag.
    //
    UTF8 aFlags[32];
    aFlags[0] = '\0';
    get_comp_attr(player, MOD_A_MAILFLAGS, aFlags, sizeof(aFlags));
    int iFlag = M_REPLY;
    if (aFlags[0])
    {
        iFlag |= atoi(reinterpret_cast<const char *>(aFlags));
    }
    UTF8 flagbuf[16];
    mux_sprintf(flagbuf, sizeof(flagbuf), T("%d"), iFlag);
    set_comp_attr(player, MOD_A_MAILFLAGS, flagbuf);
}

void CMailMod::do_mail_proof(dbref player)
{
    // Check composing state.
    //
    bool bComposing = false;
    if (nullptr != m_pIMailDelivery)
    {
        m_pIMailDelivery->IsComposing(player, &bComposing);
    }
    if (!bComposing)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player, T("MAIL: No message started."));
        }
        return;
    }

    // Read and display the draft (#1229: raw attr APIs for AF_INTERNAL).
    //
    UTF8 aTolist[MOD_LBUF_SIZE];
    aTolist[0] = '\0';
    UTF8 aMailMsg[MOD_LBUF_SIZE];
    aMailMsg[0] = '\0';
    UTF8 aMailSub[MOD_LBUF_SIZE];
    aMailSub[0] = '\0';

    get_comp_attr(player, MOD_A_MAILTO, aTolist, sizeof(aTolist));
    get_comp_attr(player, MOD_A_MAILMSG, aMailMsg, sizeof(aMailMsg));
    get_comp_attr(player, MOD_A_MAILSUB, aMailSub, sizeof(aMailSub));

    std::string names = make_namelist(player, aTolist);
    if (nullptr != m_pINotify)
    {
        UTF8 msg[MOD_LBUF_SIZE];
        mux_sprintf(msg, sizeof(msg), T("MAIL: To: %s"),
            names.c_str());
        m_pINotify->RawNotify(player, msg);

        mux_sprintf(msg, sizeof(msg), T("MAIL: Subject: %s"),
            reinterpret_cast<const char *>(aMailSub));
        m_pINotify->RawNotify(player, msg);

        m_pINotify->RawNotify(player, aMailMsg);
    }
}

void CMailMod::do_edit_msg(dbref player, const UTF8 *from, const UTF8 *to)
{
    // Check composing state.
    //
    bool bComposing = false;
    if (nullptr != m_pIMailDelivery)
    {
        m_pIMailDelivery->IsComposing(player, &bComposing);
    }
    if (!bComposing)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player, T("MAIL: No message in progress."));
        }
        return;
    }

    // Read current message.
    //
    UTF8 aMailMsg[MOD_LBUF_SIZE];
    aMailMsg[0] = '\0';
    get_comp_attr(player, MOD_A_MAILMSG, aMailMsg, sizeof(aMailMsg));

    // Simple find-and-replace (first occurrence only, matching server behavior).
    //
    const char *pFrom = reinterpret_cast<const char *>(from);
    const char *pTo = reinterpret_cast<const char *>(to);
    size_t nFrom = strlen(pFrom);
    size_t nTo = strlen(pTo);

    if (nFrom == 0)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player, T("MAIL: Nothing to edit."));
        }
        return;
    }

    // Build result with all occurrences replaced.
    //
    UTF8 result[MOD_LBUF_SIZE];
    char *rp = reinterpret_cast<char *>(result);
    const char *src = reinterpret_cast<const char *>(aMailMsg);
    size_t remain = sizeof(result) - 1;

    while (*src)
    {
        const char *found = strstr(src, pFrom);
        if (found)
        {
            // Copy prefix.
            //
            size_t prefix = found - src;
            if (prefix > remain)
            {
                prefix = remain;
            }
            memcpy(rp, src, prefix);
            rp += prefix;
            remain -= prefix;

            // Copy replacement.
            //
            size_t rlen = nTo;
            if (rlen > remain)
            {
                rlen = remain;
            }
            memcpy(rp, pTo, rlen);
            rp += rlen;
            remain -= rlen;

            src = found + nFrom;
        }
        else
        {
            // Copy rest.
            //
            size_t rest = strlen(src);
            if (rest > remain)
            {
                rest = remain;
            }
            memcpy(rp, src, rest);
            rp += rest;
            remain -= rest;
            break;
        }
    }
    *rp = '\0';

    set_comp_attr(player, MOD_A_MAILMSG, result);

    if (nullptr != m_pINotify)
    {
        m_pINotify->RawNotify(player, T("Text edited."));
    }
}

// ---------------------------------------------------------------------------
// Malias helpers and commands.
// ---------------------------------------------------------------------------

bool CMailMod::is_exp_mail(dbref player)
{
    bool bWiz = false;
    if (nullptr != m_pIPermissions)
    {
        m_pIPermissions->IsWizard(player, &bWiz);
    }
    return bWiz;
}

bool CMailMod::make_canonical_alias(const UTF8 *alias, UTF8 *buf,
    size_t bufsize, size_t *pnLen)
{
    if (nullptr == alias || !isalpha(*alias))
    {
        *pnLen = 0;
        return false;
    }

    const UTF8 *p = alias;
    UTF8 *q = buf;
    size_t nLeft = bufsize - 1;

    while (*p && nLeft)
    {
        if (!isalpha(*p) && !isdigit(*p) && *p != '_')
        {
            break;
        }
        *q++ = *p++;
        nLeft--;
    }
    *q = '\0';
    *pnLen = q - buf;
    return true;
}

malias_t *CMailMod::get_malias(dbref player, const UTF8 *alias, int *pnResult)
{
    *pnResult = GMA_INVALIDFORM;
    if (nullptr == alias)
    {
        return nullptr;
    }

    if (alias[0] == '#')
    {
        if (is_exp_mail(player))
        {
            int x = atoi(reinterpret_cast<const char *>(alias + 1));
            if (x < 0 || x >= static_cast<int>(m_malias.size()))
            {
                *pnResult = GMA_NOTFOUND;
                return nullptr;
            }
            *pnResult = GMA_FOUND;
            return m_malias[x].get();
        }
    }
    else if (alias[0] == '*')
    {
        UTF8 canonical[SIZEOF_MALIAS];
        size_t nLen;
        if (make_canonical_alias(alias + 1, canonical, sizeof(canonical), &nLen))
        {
            for (size_t i = 0; i < m_malias.size(); i++)
            {
                malias_t *m = m_malias[i].get();
                if (  m->owner == player
                   || m->owner == 1  // GOD
                   || is_exp_mail(player))
                {
                    if (0 == strcasecmp(
                            reinterpret_cast<const char *>(canonical),
                            m->name.c_str()))
                    {
                        *pnResult = GMA_FOUND;
                        return m;
                    }
                }
            }
            *pnResult = GMA_NOTFOUND;
        }
    }

    if (*pnResult == GMA_INVALIDFORM)
    {
        if (is_exp_mail(player))
        {
            m_pINotify->RawNotify(player,
                T("MAIL: Mail aliases must be of the form *<name> or #<num>."));
        }
        else
        {
            m_pINotify->RawNotify(player,
                T("MAIL: Mail aliases must be of the form *<name>."));
        }
    }
    return nullptr;
}

void CMailMod::do_malias_create(dbref player, const UTF8 *alias,
    const UTF8 *tolist)
{
    int nResult;
    get_malias(player, alias, &nResult);

    if (nResult == GMA_INVALIDFORM)
    {
        m_pINotify->RawNotify(player,
            T("MAIL: What alias do you want to create?."));
        return;
    }
    if (nResult == GMA_FOUND)
    {
        UTF8 msg[256];
        mux_sprintf(msg, sizeof(msg), T("MAIL: Mail Alias ‘%s’ already exists."),
                 reinterpret_cast<const char *>(alias));
        m_pINotify->RawNotify(player, msg);
        return;
    }

    auto pt = std::make_unique<malias_t>();

    // Parse the player list.
    //
    UTF8 head_buf[MOD_LBUF_SIZE];
    strncpy(reinterpret_cast<char *>(head_buf),
            reinterpret_cast<const char *>(tolist),
            sizeof(head_buf) - 1);
    head_buf[sizeof(head_buf) - 1] = '\0';

    char *head = reinterpret_cast<char *>(head_buf);
    int count = 0;

    while (*head)
    {
        while (*head == ' ')
        {
            head++;
        }
        if (!*head)
        {
            break;
        }

        char *tail = head;
        if (*tail == '"')
        {
            head++;
            tail++;
            while (*tail && *tail != '"')
            {
                tail++;
            }
        }
        else
        {
            while (*tail && *tail != ' ')
            {
                tail++;
            }
        }

        char saved = *tail;
        *tail = '\0';

        // Look up target.
        //
        dbref target = NOTHING;
        if (0 == strcasecmp(head, "me"))
        {
            target = player;
        }
        else if (*head == '#')
        {
            target = atoi(head + 1);
        }
        else if (nullptr != m_pIObjectInfo)
        {
            UTF8 namebuf[MOD_LBUF_SIZE + 1];
            mux_sprintf(namebuf, sizeof(namebuf), T("*%s"), head);
            m_pIObjectInfo->MatchThing(player, namebuf, &target);
        }

        bool bPlayer = false;
        if (target >= 0 && nullptr != m_pIObjectInfo)
        {
            m_pIObjectInfo->IsPlayer(target, &bPlayer);
        }

        if (!bPlayer)
        {
            m_pINotify->RawNotify(player, T("MAIL: No such player."));
        }
        else
        {
            UTF8 addmsg[256];
            UTF8 szName[64];
            get_player_name(target, szName, sizeof(szName));
            mux_sprintf(addmsg, sizeof(addmsg), T("MAIL: %s added to alias %s"),
                     reinterpret_cast<const char *>(szName),
                     reinterpret_cast<const char *>(alias));
            m_pINotify->RawNotify(player, addmsg);
            pt->list.push_back(target);
            count++;
        }

        *tail = saved;
        head = tail;
        if (*head == '"' || *head == ' ')
        {
            head++;
        }
    }

    UTF8 canonical[SIZEOF_MALIAS];
    size_t nLen;
    if (!make_canonical_alias(alias + 1, canonical, sizeof(canonical), &nLen))
    {
        m_pINotify->RawNotify(player, T("MAIL: Invalid mail alias."));
        return;
    }

    pt->name = reinterpret_cast<const char *>(canonical);
    pt->owner = player;
    pt->desc = reinterpret_cast<const char *>(canonical);
    pt->desc_width = nLen;
    m_malias.push_back(std::move(pt));
    sqlite_wt_sync_all_aliases();

    UTF8 msg[256];
    mux_sprintf(msg, sizeof(msg), T("MAIL: Alias set ‘%s’ defined."),
             reinterpret_cast<const char *>(alias));
    m_pINotify->RawNotify(player, msg);
}

void CMailMod::do_malias_list(dbref player, const UTF8 *alias)
{
    int nResult;
    malias_t *m = get_malias(player, alias, &nResult);
    if (nResult == GMA_NOTFOUND)
    {
        UTF8 msg[256];
        mux_sprintf(msg, sizeof(msg), T("MAIL: Alias ‘%s’ not found."),
                 reinterpret_cast<const char *>(alias));
        m_pINotify->RawNotify(player, msg);
        return;
    }
    if (nResult != GMA_FOUND)
    {
        return;
    }

    bool bGod = false;
    if (nullptr != m_pIPermissions)
    {
        m_pIPermissions->IsGod(m->owner, &bGod);
    }
    if (!is_exp_mail(player) && player != m->owner && !bGod)
    {
        m_pINotify->RawNotify(player, T("MAIL: Permission denied."));
        return;
    }

    UTF8 buf[MOD_LBUF_SIZE];
    UTF8 *bp = buf;
    bp += mail_sprintf(bp, sizeof(buf), T("MAIL: Alias *%s: "),
        reinterpret_cast<const UTF8 *>(m->name.c_str()));

    for (int i = static_cast<int>(m->list.size()) - 1; i >= 0; i--)
    {
        UTF8 szName[64];
        get_player_name(m->list[i], szName, sizeof(szName));
        size_t remain = sizeof(buf) - static_cast<size_t>(bp - buf);
        if (remain < 64)
        {
            break;
        }
        if (strchr(reinterpret_cast<const char *>(szName), ' '))
        {
            bp += mail_sprintf(bp, remain, T("\"%s\" "), szName);
        }
        else
        {
            bp += mail_sprintf(bp, remain, T("%s "), szName);
        }
    }
    m_pINotify->RawNotify(player, buf);
}

void CMailMod::do_malias_list_all(dbref player)
{
    bool bGod = false;
    if (nullptr != m_pIPermissions)
    {
        m_pIPermissions->IsGod(player, &bGod);
    }

    bool notified = false;
    for (size_t i = 0; i < m_malias.size(); i++)
    {
        malias_t *m = m_malias[i].get();
        bool bOwnerGod = false;
        if (nullptr != m_pIPermissions)
        {
            m_pIPermissions->IsGod(m->owner, &bOwnerGod);
        }
        if (bOwnerGod || m->owner == player || bGod)
        {
            if (!notified)
            {
                m_pINotify->RawNotify(player,
                    T("Name         Description                              Owner"));
                notified = true;
            }

            UTF8 szOwner[64];
            get_player_name(m->owner, szOwner, sizeof(szOwner));

            UTF8 line[256];
            size_t pos = 0;
            pos = append_ljust_field(line, sizeof(line), pos,
                reinterpret_cast<const UTF8 *>(m->name.c_str()), 12);
            pos = append_bytes(line, sizeof(line), pos, " ");
            pos = append_ljust_field(line, sizeof(line), pos,
                reinterpret_cast<const UTF8 *>(m->desc.c_str()), 40);
            pos = append_bytes(line, sizeof(line), pos, " ");
            pos = append_ljust_field(line, sizeof(line), pos, szOwner, 15);
            m_pINotify->RawNotify(player, line);
        }
    }
    m_pINotify->RawNotify(player, T("*****  End of Mail Aliases *****"));
}

void CMailMod::do_malias_adminlist(dbref player)
{
    if (!is_exp_mail(player))
    {
        do_malias_list_all(player);
        return;
    }

    m_pINotify->RawNotify(player,
        T("Num  Name         Description                              Owner"));

    for (size_t i = 0; i < m_malias.size(); i++)
    {
        malias_t *m = m_malias[i].get();
        UTF8 szOwner[64];
        get_player_name(m->owner, szOwner, sizeof(szOwner));

        UTF8 line[256];
        size_t pos = 0;
        if (pos < sizeof(line))
        {
            mux_sprintf(line + pos, sizeof(line) - pos, T("%-4d "),
                static_cast<int>(i));
            pos = pos_after(line, sizeof(line), pos);
        }
        pos = append_ljust_field(line, sizeof(line), pos,
            reinterpret_cast<const UTF8 *>(m->name.c_str()), 12);
        pos = append_bytes(line, sizeof(line), pos, " ");
        pos = append_ljust_field(line, sizeof(line), pos,
            reinterpret_cast<const UTF8 *>(m->desc.c_str()), 40);
        pos = append_bytes(line, sizeof(line), pos, " ");
        pos = append_ljust_field(line, sizeof(line), pos, szOwner, 15);
        m_pINotify->RawNotify(player, line);
    }
    m_pINotify->RawNotify(player, T("***** End of Mail Aliases *****"));
}

void CMailMod::do_malias_desc(dbref player, const UTF8 *alias,
    const UTF8 *desc)
{
    int nResult;
    malias_t *m = get_malias(player, alias, &nResult);
    if (nResult == GMA_NOTFOUND)
    {
        UTF8 msg[256];
        mux_sprintf(msg, sizeof(msg), T("MAIL: Alias ‘%s’ not found."),
                 reinterpret_cast<const char *>(alias));
        m_pINotify->RawNotify(player, msg);
        return;
    }
    if (nResult != GMA_FOUND)
    {
        return;
    }
    bool bOwnerGod = false;
    if (nullptr != m_pIPermissions)
    {
        m_pIPermissions->IsGod(m->owner, &bOwnerGod);
    }
    if (bOwnerGod && !is_exp_mail(player))
    {
        m_pINotify->RawNotify(player, T("MAIL: Permission denied."));
        return;
    }

    if (nullptr == desc || '\0' == *desc)
    {
        m_pINotify->RawNotify(player, T("MAIL: Description is not valid."));
        return;
    }

    // Simple validation: just use the desc as-is (truncated).
    //
    size_t nDesc = strlen(reinterpret_cast<const char *>(desc));
    if (nDesc > 40)
    {
        nDesc = 40;
    }
    m->desc.assign(reinterpret_cast<const char *>(desc), nDesc);
    m->desc_width = nDesc;
    sqlite_wt_sync_all_aliases();
    m_pINotify->RawNotify(player, T("MAIL: Description changed."));
}

void CMailMod::do_malias_chown(dbref player, const UTF8 *alias,
    const UTF8 *owner)
{
    if (!is_exp_mail(player))
    {
        m_pINotify->RawNotify(player, T("MAIL: You cannot do that!"));
        return;
    }

    int nResult;
    malias_t *m = get_malias(player, alias, &nResult);
    if (nResult == GMA_NOTFOUND)
    {
        UTF8 msg[256];
        mux_sprintf(msg, sizeof(msg), T("MAIL: Alias ‘%s’ not found."),
                 reinterpret_cast<const char *>(alias));
        m_pINotify->RawNotify(player, msg);
        return;
    }
    if (nResult != GMA_FOUND)
    {
        return;
    }

    dbref target = NOTHING;
    if (nullptr != m_pIObjectInfo)
    {
        UTF8 namebuf[256];
        mux_sprintf(namebuf, sizeof(namebuf), T("*%s"), reinterpret_cast<const char *>(owner));
        m_pIObjectInfo->MatchThing(player, namebuf, &target);
    }
    if (NOTHING == target)
    {
        m_pINotify->RawNotify(player, T("MAIL: I do not see that here."));
        return;
    }
    m->owner = target;
    sqlite_wt_sync_all_aliases();
    m_pINotify->RawNotify(player, T("MAIL: Owner changed for alias."));
}

void CMailMod::do_malias_add(dbref player, const UTF8 *alias,
    const UTF8 *person)
{
    int nResult;
    malias_t *m = get_malias(player, alias, &nResult);
    if (nResult == GMA_NOTFOUND)
    {
        UTF8 msg[256];
        mux_sprintf(msg, sizeof(msg), T("MAIL: Alias ‘%s’ not found."),
                 reinterpret_cast<const char *>(alias));
        m_pINotify->RawNotify(player, msg);
        return;
    }
    if (nResult != GMA_FOUND)
    {
        return;
    }

    bool bOwnerGod = false;
    if (nullptr != m_pIPermissions)
    {
        m_pIPermissions->IsGod(m->owner, &bOwnerGod);
    }
    if (bOwnerGod && !is_exp_mail(player))
    {
        m_pINotify->RawNotify(player, T("MAIL: Permission denied."));
        return;
    }

    dbref thing = NOTHING;
    if (nullptr != person && *person == '#')
    {
        thing = atoi(reinterpret_cast<const char *>(person) + 1);
        bool bPlayer = false;
        if (thing >= 0 && nullptr != m_pIObjectInfo)
        {
            m_pIObjectInfo->IsPlayer(thing, &bPlayer);
        }
        if (!bPlayer)
        {
            m_pINotify->RawNotify(player, T("MAIL: Only players may be added."));
            return;
        }
    }
    if (NOTHING == thing && nullptr != m_pIObjectInfo)
    {
        UTF8 namebuf[256];
        mux_sprintf(namebuf, sizeof(namebuf), T("*%s"), reinterpret_cast<const char *>(person));
        m_pIObjectInfo->MatchThing(player, namebuf, &thing);
    }
    if (NOTHING == thing)
    {
        m_pINotify->RawNotify(player, T("MAIL: I do not see that person here."));
        return;
    }

    for (size_t i = 0; i < m->list.size(); i++)
    {
        if (m->list[i] == thing)
        {
            m_pINotify->RawNotify(player,
                T("MAIL: That person is already on the list."));
            return;
        }
    }

    m->list.push_back(thing);
    sqlite_wt_sync_all_aliases();

    UTF8 szName[64];
    get_player_name(thing, szName, sizeof(szName));
    UTF8 msg[256];
    mux_sprintf(msg, sizeof(msg), T("MAIL: %s added to %s"),
             reinterpret_cast<const char *>(szName),
             m->name.c_str());
    m_pINotify->RawNotify(player, msg);
}

void CMailMod::do_malias_remove(dbref player, const UTF8 *alias,
    const UTF8 *person)
{
    int nResult;
    malias_t *m = get_malias(player, alias, &nResult);
    if (nResult == GMA_NOTFOUND)
    {
        UTF8 msg[256];
        mux_sprintf(msg, sizeof(msg), T("MAIL: Alias ‘%s’ not found."),
                 reinterpret_cast<const char *>(alias));
        m_pINotify->RawNotify(player, msg);
        return;
    }
    if (nResult != GMA_FOUND)
    {
        return;
    }

    bool bOwnerGod = false;
    if (nullptr != m_pIPermissions)
    {
        m_pIPermissions->IsGod(m->owner, &bOwnerGod);
    }
    if (bOwnerGod && !is_exp_mail(player))
    {
        m_pINotify->RawNotify(player, T("MAIL: Permission denied."));
        return;
    }

    dbref thing = NOTHING;
    if (nullptr != person && *person == '#')
    {
        thing = atoi(reinterpret_cast<const char *>(person) + 1);
    }
    if (NOTHING == thing && nullptr != m_pIObjectInfo)
    {
        UTF8 namebuf[256];
        mux_sprintf(namebuf, sizeof(namebuf), T("*%s"), reinterpret_cast<const char *>(person));
        m_pIObjectInfo->MatchThing(player, namebuf, &thing);
    }
    if (NOTHING == thing)
    {
        m_pINotify->RawNotify(player, T("MAIL: I do not see that person here."));
        return;
    }

    auto it = std::find(m->list.begin(), m->list.end(), thing);
    bool found = (it != m->list.end());

    if (found)
    {
        m->list.erase(it);
        sqlite_wt_sync_all_aliases();

        UTF8 szName[64];
        get_player_name(thing, szName, sizeof(szName));
        UTF8 msg[256];
        mux_sprintf(msg, sizeof(msg), T("MAIL: %s removed from alias %s."),
                 reinterpret_cast<const char *>(szName),
                 reinterpret_cast<const char *>(alias));
        m_pINotify->RawNotify(player, msg);
    }
    else
    {
        UTF8 szName[64];
        get_player_name(thing, szName, sizeof(szName));
        UTF8 msg[256];
        mux_sprintf(msg, sizeof(msg), T("MAIL: %s is not a member of alias %s."),
                 reinterpret_cast<const char *>(szName),
                 reinterpret_cast<const char *>(alias));
        m_pINotify->RawNotify(player, msg);
    }
}

void CMailMod::do_malias_rename(dbref player, const UTF8 *alias,
    const UTF8 *newname)
{
    int nResult;
    malias_t *m = get_malias(player, newname, &nResult);
    if (nResult == GMA_FOUND)
    {
        m_pINotify->RawNotify(player, T("MAIL: That name already exists!"));
        return;
    }
    if (nResult != GMA_NOTFOUND)
    {
        return;
    }
    m = get_malias(player, alias, &nResult);
    if (nResult == GMA_NOTFOUND)
    {
        m_pINotify->RawNotify(player, T("MAIL: I cannot find that alias!"));
        return;
    }
    if (nResult != GMA_FOUND)
    {
        return;
    }
    if (!is_exp_mail(player) && m->owner != player)
    {
        m_pINotify->RawNotify(player, T("MAIL: Permission denied."));
        return;
    }

    UTF8 canonical[SIZEOF_MALIAS];
    size_t nLen;
    if (make_canonical_alias(newname + 1, canonical, sizeof(canonical), &nLen))
    {
        m->name = reinterpret_cast<const char *>(canonical);
        sqlite_wt_sync_all_aliases();
        m_pINotify->RawNotify(player, T("MAIL: Mailing Alias renamed."));
    }
    else
    {
        m_pINotify->RawNotify(player, T("MAIL: Alias is not valid."));
    }
}

void CMailMod::do_malias_delete(dbref player, const UTF8 *alias)
{
    int nResult;
    malias_t *m = get_malias(player, alias, &nResult);
    if (nResult == GMA_NOTFOUND)
    {
        UTF8 msg[256];
        mux_sprintf(msg, sizeof(msg), T("MAIL: Alias ‘%s’ not found."),
                 reinterpret_cast<const char *>(alias));
        m_pINotify->RawNotify(player, msg);
        return;
    }
    if (nResult != GMA_FOUND)
    {
        return;
    }

    bool done = false;
    for (size_t i = 0; i < m_malias.size(); i++)
    {
        if (m == m_malias[i].get())
        {
            if (m->owner == player || is_exp_mail(player))
            {
                done = true;
                m_pINotify->RawNotify(player, T("MAIL: Alias Deleted."));
                m_malias.erase(m_malias.begin() + i);
                break;
            }
        }
    }

    if (!done)
    {
        UTF8 msg[256];
        mux_sprintf(msg, sizeof(msg), T("MAIL: Alias ‘%s’ not found."),
                 reinterpret_cast<const char *>(alias));
        m_pINotify->RawNotify(player, msg);
    }
    else
    {
        sqlite_wt_sync_all_aliases();
    }
}

void CMailMod::do_malias_status(dbref player)
{
    if (!is_exp_mail(player))
    {
        m_pINotify->RawNotify(player, T("MAIL: Permission denied."));
        return;
    }
    UTF8 msg[128];
    mux_sprintf(msg, sizeof(msg), T("MAIL: Number of mail aliases defined: %d"),
             static_cast<int>(m_malias.size()));
    m_pINotify->RawNotify(player, msg);
    mux_sprintf(msg, sizeof(msg), T("MAIL: Allocated slots %d"),
             static_cast<int>(m_malias.capacity()));
    m_pINotify->RawNotify(player, msg);
}

void CMailMod::do_malias_switch(dbref player, const UTF8 *a1,
    const UTF8 *a2)
{
    if (nullptr != a1 && '\0' != *a1)
    {
        if (nullptr != a2 && '\0' != *a2)
        {
            do_malias_create(player, a1, a2);
        }
        else
        {
            do_malias_list(player, a1);
        }
    }
    else
    {
        do_malias_list_all(player);
    }
}

// ---------------------------------------------------------------------------
// Command implementations.
// ---------------------------------------------------------------------------

void CMailMod::do_mail_flags(dbref player, const UTF8 *msglist,
    mail_flag flag, bool negate)
{
    struct mail_selector ms;

    if (!parse_msglist(msglist, &ms, player))
    {
        return;
    }

    int i = 0, j = 0;
    int folder = player_folder(player);

    std::list<mail> *pList = MailList(player);
    if (pList)
    {
        for (auto &m : *pList)
        {
            struct mail *mp = &m;
            if (All(ms) || Folder(mp) == folder)
            {
                i++;
                if (mail_match(mp, ms, i))
                {
                    j++;
                    if (negate)
                    {
                        mp->read &= ~flag;
                    }
                    else
                    {
                        mp->read |= flag;
                    }
                    sqlite_wt_update_mail_flags(mp);

                    UTF8 msg[MOD_LBUF_SIZE];
                    switch (flag)
                    {
                    case M_TAG:
                        mux_sprintf(msg, sizeof(msg), T("MAIL: Msg #%d %s."), i,
                                 negate ? "untagged" : "tagged");
                        break;

                    case M_CLEARED:
                        if (Unread(mp) && !negate)
                        {
                            mux_sprintf(msg, sizeof(msg), T("MAIL: Unread Msg #%d cleared! Use @mail/unclear %d to recover."),
                                     i, i);
                        }
                        else
                        {
                            mux_sprintf(msg, sizeof(msg), T("MAIL: Msg #%d %s."), i,
                                     negate ? "uncleared" : "cleared");
                        }
                        break;

                    case M_SAFE:
                        mux_sprintf(msg, sizeof(msg), T("MAIL: Msg #%d %s."), i,
                                 negate ? "marked unsafe" : "marked safe");
                        break;

                    default:
                        msg[0] = '\0';
                        break;
                    }

                    if (msg[0] != '\0')
                    {
                        m_pINotify->RawNotify(player, msg);
                    }
                }
            }
        }
    }

    if (!j)
    {
        m_pINotify->RawNotify(player,
            T("MAIL: You don’t have any matching messages!"));
    }
}

// ---------------------------------------------------------------------------
// Display helpers.
// ---------------------------------------------------------------------------

UTF8 *CMailMod::status_chars(struct mail *mp)
{
    thread_local UTF8 res[10];
    UTF8 *p = res;
    *p++ = Read(mp)     ? '-' : 'N';
    *p++ = M_Safe(mp)   ? 'S' : '-';
    *p++ = Cleared(mp)  ? 'C' : '-';
    *p++ = Urgent(mp)   ? 'U' : '-';
    *p++ = Mass(mp)     ? 'M' : '-';
    *p++ = Forward(mp)  ? 'F' : '-';
    *p++ = Tagged(mp)   ? '+' : '-';
    *p = '\0';
    return res;
}

UTF8 *CMailMod::mail_list_time(const UTF8 *the_time)
{
    // Format: "day mon dd hh:mm:ss yyyy" → "day mon dd hh:mm yyyy"
    // Chop out ":ss"
    //
    thread_local UTF8 buf[32];
    if (nullptr == the_time || '\0' == *the_time)
    {
        buf[0] = '\0';
        return buf;
    }

    const char *p = reinterpret_cast<const char *>(the_time);
    char *q = reinterpret_cast<char *>(buf);
    int i;

    // Copy first 16 chars (up to and including "hh:mm")
    //
    for (i = 0; i < 16 && *p; i++)
    {
        *q++ = *p++;
    }

    // Skip 3 chars (":ss")
    //
    for (i = 0; i < 3 && *p; i++)
    {
        p++;
    }

    // Copy remaining (up to 5 chars for " yyyy")
    //
    for (i = 0; i < 5 && *p; i++)
    {
        *q++ = *p++;
    }
    *q = '\0';
    return buf;
}

void CMailMod::get_player_name(dbref who, UTF8 *buf, size_t bufsize)
{
    const UTF8 *pName = nullptr;
    if (nullptr != m_pIObjectInfo)
    {
        m_pIObjectInfo->GetName(who, &pName);
    }
    if (nullptr != pName)
    {
        strncpy(reinterpret_cast<char *>(buf),
                reinterpret_cast<const char *>(pName), bufsize - 1);
        buf[bufsize - 1] = '\0';
    }
    else
    {
        mux_sprintf(buf, bufsize, T("#%d"), who);
    }
}

bool CMailMod::is_connected_visible(dbref who, dbref viewer)
{
    if (nullptr == m_pIObjectInfo)
    {
        return false;
    }
    bool bConnected = false;
    m_pIObjectInfo->IsConnected(who, &bConnected);
    if (!bConnected)
    {
        return false;
    }

    // If 'who' is Hidden (DARK flag), only visible to See_Hidden viewers.
    //
    unsigned int flags = 0;
    m_pIObjectInfo->GetFlags(who, 0, &flags);  // FLAG_WORD1 = 0
    if (flags & 0x00000040)  // DARK = 0x00000040
    {
        bool bSeeHidden = false;
        m_pIObjectInfo->SeeHidden(viewer, &bSeeHidden);
        return bSeeHidden;
    }
    return true;
}

// ---------------------------------------------------------------------------
// @mail/list — list messages in current folder.
// ---------------------------------------------------------------------------

void CMailMod::do_mail_list(dbref player, const UTF8 *arg1,
    const UTF8 *arg2)
{
    const UTF8 *msglist;
    int folder = player_folder(player);

    if (nullptr == arg2 || '\0' == *arg2)
    {
        msglist = arg1;
    }
    else
    {
        // arg1 is a folder, arg2 is the msglist.
        //
        if (nullptr != arg1 && isdigit(*arg1))
        {
            int f = atoi(reinterpret_cast<const char *>(arg1));
            if (f >= 0 && f <= MAX_FOLDERS)
            {
                folder = f;
            }
        }
        msglist = arg2;
    }

    struct mail_selector ms;
    if (!parse_msglist(msglist, &ms, player))
    {
        return;
    }

    UTF8 hdr[MOD_LBUF_SIZE];
    mux_sprintf(hdr, sizeof(hdr), T("––––––"
             "––––––"
             "––––––"
             "––––––"
             "–––"
             "   MAIL: Folder %d   "
             "––––––"
             "––––––"
             "––––––"
             "––––––"
             "––––"),
             folder);
    m_pINotify->RawNotify(player, hdr);

    int i = 0;

    std::list<mail> *pList = MailList(player);
    if (pList)
    {
        for (auto &m : *pList)
        {
            struct mail *mp = &m;
            if (Folder(mp) == folder)
            {
                i++;
                if (mail_match(mp, ms, i))
                {
                    UTF8 *time = mail_list_time(
                        reinterpret_cast<const UTF8 *>(mp->time.c_str()));
                    size_t nSize = MessageFetchSize(mp->number);

                    UTF8 szFromName[64];
                    get_player_name(mp->from, szFromName, sizeof(szFromName));

                    UTF8 line[MOD_LBUF_SIZE];
                    size_t pos = 0;
                    pos = append_bytes(line, sizeof(line), pos, "[");
                    pos = append_bytes(line, sizeof(line), pos,
                        reinterpret_cast<const char *>(status_chars(mp)));
                    pos = append_bytes(line, sizeof(line), pos, "] ");
                    if (pos < sizeof(line))
                    {
                        mux_sprintf(line + pos, sizeof(line) - pos,
                            T("%-3d (%4zu) From: "), i, nSize);
                        pos = pos_after(line, sizeof(line), pos);
                    }
                    pos = append_ljust_field(line, sizeof(line), pos,
                        szFromName, 16);
                    pos = append_bytes(line, sizeof(line), pos, " At: ");
                    if (pos < sizeof(line))
                    {
                        mux_sprintf(line + pos, sizeof(line) - pos, T("%s %s"),
                            time,
                            is_connected_visible(mp->from, player)
                                ? T("Conn") : T(" "));
                    }
                    m_pINotify->RawNotify(player, line);
                }
            }
        }
    }
    m_pINotify->RawNotify(player, MOD_DASH_LINE);
}

// ---------------------------------------------------------------------------
// @mail/read — read a mail message.
// ---------------------------------------------------------------------------

void CMailMod::do_mail_read(dbref player, const UTF8 *arg1,
    const UTF8 *arg2)
{
    const UTF8 *msglist;
    int folder = player_folder(player);

    if (nullptr == arg2 || '\0' == *arg2)
    {
        msglist = arg1;
    }
    else
    {
        if (nullptr != arg1 && isdigit(*arg1))
        {
            int f = atoi(reinterpret_cast<const char *>(arg1));
            if (f >= 0 && f <= MAX_FOLDERS)
            {
                folder = f;
            }
        }
        msglist = arg2;
    }

    struct mail_selector ms;
    if (!parse_msglist(msglist, &ms, player))
    {
        return;
    }

    int i = 0, j = 0;

    std::list<mail> *pList = MailList(player);
    if (pList)
    {
        for (auto &m : *pList)
        {
            struct mail *mp = &m;
            if (Folder(mp) == folder)
            {
                i++;
                if (mail_match(mp, ms, i))
                {
                    j++;
                    const UTF8 *body = MessageFetch(mp->number);

                    m_pINotify->RawNotify(player, MOD_DASH_LINE);

                    UTF8 szFromName[64];
                    get_player_name(mp->from, szFromName, sizeof(szFromName));

                    UTF8 hdr[MOD_LBUF_SIZE];
                    format_mail_detail_header(hdr, sizeof(hdr),
                        i, szFromName,
                        reinterpret_cast<const UTF8 *>(mp->time.c_str()),
                        is_connected_visible(mp->from, player),
                        folder,
                        Read(mp) ? T("Read") : T("Unread"),
                        Cleared(mp)  ? " Cleared" : "",
                        Urgent(mp)   ? " Urgent"  : "",
                        Mass(mp)     ? " Mass"    : "",
                        Forward(mp)  ? " Forward" : "",
                        M_Safe(mp)   ? " Safe"    : "",
                        Tagged(mp)   ? " Tagged"  : "",
                        reinterpret_cast<const UTF8 *>(mp->subject.c_str()),
                        0);
                    m_pINotify->RawNotify(player, hdr);
                    m_pINotify->RawNotify(player, MOD_DASH_LINE);
                    m_pINotify->RawNotify(player, body);
                    m_pINotify->RawNotify(player, MOD_DASH_LINE);

                    if (Unread(mp))
                    {
                        mp->read |= M_ISREAD;
                        sqlite_wt_update_mail_flags(mp);
                    }
                }
            }
        }
    }

    if (!j)
    {
        m_pINotify->RawNotify(player,
            T("MAIL: You don’t have that many matching messages!"));
    }
}

// ---------------------------------------------------------------------------
// @mail/next — read the next unread message.
// ---------------------------------------------------------------------------

void CMailMod::do_mail_next(dbref player)
{
    int folder = player_folder(player);
    int i = 0;

    std::list<mail> *pList = MailList(player);
    if (pList)
    {
        for (auto &m : *pList)
        {
            struct mail *mp = &m;
            if (Folder(mp) == folder)
            {
                i++;
                if (Unread(mp))
                {
                    const UTF8 *body = MessageFetch(mp->number);

                    m_pINotify->RawNotify(player, MOD_DASH_LINE);

                    UTF8 szFromName[64];
                    get_player_name(mp->from, szFromName, sizeof(szFromName));

                    UTF8 hdr[MOD_LBUF_SIZE];
                    format_mail_detail_header(hdr, sizeof(hdr),
                        i, szFromName,
                        reinterpret_cast<const UTF8 *>(mp->time.c_str()),
                        is_connected_visible(mp->from, player),
                        folder,
                        T("Unread"),
                        Urgent(mp)   ? " Urgent"  : "",
                        Mass(mp)     ? " Mass"    : "",
                        Forward(mp)  ? " Forward" : "",
                        M_Safe(mp)   ? " Safe"    : "",
                        Tagged(mp)   ? " Tagged"  : "",
                        "",
                        reinterpret_cast<const UTF8 *>(mp->subject.c_str()),
                        0);
                    m_pINotify->RawNotify(player, hdr);
                    m_pINotify->RawNotify(player, MOD_DASH_LINE);
                    m_pINotify->RawNotify(player, body);
                    m_pINotify->RawNotify(player, MOD_DASH_LINE);

                    mp->read |= M_ISREAD;
                    sqlite_wt_update_mail_flags(mp);
                    return;
                }
            }
        }
    }

    m_pINotify->RawNotify(player,
        T("MAIL: You have no unread messages in that folder."));
}

void CMailMod::do_mail_purge(dbref player)
{
    std::list<mail> *pList = MailList(player);
    if (pList)
    {
        for (auto it = pList->begin(); it != pList->end(); )
        {
            struct mail *mp = &*it;
            if (Cleared(mp))
            {
                sqlite_wt_delete_mail(mp);
                MessageReferenceDec(mp->number);
                it = pList->erase(it);
            }
            else
            {
                ++it;
            }
        }
        if (pList->empty()) m_mail_htab.erase(player);
    }
    m_pINotify->RawNotify(player, T("MAIL: Mailbox purged."));
}

// ---------------------------------------------------------------------------
// @mail/file — move messages to a folder.
// ---------------------------------------------------------------------------

void CMailMod::do_mail_file(dbref player, const UTF8 *msglist,
    const UTF8 *folder)
{
    struct mail_selector ms;
    if (!parse_msglist(msglist, &ms, player))
    {
        return;
    }

    // Parse folder number.
    //
    int foldernum = -1;
    if (nullptr != folder && isdigit(*folder))
    {
        foldernum = atoi(reinterpret_cast<const char *>(folder));
        if (foldernum < 0 || foldernum > MAX_FOLDERS)
        {
            foldernum = -1;
        }
    }
    if (-1 == foldernum)
    {
        m_pINotify->RawNotify(player,
            T("MAIL: Invalid folder specification"));
        return;
    }

    int i = 0, j = 0;
    int origfold = player_folder(player);

    std::list<mail> *pList = MailList(player);
    if (pList)
    {
        for (auto &m : *pList)
        {
            struct mail *mp = &m;
            if (All(ms) || (Folder(mp) == origfold))
            {
                i++;
                if (mail_match(mp, ms, i))
                {
                    j++;
                    mp->read &= M_FMASK;
                    mp->read |= FolderBit(foldernum);
                    sqlite_wt_update_mail_flags(mp);

                    UTF8 msg[128];
                    mux_sprintf(msg, sizeof(msg), T("MAIL: Msg %d filed in folder %d"), i, foldernum);
                    m_pINotify->RawNotify(player, msg);
                }
            }
        }
    }

    if (!j)
    {
        m_pINotify->RawNotify(player,
            T("MAIL: You don’t have any matching messages!"));
    }
}

// ---------------------------------------------------------------------------
// @mail/stats, @mail/dstats, @mail/fstats — mail statistics.
//
// #1631: this was one invented per-player summary served identically for all
// three switches, ignoring both the [<player>] argument and the wizard gate.
// The engine (modules/engine/mail.cpp do_mail_stats) is the oracle here: three
// cumulative detail levels, an optional target, and "no target from a wizard"
// meaning the whole spool rather than the caller.  Mirror it.
//
// One deliberate divergence remains: the engine charges mudconf.searchcost via
// payfor() before doing the work, and no module-visible interface exposes that
// value (DRIVER_CONFIG is driver/network config only).  payfor() exempts
// wizards outright, and every cross-player query is wizard-gated, so this is
// only reachable by a non-wizard running stats on themselves on a game with a
// nonzero searchcost.  Tracked separately rather than bent into an ABI change
// here.
// ---------------------------------------------------------------------------

void CMailMod::do_mail_stats(dbref player, const UTF8 *name, int full)
{
    int fc = 0, fr = 0, fu = 0, tc = 0, tr = 0, tu = 0, count = 0;
    size_t cchars = 0, fchars = 0, tchars = 0;
    UTF8 msg[MOD_LBUF_SIZE];

    if (nullptr == m_pIObjectInfo)
    {
        m_pINotify->RawNotify(player, M_("MAIL: Mail statistics are unavailable."));
        return;
    }

    bool bWizard = false;
    m_pIObjectInfo->IsWizard(player, &bWizard);

    // Resolve the target.  bAll stands in for the engine's AMBIGUOUS: no name
    // from a wizard means the whole spool rather than a particular player.
    //
    bool bAll = false;
    dbref target = NOTHING;
    if (  nullptr == name
       || '\0' == *name)
    {
        if (bWizard)
        {
            bAll = true;
        }
        else
        {
            target = player;
        }
    }
    else if (NUMBER_TOKEN == *name)
    {
        target = static_cast<dbref>(strtol(
            reinterpret_cast<const char *>(name) + 1, nullptr, 10));
        bool bPlayer = false;
        if (  MUX_FAILED(m_pIObjectInfo->IsPlayer(target, &bPlayer))
           || !bPlayer)
        {
            target = NOTHING;
        }
    }
    else if (0 == strcasecmp(reinterpret_cast<const char *>(name), "me"))
    {
        target = player;
    }
    else
    {
        m_pIObjectInfo->LookupPlayer(player, name, true, &target);
    }

    if (  !bAll
       && NOTHING == target)
    {
        m_pIObjectInfo->MatchThing(player, name, &target);
    }
    if (  !bAll
       && NOTHING == target)
    {
        mux_sprintf(msg, sizeof(msg), M_("%s: No such player."),
                    (nullptr != name) ? name : T(""));
        m_pINotify->RawNotify(player, msg);
        return;
    }
    if (  !bWizard
       && target != player)
    {
        m_pINotify->RawNotify(player, M_("The post office protects privacy!"));
        return;
    }

    if (bAll)
    {
        // Stats for all.
        //
        if (0 == full)
        {
            for (auto &it : m_mail_htab)
            {
                count += static_cast<int>(it.second.size());
            }
            mux_sprintf(msg, sizeof(msg),
                MN_("There is %d message in the mail spool.",
                    "There are %d messages in the mail spool.", count),
                count);
            m_pINotify->RawNotify(player, msg);
            return;
        }

        for (auto &it : m_mail_htab)
        {
            for (auto &m : it.second)
            {
                struct mail *mp = &m;
                // No +1: MessageFetchSize is std::string::size() and does
                // not include a NUL.  The engine dropped the same phantom
                // in #1639; the module's stats path still had it, so
                // @mail/fstats disagreed with list sizes and with the
                // engine by one byte per message (#1652).
                //
                if (Cleared(mp))
                {
                    fc++;
                    cchars += MessageFetchSize(mp->number);
                }
                else if (Read(mp))
                {
                    fr++;
                    fchars += MessageFetchSize(mp->number);
                }
                else
                {
                    fu++;
                    tchars += MessageFetchSize(mp->number);
                }
            }
        }

        if (1 == full)
        {
            mux_sprintf(msg, sizeof(msg),
                MN_("MAIL: There is %d msg in the mail spool, %d unread, %d cleared.",
                    "MAIL: There are %d msgs in the mail spool, %d unread, %d cleared.",
                    fc + fr + fu),
                fc + fr + fu, fu, fc);
            m_pINotify->RawNotify(player, msg);
            return;
        }

        mux_sprintf(msg, sizeof(msg),
            MN_("MAIL: There is %d old msg in the mail spool, totalling %d characters.",
                "MAIL: There are %d old msgs in the mail spool, totalling %d characters.",
                fr),
            fr, static_cast<int>(fchars));
        m_pINotify->RawNotify(player, msg);
        mux_sprintf(msg, sizeof(msg),
            MN_("MAIL: There is %d new msg in the mail spool, totalling %d characters.",
                "MAIL: There are %d new msgs in the mail spool, totalling %d characters.",
                fu),
            fu, static_cast<int>(tchars));
        m_pINotify->RawNotify(player, msg);
        mux_sprintf(msg, sizeof(msg),
            MN_("MAIL: There is %d cleared msg in the mail spool, totalling %d characters.",
                "MAIL: There are %d cleared msgs in the mail spool, totalling %d characters.",
                fc),
            fc, static_cast<int>(cchars));
        m_pINotify->RawNotify(player, msg);
        return;
    }

    // Individual stats.  Mail *to* the target lives in the target's own list,
    // but the "sent by target" side needs the full sweep.
    //
    const UTF8 *pMoniker = nullptr;
    m_pIObjectInfo->GetMoniker(target, &pMoniker);
    if (nullptr == pMoniker)
    {
        pMoniker = T("");
    }

    if (0 == full)
    {
        for (auto &it : m_mail_htab)
        {
            for (auto &m : it.second)
            {
                if (m.from == target)
                {
                    fr++;
                }
                if (m.to == target)
                {
                    tr++;
                }
            }
        }
        mux_sprintf(msg, sizeof(msg),
            MN_("%s sent %d message.", "%s sent %d messages.", fr),
            pMoniker, fr);
        m_pINotify->RawNotify(player, msg);
        mux_sprintf(msg, sizeof(msg),
            MN_("%s has %d message.", "%s has %d messages.", tr),
            pMoniker, tr);
        m_pINotify->RawNotify(player, msg);
        return;
    }

    // More detailed message count.
    //
    UTF8 last[50];
    last[0] = '\0';
    for (auto &it : m_mail_htab)
    {
        for (auto &m : it.second)
        {
            struct mail *mp = &m;
            if (mp->from == target)
            {
                if (Cleared(mp))
                {
                    fc++;
                }
                else if (Read(mp))
                {
                    fr++;
                }
                else
                {
                    fu++;
                }
                if (2 == full)
                {
                    fchars += MessageFetchSize(mp->number);
                }
            }
            if (mp->to == target)
            {
                if (  !tr
                   && !tu)
                {
                    mux_strncpy(last, reinterpret_cast<const UTF8 *>(
                        mp->time.c_str()), sizeof(last) - 1);
                }
                if (Cleared(mp))
                {
                    tc++;
                }
                else if (Read(mp))
                {
                    tr++;
                }
                else
                {
                    tu++;
                }
                if (2 == full)
                {
                    tchars += MessageFetchSize(mp->number);
                }
            }
        }
    }

    mux_sprintf(msg, sizeof(msg), M_("Mail statistics for %s:"), pMoniker);
    m_pINotify->RawNotify(player, msg);

    if (1 == full)
    {
        mux_sprintf(msg, sizeof(msg),
            MN_("%d message sent, %d unread, %d cleared.",
                "%d messages sent, %d unread, %d cleared.",
                fr + fu + fc),
            fc + fr + fu, fu, fc);
        m_pINotify->RawNotify(player, msg);
        mux_sprintf(msg, sizeof(msg),
            MN_("%d message received, %d unread, %d cleared.",
                "%d messages received, %d unread, %d cleared.",
                tr + tu + tc),
            tc + tr + tu, tu, tc);
        m_pINotify->RawNotify(player, msg);
    }
    else
    {
        mux_sprintf(msg, sizeof(msg),
            MN_("%d message sent, %d unread, %d cleared, totalling %d characters.",
                "%d messages sent, %d unread, %d cleared, totalling %d characters.",
                fr + fu + fc),
            fc + fr + fu, fu, fc, static_cast<int>(fchars));
        m_pINotify->RawNotify(player, msg);
        mux_sprintf(msg, sizeof(msg),
            MN_("%d message received, %d unread, %d cleared, totalling %d characters.",
                "%d messages received, %d unread, %d cleared, totalling %d characters.",
                tr + tu + tc),
            tc + tr + tu, tu, tc, static_cast<int>(tchars));
        m_pINotify->RawNotify(player, msg);
    }

    if (0 < tc + tr + tu)
    {
        mux_sprintf(msg, sizeof(msg), M_("Last is dated %s"), last);
        m_pINotify->RawNotify(player, msg);
    }
}

// ---------------------------------------------------------------------------
// Default @mail handler — routes to list, read, clear, purge, or send.
// Returns true if handled, false if sending (unimplemented).
// ---------------------------------------------------------------------------

bool CMailMod::do_mail_stub(dbref player, const UTF8 *arg1,
    const UTF8 *arg2)
{
    if (nullptr == arg1 || '\0' == *arg1)
    {
        if (arg2 && *arg2)
        {
            m_pINotify->RawNotify(player,
                T("MAIL: Invalid mail command."));
            return true;
        }
        do_mail_list(player, arg1, nullptr);
        return true;
    }

    if (0 == strcasecmp(reinterpret_cast<const char *>(arg1), "purge"))
    {
        do_mail_purge(player);
        return true;
    }

    if (0 == strcasecmp(reinterpret_cast<const char *>(arg1), "clear"))
    {
        do_mail_flags(player, arg2, M_CLEARED, false);
        return true;
    }

    if (0 == strcasecmp(reinterpret_cast<const char *>(arg1), "unclear"))
    {
        do_mail_flags(player, arg2, M_CLEARED, true);
        return true;
    }

    if (nullptr != arg2 && '\0' != *arg2)
    {
        // #1198: composition start lives in the module store — do not
        // return NOTIMPLEMENTED and let the engine fall through.
        //
        do_expmail_start(player, arg1, arg2);
        return true;
    }

    // Must be reading or listing mail.
    //
    if (isdigit(*arg1) && nullptr == strchr(
            reinterpret_cast<const char *>(arg1), '-'))
    {
        do_mail_read(player, arg1, nullptr);
    }
    else
    {
        do_mail_list(player, arg1, nullptr);
    }
    return true;
}

// ---------------------------------------------------------------------------
// mux_IMailControl implementation.
// ---------------------------------------------------------------------------

MUX_RESULT CMailMod::PlayerConnect(dbref player)
{
    CheckMail(player, player_folder(player), false);
    return MUX_S_OK;
}

MUX_RESULT CMailMod::PlayerNuke(dbref player)
{
    return DestroyPlayerMail(player);
}

MUX_RESULT CMailMod::MailCommand(dbref executor, int key,
    const UTF8 *pArg1, const UTF8 *pArg2)
{
    switch (key & ~MAIL_QUOTE)
    {
    case MAIL_CLEAR:
        do_mail_flags(executor, pArg1, M_CLEARED, false);
        return MUX_S_OK;

    case MAIL_UNCLEAR:
        do_mail_flags(executor, pArg1, M_CLEARED, true);
        return MUX_S_OK;

    case MAIL_TAG:
        do_mail_flags(executor, pArg1, M_TAG, false);
        return MUX_S_OK;

    case MAIL_UNTAG:
        do_mail_flags(executor, pArg1, M_TAG, true);
        return MUX_S_OK;

    case MAIL_SAFE:
        do_mail_flags(executor, pArg1, M_SAFE, false);
        return MUX_S_OK;

    case MAIL_UNSAFE:
        do_mail_flags(executor, pArg1, M_SAFE, true);
        return MUX_S_OK;

    case MAIL_PURGE:
        do_mail_purge(executor);
        return MUX_S_OK;

    case MAIL_LIST:
        do_mail_list(executor, pArg1, pArg2);
        return MUX_S_OK;

    case MAIL_READ:
        do_mail_read(executor, pArg1, pArg2);
        return MUX_S_OK;

    case MAIL_NEXT:
        do_mail_next(executor);
        return MUX_S_OK;

    case MAIL_FILE:
        do_mail_file(executor, pArg1, pArg2);
        return MUX_S_OK;

    case MAIL_STATS:
        do_mail_stats(executor, pArg1, 0);
        return MUX_S_OK;

    case MAIL_DSTATS:
        do_mail_stats(executor, pArg1, 1);
        return MUX_S_OK;

    case MAIL_FSTATS:
        do_mail_stats(executor, pArg1, 2);
        return MUX_S_OK;

    case MAIL_REVIEW:
        do_mail_review(executor, pArg1, pArg2);
        return MUX_S_OK;

    case MAIL_FOLDER:
        do_mail_change_folder(executor, pArg1, pArg2);
        return MUX_S_OK;

    case MAIL_RETRACT:
        do_mail_retract(executor, pArg1, pArg2);
        return MUX_S_OK;

    case MAIL_NUKE:
        do_mail_nuke(executor);
        return MUX_S_OK;

    case MAIL_SEND:
        do_expmail_stop(executor, 0);
        return MUX_S_OK;

    case MAIL_QUICK:
        do_mail_quick(executor, pArg1, pArg2);
        return MUX_S_OK;

    case MAIL_CC:
        // #1196: composition add, not one-shot send.
        //
        do_mail_cc(executor, pArg1, false);
        return MUX_S_OK;

    case MAIL_BCC:
        // #1196: composition add with blind numlist prefix.
        //
        do_mail_cc(executor, pArg1, true);
        return MUX_S_OK;

    case MAIL_PROOF:
        do_mail_proof(executor);
        return MUX_S_OK;

    case MAIL_ABORT:
        do_expmail_abort(executor);
        return MUX_S_OK;

    case MAIL_FORWARD:
        do_mail_fwd(executor, pArg1, pArg2);
        return MUX_S_OK;

    case MAIL_REPLY:
        do_mail_reply(executor, pArg1, false, key);
        return MUX_S_OK;

    case MAIL_REPLYALL:
        do_mail_reply(executor, pArg1, true, key);
        return MUX_S_OK;

    case MAIL_URGENT:
        do_expmail_stop(executor, M_URGENT);
        return MUX_S_OK;

    case MAIL_EDIT:
        do_edit_msg(executor, pArg1, pArg2);
        return MUX_S_OK;

    case 0:
        // Default @mail (no switch).
        //
        if (do_mail_stub(executor, pArg1, pArg2))
        {
            return MUX_S_OK;
        }
        return MUX_E_NOTIMPLEMENTED;

    default:
        return MUX_E_NOTIMPLEMENTED;
    }
}

MUX_RESULT CMailMod::MaliasCommand(dbref executor, int key,
    const UTF8 *pArg1, const UTF8 *pArg2)
{
    switch (key)
    {
    case 0:
        do_malias_switch(executor, pArg1, pArg2);
        return MUX_S_OK;

    case MALIAS_DESC:
        do_malias_desc(executor, pArg1, pArg2);
        return MUX_S_OK;

    case MALIAS_CHOWN:
        do_malias_chown(executor, pArg1, pArg2);
        return MUX_S_OK;

    case MALIAS_ADD:
        do_malias_add(executor, pArg1, pArg2);
        return MUX_S_OK;

    case MALIAS_REMOVE:
        do_malias_remove(executor, pArg1, pArg2);
        return MUX_S_OK;

    case MALIAS_DELETE:
        do_malias_delete(executor, pArg1);
        return MUX_S_OK;

    case MALIAS_RENAME:
        do_malias_rename(executor, pArg1, pArg2);
        return MUX_S_OK;

    case MALIAS_LIST:
        do_malias_adminlist(executor);
        return MUX_S_OK;

    case MALIAS_STATUS:
        do_malias_status(executor);
        return MUX_S_OK;

    default:
        return MUX_E_NOTIMPLEMENTED;
    }
}

MUX_RESULT CMailMod::FolderCommand(dbref executor, int key, int nargs,
    const UTF8 *pArg1, const UTF8 *pArg2)
{
    switch (key)
    {
    case FOLDER_FILE:
        do_mail_file(executor, pArg1, pArg2);
        return MUX_S_OK;

    case FOLDER_LIST:
        ListMailInFolder(executor, pArg1, pArg2);
        return MUX_S_OK;

    case FOLDER_READ:
        do_mail_read(executor, pArg1, pArg2);
        return MUX_S_OK;

    case FOLDER_SET:
        do_mail_change_folder(executor, pArg1, pArg2);
        return MUX_S_OK;

    default:
        if (nullptr == pArg1 || '\0' == *pArg1)
        {
            DoListMailBrief(executor);
        }
        else if (2 == nargs)
        {
            do_mail_read(executor, pArg1, pArg2);
        }
        else
        {
            do_mail_change_folder(executor, pArg1, pArg2);
        }
        return MUX_S_OK;
    }
}

MUX_RESULT CMailMod::CheckMail(dbref player, int folder, bool silent)
{
    int rc, uc, cc, gc;
    count_mail_internal(player, folder, &rc, &uc, &cc);
    urgent_mail_internal(player, folder, &gc);

    if (rc + uc > 0)
    {
        UTF8 msg[256];
        mux_sprintf(msg, sizeof(msg), T("MAIL: %d messages in folder %d [%s] (%d unread, %d cleared)."),
                 rc + uc, folder,
                 reinterpret_cast<const char *>(get_folder_name(player, folder)),
                 uc, cc);
        m_pINotify->RawNotify(player, msg);
    }
    else if (!silent)
    {
        m_pINotify->RawNotify(player,
            T("\r\nMAIL: You have no mail.\r\n"));
    }
    if (gc > 0)
    {
        UTF8 msg[256];
        mux_sprintf(msg, sizeof(msg), T("URGENT MAIL: You have %d urgent messages in folder %d [%s]."),
                 gc, folder,
                 reinterpret_cast<const char *>(get_folder_name(player, folder)));
        m_pINotify->RawNotify(player, msg);
    }
    return MUX_S_OK;
}

MUX_RESULT CMailMod::ExpireMail(void)
{
    // #1196: align with engine check_mail_expiration — negative/zero never
    // expire; age in seconds; skip M_Safe; expire unread; wizard exempt.
    // (Engine also exempts No_Mail_Expire power holders; COM exposes only
    // IsWizard today.)
    //
    if (m_mail_expiration <= 0)
    {
        return MUX_S_OK;
    }

    CLinearTimeAbsolute ltaNow;
    ltaNow.GetLocal();
    const int expire_secs = m_mail_expiration * 86400;

    for (auto &kv : m_mail_htab)
    {
        dbref player = kv.first;

        bool bWizard = false;
        if (nullptr != m_pIObjectInfo)
        {
            m_pIObjectInfo->IsWizard(player, &bWizard);
        }
        if (bWizard)
        {
            continue;
        }

        auto &mlist = kv.second;
        for (auto it = mlist.begin(); it != mlist.end(); )
        {
            // @mail/safe messages never expire.
            //
            if (it->read & M_SAFE)
            {
                ++it;
                continue;
            }

            CLinearTimeAbsolute ltaMail;
            if (!ltaMail.SetString(reinterpret_cast<const UTF8 *>(it->time.c_str())))
            {
                ++it;
                continue;
            }

            CLinearTimeDelta ltd(ltaMail, ltaNow);
            if (ltd.ReturnSeconds() <= expire_secs)
            {
                ++it;
                continue;
            }

            MessageReferenceDec(it->number);
            sqlite_wt_delete_mail(&(*it));
            it = mlist.erase(it);
        }
    }

    return MUX_S_OK;
}

MUX_RESULT CMailMod::CountMail(dbref player, int folder,
    int *pRead, int *pUnread, int *pCleared)
{
    count_mail_internal(player, folder, pRead, pUnread, pCleared);
    return MUX_S_OK;
}

MUX_RESULT CMailMod::DestroyPlayerMail(dbref player)
{
    // Step 1: Purge received mail.
    //
    MailListRemoveAll(player);
    sqlite_wt_delete_all_mail(player);

    // Step 2: Orphan sent mail in every other player's mailbox.
    //
    for (auto &kv : m_mail_htab)
    {
        for (auto &m : kv.second)
        {
            if (m.from == player)
            {
                m.from = NOTHING;
                sqlite_wt_update_mail_flags(&m);
            }
        }
    }

    // Step 3: Clean up mail aliases.
    //
    for (size_t i = 0; i < m_malias.size(); i++)
    {
        if (m_malias[i])
        {
            m_malias[i]->list.erase(
                std::remove(m_malias[i]->list.begin(),
                            m_malias[i]->list.end(), player),
                m_malias[i]->list.end());
        }
    }
    sqlite_wt_sync_all_aliases();

    return MUX_S_OK;
}

MUX_RESULT CMailMod::GetRevision(int *pRev)
{
    if (nullptr == pRev)
    {
        return MUX_E_INVALIDARG;
    }
    *pRev = m_revision;
    return MUX_S_OK;
}

// Softcode mailsend() entry — mirrors engine do_mail_send_softcode against
// the module-owned store (#1191).
//
MUX_RESULT CMailMod::SoftcodeSend(dbref player, const UTF8 *recipients,
    const UTF8 *subject, const UTF8 *message, const UTF8 **ppError)
{
    if (nullptr == ppError)
    {
        return MUX_E_INVALIDARG;
    }
    *ppError = nullptr;

    if (player < 0)
    {
        *ppError = S_("#-1 NOT A PLAYER");
        return MUX_E_INVALIDARG;
    }
    if (nullptr == recipients || '\0' == recipients[0])
    {
        *ppError = S_("#-1 NO RECIPIENTS");
        return MUX_E_INVALIDARG;
    }
    if (nullptr == subject || '\0' == subject[0])
    {
        *ppError = S_("#-1 NO SUBJECT");
        return MUX_E_INVALIDARG;
    }
    if (nullptr == message || '\0' == message[0])
    {
        *ppError = S_("#-1 NO MESSAGE");
        return MUX_E_INVALIDARG;
    }

    // Resolve owner if needed — softcode may run on a non-player object.
    // IMailDelivery / ObjectInfo tell us if player is a player.
    //
    bool bIsPlayer = false;
    if (nullptr != m_pIObjectInfo)
    {
        m_pIObjectInfo->IsPlayer(player, &bIsPlayer);
    }
    if (!bIsPlayer && nullptr != m_pIObjectInfo)
    {
        dbref owner = -1;
        m_pIObjectInfo->GetOwner(player, &owner);
        player = owner;
        if (nullptr != m_pIObjectInfo)
        {
            m_pIObjectInfo->IsPlayer(player, &bIsPlayer);
        }
        if (!bIsPlayer)
        {
            *ppError = S_("#-1 NOT A PLAYER");
            return MUX_E_INVALIDARG;
        }
    }

    if (nullptr != m_pIMailDelivery)
    {
        bool bComposing = false;
        m_pIMailDelivery->IsComposing(player, &bComposing);
        if (bComposing)
        {
            *ppError = S_("#-1 MAIL ALREADY IN PROGRESS");
            return MUX_E_FAIL;
        }
        bool bWiz = false;
        if (nullptr != m_pIPermissions)
        {
            m_pIPermissions->IsWizard(player, &bWiz);
        }
        if (!bWiz)
        {
            bool bThrottled = false;
            m_pIMailDelivery->ThrottleCheck(player, &bThrottled);
            if (bThrottled)
            {
                *ppError = S_("#-1 TOO MUCH MAIL SENT");
                return MUX_E_FAIL;
            }
        }
    }

    std::string numlist = make_numlist(player, recipients, false);
    if (numlist.empty())
    {
        *ppError = S_("#-1 NO VALID RECIPIENTS");
        return MUX_E_FAIL;
    }

    // mail_to_list takes ownership of a malloc'd buffer.
    //
    size_t nLen = numlist.size() + 1;
    UTF8 *numlist_copy = static_cast<UTF8 *>(malloc(nLen));
    if (nullptr == numlist_copy)
    {
        *ppError = S_("#-1 OUT OF MEMORY");
        return MUX_E_OUTOFMEMORY;
    }
    memcpy(numlist_copy, numlist.c_str(), nLen);

    mail_to_list(player, numlist_copy, subject, message, 0, true);
    return MUX_S_OK;
}

// ---------------------------------------------------------------------------
// mux_IServerEventsSink implementation.
// ---------------------------------------------------------------------------

void CMailMod::startup(void)
{
    if (nullptr != m_pILog)
    {
        bool fStarted;
        MUX_RESULT mr = m_pILog->start_log(&fStarted, LOG_ALWAYS,
                                            T("MAIL"), T("INFO"));
        if (MUX_SUCCEEDED(mr) && fStarted)
        {
            m_pILog->log_text(T("Mail module: startup event."));
            m_pILog->end_log();
        }
    }
}

void CMailMod::presync_database(void)
{
    // Nothing needed — SQLite write-through handles persistence.
}

void CMailMod::presync_database_sigsegv(void)
{
    // Nothing.
}

void CMailMod::dump_database(int dump_type)
{
    UNUSED_PARAMETER(dump_type);
    // Nothing needed — SQLite WAL checkpoint handles durability.
}

void CMailMod::dump_complete_signal(void)
{
    // Nothing.
}

void CMailMod::shutdown(void)
{
    // Release storage interface. Null the member field *before* calling
    // Release() so that any concurrent caller which re-reads m_pIStorage
    // during the release (in a future multi-threaded evaluator) sees the
    // null and bails, instead of racing with the decrement and possibly
    // triggering a double Release.
    //
    mux_IMailStorage *pStorage = m_pIStorage;
    m_pIStorage = nullptr;
    if (nullptr != pStorage)
    {
        pStorage->Release();
    }

    if (nullptr != m_pILog)
    {
        bool fStarted;
        MUX_RESULT mr = m_pILog->start_log(&fStarted, LOG_ALWAYS,
                                            T("MAIL"), T("INFO"));
        if (MUX_SUCCEEDED(mr) && fStarted)
        {
            m_pILog->log_text(T("Mail module: shutdown event."));
            m_pILog->end_log();
        }
    }
}

void CMailMod::dbck(void)
{
    // Mail consistency check: remove mail referencing destroyed players.
    //
    if (nullptr == m_pIObjectInfo)
    {
        return;
    }

    for (auto it = m_mail_htab.begin(); it != m_mail_htab.end(); )
    {
        bool bValid = false;
        m_pIObjectInfo->IsValid(it->first, &bValid);
        if (!bValid)
        {
            // Recipient destroyed — purge their mail.
            //
            for (auto &m : it->second)
            {
                MessageReferenceDec(m.number);
            }
            sqlite_wt_delete_all_mail(it->first);
            it = m_mail_htab.erase(it);
        }
        else
        {
            // Check for mail from destroyed senders — mark as NOTHING.
            //
            for (auto &m : it->second)
            {
                if (m.from != NOTHING)
                {
                    bool bFromValid = false;
                    m_pIObjectInfo->IsValid(m.from, &bFromValid);
                    if (!bFromValid)
                    {
                        m.from = NOTHING;
                        sqlite_wt_update_mail_flags(&m);
                    }
                }
            }
            ++it;
        }
    }
}

void CMailMod::connect(dbref player, int isnew, int num)
{
    UNUSED_PARAMETER(isnew);
    UNUSED_PARAMETER(num);
    PlayerConnect(player);
}

void CMailMod::disconnect(dbref player, int num)
{
    UNUSED_PARAMETER(player);
    UNUSED_PARAMETER(num);
}

void CMailMod::data_create(dbref object)
{
    UNUSED_PARAMETER(object);
}

void CMailMod::data_clone(dbref clone, dbref source)
{
    UNUSED_PARAMETER(clone);
    UNUSED_PARAMETER(source);
}

void CMailMod::data_free(dbref object)
{
    // When a player is destroyed, purge their mail and clean up
    // references from other players' mailboxes.
    //
    DestroyPlayerMail(object);
}

// ---------------------------------------------------------------------------
// CMailModFactory — boilerplate.
// ---------------------------------------------------------------------------

CMailModFactory::CMailModFactory(void) : m_cRef(1)
{
}

CMailModFactory::~CMailModFactory()
{
}

MUX_RESULT CMailModFactory::QueryInterface(MUX_IID iid, void **ppv)
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

uint32_t CMailModFactory::AddRef(void)
{
    return m_cRef.fetch_add(1, std::memory_order_relaxed) + 1;
}

uint32_t CMailModFactory::Release(void)
{
    uint32_t prev = m_cRef.fetch_sub(1, std::memory_order_acq_rel);
    if (1 == prev)
    {
        delete this;
        return 0;
    }
    return prev - 1;
}

MUX_RESULT CMailModFactory::CreateInstance(mux_IUnknown *pUnknownOuter,
    MUX_IID iid, void **ppv)
{
    UNUSED_PARAMETER(pUnknownOuter);

    CMailMod *pMailMod = nullptr;
    try
    {
        pMailMod = new CMailMod;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == pMailMod)
    {
        return MUX_E_OUTOFMEMORY;
    }

    MUX_RESULT mr = pMailMod->FinalConstruct();
    if (MUX_FAILED(mr))
    {
        pMailMod->Release();
        return mr;
    }

    mr = pMailMod->QueryInterface(iid, ppv);
    pMailMod->Release();
    return mr;
}

MUX_RESULT CMailModFactory::LockServer(bool bLock)
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
