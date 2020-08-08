/*! \file file_c.cpp
 * \brief File cache management.
 *
 * These functions load, cache, and display text files for the
 * connect screen, motd, and similar uses.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "command.h"
#include "file_c.h"
#include "mathutil.h"

typedef struct filecache_block_hdr FBLKHDR;
typedef struct filecache_block FBLOCK;

struct filecache_block
{
    struct filecache_block_hdr
    {
        struct filecache_block *nxt;
        unsigned int nchars;
    } hdr;
    UTF8 data[MBUF_SIZE - sizeof(struct filecache_block_hdr)];
};

struct filecache_hdr
{
    UTF8 **ppFilename;
    FBLOCK *fileblock;
    const UTF8 *desc;
};

typedef struct filecache_hdr FCACHE;

#define FBLOCK_SIZE (MBUF_SIZE - sizeof(FBLKHDR))

static FCACHE fcache[] =
{
    { &mudconf.conn_file,    nullptr,   T("Conn") },
    { &mudconf.site_file,    nullptr,   T("Conn/Badsite") },
    { &mudconf.down_file,    nullptr,   T("Conn/Down") },
    { &mudconf.full_file,    nullptr,   T("Conn/Full") },
    { &mudconf.guest_file,   nullptr,   T("Conn/Guest") },
    { &mudconf.creg_file,    nullptr,   T("Conn/Reg") },
    { &mudconf.crea_file,    nullptr,   T("Crea/Newuser") },
    { &mudconf.regf_file,    nullptr,   T("Crea/RegFaill") },
    { &mudconf.motd_file,    nullptr,   T("Motd") },
    { &mudconf.wizmotd_file, nullptr,   T("Wizmotd") },
    { &mudconf.quit_file,    nullptr,   T("Quit") },
    { nullptr,               nullptr,   (UTF8 *)nullptr }
};

static NAMETAB list_files[] =
{
    {T("badsite_connect"),  1,  CA_WIZARD,  FC_CONN_SITE},
    {T("connect"),          2,  CA_WIZARD,  FC_CONN},
    {T("create_register"),  2,  CA_WIZARD,  FC_CREA_REG},
    {T("down"),             1,  CA_WIZARD,  FC_CONN_DOWN},
    {T("full"),             1,  CA_WIZARD,  FC_CONN_FULL},
    {T("guest_motd"),       1,  CA_WIZARD,  FC_CONN_GUEST},
    {T("motd"),             1,  CA_WIZARD,  FC_MOTD},
    {T("newuser"),          1,  CA_WIZARD,  FC_CREA_NEW},
    {T("quit"),             1,  CA_WIZARD,  FC_QUIT},
    {T("register_connect"), 1,  CA_WIZARD,  FC_CONN_REG},
    {T("wizard_motd"),      1,  CA_WIZARD,  FC_WIZMOTD},
    {(UTF8 *) nullptr,      0,  0,          0}
};

void do_list_file(dbref executor, dbref caller, dbref enactor, int eval, int key, UTF8 *arg, const UTF8 *cargs[], int ncargs)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(key);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    int flagvalue;
    if (!search_nametab(executor, list_files, arg, &flagvalue))
    {
        display_nametab(executor, list_files, T("Unknown file.  Use one of"), true);
        return;
    }
    fcache_send(executor, flagvalue);
}

static FBLOCK *fcache_fill(FBLOCK *fp, UTF8 ch)
{
    if (fp->hdr.nchars >= sizeof(fp->data))
    {
        // We filled the current buffer.  Go get a new one.
        //
        FBLOCK *tfp = fp;
        fp = (FBLOCK *) alloc_mbuf("fcache_fill");
        fp->hdr.nxt = nullptr;
        fp->hdr.nchars = 0;
        tfp->hdr.nxt = fp;
    }
    fp->data[fp->hdr.nchars++] = ch;
    return fp;
}

static int fcache_read(FBLOCK **cp, UTF8 *filename)
{
    // Free a prior buffer chain.
    //
    FBLOCK *fp = *cp;
    while (fp != nullptr)
    {
        FBLOCK *tfp = fp->hdr.nxt;
        free_mbuf(fp);
        fp = tfp;
    }
    *cp = nullptr;

    // Read the text file into a new chain.
    //
    int   fd;
    UTF8 *buff;
    if (!mux_open(&fd, filename, O_RDONLY|O_BINARY))
    {
        // Failure: log the event
        //
        STARTLOG(LOG_PROBLEMS, "FIL", "OPEN");
        buff = alloc_mbuf("fcache_read.LOG");
        mux_sprintf(buff, MBUF_SIZE, T("Couldn\xE2\x80\x99t open file \xE2\x80\x98%s\xE2\x80\x99."), filename);
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
    fp->hdr.nxt = nullptr;
    fp->hdr.nchars = 0;
    *cp = fp;
    int tchars = 0;

    // Process the file, one lbuf at a time.
    //
    int nmax = mux_read(fd, buff, LBUF_SIZE);
    while (nmax > 0)
    {
        for (int n = 0; n < nmax; n++)
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
        nmax = mux_read(fd, buff, LBUF_SIZE);
    }
    free_lbuf(buff);
    if (mux_close(fd) == 0)
    {
        DebugTotalFiles--;
    }

    // If we didn't read anything in, toss the initial buffer.
    //
    if (fp->hdr.nchars == 0)
    {
        *cp = nullptr;
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
    UTF8 *start;

    while (fp != nullptr)
    {
        start = fp->data;
        remaining = fp->hdr.nchars;
        while (remaining > 0)
        {
            cnt = SOCKET_WRITE(fd, (char *)start, remaining, 0);
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

    while (fp != nullptr)
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
    UTF8 *buff, *bufc, *sbuf;

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
                safe_str(T("File sizes: "), buff, &bufc);
            }
            else
            {
                safe_str(T("  "), buff, &bufc);
            }
            safe_str(fp->desc, buff, &bufc);
            safe_str(T("..."), buff, &bufc);
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
        fp->fileblock = nullptr;
    }

    fcache_load(NOTHING);
}
