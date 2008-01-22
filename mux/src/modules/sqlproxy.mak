# Makefile for building AMD64 version with Intel 9.1 Compiler.
#
!IF "$(CFG)" == ""
CFG=sqlproxy - Win64 Release
!MESSAGE No configuration specified. Defaulting to sqlproxy - Win64 Release.
!ENDIF 

!IF "$(CFG)" != "sqlproxy - Win64 Release" && "$(CFG)" != "sqlproxy - Win64 Debug"
!MESSAGE Invalid configuration "$(CFG)" specified.
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "sqlproxy.mak" CFG="sqlproxy - Win64 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "sqlproxy - Win64 Release"
!MESSAGE "sqlproxy - Win64 Debug"
!MESSAGE 
!ERROR An invalid configuration is specified.
!ENDIF 

!IF "$(OS)" == "Windows_NT"
NULL=
!ELSE 
NULL=nul
!ENDIF 

!IF  "$(CFG)" == "sqlproxy - Win64 Release"

OUTDIR=.\bin_release
INTDIR=.\bin_release_sqlproxy
# Begin Custom Macros
OutDir=.\bin_release
# End Custom Macros

ALL : "$(OUTDIR)\sqlproxy.dll"


CLEAN :
	-@erase "$(INTDIR)\sqlproxy.obj"
	-@erase "$(INTDIR)\vc60.idb"
	-@erase "$(OUTDIR)\sqlproxy.dll"
	-@erase "$(INTDIR)\sqlproxy.exp"
	-@erase "$(INTDIR)\sqlproxy.lib"

"$(OUTDIR)" :
    if not exist "$(OUTDIR)/$(NULL)" mkdir "$(OUTDIR)"

"$(INTDIR)" :
    if not exist "$(INTDIR)/$(NULL)" mkdir "$(INTDIR)"

CPP=icl.exe
CPP_PROJ=/nologo /MT /W3 /GX /Ot /Oa /Og /Oi /Ob2 /Gy /D "NDEBUG" /D "WIN32" /D "WIN64" /D "_CONSOLE" /D "UNICODE" /D "WOD_REALMS" /FR"$(INTDIR)\\" /Fp"$(INTDIR)\sqlproxy.pch" /YX /Fo"$(INTDIR)\\" /Fd"$(INTDIR)\\" /FD /G7 /GF /O3 /Qprec_div /Qipo2 /Qsfalign8 /Qparallel /Qpar_threshold50 /Qunroll /c 

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
LINK32_FLAGS=kernel32.lib user32.lib gdi32.lib wsock32.lib "..\bin_release\libmux.lib" /nologo /version:2.8 /subsystem:console /dll /incremental:no /pdb:"$(OUTDIR)\sqlproxy.pdb" /machine:amd64 /def:".\sqlproxy.def" /out:"$(OUTDIR)\sqlproxy.dll"
DEF_FILE= \
	".\sqlproxy.def"
LINK32_OBJS= \
	"$(INTDIR)\sqlproxy.obj" \
	"..\bin_release\libmux.lib"

"$(OUTDIR)\sqlproxy.dll" : "$(OUTDIR)" $(DEF_FILE) $(LINK32_OBJS)
    $(LINK32) @<<
  $(LINK32_FLAGS) $(LINK32_OBJS)
<<

!ELSEIF  "$(CFG)" == "sqlproxy - Win64 Debug"

OUTDIR=.\bin_debug
INTDIR=.\bin_debug_sqlproxy
# Begin Custom Macros
OutDir=.\bin_debug
# End Custom Macros

ALL : "$(OUTDIR)\sqlproxy.dll"


CLEAN :
	-@erase "$(INTDIR)\sqlproxy.obj"
	-@erase "$(INTDIR)\vc60.idb"
	-@erase "$(INTDIR)\vc60.pdb"
	-@erase "$(OUTDIR)\sqlproxy.dll"
	-@erase "$(OUTDIR)\sqlproxy.exp"
	-@erase "$(OUTDIR)\sqlproxy.ilk"
	-@erase "$(OUTDIR)\sqlproxy.lib"
	-@erase "$(OUTDIR)\sqlproxy.pdb"

"$(OUTDIR)" :
    if not exist "$(OUTDIR)/$(NULL)" mkdir "$(OUTDIR)"

"$(INTDIR)" :
    if not exist "$(INTDIR)/$(NULL)" mkdir "$(INTDIR)"

CPP=icl.exe
CPP_PROJ=/nologo /MTd /W3 /GX /Zi /Od /D "_DEBUG" /D "WIN32" /D "WIN64" /D "_CONSOLE" /D "UNICODE" /D "WOD_REALMS" /FR"$(INTDIR)\\" /Fp"$(INTDIR)\sqlproxy.pch" /YX /Fo"$(INTDIR)\\" /Fd"$(INTDIR)\\" /FD /G7 /GZ /c 

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
LINK32_FLAGS=kernel32.lib user32.lib gdi32.lib wsock32.lib "..\bin_debug\libmux.lib" /nologo /subsystem:console /dll /incremental:yes /pdb:"$(OUTDIR)\sqlproxy.pdb" /debug /machine:amd64 /def:".\sqlproxy.def" /out:"$(OUTDIR)\sqlproxy.dll" /pdbtype:sept 
DEF_FILE= \
	".\sqlproxy.def"
LINK32_OBJS= \
	"$(INTDIR)\sqlproxy.obj" \
	"..\bin_debug\libmux.lib"

"$(OUTDIR)\sqlproxy.dll" : "$(OUTDIR)" $(DEF_FILE) $(LINK32_OBJS)
    $(LINK32) @<<
  $(LINK32_FLAGS) $(LINK32_OBJS)
<<

!ENDIF 


!IF "$(NO_EXTERNAL_DEPS)" != "1"
!IF EXISTS("sqlproxy.dep")
!INCLUDE "sqlproxy.dep"
!ELSE 
!MESSAGE Warning: cannot find "sqlproxy.dep"
!ENDIF 
!ENDIF 


!IF "$(CFG)" == "sqlproxy - Win64 Release" || "$(CFG)" == "sqlproxy - Win64 Debug"
SOURCE=.\sqlproxy.cpp

"$(INTDIR)\sqlproxy.obj" : $(SOURCE) "$(INTDIR)"


!ENDIF 

