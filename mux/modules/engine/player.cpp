/*! \file player.cpp
 * \brief Player-related routines.
 *
 * As opposed to other types of objects, players have passwords, have more
 * limited names, can log in, etc.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"
#include "functions.h"
#include "sha1.h"

#define NUM_GOOD    4   // # of successful logins to save data for.
#define NUM_BAD     3   // # of failed logins to save data for.

typedef struct hostdtm HOSTDTM;
struct hostdtm
{
    const UTF8 *host;
    const UTF8 *dtm;
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

NAMETAB method_nametab[] =
{
    {T("sha1"),            4,  CA_GOD,     CRYPT_SHA1},
    {T("des"),             3,  CA_GOD,     CRYPT_DES},
    {T("md5"),             3,  CA_GOD,     CRYPT_MD5},
    {T("sha256"),          6,  CA_GOD,     CRYPT_SHA256},
    {T("sha512"),          6,  CA_GOD,     CRYPT_SHA512},
    { nullptr,     0,       0,     0}
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
        info->good[i].host = nullptr;
        info->good[i].dtm = nullptr;
    }
    for (i = 0; i < NUM_BAD; i++)
    {
        info->bad[i].host = nullptr;
        info->bad[i].dtm = nullptr;
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
    LBuf bp = LBuf_Src("encrypt_logindata");
    mux_sprintf(bp.get(), LBUF_SIZE,
        T("#%d;%s;%s;%s;%s;%s;%s;%s;%s;%d;%d;%s;%s;%s;%s;%s;%s;"),
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
    const UTF8  *ldate,
    const UTF8  *lhost,
    const UTF8  *lusername,
    const UTF8  *lipaddr
)
{
    LDATA login_info;
    dbref aowner;
    int aflags, i;

    LBuf atrbuf = LBuf_Adopt(atr_get("record_login.143", player, A_LOGINDATA, &aowner, &aflags));
    decrypt_logindata(atrbuf, &login_info);
    if (isgood)
    {
        if (login_info.new_bad > 0)
        {
            notify(player, T(""));
            notify(player, tprintf(T("**** %d failed connect%s since your last successful connect. ****"),
                login_info.new_bad, (login_info.new_bad == 1 ? "" : "s")));
            notify(player, tprintf(T("Most recent attempt was from %s on %s."),
                login_info.bad[0].host, login_info.bad[0].dtm));
            notify(player, T(""));
            login_info.new_bad = 0;
        }
        if (  login_info.good[0].host
           && *login_info.good[0].host
           && login_info.good[0].dtm
           && *login_info.good[0].dtm)
        {
            notify(player, tprintf(T("Last connect was from %s on %s."),
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
            atr_add_raw(player, A_LASTSITE, tprintf(T("%s@%s"), lusername, lhost));
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
}

const UTF8 Base64Table[65] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

#define ENCODED_LENGTH(x) ((((x)+2)/3)*4)

static void EncodeBase64(size_t nIn, const UTF8 *pIn, UTF8 *pOut)
{
    size_t nTriples  = nIn/3;
    size_t nLeftover = nIn%3;
    uint32_t stage;

    const uint8_t *p = (const uint8_t *)pIn;
          uint8_t *q = (      uint8_t *)pOut;

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

// Historically, TinyMUX DES passwords use a fixed salt of 'XX', but DES-based
// crypt is not limited to this in general.  Because of the fixed salt, any
// encrypted password that did not begin with a salt of 'XX' was interpreted
// as a clear-text password.
//
// A fixed salt completely undermines the purpose of salting passwords, but
// to support the legacy behavior, and to provide a path for clear-text
// passwords, the default behavior is to continue limiting salt to 'XX'.  To
// remove this limit, uncomment the line that follows:
//
//#define ENABLE_PROPER_DES

const UTF8 szFail[] = "$FAIL$$";

const UTF8 szSHA1Prefix[] = "$SHA1$";
#define SHA1_PREFIX_LENGTH (sizeof(szSHA1Prefix)-1)
#define SHA1_HASH_LENGTH 5*sizeof(uint32_t)
#define SHA1_ENCODED_HASH_LENGTH ENCODED_LENGTH(SHA1_HASH_LENGTH)
#define SHA1_SALT_LENGTH 9
#define SHA1_ENCODED_SALT_LENGTH ENCODED_LENGTH(SHA1_SALT_LENGTH)

#define DES_SALT_LENGTH 2

const UTF8 szMD5Prefix[] = "$1$";
#define MD5_PREFIX_LENGTH (sizeof(szMD5Prefix)-1)
#define MD5_SALT_LENGTH 16

const UTF8 szSHA256Prefix[] = "$5$";
#define SHA256_PREFIX_LENGTH (sizeof(szSHA256Prefix)-1)
#define SHA256_SALT_LENGTH 16

const UTF8 szSHA512Prefix[] = "$6$";
#define SHA512_PREFIX_LENGTH (sizeof(szSHA512Prefix)-1)
#define SHA512_SALT_LENGTH 16

const UTF8 szP6HPrefix[] = "$P6H$";
#define P6H_PREFIX_LENGTH (sizeof(szP6HPrefix)-1)
#define P6H_XX_HASH_LENGTH_MAX 40

const UTF8 szP6HPrefix1SHA1[] = "$P6H$$1:sha1:";
#define P6H_VAHT_1SHA1_PREFIX_LENGTH (sizeof(szP6HPrefix1SHA1)-1)
#define P6H_VAHT_HASH_LENGTH_MAX (2*SHA1_HASH_LENGTH)
#define P6H_VAHT_TIMESTAMP_LENGTH_MAX 11

// These are known but passed through as CRYPT_OTHER:
//
// Blowfish   $2a$

static const UTF8 *GenerateSalt(int iType)
{
    // Must be large enough for any supported format: prefix + salt + NUL.
    //   DES:    0 + 2  + 1 =  3
    //   MD5:    3 + 16 + 1 = 20
    //   SHA1:   6 + 12 + 1 = 19
    //   SHA256: 3 + 16 + 1 = 20
    //   SHA512: 3 + 16 + 1 = 20
    //
    static constexpr size_t MAX_SALT_SIZE = 32;
    thread_local UTF8 szSalt[MAX_SALT_SIZE];

    szSalt[0] = '\0';
    if (CRYPT_SHA1 == iType)
    {
        UTF8 szSaltRaw[SHA1_SALT_LENGTH+1];
        for (int i = 0; i < SHA1_SALT_LENGTH; i++)
        {
            szSaltRaw[i] = static_cast<UTF8>(RandomINT32(0, 255));
        }
        szSaltRaw[SHA1_SALT_LENGTH] = '\0';

        mux_strncpy(szSalt, szSHA1Prefix, SHA1_PREFIX_LENGTH);
        EncodeBase64(SHA1_SALT_LENGTH, szSaltRaw, szSalt + SHA1_PREFIX_LENGTH);
    }
    else if (CRYPT_DES == iType)
    {
#if defined(ENABLE_PROPER_DES)
        for (int i = 0; i < DES_SALT_LENGTH; i++)
        {
            // Map random number to set 'a-zA-Z0-9./'.
            //
            int32_t j = RandomINT32(0, sizeof(Base64Table)-1);
            UTF8 ch = Base64Table[j];
            if ('+' == ch)
            {
                ch = '.';
            }
            szSalt[i] = ch;
        }
        szSalt[DES_SALT_LENGTH] = '\0';
#else
        return T("XX");
#endif
    }
    else if (  CRYPT_MD5 == iType
            || CRYPT_SHA256 == iType
            || CRYPT_SHA512 == iType)
    {
        const UTF8 *pPrefix = nullptr;
        size_t      nPrefix = 0;
        size_t      nSalt = 0;

        if (CRYPT_MD5 == iType)
        {
            pPrefix = szMD5Prefix;
            nPrefix = MD5_PREFIX_LENGTH;
            nSalt   = MD5_SALT_LENGTH;
        }
        else if (CRYPT_SHA256 == iType)
        {
            pPrefix = szSHA256Prefix;
            nPrefix = SHA256_PREFIX_LENGTH;
            nSalt   = SHA256_SALT_LENGTH;
        }
        else if (CRYPT_SHA512 == iType)
        {
            pPrefix = szSHA512Prefix;
            nPrefix = SHA512_PREFIX_LENGTH;
            nSalt   = SHA512_SALT_LENGTH;
        }

        mux_strncpy(szSalt, pPrefix, nPrefix);
        for (size_t i = nPrefix; i < nPrefix + nSalt; i++)
        {
            // Map random number to set 'a-zA-Z0-9./'.
            //
            int32_t j = RandomINT32(0, sizeof(Base64Table)-1);
            UTF8 ch = Base64Table[j];
            if ('+' == ch)
            {
                ch = '.';
            }
            szSalt[i] = ch;
        }
        szSalt[nPrefix + nSalt] = '\0';
    }
    return szSalt;
}

void ChangePassword(dbref player, const UTF8 *szPassword)
{
    int iTypeOut;
    const UTF8 *pEncodedPassword = nullptr;
    int methods[] = { CRYPT_SHA512, CRYPT_SHA256, CRYPT_MD5, CRYPT_SHA1, CRYPT_DES };
    for (size_t i = 0; i < sizeof(methods)/sizeof(methods[0]); i++)
    {
        if (  (mudconf.password_methods & methods[i])
           && nullptr != (pEncodedPassword = mux_crypt(szPassword, GenerateSalt(methods[i]), &iTypeOut)))
        {
            break;
        }
    }

    if (nullptr == pEncodedPassword)
    {
        pEncodedPassword = mux_crypt(szPassword, GenerateSalt(CRYPT_SHA1), &iTypeOut);
        mux_assert(nullptr != pEncodedPassword);
    }
    s_Pass(player, pEncodedPassword);
}

#if defined(UNIX_DIGEST) && defined(HAVE_SHA_INIT)
const UTF8 *p6h_xx_crypt(const UTF8 *szPassword)
{
    // Calculate SHA-0 Hash.
    //
    SHA_CTX shac;
    UTF8 szHashRaw[SHA_DIGEST_LENGTH];
    SHA_Init(&shac);
    SHA_Update(&shac, szPassword, strlen(reinterpret_cast<const char *>(szPassword)));
    SHA_Final(szHashRaw, &shac);

    //          1         2
    // 1234567890123456789012345678
    // $P6H$$XXhhhhhhhhhhhhhhhhhhhh
    //
    thread_local UTF8 buf[P6H_PREFIX_LENGTH + 1 + P6H_XX_HASH_LENGTH_MAX + 1 + 16];
    mux_strncpy(buf, szP6HPrefix, P6H_PREFIX_LENGTH);
    buf[P6H_PREFIX_LENGTH] = '$';

    unsigned int a = (static_cast<unsigned int>(szHashRaw[0])) << 24
                   | (static_cast<unsigned int>(szHashRaw[1])) << 16
                   | (static_cast<unsigned int>(szHashRaw[2])) <<  8
                   | (static_cast<unsigned int>(szHashRaw[3]));

    unsigned int b = (static_cast<unsigned int>(szHashRaw[4])) << 24
                   | (static_cast<unsigned int>(szHashRaw[5])) << 16
                   | (static_cast<unsigned int>(szHashRaw[6])) <<  8
                   | (static_cast<unsigned int>(szHashRaw[7]));

    mux_sprintf(buf + P6H_PREFIX_LENGTH + 1, P6H_XX_HASH_LENGTH_MAX, T("XX%lu%lu"), a, b);
    return buf;
}
#endif

const UTF8 *p6h_vaht_crypt(const UTF8 *szPassword, const UTF8 *szSetting)
{
    size_t nSetting = strlen(reinterpret_cast<const char *>(szSetting));
    if (  P6H_VAHT_1SHA1_PREFIX_LENGTH <= nSetting
       && memcmp(szSetting, szP6HPrefix1SHA1, P6H_VAHT_1SHA1_PREFIX_LENGTH) == 0)
    {
        // Calculate SHA-1 Hash.
        //
#ifdef UNIX_DIGEST
        uint8_t md[EVP_MAX_MD_SIZE];
#else
        uint8_t md[MUX_SHA1_DIGEST_LENGTH];
#endif
        unsigned int len = 0;
        const UTF8 *parts[] = { szPassword };
        const size_t lens[] = { strlen(reinterpret_cast<const char *>(szPassword)) };
        if (mux_sha1_digest(parts, lens, 1, md, &len))
        {
            //          1         2         3         4         5         6
            // 123456789012345678901234567890123456789012345678901234567890123456
            // $P6H$$1:sha1:hhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhh:tttttttttttt
            //
            thread_local UTF8 buff[LBUF_SIZE];
            UTF8 *bufc = buff;

            safe_str(szP6HPrefix1SHA1, buff, &bufc);
            safe_hex(md, len, false, buff, &bufc);
            safe_chr(':', buff, &bufc);
            safe_str(szSetting + P6H_VAHT_1SHA1_PREFIX_LENGTH + P6H_VAHT_HASH_LENGTH_MAX + 1, buff, &bufc);
            *bufc = '\0';

            return buff;
        }
    }
    return szFail;
}

// There is no longer any support for DES-encrypted passwords on the Windows
// build.  To convert these, using #1 to @newpassword, go through an older
// version of TinyMUX, or go through a Unix host.
//
const UTF8 *mux_crypt(const UTF8 *szPassword, const UTF8 *szSetting, int *piType)
{
    const UTF8 *pSaltField = nullptr;
    size_t nSaltField = 0;

    *piType = CRYPT_FAIL;

    if (szSetting[0] == '$')
    {
        const UTF8 *p = reinterpret_cast<const UTF8 *>(strchr(reinterpret_cast<const char *>(szSetting)+1, '$'));
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
                p = reinterpret_cast<const UTF8 *>(strchr(reinterpret_cast<const char *>(pSaltField), '$'));
                if (p)
                {
                    nSaltField = p - pSaltField;
                }
                else
                {
                    nSaltField = strlen(reinterpret_cast<const char *>(pSaltField));
                }
                if (nSaltField <= SHA1_ENCODED_SALT_LENGTH)
                {
                    *piType = CRYPT_SHA1;
                }
            }
            else if (  nAlgo == MD5_PREFIX_LENGTH
                    && memcmp(szSetting, szMD5Prefix, MD5_PREFIX_LENGTH) == 0)
            {
                *piType = CRYPT_MD5;
            }
            else if (  nAlgo == SHA256_PREFIX_LENGTH
                    && memcmp(szSetting, szSHA256Prefix, SHA256_PREFIX_LENGTH) == 0)
            {
                *piType = CRYPT_SHA256;
            }
            else if (  nAlgo == SHA512_PREFIX_LENGTH
                    && memcmp(szSetting, szSHA512Prefix, SHA512_PREFIX_LENGTH) == 0)
            {
                *piType = CRYPT_SHA512;
            }
            else if (  nAlgo == P6H_PREFIX_LENGTH
                    && memcmp(szSetting, szP6HPrefix, P6H_PREFIX_LENGTH) == 0)
            {
#ifdef UNIX_DIGEST
                if ('X' == p[0] && 'X' == p[1])
                {
                    *piType = CRYPT_P6H_XX;
                }
                else
#endif
                {
                    *piType = CRYPT_P6H_VAHT;
                }
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
#if defined(ENABLE_PROPER_DES)
        // Strictly speaking, we can say the algorithm is DES.
        //
        *piType = CRYPT_DES;
#else
        // However, in order to support clear-text passwords, we restrict
        // ourselves to only verifying an existing DES-encrypted password and
        // we assume a fixed salt of 'XX'.  If you have been using a different
        // salt, the following code won't work.
        //
        size_t nSetting = strlen(reinterpret_cast<const char *>(szSetting));
        if (  2 <= nSetting
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

#if defined(UNIX_DIGEST) && defined(HAVE_SHA_INIT)
    case CRYPT_P6H_XX:
        return p6h_xx_crypt(szPassword);
#endif
    case CRYPT_P6H_VAHT:
        return p6h_vaht_crypt(szPassword, szSetting);

    case CRYPT_OTHER:
    case CRYPT_DES_EXT:
    case CRYPT_MD5:
    case CRYPT_SHA256:
    case CRYPT_SHA512:
#if defined(WINDOWS_CRYPT)
        // The Windows release of TinyMUX only supports SHA1 and clear-text.
        //
        return szFail;
#endif // WINDOWS_CRYPT

    case CRYPT_DES:
#if defined(HAVE_CRYPT)
        return reinterpret_cast<const UTF8 *>(crypt(reinterpret_cast<const char *>(szPassword), reinterpret_cast<const char *>(szSetting)));
#else
        return szFail;
#endif
    }

    // Calculate SHA-1 Hash.
    //
#ifdef UNIX_DIGEST
    uint8_t md[EVP_MAX_MD_SIZE+1];
#else
    uint8_t md[MUX_SHA1_DIGEST_LENGTH+1];
#endif
    unsigned int len = 0;
    const UTF8 *parts[] = { pSaltField, szPassword };
    const size_t lens[] = { nSaltField, strlen(reinterpret_cast<const char *>(szPassword)) };
    if (!mux_sha1_digest(parts, lens, 2, md, &len))
    {
        return szFail;
    }
    md[len] = '\0';

    //          1         2         3         4
    // 12345678901234567890123456789012345678901234567
    // $SHA1$ssssssssssss$hhhhhhhhhhhhhhhhhhhhhhhhhhhh
    //
    thread_local UTF8 buf[SHA1_PREFIX_LENGTH + SHA1_ENCODED_SALT_LENGTH + 1 + SHA1_ENCODED_HASH_LENGTH + 1 + 16];
    mux_strncpy(buf, szSHA1Prefix, SHA1_PREFIX_LENGTH);
    memcpy(buf + SHA1_PREFIX_LENGTH, pSaltField, nSaltField);
    buf[SHA1_PREFIX_LENGTH + nSaltField] = '$';
    EncodeBase64(len, md, buf + SHA1_PREFIX_LENGTH + nSaltField + 1);
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
    LBuf pTarget = LBuf_Adopt(atr_get("check_pass.466", player, A_PASS, &aowner, &aflags));
    if (*pTarget)
    {
        if (strcmp(reinterpret_cast<const char *>(mux_crypt(pPassword, pTarget, &iType)), reinterpret_cast<const char *>(pTarget.get())) == 0)
        {
            bValidPass = true;
            if (0 == (iType & mudconf.password_methods))
            {
                ChangePassword(player, pPassword);
            }
        }
    }
    return bValidPass;
}

/* ---------------------------------------------------------------------------
 * connect_player: Try to connect to an existing player.
 */

dbref connect_player(UTF8 *name, UTF8 *password, UTF8 *host, UTF8 *username, UTF8 *ipaddr)
{
    CLinearTimeAbsolute ltaNow;
    ltaNow.GetLocal();
    const UTF8 *time_str = ltaNow.ReturnDateString(7);

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
    LBuf player_last = LBuf_Adopt(atr_get("connect_player.516", player, A_LAST, &aowner, &aflags));
    if (strncmp(reinterpret_cast<const char *>(player_last.get()), reinterpret_cast<const char *>(time_str), 10) != 0)
    {
        LBuf allowance = LBuf_Adopt(atr_pget(player, A_ALLOWANCE, &aowner, &aflags));
        if (*allowance == '\0')
        {
            giveto(player, mudconf.paycheck);
        }
        else
        {
            giveto(player, mux_atol(allowance));
        }
    }
    atr_add_raw(player, A_LAST, time_str);
    return player;
}

void AddToPublicChannel(dbref player)
{
    if (  mudconf.public_channel[0] != '\0'
       && mudconf.public_channel_alias[0] != '\0')
    {
        do_addcom(player, player, player, 0, 0, 2,
            mudconf.public_channel_alias, mudconf.public_channel, nullptr, 0);
    }
}

void AddToPlayerChannels(dbref player)
{
    if ('\0' == mudconf.player_channels[0])
    {
        return;
    }

    LBuf buff = LBuf_Src("AddToPlayerChannels");
    mux_strncpy(buff, mudconf.player_channels, LBUF_SIZE - 1);

    UTF8 *p = buff.get();
    while ('\0' != *p)
    {
        // Skip leading spaces.
        //
        while (mux_isspace(*p))
        {
            p++;
        }
        if ('\0' == *p)
        {
            break;
        }

        // Channel name.
        //
        UTF8 *channel = p;
        while ('\0' != *p && !mux_isspace(*p))
        {
            p++;
        }
        if ('\0' != *p)
        {
            *p++ = '\0';
        }

        // Skip spaces between channel and alias.
        //
        while (mux_isspace(*p))
        {
            p++;
        }
        if ('\0' == *p)
        {
            // Odd trailing token (channel without alias) — skip.
            //
            break;
        }

        // Alias.
        //
        UTF8 *alias = p;
        while ('\0' != *p && !mux_isspace(*p))
        {
            p++;
        }
        if ('\0' != *p)
        {
            *p++ = '\0';
        }

        do_addcom(player, player, player, 0, 0, 2, alias, channel,
            nullptr, 0);
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
    *pmsg = nullptr;

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

    // Check if the name is protected by another player.
    //
    if (!protectname_check(name, NOTHING))
    {
        *pmsg = T("That name is protected by another player.");
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
    if (mudconf.talk_mode_default)
    {
        s_Flags(player, FLAG_WORD2, Flags2(player) | TALKMODE);
    }
    local_data_create(player);
    ServerEventsSinkNode *p = g_pServerEventsSinkListHead;
    while (nullptr != p)
    {
        p->pSink->data_create(player);
        p = p->pNext;
    }
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
    int   eval,
    int   key,
    int   nargs,
    UTF8 *oldpass,
    UTF8 *newpass,
    const UTF8 *cargs[],
    int   ncargs
)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(key);
    UNUSED_PARAMETER(nargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref aowner;
    int   aflags;
    LBuf target = LBuf_Adopt(atr_get("do_password.618", executor, A_PASS, &aowner, &aflags));
    const UTF8 *pmsg;
    if (  !*target
       || !check_pass(executor, oldpass))
    {
        notify(executor, T("Sorry."));
    }
    else if (ok_password(newpass, &pmsg))
    {
        ChangePassword(executor, newpass);
        notify(executor,T("Password changed."));
    }
    else
    {
        notify(executor, pmsg);
    }
}

/* ---------------------------------------------------------------------------
 * do_last: Display login history data.
 */

static void disp_from_on(dbref player, const UTF8 *dtm_str, const UTF8 *host_str)
{
    if (dtm_str && *dtm_str && host_str && *host_str)
    {
        notify(player,
               tprintf(T("     From: %s   On: %s"), dtm_str, host_str));
    }
}

void do_last(dbref executor, dbref caller, dbref enactor, int eval, int key, UTF8 *who, const UTF8 *cargs[], int ncargs)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(key);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

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
        notify(executor, T("I couldn\xE2\x80\x99t find that player."));
    }
    else if (!(  WizRoy(executor)
              || Controls(executor, target)))
    {
        notify(executor, NOPERM_MESSAGE);
    }
    else
    {
        LBuf atrbuf = LBuf_Adopt(atr_get("do_last.684", target, A_LOGINDATA, &aowner, &aflags));
        LDATA login_info;
        decrypt_logindata(atrbuf, &login_info);

        notify(executor, tprintf(T("Total successful connects: %d"), login_info.tot_good));
        for (i = 0; i < NUM_GOOD; i++)
        {
            disp_from_on(executor, login_info.good[i].host, login_info.good[i].dtm);
        }
        notify(executor, tprintf(T("Total failed connects: %d"), login_info.tot_bad));
        for (i = 0; i < NUM_BAD; i++)
        {
            disp_from_on(executor, login_info.bad[i].host, login_info.bad[i].dtm);
        }
    }
}

/* ---------------------------------------------------------------------------
 * add_player_name, delete_player_name, lookup_player:
 * Manage playername->dbref mapping
 */

typedef struct
{
    dbref dbPlayer;
    bool  bAlias;
} player_name_entry;

bool add_player_name(dbref player, const UTF8 *name, bool bAlias)
{
    if (  !Good_obj(player)
       || !isPlayer(player))
    {
        return false;
    }

    bool stat = false;

    // Convert to all lowercase.
    //
    size_t nCased;
    UTF8  *pCased = mux_strlwr(name, nCased);

    auto it = mudstate.player_htab.find(std::vector<UTF8>(pCased, pCased + nCased));

    if (it != mudstate.player_htab.end())
    {
        player_name_entry *p = static_cast<player_name_entry *>(it->second);

        // Entry found in the hashtable.  Succeed if the numbers are already
        // correctly in the hash table.
        //
        if (  Good_obj(p->dbPlayer)
           && isPlayer(p->dbPlayer))
        {
            if (  p->dbPlayer == player
               && p->bAlias == bAlias)
            {
                return true;
            }
            else
            {
                return false;
            }
        }

        // It's an invalid entry.  Clobber it.
        //
        player_name_entry *pOrig = p;
        p = nullptr;
        try
        {
            p = new player_name_entry;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (nullptr != p)
        {
            p->dbPlayer = player;
            p->bAlias = bAlias;
            it->second = p;
            stat = true;
            delete pOrig;
            pOrig = nullptr;
        }
    }
    else
    {
        player_name_entry *p = nullptr;
        try
        {
            p = new player_name_entry;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (nullptr != p)
        {
            p->dbPlayer = player;
            p->bAlias = bAlias;
            mudstate.player_htab.emplace(std::vector<UTF8>(pCased, pCased + nCased), p);
            stat = true;
        }
    }
    return stat;
}

bool delete_player_name(dbref player, const UTF8 *name, bool bAlias)
{
    if (NOTHING == player)
    {
        return false;
    }

    size_t nCased;
    UTF8  *pCased = mux_strlwr(name, nCased);

    auto it = mudstate.player_htab.find(std::vector<UTF8>(pCased, pCased + nCased));

    if (it == mudstate.player_htab.end())
    {
        return false;
    }

    player_name_entry *p = static_cast<player_name_entry *>(it->second);

    if (  Good_obj(p->dbPlayer)
       && isPlayer(p->dbPlayer)
       && (  p->dbPlayer != player
          || p->bAlias != bAlias))
    {
        return false;
    }

    delete p;
    p = nullptr;
    mudstate.player_htab.erase(it);
    return true;
}

#ifdef SELFCHECK
void delete_all_player_names()
{
    for (auto &[key, val] : mudstate.player_htab)
    {
        player_name_entry *pne = static_cast<player_name_entry *>(val);
        delete pne;
    }
    mudstate.player_htab.clear();
}
#endif

dbref lookup_player_name(const UTF8 *name, bool &bAlias)
{
    dbref thing = NOTHING;
    size_t nCased;
    UTF8  *pCased = mux_strlwr(name, nCased);
    auto it = mudstate.player_htab.find(std::vector<UTF8>(pCased, pCased + nCased));
    if (it != mudstate.player_htab.end())
    {
        player_name_entry *p = static_cast<player_name_entry *>(it->second);
        if (  nullptr != p
           && Good_obj(p->dbPlayer))
        {
            thing = p->dbPlayer;
            bAlias = p->bAlias;
        }
    }
    return thing;
}

dbref lookup_player(dbref doer, UTF8 *name, bool check_who)
{
    if (string_compare(name, T("me")) == 0)
    {
        return doer;
    }

    while (LOOKUP_TOKEN == name[0])
    {
        name++;
    }

    dbref thing = NOTHING;
    if (NUMBER_TOKEN == name[0])
    {
        name++;
        if (!is_integer(name, nullptr))
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

    bool bAlias = false;
    thing = lookup_player_name(name, bAlias);
    if (  NOTHING == thing
       && check_who)
    {
        thing = find_connected_name(doer, name);
        if (Hidden(thing))
        {
            thing = NOTHING;
        }
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
            add_player_name(i, Name(i), false);
        }
    }
    LBuf alias = LBuf_Src("load_player_names");
    DO_WHOLE_DB(i)
    {
        if (isPlayer(i))
        {
            dbref aowner;
            int aflags;
            atr_pget_str(alias, i, A_ALIAS, &aowner, &aflags);
            if (alias[0])
            {
                add_player_name(i, alias, true);
            }
        }
    }
}

/* ---------------------------------------------------------------------------
 * badname_add, badname_check, badname_list: Add/look for/display bad names.
 */

void badname_add(UTF8 *bad_name)
{
    // Make a new node and link it in at the top.
    //
    BADNAME *bp = nullptr;
    try
    {
        bp = new BADNAME;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr != bp)
    {
        bp->name = StringClone(bad_name);
        bp->next = mudstate.badname_head;
        mudstate.badname_head = bp;
    }
    else
    {
        STARTLOG(LOG_PROBLEMS, "NAM", "MEM");
        log_printf(T("badname_add: out of memory."));
        ENDLOG;
    }
}

void badname_remove(UTF8 *bad_name)
{
    // Look for an exact match on the bad name and remove if found.
    //
    BADNAME *bp;
    BADNAME *backp = nullptr;
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
            bp->name = nullptr;
            delete bp;
            bp = nullptr;
            return;
        }
    }
}

bool badname_check(const UTF8 *bad_name)
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
    UTF8 *bufp;

    // Construct an lbuf with all the names separated by spaces.
    //
    LBuf buff = LBuf_Src("badname_list");
    bufp = buff.get();
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
}

// ---------------------------------------------------------------------------
// protectname_check: Check if a name is protected by another player.
// Returns true if the name is available (not protected by someone else).
//
bool protectname_check(const UTF8 *name, dbref player)
{
    SEP sepPipe = { 1, { '|' } };
    dbref i;
    DO_WHOLE_DB(i)
    {
        if (  !isPlayer(i)
           || i == player)
        {
            continue;
        }

        dbref aowner;
        int aflags;
        LBuf pProtect = LBuf_Adopt(atr_pget(i, A_PROTECTNAME, &aowner, &aflags));
        if ('\0' != pProtect[0])
        {
            UTF8 *bp = pProtect;
            UTF8 *token;
            while (nullptr != (token = split_token(&bp, sepPipe)))
            {
                if (0 == string_compare(token, name))
                {
                    return false;
                }
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// do_protect: @protect command - reserve player names.
//
void do_protect
(
    dbref executor,
    dbref caller,
    dbref enactor,
    int   eval,
    int   key,
    int   nargs,
    UTF8 *arg1,
    UTF8 *arg2,
    const UTF8 *cargs[],
    int   ncargs
)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nargs);
    UNUSED_PARAMETER(arg2);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (!isPlayer(executor))
    {
        notify(executor, T("Only players may use @protect."));
        return;
    }

    // Names are stored pipe-delimited so that names containing spaces
    // (allowed when player_name_spaces is on) are handled correctly.
    //
    SEP sepPipe = { 1, { '|' } };

    dbref aowner;
    int aflags;

    if (key & PROTECT_ALL)
    {
        if (!Wizard(executor))
        {
            notify(executor, NOPERM_MESSAGE);
            return;
        }

        bool found_any = false;
        dbref i;
        DO_WHOLE_DB(i)
        {
            if (!isPlayer(i))
            {
                continue;
            }

            LBuf pProtect = LBuf_Adopt(atr_pget(i, A_PROTECTNAME, &aowner, &aflags));
            if ('\0' != pProtect[0])
            {
                LBuf display = LBuf_Src("do_protect.all");
                UTF8 *dp = display.get();
                UTF8 *bp = pProtect;
                UTF8 *token;
                while (nullptr != (token = split_token(&bp, sepPipe)))
                {
                    if (dp != display)
                    {
                        safe_str(T(", "), display, &dp);
                    }
                    safe_str(token, display, &dp);
                }
                *dp = '\0';
                notify(executor, tprintf(T("%s: %s"), Name(i), display.get()));
                found_any = true;
            }
        }
        if (!found_any)
        {
            notify(executor, T("No protected names in the database."));
        }
        return;
    }

    if (key & PROTECT_LIST)
    {
        dbref target = executor;
        if (  nullptr != arg1
           && '\0' != arg1[0]
           && Wizard(executor))
        {
            target = lookup_player(executor, arg1, true);
            if (NOTHING == target)
            {
                notify(executor, T("No such player."));
                return;
            }
        }

        LBuf pProtect = LBuf_Adopt(atr_pget(target, A_PROTECTNAME, &aowner, &aflags));
        if ('\0' == pProtect[0])
        {
            notify(executor, T("No protected names."));
        }
        else
        {
            // Display pipe-delimited list as comma-separated for readability.
            //
            LBuf display = LBuf_Src("do_protect.list");
            UTF8 *dp = display.get();
            UTF8 *bp = pProtect;
            UTF8 *token;
            while (nullptr != (token = split_token(&bp, sepPipe)))
            {
                if (dp != display)
                {
                    safe_str(T(", "), display, &dp);
                }
                safe_str(token, display, &dp);
            }
            *dp = '\0';
            notify(executor, tprintf(T("Protected names for %s: %s"), Name(target), display.get()));
        }
        return;
    }

    if (  nullptr == arg1
       || '\0' == arg1[0])
    {
        notify(executor, T("Protect what name?"));
        return;
    }

    if (key & PROTECT_ALIAS)
    {
        // Set a protected name as this player's alias.
        // The name must be in the player's protected list.
        //
        LBuf pProtect = LBuf_Adopt(atr_pget(executor, A_PROTECTNAME, &aowner, &aflags));
        bool found = false;
        if ('\0' != pProtect[0])
        {
            UTF8 *bp = pProtect;
            UTF8 *token;
            while (nullptr != (token = split_token(&bp, sepPipe)))
            {
                if (0 == string_compare(token, arg1))
                {
                    found = true;
                    break;
                }
            }
        }

        if (!found)
        {
            notify(executor, T("That name is not in your protected list."));
            return;
        }

        // Check the name isn't already in use by someone else.
        //
        bool bAlias = false;
        dbref nPlayer = lookup_player_name(arg1, bAlias);
        if (  NOTHING != nPlayer
           && (  nPlayer != executor
              || !bAlias))
        {
            notify(executor, T("That name is already in use."));
            return;
        }

        // Remove old alias if any, set new one.
        //
        {
            LBuf oldalias = LBuf_Adopt(atr_pget(executor, A_ALIAS, &aowner, &aflags));
            if ('\0' != oldalias[0])
            {
                delete_player_name(executor, oldalias, true);
            }
        }

        atr_add(executor, A_ALIAS, arg1, Owner(executor), aflags);
        if (add_player_name(executor, arg1, true))
        {
            notify(executor, tprintf(T("Alias set to \xE2\x80\x98%s\xE2\x80\x99."), arg1));
        }
        else
        {
            notify(executor, T("That name is already in use or is illegal, alias cleared."));
            atr_clr(executor, A_ALIAS);
        }
        return;
    }

    if (key & PROTECT_UNALIAS)
    {
        // Remove the player's alias, but only if it matches a protected name.
        //
        LBuf oldalias = LBuf_Adopt(atr_pget(executor, A_ALIAS, &aowner, &aflags));
        if ('\0' == oldalias[0])
        {
            notify(executor, T("You have no alias set."));
            return;
        }

        if (0 != string_compare(oldalias, arg1))
        {
            notify(executor, tprintf(T("Your alias is \xE2\x80\x98%s\xE2\x80\x99, not \xE2\x80\x98%s\xE2\x80\x99."),
                oldalias.get(), arg1));
            return;
        }

        delete_player_name(executor, oldalias, true);
        atr_clr(executor, A_ALIAS);
        notify(executor, tprintf(T("Alias \xE2\x80\x98%s\xE2\x80\x99 removed."), oldalias.get()));
        return;
    }

    if (key & PROTECT_DEL)
    {
        // Remove a protected name.
        //
        LBuf pProtect = LBuf_Adopt(atr_pget(executor, A_PROTECTNAME, &aowner, &aflags));
        if ('\0' == pProtect[0])
        {
            notify(executor, T("You have no protected names."));
            return;
        }

        LBuf newlist = LBuf_Src("do_protect.del");
        UTF8 *np = newlist.get();
        bool found = false;

        UTF8 *bp = pProtect;
        UTF8 *token;
        while (nullptr != (token = split_token(&bp, sepPipe)))
        {
            if (!found && 0 == string_compare(token, arg1))
            {
                found = true;
                continue;
            }
            if (np != newlist)
            {
                safe_chr('|', newlist, &np);
            }
            safe_str(token, newlist, &np);
        }
        *np = '\0';

        if (!found)
        {
            notify(executor, T("That name is not in your protected list."));
        }
        else
        {
            atr_add_raw(executor, A_PROTECTNAME, newlist);
            notify(executor, tprintf(T("Name \xE2\x80\x98%s\xE2\x80\x99 removed from protected list."), arg1));
        }
        return;
    }

    // Default: /add
    //
    if (!ValidatePlayerName(arg1))
    {
        notify(executor, T("That is not a valid player name."));
        return;
    }

    if (!badname_check(arg1))
    {
        notify(executor, T("That name is not allowed."));
        return;
    }

    // Check the per-player limit.
    //
    LBuf pProtect = LBuf_Adopt(atr_pget(executor, A_PROTECTNAME, &aowner, &aflags));
    int count = 0;
    if ('\0' != pProtect[0])
    {
        UTF8 *bp = pProtect;
        UTF8 *token;
        while (nullptr != (token = split_token(&bp, sepPipe)))
        {
            if (0 == string_compare(token, arg1))
            {
                notify(executor, T("That name is already in your protected list."));
                return;
            }
            count++;
        }
    }

    if (count >= mudconf.max_name_protect)
    {
        notify(executor, tprintf(T("You may only protect %d names."), mudconf.max_name_protect));
        return;
    }

    // Check if name is protected by someone else.
    //
    if (!protectname_check(arg1, executor))
    {
        notify(executor, T("That name is already protected by another player."));
        return;
    }

    // Add the name.
    //
    LBuf newlist = LBuf_Src("do_protect.add");
    UTF8 *np = newlist.get();
    if ('\0' != pProtect[0])
    {
        safe_str(pProtect, newlist, &np);
        safe_chr('|', newlist, &np);
    }
    safe_str(arg1, newlist, &np);
    *np = '\0';

    atr_add_raw(executor, A_PROTECTNAME, newlist);
    notify(executor, tprintf(T("Name \xE2\x80\x98%s\xE2\x80\x99 added to protected list."), arg1));
}
