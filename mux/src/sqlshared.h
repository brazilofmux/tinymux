// sqlshared.h -- Shared code between the main server and the sqlslave.
//
// $Id: sqlshared.h,v 1.2 2005-10-14 17:34:09 sdennis Exp $
//

#ifndef SQLSHARED_H
#define SQLSHARED_H
#ifdef QUERY_SLAVE

extern void sqlfoo(void);

#endif // QUERY_SLAVE
#endif // SQLSHARED_H
