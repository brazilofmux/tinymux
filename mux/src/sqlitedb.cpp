/*! \file sqlitedb.cpp
 * \brief TinyMUX build wrapper for the SQLite database module.
 *
 * This file includes the implementation from db/sqlitedb.cpp with the
 * proper TinyMUX type definitions in scope.
 */

#ifdef SQLITE_STORAGE

#include "copyright.h"
#include "autoconf.h"
#include "config.h"

// Tell the db/ headers to use TinyMUX's types instead of their own.
//
#define TINYMUX_TYPES_DEFINED

#include "../../db/sqlitedb.cpp"

#endif // SQLITE_STORAGE
