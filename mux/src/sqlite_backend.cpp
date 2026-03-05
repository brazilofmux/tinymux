/*! \file sqlite_backend.cpp
 * \brief TinyMUX build wrapper for the SQLite storage backend.
 *
 * This file includes the implementation from db/sqlite_backend.cpp with the
 * proper TinyMUX type definitions in scope.
 */

#ifdef SQLITE_STORAGE

#include "copyright.h"
#include "autoconf.h"
#include "config.h"

// Tell the db/ headers to use TinyMUX's types instead of their own.
//
#define TINYMUX_TYPES_DEFINED

#include "../../db/sqlite_backend.cpp"

#endif // SQLITE_STORAGE
