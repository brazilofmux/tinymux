# Microsoft Developer Studio Generated NMAKE File, Based on netmux.dsp
!IF "$(CFG)" == ""
CFG=netmux - Win64 Debug
!MESSAGE No configuration specified. Defaulting to netmux - Win64 Debug.
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
!MESSAGE "netmux - Win64 Release" (based on "Win64 (x86) Console Application")
!MESSAGE "netmux - Win64 Debug" (based on "Win64 (x86) Console Application")
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

.c{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cpp{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cxx{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

RSC=rc.exe

LINK32=xilink.exe
LINK32_FLAGS=kernel32.lib user32.lib gdi32.lib wsock32.lib /nologo /version:2.4 /subsystem:console /incremental:no /pdb:"$(OUTDIR)\netmux.pdb" /machine:amd64 /out:"$(OUTDIR)\netmux.exe" 
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
	-@erase "$(INTDIR)\_build.sbr"
	-@erase "$(INTDIR)\alloc.obj"
	-@erase "$(INTDIR)\alloc.sbr"
	-@erase "$(INTDIR)\attrcache.obj"
	-@erase "$(INTDIR)\attrcache.sbr"
	-@erase "$(INTDIR)\boolexp.obj"
	-@erase "$(INTDIR)\boolexp.sbr"
	-@erase "$(INTDIR)\bsd.obj"
	-@erase "$(INTDIR)\bsd.sbr"
	-@erase "$(INTDIR)\command.obj"
	-@erase "$(INTDIR)\command.sbr"
	-@erase "$(INTDIR)\comsys.obj"
	-@erase "$(INTDIR)\comsys.sbr"
	-@erase "$(INTDIR)\conf.obj"
	-@erase "$(INTDIR)\conf.sbr"
	-@erase "$(INTDIR)\cque.obj"
	-@erase "$(INTDIR)\cque.sbr"
	-@erase "$(INTDIR)\create.obj"
	-@erase "$(INTDIR)\create.sbr"
	-@erase "$(INTDIR)\db.obj"
	-@erase "$(INTDIR)\db.sbr"
	-@erase "$(INTDIR)\db_rw.obj"
	-@erase "$(INTDIR)\db_rw.sbr"
	-@erase "$(INTDIR)\eval.obj"
	-@erase "$(INTDIR)\eval.sbr"
	-@erase "$(INTDIR)\file_c.obj"
	-@erase "$(INTDIR)\file_c.sbr"
	-@erase "$(INTDIR)\flags.obj"
	-@erase "$(INTDIR)\flags.sbr"
	-@erase "$(INTDIR)\funceval.obj"
	-@erase "$(INTDIR)\funceval.sbr"
	-@erase "$(INTDIR)\functions.obj"
	-@erase "$(INTDIR)\functions.sbr"
	-@erase "$(INTDIR)\funmath.obj"
	-@erase "$(INTDIR)\funmath.sbr"
	-@erase "$(INTDIR)\game.obj"
	-@erase "$(INTDIR)\game.sbr"
	-@erase "$(INTDIR)\help.obj"
	-@erase "$(INTDIR)\help.sbr"
	-@erase "$(INTDIR)\htab.obj"
	-@erase "$(INTDIR)\htab.sbr"
	-@erase "$(INTDIR)\levels.obj"
	-@erase "$(INTDIR)\levels.sbr"
	-@erase "$(INTDIR)\local.obj"
	-@erase "$(INTDIR)\local.sbr"
	-@erase "$(INTDIR)\log.obj"
	-@erase "$(INTDIR)\log.sbr"
	-@erase "$(INTDIR)\look.obj"
	-@erase "$(INTDIR)\look.sbr"
	-@erase "$(INTDIR)\mail.obj"
	-@erase "$(INTDIR)\mail.sbr"
	-@erase "$(INTDIR)\match.obj"
	-@erase "$(INTDIR)\match.sbr"
	-@erase "$(INTDIR)\mguests.obj"
	-@erase "$(INTDIR)\mguests.sbr"
	-@erase "$(INTDIR)\move.obj"
	-@erase "$(INTDIR)\move.sbr"
	-@erase "$(INTDIR)\muxcli.obj"
	-@erase "$(INTDIR)\muxcli.sbr"
	-@erase "$(INTDIR)\netcommon.obj"
	-@erase "$(INTDIR)\netcommon.sbr"
	-@erase "$(INTDIR)\object.obj"
	-@erase "$(INTDIR)\object.sbr"
	-@erase "$(INTDIR)\pcre.obj"
	-@erase "$(INTDIR)\pcre.sbr"
	-@erase "$(INTDIR)\player.obj"
	-@erase "$(INTDIR)\player.sbr"
	-@erase "$(INTDIR)\player_c.obj"
	-@erase "$(INTDIR)\player_c.sbr"
	-@erase "$(INTDIR)\plusemail.obj"
	-@erase "$(INTDIR)\plusemail.sbr"
	-@erase "$(INTDIR)\powers.obj"
	-@erase "$(INTDIR)\powers.sbr"
	-@erase "$(INTDIR)\predicates.obj"
	-@erase "$(INTDIR)\predicates.sbr"
	-@erase "$(INTDIR)\quota.obj"
	-@erase "$(INTDIR)\quota.sbr"
	-@erase "$(INTDIR)\rob.obj"
	-@erase "$(INTDIR)\rob.sbr"
	-@erase "$(INTDIR)\set.obj"
	-@erase "$(INTDIR)\set.sbr"
	-@erase "$(INTDIR)\sha1.obj"
	-@erase "$(INTDIR)\sha1.sbr"
	-@erase "$(INTDIR)\speech.obj"
	-@erase "$(INTDIR)\speech.sbr"
	-@erase "$(INTDIR)\stringutil.obj"
	-@erase "$(INTDIR)\stringutil.sbr"
	-@erase "$(INTDIR)\strtod.obj"
	-@erase "$(INTDIR)\strtod.sbr"
	-@erase "$(INTDIR)\svdhash.obj"
	-@erase "$(INTDIR)\svdhash.sbr"
	-@erase "$(INTDIR)\svdrand.obj"
	-@erase "$(INTDIR)\svdrand.sbr"
	-@erase "$(INTDIR)\svdreport.obj"
	-@erase "$(INTDIR)\svdreport.sbr"
	-@erase "$(INTDIR)\timer.obj"
	-@erase "$(INTDIR)\timer.sbr"
	-@erase "$(INTDIR)\timeutil.obj"
	-@erase "$(INTDIR)\timeutil.sbr"
	-@erase "$(INTDIR)\unparse.obj"
	-@erase "$(INTDIR)\unparse.sbr"
	-@erase "$(INTDIR)\vattr.obj"
	-@erase "$(INTDIR)\vattr.sbr"
	-@erase "$(INTDIR)\vc60.idb"
	-@erase "$(INTDIR)\vc60.pdb"
	-@erase "$(INTDIR)\version.obj"
	-@erase "$(INTDIR)\version.sbr"
	-@erase "$(INTDIR)\walkdb.obj"
	-@erase "$(INTDIR)\walkdb.sbr"
	-@erase "$(INTDIR)\wild.obj"
	-@erase "$(INTDIR)\wild.sbr"
	-@erase "$(INTDIR)\wiz.obj"
	-@erase "$(INTDIR)\wiz.sbr"
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

.c{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cpp{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cxx{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

RSC=rc.exe

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

"$(INTDIR)\_build.obj"	"$(INTDIR)\_build.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\alloc.cpp

"$(INTDIR)\alloc.obj"	"$(INTDIR)\alloc.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\attrcache.cpp

"$(INTDIR)\attrcache.obj"	"$(INTDIR)\attrcache.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\boolexp.cpp

"$(INTDIR)\boolexp.obj"	"$(INTDIR)\boolexp.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\bsd.cpp

"$(INTDIR)\bsd.obj"	"$(INTDIR)\bsd.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\command.cpp

"$(INTDIR)\command.obj"	"$(INTDIR)\command.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\comsys.cpp

"$(INTDIR)\comsys.obj"	"$(INTDIR)\comsys.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\conf.cpp

"$(INTDIR)\conf.obj"	"$(INTDIR)\conf.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\cque.cpp

"$(INTDIR)\cque.obj"	"$(INTDIR)\cque.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\create.cpp

"$(INTDIR)\create.obj"	"$(INTDIR)\create.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\db.cpp

"$(INTDIR)\db.obj"	"$(INTDIR)\db.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\db_rw.cpp

"$(INTDIR)\db_rw.obj"	"$(INTDIR)\db_rw.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\eval.cpp

"$(INTDIR)\eval.obj"	"$(INTDIR)\eval.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\file_c.cpp

"$(INTDIR)\file_c.obj"	"$(INTDIR)\file_c.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\flags.cpp

"$(INTDIR)\flags.obj"	"$(INTDIR)\flags.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\funceval.cpp

"$(INTDIR)\funceval.obj"	"$(INTDIR)\funceval.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\functions.cpp

"$(INTDIR)\functions.obj"	"$(INTDIR)\functions.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\funmath.cpp

"$(INTDIR)\funmath.obj"	"$(INTDIR)\funmath.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\game.cpp

"$(INTDIR)\game.obj"	"$(INTDIR)\game.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\help.cpp

"$(INTDIR)\help.obj"	"$(INTDIR)\help.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\htab.cpp

"$(INTDIR)\htab.obj"	"$(INTDIR)\htab.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\levels.cpp

"$(INTDIR)\levels.obj"	"$(INTDIR)\levels.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\local.cpp

"$(INTDIR)\local.obj"	"$(INTDIR)\local.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\log.cpp

"$(INTDIR)\log.obj"	"$(INTDIR)\log.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\look.cpp

"$(INTDIR)\look.obj"	"$(INTDIR)\look.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\mail.cpp

"$(INTDIR)\mail.obj"	"$(INTDIR)\mail.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\match.cpp

"$(INTDIR)\match.obj"	"$(INTDIR)\match.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\mguests.cpp

"$(INTDIR)\mguests.obj"	"$(INTDIR)\mguests.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\move.cpp

"$(INTDIR)\move.obj"	"$(INTDIR)\move.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\muxcli.cpp

"$(INTDIR)\muxcli.obj"	"$(INTDIR)\muxcli.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\netcommon.cpp

"$(INTDIR)\netcommon.obj"	"$(INTDIR)\netcommon.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\object.cpp

"$(INTDIR)\object.obj"	"$(INTDIR)\object.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\pcre.cpp

"$(INTDIR)\pcre.obj"	"$(INTDIR)\pcre.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\player.cpp

"$(INTDIR)\player.obj"	"$(INTDIR)\player.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\player_c.cpp

"$(INTDIR)\player_c.obj"	"$(INTDIR)\player_c.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\plusemail.cpp

"$(INTDIR)\plusemail.obj"	"$(INTDIR)\plusemail.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\powers.cpp

"$(INTDIR)\powers.obj"	"$(INTDIR)\powers.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\predicates.cpp

"$(INTDIR)\predicates.obj"	"$(INTDIR)\predicates.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\quota.cpp

"$(INTDIR)\quota.obj"	"$(INTDIR)\quota.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\rob.cpp

"$(INTDIR)\rob.obj"	"$(INTDIR)\rob.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\set.cpp

"$(INTDIR)\set.obj"	"$(INTDIR)\set.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\sha1.cpp

"$(INTDIR)\sha1.obj"	"$(INTDIR)\sha1.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\speech.cpp

"$(INTDIR)\speech.obj"	"$(INTDIR)\speech.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\stringutil.cpp

"$(INTDIR)\stringutil.obj"	"$(INTDIR)\stringutil.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\strtod.cpp

"$(INTDIR)\strtod.obj"	"$(INTDIR)\strtod.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\svdhash.cpp

"$(INTDIR)\svdhash.obj"	"$(INTDIR)\svdhash.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\svdrand.cpp

"$(INTDIR)\svdrand.obj"	"$(INTDIR)\svdrand.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\svdreport.cpp

"$(INTDIR)\svdreport.obj"	"$(INTDIR)\svdreport.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\timer.cpp

"$(INTDIR)\timer.obj"	"$(INTDIR)\timer.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\timeutil.cpp

"$(INTDIR)\timeutil.obj"	"$(INTDIR)\timeutil.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\unparse.cpp

"$(INTDIR)\unparse.obj"	"$(INTDIR)\unparse.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\vattr.cpp

"$(INTDIR)\vattr.obj"	"$(INTDIR)\vattr.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\version.cpp

"$(INTDIR)\version.obj"	"$(INTDIR)\version.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\walkdb.cpp

"$(INTDIR)\walkdb.obj"	"$(INTDIR)\walkdb.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\wild.cpp

"$(INTDIR)\wild.obj"	"$(INTDIR)\wild.sbr" : $(SOURCE) "$(INTDIR)"


SOURCE=.\wiz.cpp

"$(INTDIR)\wiz.obj"	"$(INTDIR)\wiz.sbr" : $(SOURCE) "$(INTDIR)"



!ENDIF 

