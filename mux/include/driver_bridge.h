/*! \file driver_bridge.h
 * \brief Declarations for driver-side COM bridge functions.
 *
 * These functions delegate through COM interfaces into the engine.
 * Driver files include this header and call drv_* functions instead
 * of using db.h macros (which expand to db[] access).
 */

#ifndef DRIVER_BRIDGE_H
#define DRIVER_BRIDGE_H

// Object accessors — delegate through mux_IObjectInfo.
//
unsigned int drv_Flags(dbref obj, int word);
void         drv_s_Flags(dbref obj, int word, unsigned int flags);
UTF8        *drv_decode_flags(dbref player, dbref obj);
int          drv_Pennies(dbref obj);
const UTF8  *drv_PureName(dbref obj);
dbref        drv_Location(dbref obj);
const UTF8  *drv_Name(dbref obj);
const UTF8  *drv_Moniker(dbref obj);
unsigned int drv_Powers(dbref obj);
dbref        drv_Owner(dbref obj);
bool         drv_Good_obj(dbref obj);
bool         drv_Connected(dbref obj);

// Compound permission queries.
//
bool         drv_Wizard(dbref obj);
bool         drv_WizRoy(dbref obj);
bool         drv_Can_Idle(dbref obj);
bool         drv_Wizard_Who(dbref obj);
bool         drv_See_Hidden(dbref obj);

// Scheduler — delegate through mux_IGameEngine.
//
void drv_CancelTask(FTASK *fpTask, void *arg_voidptr, int arg_Integer);
void drv_DeferImmediateTask(int iPriority, FTASK *fpTask,
    void *arg_voidptr, int arg_Integer);
void drv_DeferTask(const CLinearTimeAbsolute &ltWhen, int iPriority,
    FTASK *fpTask, void *arg_voidptr, int arg_Integer);

// Guest management — delegate through mux_IPlayerSession.
//
const UTF8 *drv_CreateGuest(DESC *d);
bool        drv_CheckGuest(dbref player);

// Command execution — delegate through mux_IGameEngine.
//
void drv_PrepareForCommand(dbref player);
UTF8 *drv_ProcessCommand(dbref executor, dbref caller, dbref enactor,
    int eval, bool bHasCmdArg, UTF8 *command,
    const UTF8 *cargs[], int ncargs);
void drv_FinishCommand(void);
void drv_HaltQueue(dbref executor, dbref target);
void drv_WaitQueue(dbref executor, dbref caller, dbref enactor,
    int eval, bool bTimed, const CLinearTimeAbsolute &ltaWhen,
    dbref sem, int attr, UTF8 *command, int ncargs,
    const UTF8 *cargs[], reg_ref *regs[], NamedRegsMap *named);

// Object movement.
//
void drv_MoveObject(dbref thing, dbref dest);

// Queries for WHO/INFO display.
//
dbref       drv_WhereRoom(dbref what);
const UTF8 *drv_TimeFormat1(int seconds, size_t maxWidth);
const UTF8 *drv_TimeFormat2(int seconds);
int         drv_GetDbTop(void);
const UTF8 **drv_GetInfoTable(void);

// Emergency/signal/shutdown operations.
//
void drv_Report(void);
void drv_PresyncDatabaseSigsegv(void);
void drv_DoRestart(dbref executor, dbref caller, dbref enactor,
    int eval, int key);
void drv_CacheClose(void);

#endif // DRIVER_BRIDGE_H
