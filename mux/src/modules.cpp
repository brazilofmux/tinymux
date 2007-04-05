/*! \file modules.cpp
 * \brief Module support
 *
 * $Id: game.cpp 1831 2007-04-04 18:50:05Z brazilofmux $
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#ifdef HAVE_DLOPEN
#include <dlfcn.h>
#endif // HAVE_DLOPEN

#include "modules.h"
