#ifndef _R7HGAME_H_
#define _R7HGAME_H_

#define R7H_V_MASK          0x000000ff
#define R7H_V_ZONE          0x00000100
#define R7H_V_LINK          0x00000200
#define R7H_V_GDBM          0x00000400
#define R7H_V_ATRNAME       0x00000800
#define R7H_V_ATRKEY        0x00001000
#define R7H_V_PERNKEY       0x00001000
#define R7H_V_PARENT        0x00002000
#define R7H_V_COMM          0x00004000
#define R7H_V_ATRMONEY      0x00008000
#define R7H_V_XFLAGS        0x00010000

#define R7H_MANDFLAGS  (R7H_V_LINK|R7H_V_PARENT|R7H_V_XFLAGS)
#define R7H_OFLAGS     (R7H_V_GDBM|R7HV_ATRKEY|R7H_V_ATRNAME|R7H_V_ATRMONEY)

#define A_USER_START    256     // Start of user-named attributes.

// Object types
//
#define R7H_TYPE_ROOM     0x0
#define R7H_TYPE_THING    0x1
#define R7H_TYPE_EXIT     0x2
#define R7H_TYPE_PLAYER   0x3
#define R7H_TYPE_GARBAGE  0x5
#define R7H_NOTYPE        0x7
#define R7H_TYPE_MASK     0x7

// Attribute flags
//
#define R7H_AF_ODARK        0x00000001
#define R7H_AF_DARK         0x00000002
#define R7H_AF_WIZARD       0x00000004
#define R7H_AF_MDARK        0x00000008
#define R7H_AF_INTERNAL     0x00000010
#define R7H_AF_NOCMD        0x00000020
#define R7H_AF_LOCK         0x00000040
#define R7H_AF_DELETED      0x00000080
#define R7H_AF_NOPROG       0x00000100
#define R7H_AF_GOD          0x00000200
#define R7H_AF_ADMIN        0x00000400
#define R7H_AF_BUILDER      0x00000800
#define R7H_AF_IS_LOCK      0x00001000
#define R7H_AF_GUILDMASTER  0x00002000
#define R7H_AF_IMMORTAL     0x00004000
#define R7H_AF_PRIVATE      0x00008000
#define R7H_AF_NONBLOCKING  0x00010000
#define R7H_AF_VISUAL       0x00020000
#define R7H_AF_NOANSI       0x00040000
#define R7H_AF_PINVIS       0x00080000
#define R7H_AF_NORETURN     0x00100000
#define R7H_AF_NOCLONE      0x00200000
#define R7H_AF_NOPARSE      0x00400000
#define R7H_AF_SAFE         0x00800000
#define R7H_AF_USELOCK      0x01000000
#define R7H_AF_SINGLETHREAD 0x02000000
#define R7H_AF_DEFAULT      0x04000000
#define R7H_AF_ATRLOCK      0x08000000
#define R7H_AF_LOGGED       0x10000000

// Attribute Numbers
//
#define R7H_A_OSUCC           1
#define R7H_A_OFAIL           2
#define R7H_A_FAIL            3
#define R7H_A_SUCC            4
#define R7H_A_PASS            5
#define R7H_A_DESC            6
#define R7H_A_SEX             7
#define R7H_A_ODROP           8
#define R7H_A_DROP            9
#define R7H_A_OKILL          10
#define R7H_A_KILL           11
#define R7H_A_ASUCC          12
#define R7H_A_AFAIL          13
#define R7H_A_ADROP          14
#define R7H_A_AKILL          15
#define R7H_A_AUSE           16
#define R7H_A_CHARGES        17
#define R7H_A_RUNOUT         18
#define R7H_A_STARTUP        19
#define R7H_A_ACLONE         20
#define R7H_A_APAY           21
#define R7H_A_OPAY           22
#define R7H_A_PAY            23
#define R7H_A_COST           24
#define R7H_A_MONEY          25
#define R7H_A_LISTEN         26
#define R7H_A_AAHEAR         27
#define R7H_A_AMHEAR         28
#define R7H_A_AHEAR          29
#define R7H_A_LAST           30
#define R7H_A_QUEUEMAX       31
#define R7H_A_IDESC          32
#define R7H_A_ENTER          33
#define R7H_A_OXENTER        34
#define R7H_A_AENTER         35
#define R7H_A_ADESC          36
#define R7H_A_ODESC          37
#define R7H_A_RQUOTA         38
#define R7H_A_ACONNECT       39
#define R7H_A_ADISCONNECT    40
#define R7H_A_ALLOWANCE      41
#define R7H_A_LOCK           42
#define R7H_A_NAME           43
#define R7H_A_COMMENT        44
#define R7H_A_USE            45
#define R7H_A_OUSE           46
#define R7H_A_SEMAPHORE      47
#define R7H_A_TIMEOUT        48
#define R7H_A_QUOTA          49
#define R7H_A_LEAVE          50
#define R7H_A_OLEAVE         51
#define R7H_A_ALEAVE         52
#define R7H_A_OENTER         53
#define R7H_A_OXLEAVE        54
#define R7H_A_MOVE           55
#define R7H_A_OMOVE          56
#define R7H_A_AMOVE          57
#define R7H_A_ALIAS          58
#define R7H_A_LENTER         59
#define R7H_A_LLEAVE         60
#define R7H_A_LPAGE          61
#define R7H_A_LUSE           62
#define R7H_A_LGIVE          63
#define R7H_A_EALIAS         64
#define R7H_A_LALIAS         65
#define R7H_A_EFAIL          66
#define R7H_A_OEFAIL         67
#define R7H_A_AEFAIL         68
#define R7H_A_LFAIL          69
#define R7H_A_OLFAIL         70
#define R7H_A_ALFAIL         71
#define R7H_A_REJECT         72
#define R7H_A_AWAY           73
#define R7H_A_IDLE           74
#define R7H_A_UFAIL          75
#define R7H_A_OUFAIL         76
#define R7H_A_AUFAIL         77
#define R7H_A_PFAIL          78
#define R7H_A_TPORT          79
#define R7H_A_OTPORT         80
#define R7H_A_OXTPORT        81
#define R7H_A_ATPORT         82
#define R7H_A_PRIVS          83
#define R7H_A_LOGINDATA      84
#define R7H_A_LTPORT         85
#define R7H_A_LDROP          86
#define R7H_A_LRECEIVE       87
#define R7H_A_LASTSITE       88
#define R7H_A_INPREFIX       89
#define R7H_A_PREFIX         90
#define R7H_A_INFILTER       91
#define R7H_A_FILTER         92
#define R7H_A_LLINK          93
#define R7H_A_LTELOUT        94
#define R7H_A_FORWARDLIST    95
#define R7H_A_LCONTROL       96
#define R7H_A_LUSER          97
#define R7H_A_LPARENT        98
#define R7H_A_LAMBDA         99
#define R7H_A_VA            100
#define R7H_A_CHANNEL       126
#define R7H_A_GUILD         127
#define R7H_A_ZA            129
#define R7H_A_BCCMAIL       155
#define R7H_A_EMAIL         156
#define R7H_A_LMAIL         157
#define R7H_A_LSHARE        158
#define R7H_A_GFAIL         159
#define R7H_A_OGFAIL        160
#define R7H_A_AGFAIL        161
#define R7H_A_RFAIL         162
#define R7H_A_ORFAIL        163
#define R7H_A_ARFAIL        164
#define R7H_A_DFAIL         165
#define R7H_A_ODFAIL        166
#define R7H_A_ADFAIL        167
#define R7H_A_TFAIL         168
#define R7H_A_OTFAIL        169
#define R7H_A_ATFAIL        170
#define R7H_A_TOFAIL        171
#define R7H_A_OTOFAIL       172
#define R7H_A_AOTFAIL       173
#define R7H_A_ATOFAIL       174
#define R7H_A_MPASS         175
#define R7H_A_MPSET         176
#define R7H_A_LASTPAGE      177
#define R7H_A_RETPAGE       178
#define R7H_A_RECTIME       179
#define R7H_A_MCURR         180
#define R7H_A_MQUOTA        181
#define R7H_A_LQUOTA        182
#define R7H_A_TQUOTA        183
#define R7H_A_MTIME         184
#define R7H_A_MSAVEMAX      185
#define R7H_A_MSAVECUR      186
#define R7H_A_IDENT         187
#define R7H_A_LZONEWIZ      188
#define R7H_A_LZONETO       189
#define R7H_A_LTWINK        190
#define R7H_A_SITEGOOD      191
#define R7H_A_SITEBAD       192
#define R7H_A_MAILSIG       193
#define R7H_A_ADESC2        194
#define R7H_A_PAYLIM        195
#define R7H_A_DESC2         196
#define R7H_A_RACE          197
#define R7H_A_CMDCHECK      198
#define R7H_A_LSPEECH       199
#define R7H_A_SFAIL         200
#define R7H_A_ASFAIL        201
#define R7H_A_AUTOREG       202
#define R7H_A_LDARK         203
#define R7H_A_STOUCH        204
#define R7H_A_SATOUCH       205
#define R7H_A_SOTOUCH       206
#define R7H_A_SLISTEN       207
#define R7H_A_SALISTEN      208
#define R7H_A_SOLISTEN      209
#define R7H_A_STASTE        210
#define R7H_A_SATASTE       211
#define R7H_A_SOTASTE       212
#define R7H_A_SSMELL        213
#define R7H_A_SASMELL       214
#define R7H_A_SOSMELL       215
#define R7H_A_LDROPTO       216
#define R7H_A_LOPEN         217
#define R7H_A_LCHOWN        218
#define R7H_A_CAPTION       219
#define R7H_A_ANSINAME      220
#define R7H_A_TOTCMDS       221
#define R7H_A_LSTCMDS       222
#define R7H_A_RECEIVELIM    223
#define R7H_A_LCON_FMT      224
#define R7H_A_LEXIT_FMT     225
#define R7H_A_LDEXIT_FMT    226
#define R7H_A_MODIFY_TIME   227
#define R7H_A_CREATED_TIME  228
#define R7H_A_ALTNAME       229
#define R7H_A_LALTNAME      230
#define R7H_A_INVTYPE       231
#define R7H_A_TOTCHARIN     232
#define R7H_A_TOTCHAROUT    233
#define R7H_A_LGIVETO       234
#define R7H_A_LGETFROM      235
#define R7H_A_SAYSTRING     236
#define R7H_A_LASTCREATE    237
#define R7H_A_SAVESENDMAIL  238
#define R7H_A_PROGBUFFER    239
#define R7H_A_PROGPROMPT    240
#define R7H_A_PROGPROMPTBUF 241
#define R7H_A_TEMPBUFFER    242
#define R7H_A_DESTVATTRMAX  243
#define R7H_A_RLEVEL        244
#define R7H_A_NAME_FMT      245
#define R7H_A_LASTIP        246
#define R7H_A_SPAMPROTECT   247
#define R7H_A_EXITTO        248
#define R7H_A_VLIST         252
#define R7H_A_LIST          253
#define R7H_A_STRUCT        254
#define R7H_A_TEMP          255

// Object Flagword 1
//
#define R7H_SEETHRU         0x00000008UL
#define R7H_WIZARD          0x00000010UL
#define R7H_LINK_OK         0x00000020UL
#define R7H_DARK            0x00000040UL
#define R7H_JUMP_OK         0x00000080UL
#define R7H_STICKY          0x00000100UL
#define R7H_DESTROY_OK      0x00000200UL
#define R7H_HAVEN           0x00000400UL
#define R7H_QUIET           0x00000800UL
#define R7H_HALT            0x00001000UL
#define R7H_TRACE           0x00002000UL
#define R7H_GOING           0x00004000UL
#define R7H_MONITOR         0x00008000UL
#define R7H_MYOPIC          0x00010000UL
#define R7H_PUPPET          0x00020000UL
#define R7H_CHOWN_OK        0x00040000UL
#define R7H_ENTER_OK        0x00080000UL
#define R7H_VISUAL          0x00100000UL
#define R7H_IMMORTAL        0x00200000UL
#define R7H_HAS_STARTUP     0x00400000UL
#define R7H_OPAQUE          0x00800000UL
#define R7H_VERBOSE         0x01000000UL
#define R7H_INHERIT         0x02000000UL
#define R7H_NOSPOOF         0x04000000UL
#define R7H_ROBOT           0x08000000UL
#define R7H_SAFE            0x10000000UL
#define R7H_CONTROL_OK      0x20000000UL
#define R7H_HEARTHRU        0x40000000UL
#define R7H_TERSE           0x80000000UL

// Object Flagword 2
//
#define R7H_KEY             0x00000001UL
#define R7H_ABODE           0x00000002UL
#define R7H_FLOATING        0x00000004UL
#define R7H_UNFINDABLE      0x00000008UL
#define R7H_PARENT_OK       0x00000010UL
#define R7H_LIGHT           0x00000020UL
#define R7H_HAS_LISTEN      0x00000040UL
#define R7H_HAS_FWDLIST     0x00000080UL
#define R7H_ADMIN           0x00000100UL
#define R7H_GUILDOBJ        0x00000200UL
#define R7H_GUILDMASTER     0x00000400UL
#define R7H_NO_WALLS        0x00000800UL
#define R7H_OLD_TEMPLE      0x00001000UL
#define R7H_OLD_NOROBOT     0x00002000UL
#define R7H_SCLOAK          0x00004000UL
#define R7H_CLOAK           0x00008000UL
#define R7H_FUBAR           0x00010000UL
#define R7H_INDESTRUCTABLE  0x00020000UL
#define R7H_NO_YELL         0x00040000UL
#define R7H_NO_TEL          0x00080000UL
#define R7H_FREE            0x00100000UL
#define R7H_GUEST_FLAG      0x00200000UL
#define R7H_RECOVER         0x00400000UL
#define R7H_BYEROOM         0x00800000UL
#define R7H_WANDERER        0x01000000UL
#define R7H_ANSI            0x02000000UL
#define R7H_ANSICOLOR       0x04000000UL
#define R7H_NOFLASH         0x08000000UL
#define R7H_SUSPECT         0x10000000UL
#define R7H_BUILDER         0x20000000UL
#define R7H_CONNECTED       0x40000000UL
#define R7H_SLAVE           0x80000000UL

// Object Flagword 3
//
#define R7H_NOCONNECT       0x00000001UL
#define R7H_DPSHIFT         0x00000002UL
#define R7H_NOPOSSESS       0x00000004UL
#define R7H_COMBAT          0x00000008UL
#define R7H_IC              0x00000010UL
#define R7H_ZONEMASTER      0x00000020UL
#define R7H_ALTQUOTA        0x00000040UL
#define R7H_NOEXAMINE       0x00000080UL
#define R7H_NOMODIFY        0x00000100UL
#define R7H_CMDCHECK        0x00000200UL
#define R7H_DOORRED         0x00000400UL
#define R7H_PRIVATE         0x00000800UL
#define R7H_NOMOVE          0x00001000UL
#define R7H_STOP            0x00002000UL
#define R7H_NOSTOP          0x00004000UL
#define R7H_NOCOMMAND       0x00008000UL
#define R7H_AUDIT           0x00010000UL
#define R7H_SEE_OEMIT       0x00020000UL
#define R7H_NO_GOBJ         0x00040000UL
#define R7H_NO_PESTER       0x00080000UL
#define R7H_LRFLAG          0x00100000UL
#define R7H_TELOK           0x00200000UL
#define R7H_NO_OVERRIDE     0x00400000UL
#define R7H_NO_USELOCK      0x00800000UL
#define R7H_DR_PURGE        0x01000000UL
#define R7H_NO_ANSINAME     0x02000000UL
#define R7H_SPOOF           0x04000000UL
#define R7H_SIDEFX          0x08000000UL
#define R7H_ZONECONTENTS    0x10000000UL
#define R7H_NOWHO           0x20000000UL
#define R7H_ANONYMOUS       0x40000000UL
#define R7H_BACKSTAGE       0x80000000UL

// Object Flagword 4
//
#define R7H_NOBACKSTAGE     0x00000001UL
#define R7H_LOGIN           0x00000002UL
#define R7H_INPROGRAM       0x00000004UL
#define R7H_COMMANDS        0x00000008UL
#define R7H_MARKER0         0x00000010UL
#define R7H_MARKER1         0x00000020UL
#define R7H_MARKER2         0x00000040UL
#define R7H_MARKER3         0x00000080UL
#define R7H_MARKER4         0x00000100UL
#define R7H_MARKER5         0x00000200UL
#define R7H_MARKER6         0x00000400UL
#define R7H_MARKER7         0x00000800UL
#define R7H_MARKER8         0x00001000UL
#define R7H_MARKER9         0x00002000UL
#define R7H_BOUNCE          0x00004000UL
#define R7H_SHOWFAILCMD     0x00008000UL
#define R7H_NOUNDERLINE     0x00010000UL
#define R7H_NONAME          0x00020000UL
#define R7H_ZONEPARENT      0x00040000UL
#define R7H_SPAMMONITOR     0x00080000UL
#define R7H_BLIND           0x00100000UL
#define R7H_NOCODE          0x00200000UL

// Object Toggleword 1
//
#define R7H_TOG_MONITOR             0x00000001UL
#define R7H_TOG_MONITOR_USERID      0x00000002UL
#define R7H_TOG_MONITOR_SITE        0x00000004UL
#define R7H_TOG_MONITOR_STATS       0x00000008UL
#define R7H_TOG_MONITOR_FAIL        0x00000010UL
#define R7H_TOG_MONITOR_CONN        0x00000020UL
#define R7H_TOG_VANILLA_ERRORS      0x00000040UL
#define R7H_TOG_NO_ANSI_EX          0x00000080UL
#define R7H_TOG_CPUTIME             0x00000100UL
#define R7H_TOG_MONITOR_DISREASON   0x00000200UL
#define R7H_TOG_MONITOR_VLIMIT      0x00000400UL
#define R7H_TOG_NOTIFY_LINK         0x00000800UL
#define R7H_TOG_MONITOR_AREG        0x00001000UL
#define R7H_TOG_MONITOR_TIME        0x00002000UL
#define R7H_TOG_CLUSTER             0x00004000UL
#define R7H_TOG_NOANSI_PLAYER       0x00010000UL
#define R7H_TOG_NOANSI_THING        0x00020000UL
#define R7H_TOG_NOANSI_ROOM         0x00040000UL
#define R7H_TOG_NOANSI_EXIT         0x00080000UL
#define R7H_TOG_NO_TIMESTAMP        0x00100000UL
#define R7H_TOG_NO_FORMAT           0x00200000UL
#define R7H_TOG_ZONE_AUTOADD        0x00400000UL
#define R7H_TOG_ZONE_AUTOADDALL     0x00800000UL
#define R7H_TOG_WIELDABLE           0x01000000UL
#define R7H_TOG_WEARABLE            0x02000000UL
#define R7H_TOG_SEE_SUSPECT         0x04000000UL
#define R7H_TOG_MONITOR_CPU         0x08000000UL
#define R7H_TOG_BRANDY_MAIL         0x10000000UL
#define R7H_TOG_FORCEHALTED         0x20000000UL
#define R7H_TOG_PROG                0x40000000UL
#define R7H_TOG_NOSHELLPROG         0x80000000UL

// Object Toggleword 2
//
#define R7H_TOG_EXTANSI             0x00000001UL
#define R7H_TOG_IMMPROG             0x00000002UL
#define R7H_TOG_MONITOR_BFAIL       0x00000004UL
#define R7H_TOG_PROG_ON_CONNECT     0x00000008UL
#define R7H_TOG_MAIL_STRIPRETURN    0x00000010UL
#define R7H_TOG_PENN_MAIL           0x00000020UL
#define R7H_TOG_SILENTEFFECTS       0x00000040UL
#define R7H_TOG_IGNOREZONE          0x00000080UL
#define R7H_TOG_VPAGE               0x00000100UL
#define R7H_TOG_PAGELOCK            0x00000200UL
#define R7H_TOG_MAIL_NOPARSE        0x00000400UL
#define R7H_TOG_MAIL_LOCKDOWN       0x00000800UL
#define R7H_TOG_MUXPAGE             0x00001000UL
#define R7H_TOG_NOZONEPARENT        0x00002000UL
#define R7H_TOG_ATRUSE              0x00004000UL
#define R7H_TOG_VARIABLE            0x00008000UL
#define R7H_TOG_KEEPALIVE           0x00010000UL
#define R7H_TOG_CHKREALITY          0x00020000UL
#define R7H_TOG_NOISY               0x00040000UL
#define R7H_TOG_ZONECMDCHK          0x00080000UL
#define R7H_TOG_HIDEIDLE            0x00100000UL
#define R7H_TOG_MORTALREALITY       0x00200000UL
#define R7H_TOG_ACCENTS             0x00400000UL
#define R7H_TOG_PREMAILVALIDATE     0x00800000UL
#define R7H_TOG_SAFELOG             0x01000000UL
#define R7H_TOG_NODEFAULT           0x08000000UL
#define R7H_TOG_EXFULLWIZATTR       0x10000000UL
#define R7H_TOG_LOGROOM             0x40000000UL
#define R7H_TOG_NOGLOBPARENT        0x80000000UL

// Object toggleword 3 (powerword 1)
// Two bits per power at given position.
//
#define R7H_POWER_CHANGE_QUOTAS        0
#define R7H_POWER_CHOWN_ME             2
#define R7H_POWER_CHOWN_ANYWHERE       4
#define R7H_POWER_CHOWN_OTHER          6
#define R7H_POWER_WIZ_WHO              8
#define R7H_POWER_EX_ALL              10
#define R7H_POWER_NOFORCE             12
#define R7H_POWER_SEE_QUEUE_ALL       14
#define R7H_POWER_FREE_QUOTA          16
#define R7H_POWER_GRAB_PLAYER         18
#define R7H_POWER_JOIN_PLAYER         20
#define R7H_POWER_LONG_FINGERS        22
#define R7H_POWER_NO_BOOT             24
#define R7H_POWER_BOOT                26
#define R7H_POWER_STEAL               28
#define R7H_POWER_SEE_QUEUE           30

// Object toggleword 4 (powerword 2)
// Two bits per power at given position.
//
#define R7H_POWER_SHUTDOWN             0
#define R7H_POWER_TEL_ANYWHERE         2
#define R7H_POWER_TEL_ANYTHING         4
#define R7H_POWER_PCREATE              6
#define R7H_POWER_STAT_ANY             8
#define R7H_POWER_FREE_WALL           10
#define R7H_POWER_FREE_PAGE           14
#define R7H_POWER_HALT_QUEUE          16
#define R7H_POWER_HALT_QUEUE_ALL      18
#define R7H_POWER_NOKILL              22
#define R7H_POWER_SEARCH_ANY          24
#define R7H_POWER_SECURITY            26
#define R7H_POWER_WHO_UNFIND          28
#define R7H_POWER_WRAITH              30

// Object toggleword 5 (powerword 3)
// Two bits per power at given position.
//
#define R7H_POWER_OPURGE               0
#define R7H_POWER_HIDEBIT              2
#define R7H_POWER_NOWHO                4
#define R7H_POWER_FULLTEL_ANYWHERE     6
#define R7H_POWER_EX_FULL              8

// Object toggleword 6 (depowerword 1)
// Two bits per depower at given position.
//
#define R7H_DP_WALL                    0
#define R7H_DP_LONG_FINGERS            2
#define R7H_DP_STEAL                   4
#define R7H_DP_CREATE                  6
#define R7H_DP_WIZ_WHO                 8
#define R7H_DP_CLOAK                  10
#define R7H_DP_BOOT                   12
#define R7H_DP_PAGE                   14
#define R7H_DP_FORCE                  16
#define R7H_DP_LOCKS                  18
#define R7H_DP_COM                    20
#define R7H_DP_COMMAND                22
#define R7H_DP_MASTER                 24
#define R7H_DP_EXAMINE                26
#define R7H_DP_NUKE                   28
#define R7H_DP_FREE                   30

// Object toggleword 7 (depowerword 2)
// Two bits per depower at given position.
//
#define R7H_DP_OVERRIDE                0
#define R7H_DP_TEL_ANYWHERE            2
#define R7H_DP_TEL_ANYTHING            4
#define R7H_DP_PCREATE                 6
#define R7H_DP_POWER                   8
#define R7H_DP_QUOTA                  10
#define R7H_DP_MODIFY                 12
#define R7H_DP_CHOWN_ME               14
#define R7H_DP_CHOWN_OTHER            16
#define R7H_DP_ABUSE                  18
#define R7H_DP_UNL_QUOTA              20
#define R7H_DP_SEARCH_ANY             22
#define R7H_DP_GIVE                   24
#define R7H_DP_RECEIVE                26
#define R7H_DP_NOGOLD                 28
#define R7H_DP_NOSTEAL                30

// Object toggleword 8 (depowerword 3)
// Two bits per depower at given position.
//
#define R7H_DP_PASSWORD                0
#define R7H_DP_MORTAL_EXAMINE          2
#define R7H_DP_PERSONAL_COMMANDS       4

#define ATR_INFO_CHAR 0x01
#define R7H_NOTHING   (-1)

class P6H_LOCKEXP;

class R7H_LOCKEXP
{
public:
    typedef enum
    {
        le_is,
        le_carry,
        le_indirect,
        le_owner,
        le_and,
        le_or,
        le_not,
        le_attr,
        le_eval,
        le_ref,
        le_text,
        le_none,
    } R7H_OP;

    R7H_OP m_op;

    R7H_LOCKEXP *m_le[2];
    int          m_dbRef;
    char        *m_p[2];

    void SetIs(R7H_LOCKEXP *p)
    {
        m_op = le_is;
        m_le[0] = p;
    }
    void SetCarry(R7H_LOCKEXP *p)
    {
        m_op = le_carry;
        m_le[0] = p;
    }
    void SetIndir(R7H_LOCKEXP *p)
    {
        m_op = le_indirect;
        m_le[0] = p;
    }
    void SetOwner(R7H_LOCKEXP *p)
    {
        m_op = le_owner;
        m_le[0] = p;
    }
    void SetAnd(R7H_LOCKEXP *p, R7H_LOCKEXP *q)
    {
        m_op = le_and;
        m_le[0] = p;
        m_le[1] = q;
    }
    void SetOr(R7H_LOCKEXP *p, R7H_LOCKEXP *q)
    {
        m_op = le_or;
        m_le[0] = p;
        m_le[1] = q;
    }
    void SetNot(R7H_LOCKEXP *p)
    {
        m_op = le_not;
        m_le[0] = p;
    }
    void SetAttr(R7H_LOCKEXP *p, R7H_LOCKEXP *q)
    {
        m_op = le_attr;
        m_le[0] = p;
        m_le[1] = q;
    }
    void SetEval(R7H_LOCKEXP *p, R7H_LOCKEXP *q)
    {
        m_op = le_eval;
        m_le[0] = p;
        m_le[1] = q;
    }
    void SetRef(int dbRef)
    {
        m_op = le_ref;
        m_dbRef = dbRef;
    }
    void SetText(char *p)
    {
        m_op = le_text;
        m_p[0] = p;
    }

    void Write(FILE *fp);
    char *Write(char *p);

    bool ConvertFromP6H(P6H_LOCKEXP *p);

    R7H_LOCKEXP()
    {
        m_op = le_none;
        m_le[0] = m_le[1] = NULL;
        m_p[0] = m_p[1] = NULL;
        m_dbRef = 0;
    }
    ~R7H_LOCKEXP()
    {
        delete m_le[0];
        delete m_le[1];
        free(m_p[0]);
        free(m_p[1]);
        m_le[0] = m_le[1] = NULL;
        m_p[0] = m_p[1] = NULL;
    }
};

class R7H_ATTRNAMEINFO
{
public:
    bool  m_fNumAndName;
    int   m_iNum;
    char *m_pName;
    void  SetNumAndName(int iNum, char *pName);

    void Validate(int ver) const;

    void Write(FILE *fp);

    R7H_ATTRNAMEINFO()
    {
        m_fNumAndName = false;
        m_pName = NULL;
    }
    ~R7H_ATTRNAMEINFO()
    {
        free(m_pName);
        m_pName = NULL;
    }
};

class R7H_ATTRINFO
{
public:
    char *m_pAllocated;

    bool m_fNumAndValue;
    int  m_iNum;
    char *m_pValueEncoded;
    void SetNumAndValue(int iNum, char *pValue);

    int  m_iFlags;
    int  m_dbOwner;
    char *m_pValueUnencoded;
    void SetNumOwnerFlagsAndValue(int iNum, int dbOwner, int iFlags, char *pValue);

    bool m_fIsLock;
    R7H_LOCKEXP *m_pKeyTree;

    enum
    {
        kNone,
        kEncode,
        kDecode,
    } m_kState;
    void EncodeDecode(int dbObj);

    void Validate() const;

    void Write(FILE *fp) const;

    R7H_ATTRINFO()
    {
        m_fNumAndValue = false;
        m_pAllocated = NULL;
        m_pValueEncoded = NULL;
        m_pValueUnencoded = NULL;
        m_fIsLock = false;
        m_pKeyTree = NULL;
        m_iFlags = 0;
        m_dbOwner = R7H_NOTHING;
        m_kState = kNone;
    }
    ~R7H_ATTRINFO()
    {
        free(m_pAllocated);
        delete m_pKeyTree;
        m_pAllocated = NULL;
        m_pValueEncoded = NULL;
        m_pValueUnencoded = NULL;
        m_pKeyTree = NULL;
        m_iFlags = 0;
        m_dbOwner = R7H_NOTHING;
    }
};

class R7H_OBJECTINFO
{
public:
    bool m_fRef;
    int  m_dbRef;
    void SetRef(int dbRef) { m_fRef = true; m_dbRef = dbRef; }

    char *m_pName;
    void SetName(char *p);

    bool m_fLocation;
    int  m_dbLocation;
    void SetLocation(int dbLocation) { m_fLocation = true; m_dbLocation = dbLocation; }

    bool m_fContents;
    int  m_dbContents;
    void SetContents(int dbContents) { m_fContents = true; m_dbContents = dbContents; }

    bool m_fExits;
    int  m_dbExits;
    void SetExits(int dbExits) { m_fExits = true; m_dbExits = dbExits; }

    bool m_fNext;
    int  m_dbNext;
    void SetNext(int dbNext) { m_fNext = true; m_dbNext = dbNext; }

    bool m_fParent;
    int  m_dbParent;
    void SetParent(int dbParent) { m_fParent = true; m_dbParent = dbParent; }

    bool m_fOwner;
    int  m_dbOwner;
    void SetOwner(int dbOwner) { m_fOwner = true; m_dbOwner = dbOwner; }

    bool m_fZone;
    int  m_dbZone;
    void SetZone(int dbZone) { m_fZone = true; m_dbZone = dbZone; }

    bool m_fPennies;
    int  m_iPennies;
    void SetPennies(int iPennies) { m_fPennies = true; m_iPennies = iPennies; }

    bool m_fFlags1;
    int  m_iFlags1;
    void SetFlags1(int iFlags1) { m_fFlags1 = true; m_iFlags1 = iFlags1; }

    bool m_fFlags2;
    int  m_iFlags2;
    void SetFlags2(int iFlags2) { m_fFlags2 = true; m_iFlags2 = iFlags2; }

    bool m_fFlags3;
    int  m_iFlags3;
    void SetFlags3(int iFlags3) { m_fFlags3 = true; m_iFlags3 = iFlags3; }

    bool m_fFlags4;
    int  m_iFlags4;
    void SetFlags4(int iFlags4) { m_fFlags4 = true; m_iFlags4 = iFlags4; }

    bool m_fToggles1;
    int  m_iToggles1;
    void SetToggles1(int iToggles1) { m_fToggles1 = true; m_iToggles1 = iToggles1; }

    bool m_fToggles2;
    int  m_iToggles2;
    void SetToggles2(int iToggles2) { m_fToggles2 = true; m_iToggles2 = iToggles2; }

    bool m_fToggles3;
    int  m_iToggles3;
    void SetToggles3(int iToggles3) { m_fToggles3 = true; m_iToggles3 = iToggles3; }

    bool m_fToggles4;
    int  m_iToggles4;
    void SetToggles4(int iToggles4) { m_fToggles4 = true; m_iToggles4 = iToggles4; }

    bool m_fToggles5;
    int  m_iToggles5;
    void SetToggles5(int iToggles5) { m_fToggles5 = true; m_iToggles5 = iToggles5; }

    bool m_fToggles6;
    int  m_iToggles6;
    void SetToggles6(int iToggles6) { m_fToggles6 = true; m_iToggles6 = iToggles6; }

    bool m_fToggles7;
    int  m_iToggles7;
    void SetToggles7(int iToggles7) { m_fToggles7 = true; m_iToggles7 = iToggles7; }

    bool m_fToggles8;
    int  m_iToggles8;
    void SetToggles8(int iToggles8) { m_fToggles8 = true; m_iToggles8 = iToggles8; }

    bool m_fZones;
    vector<int> *m_pvz;
    void SetZones(vector<int> *pvz) { m_fZones = true; delete m_pvz; m_pvz = pvz; }

    bool m_fLink;
    int  m_dbLink;
    void SetLink(int dbLink) { m_fLink = true; m_dbLink = dbLink; }

    bool m_fAttrCount;
    int  m_nAttrCount;
    vector<R7H_ATTRINFO *> *m_pvai;
    void SetAttrs(int nAttrCount, vector<R7H_ATTRINFO *> *pvai);

    R7H_LOCKEXP *m_ple;
    void SetDefaultLock(R7H_LOCKEXP *p) { delete m_ple; m_ple = p; }

    void Validate() const;

    void Write(FILE *fp, bool bWriteLock);

    R7H_OBJECTINFO()
    {
        m_fRef = false;
        m_pName = NULL;
        m_fLocation = false;
        m_fContents = false;
        m_fExits = false;
        m_fNext = false;
        m_fParent = false;
        m_fOwner = false;
        m_fZone = false;
        m_fPennies = false;
        m_fFlags1 = false;
        m_fFlags2 = false;
        m_fFlags3 = false;
        m_fFlags4 = false;
        m_fToggles1 = false;
        m_fToggles2 = false;
        m_fToggles3 = false;
        m_fToggles4 = false;
        m_fToggles5 = false;
        m_fToggles6 = false;
        m_fToggles7 = false;
        m_fToggles8 = false;
        m_fAttrCount = false;
        m_pvai = NULL;
        m_ple = NULL;
        m_pvz = NULL;
    }
    ~R7H_OBJECTINFO()
    {
        free(m_pName);
        delete m_ple;
        delete m_pvz;
        m_pName = NULL;
        m_ple = NULL;
        m_pvz = NULL;
        if (NULL != m_pvai)
        {
            for (vector<R7H_ATTRINFO *>::iterator it = m_pvai->begin(); it != m_pvai->end(); ++it)
            {
               delete *it;
            }
            delete m_pvai;
            m_pvai = NULL;
        }
    }
};

class R7H_GAME
{
public:
    int  m_flags;
    void SetFlags(int flags) { m_flags = flags; }
    int  GetFlags()          { return m_flags;  }

    bool m_fSizeHint;
    int  m_nSizeHint;
    void SetSizeHint(int nSizeHint) { m_fSizeHint = true; m_nSizeHint = nSizeHint; }

    bool m_fNextAttr;
    int  m_nNextAttr;
    void SetNextAttr(int nNextAttr) { m_fNextAttr = true; m_nNextAttr = nNextAttr; }

    bool m_fRecordPlayers;
    int  m_nRecordPlayers;
    void SetRecordPlayers(int nRecordPlayers) { m_fRecordPlayers = true; m_nRecordPlayers = nRecordPlayers; }

    vector<R7H_ATTRNAMEINFO *> m_vAttrNames;
    void AddNumAndName(int iNum, char *pName);

    map<int, R7H_OBJECTINFO *, lti> m_mObjects;
    void AddObject(R7H_OBJECTINFO *poi);

    void Pass2();

    void Validate() const;
    void ValidateFlags() const;
    void ValidateAttrNames(int ver) const;
    void ValidateObjects() const;

    void Write(FILE *fp);
    void Extract(FILE *fp, int dbExtract);

    void ConvertFromP6H();

    void ResetPassword();

    R7H_GAME()
    {
        m_flags = 0;
        m_fSizeHint = false;
        m_fNextAttr = false;
        m_fRecordPlayers = false;
    }
    ~R7H_GAME()
    {
        for (vector<R7H_ATTRNAMEINFO *>::iterator it = m_vAttrNames.begin(); it != m_vAttrNames.end(); ++it)
        {
            delete *it;
        }
        m_vAttrNames.clear();
        for (map<int, R7H_OBJECTINFO *, lti>::iterator it = m_mObjects.begin(); it != m_mObjects.end(); ++it)
        {
            delete it->second;
        }
        m_mObjects.clear();
    }
};

extern R7H_GAME g_r7hgame;
extern int r7hparse();
extern FILE *r7hin;

char *r7h_ConvertAttributeName(const char *);

#endif
