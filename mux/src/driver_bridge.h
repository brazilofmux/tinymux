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

#endif // DRIVER_BRIDGE_H
