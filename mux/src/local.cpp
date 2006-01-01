/* local.cpp
 *
 * Inspired by Penn's local extensions; implemented for TinyMUX by
 * M. Hassman (June 2005)
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"
#include "functions.h"
#include "command.h"

// ----------------------------------------------------------------------------
// local_funlist: List of existing functions in alphabetical order.
//
//   Name          Handler      # of args   min #    max #   flags  permissions
//                               to parse  of args  of args
//
FUN local_funlist[] =
{
    {NULL,          NULL,           MAX_ARG, 0,       0,         0, 0}
};

// ---------------------------------------------------------------------------
// Local command tables: Definitions for local hardcode commands.
//
//   Name       Switches    Permissions    Key Calling Seq   hook mask  Handler
//
CMDENT_NO_ARG local_command_table_no_arg[] =
{
    {NULL,          NULL,       0,           0,          0,          0, NULL}
};

CMDENT_ONE_ARG local_command_table_one_arg[] =
{
    {NULL,          NULL,       0,           0,          0,          0, NULL}
};

CMDENT_ONE_ARG_CMDARG local_command_table_one_arg_cmdarg[] =
{
    {NULL,          NULL,       0,           0,          0,          0, NULL}
};

CMDENT_TWO_ARG local_command_table_two_arg[] =
{
    {NULL,          NULL,       0,           0,          0,          0, NULL}
};

CMDENT_TWO_ARG_CMDARG local_command_table_two_arg_cmdarg[] =
{
    {NULL,          NULL,       0,           0,          0,          0, NULL}
};

CMDENT_TWO_ARG_ARGV local_command_table_two_arg_argv[] =
{
    {NULL,          NULL,       0,           0,          0,          0, NULL}
};

CMDENT_TWO_ARG_ARGV_CMDARG local_command_table_two_argv_cmdarg[] =
{
    {NULL,          NULL,       0,           0,          0,          0, NULL}
};

// Called after all normal MUX initialization is complete
//
void local_startup(void)
{
    // Add additional hardcode functions to the above table.
    //
    functions_add(local_funlist);

    // Add additional CMDENT_NO_ARG commands to the above table.
    //
    commands_no_arg_add(local_command_table_no_arg);
    commands_one_arg_add(local_command_table_one_arg);
    commands_one_arg_cmdarg_add(local_command_table_one_arg_cmdarg);
    commands_two_arg_add(local_command_table_two_arg);
    commands_two_arg_cmdarg_add(local_command_table_two_arg_cmdarg);
    commands_two_arg_argv_add(local_command_table_two_arg_argv);
    commands_two_arg_argv_cmdarg_add(local_command_table_two_argv_cmdarg);
}

// This is called prior to the game syncronizing its own state to its own
// database.  If you depend on the the core database to store your data, you
// need to checkpoint your changes here. The write-protection
// mechanism in MUX is not turned on at this point.  You are guaranteed
// to not be a fork()-ed dumping process.
//
void local_presync_database(void)
{
}

// Like the above routine except that it called from the SIGSEGV handler.
// At this point, your choices are limited. You can attempt to use the core
// database. The core won't stop you, but it is risky.
//
void local_presync_database_sigsegv(void)
{
}

// This is called prior to the game database writing out it's own database.
// This is typically only called from the fork()-ed process so write-
// protection is in force and you will be unable to modify the game's
// database for you own needs.  You can however, use this point to maintain
// your own dump file.
//
// The caveat is that it is possible the game will crash while you are doing
// this, or it is already in the process of crashing.  You may be called
// reentrantly.  Therefore, it is recommended that you follow the pattern in
// dump_database_internal() and write your database to a temporary file, and
// then if completed successfully, move your temporary over the top of your
// old database.
//
// The argument dump_type is one of the 5 DUMP_I_x defines declared in
// externs.h
//
void local_dump_database(int dump_type)
{
}

// The function is called when the dumping process has completed. Typically,
// this will be called from within a signal handler. Your ability to do
// anything interesting from within a signal handler is severly limited.
// This is also called at the end of the dumping process if either no dumping
// child was created or if the child finished quickly. In fact, this
// may be called twice at the end of the same dump.
//
void local_dump_complete_signal(void)
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
