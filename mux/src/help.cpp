/*! \file help.cpp
 * \brief In-game help system.
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include <fcntl.h>

#include "command.h"
#include "help.h"

// Pointers to this struct is what gets stored in the help_htab's.
//
struct help_entry
{
    size_t pos;       // Position in file.
    UTF8  *key;       // The key this is stored under. nullptr if this is an
                      // automatically generated initial substring alias.
};

void helpindex_clean(int iHelpfile)
{
    CHashTable *htab = mudstate.aHelpDesc[iHelpfile].ht;
    if (nullptr == htab)
    {
        return;
    }

    struct help_entry *htab_entry;
    for (htab_entry = (struct help_entry *)hash_firstentry(htab);
         htab_entry;
         htab_entry = (struct help_entry *)hash_nextentry(htab))
    {
        if (htab_entry->key)
        {
            MEMFREE(htab_entry->key);
            htab_entry->key = nullptr;
        }
        delete htab_entry;
        htab_entry = nullptr;
    }
    delete mudstate.aHelpDesc[iHelpfile].ht;
    mudstate.aHelpDesc[iHelpfile].ht = nullptr;
}

static int lineno;
static int ntopics;
static FILE *rfp;

static UTF8 Line[LBUF_SIZE];

static void HelpIndex_Start(FILE *fp)
{
    lineno = 0;
    ntopics = 0;
    rfp = fp;
}

static bool HelpIndex_Read(size_t *pPos, size_t *nTopic, UTF8 pTopic[TOPIC_NAME_LEN+1])
{
    size_t nLine = 0;

    while (  0 == nLine
          || '&' != Line[0])
    {
        if (fgets((char *)Line, LBUF_SIZE-2, rfp) == nullptr)
        {
            *pPos   = 0L;
            *nTopic = 0;
            return false;
        }
        ++lineno;

        nLine = strlen((char *)Line);
        *pPos += nLine;
        if (  0 < nLine
           && '\n' != Line[nLine - 1])
        {
            Log.tinyprintf(T("HelpIndex_Read, line %d: line too long" ENDLINE), lineno);
        }
    }

    ++ntopics;
    UTF8 *topic = Line + 1;
    while (  ' '  == *topic
          || '\t' == *topic
          || '\r' == *topic)
    {
        topic++;
    }

    UTF8   *s = topic;
    size_t  i = 0;
    while (  '\n' != *s
          && '\r' != *s
          && '\0' != *s
          && i < TOPIC_NAME_LEN)
    {
        if (  ' ' != *s
           || (  0 < i
              && ' ' != pTopic[i-1]))
        {
            pTopic[i++] = *s;
        }
        s++;
    }
    *nTopic = i;
    pTopic[i] = '\0';
    return true;
}

static void HelpIndex_End(void)
{
    lineno = 0;
    ntopics = 0;
    rfp = nullptr;
}

static void helpindex_read(int iHelpfile)
{
    helpindex_clean(iHelpfile);

    mudstate.aHelpDesc[iHelpfile].ht = new CHashTable;
    CHashTable *htab = mudstate.aHelpDesc[iHelpfile].ht;

    UTF8 szTextFilename[SBUF_SIZE+8];
    mux_sprintf(szTextFilename, sizeof(szTextFilename), T("%s.txt"),
        mudstate.aHelpDesc[iHelpfile].pBaseFilename);

    FILE *fp;
    if (!mux_fopen(&fp, szTextFilename, T("rb")))
    {
        STARTLOG(LOG_PROBLEMS, "HLP", "RINDX");
        UTF8 *p = alloc_lbuf("helpindex_read.LOG");
        mux_sprintf(p, LBUF_SIZE, T("Can\xE2\x80\x99t open %s for reading."), szTextFilename);
        log_text(p);
        free_lbuf(p);
        ENDLOG;
        return;
    }
    DebugTotalFiles++;

    size_t pos   = 0;
    size_t nTopicOriginal = 0;
    UTF8   topic[TOPIC_NAME_LEN+1];

    HelpIndex_Start(fp);
    while (HelpIndex_Read(&pos, &nTopicOriginal, topic))
    {
        // Convert the entry to all lowercase letters and add all leftmost
        // substrings.
        //
        // Topic names which appear earlier in the help file have priority
        // over topics names which appear later in the help file.  That is,
        // we do not associate prefixes with this topic if they have already
        // been used on a previous topic.
        //
        size_t nCased;
        UTF8  *pCased = mux_strlwr(topic, nCased);

        bool bOriginal = true; // First is the longest.

        for (size_t nTopic = nCased; 0 < nTopic; nTopic--)
        {
            // Avoid adding any entries with a trailing space.
            //
            if (mux_isspace(pCased[nTopic-1]))
            {
                continue;
            }

            struct help_entry *htab_entry =
              (struct help_entry *)hashfindLEN(pCased, nTopic, htab);

            if (htab_entry)
            {
                if (!bOriginal)
                {
                    continue;
                }

                hashdeleteLEN(pCased, nTopic, htab);

                if (htab_entry->key)
                {
                    MEMFREE(htab_entry->key);
                    htab_entry->key = nullptr;
                    Log.tinyprintf(T("helpindex_read: duplicate %s entries for %s" ENDLINE),
                        szTextFilename, pCased);
                }
                delete htab_entry;
                htab_entry = nullptr;
            }

            try
            {
                htab_entry = new struct help_entry;
            }
            catch (...)
            {
                ; // Nothing.
            }

            if (htab_entry)
            {
                htab_entry->pos = pos;
                htab_entry->key = bOriginal ? StringCloneLen(pCased, nTopic) : nullptr;
                bOriginal = false;

                hashaddLEN(pCased, nTopic, htab_entry, htab);
            }
        }
    }
    HelpIndex_End();

    if (fclose(fp) == 0)
    {
        DebugTotalFiles--;
    }
    hashreset(htab);
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
        notify(player, T("Cache for help indexes refreshed."));
    }
}

void helpindex_init(void)
{
    helpindex_load(NOTHING);
}

static const UTF8 *MakeCanonicalTopicName(UTF8 *topic_arg, size_t &nTopic)
{
    static UTF8 Buffer[LBUF_SIZE];

    const UTF8 *topic;
    if (topic_arg[0] == '\0')
    {
        topic = T("help");
        nTopic = 4;
    }
    else
    {
        topic = mux_strlwr(topic_arg, nTopic);
        memcpy(Buffer, topic, nTopic+1);
        topic = Buffer;
    }
    return topic;
}

static void ReportMatchedTopics(dbref executor, const UTF8 *topic, CHashTable *htab)
{
    bool matched = false;
    UTF8 *topic_list = nullptr;
    UTF8 *buffp = nullptr;
    struct help_entry *htab_entry;
    for (htab_entry = (struct help_entry *)hash_firstentry(htab);
         htab_entry != nullptr;
         htab_entry = (struct help_entry *)hash_nextentry(htab))
    {
        mudstate.wild_invk_ctr = 0;
        if (  htab_entry->key
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
        notify(executor, tprintf(T("No entry for \xE2\x80\x98%s\xE2\x80\x99."), topic));
    }
    else
    {
        notify(executor, tprintf(T("Here are the entries which match \xE2\x80\x98%s\xE2\x80\x99:"), topic));
        *buffp = '\0';
        notify(executor, topic_list);
        free_lbuf(topic_list);
    }
}

static bool ReportTopic(dbref executor, struct help_entry *htab_entry, int iHelpfile,
    UTF8 *result)
{
    UTF8 szTextFilename[SBUF_SIZE+8];
    mux_sprintf(szTextFilename, sizeof(szTextFilename), T("%s.txt"),
        mudstate.aHelpDesc[iHelpfile].pBaseFilename);

    size_t offset = htab_entry->pos;
    FILE *fp;
    if (!mux_fopen(&fp, szTextFilename, T("rb")))
    {
        STARTLOG(LOG_PROBLEMS, "HLP", "OPEN");
        UTF8 *line = alloc_lbuf("ReportTopic.open");
        mux_sprintf(line, LBUF_SIZE, T("Can\xE2\x80\x99t open %s for reading."), szTextFilename);
        log_text(line);
        free_lbuf(line);
        ENDLOG;
        return false;
    }
    DebugTotalFiles++;

    if (fseek(fp, static_cast<long>(offset), 0) < 0L)
    {
        STARTLOG(LOG_PROBLEMS, "HLP", "SEEK");
        UTF8 *line = alloc_lbuf("ReportTopic.seek");
        mux_sprintf(line, LBUF_SIZE, T("Seek error in file %s."), szTextFilename);
        log_text(line);
        free_lbuf(line);
        ENDLOG;
        if (fclose(fp) == 0)
        {
            DebugTotalFiles--;
        }
        return false;
    }
    UTF8 *line = alloc_lbuf("ReportTopic");
    UTF8 *bp = result;
    bool bInTopicAliases = true;
    for (;;)
    {
        if (  fgets((char *)line, LBUF_SIZE - 2, fp) == nullptr
           || '\0' == line[0])
        {
            break;
        }

        if ('&' == line[0])
        {
            if (bInTopicAliases)
            {
                continue;
            }
            else
            {
                break;
            }
        }
        bInTopicAliases = false;

        // Transform LF into CRLF to be telnet-friendly.
        //
        size_t len = strlen((char *)line);
        if (  0 < len
           && '\n' == line[len-1]
           && (  1 == len
              || '\r' != line[len-2]))
        {
            line[len-1] = '\r';
            line[len  ] = '\n';
            line[++len] = '\0';
        }

        bool bEval = mudstate.aHelpDesc[iHelpfile].bEval;
        if (bEval)
        {
            dbref executor_for_help = executor;
            if (Good_obj(mudconf.help_executor))
            {
                executor_for_help = mudconf.help_executor;
            }
            mux_exec(line, len, result, &bp, executor_for_help, executor, executor,
                    EV_NO_COMPRESS | EV_FIGNORE | EV_EVAL, nullptr, 0);
        }
        else
        {
            safe_str(line, result, &bp);
        }
    }

    // Zap trailing CRLF if present.
    //
    if (  result < bp - 1
       && '\r' == bp[-2]
       && '\n' == bp[-1])
    {
        bp -= 2;
    }
    *bp = '\0';

    if (fclose(fp) == 0)
    {
        DebugTotalFiles--;
    }
    free_lbuf(line);
    return true;
}

static void help_write(dbref executor, UTF8 *topic_arg, int iHelpfile)
{
    size_t nTopic;
    const UTF8 *topic = MakeCanonicalTopicName(topic_arg, nTopic);

    CHashTable *htab = mudstate.aHelpDesc[iHelpfile].ht;
    struct help_entry *htab_entry =
        (struct help_entry *)hashfindLEN(topic, nTopic, htab);

    if (htab_entry)
    {
        UTF8 *result = alloc_lbuf("help_write");
        if (ReportTopic(executor, htab_entry, iHelpfile, result))
        {
            notify(executor, result);
        }
        else
        {
            notify(executor, T("Sorry, that function is temporarily unavailable."));
        }
        free_lbuf(result);
    }
    else
    {
        ReportMatchedTopics(executor, topic, htab);
        return;
    }
}

static bool ValidateHelpFileIndex(int iHelpfile)
{
    if (  iHelpfile < 0
       || mudstate.mHelpDesc <= iHelpfile)
    {
        UTF8 *buf = alloc_mbuf("do_help.LOG");
        STARTLOG(LOG_BUGS, "BUG", "HELP");
        mux_sprintf(buf, MBUF_SIZE, T("Unknown help file number: %d"), iHelpfile);
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

void do_help(dbref executor, dbref caller, dbref enactor, int eval, int key, UTF8 *message, const UTF8 *cargs[], int ncargs)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    int iHelpfile = key;

    if (!ValidateHelpFileIndex(iHelpfile))
    {
        notify(executor, T("No such indexed file found."));
        return;
    }
    help_write(executor, message, iHelpfile);
}

void help_helper(dbref executor, int iHelpfile, UTF8 *topic_arg,
    UTF8 *buff, UTF8 **bufc)
{
    if (!ValidateHelpFileIndex(iHelpfile))
    {
        return;
    }

    size_t nTopic;
    const UTF8 *topic = MakeCanonicalTopicName(topic_arg, nTopic);

    CHashTable *htab = mudstate.aHelpDesc[iHelpfile].ht;
    struct help_entry *htab_entry =
        (struct help_entry *)hashfindLEN(topic, nTopic, htab);

    if (htab_entry)
    {
        UTF8 *result = alloc_lbuf("help_helper");
        if (ReportTopic(executor, htab_entry, iHelpfile, result))
        {
            safe_str(result, buff, bufc);
        }
        else
        {
            safe_str(T("#-1 ERROR"), buff, bufc);
        }
        free_lbuf(result);
    }
    else
    {
        safe_str(T("#-1 TOPIC DOES NOT EXIST"), buff, bufc);
    }
}
