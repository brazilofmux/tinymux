#include "omega.h"
#include "p6hgame.h"
#include "t5xgame.h"

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

void Usage()
{
    fprintf(stderr, "Version: %s\n", OMEGA_VERSION);
    fprintf(stderr, "omega <options> <infile> <outfile>\n");
    fprintf(stderr, "Supported options:\n");
    fprintf(stderr, "  -i <type>      Input file type (p6h, r6h, t5x, t6h)\n");
    fprintf(stderr, "  -o <type>      Output file type (p6h, r6h, t5x, t6h)\n");
    fprintf(stderr, "  -v <ver>       Output version\n");
    fprintf(stderr, "  -c <charset>   Input charset\n");
    fprintf(stderr, "  -d <charset>   Output charset\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "  -1   Reset #1 password to 'potrzebie'\n");
}

typedef enum
{
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
    ServerType eInputType = eServerUnknown;
    ServerType eOutputType = eServerUnknown;
    ServerVersion eOutputVersion = eSame;
    Charset eInputCharset = eCharsetUnknown;
    Charset eOutputCharset = eCharsetUnknown;

    int ch;
    while ((ch = getopt(argc, argv, "1i:o:v:c:d:")) != -1)
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
            else if (  strcasecmp(optarg, "r6h") == 0
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
                fprintf(stderr, "Input type not recognized.  Expected pennmush (p6h), tinymux (t5x), rhostmush (r6h), or tinymush (t6h).\n");
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
            else if (  strcasecmp(optarg, "r6h") == 0
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
                fprintf(stderr, "Output type not recognized.  Expected pennmush (p6h), tinymux (t5x), rhostmush (r6h), or tinymush (t6h).\n");
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
                    fprintf(stderr, "TinyMUX 'latest' is version 3 (produced since 2.7).\n");
                    fprintf(stderr, "TinyMUX 'legacy' is version 2 (produced by 2.6).\n");
                    fprintf(stderr, "TinyMUX 'legacyalt' is version 1 (produced by 1.x through 2.4).\n");
                }
                if (  eServerUnknown == eOutputType
                   || eRhostMUSH == eOutputType)
                {
                    fprintf(stderr, "RhostMUSH 'latest' is the only known version.\n");
                }
                if (  eServerUnknown == eOutputType
                   || eTinyMUSH == eOutputType)
                {
                    fprintf(stderr, "TinyMUSH 'latest' is produced by 3.1.\n");
                    fprintf(stderr, "TinyMUSH 'legacy' is produced by 3.0.\n");
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

    if (  eServerUnknown == eInputType
       || eServerUnknown == eOutputType)
    {
        fprintf(stderr, "Server type not specified.\n");
        Usage();
        return 1;
    }

    // We should have two remaining arguments (input and output file).
    //
    if (2 != argc)
    {
        fprintf(stderr, "After options, there should be only two command-line arguments left.\n");
        Usage();
        return 1;
    }

    if (  eTinyMUSH == eInputType
       || eTinyMUSH == eOutputType)
    {
        fprintf(stderr, "TinyMUSH is not currently supported by this convertor. Use the tm3tomux converter under tinymux/convert/tinymush instead.\n");
        Usage();
        return 1;
    }

    if (  eRhostMUSH == eInputType
       || eRhostMUSH == eOutputType)
    {
        fprintf(stderr, "RhostMUSH is not currently supported by this convertor. Ashen-Shugar will need to provide some input.\n");
        Usage();
        return 1;
    }

    if (  eOutputVersion == eLegacyTwo
       && (  ePennMUSH == eOutputType
          || eRhostMUSH == eOutputType
          || eTinyMUSH == eOutputType))
    {
        fprintf(stderr, "PennMUSH, RhostMUSH, and TinyMUSH do not currently have support for flatfiles earlier than their chosen legacy version.\n");
        Usage();
        return 1;
    }

    if (  eOutputVersion == eLegacyOne
       && eRhostMUSH == eOutputType)
    {
        fprintf(stderr, "There is only one known RhostMUSH flatfie version.\n");
        Usage();
        return 1;
    }

    fpin = fopen(argv[0], "rb");
    if (NULL == fpin)
    {
        fprintf(stderr, "Input file, %s, not found.\n", argv[0]);
        return 1;
    }
    fpout = fopen(argv[1], "wb");
    if (NULL == fpout)
    {
        fclose(fpin);
        fprintf(stderr, "Output file, %s, not found.\n", argv[1]);
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
        g_t5xgame.Validate();
    }
    else
    {
        fprintf(stderr, "Requested input type is not currently supported.\n");
        return 1;
    }

    // Conversions, upgrades, and downgrades.
    //
    if (eInputType == eOutputType)
    {
        if (ePennMUSH == eInputType)
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
                    fprintf(stderr, "There is no parser for PennMUSH flaDowngrading from PennMUSH latest to PennMUSH legacy is not not currently supported.\n");
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
                    fprintf(stderr, "There is no parser for PennMUSH flaDowngrading from PennMUSH latest to PennMUSH legacy is not not currently supported.\n");
                    return 1;
                    break;
                }
            }
        }
        else if (eTinyMUX == eInputType)
        {
            int ver = (g_t5xgame.m_flags & V_MASK);
            switch (ver)
            {
            case 3:
                switch (eOutputVersion)
                {
                case eSame:
                case eLatest:
                    break;

                case eLegacyOne:
                    fprintf(stderr, "Omega does not support downgrading to TinyMUX 2.6 flatfile, but the TinyMUX 2.6 server will read TinyMUX 2.7 flatfiles.\n");
                    return 1;
                    break;
                   
                case eLegacyTwo:
                    fprintf(stderr, "Omega does not support downgrading to TinyMUX 1.x flatfiles, but TinyMUX 2.4 will read TinyMUX 2.6 flatfiles.\n");
                    return 1;
                    break;
                }
                break;

            case 2:
                switch (eOutputVersion)
                {
                case eLatest:
                    fprintf(stderr, "Omega does not support upgrading to 2.7 flatfiles, but TinyMUX 2.7 will read 2.6 flatfiles.\n");
                    return 1;
                    break;

                case eSame:
                case eLegacyOne:
                    break;
                   
                case eLegacyTwo:
                    fprintf(stderr, "Omega does not support downgrading to TinyMUX 1.x flatfiles, but TinyMUX 2.4 will read TinyMUX 2.6 flatfiles.\n");
                    return 1;
                    break;
                }
                break;

            case 1:
                switch (eOutputVersion)
                {
                case eLatest:
                    fprintf(stderr, "Omega does not support upgrading to 2.7 flatfiles, but TinyMUX 2.7 will read 2.6 flatfiles.\n");
                    return 1;
                    break;

                case eLegacyOne:
                    fprintf(stderr, "Omega does not support upgrading to 2.6 flatfiles, but TinyMUX 2.6 will read 1.x flatfiles.\n");
                    return 1;
                    break;
                   
                case eSame:
                case eLegacyTwo:
                    break;
                }
                break;
            }
        }
    }
    else
    {
        if (  ePennMUSH == eInputType
           && eTinyMUX == eOutputType)
        {
            g_t5xgame.ConvertFromP6H();
        }
        else
        {
            fprintf(stderr, "Requested conversion is not currently supported.\n");
            return 1;
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
        else
        {
            g_t5xgame.ResetPassword();
        }
    }

    // Output stage.
    //
    if (ePennMUSH == eOutputType)
    {
        g_p6hgame.Write(fpout);
    }
    else if (eTinyMUX == eOutputType)
    {
        g_t5xgame.Write(fpout);
    }

    fclose(fpout);
    fclose(fpin);
    return 0;
}
