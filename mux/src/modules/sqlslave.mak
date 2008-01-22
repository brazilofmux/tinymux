# Makefile for building AMD64 version with Intel 9.1 Compiler.
#
!IF "$(CFG)" == ""
CFG=sqlslave - Win64 Release
!MESSAGE No configuration specified. Defaulting to sqlslave - Win64 Release.
!ENDIF 

!IF "$(CFG)" != "sqlslave - Win64 Release" && "$(CFG)" != "sqlslave - Win64 Debug"
!MESSAGE Invalid configuration "$(CFG)" specified.
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "sqlslave.mak" CFG="sqlslave - Win64 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "sqlslave - Win64 Release"
!MESSAGE "sqlslave - Win64 Debug"
!MESSAGE 
!ERROR An invalid configuration is specified.
!ENDIF 

!IF "$(OS)" == "Windows_NT"
NULL=
!ELSE 
NULL=nul
!ENDIF 

!IF  "$(CFG)" == "sqlslave - Win64 Release"

OUTDIR=.\bin_release
INTDIR=.\bin_release_sqlslave
# Begin Custom Macros
OutDir=.\bin_release
# End Custom Macros

ALL : "$(OUTDIR)\sqlslave.dll"


CLEAN :
	-@erase "$(INTDIR)\sqlslave.obj"
	-@erase "$(INTDIR)\vc60.idb"
	-@erase "$(OUTDIR)\sqlslave.dll"
	-@erase "$(INTDIR)\sqlslave.exp"
	-@erase "$(INTDIR)\sqlslave.lib"

"$(OUTDIR)" :
    if not exist "$(OUTDIR)/$(NULL)" mkdir "$(OUTDIR)"

"$(INTDIR)" :
    if not exist "$(INTDIR)/$(NULL)" mkdir "$(INTDIR)"

CPP=icl.exe
CPP_PROJ=/nologo /MT /W3 /GX /Ot /Oa /Og /Oi /Ob2 /Gy /D "NDEBUG" /D "WIN32" /D "WIN64" /D "_CONSOLE" /D "UNICODE" /D "WOD_REALMS" /FR"$(INTDIR)\\" /Fp"$(INTDIR)\sqlslave.pch" /YX /Fo"$(INTDIR)\\" /Fd"$(INTDIR)\\" /FD /G7 /GF /O3 /Qprec_div /Qipo2 /Qsfalign8 /Qparallel /Qpar_threshold50 /Qunroll /c 

.c{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cpp{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cxx{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

LINK32=xilink.exe
LINK32_FLAGS=kernel32.lib user32.lib gdi32.lib wsock32.lib "..\bin_release\libmux.lib" /nologo /version:2.8 /subsystem:console /dll /incremental:no /pdb:"$(OUTDIR)\sqlslave.pdb" /machine:amd64 /def:".\sqlslave.def" /out:"$(OUTDIR)\sqlslave.dll"
DEF_FILE= \
	".\sqlslave.def"
LINK32_OBJS= \
	"$(INTDIR)\sqlslave.obj" \
	"..\bin_release\libmux.lib"

"$(OUTDIR)\sqlslave.dll" : "$(OUTDIR)" $(DEF_FILE) $(LINK32_OBJS)
    $(LINK32) @<<
  $(LINK32_FLAGS) $(LINK32_OBJS)
<<

!ELSEIF  "$(CFG)" == "sqlslave - Win64 Debug"

OUTDIR=.\bin_debug
INTDIR=.\bin_debug_sqlslave
# Begin Custom Macros
OutDir=.\bin_debug
# End Custom Macros

ALL : "$(OUTDIR)\sqlslave.dll"


CLEAN :
	-@erase "$(INTDIR)\sqlslave.obj"
	-@erase "$(INTDIR)\vc60.idb"
	-@erase "$(INTDIR)\vc60.pdb"
	-@erase "$(OUTDIR)\sqlslave.dll"
	-@erase "$(OUTDIR)\sqlslave.exp"
	-@erase "$(OUTDIR)\sqlslave.ilk"
	-@erase "$(OUTDIR)\sqlslave.lib"
	-@erase "$(OUTDIR)\sqlslave.pdb"

"$(OUTDIR)" :
    if not exist "$(OUTDIR)/$(NULL)" mkdir "$(OUTDIR)"

"$(INTDIR)" :
    if not exist "$(INTDIR)/$(NULL)" mkdir "$(INTDIR)"

CPP=icl.exe
CPP_PROJ=/nologo /MTd /W3 /GX /Zi /Od /D "_DEBUG" /D "WIN32" /D "WIN64" /D "_CONSOLE" /D "UNICODE" /D "WOD_REALMS" /FR"$(INTDIR)\\" /Fp"$(INTDIR)\sqlslave.pch" /YX /Fo"$(INTDIR)\\" /Fd"$(INTDIR)\\" /FD /G7 /GZ /c 

.c{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cpp{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cxx{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<
	
LINK32=xilink.exe
LINK32_FLAGS=kernel32.lib user32.lib gdi32.lib wsock32.lib "..\bin_debug\libmux.lib" /nologo /subsystem:console /dll /incremental:yes /pdb:"$(OUTDIR)\sqlslave.pdb" /debug /machine:amd64 /def:".\sqlslave.def" /out:"$(OUTDIR)\sqlslave.dll" /pdbtype:sept 
DEF_FILE= \
	".\sqlslave.def"
LINK32_OBJS= \
	"$(INTDIR)\sqlslave.obj" \
	"..\bin_debug\libmux.lib"

"$(OUTDIR)\sqlslave.dll" : "$(OUTDIR)" $(DEF_FILE) $(LINK32_OBJS)
    $(LINK32) @<<
  $(LINK32_FLAGS) $(LINK32_OBJS)
<<

!ENDIF 


!IF "$(NO_EXTERNAL_DEPS)" != "1"
!IF EXISTS("sqlslave.dep")
!INCLUDE "sqlslave.dep"
!ELSE 
!MESSAGE Warning: cannot find "sqlslave.dep"
!ENDIF 
!ENDIF 


!IF "$(CFG)" == "sqlslave - Win64 Release" || "$(CFG)" == "sqlslave - Win64 Debug"
SOURCE=.\sqlslave.cpp

"$(INTDIR)\sqlslave.obj" : $(SOURCE) "$(INTDIR)"


!ENDIF 

