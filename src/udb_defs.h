/*
	Header file for the UnterMud DB layer, as applied to MUX 1.0

	Andrew Molitor, amolitor@eagle.wesleyan.edu
	1991
	
	$Id: udb_defs.h,v 1.3 1997/04/16 15:45:35 dpassmor Exp $
*/


/*
some machines stdio use different parameters to tell fopen to open
a file for creation and binary read/write. that is why they call it
the "standard" I/O library, I guess. for HP/UX, you need to have
this "rb+", and make sure the file already exists before use. bleah
*/
#define	FOPEN_BINARY_RW	"a+"


#ifdef VMS
#define MAXPATHLEN	256
#define	NOSYSTYPES_H
#define	NOSYSFILE_H
#endif /* VMS */

/* If your malloc() returns void or char pointers... */
/* typedef	void	*mall_t; */
typedef	char	*mall_t;

/* default (runtime-resettable) cache parameters */

#define	CACHE_DEPTH	10
#define	CACHE_WIDTH	20

/* Macros for calling the DB layer */

#define	DB_INIT()	dddb_init()
#define	DB_CLOSE()	dddb_close()
#define	DB_SYNC()	dddb_sync()
#define	DB_GET(n)	dddb_get(n)
#define	DB_PUT(o,n)	dddb_put(o,n)
#define	DB_CHECK(s)	dddb_check(s)

#define	DB_DEL(n,f)	dddb_del(n)

#define	CMD__DBCONFIG	cmd__dddbconfig

