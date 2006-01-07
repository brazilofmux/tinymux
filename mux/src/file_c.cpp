// file_c.cpp -- File cache management.
//
// $Id: file_c.cpp,v 1.7 2006-01-07 06:01:59 sdennis Exp $
//

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "command.h"
#include "file_c.h"

typedef struct filecache_block_hdr FBLKHDR;
typedef struct filecache_block FBLOCK;

struct filecache_block
{
    struct filecache_block_hdr
    {
        struct filecache_block *nxt;
        unsigned int nchars;
    } hdr;
    char data[MBUF_SIZE - sizeof(struct filecache_block_hdr)];
};

struct filecache_hdr
{
    char **ppFilename;
    FBLOCK *fileblock;
    const char *desc;
};

typedef struct filecache_hdr FCACHE;

#define FBLOCK_SIZE (MBUF_SIZE - sizeof(FBLKHDR))

FCACHE fcache[] =
{
    { &mudconf.conn_file,    NULL,   "Conn" },
    { &mudconf.site_file,    NULL,   "Conn/Badsite" },
    { &mudconf.down_file,    NULL,   "Conn/Down" },
    { &mudconf.full_file,    NULL,   "Conn/Full" },
    { &mudconf.guest_file,   NULL,   "Conn/Guest" },
    { &mudconf.creg_file,    NULL,   "Conn/Reg" },
    { &mudconf.crea_file,    NULL,   "Crea/Newuser" },
    { &mudconf.regf_file,    NULL,   "Crea/RegFaill" },
    { &mudconf.motd_file,    NULL,   "Motd" },
    { &mudconf.wizmotd_file, NULL,   "Wizmotd" },
    { &mudconf.quit_file,    NULL,   "Quit" },
    { NULL,                  NULL,   NULL }
};

NAMETAB list_files[] =
{
    {"badsite_connect",  1,  CA_WIZARD,  FC_CONN_SITE},
    {"connect",          2,  CA_WIZARD,  FC_CONN},
    {"create_register",  2,  CA_WIZARD,  FC_CREA_REG},
    {"down",             1,  CA_WIZARD,  FC_CONN_DOWN},
    {"full",             1,  CA_WIZARD,  FC_CONN_FULL},
    {"guest_motd",       1,  CA_WIZARD,  FC_CONN_GUEST},
    {"motd",             1,  CA_WIZARD,  FC_MOTD},
    {"newuser",          1,  CA_WIZARD,  FC_CREA_NEW},
    {"quit",             1,  CA_WIZARD,  FC_QUIT},
    {"register_connect", 1,  CA_WIZARD,  FC_CONN_REG},
    {"wizard_motd",      1,  CA_WIZARD,  FC_WIZMOTD},
    { NULL,              0,  0,          0}
};

void do_list_file(dbref executor, dbref caller, dbref enactor, int extra, char *arg)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(extra);

    int flagvalue;
    if (!search_nametab(executor, list_files, arg, &flagvalue))
    {
        display_nametab(executor, list_files, "Unknown file.  Use one of:", true);
        return;
    }
    fcache_send(executor, flagvalue);
}

static FBLOCK *fcache_fill(FBLOCK *fp, char ch)
{
    if (fp->hdr.nchars >= sizeof(fp->data))
    {
        // We filled the current buffer.  Go get a new one.
        //
        FBLOCK *tfp = fp;
        fp = (FBLOCK *) alloc_mbuf("fcache_fill");
        fp->hdr.nxt = NULL;
        fp->hdr.nchars = 0;
        tfp->hdr.nxt = fp;
    }
    fp->data[fp->hdr.nchars++] = ch;
    return fp;
}

static int fcache_read(FBLOCK **cp, char *filename)
{
    int n, nmax, tchars, fd;
    char *buff;
    FBLOCK *fp, *tfp;

    // Free a prior buffer chain.
    //
    fp = *cp;
    while (fp != NULL)
    {
        tfp = fp->hdr.nxt;
        free_mbuf(fp);
        fp = tfp;
    }
    *cp = NULL;

    // Read the text file into a new chain.
    //
    if ((fd = open(filename, O_RDONLY|O_BINARY)) == -1)
    {
        // Failure: log the event
        //
        STARTLOG(LOG_PROBLEMS, "FIL", "OPEN");
        buff = alloc_mbuf("fcache_read.LOG");
        sprintf(buff, "Couldn't open file '%s'.", filename);
        log_text(buff);
        free_mbuf(buff);
        ENDLOG
        return -1;
    }
    DebugTotalFiles++;
    buff = alloc_lbuf("fcache_read.temp");

    // Set up the initial cache buffer to make things easier.
    //
    fp = (FBLOCK *) alloc_mbuf("fcache_read.first");
    fp->hdr.nxt = NULL;
    fp->hdr.nchars = 0;
    *cp = fp;
    tchars = 0;

    // Process the file, one lbuf at a time.
    //
    nmax = read(fd, buff, LBUF_SIZE);
    while (nmax > 0)
    {
        for (n = 0; n < nmax; n++)
        {
            switch (buff[n])
            {
            case '\n':
                fp = fcache_fill(fp, '\r');
                fp = fcache_fill(fp, '\n');
                tchars += 2;
            case '\0':
            case '\r':
                break;
            default:
                fp = fcache_fill(fp, buff[n]);
                tchars++;
            }
        }
        nmax = read(fd, buff, LBUF_SIZE);
    }
    free_lbuf(buff);
    if (close(fd) == 0)
    {
        DebugTotalFiles--;
    }

    // If we didn't read anything in, toss the initial buffer.
    //
    if (fp->hdr.nchars == 0)
    {
        *cp = NULL;
        free_mbuf(fp);
    }
    return tchars;
}

void fcache_rawdump(SOCKET fd, int num)
{
    if ((num < 0) || (num > FC_LAST))
    {
        return;
    }

    FBLOCK *fp = fcache[num].fileblock;
    int cnt, remaining;
    char *start;

    while (fp != NULL)
    {
        start = fp->data;
        remaining = fp->hdr.nchars;
        while (remaining > 0)
        {
            cnt = SOCKET_WRITE(fd, start, remaining, 0);
            if (cnt < 0)
            {
                return;
            }
            remaining -= cnt;
            start += cnt;
        }
        fp = fp->hdr.nxt;
    }
    return;
}

void fcache_dump(DESC *d, int num)
{
    if ((num < 0) || (num > FC_LAST))
    {
        return;
    }
    FBLOCK *fp = fcache[num].fileblock;

    while (fp != NULL)
    {
        queue_write_LEN(d, fp->data, fp->hdr.nchars);
        fp = fp->hdr.nxt;
    }
}

void fcache_send(dbref player, int num)
{
    DESC *d;

    DESC_ITER_PLAYER(player, d)
    {
        fcache_dump(d, num);
    }
}

void fcache_load(dbref player)
{
    FCACHE *fp;
    char *buff, *bufc, *sbuf;

    buff = bufc = alloc_lbuf("fcache_load.lbuf");
    sbuf = alloc_sbuf("fcache_load.sbuf");
    for (fp = fcache; fp->ppFilename; fp++)
    {
        int i = fcache_read(&fp->fileblock, *fp->ppFilename);
        if (  player != NOTHING
           && !Quiet(player))
        {
            mux_ltoa(i, sbuf);
            if (fp == fcache)
            {
                safe_str("File sizes: ", buff, &bufc);
            }
            else
            {
                safe_str("  ", buff, &bufc);
            }
            safe_str(fp->desc, buff, &bufc);
            safe_str("...", buff, &bufc);
            safe_str(sbuf, buff, &bufc);
        }
    }
    *bufc = '\0';
    if (  player != NOTHING
       && !Quiet(player))
    {
        notify(player, buff);
    }
    free_lbuf(buff);
    free_sbuf(sbuf);
}

void fcache_init(void)
{
    FCACHE *fp = fcache;
    for (fp = fcache; fp->ppFilename; fp++)
    {
        fp->fileblock = NULL;
    }

    fcache_load(NOTHING);
}
