// help.cpp -- Commands for giving help.
//
// $Id: help.cpp,v 1.14 2005-10-24 16:52:17 sdennis Exp $
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

bool bHaveTopic;
long pos;
int lineno;
int ntopics;
FILE *rfp;

#define LINE_SIZE 4096
char Line[LINE_SIZE + 1];
int  nLine;

void HelpIndex_Start(FILE *fp)
{
    pos = 0L;
    lineno = 0;
    ntopics = 0;
    rfp = fp;
    bHaveTopic = false;
    nLine = 0;
}

bool HelpIndex_Read(help_indx *pEntry)
{
    for (;;)
    {
        while (nLine == 0)
        {
            if (fgets(Line, LINE_SIZE, rfp) == NULL)
            {
                if (bHaveTopic)
                {
                    pEntry->len = (int)(pos - pEntry->pos);
                    bHaveTopic = false;
                    return true;
                }
                return false;
            }
            ++lineno;

            nLine = strlen(Line);
            if (  nLine > 0
               && Line[nLine - 1] != '\n')
            {
                Log.tinyprintf("HelpIndex_Read, line %d: line too long\n", lineno);
            }
        }

        if (Line[0] == '&')
        {
            if (bHaveTopic)
            {
                pEntry->len = (int)(pos - pEntry->pos);
                bHaveTopic = false;
                return true;
            }

            ++ntopics;
            char *topic = Line + 1;
            while (  *topic == ' '
                  || *topic == '\t'
                  || *topic == '\r')
            {
                topic++;
            }
            char *s;
            int i;
            memset(pEntry->topic, 0, sizeof(pEntry->topic));
            for (i = -1, s = topic; *s != '\n' && *s != '\r' && *s != '\0'; s++)
            {
                if (i >= TOPIC_NAME_LEN - 1)
                {
                    break;
                }
                if (*s != ' ' || pEntry->topic[i] != ' ')
                {
                    pEntry->topic[++i] = *s;
                }
            }
            pEntry->topic[++i] = '\0';
            pEntry->pos = pos + (long)nLine;
            bHaveTopic = true;
        }
        pos += nLine;
        nLine = 0;
    }
}

void HelpIndex_End(void)
{
    pos = 0L;
    lineno = 0;
    ntopics = 0;
    rfp = NULL;
}

int helpindex_read(int iHelpfile)
{
    helpindex_clean(iHelpfile);

    mudstate.aHelpDesc[iHelpfile].ht = new CHashTable;
    CHashTable *htab = mudstate.aHelpDesc[iHelpfile].ht;

    char szTextFilename[SBUF_SIZE+8];
    sprintf(szTextFilename, "%s.txt", mudstate.aHelpDesc[iHelpfile].pBaseFilename);

    help_indx entry;

    FILE *fp = fopen(szTextFilename, "rb");
    if (fp == NULL)
    {
        STARTLOG(LOG_PROBLEMS, "HLP", "RINDX");
        char *p = alloc_lbuf("helpindex_read.LOG");
        sprintf(p, "Can't open %s for reading.", szTextFilename);
        log_text(p);
        free_lbuf(p);
        ENDLOG;
        return -1;
    }
    DebugTotalFiles++;
    int count = 0;
    HelpIndex_Start(fp);
    while (HelpIndex_Read(&entry))
    {
        // Convert the entry to all lowercase letters and add all leftmost
        // substrings.
        //
        // Topic names which appear earlier in the help file have priority
        // over topics names which appear later in the help file.  That is,
        // we do not associate prefixes with this topic if they have already
        // been used on a previous topic.
        //
        mux_strlwr(entry.topic);
        bool bOriginal = true; // First is the longest.
        int nTopic = strlen(entry.topic);

        for (nTopic = strlen(entry.topic); nTopic > 0; nTopic--)
        {
            if (mux_isspace(entry.topic[nTopic-1]))
            {
                continue;
            }
            struct help_entry *htab_entry = (struct help_entry *)MEMALLOC(sizeof(struct help_entry));
            ISOUTOFMEMORY(htab_entry);
            htab_entry->pos = entry.pos;
            htab_entry->original = bOriginal;
            bOriginal = false;
            htab_entry->key = StringCloneLen(entry.topic, nTopic);

            if (!hashfindLEN(entry.topic, nTopic, htab))
            {
                hashaddLEN(entry.topic, nTopic, htab_entry, htab);
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
    HelpIndex_End();
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

const char *MakeCanonicalTopicName(char *topic_arg)
{
    const char *topic;
    if (topic_arg[0] == '\0')
    {
        topic = "help";
    }
    else
    {
        mux_strlwr(topic_arg);
        topic = topic_arg;
    }
    return topic;
}

void ReportMatchedTopics(dbref executor, const char *topic, CHashTable *htab)
{
    bool matched = false;
    char *topic_list = NULL;
    char *buffp = NULL;
    struct help_entry *htab_entry;
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
                matched = true;
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
        notify(executor, tprintf("No entry for '%s'.", topic));
    }
    else
    {
        notify(executor, tprintf("Here are the entries which match '%s':", topic));
        *buffp = '\0';
        notify(executor, topic_list);
        free_lbuf(topic_list);
    }
}

bool ReportTopic(dbref executor, struct help_entry *htab_entry, int iHelpfile,
    char *result)
{
    char szTextFilename[SBUF_SIZE+8];
    sprintf(szTextFilename, "%s.txt", mudstate.aHelpDesc[iHelpfile].pBaseFilename);

    int offset = htab_entry->pos;
    FILE *fp = fopen(szTextFilename, "rb");
    if (fp == NULL)
    {
        STARTLOG(LOG_PROBLEMS, "HLP", "OPEN");
        char *line = alloc_lbuf("ReportTopic.open");
        sprintf(line, "Can't open %s for reading.", szTextFilename);
        log_text(line);
        free_lbuf(line);
        ENDLOG;
        return false;
    }
    DebugTotalFiles++;
    if (fseek(fp, offset, 0) < 0L)
    {
        STARTLOG(LOG_PROBLEMS, "HLP", "SEEK");
        char *line = alloc_lbuf("ReportTopic.seek");
        sprintf(line, "Seek error in file %s.", szTextFilename);
        log_text(line);
        free_lbuf(line);
        ENDLOG;
        if (fclose(fp) == 0)
        {
            DebugTotalFiles--;
        }
        return false;
    }
    char *line = alloc_lbuf("ReportTopic");
    char *bp = result;
    for (;;)
    {
        if (  fgets(line, LBUF_SIZE - 1, fp) == NULL
           || line[0] == '&'
           || line[0] == '\0')
        {
            break;
        }

        bool bEval = mudstate.aHelpDesc[iHelpfile].bEval;
        if (bEval)
        {
            char *str = line;
            mux_exec(result, &bp, executor, executor, executor,
                     EV_NO_COMPRESS | EV_FIGNORE | EV_EVAL, &str, (char **)NULL, 0);
        }
        else
        {
            safe_str(line, result, &bp);
        }
    }
    *bp = '\0';
    if (fclose(fp) == 0)
    {
        DebugTotalFiles--;
    }
    free_lbuf(line);
    return true;
}

void help_write(dbref executor, char *topic_arg, int iHelpfile)
{
    const char *topic = MakeCanonicalTopicName(topic_arg);

    CHashTable *htab = mudstate.aHelpDesc[iHelpfile].ht;
    struct help_entry *htab_entry =
        (struct help_entry *)hashfindLEN(topic, strlen(topic), htab);
    if (htab_entry)
    {
        char *result = alloc_lbuf("help_write");
        if (ReportTopic(executor, htab_entry, iHelpfile, result))
        {
            notify(executor, result);
        }
        else
        {
            notify(executor, "Sorry, that function is temporarily unavailable.");
        }
        free_lbuf(result);
    }
    else
    {
        ReportMatchedTopics(executor, topic, htab);
        return;
    }
}

bool ValidateHelpFileIndex(int iHelpfile)
{
    if (  iHelpfile < 0
       || mudstate.mHelpDesc <= iHelpfile)
    {
        char *buf = alloc_mbuf("do_help.LOG");
        STARTLOG(LOG_BUGS, "BUG", "HELP");
        sprintf(buf, "Unknown help file number: %d", iHelpfile);
        log_text(buf);
        ENDLOG;
        free_mbuf(buf);
        return false;
    }
    return true;
}

/*
 * ---------------------------------------------------------------------------
 * * do_help: display information from new-format news and help files
 */

void do_help(dbref executor, dbref caller, dbref enactor, int key, char *message)
{
    int iHelpfile = key;

    if (!ValidateHelpFileIndex(iHelpfile))
    {
        notify(executor, "No such indexed file found.");
        return;
    }
    help_write(executor, message, iHelpfile);
}

void help_helper(dbref executor, int iHelpfile, char *topic_arg,
    char *buff, char **bufc)
{
    if (!ValidateHelpFileIndex(iHelpfile))
    {
        return;
    }

    const char *topic = MakeCanonicalTopicName(topic_arg);

    CHashTable *htab = mudstate.aHelpDesc[iHelpfile].ht;
    struct help_entry *htab_entry =
        (struct help_entry *)hashfindLEN(topic, strlen(topic), htab);
    if (htab_entry)
    {
        char *result = alloc_lbuf("help_helper");
        if (ReportTopic(executor, htab_entry, iHelpfile, result))
        {
            safe_str(result, buff, bufc);
        }
        else
        {
            safe_str("#-1 ERROR", buff, bufc);
        }
        free_lbuf(result);
    }
    else
    {
        safe_str("#-1 TOPIC DOES NOT EXIST", buff, bufc);
    }
}
