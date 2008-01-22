# Makefile for building AMD64 version with Intel 9.1 Compiler.
#
!IF "$(CFG)" == ""
CFG=sum - Win64 Release
!MESSAGE No configuration specified. Defaulting to sum - Win64 Release.
!ENDIF 

!IF "$(CFG)" != "sum - Win64 Release" && "$(CFG)" != "sum - Win64 Debug"
!MESSAGE Invalid configuration "$(CFG)" specified.
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "sum.mak" CFG="sum - Win64 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "sum - Win64 Release"
!MESSAGE "sum - Win64 Debug"
!MESSAGE 
!ERROR An invalid configuration is specified.
!ENDIF 

!IF "$(OS)" == "Windows_NT"
NULL=
!ELSE 
NULL=nul
!ENDIF 

!IF  "$(CFG)" == "sum - Win64 Release"

OUTDIR=.\bin_release
INTDIR=.\bin_release_sum
# Begin Custom Macros
OutDir=.\bin_release
# End Custom Macros

ALL : "$(OUTDIR)\sum.dll"


CLEAN :
	-@erase "$(INTDIR)\sum.obj"
	-@erase "$(INTDIR)\vc60.idb"
	-@erase "$(OUTDIR)\sum.dll"
	-@erase "$(INTDIR)\sum.exp"
	-@erase "$(INTDIR)\sum.lib"

"$(OUTDIR)" :
    if not exist "$(OUTDIR)/$(NULL)" mkdir "$(OUTDIR)"

"$(INTDIR)" :
    if not exist "$(INTDIR)/$(NULL)" mkdir "$(INTDIR)"

CPP=icl.exe
CPP_PROJ=/nologo /MT /W3 /GX /Ot /Oa /Og /Oi /Ob2 /Gy /D "NDEBUG" /D "WIN32" /D "WIN64" /D "_CONSOLE" /D "UNICODE" /D "WOD_REALMS" /FR"$(INTDIR)\\" /Fp"$(INTDIR)\sum.pch" /YX /Fo"$(INTDIR)\\" /Fd"$(INTDIR)\\" /FD /G7 /GF /O3 /Qprec_div /Qipo2 /Qsfalign8 /Qparallel /Qpar_threshold50 /Qunroll /c 

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
LINK32_FLAGS=kernel32.lib user32.lib gdi32.lib wsock32.lib "..\bin_release\libmux.lib" /nologo /version:2.8 /subsystem:console /dll /incremental:no /pdb:"$(OUTDIR)\sum.pdb" /machine:amd64 /def:".\sum.def" /out:"$(OUTDIR)\sum.dll"
DEF_FILE= \
	".\sum.def"
LINK32_OBJS= \
	"$(INTDIR)\sum.obj" \
	"..\bin_release\libmux.lib"

"$(OUTDIR)\sum.dll" : "$(OUTDIR)" $(DEF_FILE) $(LINK32_OBJS)
    $(LINK32) @<<
  $(LINK32_FLAGS) $(LINK32_OBJS)
<<

!ELSEIF  "$(CFG)" == "sum - Win64 Debug"

OUTDIR=.\bin_debug
INTDIR=.\bin_debug_sum
# Begin Custom Macros
OutDir=.\bin_debug
# End Custom Macros

ALL : "$(OUTDIR)\sum.dll"


CLEAN :
	-@erase "$(INTDIR)\sum.obj"
	-@erase "$(INTDIR)\vc60.idb"
	-@erase "$(INTDIR)\vc60.pdb"
	-@erase "$(OUTDIR)\sum.dll"
	-@erase "$(OUTDIR)\sum.exp"
	-@erase "$(OUTDIR)\sum.ilk"
	-@erase "$(OUTDIR)\sum.lib"
	-@erase "$(OUTDIR)\sum.pdb"

"$(OUTDIR)" :
    if not exist "$(OUTDIR)/$(NULL)" mkdir "$(OUTDIR)"

"$(INTDIR)" :
    if not exist "$(INTDIR)/$(NULL)" mkdir "$(INTDIR)"

CPP=icl.exe
CPP_PROJ=/nologo /MTd /W3 /GX /Zi /Od /D "_DEBUG" /D "WIN32" /D "WIN64" /D "_CONSOLE" /D "UNICODE" /D "WOD_REALMS" /FR"$(INTDIR)\\" /Fp"$(INTDIR)\sum.pch" /YX /Fo"$(INTDIR)\\" /Fd"$(INTDIR)\\" /FD /G7 /GZ /c 

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
LINK32_FLAGS=kernel32.lib user32.lib gdi32.lib wsock32.lib "..\bin_debug\libmux.lib" /nologo /subsystem:console /dll /incremental:yes /pdb:"$(OUTDIR)\sum.pdb" /debug /machine:amd64 /def:".\sum.def" /out:"$(OUTDIR)\sum.dll" /pdbtype:sept 
DEF_FILE= \
	".\sum.def"
LINK32_OBJS= \
	"$(INTDIR)\sum.obj" \
	"..\bin_debug\libmux.lib"

"$(OUTDIR)\sum.dll" : "$(OUTDIR)" $(DEF_FILE) $(LINK32_OBJS)
    $(LINK32) @<<
  $(LINK32_FLAGS) $(LINK32_OBJS)
<<

!ENDIF 


!IF "$(NO_EXTERNAL_DEPS)" != "1"
!IF EXISTS("sum.dep")
!INCLUDE "sum.dep"
!ELSE 
!MESSAGE Warning: cannot find "sum.dep"
!ENDIF 
!ENDIF 


!IF "$(CFG)" == "sum - Win64 Release" || "$(CFG)" == "sum - Win64 Debug"
SOURCE=.\sum.cpp

"$(INTDIR)\sum.obj" : $(SOURCE) "$(INTDIR)"


!ENDIF 

