# Makefile for building AMD64 version with Intel 9.1 Compiler.
#
!IF "$(CFG)" == ""
CFG=netmux - Win64 Release
!MESSAGE No configuration specified. Defaulting to netmux - Win64 Release.
!ENDIF 

!IF "$(CFG)" != "netmux - Win64 Release" && "$(CFG)" != "netmux - Win64 Debug"
!MESSAGE Invalid configuration "$(CFG)" specified.
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "netmux.mak" CFG="netmux - Win64 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "netmux - Win64 Release"
!MESSAGE "netmux - Win64 Debug"
!MESSAGE 
!ERROR An invalid configuration is specified.
!ENDIF 

!IF "$(OS)" == "Windows_NT"
NULL=
!ELSE 
NULL=nul
!ENDIF 

!IF  "$(CFG)" == "netmux - Win64 Release"

OUTDIR=.\bin_release
INTDIR=.\bin_release_netmux
# Begin Custom Macros
OutDir=.\bin_release
# End Custom Macros

ALL : "$(OUTDIR)\netmux.exe"


CLEAN :
	-@erase "$(INTDIR)\_build.obj"
	-@erase "$(INTDIR)\alloc.obj"
	-@erase "$(INTDIR)\attrcache.obj"
	-@erase "$(INTDIR)\boolexp.obj"
	-@erase "$(INTDIR)\bsd.obj"
	-@erase "$(INTDIR)\command.obj"
	-@erase "$(INTDIR)\comsys.obj"
	-@erase "$(INTDIR)\conf.obj"
	-@erase "$(INTDIR)\cque.obj"
	-@erase "$(INTDIR)\create.obj"
	-@erase "$(INTDIR)\db.obj"
	-@erase "$(INTDIR)\db_rw.obj"
	-@erase "$(INTDIR)\eval.obj"
	-@erase "$(INTDIR)\file_c.obj"
	-@erase "$(INTDIR)\flags.obj"
	-@erase "$(INTDIR)\funceval.obj"
	-@erase "$(INTDIR)\functions.obj"
	-@erase "$(INTDIR)\funmath.obj"
	-@erase "$(INTDIR)\game.obj"
	-@erase "$(INTDIR)\help.obj"
	-@erase "$(INTDIR)\htab.obj"
	-@erase "$(INTDIR)\levels.obj"
	-@erase "$(INTDIR)\local.obj"
	-@erase "$(INTDIR)\log.obj"
	-@erase "$(INTDIR)\look.obj"
	-@erase "$(INTDIR)\mail.obj"
	-@erase "$(INTDIR)\match.obj"
	-@erase "$(INTDIR)\mguests.obj"
	-@erase "$(INTDIR)\move.obj"
	-@erase "$(INTDIR)\muxcli.obj"
	-@erase "$(INTDIR)\netcommon.obj"
	-@erase "$(INTDIR)\object.obj"
	-@erase "$(INTDIR)\pcre.obj"
	-@erase "$(INTDIR)\player.obj"
	-@erase "$(INTDIR)\player_c.obj"
	-@erase "$(INTDIR)\plusemail.obj"
	-@erase "$(INTDIR)\powers.obj"
	-@erase "$(INTDIR)\predicates.obj"
	-@erase "$(INTDIR)\quota.obj"
	-@erase "$(INTDIR)\rob.obj"
	-@erase "$(INTDIR)\set.obj"
	-@erase "$(INTDIR)\sha1.obj"
	-@erase "$(INTDIR)\speech.obj"
	-@erase "$(INTDIR)\stringutil.obj"
	-@erase "$(INTDIR)\strtod.obj"
	-@erase "$(INTDIR)\svdhash.obj"
	-@erase "$(INTDIR)\svdrand.obj"
	-@erase "$(INTDIR)\svdreport.obj"
	-@erase "$(INTDIR)\timer.obj"
	-@erase "$(INTDIR)\timeutil.obj"
	-@erase "$(INTDIR)\unparse.obj"
	-@erase "$(INTDIR)\vattr.obj"
	-@erase "$(INTDIR)\version.obj"
	-@erase "$(INTDIR)\walkdb.obj"
	-@erase "$(INTDIR)\wild.obj"
	-@erase "$(INTDIR)\wiz.obj"
	-@erase "$(OUTDIR)\netmux.exe"

"$(OUTDIR)" :
    if not exist "$(OUTDIR)/$(NULL)" mkdir "$(OUTDIR)"

"$(INTDIR)" :
    if not exist "$(INTDIR)/$(NULL)" mkdir "$(INTDIR)"

CPP=icl.exe
CPP_PROJ=/nologo /MT /W3 /GX /Ot /Oa /Og /Oi /Ob2 /Gy /D "NDEBUG" /D "WIN32" /D "WIN64" /D "_CONSOLE" /D "_MBCS" /D "WOD_REALMS" /FR"$(INTDIR)\\" /Fp"$(INTDIR)\netmux.pch" /YX /Fo"$(INTDIR)\\" /Fd"$(INTDIR)\\" /FD /G7 /GF /O3 /Qprec_div /Qipo2 /Qsfalign8 /Qparallel /Qpar_threshold50 /Qunroll /Qprof_use /c 

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
LINK32_FLAGS=kernel32.lib user32.lib gdi32.lib wsock32.lib /nologo /version:2.6 /subsystem:console /incremental:no /pdb:"$(OUTDIR)\netmux.pdb" /machine:amd64 /out:"$(OUTDIR)\netmux.exe" 
LINK32_OBJS= \
	"$(INTDIR)\_build.obj" \
	"$(INTDIR)\alloc.obj" \
	"$(INTDIR)\attrcache.obj" \
	"$(INTDIR)\boolexp.obj" \
	"$(INTDIR)\bsd.obj" \
	"$(INTDIR)\command.obj" \
	"$(INTDIR)\comsys.obj" \
	"$(INTDIR)\conf.obj" \
	"$(INTDIR)\cque.obj" \
	"$(INTDIR)\create.obj" \
	"$(INTDIR)\db.obj" \
	"$(INTDIR)\db_rw.obj" \
	"$(INTDIR)\eval.obj" \
	"$(INTDIR)\file_c.obj" \
	"$(INTDIR)\flags.obj" \
	"$(INTDIR)\funceval.obj" \
	"$(INTDIR)\functions.obj" \
	"$(INTDIR)\funmath.obj" \
	"$(INTDIR)\game.obj" \
	"$(INTDIR)\help.obj" \
	"$(INTDIR)\htab.obj" \
	"$(INTDIR)\levels.obj" \
	"$(INTDIR)\local.obj" \
	"$(INTDIR)\log.obj" \
	"$(INTDIR)\look.obj" \
	"$(INTDIR)\mail.obj" \
	"$(INTDIR)\match.obj" \
	"$(INTDIR)\mguests.obj" \
	"$(INTDIR)\move.obj" \
	"$(INTDIR)\muxcli.obj" \
	"$(INTDIR)\netcommon.obj" \
	"$(INTDIR)\object.obj" \
	"$(INTDIR)\pcre.obj" \
	"$(INTDIR)\player.obj" \
	"$(INTDIR)\player_c.obj" \
	"$(INTDIR)\plusemail.obj" \
	"$(INTDIR)\powers.obj" \
	"$(INTDIR)\predicates.obj" \
	"$(INTDIR)\quota.obj" \
	"$(INTDIR)\rob.obj" \
	"$(INTDIR)\set.obj" \
	"$(INTDIR)\sha1.obj" \
	"$(INTDIR)\speech.obj" \
	"$(INTDIR)\stringutil.obj" \
	"$(INTDIR)\strtod.obj" \
	"$(INTDIR)\svdhash.obj" \
	"$(INTDIR)\svdrand.obj" \
	"$(INTDIR)\svdreport.obj" \
	"$(INTDIR)\timer.obj" \
	"$(INTDIR)\timeutil.obj" \
	"$(INTDIR)\unparse.obj" \
	"$(INTDIR)\vattr.obj" \
	"$(INTDIR)\version.obj" \
	"$(INTDIR)\walkdb.obj" \
	"$(INTDIR)\wild.obj" \
	"$(INTDIR)\wiz.obj"

"$(OUTDIR)\netmux.exe" : "$(OUTDIR)" $(DEF_FILE) $(LINK32_OBJS)
    $(LINK32) @<<
  $(LINK32_FLAGS) $(LINK32_OBJS)
<<

!ELSEIF  "$(CFG)" == "netmux - Win64 Debug"

OUTDIR=.\bin_debug
INTDIR=.\bin_debug_netmux
# Begin Custom Macros
OutDir=.\bin_debug
# End Custom Macros

ALL : "$(OUTDIR)\netmux.exe" "$(OUTDIR)\netmux.bsc"


CLEAN :
	-@erase "$(INTDIR)\_build.obj"
	-@erase "$(INTDIR)\alloc.obj"
	-@erase "$(INTDIR)\attrcache.obj"
	-@erase "$(INTDIR)\boolexp.obj"
	-@erase "$(INTDIR)\bsd.obj"
	-@erase "$(INTDIR)\command.obj"
	-@erase "$(INTDIR)\comsys.obj"
	-@erase "$(INTDIR)\conf.obj"
	-@erase "$(INTDIR)\cque.obj"
	-@erase "$(INTDIR)\create.obj"
	-@erase "$(INTDIR)\db.obj"
	-@erase "$(INTDIR)\db_rw.obj"
	-@erase "$(INTDIR)\eval.obj"
	-@erase "$(INTDIR)\file_c.obj"
	-@erase "$(INTDIR)\flags.obj"
	-@erase "$(INTDIR)\funceval.obj"
	-@erase "$(INTDIR)\functions.obj"
	-@erase "$(INTDIR)\funmath.obj"
	-@erase "$(INTDIR)\game.obj"
	-@erase "$(INTDIR)\help.obj"
	-@erase "$(INTDIR)\htab.obj"
	-@erase "$(INTDIR)\levels.obj"
	-@erase "$(INTDIR)\local.obj"
	-@erase "$(INTDIR)\log.obj"
	-@erase "$(INTDIR)\look.obj"
	-@erase "$(INTDIR)\mail.obj"
	-@erase "$(INTDIR)\match.obj"
	-@erase "$(INTDIR)\mguests.obj"
	-@erase "$(INTDIR)\move.obj"
	-@erase "$(INTDIR)\muxcli.obj"
	-@erase "$(INTDIR)\netcommon.obj"
	-@erase "$(INTDIR)\object.obj"
	-@erase "$(INTDIR)\pcre.obj"
	-@erase "$(INTDIR)\player.obj"
	-@erase "$(INTDIR)\player_c.obj"
	-@erase "$(INTDIR)\plusemail.obj"
	-@erase "$(INTDIR)\powers.obj"
	-@erase "$(INTDIR)\predicates.obj"
	-@erase "$(INTDIR)\quota.obj"
	-@erase "$(INTDIR)\rob.obj"
	-@erase "$(INTDIR)\set.obj"
	-@erase "$(INTDIR)\sha1.obj"
	-@erase "$(INTDIR)\speech.obj"
	-@erase "$(INTDIR)\stringutil.obj"
	-@erase "$(INTDIR)\strtod.obj"
	-@erase "$(INTDIR)\svdhash.obj"
	-@erase "$(INTDIR)\svdrand.obj"
	-@erase "$(INTDIR)\svdreport.obj"
	-@erase "$(INTDIR)\timer.obj"
	-@erase "$(INTDIR)\timeutil.obj"
	-@erase "$(INTDIR)\unparse.obj"
	-@erase "$(INTDIR)\vattr.obj"
	-@erase "$(INTDIR)\vc60.idb"
	-@erase "$(INTDIR)\vc60.pdb"
	-@erase "$(INTDIR)\version.obj"
	-@erase "$(INTDIR)\walkdb.obj"
	-@erase "$(INTDIR)\wild.obj"
	-@erase "$(INTDIR)\wiz.obj"
	-@erase "$(OUTDIR)\netmux.bsc"
	-@erase "$(OUTDIR)\netmux.exe"
	-@erase "$(OUTDIR)\netmux.ilk"
	-@erase "$(OUTDIR)\netmux.pdb"

"$(OUTDIR)" :
    if not exist "$(OUTDIR)/$(NULL)" mkdir "$(OUTDIR)"

"$(INTDIR)" :
    if not exist "$(INTDIR)/$(NULL)" mkdir "$(INTDIR)"

CPP=icl.exe
CPP_PROJ=/nologo /MTd /W3 /GX /Zi /Od /D "_DEBUG" /D "WIN32" /D "WIN64" /D "_CONSOLE" /D "_MBCS" /D "WOD_REALMS" /FR"$(INTDIR)\\" /Fp"$(INTDIR)\netmux.pch" /YX /Fo"$(INTDIR)\\" /Fd"$(INTDIR)\\" /FD /G7 /GZ /c 

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
LINK32_FLAGS=kernel32.lib user32.lib gdi32.lib wsock32.lib /nologo /subsystem:console /incremental:yes /pdb:"$(OUTDIR)\netmux.pdb" /debug /machine:amd64 /out:"$(OUTDIR)\netmux.exe" /pdbtype:sept 
LINK32_OBJS= \
	"$(INTDIR)\_build.obj" \
	"$(INTDIR)\alloc.obj" \
	"$(INTDIR)\attrcache.obj" \
	"$(INTDIR)\boolexp.obj" \
	"$(INTDIR)\bsd.obj" \
	"$(INTDIR)\command.obj" \
	"$(INTDIR)\comsys.obj" \
	"$(INTDIR)\conf.obj" \
	"$(INTDIR)\cque.obj" \
	"$(INTDIR)\create.obj" \
	"$(INTDIR)\db.obj" \
	"$(INTDIR)\db_rw.obj" \
	"$(INTDIR)\eval.obj" \
	"$(INTDIR)\file_c.obj" \
	"$(INTDIR)\flags.obj" \
	"$(INTDIR)\funceval.obj" \
	"$(INTDIR)\functions.obj" \
	"$(INTDIR)\funmath.obj" \
	"$(INTDIR)\game.obj" \
	"$(INTDIR)\help.obj" \
	"$(INTDIR)\htab.obj" \
	"$(INTDIR)\levels.obj" \
	"$(INTDIR)\local.obj" \
	"$(INTDIR)\log.obj" \
	"$(INTDIR)\look.obj" \
	"$(INTDIR)\mail.obj" \
	"$(INTDIR)\match.obj" \
	"$(INTDIR)\mguests.obj" \
	"$(INTDIR)\move.obj" \
	"$(INTDIR)\muxcli.obj" \
	"$(INTDIR)\netcommon.obj" \
	"$(INTDIR)\object.obj" \
	"$(INTDIR)\pcre.obj" \
	"$(INTDIR)\player.obj" \
	"$(INTDIR)\player_c.obj" \
	"$(INTDIR)\plusemail.obj" \
	"$(INTDIR)\powers.obj" \
	"$(INTDIR)\predicates.obj" \
	"$(INTDIR)\quota.obj" \
	"$(INTDIR)\rob.obj" \
	"$(INTDIR)\set.obj" \
	"$(INTDIR)\sha1.obj" \
	"$(INTDIR)\speech.obj" \
	"$(INTDIR)\stringutil.obj" \
	"$(INTDIR)\strtod.obj" \
	"$(INTDIR)\svdhash.obj" \
	"$(INTDIR)\svdrand.obj" \
	"$(INTDIR)\svdreport.obj" \
	"$(INTDIR)\timer.obj" \
	"$(INTDIR)\timeutil.obj" \
	"$(INTDIR)\unparse.obj" \
	"$(INTDIR)\vattr.obj" \
	"$(INTDIR)\version.obj" \
	"$(INTDIR)\walkdb.obj" \
	"$(INTDIR)\wild.obj" \
	"$(INTDIR)\wiz.obj"

"$(OUTDIR)\netmux.exe" : "$(OUTDIR)" $(DEF_FILE) $(LINK32_OBJS)
    $(LINK32) @<<
  $(LINK32_FLAGS) $(LINK32_OBJS)
<<

!ENDIF 


!IF "$(NO_EXTERNAL_DEPS)" != "1"
!IF EXISTS("netmux.dep")
!INCLUDE "netmux.dep"
!ELSE 
!MESSAGE Warning: cannot find "netmux.dep"
!ENDIF 
!ENDIF 


!IF "$(CFG)" == "netmux - Win64 Release" || "$(CFG)" == "netmux - Win64 Debug"
SOURCE=.\_build.cpp

"$(INTDIR)\_build.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\alloc.cpp

"$(INTDIR)\alloc.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\attrcache.cpp

"$(INTDIR)\attrcache.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\boolexp.cpp

"$(INTDIR)\boolexp.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\bsd.cpp

"$(INTDIR)\bsd.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\command.cpp

"$(INTDIR)\command.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\comsys.cpp

"$(INTDIR)\comsys.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\conf.cpp

"$(INTDIR)\conf.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\cque.cpp

"$(INTDIR)\cque.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\create.cpp

"$(INTDIR)\create.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\db.cpp

"$(INTDIR)\db.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\db_rw.cpp

"$(INTDIR)\db_rw.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\eval.cpp

"$(INTDIR)\eval.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\file_c.cpp

"$(INTDIR)\file_c.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\flags.cpp

"$(INTDIR)\flags.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\funceval.cpp

"$(INTDIR)\funceval.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\functions.cpp

"$(INTDIR)\functions.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\funmath.cpp

"$(INTDIR)\funmath.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\game.cpp

"$(INTDIR)\game.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\help.cpp

"$(INTDIR)\help.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\htab.cpp

"$(INTDIR)\htab.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\levels.cpp

"$(INTDIR)\levels.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\local.cpp

"$(INTDIR)\local.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\log.cpp

"$(INTDIR)\log.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\look.cpp

"$(INTDIR)\look.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\mail.cpp

"$(INTDIR)\mail.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\match.cpp

"$(INTDIR)\match.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\mguests.cpp

"$(INTDIR)\mguests.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\move.cpp

"$(INTDIR)\move.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\muxcli.cpp

"$(INTDIR)\muxcli.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\netcommon.cpp

"$(INTDIR)\netcommon.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\object.cpp

"$(INTDIR)\object.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\pcre.cpp

"$(INTDIR)\pcre.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\player.cpp

"$(INTDIR)\player.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\player_c.cpp

"$(INTDIR)\player_c.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\plusemail.cpp

"$(INTDIR)\plusemail.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\powers.cpp

"$(INTDIR)\powers.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\predicates.cpp

"$(INTDIR)\predicates.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\quota.cpp

"$(INTDIR)\quota.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\rob.cpp

"$(INTDIR)\rob.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\set.cpp

"$(INTDIR)\set.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\sha1.cpp

"$(INTDIR)\sha1.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\speech.cpp

"$(INTDIR)\speech.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\stringutil.cpp

"$(INTDIR)\stringutil.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\strtod.cpp

"$(INTDIR)\strtod.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\svdhash.cpp

"$(INTDIR)\svdhash.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\svdrand.cpp

"$(INTDIR)\svdrand.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\svdreport.cpp

"$(INTDIR)\svdreport.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\timer.cpp

"$(INTDIR)\timer.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\timeutil.cpp

"$(INTDIR)\timeutil.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\unparse.cpp

"$(INTDIR)\unparse.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\vattr.cpp

"$(INTDIR)\vattr.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\version.cpp

"$(INTDIR)\version.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\walkdb.cpp

"$(INTDIR)\walkdb.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\wild.cpp

"$(INTDIR)\wild.obj": $(SOURCE) "$(INTDIR)"


SOURCE=.\wiz.cpp

"$(INTDIR)\wiz.obj": $(SOURCE) "$(INTDIR)"


!ENDIF 

