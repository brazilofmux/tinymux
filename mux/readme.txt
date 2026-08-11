TinyMUX 2.14: README (Windows)
Last Update: August 2026
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Herein are extra notes for the Windows distribution.  These notes do not
apply well to the Unix distribution.

Two Windows distributions are provided, binary and source, each as a .zip
archive.  Both are x64 only; there is no 32-bit or IA-64 build.

Unless you want to build the server yourself, use the binary distribution.

If you would rather build under Cygwin or MSYS, do not use the Windows
distribution at all.  Download one of the Unix distributions of TinyMUX and
follow the instructions contained there.


To use a binary distribution:

 1. Open a Command Prompt window and unpack the distribution.  Windows 10
    and later include tar, which reads .zip:

        tar xf mux-2.14.0.11.win32.bin.zip

    Any archiver that preserves long filenames will do as well.

 2. The prebuilt binaries are already in mux2.14\game\bin and are ready to
    run.

 3. cd mux2.14\game

 4. Possibly edit netmux.conf to tweak the configuration.  Startmux.bat
    keeps its own settings -- the bin directory, game name, log directory
    and pid file -- in variables at the top of the file.

 5. Start the server with the following:

        Startmux.bat


To build from a source distribution:

Prerequisites:

 *  Visual Studio 2022 or later, with the "Desktop development with C++"
    workload.  The projects build as C++17 and target x64.

 *  vcpkg, which supplies the third-party libraries.  mux2.14\vcpkg.json
    declares them -- currently grpc, nlohmann-json and pcre2 -- and pins a
    builtin-baseline commit, so you get the same versions the release was
    built against:

        git clone https://github.com/microsoft/vcpkg C:\vcpkg
        C:\vcpkg\bootstrap-vcpkg.bat
        cd mux2.14
        C:\vcpkg\vcpkg.exe install --triplet x64-windows

    Do -not- clone with --depth 1.  The pinned baseline commit is not
    present in a shallow clone, and vcpkg fails with "failed to git show
    versions/baseline.json".  If you already made a shallow clone, run
    'git fetch --depth 1 origin <baseline-sha>' against it to repair it.

    Expect the first run to be slow: grpc dominates, and it takes roughly
    an hour and about 11 GB.  The cost is one-time.  vcpkg caches what it
    builds under %LOCALAPPDATA%\vcpkg\archives, so any later tree installs
    in seconds.

    The install writes mux2.14\vcpkg_installed\x64-windows\, containing
    include\, lib\ and bin\ for release, and debug\lib\ and debug\bin\ for
    debug.  Every project locates it through the $(VcpkgDir) property, so
    no further configuration is needed.  If you keep vcpkg's packages
    somewhere else, override that one property.

Building:

 1. Open mux2.14\netmux.sln in Visual Studio and build the Release|x64
    configuration, or build it from a Developer Command Prompt:

        cd mux2.14
        msbuild netmux.sln -p:Configuration=Release -p:Platform=x64 -m

 2. Output lands in mux2.14\bin_release (Debug builds go to bin_debug).
    engine.dll imports PCRE2, so the build also copies pcre2-8.dll from
    vcpkg into that directory; without it, loading engine.dll fails with
    "The specified module could not be found."

 3. Copy the built files into mux2.14\game\bin:

        netmux.exe, muxscript.exe
        libmux.dll, engine.dll, comsys_mod.dll, mail_mod.dll, exp3.dll,
        sqlslave.dll, sqlproxy.dll
        pcre2-8.dll

    Also copy mux2.14\rv64\softlib.rv64 into game\bin.

    A machine without the Visual C++ redistributable installed needs
    msvcp140.dll, vcruntime140.dll and vcruntime140_1.dll beside
    netmux.exe as well.  The binary distribution ships those three for
    that reason; a source build does not produce them.

 4. Start the server as described for the binary distribution above.


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
