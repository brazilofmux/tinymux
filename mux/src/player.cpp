/*! \file player.cpp
 * \brief Player-related routines.
 *
 * $Id$
 *
 * As opposed to other types of objects, players have passwords, have more
 * limited names, can log in, etc.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "attrs.h"
#include "command.h"
#include "comsys.h"
#include "functions.h"
#include "interface.h"
#include "powers.h"
#include "sha1.h"

#define NUM_GOOD    4   // # of successful logins to save data for.
#define NUM_BAD     3   // # of failed logins to save data for.

typedef struct hostdtm HOSTDTM;
struct hostdtm
{
    UTF8 *host;
    UTF8 *dtm;
};

typedef struct logindata LDATA;
struct logindata
{
    HOSTDTM good[NUM_GOOD];
    HOSTDTM bad[NUM_BAD];
    int tot_good;
    int tot_bad;
    int new_bad;
};


/* ---------------------------------------------------------------------------
 * decrypt_logindata, encrypt_logindata: Decode and encode login info.
 */

static void decrypt_logindata(UTF8 *atrbuf, LDATA *info)
{
    int i;

    info->tot_good = 0;
    info->tot_bad = 0;
    info->new_bad = 0;
    for (i = 0; i < NUM_GOOD; i++)
    {
        info->good[i].host = NULL;
        info->good[i].dtm = NULL;
    }
    for (i = 0; i < NUM_BAD; i++)
    {
        info->bad[i].host = NULL;
        info->bad[i].dtm = NULL;
    }

    if (*atrbuf == '#')
    {
        atrbuf++;
        info->tot_good = mux_atol(grabto(&atrbuf, ';'));
        for (i = 0; i < NUM_GOOD; i++)
        {
            info->good[i].host = grabto(&atrbuf, ';');
            info->good[i].dtm = grabto(&atrbuf, ';');
        }
        info->new_bad = mux_atol(grabto(&atrbuf, ';'));
        info->tot_bad = mux_atol(grabto(&atrbuf, ';'));
        for (i = 0; i < NUM_BAD; i++)
        {
            info->bad[i].host = grabto(&atrbuf, ';');
            info->bad[i].dtm = grabto(&atrbuf, ';');
        }
    }
}

static void encrypt_logindata(UTF8 *atrbuf, LDATA *info)
{
    // Make sure the SPRINTF call tracks NUM_GOOD and NUM_BAD for the number
    // of host/dtm pairs of each type.
    //
    UTF8 nullc = '\0';
    int i;
    for (i = 0; i < NUM_GOOD; i++)
    {
        if (!info->good[i].host)
            info->good[i].host = &nullc;
        if (!info->good[i].dtm)
            info->good[i].dtm = &nullc;
    }
    for (i = 0; i < NUM_BAD; i++)
    {
        if (!info->bad[i].host)
            info->bad[i].host = &nullc;
        if (!info->bad[i].dtm)
            info->bad[i].dtm = &nullc;
    }
    UTF8 *bp = alloc_lbuf("encrypt_logindata");
    mux_sprintf(bp, LBUF_SIZE,
        "#%d;%s;%s;%s;%s;%s;%s;%s;%s;%d;%d;%s;%s;%s;%s;%s;%s;",
        info->tot_good,
        info->good[0].host, info->good[0].dtm,
        info->good[1].host, info->good[1].dtm,
        info->good[2].host, info->good[2].dtm,
        info->good[3].host, info->good[3].dtm,
        info->new_bad, info->tot_bad,
        info->bad[0].host, info->bad[0].dtm,
        info->bad[1].host, info->bad[1].dtm,
        info->bad[2].host, info->bad[2].dtm);
    mux_strncpy(atrbuf, bp, LBUF_SIZE-1);
    free_lbuf(bp);
}

/* ---------------------------------------------------------------------------
 * record_login: Record successful or failed login attempt.
 * If successful, report last successful login and number of failures since
 * last successful login.
 */

void record_login
(
    dbref player,
    bool  isgood,
    UTF8  *ldate,
    UTF8  *lhost,
    UTF8  *lusername,
    UTF8  *lipaddr
)
{
    LDATA login_info;
    dbref aowner;
    int aflags, i;

    UTF8 *atrbuf = atr_get("record_login.143", player, A_LOGINDATA, &aowner, &aflags);
    decrypt_logindata(atrbuf, &login_info);
    if (isgood)
    {
        if (login_info.new_bad > 0)
        {
            notify(player, T(""));
            notify(player, tprintf("**** %d failed connect%s since your last successful connect. ****",
                login_info.new_bad, (login_info.new_bad == 1 ? "" : "s")));
            notify(player, tprintf("Most recent attempt was from %s on %s.",
                login_info.bad[0].host, login_info.bad[0].dtm));
            notify(player, T(""));
            login_info.new_bad = 0;
        }
        if (  login_info.good[0].host
           && *login_info.good[0].host
           && login_info.good[0].dtm
           && *login_info.good[0].dtm)
        {
            notify(player, tprintf("Last connect was from %s on %s.",
                login_info.good[0].host, login_info.good[0].dtm));
        }

        for (i = NUM_GOOD - 1; i > 0; i--)
        {
            login_info.good[i].dtm = login_info.good[i - 1].dtm;
            login_info.good[i].host = login_info.good[i - 1].host;
        }
        login_info.good[0].dtm = ldate;
        login_info.good[0].host = lhost;
        login_info.tot_good++;
        if (*lusername)
        {
            atr_add_raw(player, A_LASTSITE, tprintf("%s@%s", lusername, lhost));
        }
        else
        {
            atr_add_raw(player, A_LASTSITE, lhost);
        }

        // Add the players last IP too.
        //
        atr_add_raw(player, A_LASTIP, lipaddr);
    }
    else
    {
        for (i = NUM_BAD - 1; i > 0; i--)
        {
            login_info.bad[i].dtm = login_info.bad[i - 1].dtm;
            login_info.bad[i].host = login_info.bad[i - 1].host;
        }
        login_info.bad[0].dtm = ldate;
        login_info.bad[0].host = lhost;
        login_info.tot_bad++;
        login_info.new_bad++;
    }
    encrypt_logindata(atrbuf, &login_info);
    atr_add_raw(player, A_LOGINDATA, atrbuf);
    free_lbuf(atrbuf);
}

const UTF8 Base64Table[65] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

#define ENCODED_LENGTH(x) ((((x)+2)/3)*4)

static void EncodeBase64(size_t nIn, const UTF8 *pIn, UTF8 *pOut)
{
    size_t nTriples  = nIn/3;
    size_t nLeftover = nIn%3;
    UINT32 stage;

    const UINT8 *p = (const UINT8 *)pIn;
          UINT8 *q = (      UINT8 *)pOut;

    while (nTriples--)
    {
        stage = (p[0] << 16) | (p[1] << 8) | p[2];

        q[0] = Base64Table[(stage >> 18)       ];
        q[1] = Base64Table[(stage >> 12) & 0x3F];
        q[2] = Base64Table[(stage >>  6) & 0x3F];
        q[3] = Base64Table[(stage      ) & 0x3F];

        q += 4;
        p += 3;
    }

    switch (nLeftover)
    {
    case 1:
        stage = p[0] << 16;

        q[0] = Base64Table[(stage >> 18)       ];
        q[1] = Base64Table[(stage >> 12) & 0x3F];
        q[2] = '=';
        q[3] = '=';

        q += 4;
        break;

    case 2:
        stage = (p[0] << 16) | (p[1] << 8);

        q[0] = Base64Table[(stage >> 18)       ];
        q[1] = Base64Table[(stage >> 12) & 0x3F];
        q[2] = Base64Table[(stage >>  6) & 0x3F];
        q[3] = '=';

        q += 4;
        break;
    }
    q[0] = '\0';
}

#define SHA1_PREFIX_LENGTH 6
const UTF8 szSHA1Prefix[SHA1_PREFIX_LENGTH+1] = "$SHA1$";
#define ENCODED_HASH_LENGTH ENCODED_LENGTH(5*sizeof(UINT32))

#define MD5_PREFIX_LENGTH 3
const UTF8 szMD5Prefix[MD5_PREFIX_LENGTH+1] = "$1$";

#define BLOWFISH_PREFIX_LENGTH 4
const UTF8 szBlowfishPrefix[BLOWFISH_PREFIX_LENGTH+1] = "$2a$";

#define SALT_LENGTH 9
#define ENCODED_SALT_LENGTH ENCODED_LENGTH(SALT_LENGTH)

static const UTF8 *GenerateSalt(void)
{
    UTF8 szSaltRaw[SALT_LENGTH+1];
    int i;
    for (i = 0; i < SALT_LENGTH; i++)
    {
        szSaltRaw[i] = (UTF8)RandomINT32(0, 255);
    }
    szSaltRaw[SALT_LENGTH] = '\0';

    static UTF8 szSaltEncoded[SHA1_PREFIX_LENGTH + ENCODED_SALT_LENGTH+1];
    mux_strncpy(szSaltEncoded, szSHA1Prefix, SHA1_PREFIX_LENGTH);
    EncodeBase64(SALT_LENGTH, szSaltRaw, szSaltEncoded + SHA1_PREFIX_LENGTH);
    return szSaltEncoded;
}

void ChangePassword(dbref player, const UTF8 *szPassword)
{
    int iType;
    s_Pass(player, mux_crypt(szPassword, GenerateSalt(), &iType));
}

#define CRYPT_FAIL        0
#define CRYPT_SHA1        1
#define CRYPT_MD5         2
#define CRYPT_DES         3
#define CRYPT_DES_EXT     4
#define CRYPT_BLOWFISH    5
#define CRYPT_CLEARTEXT   6
#define CRYPT_OTHER       7

const UTF8 szFail[] = "$FAIL$$";

// REMOVE: After 2006-JUL-23, remove support for DES-encrypted passwords on
// Win32 build.  This should allow support for DES-encrypted passwords to
// strattle three distinct versions of MUX.  After that, to convert the older
// passwords automatically would require going through one of these three
// older versions of the server.  Alternatively, since crypt and DES-encrypted
// passwords should be supported on Unix for even longer, converting the
// flatfile on a Unix box remains an option.
//
const UTF8 *mux_crypt(const UTF8 *szPassword, const UTF8 *szSetting, int *piType)
{
    const UTF8 *pSaltField = NULL;
    size_t nSaltField = 0;

    *piType = CRYPT_FAIL;

    if (szSetting[0] == '$')
    {
        const UTF8 *p = (UTF8 *)strchr((char *)szSetting+1, '$');
        if (p)
        {
            p++;
            size_t nAlgo = p - szSetting;
            if (  nAlgo == SHA1_PREFIX_LENGTH
               && memcmp(szSetting, szSHA1Prefix, SHA1_PREFIX_LENGTH) == 0)
            {
                // SHA-1
                //
                pSaltField = p;
                p = (UTF8 *)strchr((char *)pSaltField, '$');
                if (p)
                {
                    nSaltField = p - pSaltField;
                }
                else
                {
                    nSaltField = strlen((char *)pSaltField);
                }
                if (nSaltField <= ENCODED_SALT_LENGTH)
                {
                    *piType = CRYPT_SHA1;
                }
            }
            else if (  nAlgo == MD5_PREFIX_LENGTH
                    && memcmp(szSetting, szMD5Prefix, MD5_PREFIX_LENGTH) == 0)
            {
                *piType = CRYPT_MD5;
            }
            else if (  nAlgo == BLOWFISH_PREFIX_LENGTH
                    && memcmp(szSetting, szBlowfishPrefix, BLOWFISH_PREFIX_LENGTH) == 0)
            {
                *piType = CRYPT_BLOWFISH;
            }
            else
            {
                *piType = CRYPT_OTHER;
            }
        }
    }
    else if (szSetting[0] == '_')
    {
        *piType = CRYPT_DES_EXT;
    }
    else
    {
#if 0
        // Strictly speaking, we can say the algorithm is DES.
        //
        *piType = CRYPT_DES;
#else
        // However, in order to support clear-text passwords, we restrict
        // ourselves to only verifying an existing DES-encrypted password and
        // we assume a fixed salt of 'XX'.  If you have been using a different
        // salt, or if you need to generate a DES-encrypted password, the
        // following code won't work.
        //
        size_t nSetting = strlen((char *)szSetting);
        if (  nSetting == 13
           && memcmp(szSetting, "XX", 2) == 0)
        {
            *piType = CRYPT_DES;
        }
        else
        {
            *piType = CRYPT_CLEARTEXT;
        }
#endif
    }

    switch (*piType)
    {
    case CRYPT_FAIL:
        return szFail;

    case CRYPT_CLEARTEXT:
        return szPassword;

    case CRYPT_MD5:
    case CRYPT_BLOWFISH:
    case CRYPT_OTHER:
    case CRYPT_DES_EXT:
#ifdef WIN32
        // The WIN32 release only supports SHA1 and clear-text.
        //
        return szFail;
#endif // WIN32

    case CRYPT_DES:
#if defined(HAVE_LIBCRYPT) \
 || defined(HAVE_CRYPT)
        return (UTF8 *)crypt((char *)szPassword, (char *)szSetting);
#else
        return szFail;
#endif
    }

    // Calculate SHA-1 Hash.
    //
    SHA1_CONTEXT shac;

    SHA1_Init(&shac);
    SHA1_Compute(&shac, nSaltField, pSaltField);
    SHA1_Compute(&shac, strlen((char *)szPassword), szPassword);
    SHA1_Final(&shac);

    // Serialize 5 UINT32 words into big-endian.
    //
    UTF8 szHashRaw[21];
    UTF8 *p = szHashRaw;

    int i;
    for (i = 0; i <= 4; i++)
    {
        *p++ = (UINT8)(shac.H[i] >> 24);
        *p++ = (UINT8)(shac.H[i] >> 16);
        *p++ = (UINT8)(shac.H[i] >>  8);
        *p++ = (UINT8)(shac.H[i]      );
    }
    *p = '\0';

    //          1         2         3         4
    // 12345678901234567890123456789012345678901234567
    // $SHA1$ssssssssssss$hhhhhhhhhhhhhhhhhhhhhhhhhhhh
    //
    static UTF8 buf[SHA1_PREFIX_LENGTH + ENCODED_SALT_LENGTH + 1 + ENCODED_HASH_LENGTH + 1 + 16];
    mux_strncpy(buf, szSHA1Prefix, SHA1_PREFIX_LENGTH);
    memcpy(buf + SHA1_PREFIX_LENGTH, pSaltField, nSaltField);
    buf[SHA1_PREFIX_LENGTH + nSaltField] = '$';
    EncodeBase64(20, szHashRaw, buf + SHA1_PREFIX_LENGTH + nSaltField + 1);
    return buf;
}

/* ---------------------------------------------------------------------------
 * check_pass: Test a password to see if it is correct.
 */

static bool check_pass(dbref player, const UTF8 *pPassword)
{
    bool bValidPass  = false;
    int  iType;

    int   aflags;
    dbref aowner;
    UTF8 *pTarget = atr_get("check_pass.466", player, A_PASS, &aowner, &aflags);
    if (*pTarget)
    {
        if (strcmp((char *)mux_crypt(pPassword, pTarget, &iType), (char *)pTarget) == 0)
        {
            bValidPass = true;
            if (iType != CRYPT_SHA1)
            {
                ChangePassword(player, pPassword);
            }
        }
    }
#if 0
    else if (GOD == player)
    {
        // When GOD doesn't have a password, we need to a way to set one.
        //
        bValidPass = true;
        ChangePassword(player, pPassword);
    }
#endif
    free_lbuf(pTarget);
    return bValidPass;
}

/* ---------------------------------------------------------------------------
 * connect_player: Try to connect to an existing player.
 */

dbref connect_player(UTF8 *name, UTF8 *password, UTF8 *host, UTF8 *username, UTF8 *ipaddr)
{
    CLinearTimeAbsolute ltaNow;
    ltaNow.GetLocal();
    UTF8 *time_str = ltaNow.ReturnDateString(7);

    dbref player = lookup_player(NOTHING, name, false);
    if (player == NOTHING)
    {
        return NOTHING;
    }
    if (!check_pass(player, password))
    {
        record_login(player, false, time_str, host, username, ipaddr);
        return NOTHING;
    }

    // Compare to last connect see if player gets salary.
    //
    int aflags;
    dbref aowner;
    UTF8 *player_last = atr_get("connect_player.516", player, A_LAST, &aowner, &aflags);
    if (strncmp((char *)player_last, (char *)time_str, 10) != 0)
    {
        UTF8 *allowance = atr_pget(player, A_ALLOWANCE, &aowner, &aflags);
        if (*allowance == '\0')
        {
            giveto(player, mudconf.paycheck);
        }
        else
        {
            giveto(player, mux_atol(allowance));
        }
        free_lbuf(allowance);
    }
    free_lbuf(player_last);
    atr_add_raw(player, A_LAST, time_str);
    return player;
}

void AddToPublicChannel(dbref player)
{
    if (  mudconf.public_channel[0] != '\0'
       && mudconf.public_channel_alias[0] != '\0')
    {
        do_addcom(player, player, player, 0, 2,
            mudconf.public_channel_alias, mudconf.public_channel);
    }
}

/* ---------------------------------------------------------------------------
 * create_player: Create a new player.
 */

dbref create_player
(
    const UTF8 *name,
    const UTF8 *password,
    dbref creator,
    bool isrobot,
    const UTF8 **pmsg
)
{
    *pmsg = NULL;

    // Potentially throttle the rate of player creation.
    //
    if (ThrottlePlayerCreate())
    {
        *pmsg = T("The limit of new players for this hour has been reached. Please try again later.");
        return NOTHING;
    }

    // Make sure the password is OK.  Name is checked in create_obj.
    //
    UTF8 *pbuf = trim_spaces(password);
    if (!ok_password(pbuf, pmsg))
    {
        free_lbuf(pbuf);
        return NOTHING;
    }

    // If so, go create him.
    //
    dbref player = create_obj(creator, TYPE_PLAYER, name, isrobot);
    if (player == NOTHING)
    {
        *pmsg = T("Either there is already a player with that name, or that name is illegal.");
        free_lbuf(pbuf);
        return NOTHING;
    }

    // Initialize everything.
    //
    ChangePassword(player, pbuf);
    s_Home(player, start_home());
    free_lbuf(pbuf);
    local_data_create(player);
    return player;
}

/* ---------------------------------------------------------------------------
 * do_password: Change the password for a player
 */

void do_password
(
    dbref executor,
    dbref caller,
    dbref enactor,
    int   key,
    int   nargs,
    UTF8 *oldpass,
    UTF8 *newpass
)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(key);
    UNUSED_PARAMETER(nargs);

    dbref aowner;
    int   aflags;
    UTF8 *target = atr_get("do_password.618", executor, A_PASS, &aowner, &aflags);
    const UTF8 *pmsg;
    if (  !*target
       || !check_pass(executor, oldpass))
    {
        notify(executor, T("Sorry."));
    }
    else if (ok_password(newpass, &pmsg))
    {
        ChangePassword(executor, newpass);
        notify(executor,(UTF8 *) "Password changed.");
    }
    else
    {
        notify(executor, pmsg);
    }
    free_lbuf(target);
}

/* ---------------------------------------------------------------------------
 * do_last: Display login history data.
 */

static void disp_from_on(dbref player, UTF8 *dtm_str, UTF8 *host_str)
{
    if (dtm_str && *dtm_str && host_str && *host_str)
    {
        notify(player,
               tprintf("     From: %s   On: %s", dtm_str, host_str));
    }
}

void do_last(dbref executor, dbref caller, dbref enactor, int eval, int key, UTF8 *who)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(key);

    dbref target, aowner;
    int i, aflags;

    if (  !who
       || !*who)
    {
        target = Owner(executor);
    }
    else if (string_compare(who, T("me")) == 0)
    {
        target = Owner(executor);
    }
    else
    {
        target = lookup_player(executor, who, true);
    }

    if (target == NOTHING)
    {
        notify(executor, T("I couldn't find that player."));
    }
    else if (!Controls(executor, target))
    {
        notify(executor, NOPERM_MESSAGE);
    }
    else
    {
        UTF8 *atrbuf = atr_get("do_last.684", target, A_LOGINDATA, &aowner, &aflags);
        LDATA login_info;
        decrypt_logindata(atrbuf, &login_info);

        notify(executor, tprintf("Total successful connects: %d", login_info.tot_good));
        for (i = 0; i < NUM_GOOD; i++)
        {
            disp_from_on(executor, login_info.good[i].host, login_info.good[i].dtm);
        }
        notify(executor, tprintf("Total failed connects: %d", login_info.tot_bad));
        for (i = 0; i < NUM_BAD; i++)
        {
            disp_from_on(executor, login_info.bad[i].host, login_info.bad[i].dtm);
        }
        free_lbuf(atrbuf);
    }
}

/* ---------------------------------------------------------------------------
 * add_player_name, delete_player_name, lookup_player:
 * Manage playername->dbref mapping
 */

bool add_player_name(dbref player, const UTF8 *name)
{
    bool stat = false;

    // Convert to all lowercase.
    //
    size_t nCased;
    UTF8  *pCased = mux_strlwr(name, nCased);

    dbref *p = (int *)hashfindLEN(pCased, nCased, &mudstate.player_htab);
    if (p)
    {
        // Entry found in the hashtable.  If a player, succeed if the
        // numbers match (already correctly in the hash table), fail
        // if they don't.
        //
        if (  Good_obj(*p)
           && isPlayer(*p))
        {
            if (*p == player)
            {
                return true;
            }
            else
            {
                return false;
            }
        }

        // It's an alias (or an incorrect entry). Clobber it.
        //
        delete p;

        p = NULL;
        try
        {
            p = new dbref;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (NULL != p)
        {
            *p = player;
            stat = hashreplLEN(pCased, nCased, p, &mudstate.player_htab);
        }
        else
        {
            ISOUTOFMEMORY(p);
        }
    }
    else
    {
        p = NULL;
        try
        {
            p = new dbref;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (NULL != p)
        {
            *p = player;
            stat = hashaddLEN(pCased, nCased, p, &mudstate.player_htab);
        }
        else
        {
            ISOUTOFMEMORY(p);
        }
    }
    return stat;
}

bool delete_player_name(dbref player, const UTF8 *name)
{
    size_t nCased;
    UTF8  *pCased = mux_strlwr(name, nCased);

    dbref *p = (int *)hashfindLEN(pCased, nCased, &mudstate.player_htab);
    if (  !p
       || *p == NOTHING
       || (  player != NOTHING
          && *p != player))
    {
        return false;
    }
    delete p;
    p = NULL;
    hashdeleteLEN(pCased, nCased, &mudstate.player_htab);
    return true;
}

dbref lookup_player(dbref doer, UTF8 *name, bool check_who)
{
    if (string_compare(name, T("me")) == 0)
    {
        return doer;
    }

    while (*name == LOOKUP_TOKEN)
    {
        name++;
    }
    dbref thing;
    if (*name == NUMBER_TOKEN)
    {
        name++;
        if (!is_integer(name, NULL))
        {
            return NOTHING;
        }
        thing = mux_atol(name);
        if (!Good_obj(thing))
        {
            return NOTHING;
        }
        if ( !(  isPlayer(thing)
              || God(doer)))
        {
            thing = NOTHING;
        }
        return thing;
    }

    size_t nCased;
    UTF8  *pCased = mux_strlwr(name, nCased);
    dbref *p = (int *)hashfindLEN(pCased, nCased, &mudstate.player_htab);
    if (!p)
    {
        if (check_who)
        {
            thing = find_connected_name(doer, name);
            if (Hidden(thing))
            {
                thing = NOTHING;
            }
        }
        else
        {
            thing = NOTHING;
        }
    }
    else if (!Good_obj(*p))
    {
        thing = NOTHING;
    }
    else
    {
        thing = *p;
    }

    return thing;
}

void load_player_names(void)
{
    dbref i;
    DO_WHOLE_DB(i)
    {
        if (isPlayer(i))
        {
            add_player_name(i, Name(i));
        }
    }
    UTF8 *alias = alloc_lbuf("load_player_names");
    DO_WHOLE_DB(i)
    {
        if (isPlayer(i))
        {
            dbref aowner;
            int aflags;
            alias = atr_pget_str(alias, i, A_ALIAS, &aowner, &aflags);
            if (*alias)
            {
                add_player_name(i, alias);
            }
        }
    }
    free_lbuf(alias);
}

/* ---------------------------------------------------------------------------
 * badname_add, badname_check, badname_list: Add/look for/display bad names.
 */

void badname_add(UTF8 *bad_name)
{
    // Make a new node and link it in at the top.
    //
    BADNAME *bp = NULL;
    try
    {
        bp = new BADNAME;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (NULL != bp)
    {
        bp->name = StringClone(bad_name);
        bp->next = mudstate.badname_head;
        mudstate.badname_head = bp;
    }
    else
    {
        ISOUTOFMEMORY(bp);
    }
}

void badname_remove(UTF8 *bad_name)
{
    // Look for an exact match on the bad name and remove if found.
    //
    BADNAME *bp;
    BADNAME *backp = NULL;
    for (bp = mudstate.badname_head; bp; backp = bp, bp = bp->next)
    {
        if (!string_compare(bad_name, bp->name))
        {
            if (backp)
            {
                backp->next = bp->next;
            }
            else
            {
                mudstate.badname_head = bp->next;
            }
            MEMFREE(bp->name);
            bp->name = NULL;
            delete bp;
            bp = NULL;
            return;
        }
    }
}

bool badname_check(UTF8 *bad_name)
{
    BADNAME *bp;

    // Walk the badname list, doing wildcard matching.  If we get a hit then
    // return false.  If no matches in the list, return true.
    //
    for (bp = mudstate.badname_head; bp; bp = bp->next)
    {
        mudstate.wild_invk_ctr = 0;
        if (quick_wild(bp->name, bad_name))
        {
            return false;
        }
    }
    return true;
}

void badname_list(dbref player, const UTF8 *prefix)
{
    BADNAME *bp;
    UTF8 *buff, *bufp;

    // Construct an lbuf with all the names separated by spaces.
    //
    buff = bufp = alloc_lbuf("badname_list");
    safe_str(prefix, buff, &bufp);
    for (bp = mudstate.badname_head; bp; bp = bp->next)
    {
        safe_chr(' ', buff, &bufp);
        safe_str(bp->name, buff, &bufp);
    }
    *bufp = '\0';

    // Now display it.
    //
    notify(player, buff);
    free_lbuf(buff);
}
