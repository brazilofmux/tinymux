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
    fprintf(stderr, "  -v <ver>       Output version\n");
    fprintf(stderr, "  -c <charset>   Input charset\n");
    fprintf(stderr, "  -d <charset>   Output charset\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "  -1             Reset #1 password to 'potrzebie'\n");
    fprintf(stderr, "  -x <dbref>     Extract <dbref> in @decomp format\n");
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
    eSame,
    eLatest,
    eLegacyOne,
    eLegacyTwo,
    eLegacyThree,
} ServerVersion;

typedef enum
{
    eLatin1,
    eWindows1252,
    eCodePage437,
    eCharsetUnknown,
} Charset;

int main(int argc, char *argv[])
{
    FILE *fpin = NULL;
    FILE *fpout = NULL;

    bool fResetPassword = false;
    bool fExtract = false;
    int  dbExtract;
    ServerType eInputType = eAuto;
    ServerType eOutputType = eServerUnknown;
    ServerVersion eOutputVersion = eSame;
    Charset eInputCharset = eCharsetUnknown;
    Charset eOutputCharset = eCharsetUnknown;

    int ch;
    while ((ch = getopt(argc, argv, "1i:o:v:c:d:x:")) != -1)
    {
        switch (ch)
        {
        case '1':
            fResetPassword = true;
            break;

        case 'i':
            if (  strcasecmp(optarg, "p6h") == 0
               || strcasecmp(optarg, "pennmush") == 0)
            {
                eInputType = ePennMUSH;
            }
            else if (  strcasecmp(optarg, "t5x") == 0
                    || strcasecmp(optarg, "tinymux") == 0)
            {
                eInputType = eTinyMUX;
            }
            else if (  strcasecmp(optarg, "r7h") == 0
                    || strcasecmp(optarg, "rhostmush") == 0)
            {
                eInputType = eRhostMUSH;
            }
            else if (  strcasecmp(optarg, "t6h") == 0
                    || strcasecmp(optarg, "tinymush") == 0)
            {
                eInputType = eTinyMUSH;
            }
            else
            {
                fprintf(stderr, "Input type not recognized.  Expected pennmush (p6h), tinymux (t5x), rhostmush (r7h), or tinymush (t6h).\n");
                Usage();
                return 1;
            }
            break;

        case 'o':
            if (  strcasecmp(optarg, "p6h") == 0
               || strcasecmp(optarg, "pennmush") == 0)
            {
                eOutputType = ePennMUSH;
            }
            else if (  strcasecmp(optarg, "t5x") == 0
                    || strcasecmp(optarg, "tinymux") == 0)
            {
                eOutputType = eTinyMUX;
            }
            else if (  strcasecmp(optarg, "r7h") == 0
                    || strcasecmp(optarg, "rhostmush") == 0)
            {
                eOutputType = eRhostMUSH;
            }
            else if (  strcasecmp(optarg, "t6h") == 0
                    || strcasecmp(optarg, "tinymush") == 0)
            {
                eOutputType = eTinyMUSH;
            }
            else
            {
                fprintf(stderr, "Output type not recognized.  Expected pennmush (p6h), tinymux (t5x), rhostmush (r7h), or tinymush (t6h).\n");
                Usage();
                return 1;
            }
            break;

        case 'v':
            if (strcasecmp(optarg, "latest") == 0)
            {
                eOutputVersion = eLatest;
            }
            else if (strcasecmp(optarg, "legacy") == 0)
            {
                eOutputVersion = eLegacyOne;
            }
            else if (strcasecmp(optarg, "legacyalt") == 0)
            {
                eOutputVersion = eLegacyTwo;
            }
            else if (strcasecmp(optarg, "legacyaltalt") == 0)
            {
                eOutputVersion = eLegacyThree;
            }
            else
            {
                fprintf(stderr, "Output version not recognized.\n");
                if (  eServerUnknown == eOutputType
                   || ePennMUSH == eOutputType)
                {
                    fprintf(stderr, "PennMUSH 'latest' is everything since 1.7.7p40.\n");
                    fprintf(stderr, "PennMUSH 'legacy' is everything between 1.7.5p0 and 1.7.7p40.\n");
                }
                if (  eServerUnknown == eOutputType
                   || eTinyMUX == eOutputType)
                {
                    fprintf(stderr, "TinyMUX 'latest' is version 4 (produced since 2.10).\n");
                    fprintf(stderr, "TinyMUX 'legacy' is version 3 (produced since 2.7).\n");
                    fprintf(stderr, "TinyMUX 'legacyalt' is version 2 (produced by 2.6).\n");
                    fprintf(stderr, "TinyMUX 'legacyaltalt' is version 1 (produced by 1.x through 2.4).\n");
                }
                if (  eServerUnknown == eOutputType
                   || eRhostMUSH == eOutputType)
                {
                    fprintf(stderr, "RhostMUSH 'latest' is the only known version.\n");
                }
                if (  eServerUnknown == eOutputType
                   || eTinyMUSH == eOutputType)
                {
                    fprintf(stderr, "TinyMUSH 'latest' is produced by 3.2.\n");
                    fprintf(stderr, "TinyMUSH 'legacy' is produced by 3.1p5 or 3.1p6.\n");
                    fprintf(stderr, "TinyMUSH 'legacyalt' is produced by 3.1p0 to 3.1p4.\n");
                    fprintf(stderr, "TinyMUSH 'legacyaltalt' is produced by 3.0.\n");
                }
                Usage();
                return 1;
            }
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

    if (eServerUnknown == eOutputType)
    {
        eOutputType = eInputType;
    }

    if (  ePennMUSH == eOutputType
       && (  eOutputVersion == eLegacyTwo
          || eOutputVersion == eLegacyOne))
    {
        fprintf(stderr, "PennMUSH has only one legacy flatfile formats:\n");
        fprintf(stderr, "PennMUSH 'latest' is everything since 1.7.7p40.\n");
        fprintf(stderr, "PennMUSH 'legacy' is everything between 1.7.5p0 and 1.7.7p40.\n");
        Usage();
        return 1;
    }

    if (  eRhostMUSH == eOutputType
       && eOutputVersion != eLatest)
    {
        fprintf(stderr, "There is only one known RhostMUSH flatfile version.\n");
        Usage();
        return 1;
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
            // It's easier to convert from 2.6 to Penn.
            //
            if (  g_t5xgame.Downgrade2()
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
            if (  g_t5xgame.Downgrade2()
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

    // Upgrades and downgrades.
    //
    if (ePennMUSH == eOutputType)
    {
        bool fLabels = g_p6hgame.HasLabels();
        if (fLabels)
        {
            switch (eOutputVersion)
            {
            case eSame:
            case eLatest:
                break;

            case eLegacyOne:
                fprintf(stderr, "Downgrading from PennMUSH latest to PennMUSH legacy is not not currently supported.\n");
                return 1;
                break;

            case eLegacyTwo:
                fprintf(stderr, "Downgrading from PennMUSH latest to PennMUSH legacy is not not currently supported.\n");
                return 1;
                break;
            }
        }
        else
        {
            switch (eOutputVersion)
            {
            case eLatest:
                g_p6hgame.Upgrade();
                g_p6hgame.Validate();
                break;

            case eSame:
            case eLegacyOne:
                break;

            case eLegacyTwo:
                fprintf(stderr, "Downgrading from PennMUSH latest to PennMUSH legacy is not not currently supported.\n");
                return 1;
                break;
            }
        }
    }
    else if (eTinyMUX == eOutputType)
    {
        int ver = (g_t5xgame.m_flags & T5X_V_MASK);
        switch (ver)
        {
        case 4:
            switch (eOutputVersion)
            {
            case eLatest:
            case eSame:
                break;

            case eLegacyOne:
                g_t5xgame.Downgrade3();
                g_t5xgame.Pass2();
                g_t5xgame.Validate();
                break;

            case eLegacyTwo:
                g_t5xgame.Downgrade2();
                g_t5xgame.Pass2();
                g_t5xgame.Validate();
                break;

            case eLegacyThree:
                g_t5xgame.Downgrade1();
                g_t5xgame.Pass2();
                g_t5xgame.Validate();
                break;
            }
            break;

        case 3:
            switch (eOutputVersion)
            {
            case eLatest:
                g_t5xgame.Upgrade4();
                g_t5xgame.Pass2();
                g_t5xgame.Validate();
                break;

            case eSame:
            case eLegacyOne:
                break;

            case eLegacyTwo:
                g_t5xgame.Downgrade2();
                g_t5xgame.Pass2();
                g_t5xgame.Validate();
                break;

            case eLegacyThree:
                g_t5xgame.Downgrade1();
                g_t5xgame.Pass2();
                g_t5xgame.Validate();
                break;
            }
            break;

        case 2:
            switch (eOutputVersion)
            {
            case eLatest:
                g_t5xgame.Upgrade4();
                g_t5xgame.Pass2();
                g_t5xgame.Validate();
                break;

            case eLegacyOne:
                g_t5xgame.Upgrade3();
                g_t5xgame.Pass2();
                g_t5xgame.Validate();
                break;

            case eSame:
            case eLegacyTwo:
                break;

            case eLegacyThree:
                g_t5xgame.Downgrade1();
                g_t5xgame.Pass2();
                g_t5xgame.Validate();
                break;
            }
            break;

        case 1:
            switch (eOutputVersion)
            {
            case eLatest:
                g_t5xgame.Upgrade4();
                g_t5xgame.Pass2();
                g_t5xgame.Validate();
                break;

            case eLegacyOne:
                g_t5xgame.Upgrade3();
                g_t5xgame.Pass2();
                g_t5xgame.Validate();
                break;

            case eLegacyTwo:
                g_t5xgame.Upgrade2();
                g_t5xgame.Pass2();
                g_t5xgame.Validate();
                break;

            case eSame:
            case eLegacyThree:
                break;
            }
            break;
        }
    }
    else if (eTinyMUSH == eOutputType)
    {
        switch (eOutputVersion)
        {
        case eSame:
            break;

        case eLatest:
            g_t6hgame.Upgrade32();
            g_t6hgame.Pass2();
            g_t6hgame.Validate();
            break;

        case eLegacyOne:
            g_t6hgame.Upgrade31b();
            g_t6hgame.Pass2();
            g_t6hgame.Validate();
            break;

        case eLegacyTwo:
            g_t6hgame.Upgrade31a();
            g_t6hgame.Pass2();
            g_t6hgame.Validate();
            break;

        case eLegacyThree:
            g_t6hgame.Downgrade();
            g_t6hgame.Pass2();
            g_t6hgame.Validate();
            break;
        }
    }

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
