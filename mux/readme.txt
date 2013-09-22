TinyMUX 2.12: README (Win32/64-based)
Last Update: July 2012
~~~~~~~~~~~~~~~~~~~~~~

Herein are extra notes for the Win32/64 distribution.  These notes do not
apply well to the Unix distribution.

There are two different Win32/64 distributions (binary and source) using three
different archive tools (PKZip, JAR from ARJ, and tar/gzip).

Unless you want to build the server yourself, you should use one of the binary
distributions.  In the binary distributions, the server has been compiled for
you using the Intel 9.1 compiler with aggressive, profile-guided
optimizations, vectorized loops, and CPU-aware dispatching.  You need at least
a Pentium Pro or above to use these binaries.  The pre-built Win32 binary
works on everything from Windows 98 through Windows Vista, however it does not
work with Windows 95 or Windows 95SR2.  The pre-built Win64 binary works on
the 64-bit editions of Windows 2003 Server, Windows XP, and Windows Vista.
IA-64 is not supported.

A source distribution is provided, and with the right compiler, it should be
possible to build for Windows 95, Windows 95SR2, and IA-64 platforms (or any
of the above supported platforms).  The sources have been successfully
compiled with Visual C 98 (part of Visual Studio 6.0), Visual Studio 2003.NET,
Visual Studio 2005, as well as the Intel 9.1 C/C++ Compiler.  If you want to
use Cygwin to compile the source, then don't use any of the Win32
distributions.  Instead, download one of the Unix distributions of TinyMUX and
follow the instructions contained there.

Regarding PKZip, you -must- use version 2.50 or use WinZip (www.winzip.com).
You cannot use PKZip 2.04g.  PKZip 2.04g does not support long filenames.
So, while PKZip 2.04g will unpack the distribution, your filenames will be
named incorrectly.  Use version 2.50 of PKZip or use WinZip.

Regarding tar/gzip on Win32, I'm using the Cygwin version of these tools.
You can use Cygwin tools to unpack the Win32 distribution.  However, as
mentioned above, you should not be using the Win32 distribution if you want
to use Cygwin to compile TinyMUX 2.12.

Regarding JAR from ARJ Software, this archiving tool produces the smallest
files.  It's available via http://www.arjsoftware.com/jar.htm.

Vista doesn't seem to like cscript //h:cscript, so you will need to use
cscript directly to launch startmux.wsf.

To use a binary distribution:

 1. Open a Command Prompt window and unpack the distribution using one of the
    following lines (depending on which archiving program you have chosen to
    use).

        jar32 x mux-2.12.0.1.win32.bin.j
        tar xzf mux-2.12.0.1.win32.bin.tar.gz
        pkzip -extract -directories mux-2.12.0.1.win32.bin.zip

    -or-

    Unpack the mux-2.12.0.1.win32.bin.zip using WinZip.

 2. The pre-built binaries for 32-bit are already placed in mux2.12/game/bin and
    ready to go.  64-bit binaries are provided in mux2.12/game/bin/win64, but to
    use those, you need to be using a 64-bit version of Windows, and you need
    to copy them up one directory level into mux2.12/game/bin.  64-bit versions
    of Windows can use either.

 3. cd mux2.12/game

 4. Possibly edit netmux.conf and mux.config to tweak the configuration.

 5. Start the server with the following:

       cscript startmux.wsf

    -or-

       cscript //h:cscript        (once per system)
       startmux


To use a source distribution:

 1. Open a Command Prompt window and unpack the distribution using one of the
    following lines (depending on which archiving program you have chosen to
    use).

        jar32 x mux-2.12.0.1.win32.src.j
        tar xzf mux-2.12.0.1.win32.src.tar.gz
        pkzip -extract -directories mux-2.12.0.1.win32.src.zip

    -or-

    Unpack the mux-2.12.0.1.win32.src.zip using WinZip.

 2. Start Visual C++ and open the workspace file (mux2.12/src/netmux.dsw).  Your
    version of Visual Studio may want to convert this workspace file into a
    'solution' file and also convert all the project files.  Let it do this,
    and then remember to work with the solution file thereafter.

 3. Within Visual C++, do a batch build in order to produce netmux.exe.  The
    non-debug version will be placed in mux2.12/src/bin_release and must be
    copied over to mux2.12/game/bin.  It will also build a libmux.dll file which
    must also be copied over to mux2.12/game/bin.  It will also build several
    modules under mux2.12/src/modules/bin_release.  If you intend to use
    these, they must also be copied into the mux2.12/game/bin directory.

 4. Start the server with the following:

       cscript startmux.wsf

    -or-

       cscript //h:cscript        (once per system)
       startmux


To load an existing database:

For TinyMUX, all transfers between Unix and Win32 must be performed in
_BINARY_ mode or the copy of your database will be corrupted and you'll get a
failed assertion message in db_rw.cpp when the server tries to load your
database.

So, if you're using FTP, be sure to use 'binary' command.  If you compressed
your database with tar/gzip, and are using WinZip to uncompress it on
Windows, WinZip will auto-mangle it for you.  You must transfer it a different
way.


*  Use db_load to load your database:

       del netmux.pag
       del netmux.dir
       db_load netmux netmux.flat netmux.db

   -or-

   Use netmux in the stand-alone mode directly:

       ..\bin\netmux -dnetmux -inetmux.flat -onetmux.db -l

*  Use db_unload to unload your database:

       db_unload netmux netmux.db.new netmux.flat

   -or-

   Use netmux in the the stand-alone mode directly:

       ..\bin\netmux -dnetmux -inetmux.db.new -onetmux.flat -u

NOTE:  Do not simply double-click on any program or script.  You must open a
'Command Prompt' window and start the game via 'startmux'.
