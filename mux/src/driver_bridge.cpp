/*! \file driver_bridge.cpp
 * \brief Driver-side bridges to engine COM interfaces.
 *
 * These functions provide the same signatures that driver files have
 * always called, but delegate through COM interfaces into the engine.
 * Engine files have their own implementations; these are for netmux
 * (driver) only.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"
#include "modules.h"
#include "driverstate.h"

// String constants also defined in match.cpp (engine).  The driver
// needs its own copies since engine.so doesn't export them.
//
const UTF8 *FUNC_FAIL_MESSAGE = T("#-1");
const UTF8 *FUNC_NOPERM_MESSAGE = T("#-1 PERMISSION DENIED");

// notify_check — driver-side bridge through mux_INotify.
//
void notify_check(dbref target, dbref sender, const UTF8 *msg, int key)
{
    if (g_pINotify)
    {
        g_pINotify->NotifyCheck(target, sender, msg, key);
    }
}

// Object accessors — driver-side bridges through mux_IObjectInfo.
//
// These use drv_ prefix to avoid collision with db.h macros (Flags,
// Location, etc.) which expand to db[] access.  Driver call sites
// are updated to use these instead of the macros.
//

unsigned int drv_Flags(dbref obj, int word)
{
    unsigned int flags = 0;
    if (g_pIObjectInfo)
    {
        g_pIObjectInfo->GetFlags(obj, word, &flags);
    }
    return flags;
}

void drv_s_Flags(dbref obj, int word, unsigned int flags)
{
    if (g_pIObjectInfo)
    {
        g_pIObjectInfo->SetFlags(obj, word, flags);
    }
}

UTF8 *drv_decode_flags(dbref player, dbref obj)
{
    UTF8 *pStr = nullptr;
    if (g_pIObjectInfo)
    {
        g_pIObjectInfo->DecodeFlags(player, obj, &pStr);
    }
    return pStr;
}

int drv_Pennies(dbref obj)
{
    int pennies = 0;
    if (g_pIObjectInfo)
    {
        g_pIObjectInfo->GetPennies(obj, &pennies);
    }
    return pennies;
}

const UTF8 *drv_PureName(dbref obj)
{
    const UTF8 *pName = nullptr;
    if (g_pIObjectInfo)
    {
        g_pIObjectInfo->GetPureName(obj, &pName);
    }
    return pName ? pName : T("");
}

dbref drv_Location(dbref obj)
{
    dbref loc = NOTHING;
    if (g_pIObjectInfo)
    {
        g_pIObjectInfo->GetLocation(obj, &loc);
    }
    return loc;
}

const UTF8 *drv_Name(dbref obj)
{
    const UTF8 *pName = nullptr;
    if (g_pIObjectInfo)
    {
        g_pIObjectInfo->GetName(obj, &pName);
    }
    return pName ? pName : T("");
}

const UTF8 *drv_Moniker(dbref obj)
{
    const UTF8 *pName = nullptr;
    if (g_pIObjectInfo)
    {
        g_pIObjectInfo->GetMoniker(obj, &pName);
    }
    return pName ? pName : T("");
}

unsigned int drv_Powers(dbref obj)
{
    unsigned int powers = 0;
    if (g_pIObjectInfo)
    {
        g_pIObjectInfo->GetPowers(obj, &powers);
    }
    return powers;
}

dbref drv_Owner(dbref obj)
{
    dbref owner = NOTHING;
    if (g_pIObjectInfo)
    {
        g_pIObjectInfo->GetOwner(obj, &owner);
    }
    return owner;
}

bool drv_Good_obj(dbref obj)
{
    bool valid = false;
    if (g_pIObjectInfo)
    {
        g_pIObjectInfo->IsValid(obj, &valid);
    }
    return valid;
}

bool drv_Connected(dbref obj)
{
    bool connected = false;
    if (g_pIObjectInfo)
    {
        g_pIObjectInfo->IsConnected(obj, &connected);
    }
    return connected;
}

// Compound permission queries — delegate through mux_IObjectInfo.
//

bool drv_Wizard(dbref obj)
{
    bool result = false;
    if (g_pIObjectInfo)
    {
        g_pIObjectInfo->IsWizard(obj, &result);
    }
    return result;
}

bool drv_WizRoy(dbref obj)
{
    bool result = false;
    if (g_pIObjectInfo)
    {
        g_pIObjectInfo->IsWizRoy(obj, &result);
    }
    return result;
}

bool drv_Can_Idle(dbref obj)
{
    bool result = false;
    if (g_pIObjectInfo)
    {
        g_pIObjectInfo->CanIdle(obj, &result);
    }
    return result;
}

bool drv_Wizard_Who(dbref obj)
{
    bool result = false;
    if (g_pIObjectInfo)
    {
        g_pIObjectInfo->WizardWho(obj, &result);
    }
    return result;
}

bool drv_See_Hidden(dbref obj)
{
    bool result = false;
    if (g_pIObjectInfo)
    {
        g_pIObjectInfo->SeeHidden(obj, &result);
    }
    return result;
}

// Attribute access — driver-side bridges through mux_IObjectInfo.
//

bool atr_add_raw(dbref obj, int attrnum, const UTF8 *value)
{
    if (g_pIObjectInfo)
    {
        return MUX_SUCCEEDED(g_pIObjectInfo->AtrAddRaw(obj, attrnum, value));
    }
    return false;
}

bool atr_clr(dbref obj, int attrnum)
{
    if (g_pIObjectInfo)
    {
        return MUX_SUCCEEDED(g_pIObjectInfo->AtrClr(obj, attrnum));
    }
    return false;
}

UTF8 *atr_get_real(const UTF8 *tag, dbref obj, int attrnum, dbref *pOwner,
    int *pFlags, const UTF8 *file, const int line)
{
    UNUSED_PARAMETER(file);
    UNUSED_PARAMETER(line);
    UTF8 *buf = alloc_lbuf(tag);
    buf[0] = '\0';
    if (g_pIObjectInfo)
    {
        g_pIObjectInfo->AtrGet(obj, attrnum, buf, LBUF_SIZE, pOwner, pFlags);
    }
    return buf;
}

UTF8 *atr_pget_real(dbref obj, int attrnum, dbref *pOwner, int *pFlags,
    const UTF8 *file, const int line)
{
    UNUSED_PARAMETER(file);
    UNUSED_PARAMETER(line);
    UTF8 *buf = alloc_lbuf("drv_atr_pget");
    buf[0] = '\0';
    if (g_pIObjectInfo)
    {
        g_pIObjectInfo->AtrPGet(obj, attrnum, buf, LBUF_SIZE, pOwner, pFlags);
    }
    return buf;
}

dbref lookup_player(dbref executor, UTF8 *pName, bool bConnected)
{
    dbref result = NOTHING;
    if (g_pIObjectInfo)
    {
        g_pIObjectInfo->LookupPlayer(executor, pName, bConnected, &result);
    }
    return result;
}

void fetch_ConnectionInfoFields(dbref player, long anFields[4])
{
    if (g_pIObjectInfo)
    {
        g_pIObjectInfo->FetchConnectionInfoFields(player, anFields);
    }
}

void put_ConnectionInfoFields(dbref player, long anFields[4],
    CLinearTimeAbsolute &ltaNow)
{
    if (g_pIObjectInfo)
    {
        g_pIObjectInfo->PutConnectionInfoFields(player, anFields, ltaNow);
    }
}

// File cache — driver-side bridge through mux_IPlayerSession.
//
void fcache_dump(DESC *d, int num)
{
    if (g_pIPlayerSession)
    {
        g_pIPlayerSession->FcacheSend(d, num);
    }
}

void fcache_rawdump(SOCKET fd, int num)
{
    if (g_pIPlayerSession)
    {
        g_pIPlayerSession->FcacheRawSend(fd, num);
    }
}

// Scheduler — driver-side bridges through mux_IGameEngine.
//
void drv_CancelTask(FTASK *fpTask, void *arg_voidptr, int arg_Integer)
{
    if (g_pIGameEngine)
    {
        g_pIGameEngine->CancelTask(fpTask, arg_voidptr, arg_Integer);
    }
}

void drv_DeferImmediateTask(int iPriority, FTASK *fpTask,
    void *arg_voidptr, int arg_Integer)
{
    if (g_pIGameEngine)
    {
        g_pIGameEngine->DeferImmediateTask(iPriority, fpTask,
            arg_voidptr, arg_Integer);
    }
}

void drv_DeferTask(const CLinearTimeAbsolute &ltWhen, int iPriority,
    FTASK *fpTask, void *arg_voidptr, int arg_Integer)
{
    if (g_pIGameEngine)
    {
        g_pIGameEngine->DeferTask(ltWhen, iPriority, fpTask,
            arg_voidptr, arg_Integer);
    }
}

// Guest management — driver-side bridges through mux_IPlayerSession.
//
const UTF8 *drv_CreateGuest(DESC *d)
{
    const UTF8 *pName = nullptr;
    if (g_pIPlayerSession)
    {
        g_pIPlayerSession->CreateGuest(d, &pName);
    }
    return pName;
}

bool drv_CheckGuest(dbref player)
{
    bool result = false;
    if (g_pIPlayerSession)
    {
        g_pIPlayerSession->CheckGuest(player, &result);
    }
    return result;
}

// NamedRegsClear — driver-local copy.  RegRelease is inline in externs.h,
// so the driver can clean up named registers without a COM round-trip.
//
void NamedRegsClear(NamedRegsMap *&map)
{
    if (nullptr == map)
    {
        return;
    }
    for (auto &kv : *map)
    {
        RegRelease(kv.second);
    }
    delete map;
    map = nullptr;
}

// raw_broadcast — driver-side implementation.  Formats the variadic args
// and calls broadcast_and_flush() directly (both live in the driver).
//
void DCL_CDECL raw_broadcast(int inflags, const UTF8 *fmt, ...)
{
    if (!fmt || !*fmt)
    {
        return;
    }

    UTF8 buff[LBUF_SIZE];

    va_list ap;
    va_start(ap, fmt);
    mux_vsnprintf(buff, LBUF_SIZE, fmt, ap);
    va_end(ap);

    broadcast_and_flush(inflags, buff);
}

// Command execution — driver-side bridges through mux_IGameEngine.
//
void drv_PrepareForCommand(dbref player)
{
    if (g_pIGameEngine)
    {
        g_pIGameEngine->PrepareForCommand(player);
    }
}

UTF8 *drv_ProcessCommand(dbref executor, dbref caller, dbref enactor,
    int eval, bool bHasCmdArg, UTF8 *command,
    const UTF8 *cargs[], int ncargs)
{
    UTF8 *pLogBuf = nullptr;
    if (g_pIGameEngine)
    {
        g_pIGameEngine->ProcessCommand(executor, caller, enactor, eval,
            bHasCmdArg, command, cargs, ncargs, &pLogBuf);
    }
    return pLogBuf;
}

void drv_FinishCommand(void)
{
    if (g_pIGameEngine)
    {
        g_pIGameEngine->FinishCommand();
    }
}

void drv_HaltQueue(dbref executor, dbref target)
{
    if (g_pIGameEngine)
    {
        g_pIGameEngine->HaltQueue(executor, target);
    }
}

void drv_WaitQueue(dbref executor, dbref caller, dbref enactor,
    int eval, bool bTimed, const CLinearTimeAbsolute &ltaWhen,
    dbref sem, int attr, UTF8 *command, int ncargs,
    const UTF8 *cargs[], reg_ref *regs[], NamedRegsMap *named)
{
    if (g_pIGameEngine)
    {
        g_pIGameEngine->WaitQueue(executor, caller, enactor, eval, bTimed,
            ltaWhen, sem, attr, command, ncargs, cargs, regs, named);
    }
}

// Object movement.
//
void drv_MoveObject(dbref thing, dbref dest)
{
    if (g_pIGameEngine)
    {
        g_pIGameEngine->MoveObject(thing, dest);
    }
}

// Queries for WHO/INFO display.
//
dbref drv_WhereRoom(dbref what)
{
    dbref room = NOTHING;
    if (g_pIGameEngine)
    {
        g_pIGameEngine->WhereRoom(what, &room);
    }
    return room;
}

const UTF8 *drv_TimeFormat1(int seconds, size_t maxWidth)
{
    const UTF8 *pResult = T("");
    if (g_pIGameEngine)
    {
        g_pIGameEngine->TimeFormat1(seconds, maxWidth, &pResult);
    }
    return pResult;
}

const UTF8 *drv_TimeFormat2(int seconds)
{
    const UTF8 *pResult = T("");
    if (g_pIGameEngine)
    {
        g_pIGameEngine->TimeFormat2(seconds, &pResult);
    }
    return pResult;
}

int drv_GetDbTop(void)
{
    int dbTop = 0;
    if (g_pIGameEngine)
    {
        g_pIGameEngine->GetDbTop(&dbTop);
    }
    return dbTop;
}

const UTF8 **drv_GetInfoTable(void)
{
    static const UTF8 *empty[] = { nullptr };
    const UTF8 **ppTable = nullptr;
    if (g_pIGameEngine)
    {
        g_pIGameEngine->GetInfoTable(&ppTable);
    }
    return ppTable ? ppTable : empty;
}

// Emergency/signal/shutdown operations.
//
void drv_Report(void)
{
    if (g_pIGameEngine)
    {
        g_pIGameEngine->Report();
    }
}

void drv_PresyncDatabaseSigsegv(void)
{
    if (g_pIGameEngine)
    {
        g_pIGameEngine->PresyncDatabaseSigsegv();
    }
}

void drv_DoRestart(dbref executor, dbref caller, dbref enactor,
    int eval, int key)
{
    if (g_pIGameEngine)
    {
        g_pIGameEngine->DoRestart(executor, caller, enactor, eval, key);
    }
}

void drv_CacheClose(void)
{
    if (g_pIGameEngine)
    {
        g_pIGameEngine->CacheClose();
    }
}
