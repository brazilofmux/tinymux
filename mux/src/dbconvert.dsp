# Microsoft Developer Studio Project File - Name="dbconvert" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Console Application" 0x0103

CFG=dbconvert - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "dbconvert.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "dbconvert.mak" CFG="dbconvert - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "dbconvert - Win32 Release" (based on "Win32 (x86) Console Application")
!MESSAGE "dbconvert - Win32 Debug" (based on "Win32 (x86) Console Application")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""$/TinyMUX22/mux/src", HDGAAAAA"
# PROP Scc_LocalPath "."
CPP=cl.exe
RSC=rc.exe

!IF  "$(CFG)" == "dbconvert - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "dbconvert___Win32_Release"
# PROP BASE Intermediate_Dir "dbconvert___Win32_Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "bin_release"
# PROP Intermediate_Dir "bin_release_dbconvert"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_CONSOLE" /D "_MBCS" /YX /FD /c
# ADD CPP /nologo /G6 /Gr /W3 /GX- /Ox /Ot /Oa /Og /Oi /Oy /Ob2 /Gf /Gy /D "NDEBUG" /D "WIN32" /D "_CONSOLE" /D "_MBCS" /D "WOD_REALMS" /D "STANDALONE" /FR /YX /FD /c
# SUBTRACT CPP /Os
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /machine:I386
# ADD LINK32 kernel32.lib user32.lib gdi32.lib /nologo /version:2.2 /subsystem:console /machine:I386

!ELSEIF  "$(CFG)" == "dbconvert - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "dbconvert___Win32_Debug"
# PROP BASE Intermediate_Dir "dbconvert___Win32_Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "bin_debug"
# PROP Intermediate_Dir "bin_debug_dbconvert"
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_CONSOLE" /D "_MBCS" /YX /FD /GZ /c
# ADD CPP /nologo /W3 /Gm /GX /ZI /Od /D "_DEBUG" /D "WIN32" /D "_CONSOLE" /D "_MBCS" /D "WOD_REALMS" /D "STANDALONE" /YX /FD /GZ /c
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /debug /machine:I386 /pdbtype:sept
# ADD LINK32 kernel32.lib user32.lib gdi32.lib /nologo /subsystem:console /debug /machine:I386 /pdbtype:sept

!ENDIF 

# Begin Target

# Name "dbconvert - Win32 Release"
# Name "dbconvert - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=.\_build.cpp
# End Source File
# Begin Source File

SOURCE=.\attrcache.cpp
# End Source File
# Begin Source File

SOURCE=.\boolexp.cpp
# End Source File
# Begin Source File

SOURCE=.\conf.cpp
# End Source File
# Begin Source File

SOURCE=.\db.cpp
# End Source File
# Begin Source File

SOURCE=.\db_rw.cpp
# End Source File
# Begin Source File

SOURCE=.\dbconvert.cpp
# End Source File
# Begin Source File

SOURCE=.\flags.cpp
# End Source File
# Begin Source File

SOURCE=.\htab.cpp
# End Source File
# Begin Source File

SOURCE=.\log.cpp
# End Source File
# Begin Source File

SOURCE=.\object.cpp
# End Source File
# Begin Source File

SOURCE=.\player_c.cpp
# End Source File
# Begin Source File

SOURCE=.\predicates.cpp
# End Source File
# Begin Source File

SOURCE=.\stringutil.cpp
# End Source File
# Begin Source File

SOURCE=.\svdhash.cpp
# End Source File
# Begin Source File

SOURCE=.\svdrand.cpp
# End Source File
# Begin Source File

SOURCE=.\timeutil.cpp
# End Source File
# Begin Source File

SOURCE=.\unparse.cpp
# End Source File
# Begin Source File

SOURCE=.\vattr.cpp
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE=.\_build.h
# End Source File
# Begin Source File

SOURCE=.\alloc.h
# End Source File
# Begin Source File

SOURCE=.\ansi.h
# End Source File
# Begin Source File

SOURCE=.\attrcache.h
# End Source File
# Begin Source File

SOURCE=.\attrs.h
# End Source File
# Begin Source File

SOURCE=.\autoconf.h
# End Source File
# Begin Source File

SOURCE=.\command.h
# End Source File
# Begin Source File

SOURCE=.\comsys.h
# End Source File
# Begin Source File

SOURCE=.\config.h
# End Source File
# Begin Source File

SOURCE=.\copyright.h
# End Source File
# Begin Source File

SOURCE=.\crypt\crypt.h
# End Source File
# Begin Source File

SOURCE=.\db.h
# End Source File
# Begin Source File

SOURCE=.\externs.h
# End Source File
# Begin Source File

SOURCE=.\flags.h
# End Source File
# Begin Source File

SOURCE=.\functions.h
# End Source File
# Begin Source File

SOURCE=.\htab.h
# End Source File
# Begin Source File

SOURCE=.\interface.h
# End Source File
# Begin Source File

SOURCE=.\mail.h
# End Source File
# Begin Source File

SOURCE=.\match.h
# End Source File
# Begin Source File

SOURCE=.\mguests.h
# End Source File
# Begin Source File

SOURCE=.\mudconf.h
# End Source File
# Begin Source File

SOURCE=.\pcre.h
# End Source File
# Begin Source File

SOURCE=.\powers.h
# End Source File
# Begin Source File

SOURCE=.\stringutil.h
# End Source File
# Begin Source File

SOURCE=.\svdhash.h
# End Source File
# Begin Source File

SOURCE=.\svdrand.h
# End Source File
# Begin Source File

SOURCE=.\timeutil.h
# End Source File
# Begin Source File

SOURCE=.\vattr.h
# End Source File
# End Group
# Begin Group "Resource Files"

# PROP Default_Filter "ico;cur;bmp;dlg;rc2;rct;bin;rgs;gif;jpg;jpeg;jpe"
# End Group
# End Target
# End Project
