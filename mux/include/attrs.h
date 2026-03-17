/*! \file attrs.h
 * \brief Attribute definitions.
 *
 */

#ifndef ATTRS_H
#define ATTRS_H

/* Attribute flags */
constexpr int AF_ODARK    = 0x00000001; // players other than owner can't see it.
constexpr int AF_DARK     = 0x00000002; // Only #1 can see it.
constexpr int AF_WIZARD   = 0x00000004; // only wizards can change it.
constexpr int AF_MDARK    = 0x00000008; // Only wizards can see it. Dark to mortals.
constexpr int AF_INTERNAL = 0x00000010; // Don't show even to #1.
constexpr int AF_NOCMD    = 0x00000020; // Don't create a @ command for it.
constexpr int AF_LOCK     = 0x00000040; // Attribute is locked.
constexpr int AF_DELETED  = 0x00000080; // Attribute should be ignored.
constexpr int AF_NOPROG   = 0x00000100; // Don't process $-commands from this attr.
constexpr int AF_GOD      = 0x00000200; // Only #1 can change it.
constexpr int AF_IS_LOCK  = 0x00000400; // Attribute is a lock.
constexpr int AF_VISUAL   = 0x00000800; // Anyone can see.
constexpr int AF_PRIVATE  = 0x00001000; // Not inherited by children.
constexpr int AF_HTML     = 0x00002000; // Don't HTML escape this in did_it().
constexpr int AF_NOPARSE  = 0x00004000; // Don't evaluate when checking for $-cmds.
constexpr int AF_REGEXP   = 0x00008000; // Do a regexp rather than wildcard match.
constexpr int AF_NOCLONE  = 0x00010000; // Don't copy this attr when cloning.
constexpr int AF_CONST    = 0x00020000; // No one can change it (set by server).
constexpr int AF_CASE     = 0x00040000; // Regexp matches are case-sensitive.
constexpr int AF_TRACE    = 0x00080000; // Trace evaluation of this attribute.
constexpr int AF_NOEVAL   = 0x00100000; // Don't evaluate attribute contents.
constexpr int AF_NONAME   = 0x00400000; // Supress name in oattr cases.
constexpr int AF_NODECOMP = 0x00800000; // Do not include in @decomp.
constexpr int AF_ISUSED   = 0x10000000; // Used to make efficient sweeps of stale
                                        // attributes.

// Allow AF_TRACE in x to control the EV_TRACE bit in y.
//
#define AttrTrace(x, y) ( ((x) & AF_TRACE)   \
                        ? ((y) | EV_TRACE)   \
                        : ((y) & ~EV_TRACE))

constexpr int A_OSUCC     = 1;   /* Others success message */
constexpr int A_OFAIL     = 2;   /* Others fail message */
constexpr int A_FAIL      = 3;   /* Invoker fail message */
constexpr int A_SUCC      = 4;   /* Invoker success message */
constexpr int A_PASS      = 5;   /* Password (only meaningful for players) */
constexpr int A_DESC      = 6;   /* Description */
constexpr int A_SEX       = 7;   /* Sex */
constexpr int A_ODROP     = 8;   /* Others drop message */
constexpr int A_DROP      = 9;   /* Invoker drop message */
constexpr int A_OKILL     = 10;  /* Others kill message */
constexpr int A_KILL      = 11;  /* Invoker kill message */
constexpr int A_ASUCC     = 12;  /* Success action list */
constexpr int A_AFAIL     = 13;  /* Failure action list */
constexpr int A_ADROP     = 14;  /* Drop action list */
constexpr int A_AKILL     = 15;  /* Kill action list */
constexpr int A_AUSE      = 16;  /* Use action list */
constexpr int A_CHARGES   = 17;  /* Number of charges remaining */
constexpr int A_RUNOUT    = 18;  /* Actions done when no more charges */
constexpr int A_STARTUP   = 19;  /* Actions run when game started up */
constexpr int A_ACLONE    = 20;  /* Actions run when obj is cloned */
constexpr int A_APAY      = 21;  /* Actions run when given COST pennies */
constexpr int A_OPAY      = 22;  /* Others pay message */
constexpr int A_PAY       = 23;  /* Invoker pay message */
constexpr int A_COST      = 24;  /* Number of pennies needed to invoke xPAY */
constexpr int A_MONEY     = 25;  /* Value or Wealth (internal) */
constexpr int A_LISTEN    = 26;  /* (Wildcarded) string to listen for */
constexpr int A_AAHEAR    = 27;  /* Actions to do when anyone says LISTEN str */
constexpr int A_AMHEAR    = 28;  /* Actions to do when I say LISTEN str */
constexpr int A_AHEAR     = 29;  /* Actions to do when others say LISTEN str */
constexpr int A_LAST      = 30;  /* Date/time of last login (players only) */
constexpr int A_QUEUEMAX  = 31;  /* Max. # of entries obj has in the queue */
constexpr int A_IDESC     = 32;  /* Inside description (ENTER to get inside) */
constexpr int A_ENTER     = 33;  /* Invoker enter message */
constexpr int A_OXENTER   = 34;  /* Others enter message in dest */
constexpr int A_AENTER    = 35;  /* Enter action list */
constexpr int A_ADESC     = 36;  /* Describe action list */
constexpr int A_ODESC     = 37;  /* Others describe message */
constexpr int A_RQUOTA    = 38;  /* Relative object quota */
constexpr int A_ACONNECT  = 39;  /* Actions run when player connects */
constexpr int A_ADISCONNECT = 40;  /* Actions run when player disconnects */
constexpr int A_ALLOWANCE = 41;  /* Daily allowance, if diff from default */
constexpr int A_LOCK      = 42;  /* Object lock */
constexpr int A_NAME      = 43;  /* Object name */
constexpr int A_COMMENT   = 44;  /* Wizard-accessable comments */
constexpr int A_USE       = 45;  /* Invoker use message */
constexpr int A_OUSE      = 46;  /* Others use message */
constexpr int A_SEMAPHORE = 47;  /* Semaphore control info */
constexpr int A_TIMEOUT   = 48;  /* Per-user disconnect timeout */
constexpr int A_QUOTA     = 49;  /* Absolute quota (to speed up @quota) */
constexpr int A_LEAVE     = 50;  /* Invoker leave message */
constexpr int A_OLEAVE    = 51;  /* Others leave message in src */
constexpr int A_ALEAVE    = 52;  /* Leave action list */
constexpr int A_OENTER    = 53;  /* Others enter message in src */
constexpr int A_OXLEAVE   = 54;  /* Others leave message in dest */
constexpr int A_MOVE      = 55;  /* Invoker move message */
constexpr int A_OMOVE     = 56;  /* Others move message */
constexpr int A_AMOVE     = 57;  /* Move action list */
constexpr int A_ALIAS     = 58;  /* Alias for player names */
constexpr int A_LENTER    = 59;  /* ENTER lock */
constexpr int A_LLEAVE    = 60;  /* LEAVE lock */
constexpr int A_LPAGE     = 61;  /* PAGE lock */
constexpr int A_LUSE      = 62;  /* USE lock */
constexpr int A_LGIVE     = 63;  /* Give lock (who may give me away?) */
constexpr int A_EALIAS    = 64;  /* Alternate names for ENTER */
constexpr int A_LALIAS    = 65;  /* Alternate names for LEAVE */
constexpr int A_EFAIL     = 66;  /* Invoker entry fail message */
constexpr int A_OEFAIL    = 67;  /* Others entry fail message */
constexpr int A_AEFAIL    = 68;  /* Entry fail action list */
constexpr int A_LFAIL     = 69;  /* Invoker leave fail message */
constexpr int A_OLFAIL    = 70;  /* Others leave fail message */
constexpr int A_ALFAIL    = 71;  /* Leave fail action list */
constexpr int A_REJECT    = 72;  /* Rejected page return message */
constexpr int A_AWAY      = 73;  /* Not_connected page return message */
constexpr int A_IDLE      = 74;  /* Success page return message */
constexpr int A_UFAIL     = 75;  /* Invoker use fail message */
constexpr int A_OUFAIL    = 76;  /* Others use fail message */
constexpr int A_AUFAIL    = 77;  /* Use fail action list */
constexpr int A_PFAIL     = 78;  /* Invoker page fail message */
constexpr int A_TPORT     = 79;  /* Invoker teleport message */
constexpr int A_OTPORT    = 80;  /* Others teleport message in src */
constexpr int A_OXTPORT   = 81;  /* Others teleport message in dst */
constexpr int A_ATPORT    = 82;  /* Teleport action list */
constexpr int A_PRIVS     = 83;  /* Individual permissions */
constexpr int A_LOGINDATA = 84;  /* Recent login information */
constexpr int A_LTPORT    = 85;  /* Teleport lock (can others @tel to me?) */
constexpr int A_LDROP     = 86;  /* Drop lock (can I be dropped or @tel'ed) */
constexpr int A_LRECEIVE  = 87;  /* Receive lock (who may give me things?) */
constexpr int A_LASTSITE  = 88;  /* Last site logged in from, in cleartext */
constexpr int A_INPREFIX  = 89;  /* Prefix on incoming messages into objects */
constexpr int A_PREFIX    = 90;  /* Prefix used by exits/objects when audible */
constexpr int A_INFILTER  = 91;  /* Filter to zap incoming text into objects */
constexpr int A_FILTER    = 92;  /* Filter to zap text forwarded by audible. */
constexpr int A_LLINK     = 93;  /* Who may link to here */
constexpr int A_LTELOUT   = 94;  /* Who may teleport out from here */
constexpr int A_FORWARDLIST = 95;  /* Recipients of AUDIBLE output */
constexpr int A_MAILFOLDERS = 96;  /* @mail folders */
constexpr int A_LUSER     = 97;  /* Spare lock not referenced by server */
constexpr int A_LPARENT   = 98;  /* Who may @parent to me if PARENT_OK set */
constexpr int A_LCONTROL  = 99;  /* Who controls me if CONTROL_OK set */
constexpr int A_VA        = 100; /* VA attribute (VB-VZ follow) */
// 126 unused
constexpr int A_LGET      = 127; /* Get lock (who may get stuff from me?) */
constexpr int A_MFAIL     = 128; /* Mail rejected fail message */
constexpr int A_GFAIL     = 129; /* Give fail message */
constexpr int A_OGFAIL    = 130; /* Others give fail message */
constexpr int A_AGFAIL    = 131; /* Give fail action */
constexpr int A_RFAIL     = 132; /* Receive fail message */
constexpr int A_ORFAIL    = 133; /* Others receive fail message */
constexpr int A_ARFAIL    = 134; /* Receive fail action */
constexpr int A_DFAIL     = 135; /* Drop fail message */
constexpr int A_ODFAIL    = 136; /* Others drop fail message */
constexpr int A_ADFAIL    = 137; /* Drop fail action */
constexpr int A_TFAIL     = 138; /* Teleport (to) fail message */
constexpr int A_OTFAIL    = 139; /* Others teleport (to) fail message */
constexpr int A_ATFAIL    = 140; /* Teleport fail action */
constexpr int A_TOFAIL    = 141; /* Teleport (from) fail message */
constexpr int A_OTOFAIL   = 142; /* Others teleport (from) fail message */
constexpr int A_ATOFAIL   = 143; /* Teleport (from) fail action */
constexpr int A_LASTIP    = 144; /* Last IP logged in from */

// Optional WoD Realm descriptions.
//
#ifdef WOD_REALMS
constexpr int A_UMBRADESC  = 145;
constexpr int A_WRAITHDESC = 146;
constexpr int A_FAEDESC    = 147;
constexpr int A_MATRIXDESC = 148;
#endif // WOD_REALMS

constexpr int A_COMJOIN   = 149;
constexpr int A_COMLEAVE  = 150;
constexpr int A_COMON     = 151;
constexpr int A_COMOFF    = 152;

// 153 - 197 unused
constexpr int A_CMDCHECK  = 198; // For @icmd. (From RhostMUSH)
constexpr int A_MONIKER   = 199; // Ansi-colored and/or accented name of object.
constexpr int A_LASTPAGE  = 200; /* Player last paged */
constexpr int A_MAIL      = 201; /* Message echoed to sender */
constexpr int A_AMAIL     = 202; /* Action taken when mail received */
constexpr int A_SIGNATURE = 203; /* Mail signature */
constexpr int A_DAILY     = 204; /* Daily attribute to be executed */
constexpr int A_MAILTO    = 205; /* Who is the mail to? */
constexpr int A_MAILMSG   = 206; /* The mail message itself */
constexpr int A_MAILSUB   = 207; /* The mail subject */
constexpr int A_MAILCURF  = 208; /* The current @mail folder */
constexpr int A_LSPEECH   = 209; /* Speechlocks */
constexpr int A_PROGCMD   = 210; /* Command for execution by @prog */
constexpr int A_MAILFLAGS = 211; /* Flags for extended mail */
constexpr int A_DESTROYER = 212; /* Who is destroying this object? */

constexpr int A_NEWOBJS       = 213;     /* New object array */
constexpr int A_SAYSTRING     = 214;     // Replaces 'says,'
constexpr int A_SPEECHMOD     = 215;     // Softcode to be applied to speech
constexpr int A_EXITVARDEST   = 216;     /* Variable exit destination */
constexpr int A_LCHOWN        = 217;     /* ChownLock */
constexpr int A_CREATED       = 218;     // Date/time created
constexpr int A_MODIFIED      = 219;     // Date/time last modified

constexpr int A_VRML_URL  = 220; /* URL of the VRML scene for this object */
constexpr int A_HTDESC    = 221; /* HTML @desc */

// Added by D.Piper (del@doofer.org) 2000-APR
//
constexpr int A_REASON    = 222; // Disconnect reason
#ifdef GAME_DOOFERMUX
constexpr int A_REGINFO   = 223; // Registration Information
#endif // GAME_DOOFERMUX
constexpr int A_CONNINFO  = 224; // Connection info: (total connected time,
                        // longest connection last connection, total
                        // connections, time of logout.
constexpr int A_LMAIL     = 225; // Lock who may @mail you
constexpr int A_LOPEN     = 226; // Lock for controlling OPEN_OK locations
constexpr int A_LASTWHISPER = 227; // Last set of people whispered to
constexpr int A_ADESTROY  = 228;  // @adestroy attribute
constexpr int A_APARENT   = 229;  // @aparent attribute
constexpr int A_ACREATE   = 230;  // @acreate attribute
constexpr int A_LVISIBLE  = 231;  // Visibility Lock Storage Attribute

constexpr int A_PRONOUN   = 232; /* Pronoun group name for %s/%o/%p/%a */
constexpr int A_GMCP      = 233; // GMCP handler attribute
constexpr int A_PROTECTNAME = 234; // Protected player names

// 235 - 239 unused

constexpr int A_IDLETMOUT  = 240; /* Idle message timeout */
constexpr int A_EXITFORMAT = 241;
constexpr int A_CONFORMAT  = 242;
constexpr int A_NAMEFORMAT = 243;
constexpr int A_DESCFORMAT = 244;

// 245 - 249 unused

#ifdef REALITY_LVLS
constexpr int A_RLEVEL      = 250;
#endif

// 251 unused
constexpr int A_VLIST     = 252;
constexpr int A_LIST      = 253; // Legacy packed attribute-list (deprecated).
constexpr int A_STRUCT    = 254;
constexpr int A_TEMP      = 255;

constexpr int A_USER_START    = 256;     // Start of user-named attributes.

constexpr int ATR_BUF_CHUNK   = 100; /* Min size to allocate for attribute buffer */
constexpr int ATR_BUF_INCR    = 6;   /* Max size of one attribute */

#endif //!ATTRS_H
