// help.cpp -- Commands for giving help.
//
// $Id: help.cpp,v 1.17 2003-01-12 18:18:15 sdennis Exp $
//

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include <fcntl.h>

#include "help.h"
#include "command.h"

// Pointers to this struct is what gets stored in the help_htab's.
//
struct help_entry
{
    int  pos;       // Position, copied from help_indx
    char original;  // 1 for the longest name for a topic. 0 for
                    // abbreviations.
    char *key;      // The key this is stored under.
};

void helpindex_clean(int iHelpfile)
{
    CHashTable *htab = mudstate.aHelpDesc[iHelpfile].ht;
    if (htab == NULL)
    {
        return;
    }
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
    delete mudstate.aHelpDesc[iHelpfile].ht;
    mudstate.aHelpDesc[iHelpfile].ht = NULL;
}

int helpindex_read(int iHelpfile)
{
    helpindex_clean(iHelpfile);

    mudstate.aHelpDesc[iHelpfile].ht = new CHashTable;
    CHashTable *htab = mudstate.aHelpDesc[iHelpfile].ht;

    char szIndexFilename[SBUF_SIZE+8];
    sprintf(szIndexFilename, "%s.indx", mudstate.aHelpDesc[iHelpfile].pBaseFilename);

    help_indx entry;

    FILE *fp = fopen(szIndexFilename, "rb");
    if (fp == NULL)
    {
        STARTLOG(LOG_PROBLEMS, "HLP", "RINDX");
        char *p = alloc_lbuf("helpindex_read.LOG");
        sprintf(p, "Can't open %s for reading.", szIndexFilename);
        log_text(p);
        free_lbuf(p);
        ENDLOG;
        return -1;
    }
    DebugTotalFiles++;
    int count = 0;
    while ((fread((char *)&entry, sizeof(help_indx), 1, fp)) == 1)
    {
        // Convert the entry to all lowercase letters and add all leftmost substrings.
        //
        // Substrings already added will be rejected by hashaddLEN.
        //
        mux_strlwr(entry.topic);
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
    for (int i = 0; i < mudstate.nHelpDesc; i++)
    {
        helpindex_read(i);
    }
    if (  player != NOTHING
       && !Quiet(player))
    {
        notify(player, "Cache for help indexes refreshed.");
    }
}

void helpindex_init(void)
{
    helpindex_load(NOTHING);
}

void help_write(dbref player, char *topic_arg, int iHelpfile)
{
    BOOL bEval = mudstate.aHelpDesc[iHelpfile].bEval;
    CHashTable *htab = mudstate.aHelpDesc[iHelpfile].ht;

    char szTextFilename[SBUF_SIZE+8];
    sprintf(szTextFilename, "%s.txt", mudstate.aHelpDesc[iHelpfile].pBaseFilename);

    mux_strlwr(topic_arg);
    const char *topic = topic_arg;

    if (topic[0] == '\0')
    {
        topic = "help";
    }
    struct help_entry *htab_entry =
        (struct help_entry *)hashfindLEN(topic, strlen(topic), htab);
    if (!htab_entry)
    {
        BOOL matched = FALSE;
        char *topic_list = NULL;
        char *buffp = NULL;
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

    int offset = htab_entry->pos;
    FILE *fp = fopen(szTextFilename, "rb");
    if (fp == NULL)
    {
        notify(player, "Sorry, that function is temporarily unavailable.");
        STARTLOG(LOG_PROBLEMS, "HLP", "OPEN");
        char *line = alloc_lbuf("help_write.LOG.open");
        sprintf(line, "Can't open %s for reading.", szTextFilename);
        log_text(line);
        free_lbuf(line);
        ENDLOG;
        return;
    }
    DebugTotalFiles++;
    if (fseek(fp, offset, 0) < 0L)
    {
        notify(player, "Sorry, that function is temporarily unavailable.");
        STARTLOG(LOG_PROBLEMS, "HLP", "SEEK");
        char *line = alloc_lbuf("help_write.LOG.seek");
        sprintf(line, "Seek error in file %s.", szTextFilename);
        log_text(line);
        free_lbuf(line);
        ENDLOG;
        if (fclose(fp) == 0)
        {
            DebugTotalFiles--;
        }
        return;
    }
    char *line = alloc_lbuf("help_write");
    char *result = alloc_lbuf("help_write.2");
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
            for (char *p = line + 1; *p; p++)
            {
                if (*p == '\n' || *p == '\r')
                {
                    *p = '\0';
                    break;
                }
            }
        }
        if (bEval)
        {
            char *str = line;
            char *bp = result;
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
    int iHelpfile = key;

    if (  iHelpfile < 0
       || mudstate.mHelpDesc <= iHelpfile)
    {
        char *buf = alloc_mbuf("do_help.LOG");
        STARTLOG(LOG_BUGS, "BUG", "HELP");
        sprintf(buf, "Unknown help file number: %d", iHelpfile);
        log_text(buf);
        ENDLOG;
        free_mbuf(buf);
        notify(executor, "No such indexed file found.");
        return;
    }

    help_write(executor, message, iHelpfile);
}
