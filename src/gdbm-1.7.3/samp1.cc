// small c++ to open store then close a gdbm file 
// by Mike MacFaden 4/93  mike@premisys.com
// tested with gdbm 1.5 and gnu v2.2 c++ compiler

#include "iostream.h"
#include "gdbm.h"

extern int errno;		/* C runtime library */

int main(int argc, char *argv[])
{
	cout << "Sample C++ program create a gdbm file ./tgdbm " << endl;

	GDBM_FILE pfile = gdbm_open("tstgdbm", 512, GDBM_WRCREAT, 00664, 0);
	if (!pfile)
      {
      cout << "main:gdbm_open " << gdbm_errno << errno << endl;
      return -1;
      }

   datum key = {"foo", strlen("foo")+1};
   datum val = {"bar", strlen("bar")+1};

	cout << "key is  : " << key.dptr << endl;
	cout << "data is : " << val.dptr << endl;

   if (gdbm_store(pfile, key, val, GDBM_INSERT) != 0)
      {
      cout << "main:gdbm_store " << gdbm_errno << errno << endl;
      return -1;
      }
	gdbm_close(pfile);

	cout << "Sample C++ program complete" << endl;
	return 0;		
}
