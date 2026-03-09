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
