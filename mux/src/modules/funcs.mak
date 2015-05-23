# Makefile for building AMD64 version with Intel 9.1 Compiler.
#
!IF "$(CFG)" == ""
CFG=funcs - Win64 Release
!MESSAGE No configuration specified. Defaulting to funcs - Win64 Release.
!ENDIF 

!IF "$(CFG)" != "funcs - Win64 Release" && "$(CFG)" != "funcs - Win64 Debug"
!MESSAGE Invalid configuration "$(CFG)" specified.
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "funcs.mak" CFG="funcs - Win64 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "funcs - Win64 Release"
!MESSAGE "funcs - Win64 Debug"
!MESSAGE 
!ERROR An invalid configuration is specified.
!ENDIF 

!IF "$(OS)" == "Windows_NT"
NULL=
!ELSE 
NULL=nul
!ENDIF 

!IF  "$(CFG)" == "funcs - Win64 Release"

OUTDIR=.\bin_release
INTDIR=.\bin_release_funcs
# Begin Custom Macros
OutDir=.\bin_release
# End Custom Macros

ALL : "$(OUTDIR)\funcs.dll"


CLEAN :
	-@erase "$(INTDIR)\funcs.obj"
	-@erase "$(INTDIR)\vc60.idb"
	-@erase "$(OUTDIR)\funcs.dll"
	-@erase "$(INTDIR)\funcs.exp"
	-@erase "$(INTDIR)\funcs.lib"

"$(OUTDIR)" :
    if not exist "$(OUTDIR)/$(NULL)" mkdir "$(OUTDIR)"

"$(INTDIR)" :
    if not exist "$(INTDIR)/$(NULL)" mkdir "$(INTDIR)"

CPP=icl.exe
CPP_PROJ=/nologo /MT /W3 /GX /Ot /Oa /Og /Oi /Ob2 /Gy /D "NDEBUG" /D "WIN32" /D "WIN64" /D "_CONSOLE" /D "UNICODE" /D "WOD_REALMS" /FR"$(INTDIR)\\" /Fp"$(INTDIR)\funcs.pch" /YX /Fo"$(INTDIR)\\" /Fd"$(INTDIR)\\" /FD /G7 /GF /O3 /Qprec_div /Qipo2 /Qsfalign8 /Qparallel /Qpar_threshold50 /Qunroll /c 

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
LINK32_FLAGS=kernel32.lib user32.lib gdi32.lib wsock32.lib "..\bin_release\libmux.lib" /nologo /version:2.10 /subsystem:console /dll /incremental:no /pdb:"$(OUTDIR)\funcs.pdb" /machine:amd64 /def:".\funcs.def" /out:"$(OUTDIR)\funcs.dll"
DEF_FILE= \
	".\funcs.def"
LINK32_OBJS= \
	"$(INTDIR)\funcs.obj" \
	"..\bin_release\libmux.lib"

"$(OUTDIR)\funcs.dll" : "$(OUTDIR)" $(DEF_FILE) $(LINK32_OBJS)
    $(LINK32) @<<
  $(LINK32_FLAGS) $(LINK32_OBJS)
<<

!ELSEIF  "$(CFG)" == "funcs - Win64 Debug"

OUTDIR=.\bin_debug
INTDIR=.\bin_debug_funcs
# Begin Custom Macros
OutDir=.\bin_debug
# End Custom Macros

ALL : "$(OUTDIR)\funcs.dll"


CLEAN :
	-@erase "$(INTDIR)\funcs.obj"
	-@erase "$(INTDIR)\vc60.idb"
	-@erase "$(INTDIR)\vc60.pdb"
	-@erase "$(OUTDIR)\funcs.dll"
	-@erase "$(OUTDIR)\funcs.exp"
	-@erase "$(OUTDIR)\funcs.ilk"
	-@erase "$(OUTDIR)\funcs.lib"
	-@erase "$(OUTDIR)\funcs.pdb"

"$(OUTDIR)" :
    if not exist "$(OUTDIR)/$(NULL)" mkdir "$(OUTDIR)"

"$(INTDIR)" :
    if not exist "$(INTDIR)/$(NULL)" mkdir "$(INTDIR)"

CPP=icl.exe
CPP_PROJ=/nologo /MTd /W3 /GX /Zi /Od /D "_DEBUG" /D "WIN32" /D "WIN64" /D "_CONSOLE" /D "UNICODE" /D "WOD_REALMS" /FR"$(INTDIR)\\" /Fp"$(INTDIR)\funcs.pch" /YX /Fo"$(INTDIR)\\" /Fd"$(INTDIR)\\" /FD /G7 /GZ /c 

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
LINK32_FLAGS=kernel32.lib user32.lib gdi32.lib wsock32.lib "..\bin_debug\libmux.lib" /nologo /subsystem:console /dll /incremental:yes /pdb:"$(OUTDIR)\funcs.pdb" /debug /machine:amd64 /def:".\funcs.def" /out:"$(OUTDIR)\funcs.dll" /pdbtype:sept 
DEF_FILE= \
	".\funcs.def"
LINK32_OBJS= \
	"$(INTDIR)\funcs.obj" \
	"..\bin_debug\libmux.lib"

"$(OUTDIR)\funcs.dll" : "$(OUTDIR)" $(DEF_FILE) $(LINK32_OBJS)
    $(LINK32) @<<
  $(LINK32_FLAGS) $(LINK32_OBJS)
<<

!ENDIF 


!IF "$(NO_EXTERNAL_DEPS)" != "1"
!IF EXISTS("funcs.dep")
!INCLUDE "funcs.dep"
!ELSE 
!MESSAGE Warning: cannot find "funcs.dep"
!ENDIF 
!ENDIF 


!IF "$(CFG)" == "funcs - Win64 Release" || "$(CFG)" == "funcs - Win64 Debug"
SOURCE=.\funcs.cpp

"$(INTDIR)\funcs.obj" : $(SOURCE) "$(INTDIR)"


!ENDIF 

