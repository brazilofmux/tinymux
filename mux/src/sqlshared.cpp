// sqlshared.cpp -- Shared code between the main server and the sqlslave.
//
// $Id: sqlshared.cpp,v 1.2 2005/10/14 17:34:09 sdennis Exp $
//
#include "autoconf.h"
#include "config.h"

#ifdef QUERY_SLAVE
#include "sqlshared.h"

void sqlfoo(void)
{
    return;
}

#endif // QUERY_SLAVE
