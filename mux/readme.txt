Last Updated: January 2006

Herein are extra notes for the Win32 distribution.  These notes do not apply
well to the Unix distribution.

There are two different Win32 distributions (binary and source) using three
different archive tools (PKZip, JAR from ARJ, and tar/gzip).

Unless you want to build the server yourself, you should use one of the binary
distributions.  In the binary distributions, the server has been compiled for
you using the Intel 9.0 compiler with aggressive, profile-guided
optimizations, vectorized loops, and CPU-aware dispatching.  You need at least
a Pentium Pro or above to use these binaries.

The source distribution is also provided.  If you have Visual C++, you can
build your own binaries.  If you want to use Cygwin to compile the source,
then don't use this distribution.  Instead, download one of the Unix
distributions of MUX and follow the instructions contained there.

Regarding PKZip, you -must- use version 2.50 or use WinZip (www.winzip.com).
You cannot use PKZip 2.04g.  PKZip 2.04g does not support long filenames.
While PKZip 2.04g will unpack the distribution, your filenames will be
named incorrectly.  Use version 2.50 of PKZip or use WinZip.

Regarding tar/gzip on Win32, I'm using the Cygwin version of these tools.
You can use Cygwin tools to unpack the Win32 distribution. However, as
mentioned above, you should not be using the Win32 distribution if you want
to use Cygwin to compile MUX.

Regarding JAR from ARJ Software, this archiving tool produces the smallest
files. It's available via http://www.arjsoftware.com/jar.htm.



To use a binary distribution:

1. Open a Command Prompt window and unpack the distribution using one of the
   following lines (depending on which archiving program you have choosen to
   use).

        jar32 x mux-2.6.0.1.win32.bin.j
        tar xzf mux-2.6.0.1.win32.bin.tar.gz
        pkzip -extract -directories mux-2.6.0.1.win32.bin.zip

   -OR-

   Unpack the mux-2.6.0.1.win32.bin.zip using WinZip.

2. cd mux2.6/game

3. Possibly edit netmux.conf and mux.config to tweak the configuration.

4. Start the server with the following:

       cscript startmux.wsf

   -or-

       cscript //h:cscript        (once per system)
       startmux



To use a source distribution:

1. Open a Command Prompt window and unpack the distribution using one of the
   following lines (depending on which archiving program you have choosen to
   use).

        jar32 x mux-2.6.0.1.win32.src.j
        tar xzf mux-2.6.0.1.win32.src.tar.gz
        pkzip -extract -directories mux-2.6.0.1.win32.src.zip

   -OR-

   Unpack the mux-2.6.0.1.win32.src.zip using WinZip.

2. Start Visual C++ and open the workspace file (mux2.6/src/netmux.dsw).

3. Within Visual C++, Do a batch build in order to produce netmux.exe.  The
   non-debug version will be placed in mux2.6/src/bin_release and must be
   copied over to mux2.6/game/bin.

4. Start the server with the following:

       cscript startmux.wsf

   -or-

       cscript //h:cscript        (once per system)
       startmux



To load an existing database:

For MUX, all transfers between Unix and Win32 must be performed in -BINARY-
mode or the copy of your database will be corrupted and you'll get a failed
assertion message in db_rw.cpp when the server tries to load your database.

So, if you're using FTP, be sure to use 'binary' command. If you compresssed
your database with tar/gzip, and are using WinZip to uncompress it on
Windows, WinZip will auto-mangle it for you. You must transfer it a different
way.


*  Use db_load to load your database:

       del netmux.pag
       del netmux.dir
       db_load netmux netmux.flat netmux.db

   -OR-

   Use netmux in the stand-alone mode directly:

       ..\bin\netmux -dnetmux -inetmux.flat -onetmux.db -l

*  Use db_unload to unload your database:

       db_unload netmux netmux.db.new netmux.flat

   -OR-

   Use netmux in the the stand-alone mode directly:

       ..\bin\netmux -dnetmux -inetmux.db.new -onetmux.flat -u

NOTE:  Do not simply double-click on any program or script. You must open a
'Command Prompt' window and start the game via 'startmux'.
