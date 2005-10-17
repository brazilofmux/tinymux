#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"
#include "functions.h"

// ----------------------------------------------------------------------------
// flist: List of existing functions in alphabetical order.
//
//   Name          Handler      # of args   min #    max #   flags  permissions
//                               to parse  of args  of args
//
FUN local_funlist[] =
{
    {NULL,          NULL,           MAX_ARG, 0,       0,         0, 0}
};

/* local.cpp
 *
 * Inspired by Penn's local extensions; implemented for MUX2.4 by
 * M. Hassman (June 2005)
 */

// Called after all normal MUX initialization is complete
//
void local_startup(void)
{
    // Add additional hardcode functions to the above table.
    //
    functions_add(local_funlist);
}

// Called prior to the game database being dumped.   Called by the
// periodic dump timer, @restart, @shutdown, etc.  The argument
// dump_type is one of the 5 DUMP_I_x defines declared in externs.h
//
void local_dump_database(int dump_type)
{
}

// Called when the game is shutting down, after the game database has
// been saved but prior to the logfiles being closed.
//
void local_shutdown(void)
{
}

// Called after the database consistency check is completed.   Add
// checks for local data consistency here.
//
void local_dbck(void)
{
}

// Called when a player connects or creates at the connection screen.
// isnew of 1 indicates it was a creation, 0 is for a connection.
// num indicates the number of current connections for player.
//
void local_connect(dbref player, int isnew, int num)
{
}

// Called when player disconnects from the game.  The parameter 'num' is
// the number of connections the player had upon being disconnected.
// Any value greater than 1 indicates multiple connections.
//
void local_disconnect(dbref player, int num)
{
}

// Called after any object type is created.
//
void local_data_create(dbref object)
{
}

// Called when an object is cloned.  clone is the new object created
// from source.
//
void local_data_clone(dbref clone, dbref source)
{
}

// Called when the object is truly destroyed, not just set GOING
//
void local_data_free(dbref object)
{
}
