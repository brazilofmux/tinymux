// help.cpp -- Commands for giving help.
//
// $Id: help.cpp,v 1.8 2002-08-22 01:00:27 sdennis Exp $
//

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include <fcntl.h>

#include "help.h"

/*
 * Pointers to this struct is what gets stored in the help_htab's
 */
struct help_entry
{
    int pos;        // Position, copied from help_indx
    char original;  // 1 for the longest name for a topic. 0 for
                    // abbreviations.
    char *key;      // The key this is stored under.
};

void helpindex_clean(CHashTable *htab)
{
    struct help_entry *htab_entry;
    for (htab_entry = (struct help_entry *)hash_firstentry(htab);
         htab_entry;
         htab_entry = (struct help_entry *)hash_nextentry(htab))
    {
        MEMFREE(htab_entry->key);
        htab_entry->key = NULL;
        MEMFREE(htab_entry);
        htab_entry = NULL;
    }

    hashflush(htab);
}

int helpindex_read(CHashTable *htab, char *filename)
{
    help_indx entry;
    char *p;
    int count;
    FILE *fp;

    // Let's clean out our hash table, before we throw it away.
    //
    helpindex_clean(htab);

    if ((fp = fopen(filename, "rb")) == NULL)
    {
        STARTLOG(LOG_PROBLEMS, "HLP", "RINDX")
        p = alloc_lbuf("helpindex_read.LOG");
        sprintf(p, "Can't open %s for reading.", filename);
        log_text(p);
        free_lbuf(p);
        ENDLOG
        return -1;
    }
    DebugTotalFiles++;
    count = 0;
    while ((fread((char *)&entry, sizeof(help_indx), 1, fp)) == 1)
    {
        // Convert the entry to all lowercase letters and add all leftmost substrings.
        //
        // Substrings already added will be rejected by hashaddLEN.
        //
        _strlwr(entry.topic);
        BOOL bOriginal = TRUE; // First is the longest.
        int nTopic = strlen(entry.topic);

        for (nTopic = strlen(entry.topic); nTopic > 0; nTopic--)
        {
            if (Tiny_IsSpace[(unsigned char)entry.topic[nTopic-1]])
            {
                continue;
            }
            struct help_entry *htab_entry = (struct help_entry *)MEMALLOC(sizeof(struct help_entry));
            (void)ISOUTOFMEMORY(htab_entry);
            htab_entry->pos = entry.pos;
            htab_entry->original = bOriginal;
            bOriginal = FALSE;
            htab_entry->key = StringCloneLen(entry.topic, nTopic);

            if ((hashaddLEN(entry.topic, nTopic, (int *)htab_entry, htab)) == 0)
            {
                count++;
            }
            else
            {
                MEMFREE(htab_entry->key);
                htab_entry->key = NULL;
                MEMFREE(htab_entry);
                htab_entry = NULL;
            }
        }
    }
    if (fclose(fp) == 0)
    {
        DebugTotalFiles--;
    }
    hashreset(htab);
    return count;
}

void helpindex_load(dbref player)
{
    int news, help, whelp;
    int phelp, wnhelp;
    int shelp;

    shelp= helpindex_read(&mudstate.staffhelp_htab, mudconf.staffhelp_indx);
    phelp = helpindex_read(&mudstate.plushelp_htab, mudconf.plushelp_indx);
    wnhelp = helpindex_read(&mudstate.wiznews_htab, mudconf.wiznews_indx);
    news = helpindex_read(&mudstate.news_htab, mudconf.news_indx);
    help = helpindex_read(&mudstate.help_htab, mudconf.help_indx);
    whelp = helpindex_read(&mudstate.wizhelp_htab, mudconf.whelp_indx);
    if ((player != NOTHING) && !Quiet(player))
        notify(player,
               tprintf("Index entries: News...%d  Help...%d  Wizhelp...%d  +Help...%d  Wiznews...%d",
                   news, help, whelp, phelp, wnhelp));
}

void helpindex_init(void)
{
    helpindex_load(NOTHING);
}

void help_write(dbref player, char *topic, CHashTable *htab, char *filename, BOOL eval)
{
    FILE *fp;
    char *p, *line, *bp, *str, *result;
    int offset;
    struct help_entry *htab_entry;
    BOOL matched;
    char *topic_list = 0, *buffp = 0;

    if (*topic == '\0')
    {
        topic = (char *)"help";
    }
    else
    {
        _strlwr(topic);
    }
    htab_entry = (struct help_entry *)hashfindLEN(topic, strlen(topic), htab);
    if (htab_entry)
    {
        offset = htab_entry->pos;
    }
    else
    {
        matched = FALSE;
        for (htab_entry = (struct help_entry *)hash_firstentry(htab);
             htab_entry != NULL;
             htab_entry = (struct help_entry *)hash_nextentry(htab))
        {
            mudstate.wild_invk_ctr = 0;
            if (  htab_entry->original
               && quick_wild(topic, htab_entry->key))
            {
                if (!matched)
                {
                    matched = TRUE;
                    topic_list = alloc_lbuf("help_write");
                    buffp = topic_list;
                }
                safe_str(htab_entry->key, topic_list, &buffp);
                safe_chr(' ', topic_list, &buffp);
                safe_chr(' ', topic_list, &buffp);
            }
        }
        if (!matched)
        {
            notify(player, tprintf("No entry for '%s'.", topic));
        }
        else
        {
            notify(player, tprintf("Here are the entries which match '%s':", topic));
            *buffp = '\0';
            notify(player, topic_list);
            free_lbuf(topic_list);
        }
        return;
    }
    if ((fp = fopen(filename, "rb")) == NULL)
    {
        notify(player, "Sorry, that function is temporarily unavailable.");
        STARTLOG(LOG_PROBLEMS, "HLP", "OPEN")
        line = alloc_lbuf("help_write.LOG.open");
        sprintf(line, "Can't open %s for reading.", filename);
        log_text(line);
        free_lbuf(line);
        ENDLOG
        return;
    }
    DebugTotalFiles++;
    if (fseek(fp, offset, 0) < 0L)
    {
        notify(player, "Sorry, that function is temporarily unavailable.");
        STARTLOG(LOG_PROBLEMS, "HLP", "SEEK")
        line = alloc_lbuf("help_write.LOG.seek");
        sprintf(line, "Seek error in file %s.", filename);
        log_text(line);
        free_lbuf(line);
        ENDLOG
        if (fclose(fp) == 0)
        {
            DebugTotalFiles--;
        }
        return;
    }
    line = alloc_lbuf("help_write");
    result = alloc_lbuf("help_write.2");
    for (;;)
    {
        if (  fgets(line, LBUF_SIZE - 1, fp) == NULL
           || line[0] == '&'
           || line[0] == '\0')
        {
            break;
        }
        if (  line[0] == '\n'
           || line[0] == '\r')
        {
            line[0] = ' ';
            line[1] = '\0';
        }
        else
        {
            for (p = line + 1; *p; p++)
            {
                if (*p == '\n' || *p == '\r')
                {
                    *p = '\0';
                    break;
                }
            }
        }
        if (eval)
        {
            str = line;
            bp = result;
            TinyExec(result, &bp, player, player, player,
                     EV_NO_COMPRESS | EV_FIGNORE | EV_EVAL, &str, (char **)NULL, 0);
            *bp = '\0';
            notify(player, result);
        }
        else
        {
            notify(player, line);
        }
    }
    if (fclose(fp) == 0)
    {
        DebugTotalFiles--;
    }
    free_lbuf(line);
    free_lbuf(result);
}

/*
 * ---------------------------------------------------------------------------
 * * do_help: display information from new-format news and help files
 */

void do_help(dbref executor, dbref caller, dbref enactor, int key, char *message)
{
    char *buf;

    switch (key)
    {
    case HELP_HELP:
        help_write(executor, message, &mudstate.help_htab, mudconf.help_file, FALSE);
        break;
    case HELP_NEWS:
        help_write(executor, message, &mudstate.news_htab, mudconf.news_file, TRUE);
        break;
    case HELP_WIZHELP:
        help_write(executor, message, &mudstate.wizhelp_htab, mudconf.whelp_file, FALSE);
        break;
    case HELP_PLUSHELP:
        help_write(executor, message, &mudstate.plushelp_htab, mudconf.plushelp_file, TRUE);
        break;
    case HELP_STAFFHELP:
        help_write(executor, message, &mudstate.staffhelp_htab, mudconf.staffhelp_file, TRUE);
        break;
    case HELP_WIZNEWS:
        help_write(executor, message, &mudstate.wiznews_htab, mudconf.wiznews_file, FALSE);
        break;
    default:
        STARTLOG(LOG_BUGS, "BUG", "HELP")
        buf = alloc_mbuf("do_help.LOG");
        sprintf(buf, "Unknown help file number: %d", key);
        log_text(buf);
        free_mbuf(buf);
        ENDLOG
    }
}
