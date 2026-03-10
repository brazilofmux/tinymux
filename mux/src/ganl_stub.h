/*! \file ganl_stub.h
 * \brief Engine-side GanlAdapter stub.
 *
 * Engine files that need g_GanlAdapter include this instead of
 * ganl_adapter.h.  The real GanlAdapter lives in the driver; the stub
 * delegates through COM (see conn_bridge.cpp).
 */

#ifndef GANL_STUB_H
#define GANL_STUB_H

#include "copyright.h"
#include "autoconf.h"
#include "config.h"

class GanlAdapter
{
public:
    bool start_email_send(dbref executor, const UTF8 *recipient,
        const UTF8 *subject, const UTF8 *body);
    void prepare_for_restart(void);
};

extern GanlAdapter g_GanlAdapter;

#endif // GANL_STUB_H
