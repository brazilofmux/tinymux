// help.cpp -- Commands for giving help.
//
// $Id: help.cpp,v 1.14 2003-01-05 22:18:02 sdennis Exp $
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

typedef struct
{
    const char *CommandName;
    CHashTable *ht;
    char       *pTextFile;
    char       *pIndexFile;
    BOOL       bEval;
    int        permissions;
} HELP_FILE_DESC;

#define HFTABLE_SIZE 6
HELP_FILE_DESC hftable[HFTABLE_SIZE] =
{
    { "help",    &mudstate.help_htab,      "text/help.txt",      "text/help.indx",      FALSE, CA_PUBLIC },
    { "news",    &mudstate.news_htab,      "text/news.txt",      "text/news.indx",       TRUE, CA_PUBLIC },
    { "wizhelp", &mudstate.wizhelp_htab,   "text/wizhelp.txt",   "text/wizhelp.indx",   FALSE, CA_WIZARD },
    { "+help",   &mudstate.plushelp_htab,  "text/plushelp.txt",  "text/plushelp.indx",   TRUE, CA_PUBLIC },
    { "wiznews", &mudstate.wiznews_htab,   "text/wiznews.txt",   "text/wiznews.indx",   FALSE, CA_WIZARD },
    { "+shelp",  &mudstate.staffhelp_htab, "text/staffhelp.txt", "text/staffhelp.indx",  TRUE, CA_STAFF  }
};

#if 0
    mudconf.whelp_file     = StringClone("text/wizhelp.txt");
    mudconf.whelp_indx     = StringClone("text/wizhelp.indx");
    mudconf.plushelp_file  = StringClone("text/plushelp.txt");
    mudconf.plushelp_indx  = StringClone("text/plushelp.indx");
    mudconf.staffhelp_file = StringClone("text/staffhelp.txt");
    mudconf.staffhelp_indx = StringClone("text/staffhelp.indx");
    mudconf.wiznews_file   = StringClone("text/wiznews.txt");
    mudconf.wiznews_indx   = StringClone("text/wiznews.indx");
    {"help_file",                 cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.help_file,       NULL, SIZEOF_PATHNAME},
    {"help_index",                cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.help_indx,       NULL, SIZEOF_PATHNAME},
    {"news_file",                 cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.news_file,       NULL, SIZEOF_PATHNAME},
    {"news_index",                cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.news_indx,       NULL, SIZEOF_PATHNAME},
    {"wizard_help_file",          cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.whelp_file,      NULL, SIZEOF_PATHNAME},
    {"wizard_help_index",         cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.whelp_indx,      NULL, SIZEOF_PATHNAME},
    {"plushelp_file",             cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.plushelp_file,   NULL, SIZEOF_PATHNAME},
    {"plushelp_index",            cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.plushelp_indx,   NULL, SIZEOF_PATHNAME},
    {"staffhelp_file",            cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.staffhelp_file,  NULL, SIZEOF_PATHNAME},
    {"staffhelp_index",           cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.staffhelp_indx,  NULL, SIZEOF_PATHNAME},
    {"wiznews_file",              cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.wiznews_file,    NULL, SIZEOF_PATHNAME},
    {"wiznews_index",             cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.wiznews_indx,    NULL, SIZEOF_PATHNAME},
#endif

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

int helpindex_read(int iHelpfile)
{
    CHashTable *htab = hftable[iHelpfile].ht;
    char *filename = hftable[iHelpfile].pIndexFile;

    help_indx entry;
    char *p;
    int count;

    // Let's clean out our hash table, before we throw it away.
    //
    helpindex_clean(htab);

    FILE *fp = fopen(filename, "rb");
    if (fp == NULL)
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
    for (int i = 0; i < HFTABLE_SIZE; i++)
    {
        CMDENT_ONE_ARG *cmdp = (CMDENT_ONE_ARG *)MEMALLOC(sizeof(CMDENT_ONE_ARG));

        cmdp->callseq = CS_ONE_ARG;
        cmdp->cmdname = StringClone(hftable[i].CommandName);
        cmdp->extra = i;
        cmdp->handler = do_help;
        cmdp->hookmask = 0;
        cmdp->perms = hftable[i].permissions;
        cmdp->switches = NULL;

        hashaddLEN(cmdp->cmdname, strlen(cmdp->cmdname),
            (int *)cmdp, &mudstate.command_htab);

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
    BOOL bEval = hftable[iHelpfile].bEval;
    CHashTable *htab = hftable[iHelpfile].ht;
    char *filename = hftable[iHelpfile].pTextFile;

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
    FILE *fp = fopen(filename, "rb");
    if (fp == NULL)
    {
        notify(player, "Sorry, that function is temporarily unavailable.");
        STARTLOG(LOG_PROBLEMS, "HLP", "OPEN");
        char *line = alloc_lbuf("help_write.LOG.open");
        sprintf(line, "Can't open %s for reading.", filename);
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
        sprintf(line, "Seek error in file %s.", filename);
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
       || HFTABLE_SIZE <= iHelpfile)
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
