1. Unpack the distribution using JAR from ARJ software as mentioned
   in the FAQ using the following command line.

	jar x mux-2.0.17.300.win32.src.j

   -or-

   Unpack the mux-2.0.17.300.win32.src.zip using WinZip.

2. Start Visual C++ and open the workspace in the top directory.

3. Within Visual C++, Do a batch build in order to get mkindx.exe,
   a.exe, dbconvert.exe, and tinymux.exe. It will be debug and
   release version of these in their own build directory in the
   topmost directory.

4. Use these binaries as you normally would by copying/moving them to
   the game/bin directory.
   
5. db_load and db_unload batch files are provided, but if you change
   your game name to something other than 'tinymux', you need to edit
   these files.

   -or-

   Instead of using db_load and db_unload, you can use dbconvert
   directly. Type dbconvert for usage. For example, to load a
   flatfile, change to the data directory and type the following:

	del tinymux.pag
	del tinymux.dir
	..\bin\dbconvert tinymux x < tinymux.flat > tinymux.db

	To unload a flatfile, change to the data directory and type
        the following:

	..\bin\dbconvert tinymux X < tinymux.db.new > tinymux.flat

	(or whatever the latest tinymux.db file is)

6. Do not simply double-click on these programs. You must open an
   'Command Prompt' window and start the game via 'Startmux'.
