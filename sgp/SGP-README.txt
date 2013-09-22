README file - Sandbox Global Project
V 1.0 - August 1 2000

**********************************************************************

The SGP is a project concieved to fill the need for a quickly ported 
and ready to use globals system for MUSH-type server platforms.  
It is meant as a basis for the building of a relatively clean and 
efficient Master Room.  The system is meant to provide a group of 
organized code systems and objects upon which to build a Master Room, 
not provide a complete game out of the box.  It is the beginning of 
the softcode system for your MUSH that will save your coder a lot of 
work.  It provides you with the basics, to which you can add your own 
code.   

This code is released as is.  What this means is that the 
code has been tested to the best of our ability and may still have a 
few small problems.  The maintainers make no warranty as to its 
function beyond the environments where we have personally tested it.
Expect updates.  Make the project better -- report 
bugs.  

This code is the work of many people, whom we have made every effort 
to properly credit.  Distribution is permissible so long as credit is 
given to both SGP and the original authors.  Distribution of SGP may 
be done as long as the credits remain in tact.  

**********************************************************************
A note about Installing this code on your game:
----------------------------------------------

SGP 1.0 is released 'as-is'.    You should not be required to edit the 
installer files before quoting SGP on target platforms. If you have made 
substantive changes to your server, then you may experience problems.

There may be some undiscovered bugs in the code.  We tried very hard 
to ferret them out before release, but may simply have overlooked 
something. For our purposes, a bug is a problem with either the 
vanilla installer or the code itself that prevents its function on 
your game. If your coder or someone else made alterations to the code, 
then we probably cannot help you. 
    
Report problems and make suggestions and feature requests at:

          http://sandbox.erisian.net/sgp/SGPbugs.html. 
          
SGP 1.0 has been installed on and run normally under the following 
platforms:

TinyMUSH 2.2.2 or later
TinyMUSH 2.2.4U1
TinyMUX 1.4 or later
PennMUSH 1.7 or later (1.7.2p26 for the places installations)
TinyMUX 2.0
RhostMUSH

Due to changes in the TinyMUSH 3.0 platform that break key function-
ality, SGP is not being tested on that platform.

If you have success on other platforms with the current release of 
SGP, please email <keaeris@erisian.net> with the version, patchlevel, 
and any mods to the server code.

**********************************************************************
System Requirements:
-------------------

-You must have a compiled and running MUSH server to use this code. 
-You must have wizard permissions on the game. 
-You must set the following configuration parameters in your .conf 
file in the master account: 

     master_room
     access @function wizard
     access @function/priviledged wizard

-Have a MUSH client that allows for the uploading of files. 
-For TinyMUSH and MUX, use @admin to set the player_queue_limit to 
1000. PennMUSH does not require this. 

**********************************************************************
Installation:
------------

Each of the target platforms has slightly different requirements. To 
assure that the system works as is on Penn, TM 2.2, and MUX, the 
installer checks for the version of the server before making certain 
decisions. SGP will work as a totally softcoded system on all target 
platforms, given appropriate changes to the server configuration. MUX 
users wishing to use the softcode only will need to consult their 
server documentation for instructions on how to disable +help and 
+shelp. 

The files required on vanilla implementations of the target servers 
are listed below. 

Files required for installation of SGP Base Globals on TinyMUSH 2.2.x 
servers:

-SGP-README.txt - Read before installing the softcode. 
   SGP-Installer.txt 
   SGP-+help.txt 
   SGP-RPpack.txt (optional)
   PLACES-SGP.txt (optional, but +help is included in base install) 

Files required for installation of SGP Base Globals on PennMUSH 1.7.2 
and later servers:

-SGP-README.txt - Read before installing the softcode. 
   SGP-Installer.txt 
   SGP-+help.txt 
   SGP-RPpack.txt (optional)
   PLACES-SGP.txt (optional, but +help is included in base install) 

Files required for installation of SGP Base Globals on MUX 2.0.x 
servers:

-SGP-README.txt - Read before installing the softcode. 
   SGP-Installer.txt 
   SGP-plushelp.txt 
   SGP-staffhelp.txt 
   SGP-RPpack.txt (optional)
   PLACES-SGP.txt (optional, but +help is included in base install) 

Files required for installation of SGP Base Globals on RhostMUSH 
and later servers:

-SGP-README.txt - Read before installing the softcode. 
   SGP-Installer.txt 
   SGP-+help.txt 
   SGP-RPpack.txt (optional)
   PLACES-SGP.txt (optional, but +help is included in base install) 

Make sure that you quote the file while you are in the master room of 
the game, since the installer drops the objects and it will save you 
having to teleport them later.

Note that all code should be installed, file by file, in the order 
listed. In Penn and TinyMUSH 2.2, installing the globals files out of 
order will result in incorrectly installed helpfiles. 
  
**********************************************************************
Customization:
-------------

While this softcode package doesn't require customization to run on 
your game, it is well worth your time to consider how you can best use 
ascii and other touches to give SGP the unique look and feel that you 
want your game to have.  Even if you don't choose to alter the 
existing package, you will need to make sure that the 'mudname' 
variable in your configuration files is set to the name you want 
displayed in any code that calls mudname().

We have included placeholder-style headers and footers that will can 
be changes to suit your needs.  As well, there are a number of pre-
generated header and footer attributes on the Global Parent Object.  
More complex ascii art can be created and converted to MUSHCode at 
various places on the internet.  There may be a Perl filter available 
for this kind of work in the future.  Watch the SGP homepage for 
details.

NOTE:  Keep track of the attributes you customize in a safe place.  
Some bug fixes will require you to hand edit heavily customized code 
and as such, you will need to comment out new code in the installers.  

**********************************************************************
Credits:
-------

Audumla@Sandbox - Scope and Design.  RPT code.
BEM@Granite     - Many many many of the base globals.  
                  Project Status Tracker
                  WoD Dice Code
                  +help system
Angel           - PLACES upgrades, CNOTES, and meticulous recoding of 
                  certain commands.
David@Sandbox   - +motd system
Megiddo         - Coding and recoding certain commands.
Lilith@Detroit  - Registration code.
Ian@BrazilMUX   - +3who

Ashen-Shugar, Brazil, Talek, Javelin have contributed heavily in terms 
of help and advice on how to best get SGP working seamlessly on their 
servers.

Paul@M*U*S*H, Corum, Vexon@M*U*S*H, Trispis@M*U*S*H, Raevnos@M*U*S*H 
all contributed bug reports and fixes to the fray.

**********************************************************************
Softcoded Mailers:
-----------------

For all games using a softcoded mailer, you will need to patch the 
+finger code so that the mail attributes are read properly.  There 
will be a mail() function added for TinyMUSH games using either 
Fluff's Mailer or Brandy Mail.  Watch the website for details.

**********************************************************************
CHANGES
-------

as of:  16 Jun 00
-- Major tweaks in the installer.  SGP now installs and runs cleanly on 
Tiny 2.2.X, MUX 2.0, and Penn  There are no reported bugs in existing 
commands.
-- Helpfiles are updated for MUX.
-- Helpfiles and +help code to be added for Penn and Tiny 2.2.X
-- Added the following commands:
   ooc and '    - <OOC> emits
   @register    - Command for registration games.
   +register    - Staff version of the registration command.
   +where       - A simple +where command
   +3who        - A 3 column +who
   +shout       - Emits a shout to nearby rooms.
   +stnotes     - Sets judgenotes.  Differs from +cnote in that it is staff-
              only.(RPPack)
   +timestamp   - Sets generic timestamps on player objects.

as of: 26 Jun 00

-- Added the following commands:
   +help       - Added for Penn and TinyMUSH.(+help system)
   +timestop   - Creates a timestop object.(RPPack)
   +resume     - Destroys timestop object.(RPPack)
   +warn       - Issues a generic warning for RP purposes.(RPPack)
-- Fixed a problem with reading mail attributes in MUX and Penn.  
Provided a version for TinyMUSH that uses Fluff's Mailer.  Waiting on 
a BrandyMail version.
-- Cleaned up PLACES installer, added status emits.
-- Separated RPPack code from the main installer in prep for release.
-- Cleaned up erratic bits of code.

as of: 28 Jun 00
-- Amended installer code for compatability with RHostMUSH.  
-- SGP needs to be reinstalled for games using a version before 28 Jun 00.
-- Fixed a problem with DO_COL that caused +help and +wizhelp to not 
work on Penn.  Thanks to Raevnos@M*U*S*H. 
-- Fixed a problem with the PLACES installer in the @switch that 
controlled flags and the @startup.
-- Fixed a problem with +resume in Penn that prevented the nuking of 
timestop objects.  Thanks to Trispis@M*U*S*H.
-- Posted a workaround for +where problem in Rhost at the bugtracking 
page.  Amended the installer to make the parent object INHERIT for 
Rhost installs.
-- Fixed an object type problem that prevented the use of the 
+timestamp code in Penn.  Thanks to Raevnos@M*U*S*H.

as of: 3 July 00
-- Fixed several typos and amended a few lines of code in the that 
coded checked for flags explicitly.  +who, +lwho, +spy(PLACES) and a 
few other commands were affected. Thanks to Ashen-Shugar for the 
reports. 
-- Fixed a legacy bug in mutter.(PLACES) Thanks to Ashen-Shugar for the 
report and fix.

as of: 11 July 00
-- Fixed a bug with @verb in PLACEFUNCTION(PLACES) that was causing 
JOIN and OJOIN messages to not work properly in Penn.  Thanks to 
Vexon@M*U*S*H for the report and Corum for the fix. 
-- Removed an unneeded } from PLACEFUNCTION(PLACES) that was wreaking 
havoc with the code.
-- Fixed a problem with escapes in DO_TT and DO_TTOOC(PLACES) that was 
causing the commands to not work in Penn.  Thanks to Vexon@M*U*S*H for 
the report and Corum for the fix. 

as of: 20 July 00
-- Added +motd code.
-- Finally fixed the problem with +3who.  Thanks to Ian@BrazilMUX for 
the fix.
-- Added +who *.
-- Fixed a typo in FN_ANDLIST.

as of: 1 August 00
-- Removed the beta markings and cleaned up a couple typos.
**********************************************************************


