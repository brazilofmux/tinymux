# Makefile for building AMD64 version with Intel 9.1 Compiler.
#
!IF "$(CFG)" == ""
CFG=sample - Win64 Release
!MESSAGE No configuration specified. Defaulting to sample - Win64 Release.
!ENDIF 

!IF "$(CFG)" != "sample - Win64 Release" && "$(CFG)" != "sample - Win64 Debug"
!MESSAGE Invalid configuration "$(CFG)" specified.
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "sample.mak" CFG="sample - Win64 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "sample - Win64 Release"
!MESSAGE "sample - Win64 Debug"
!MESSAGE 
!ERROR An invalid configuration is specified.
!ENDIF 

!IF "$(OS)" == "Windows_NT"
NULL=
!ELSE 
NULL=nul
!ENDIF 

!IF  "$(CFG)" == "sample - Win64 Release"

OUTDIR=.\bin_release
INTDIR=.\bin_release_sample
# Begin Custom Macros
OutDir=.\bin_release
# End Custom Macros

ALL : "$(OUTDIR)\sample.dll"


CLEAN :
	-@erase "$(INTDIR)\sample.obj"
	-@erase "$(OUTDIR)\sample.dll"

"$(OUTDIR)" :
    if not exist "$(OUTDIR)/$(NULL)" mkdir "$(OUTDIR)"

"$(INTDIR)" :
    if not exist "$(INTDIR)/$(NULL)" mkdir "$(INTDIR)"

CPP=icl.exe
CPP_PROJ=/nologo /MT /W3 /GX /Ot /Oa /Og /Oi /Ob2 /Gy /D "NDEBUG" /D "WIN32" /D "WIN64" /D "_CONSOLE" /D "UNICODE" /D "WOD_REALMS" /FR"$(INTDIR)\\" /Fp"$(INTDIR)\sample.pch" /YX /Fo"$(INTDIR)\\" /Fd"$(INTDIR)\\" /FD /G7 /GF /O3 /Qprec_div /Qipo2 /Qsfalign8 /Qparallel /Qpar_threshold50 /Qunroll /c 

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
LINK32_FLAGS=kernel32.lib user32.lib gdi32.lib wsock32.lib "..\bin_release\libmux.lib" /nologo /version:2.4 /subsystem:console /dll /incremental:no /pdb:"$(OUTDIR)\sample.pdb" /machine:amd64 /out:"$(OUTDIR)\sample.dll" 
LINK32_OBJS= \
	"$(INTDIR)\sample.obj"

"$(OUTDIR)\sample.dll" : "$(OUTDIR)" $(DEF_FILE) $(LINK32_OBJS)
    $(LINK32) @<<
  $(LINK32_FLAGS) $(LINK32_OBJS)
<<

!ELSEIF  "$(CFG)" == "sample - Win64 Debug"

OUTDIR=.\bin_debug
INTDIR=.\bin_debug_sample
# Begin Custom Macros
OutDir=.\bin_debug
# End Custom Macros

ALL : "$(OUTDIR)\sample.dll" "$(OUTDIR)\sample.bsc"


CLEAN :
	-@erase "$(INTDIR)\sample.obj"
	-@erase "$(OUTDIR)\sample.dll"
	-@erase "$(OUTDIR)\sample.ilk"
	-@erase "$(OUTDIR)\sample.pdb"

"$(OUTDIR)" :
    if not exist "$(OUTDIR)/$(NULL)" mkdir "$(OUTDIR)"

"$(INTDIR)" :
    if not exist "$(INTDIR)/$(NULL)" mkdir "$(INTDIR)"

CPP=icl.exe
CPP_PROJ=/nologo /MTd /W3 /GX /Zi /Od /D "_DEBUG" /D "WIN32" /D "WIN64" /D "_CONSOLE" /D "UNICODE" /D "WOD_REALMS" /FR"$(INTDIR)\\" /Fp"$(INTDIR)\sample.pch" /YX /Fo"$(INTDIR)\\" /Fd"$(INTDIR)\\" /FD /G7 /GZ /c 

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
LINK32_FLAGS=kernel32.lib user32.lib gdi32.lib wsock32.lib "..\bin_release\libmux.lib" /nologo /subsystem:console /dll /incremental:yes /pdb:"$(OUTDIR)\sample.pdb" /debug /machine:amd64 /out:"$(OUTDIR)\sample.dll" /pdbtype:sept 
LINK32_OBJS= \
	"$(INTDIR)\sample.obj"

"$(OUTDIR)\sample.dll" : "$(OUTDIR)" $(DEF_FILE) $(LINK32_OBJS)
    $(LINK32) @<<
  $(LINK32_FLAGS) $(LINK32_OBJS)
<<

!ENDIF 


!IF "$(NO_EXTERNAL_DEPS)" != "1"
!IF EXISTS("sample.dep")
!INCLUDE "sample.dep"
!ELSE 
!MESSAGE Warning: cannot find "sample.dep"
!ENDIF 
!ENDIF 


!IF "$(CFG)" == "sample - Win64 Release" || "$(CFG)" == "sample - Win64 Debug"
SOURCE=.\sample.cpp

"$(INTDIR)\sample.obj": $(SOURCE) "$(INTDIR)"


!ENDIF 

