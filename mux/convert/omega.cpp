#include "omega.h"
#include "p6hgame.h"
#include "t5xgame.h"
#include "t6hgame.h"
#include "r7hgame.h"

// --------------------------------------------------------------------------
// StringCloneLen: allocate memory and copy string
//
char *StringCloneLen(const char *str, size_t nStr)
{
    char *buff = (char *)malloc(nStr+1);
    if (buff)
    {
        memcpy(buff, str, nStr);
        buff[nStr] = '\0';
    }
    else
    {
        exit(1);
    }
    return buff;
}

// --------------------------------------------------------------------------
// StringClone: allocate memory and copy string
//
char *StringClone(const char *str)
{
    return StringCloneLen(str, strlen(str));
}

bool ConvertTimeString(char *pTime, time_t *pt)
{
    char buffer[100];
    char *p = buffer;

    while (  '\0' != *pTime
          && '.'  != *pTime
          && p < buffer + sizeof(buffer) - 1)
    {
        *p++ = *pTime++;
    }

    while (  '\0' != *pTime
          && !isspace(*pTime))
    {
        pTime++;
    }

    if (  isspace(*pTime)
       && p < buffer + sizeof(buffer) - 1)
    {
       *p++ = *pTime++;
    }

    while (  '\0' != *pTime
          && p < buffer + sizeof(buffer) - 1)
    {
        *p++ = *pTime++;
    }
    *pTime = '\0';

    struct tm tm;
    if (strptime(buffer, "%a %b %d %H:%M:%S %Y", &tm) != NULL)
    {
        tm.tm_isdst = -1;
        time_t t = mktime(&tm);
        if (-1 != t)
        {
            *pt = t;
            return true;
        }
    }
    return false;
}

void Usage()
{
    fprintf(stderr, "Version: %s\n", OMEGA_VERSION);
    fprintf(stderr, "omega <options> <infile> [<outfile>]\n");
    fprintf(stderr, "Supported options:\n");
    fprintf(stderr, "  -i <type>      Input file type (p6h, r7h, t5x, t6h)\n");
    fprintf(stderr, "  -o <type>      Output file type (p6h, r7h, t5x, t6h)\n");
    fprintf(stderr, "  -v <ver>       Output version (id, alias, or latest/oldest/same;\n");
    fprintf(stderr, "                 optionally namespaced, e.g. t5x:3 or mux:latest)\n");
    fprintf(stderr, "  -c <charset>   Input charset\n");
    fprintf(stderr, "  -d <charset>   Output charset\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "  -1             Reset #1 password to 'potrzebie'\n");
    fprintf(stderr, "  -x <dbref>     Extract <dbref> in @decomp format\n");
    fprintf(stderr, "  -l, --list     List supported flatfile versions and exit\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "If no <outfile> is given, output is directed to standard out.\n");
}

typedef enum
{
    eAuto,
    ePennMUSH,
    eTinyMUX,
    eRhostMUSH,
    eTinyMUSH,
    eServerUnknown,
} ServerType;

typedef enum
{
    eLatin1,
    eWindows1252,
    eCodePage437,
    eCharsetUnknown,
} Charset;

// --------------------------------------------------------------------------
// Version registry.
//
// Each server family advertises an ordered list of flatfile versions it can
// produce.  A version is selected on the command line with '-v <id>', where
// <id> is one of:
//
//   latest / oldest / same     generic keywords
//   <token>                    a canonical id or alias from the tables below
//   <prefix>:<token>           namespaced form, e.g. 't5x:3' or 'mux:latest'
//
// The 'key' is a family-specific handle consumed by DetectKey()/MigrateOutput().
//
typedef struct
{
    const char *pId;        // canonical id (the part after the optional colon)
    const char *pAliases;   // space-separated alternates, or ""
    const char *pLabel;     // human-readable description
    int         key;        // family-specific version key
} VersionInfo;

typedef struct
{
    ServerType         server;
    const char        *pPrefix;     // canonical namespace, e.g. "t5x"
    const char        *pAltPrefix;  // space-separated friendly namespaces
    const VersionInfo *pVersions;
    int                nVersions;
    int                iOldest;     // index into pVersions of the oldest
    int                iLatest;     // index into pVersions of the newest
} ServerRegistry;

#define ARRAY_LEN(a) ((int)(sizeof(a)/sizeof((a)[0])))

static const VersionInfo g_p6hVersions[] =
{
    { "old", "legacy", "PennMUSH old-style flatfile (1.7.5p0-1.7.7p40)",      0 },
    { "new", "",       "PennMUSH new-style labelled flatfile (1.7.7p40+)",    1 },
};

static const VersionInfo g_t5xVersions[] =
{
    { "1", "1.6 2.0 2.4", "TinyMUX v1 - Latin-1, raw ANSI color (1.x-2.4)",   1 },
    { "2", "2.6",         "TinyMUX v2 - Latin-1, raw ANSI color (2.6)",       2 },
    { "3", "2.7 2.13",    "TinyMUX v3 - UTF-8, PUA color v1 encoding (2.7-2.13)", 3 },
    { "4", "",            "TinyMUX v4 - UTF-8, PUA color v1 encoding",        4 },
    { "5", "2.14",        "TinyMUX v5 - UTF-8, PUA color v2 encoding (2.14+)", 5 },
};

static const VersionInfo g_t6hVersions[] =
{
    { "3.0",   "",          "TinyMUSH 3.0",            0 },
    { "3.1p4", "3.1 3.1p0", "TinyMUSH 3.1p0 to 3.1p4", 1 },
    { "3.1p6", "3.1p5",     "TinyMUSH 3.1p5 or 3.1p6", 2 },
    { "3.2",   "",          "TinyMUSH 3.2 or later",   3 },
};

static const VersionInfo g_r7hVersions[] =
{
    { "7", "", "RhostMUSH v7", 7 },
};

static const ServerRegistry g_registries[] =
{
    { ePennMUSH,  "p6h", "pennmush",      g_p6hVersions, ARRAY_LEN(g_p6hVersions), 0, 1 },
    { eTinyMUX,   "t5x", "tinymux mux",   g_t5xVersions, ARRAY_LEN(g_t5xVersions), 0, 4 },
    { eTinyMUSH,  "t6h", "tinymush mush", g_t6hVersions, ARRAY_LEN(g_t6hVersions), 0, 3 },
    { eRhostMUSH, "r7h", "rhostmush rhost", g_r7hVersions, ARRAY_LEN(g_r7hVersions), 0, 0 },
};
#define NUM_REGISTRIES ARRAY_LEN(g_registries)

static const ServerRegistry *RegistryFor(ServerType t)
{
    for (int i = 0; i < NUM_REGISTRIES; i++)
    {
        if (g_registries[i].server == t)
        {
            return &g_registries[i];
        }
    }
    return NULL;
}

// Case-insensitive test for whether pTok appears in a space-separated list.
//
static bool TokenInList(const char *pList, const char *pTok)
{
    size_t n = strlen(pTok);
    const char *p = pList;
    while ('\0' != *p)
    {
        while (' ' == *p)
        {
            p++;
        }
        const char *q = p;
        while ('\0' != *q && ' ' != *q)
        {
            q++;
        }
        if (  (size_t)(q - p) == n
           && 0 == strncasecmp(p, pTok, n))
        {
            return true;
        }
        p = q;
    }
    return false;
}

// Map a server-type prefix ("t5x", "tinymux", "mux", ...) to a ServerType.
//
static ServerType PrefixToServer(const char *pPrefix)
{
    for (int i = 0; i < NUM_REGISTRIES; i++)
    {
        if (  0 == strcasecmp(pPrefix, g_registries[i].pPrefix)
           || TokenInList(g_registries[i].pAltPrefix, pPrefix))
        {
            return g_registries[i].server;
        }
    }
    return eServerUnknown;
}

// Return the live version key of the currently loaded/produced object.
//
static int DetectKey(ServerType t)
{
    switch (t)
    {
    case ePennMUSH:
        return g_p6hgame.HasLabels() ? 1 : 0;

    case eTinyMUX:
        return (g_t5xgame.m_flags & T5X_V_MASK);

    case eTinyMUSH:
        {
            const int Mask31p0 = T6H_V_TIMESTAMPS | T6H_V_VISUALATTRS;
            const int Mask32   = Mask31p0 | T6H_V_CREATETIME;
            if ((g_t6hgame.m_flags & Mask32) == Mask32)
            {
                return 3;
            }
            else if ((g_t6hgame.m_flags & Mask31p0) == Mask31p0)
            {
                return g_t6hgame.m_fExtraEscapes ? 2 : 1;
            }
            return 0;
        }

    case eRhostMUSH:
        return 7;

    default:
        return -1;
    }
}

// Human-readable label for a (server, key) pair.
//
static const char *DescribeVersion(ServerType t, int key)
{
    const ServerRegistry *pReg = RegistryFor(t);
    if (NULL != pReg)
    {
        for (int i = 0; i < pReg->nVersions; i++)
        {
            if (pReg->pVersions[i].key == key)
            {
                return pReg->pVersions[i].pLabel;
            }
        }
    }

    // A version the converter does not (yet) model -- name the family and the
    // raw key so the gap is obvious rather than silent.
    //
    static char aBuffer[64];
    snprintf(aBuffer, sizeof(aBuffer), "%s (unsupported flatfile version %d)",
             NULL != pReg ? pReg->pPrefix : "unknown", key);
    return aBuffer;
}

// Resolve a -v argument against the output family.  On success returns true
// and sets *pKey (the target version key) and *pSame (true for 'same').
//
static bool ResolveVersion(ServerType t, const char *pArg, int *pKey, bool *pSame)
{
    *pSame = false;
    *pKey  = -1;

    const ServerRegistry *pReg = RegistryFor(t);
    if (NULL == pReg)
    {
        fprintf(stderr, "No version table for the requested output type.\n");
        return false;
    }

    // Strip an optional '<prefix>:' namespace and verify it names this family.
    //
    const char *pId = pArg;
    const char *pColon = strchr(pArg, ':');
    if (NULL != pColon)
    {
        char prefix[32];
        size_t n = (size_t)(pColon - pArg);
        if (n >= sizeof(prefix))
        {
            n = sizeof(prefix) - 1;
        }
        memcpy(prefix, pArg, n);
        prefix[n] = '\0';
        if (PrefixToServer(prefix) != t)
        {
            fprintf(stderr, "Version namespace '%s' does not match the output type (%s).\n",
                    prefix, pReg->pPrefix);
            return false;
        }
        pId = pColon + 1;
    }

    if (0 == strcasecmp(pId, "same"))
    {
        *pSame = true;
        return true;
    }
    if (0 == strcasecmp(pId, "latest"))
    {
        *pKey = pReg->pVersions[pReg->iLatest].key;
        return true;
    }
    if (0 == strcasecmp(pId, "oldest"))
    {
        *pKey = pReg->pVersions[pReg->iOldest].key;
        return true;
    }
    for (int i = 0; i < pReg->nVersions; i++)
    {
        if (  0 == strcasecmp(pId, pReg->pVersions[i].pId)
           || TokenInList(pReg->pVersions[i].pAliases, pId))
        {
            *pKey = pReg->pVersions[i].key;
            return true;
        }
    }
    fprintf(stderr, "Output version '%s' not recognized for %s.\n", pId, pReg->pPrefix);
    fprintf(stderr, "Run 'omega --list' to see supported versions.\n");
    return false;
}

// Print the full table of supported families and versions.
//
static void ListVersions()
{
    fprintf(stderr, "Version: %s\n", OMEGA_VERSION);
    fprintf(stderr, "Supported flatfile versions (select with -v <id> or -v <prefix>:<id>):\n");
    for (int i = 0; i < NUM_REGISTRIES; i++)
    {
        const ServerRegistry *pReg = &g_registries[i];
        fprintf(stderr, "\n  %s (also: %s):\n", pReg->pPrefix, pReg->pAltPrefix);
        for (int j = 0; j < pReg->nVersions; j++)
        {
            const VersionInfo *pV = &pReg->pVersions[j];
            const char *pTag = "";
            if (j == pReg->iLatest && j == pReg->iOldest)
            {
                pTag = "  [latest,oldest]";
            }
            else if (j == pReg->iLatest)
            {
                pTag = "  [latest]";
            }
            else if (j == pReg->iOldest)
            {
                pTag = "  [oldest]";
            }
            fprintf(stderr, "    %-7s %s%s\n", pV->pId, pV->pLabel, pTag);
            if ('\0' != pV->pAliases[0])
            {
                fprintf(stderr, "            aliases: %s\n", pV->pAliases);
            }
        }
    }
    fprintf(stderr, "\nGeneric ids: latest, oldest, same.\n");
}

// Apply upgrades/downgrades so the output object reaches targetKey.
//
static bool MigrateOutput(ServerType t, int targetKey)
{
    switch (t)
    {
    case ePennMUSH:
        {
            bool fLabels = g_p6hgame.HasLabels();
            if (1 == targetKey)
            {
                // new-style
                //
                if (!fLabels)
                {
                    g_p6hgame.Upgrade();
                    g_p6hgame.Validate();
                }
            }
            else
            {
                // old-style
                //
                if (fLabels)
                {
                    fprintf(stderr, "Downgrading from PennMUSH new-style to old-style is not currently supported.\n");
                    return false;
                }
            }
        }
        break;

    case eTinyMUX:
        {
            int ver = (g_t5xgame.m_flags & T5X_V_MASK);
            if (targetKey != ver)
            {
                if (targetKey > ver)
                {
                    if      (5 == targetKey) g_t5xgame.Upgrade5();
                    else if (4 == targetKey) g_t5xgame.Upgrade4();
                    else if (3 == targetKey) g_t5xgame.Upgrade3();
                    else if (2 == targetKey) g_t5xgame.Upgrade2();
                }
                else
                {
                    if      (4 == targetKey) g_t5xgame.Downgrade4();
                    else if (3 == targetKey) g_t5xgame.Downgrade3();
                    else if (2 == targetKey) g_t5xgame.Downgrade2();
                    else if (1 == targetKey) g_t5xgame.Downgrade1();
                }
                g_t5xgame.Pass2();
                g_t5xgame.Validate();
            }
        }
        break;

    case eTinyMUSH:
        switch (targetKey)
        {
        case 3: g_t6hgame.Upgrade32();  break;
        case 2: g_t6hgame.Upgrade31b(); break;
        case 1: g_t6hgame.Upgrade31a(); break;
        case 0: g_t6hgame.Downgrade();  break;
        }
        g_t6hgame.Pass2();
        g_t6hgame.Validate();
        break;

    case eRhostMUSH:
        // Single known version; nothing to do.
        //
        break;

    default:
        break;
    }
    return true;
}

int main(int argc, char *argv[])
{
    FILE *fpin = NULL;
    FILE *fpout = NULL;

    bool fResetPassword = false;
    bool fExtract = false;
    int  dbExtract;
    ServerType eInputType = eAuto;
    ServerType eOutputType = eServerUnknown;
    char *pVersionArg = NULL;
    Charset eInputCharset = eCharsetUnknown;
    Charset eOutputCharset = eCharsetUnknown;

    // Handle long options that getopt() does not parse for us.
    //
    for (int i = 1; i < argc; i++)
    {
        if (0 == strcmp(argv[i], "--list"))
        {
            ListVersions();
            return 0;
        }
        if (  0 == strcmp(argv[i], "--help")
           || 0 == strcmp(argv[i], "-h"))
        {
            Usage();
            return 0;
        }
    }

    int ch;
    while ((ch = getopt(argc, argv, "1i:o:v:c:d:x:l")) != -1)
    {
        switch (ch)
        {
        case '1':
            fResetPassword = true;
            break;

        case 'l':
            ListVersions();
            return 0;

        case 'i':
            eInputType = PrefixToServer(optarg);
            if (eServerUnknown == eInputType)
            {
                fprintf(stderr, "Input type not recognized.  Expected pennmush (p6h), tinymux (t5x), rhostmush (r7h), or tinymush (t6h).\n");
                Usage();
                return 1;
            }
            break;

        case 'o':
            eOutputType = PrefixToServer(optarg);
            if (eServerUnknown == eOutputType)
            {
                fprintf(stderr, "Output type not recognized.  Expected pennmush (p6h), tinymux (t5x), rhostmush (r7h), or tinymush (t6h).\n");
                Usage();
                return 1;
            }
            break;

        case 'v':
            // Resolution is deferred until the output type is known.
            //
            pVersionArg = optarg;
            break;

         case 'c':
            if (strcasecmp(optarg, "latin1") == 0)
            {
                eInputCharset = eLatin1;
            }
            else if (strcasecmp(optarg, "cp437") == 0)
            {
                eInputCharset = eCodePage437;
            }
            else if (strcasecmp(optarg, "Windows1252") == 0)
            {
                eInputCharset = eWindows1252;
            }
            else
            {
                fprintf(stderr, "Input charset not recognized.  Expected latin1, cp437, or Windows1252.\n");
                Usage();
                return 1;
            }
            break;

         case 'd':
            if (strcasecmp(optarg, "latin1") == 0)
            {
                eOutputCharset = eLatin1;
            }
            else if (strcasecmp(optarg, "cp437") == 0)
            {
                eOutputCharset = eCodePage437;
            }
            else if (strcasecmp(optarg, "Windows1252") == 0)
            {
                eOutputCharset = eWindows1252;
            }
            else
            {
                fprintf(stderr, "Output charset not recognized.  Expected latin1, cp437, or Windows1252.\n");
                Usage();
                return 1;
            }
            break;

        case 'x':
            {
                fExtract = true;
                char *p = optarg;
                if ('#' == *p)
                {
                    p++;
                }
                dbExtract = atoi(p);
            }
            break;

        case '?':
        default:
            fprintf(stderr, "Command-line option not recognized.\n");
            Usage();
            return 1;
            break;
        }
    }
    argc -= optind;
    argv += optind;

    // We should have two remaining arguments (input and output file).
    //
    if (  1 != argc
       && 2 != argc)
    {
        fprintf(stderr, "After the options, there should be one or two command-line arguments left.\n");
        Usage();
        return 1;
    }

    fpin = fopen(argv[0], "rb");
    if (NULL == fpin)
    {
        fprintf(stderr, "Input file, %s, not found.\n", argv[0]);
        return 1;
    }
    if (1 == argc)
    {
        fpout = stdout;
    }
    else
    {
        fpout = fopen(argv[1], "wb");
        if (NULL == fpout)
        {
            fclose(fpin);
            fprintf(stderr, "Output file, %s, not found.\n", argv[1]);
            return 1;
        }
    }

    if (eAuto == eInputType)
    {
        // PennMUSH is +Vn where (n & 0xFF) == 2
        // TinyMUX is +Xn
        // TinyMUSH is +Tn
        // RhostMUSH is +Vn where (n & 0xFF) >= 7
        //
        char buffer[100];
        fgets(buffer, sizeof(buffer), fpin);
        fseek(fpin, 0, SEEK_SET);
        if ('+' == buffer[0])
        {
            if ('V' == buffer[1])
            {
                int n = 255 & atoi(buffer+2);
                if (2 == n)
                {
                    eInputType = ePennMUSH;
                }
                else if (7 <= n)
                {
                    eInputType = eRhostMUSH;
                }
            }
            else if ('T' == buffer[1])
            {
                eInputType = eTinyMUSH;
            }
            else if ('X' == buffer[1])
            {
                eInputType = eTinyMUX;
            }
        }
    }

    if (eAuto == eInputType)
    {
        fprintf(stderr, "Could not automatically determine flatfile type.\n");
        return 1;
    }

    // Determine the output type.  An explicit -o wins; otherwise a namespaced
    // -v (e.g. 'mux:latest') implies it; otherwise it mirrors the input.
    //
    if (eServerUnknown == eOutputType)
    {
        ServerType inferred = eServerUnknown;
        if (NULL != pVersionArg)
        {
            const char *pColon = strchr(pVersionArg, ':');
            if (NULL != pColon)
            {
                char prefix[32];
                size_t n = (size_t)(pColon - pVersionArg);
                if (n >= sizeof(prefix))
                {
                    n = sizeof(prefix) - 1;
                }
                memcpy(prefix, pVersionArg, n);
                prefix[n] = '\0';
                inferred = PrefixToServer(prefix);
            }
        }
        eOutputType = (eServerUnknown != inferred) ? inferred : eInputType;
    }

    // Resolve the requested output version against the output family.  With no
    // -v, the version is left unchanged ('same').
    //
    int  targetKey = -1;
    bool fSameVersion = true;
    if (NULL != pVersionArg)
    {
        if (!ResolveVersion(eOutputType, pVersionArg, &targetKey, &fSameVersion))
        {
            return 1;
        }
    }

    if (  (  eTinyMUX == eInputType
          && eCharsetUnknown != eInputCharset)
       || (  eTinyMUX == eOutputType
          && eCharsetUnknown != eOutputCharset))
    {
        fprintf(stderr, "It isn't necessary to specify a charset for TinyMUX.  TinyMUX 2.6 flatfiles\n"
                        "are always Windows-1252 (a superset of Latin-1 which in turn is a superset of\n"
                        "US-ASCII), and TinyMUX 2.7 flatfiles are always UTF-8.\n");
        Usage();
        return 1;
    }

    // Input handling. After this, we will have the specific flatfile version.
    //
    if (ePennMUSH == eInputType)
    {
        p6hin = fpin;
        p6hparse();
        p6hin = NULL;
        g_p6hgame.Validate();
    }
    else if (eTinyMUX == eInputType)
    {
        t5xin = fpin;
        t5xparse();
        t5xin = NULL;
        g_t5xgame.Pass2();
        g_t5xgame.Validate();

        // Normalize 24-bit color to the v5 (two-code-point) form used
        // internally.  A v3/v4 flatfile stores it in the older per-channel
        // delta form; migrate it up so every decode path sees one form.
        //
        int inVer = (g_t5xgame.m_flags & T5X_V_MASK);
        if (3 <= inVer && inVer <= 4)
        {
            g_t5xgame.MigrateColor(true);
            g_t5xgame.Pass2();
        }
    }
    else if (eTinyMUSH == eInputType)
    {
        t6hin = fpin;
        t6hparse();
        t6hin = NULL;
        g_t6hgame.Pass2();
        g_t6hgame.Validate();
    }
    else if (eRhostMUSH == eInputType)
    {
        r7hin = fpin;
        r7hparse();
        r7hin = NULL;
        g_r7hgame.Pass2();
        g_r7hgame.Validate();
    }
    else
    {
        fprintf(stderr, "Requested input type is not currently supported.\n");
        return 1;
    }

    fprintf(stderr, "Detected input: %s\n", DescribeVersion(eInputType, DetectKey(eInputType)));

    // Conversions
    //
    if (eInputType != eOutputType)
    {
        if (  ePennMUSH == eInputType
           && eTinyMUX == eOutputType)
        {
            g_t5xgame.ConvertFromP6H();
            g_t5xgame.Pass2();
            g_t5xgame.Validate();
        }
        else if (  eTinyMUX == eInputType
                && ePennMUSH == eOutputType)
        {
            // Downgrade to a Penn-flavored v2 t5x first: PUA color -> \x02c..\x03
            // markup (24-bit preserved), text -> Latin-1.
            //
            if (  g_t5xgame.DowngradeToPenn()
               || g_t5xgame.Upgrade2())
            {
                g_t5xgame.Pass2();
                g_t5xgame.Validate();
            }
            g_p6hgame.ConvertFromT5X();
            g_p6hgame.Validate();
        }
        else if (  ePennMUSH == eInputType
                && eTinyMUSH == eOutputType)
        {
            g_t6hgame.ConvertFromP6H();
            g_t6hgame.Pass2();
            g_t6hgame.Validate();
        }
        else if (  eTinyMUSH == eInputType
                && eTinyMUX  == eOutputType)
        {
            g_t5xgame.ConvertFromT6H();
            g_t5xgame.Pass2();
            g_t5xgame.Validate();
        }
        else if (  ePennMUSH == eInputType
                && eRhostMUSH == eOutputType)
        {
            g_r7hgame.ConvertFromP6H();
            g_r7hgame.Pass2();
            g_r7hgame.Validate();
        }
        else if (  eRhostMUSH == eInputType
                && eTinyMUX  == eOutputType)
        {
            g_t5xgame.ConvertFromR7H();
            g_t5xgame.Pass2();
            g_t5xgame.Validate();
        }
        else if (  eTinyMUX  == eInputType
                && eTinyMUSH == eOutputType)
        {
            // It's easier to convert from 2.6.
            //
            if (  g_t5xgame.DowngradeToT6H()
               || g_t5xgame.Upgrade2())
            {
                g_t5xgame.Pass2();
                g_t5xgame.Validate();
            }
            g_t6hgame.ConvertFromT5X();
            g_t6hgame.Pass2();
            g_t6hgame.Validate();
        }
        else if (  eTinyMUX  == eInputType
                && eRhostMUSH == eOutputType)
        {
            // Direct T5X -> R7H with full color fidelity.
            //
            if (  g_t5xgame.DowngradeToRhost()
               || g_t5xgame.Upgrade2())
            {
                g_t5xgame.Pass2();
                g_t5xgame.Validate();
            }
            g_r7hgame.ConvertFromT5X();
            g_r7hgame.Pass2();
            g_r7hgame.Validate();
        }
        else if (  eTinyMUSH == eInputType
                && ePennMUSH == eOutputType)
        {
            // T6H -> T5X -> P6H
            //
            g_t5xgame.ConvertFromT6H();
            g_t5xgame.Pass2();
            g_t5xgame.Validate();
            if (  g_t5xgame.DowngradeToPenn()
               || g_t5xgame.Upgrade2())
            {
                g_t5xgame.Pass2();
                g_t5xgame.Validate();
            }
            g_p6hgame.ConvertFromT5X();
            g_p6hgame.Validate();
        }
        else if (  eTinyMUSH == eInputType
                && eRhostMUSH == eOutputType)
        {
            // T6H -> T5X -> R7H (direct, with full color fidelity).
            //
            g_t5xgame.ConvertFromT6H();
            g_t5xgame.Pass2();
            g_t5xgame.Validate();
            if (  g_t5xgame.DowngradeToRhost()
               || g_t5xgame.Upgrade2())
            {
                g_t5xgame.Pass2();
                g_t5xgame.Validate();
            }
            g_r7hgame.ConvertFromT5X();
            g_r7hgame.Pass2();
            g_r7hgame.Validate();
        }
        else if (  eRhostMUSH == eInputType
                && ePennMUSH  == eOutputType)
        {
            // R7H -> T5X -> P6H
            //
            g_t5xgame.ConvertFromR7H();
            g_t5xgame.Pass2();
            g_t5xgame.Validate();
            if (  g_t5xgame.DowngradeToPenn()
               || g_t5xgame.Upgrade2())
            {
                g_t5xgame.Pass2();
                g_t5xgame.Validate();
            }
            g_p6hgame.ConvertFromT5X();
            g_p6hgame.Validate();
        }
        else if (  eRhostMUSH == eInputType
                && eTinyMUSH  == eOutputType)
        {
            // R7H -> T5X -> T6H
            //
            g_t5xgame.ConvertFromR7H();
            g_t5xgame.Pass2();
            g_t5xgame.Validate();
            if (  g_t5xgame.DowngradeToT6H()
               || g_t5xgame.Upgrade2())
            {
                g_t5xgame.Pass2();
                g_t5xgame.Validate();
            }
            g_t6hgame.ConvertFromT5X();
            g_t6hgame.Pass2();
            g_t6hgame.Validate();
        }
        else
        {
            fprintf(stderr, "Requested conversion is not currently supported.\n");
            return 1;
        }
    }

    // Apply the requested output version (skipped when 'same').
    //
    if (!fSameVersion)
    {
        if (!MigrateOutput(eOutputType, targetKey))
        {
            return 1;
        }
    }

    fprintf(stderr, "Producing output: %s\n", DescribeVersion(eOutputType, DetectKey(eOutputType)));

    // Optionally reset password.
    //
    if (fResetPassword)
    {
        if (ePennMUSH == eOutputType)
        {
            g_p6hgame.ResetPassword();
        }
        else if (eTinyMUX == eOutputType)
        {
            g_t5xgame.ResetPassword();
            g_t5xgame.Pass2();
        }
        else if (eTinyMUSH == eOutputType)
        {
            g_t6hgame.ResetPassword();
            g_t6hgame.Pass2();
        }
        else if (eRhostMUSH == eOutputType)
        {
            g_r7hgame.ResetPassword();
            g_r7hgame.Pass2();
        }
        else
        {
            fprintf(stderr, "Requested password reset is not currently supported.\n");
        }
    }

    // Color is held internally in v5 (two-code-point) form.  A v3/v4 flatfile
    // must store the older per-channel delta form, so migrate it down just
    // before writing.  (Extraction emits @decomp softcode, which wants the v5
    // internal form, so it is left alone.)
    //
    if (  !fExtract
       && eTinyMUX == eOutputType)
    {
        int outVer = (g_t5xgame.m_flags & T5X_V_MASK);
        if (3 <= outVer && outVer <= 4)
        {
            g_t5xgame.MigrateColor(false);
            g_t5xgame.Pass2();
        }
    }

    // Output stage.
    //
    if (fExtract)
    {
        if (ePennMUSH == eOutputType)
        {
            g_p6hgame.Extract(fpout, dbExtract);
        }
        else if (eTinyMUX == eOutputType)
        {
            g_t5xgame.Extract(fpout, dbExtract);
        }
        else if (eTinyMUSH == eOutputType)
        {
            g_t6hgame.Extract(fpout, dbExtract);
        }
        else if (eRhostMUSH == eOutputType)
        {
            g_r7hgame.Extract(fpout, dbExtract);
        }
    }
    else
    {
        if (ePennMUSH == eOutputType)
        {
            g_p6hgame.Write(fpout);
        }
        else if (eTinyMUX == eOutputType)
        {
            g_t5xgame.Write(fpout);
        }
        else if (eTinyMUSH == eOutputType)
        {
            g_t6hgame.Write(fpout);
        }
        else if (eRhostMUSH == eOutputType)
        {
            g_r7hgame.Write(fpout);
        }
    }

    if (2 == argc)
    {
        fclose(fpout);
    }
    fclose(fpin);
    return 0;
}
