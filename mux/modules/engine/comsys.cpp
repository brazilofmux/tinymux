/*! \file comsys.cpp
 * \brief Channel Communication System.
 *
 * The functions here manage channels, channel membership, the comsys.db, and
 * the interaction of players and other objects with channels.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include <algorithm>
#include <unordered_map>

using namespace std;

static unordered_map<dbref, comsys_t> comsys_table;

#define DFLT_MAX_LOG        0
#define MIN_RECALL_REQUEST  1
#define DFLT_RECALL_REQUEST 10
#define MAX_RECALL_REQUEST  200

// ---------------------------------------------------------------------------
// SQLite write-through helpers for comsys mutations.
// ---------------------------------------------------------------------------

#include "sqlite_backend.h"

#define SQLITE_COMSYS_WRITABLE() (!mudstate.bSQLiteLoading)

static void sqlite_wt_channel(struct channel *ch)
{
    if (!SQLITE_COMSYS_WRITABLE()) return;
    CSQLiteDB &sqldb = g_pSQLiteBackend->GetDB();
    if (!sqldb.SyncChannel(ch->name, ch->header,
        ch->type, ch->temp1, ch->temp2,
        ch->charge, ch->charge_who, ch->amount_col,
        ch->num_messages, ch->chan_obj))
    {
        Log.tinyprintf(T("comsys sqlite_wt_channel failed for %s" ENDLINE), ch->name);
    }
}

static void sqlite_wt_delete_channel(const UTF8 *name)
{
    if (!SQLITE_COMSYS_WRITABLE()) return;
    CSQLiteDB &sqldb = g_pSQLiteBackend->GetDB();
    if (!sqldb.DeleteChannel(name))
    {
        Log.tinyprintf(T("comsys sqlite_wt_delete_channel failed for %s" ENDLINE), name);
    }
}

static void sqlite_wt_channel_user(const UTF8 *channel_name, struct comuser *user)
{
    if (!SQLITE_COMSYS_WRITABLE()) return;
    CSQLiteDB &sqldb = g_pSQLiteBackend->GetDB();
    if (!sqldb.SyncChannelUser(channel_name, user->who,
        user->bUserIsOn, user->ComTitleStatus,
        user->bGagJoinLeave,
        reinterpret_cast<const UTF8 *>(user->title.c_str())))
    {
        Log.tinyprintf(T("comsys sqlite_wt_channel_user failed for %s/#%d" ENDLINE),
            channel_name, user->who);
    }
}

static void sqlite_wt_delete_channel_user(const UTF8 *channel_name, int who)
{
    if (!SQLITE_COMSYS_WRITABLE()) return;
    CSQLiteDB &sqldb = g_pSQLiteBackend->GetDB();
    if (!sqldb.DeleteChannelUser(channel_name, who))
    {
        Log.tinyprintf(T("comsys sqlite_wt_delete_channel_user failed for %s/#%d" ENDLINE),
            channel_name, who);
    }
}

static void sqlite_wt_player_channel(int who, const UTF8 *alias, const UTF8 *channel_name)
{
    if (!SQLITE_COMSYS_WRITABLE()) return;
    CSQLiteDB &sqldb = g_pSQLiteBackend->GetDB();
    if (!sqldb.SyncPlayerChannel(who, alias, channel_name))
    {
        Log.tinyprintf(T("comsys sqlite_wt_player_channel failed for #%d/%s" ENDLINE),
            who, alias);
    }
}

static void sqlite_wt_delete_player_channel(int who, const UTF8 *alias)
{
    if (!SQLITE_COMSYS_WRITABLE()) return;
    CSQLiteDB &sqldb = g_pSQLiteBackend->GetDB();
    if (!sqldb.DeletePlayerChannel(who, alias))
    {
        Log.tinyprintf(T("comsys sqlite_wt_delete_player_channel failed for #%d/%s" ENDLINE),
            who, alias);
    }
}

// Return value is a static buffer.
//
static UTF8* RestrictTitleValue(const UTF8* pTitleRequest)
{
    // Remove all '\r\n\t' from the string.
    // Terminate any ANSI in the string.
    //
    static UTF8 NewTitle[MAX_TITLE_LEN + 1];
    StripTabsAndTruncate(pTitleRequest, NewTitle, MAX_TITLE_LEN, MAX_TITLE_LEN);
    return NewTitle;
}

static void do_setcomtitlestatus(const dbref player, struct channel* ch, const bool status)
{
    struct comuser* user = select_user(ch, player);
    if (ch && user)
    {
        user->ComTitleStatus = status;
        sqlite_wt_channel_user(ch->name, user);
    }
}

static void do_setgagjoinleavestatus(const dbref player, struct channel* ch, const bool status)
{
    struct comuser* user = select_user(ch, player);
    if (ch && user)
    {
        user->bGagJoinLeave = status;
        sqlite_wt_channel_user(ch->name, user);
    }
}

static void do_setnewtitle(const dbref player, struct channel* ch, const UTF8* pValidatedTitle)
{
    struct comuser* user = select_user(ch, player);

    if (ch && user)
    {
        user->title.assign(reinterpret_cast<const char *>(pValidatedTitle));
        sqlite_wt_channel_user(ch->name, user);
    }
}

// Save communication system data to disk.
//
void save_comsys(UTF8* filename)
{
    UTF8 buffer[500];

    mux_sprintf(buffer, sizeof(buffer), T("%s.#"), filename);
    FILE* fp;
    if (!mux_fopen(&fp, buffer, T("wb")))
    {
        Log.tinyprintf(T("Unable to open %s for writing." ENDLINE), buffer);
        return;
    }
    mux_fprintf(fp, T("+V5\n"));
    mux_fprintf(fp, T("*** Begin CHANNELS ***\n"));

    save_channels(fp);

    mux_fprintf(fp, T("*** Begin COMSYS ***\n"));
    save_comsystem(fp);

    mux_fclose(fp);

    ReplaceFile(buffer, filename);
}

// Aliases must be between 1 and MAX_ALIAS_LEN characters. No spaces. No ANSI.
//
UTF8* MakeCanonicalComAlias
(
    const UTF8* pAlias,
    size_t* nValidAlias,
    bool* bValidAlias
)
{
    static UTF8 Buffer[MAX_ALIAS_LEN + 1];
    *nValidAlias = 0;
    *bValidAlias = false;

    if (!pAlias)
    {
        return nullptr;
    }
    size_t n = 0;
    while (pAlias[n])
    {
        if (!mux_isprint_ascii(pAlias[n])
            || ' ' == pAlias[n])
        {
            return nullptr;
        }
        n++;
    }

    if (n < 1)
    {
        return nullptr;
    }
    if (MAX_ALIAS_LEN < n)
    {
        n = MAX_ALIAS_LEN;
    }
    memcpy(Buffer, pAlias, n);
    Buffer[n] = '\0';
    *nValidAlias = n;
    *bValidAlias = true;
    return Buffer;
}

static bool ParseChannelLine(UTF8* pBuffer, UTF8* pAlias5, UTF8** ppChannelName)
{
    // Fetch alias portion. We need to find the first space.
    //
    auto p = reinterpret_cast<UTF8*>(strchr(reinterpret_cast<char*>(pBuffer), ' '));
    if (!p)
    {
        return false;
    }

    *p = '\0';
    bool bValidAlias;
    size_t nValidAlias;
    const UTF8* pValidAlias = MakeCanonicalComAlias(pBuffer, &nValidAlias, &bValidAlias);
    if (!bValidAlias)
    {
        return false;
    }
    mux_strncpy(pAlias5, pValidAlias, MAX_ALIAS_LEN);

    // Skip any leading space before the channel name.
    //
    p++;
    while (mux_isspace(*p))
    {
        p++;
    }

    if (*p == '\0')
    {
        return false;
    }

    // The rest of the line is the channel name.
    //
    *ppChannelName = StringClone(p);
    return true;
}

static bool ReadListOfNumbers(FILE* fp, const int cnt, int anum[])
{
    UTF8 buffer[200];
    if (fgets(reinterpret_cast<char*>(buffer), sizeof(buffer), fp))
    {
        UTF8* p = buffer;
        for (int i = 0; i < cnt; i++)
        {
            if (mux_isdigit(p[0])
                || ('-' == p[0]
                    && mux_isdigit(p[1])))
            {
                anum[i] = mux_atol(p);
                do
                {
                    p++;
                }
                while (mux_isdigit(*p));

                if (' ' == *p)
                {
                    p++;
                }
            }
            else
            {
                return false;
            }
        }

        if ('\n' == *p)
        {
            return true;
        }
    }
    return false;
}


// Perform cleanup of comsystem data for players with 0 channels or player
// objects that were destroyed.
//
void purge_comsystem(void)
{
#ifdef ABORT_PURGE_COMSYS
    return;
#endif // ABORT_PURGE_COMSYS

    for (auto it = comsys_table.begin(); it != comsys_table.end(); )
    {
        const comsys_t &c = it->second;
        if (c.aliases.empty())
        {
            it = comsys_table.erase(it);
            continue;
        }
        if (isPlayer(c.who))
        {
            ++it;
            continue;
        }
        if (God(Owner(c.who))
            && Going(c.who))
        {
            it = comsys_table.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

// Save Comsys channel data to the indicated file.
//
void save_channels(FILE* fp)
{
    purge_comsystem();

    int np = static_cast<int>(comsys_table.size());

    mux_fprintf(fp, T("%d\n"), np);
    for (auto &kv : comsys_table)
    {
        const comsys_t &c = kv.second;

        // Write user dbref and # of channels.
        //
        mux_fprintf(fp, T("%d %d\n"), c.who, static_cast<int>(c.aliases.size()));
        for (size_t j = 0; j < c.aliases.size(); j++)
        {
            // Write channel alias and channel name.
            //
            mux_fprintf(fp, T("%s %s\n"),
                reinterpret_cast<const UTF8 *>(c.aliases[j].alias.c_str()),
                reinterpret_cast<const UTF8 *>(c.aliases[j].channel.c_str()));
        }
    }
}

static comsys_t* get_comsys(const dbref which)
{
    if (which < 0)
    {
        return nullptr;
    }

    comsys_t &c = comsys_table[which];
    c.who = which;
    return &c;
}

void del_comsys(const dbref who)
{
    comsys_table.erase(who);
}

// Sort aliases.
//
void sort_com_aliases(comsys_t &c)
{
    std::sort(c.aliases.begin(), c.aliases.end(),
        [](const com_alias &a, const com_alias &b) { return a.alias < b.alias; });
}

// Lookup player's comsys data and find the channel associated with
// the given alias.
//
static const UTF8* get_channel_from_alias(dbref player, UTF8* alias)
{
    auto it = comsys_table.find(player);
    if (it == comsys_table.end())
    {
        return T("");
    }
    const comsys_t &c = it->second;
    for (const auto &ca : c.aliases)
    {
        if (ca.alias == reinterpret_cast<const char *>(alias))
        {
            return reinterpret_cast<const UTF8 *>(ca.channel.c_str());
        }
    }
    return T("");
}

// Version 5 start on 2026-MAR-04
//
//   -- Adds bGagJoinLeave as a 4th number on each user line.
//
void load_comsystem_V5(FILE* fp)
{
    LBuf temp = LBuf_Src("load_comsystem_V5");

    int nc = 0;
    if (nullptr == fgets(reinterpret_cast<char*>(temp.get()), LBUF_SIZE, fp))
    {
        return;
    }
    nc = mux_atol(temp);

    for (int i = 0; i < nc; i++)
    {
        int anum[10];

        auto ch = new channel();

        size_t nChannel = GetLineTrunc(temp, LBUF_SIZE, fp);
        if (nChannel > MAX_CHANNEL_LEN)
        {
            nChannel = MAX_CHANNEL_LEN;
        }
        if (temp[nChannel - 1] == '\n')
        {
            // Get rid of trailing '\n'.
            //
            nChannel--;
        }
        memcpy(ch->name, temp, nChannel);
        ch->name[nChannel] = '\0';

        size_t nHeader = GetLineTrunc(temp, LBUF_SIZE, fp);
        if (nHeader > MAX_HEADER_LEN)
        {
            nHeader = MAX_HEADER_LEN;
        }
        if (temp[nHeader - 1] == '\n')
        {
            nHeader--;
        }
        memcpy(ch->header, temp, nHeader);
        ch->header[nHeader] = '\0';

        vector<UTF8> channel_name_vector(ch->name, ch->name+nChannel);
        mudstate.channel_names.insert(make_pair(channel_name_vector, ch));

        ch->type = 127;
        ch->temp1 = 0;
        ch->temp2 = 0;
        ch->charge = 0;
        ch->charge_who = NOTHING;
        ch->amount_col = 0;
        ch->num_messages = 0;
        ch->chan_obj = NOTHING;

        mux_assert(ReadListOfNumbers(fp, 8, anum));
        ch->type = anum[0];
        ch->temp1 = anum[1];
        ch->temp2 = anum[2];
        ch->charge = anum[3];
        ch->charge_who = anum[4];
        ch->amount_col = anum[5];
        ch->num_messages = anum[6];
        ch->chan_obj = anum[7];

        int num_users_to_read = 0;
        mux_assert(ReadListOfNumbers(fp, 1, &num_users_to_read));
        if (num_users_to_read > 0)
        {
            for (int j = 0; j < num_users_to_read; j++)
            {
                comuser t_user;

                t_user.who = NOTHING;
                t_user.bUserIsOn = false;
                t_user.ComTitleStatus = false;

                mux_assert(ReadListOfNumbers(fp, 4, anum));
                t_user.who = anum[0];
                t_user.bUserIsOn = (anum[1] ? true : false);
                t_user.ComTitleStatus = (anum[2] ? true : false);
                t_user.bGagJoinLeave = (anum[3] ? true : false);

                // Read Comtitle.
                //
                size_t nTitle = GetLineTrunc(temp, LBUF_SIZE, fp);
                const UTF8* pTitle = temp;

                if (!Good_dbref(t_user.who))
                {
                    Log.tinyprintf(
                        T("load_comsystem: dbref %d out of range [0, %d)." ENDLINE), t_user.who, mudstate.db_top);
                }
                else if (isGarbage(t_user.who))
                {
                    Log.tinyprintf(T("load_comsystem: dbref is GARBAGE." ENDLINE), t_user.who);
                }
                else
                {
                    // Validate comtitle.
                    //
                    if (3 < nTitle && temp[0] == 't' && temp[1] == ':')
                    {
                        pTitle = temp + 2;
                        nTitle -= 2;
                        if (pTitle[nTitle - 1] == '\n')
                        {
                            // Get rid of trailing '\n'.
                            //
                            nTitle--;
                        }
                        if (nTitle <= 0 || MAX_TITLE_LEN < nTitle)
                        {
                            nTitle = 0;
                            pTitle = temp;
                        }
                    }
                    else
                    {
                        nTitle = 0;
                    }

                    t_user.title.assign(reinterpret_cast<const char *>(pTitle), nTitle);

                    auto result = ch->users.emplace(t_user.who, std::move(t_user));
                    comuser &user = result.first->second;

                    if (!(isPlayer(user.who))
                        && !(Going(user.who)
                            && (God(Owner(user.who)))))
                    {
                        do_joinchannel(user.who, ch);
                    }
                    user.bConnected = true;
                }
            }
        }
    }
}

// Version 4 start on 2007-MAR-17
//
//   -- Supports UTF-8 and ANSI as code-points.
//   -- Relies on a version number at the top of the file instead of within
//      this section.
//
void load_comsystem_V4(FILE* fp)
{
    LBuf temp = LBuf_Src("load_comsystem_V4");

    int nc = 0;
    if (nullptr == fgets(reinterpret_cast<char*>(temp.get()), LBUF_SIZE, fp))
    {
        return;
    }
    nc = mux_atol(temp);

    for (int i = 0; i < nc; i++)
    {
        int anum[10];

        auto ch = new channel();

        size_t nChannel = GetLineTrunc(temp, LBUF_SIZE, fp);
        if (nChannel > MAX_CHANNEL_LEN)
        {
            nChannel = MAX_CHANNEL_LEN;
        }
        if (temp[nChannel - 1] == '\n')
        {
            // Get rid of trailing '\n'.
            //
            nChannel--;
        }
        memcpy(ch->name, temp, nChannel);
        ch->name[nChannel] = '\0';

        size_t nHeader = GetLineTrunc(temp, LBUF_SIZE, fp);
        if (nHeader > MAX_HEADER_LEN)
        {
            nHeader = MAX_HEADER_LEN;
        }
        if (temp[nHeader - 1] == '\n')
        {
            nHeader--;
        }
        memcpy(ch->header, temp, nHeader);
        ch->header[nHeader] = '\0';

        vector<UTF8> channel_name_vector(ch->name, ch->name+nChannel);
        mudstate.channel_names.insert(make_pair(channel_name_vector, ch));

        ch->type = 127;
        ch->temp1 = 0;
        ch->temp2 = 0;
        ch->charge = 0;
        ch->charge_who = NOTHING;
        ch->amount_col = 0;
        ch->num_messages = 0;
        ch->chan_obj = NOTHING;

        mux_assert(ReadListOfNumbers(fp, 8, anum));
        ch->type = anum[0];
        ch->temp1 = anum[1];
        ch->temp2 = anum[2];
        ch->charge = anum[3];
        ch->charge_who = anum[4];
        ch->amount_col = anum[5];
        ch->num_messages = anum[6];
        ch->chan_obj = anum[7];

        int num_users_to_read = 0;
        mux_assert(ReadListOfNumbers(fp, 1, &num_users_to_read));
        if (num_users_to_read > 0)
        {
            for (int j = 0; j < num_users_to_read; j++)
            {
                comuser t_user;

                t_user.who = NOTHING;
                t_user.bUserIsOn = false;
                t_user.ComTitleStatus = false;

                mux_assert(ReadListOfNumbers(fp, 3, anum));
                t_user.who = anum[0];
                t_user.bUserIsOn = (anum[1] ? true : false);
                t_user.ComTitleStatus = (anum[2] ? true : false);

                // Read Comtitle.
                //
                size_t nTitle = GetLineTrunc(temp, LBUF_SIZE, fp);
                const UTF8* pTitle = temp;

                if (!Good_dbref(t_user.who))
                {
                    Log.tinyprintf(
                        T("load_comsystem: dbref %d out of range [0, %d)." ENDLINE), t_user.who, mudstate.db_top);
                }
                else if (isGarbage(t_user.who))
                {
                    Log.tinyprintf(T("load_comsystem: dbref is GARBAGE." ENDLINE), t_user.who);
                }
                else
                {
                    // Validate comtitle.
                    //
                    if (3 < nTitle && temp[0] == 't' && temp[1] == ':')
                    {
                        pTitle = temp + 2;
                        nTitle -= 2;
                        if (pTitle[nTitle - 1] == '\n')
                        {
                            // Get rid of trailing '\n'.
                            //
                            nTitle--;
                        }
                        if (nTitle <= 0 || MAX_TITLE_LEN < nTitle)
                        {
                            nTitle = 0;
                            pTitle = temp;
                        }
                    }
                    else
                    {
                        nTitle = 0;
                    }

                    t_user.title.assign(reinterpret_cast<const char *>(pTitle), nTitle);

                    auto result = ch->users.emplace(t_user.who, std::move(t_user));
                    comuser &user = result.first->second;

                    if (!(isPlayer(user.who))
                        && !(Going(user.who)
                            && (God(Owner(user.who)))))
                    {
                        do_joinchannel(user.who, ch);
                    }
                    user.bConnected = true;
                }
            }
        }
    }
}

// Load comsys database types 0, 1, 2 or 3.
//  Used Prior to Version 4 (2007-MAR-17)
//
void load_comsystem_V0123(FILE* fp)
{
    int ver = 0;
    LBuf temp = LBuf_Src("load_comsystem_V0123");

    int nc = 0;
    if (nullptr == fgets(reinterpret_cast<char *>(temp.get()), LBUF_SIZE, fp))
    {
        return;
    }
    if (!strncmp(reinterpret_cast<char *>(temp.get()), "+V", 2))
    {
        // +V2 has colored headers.
        //
        ver = mux_atol(temp + 2);
        if (ver < 1 || 3 < ver)
        {
            return;
        }

        mux_assert(ReadListOfNumbers(fp, 1, &nc));
    }
    else
    {
        nc = mux_atol(temp);
    }

    for (int i = 0; i < nc; i++)
    {
        int anum[10];

        auto ch = new channel();

        size_t nChannel = GetLineTrunc(temp, LBUF_SIZE, fp);
        if (temp[nChannel - 1] == '\n')
        {
            // Get rid of trailing '\n'.
            //
            nChannel--;
            temp[nChannel] = '\0';
        }

        // Convert entire line to UTF-8 including ANSI escapes.
        //
        const UTF8* pBufferUnicode = ConvertToUTF8(reinterpret_cast<char*>(temp.get()), &nChannel);
        if (MAX_CHANNEL_LEN < nChannel)
        {
            nChannel = MAX_CHANNEL_LEN;
            while (0 < nChannel
                && UTF8_CONTINUE <= utf8_FirstByte[temp[nChannel - 1]])
            {
                nChannel--;
            }
        }

        memcpy(ch->name, pBufferUnicode, nChannel);
        ch->name[nChannel] = '\0';

        if (ver >= 2)
        {
            size_t nHeader = GetLineTrunc(temp, LBUF_SIZE, fp);
            if (temp[nHeader - 1] == '\n')
            {
                nHeader--;
                temp[nHeader] = '\0';
            }

            // Convert entire line to UTF-8 including ANSI escapes.
            //
            pBufferUnicode = ConvertToUTF8(reinterpret_cast<char*>(temp.get()), &nHeader);
            if (MAX_HEADER_LEN < nHeader)
            {
                nHeader = MAX_HEADER_LEN;
                while (0 < nHeader
                    && UTF8_CONTINUE <= utf8_FirstByte[static_cast<unsigned char>(pBufferUnicode[nHeader - 1])])
                {
                    nHeader--;
                }
            }

            memcpy(ch->header, pBufferUnicode, nHeader);
            ch->header[nHeader] = '\0';
        }

        vector<UTF8> channel_name_vector(ch->name, ch->name + nChannel);
        mudstate.channel_names.insert(make_pair(channel_name_vector, ch));

        ch->type = 127;
        ch->temp1 = 0;
        ch->temp2 = 0;
        ch->charge = 0;
        ch->charge_who = NOTHING;
        ch->amount_col = 0;
        ch->num_messages = 0;
        ch->chan_obj = NOTHING;

        if (ver >= 1)
        {
            mux_assert(ReadListOfNumbers(fp, 8, anum));
            ch->type = anum[0];
            ch->temp1 = anum[1];
            ch->temp2 = anum[2];
            ch->charge = anum[3];
            ch->charge_who = anum[4];
            ch->amount_col = anum[5];
            ch->num_messages = anum[6];
            ch->chan_obj = anum[7];
        }
        else
        {
            mux_assert(ReadListOfNumbers(fp, 10, anum));
            ch->type = anum[0];
            // anum[1] is not used.
            ch->temp1 = anum[2];
            ch->temp2 = anum[3];
            // anum[4] is not used.
            ch->charge = anum[5];
            ch->charge_who = anum[6];
            ch->amount_col = anum[7];
            ch->num_messages = anum[8];
            ch->chan_obj = anum[9];
        }

        if (ver <= 1)
        {
            // Build colored header if not +V2 or later db.
            //
            if (ch->type & CHANNEL_PUBLIC)
            {
                mux_sprintf(temp, LBUF_SIZE, T("%s[%s%s%s%s%s]%s"), COLOR_FG_CYAN, COLOR_INTENSE,
                            COLOR_FG_BLUE, ch->name, COLOR_RESET, COLOR_FG_CYAN, COLOR_RESET);
            }
            else
            {
                mux_sprintf(temp, LBUF_SIZE, T("%s[%s%s%s%s%s]%s"), COLOR_FG_MAGENTA, COLOR_INTENSE,
                            COLOR_FG_RED, ch->name, COLOR_RESET, COLOR_FG_MAGENTA,
                            COLOR_RESET);
            }
            StripTabsAndTruncate(temp, ch->header, MAX_HEADER_LEN, MAX_HEADER_LEN);
        }

        int num_users_to_read = 0;
        mux_assert(ReadListOfNumbers(fp, 1, &num_users_to_read));
        if (num_users_to_read > 0)
        {
            for (int j = 0; j < num_users_to_read; j++)
            {
                comuser t_user;

                t_user.who = NOTHING;
                t_user.bUserIsOn = false;
                t_user.ComTitleStatus = false;

                if (ver == 3)
                {
                    mux_assert(ReadListOfNumbers(fp, 3, anum));
                    t_user.who = anum[0];
                    t_user.bUserIsOn = (anum[1] ? true : false);
                    t_user.ComTitleStatus = (anum[2] ? true : false);
                }
                else
                {
                    t_user.ComTitleStatus = true;
                    if (ver)
                    {
                        mux_assert(ReadListOfNumbers(fp, 2, anum));
                        t_user.who = anum[0];
                        t_user.bUserIsOn = (anum[1] ? true : false);
                    }
                    else
                    {
                        mux_assert(ReadListOfNumbers(fp, 4, anum));
                        t_user.who = anum[0];
                        // anum[1] is not used.
                        // anum[2] is not used.
                        t_user.bUserIsOn = (anum[3] ? true : false);
                    }
                }

                // Read Comtitle.
                //
                size_t nTitle = GetLineTrunc(temp, LBUF_SIZE, fp);

                // Convert entire line to UTF-8 including ANSI escapes.
                //
                UTF8* pTitle = ConvertToUTF8(reinterpret_cast<char*>(temp.get()), &nTitle);

                if (!Good_dbref(t_user.who))
                {
                    Log.tinyprintf(
                        T("load_comsystem: dbref %d out of range [0, %d)." ENDLINE), t_user.who, mudstate.db_top);
                }
                else if (isGarbage(t_user.who))
                {
                    Log.tinyprintf(T("load_comsystem: dbref is GARBAGE." ENDLINE), t_user.who);
                }
                else
                {
                    // Validate comtitle.
                    //
                    if (3 < nTitle && pTitle[0] == 't' && pTitle[1] == ':')
                    {
                        pTitle = pTitle + 2;
                        nTitle -= 2;
                        if (pTitle[nTitle - 1] == '\n')
                        {
                            // Get rid of trailing '\n'.
                            //
                            nTitle--;
                        }

                        if (nTitle <= 0
                            || MAX_TITLE_LEN < nTitle)
                        {
                            nTitle = 0;
                            pTitle = temp;
                        }
                    }
                    else
                    {
                        nTitle = 0;
                    }

                    t_user.title.assign(reinterpret_cast<const char *>(pTitle), nTitle);

                    auto result = ch->users.emplace(t_user.who, std::move(t_user));
                    comuser &user = result.first->second;

                    if (!(isPlayer(user.who))
                        && !(Going(user.who)
                            && (God(Owner(user.who)))))
                    {
                        do_joinchannel(user.who, ch);
                    }
                    user.bConnected = true;
                }
            }
        }
    }
}

void load_channels_V4(FILE* fp)
{
    LBuf buffer = LBuf_Src("load_channels_V4");

    int np = 0;
    mux_assert(ReadListOfNumbers(fp, 1, &np));
    for (int i = 0; i < np; i++)
    {
        int anum[2];
        int who = 0;
        int numchannels = 0;
        mux_assert(ReadListOfNumbers(fp, 2, anum));
        who = anum[0];
        numchannels = anum[1];

        comsys_t c;
        c.who = who;

        if (numchannels > 0)
        {
            for (int j = 0; j < numchannels; j++)
            {
                size_t n = GetLineTrunc(buffer, LBUF_SIZE, fp);
                if (buffer[n - 1] == '\n')
                {
                    // Get rid of trailing '\n'.
                    //
                    n--;
                    buffer[n] = '\0';
                }
                UTF8 aliasBuf[MAX_ALIAS_LEN + 1];
                UTF8 *channelName = nullptr;
                if (ParseChannelLine(buffer, aliasBuf, &channelName))
                {
                    com_alias ca;
                    ca.alias.assign(reinterpret_cast<const char *>(aliasBuf));
                    ca.channel.assign(reinterpret_cast<const char *>(channelName));
                    c.aliases.push_back(std::move(ca));
                    MEMFREE(channelName);
                }
            }
            sort_com_aliases(c);
        }
        if (Good_obj(c.who))
        {
            comsys_table[c.who] = std::move(c);
        }
        else
        {
            Log.tinyprintf(T("Invalid dbref %d." ENDLINE), who);
        }
        purge_comsystem();
    }
}

void load_channels_V0123(FILE* fp)
{
    char buffer[LBUF_SIZE];

    int np = 0;
    mux_assert(ReadListOfNumbers(fp, 1, &np));
    for (int i = 0; i < np; i++)
    {
        int anum[2];
        int who = 0;
        int numchannels = 0;
        mux_assert(ReadListOfNumbers(fp, 2, anum));
        who = anum[0];
        numchannels = anum[1];

        comsys_t c;
        c.who = who;

        if (numchannels > 0)
        {
            for (int j = 0; j < numchannels; j++)
            {
                size_t n = GetLineTrunc(reinterpret_cast<UTF8*>(buffer), sizeof(buffer), fp);
                if (buffer[n - 1] == '\n')
                {
                    // Get rid of trailing '\n'.
                    //
                    n--;
                    buffer[n] = '\0';
                }

                // Convert the entire line to UTF8 before parsing the line
                // into fields.  The first field no ANSI in it anyway.
                //
                size_t nBufferUnicode;
                UTF8* pBufferUnicode = ConvertToUTF8(buffer, &nBufferUnicode);
                UTF8 aliasBuf[MAX_ALIAS_LEN + 1];
                UTF8 *channelName = nullptr;
                if (ParseChannelLine(pBufferUnicode, aliasBuf, &channelName))
                {
                    com_alias ca;
                    ca.alias.assign(reinterpret_cast<const char *>(aliasBuf));
                    ca.channel.assign(reinterpret_cast<const char *>(channelName));
                    c.aliases.push_back(std::move(ca));
                    MEMFREE(channelName);
                }
            }
            sort_com_aliases(c);
        }
        if (Good_obj(c.who))
        {
            comsys_table[c.who] = std::move(c);
        }
        else
        {
            Log.tinyprintf(T("Invalid dbref %d." ENDLINE), who);
        }
        purge_comsystem();
    }
}

void load_comsys_V5(FILE* fp)
{
    char buffer[200];
    if (fgets(buffer, sizeof(buffer), fp)
        && strcmp(buffer, "*** Begin CHANNELS ***\n") == 0)
    {
        load_channels_V4(fp);
    }
    else
    {
        Log.tinyprintf(T("Error: Couldn\xE2\x80\x99t find Begin CHANNELS." ENDLINE));
        return;
    }

    if (fgets(buffer, sizeof(buffer), fp)
        && strcmp(buffer, "*** Begin COMSYS ***\n") == 0)
    {
        load_comsystem_V5(fp);
    }
    else
    {
        Log.tinyprintf(T("Error: Couldn\xE2\x80\x99t find Begin COMSYS." ENDLINE));
    }
}

void load_comsys_V4(FILE* fp)
{
    char buffer[200];
    if (fgets(buffer, sizeof(buffer), fp)
        && strcmp(buffer, "*** Begin CHANNELS ***\n") == 0)
    {
        load_channels_V4(fp);
    }
    else
    {
        Log.tinyprintf(T("Error: Couldn\xE2\x80\x99t find Begin CHANNELS." ENDLINE));
        return;
    }

    if (fgets(buffer, sizeof(buffer), fp)
        && strcmp(buffer, "*** Begin COMSYS ***\n") == 0)
    {
        load_comsystem_V4(fp);
    }
    else
    {
        Log.tinyprintf(T("Error: Couldn\xE2\x80\x99t find Begin COMSYS." ENDLINE));
    }
}

void load_comsys_V0123(FILE* fp)
{
    char buffer[200];
    if (fgets(buffer, sizeof(buffer), fp)
        && strcmp(buffer, "*** Begin CHANNELS ***\n") == 0)
    {
        load_channels_V0123(fp);
    }
    else
    {
        Log.tinyprintf(T("Error: Couldn\xE2\x80\x99t find Begin CHANNELS." ENDLINE));
        return;
    }

    if (fgets(buffer, sizeof(buffer), fp)
        && strcmp(buffer, "*** Begin COMSYS ***\n") == 0)
    {
        load_comsystem_V0123(fp);
    }
    else
    {
        Log.tinyprintf(T("Error: Couldn\xE2\x80\x99t find Begin COMSYS." ENDLINE));
    }
}


// Open the given filename, check version, and attempt to read comsystem
// data as indicated by the version number at the head of the file.
//
void load_comsys(UTF8* filename)
{
    comsys_table.clear();

    FILE* fp;
    if (!mux_fopen(&fp, filename, T("rb")))
    {
        Log.tinyprintf(T("Error: Couldn\xE2\x80\x99t find %s." ENDLINE), filename);
    }
    else
    {
        Log.tinyprintf(T("LOADING: %s" ENDLINE), filename);
        const int ch = getc(fp);
        if (EOF == ch)
        {
            Log.tinyprintf(T("Error: Couldn\xE2\x80\x99t read first byte."));
        }
        else
        {
            ungetc(ch, fp);
            if ('+' == ch)
            {
                // Version 4 or later.
                //
                UTF8 nbuf1[8];

                // Read the version number.
                //
                if (fgets(reinterpret_cast<char*>(nbuf1), sizeof(nbuf1), fp))
                {
                    if (strncmp(reinterpret_cast<char*>(nbuf1), "+V5", 3) == 0)
                    {
                        // Started v5 on 2026-MAR-04.
                        //
                        load_comsys_V5(fp);
                    }
                    else if (strncmp(reinterpret_cast<char*>(nbuf1), "+V4", 3) == 0)
                    {
                        // Started v4 on 2007-MAR-13.
                        //
                        load_comsys_V4(fp);
                    }
                }
            }
            else
            {
                load_comsys_V0123(fp);
            }
        }

        mux_fclose(fp);
        Log.tinyprintf(T("LOADING: %s (done)" ENDLINE), filename);
    }
}

// Save channel data and some user state info on a per-channel basis.
//
void save_comsystem(FILE* fp)
{
    // Number of channels.
    //
    mux_fprintf(fp, T("%d\n"), static_cast<int>(mudstate.channel_names.size()));
    for (auto it = mudstate.channel_names.begin(); it != mudstate.channel_names.end(); ++it)
    {
        const auto ch = it->second;

        // Channel name.
        //
        mux_fprintf(fp, T("%s\n"), ch->name);

        // Channel header.
        //
        mux_fprintf(fp, T("%s\n"), ch->header);

        mux_fprintf(fp, T("%d %d %d %d %d %d %d %d\n"), ch->type, ch->temp1,
                    ch->temp2, ch->charge, ch->charge_who, ch->amount_col,
                    ch->num_messages, ch->chan_obj);

        // Count the number of 'valid' users to dump.
        //
        int number_of_valid_users = 0;
        for (auto &kv : ch->users)
        {
            const comuser &user = kv.second;
            if (user.who >= 0 && user.who < mudstate.db_top)
            {
                number_of_valid_users++;
            }
        }

        // Number of users on this channel.
        //
        mux_fprintf(fp, T("%d\n"), number_of_valid_users);
        for (auto &kv : ch->users)
        {
            const comuser &user = kv.second;
            if (user.who >= 0 && user.who < mudstate.db_top)
            {
                // Write user state: dbref, on flag, comtitle status, and gag status.
                //
                mux_fprintf(fp, T("%d %d %d %d\n"), user.who, user.bUserIsOn, user.ComTitleStatus, user.bGagJoinLeave);

                // Write user title data.
                //
                if (!user.title.empty())
                {
                    mux_fprintf(fp, T("t:%s\n"), reinterpret_cast<const UTF8 *>(user.title.c_str()));
                }
                else
                {
                    mux_fprintf(fp, T("t:\n"));
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// call_mogrifier: Evaluate a MOGRIFY`<suffix> attribute on the channel
// object.  Returns an alloc_lbuf'd result (caller must free_lbuf) or
// nullptr if the attribute doesn't exist or is empty.
//
// Args passed as %0..%N-1.
//
static UTF8 *call_mogrifier
(
    const dbref chan_obj,
    const dbref executor,
    const UTF8 *suffix,
    const UTF8 *args[],
    int nargs
)
{
    if (!Good_obj(chan_obj))
    {
        return nullptr;
    }

    // Build attribute name: MOGRIFY`<suffix>
    //
    UTF8 aname[SBUF_SIZE];
    mux_sprintf(aname, sizeof(aname), T("MOGRIFY`%s"), suffix);

    ATTR *pattr = atr_str(aname);
    if (!pattr)
    {
        return nullptr;
    }

    dbref aowner;
    int aflags;
    UTF8 *atext = atr_pget(chan_obj, pattr->number, &aowner, &aflags);
    if (!*atext)
    {
        free_lbuf(atext);
        return nullptr;
    }

    UTF8 *result = alloc_lbuf("call_mogrifier");
    UTF8 *bp = result;
    mux_exec(atext, LBUF_SIZE-1, result, &bp, chan_obj, chan_obj, executor,
        AttrTrace(aflags, EV_FCHECK|EV_EVAL|EV_TOP),
        args, nargs);
    *bp = '\0';
    free_lbuf(atext);
    return result;
}

static void BuildChannelMessage
(
    const bool bSpoof,
    const UTF8* pHeader,
    const struct comuser* user,
    const dbref ch_obj,
    const UTF8* pPose,
    UTF8** messNormal,
    UTF8** messNoComtitle
)
{
    // Allocate necessary buffers.
    //
    *messNormal = alloc_lbuf("BCM.messNormal");
    *messNoComtitle = nullptr;
    if (!bSpoof)
    {
        *messNoComtitle = alloc_lbuf("BCM.messNoComtitle");
    }

    // Comtitle Check.
    //
    const bool hasComTitle = !user->title.empty();

    UTF8* mnptr = *messNormal; // Message without comtitle removal.
    UTF8* mncptr = *messNoComtitle; // Message with comtitle removal.

    safe_str(pHeader, *messNormal, &mnptr);
    safe_chr(' ', *messNormal, &mnptr);
    if (!bSpoof)
    {
        safe_str(pHeader, *messNoComtitle, &mncptr);
        safe_chr(' ', *messNoComtitle, &mncptr);
    }

    // Don't evaluate a title if there isn't one to parse or evaluation of
    // comtitles is disabled.  If they're set spoof, ComTitleStatus doesn't
    // matter.
    //
    if (hasComTitle && (user->ComTitleStatus || bSpoof))
    {
        if (mudconf.eval_comtitle)
        {
            // Evaluate the comtitle as code.
            //
            LBuf TempToEval = LBuf_Src("eval_comtitle");
            mux_strncpy(TempToEval, reinterpret_cast<const UTF8 *>(user->title.c_str()), LBUF_SIZE - 1);
            mux_exec(TempToEval, LBUF_SIZE - 1, *messNormal, &mnptr, user->who, user->who, user->who,
                     EV_FCHECK | EV_EVAL | EV_TOP, nullptr, 0);
        }
        else
        {
            safe_str(reinterpret_cast<const UTF8 *>(user->title.c_str()), *messNormal, &mnptr);
        }
        if (!bSpoof)
        {
            safe_chr(' ', *messNormal, &mnptr);
            safe_str(Moniker(user->who), *messNormal, &mnptr);
            safe_str(Moniker(user->who), *messNoComtitle, &mncptr);
        }
    }
    else
    {
        safe_str(Moniker(user->who), *messNormal, &mnptr);
        if (!bSpoof)
        {
            safe_str(Moniker(user->who), *messNoComtitle, &mncptr);
        }
    }

    bool bChannelSayString = false;
    bool bChannelSpeechMod = false;

    if (Good_obj(ch_obj))
    {
        dbref aowner;
        int aflags;
        UTF8* test_attr = atr_get("BuildChannelMessage.1304", ch_obj,
                                  A_SAYSTRING, &aowner, &aflags);

        if ('\0' != test_attr[0])
        {
            bChannelSayString = true;
        }
        free_lbuf(test_attr);

        test_attr = atr_get("BuildChannelMessage.1312", ch_obj,
                            A_SPEECHMOD, &aowner, &aflags);

        if ('\0' != test_attr[0])
        {
            bChannelSpeechMod = true;
        }
        free_lbuf(test_attr);
    }

    UTF8* saystring = nullptr;
    UTF8* newPose = nullptr;

    switch (pPose[0])
    {
    case ':':
        pPose++;
        if (' ' == *pPose)
        {
            pPose++;
        }
        else
        {
            safe_chr(' ', *messNormal, &mnptr);
            if (!bSpoof)
            {
                safe_chr(' ', *messNoComtitle, &mncptr);
            }
        }
        newPose = modSpeech(bChannelSpeechMod ? ch_obj : user->who, pPose, true, T("channel/pose"));
        if (newPose)
        {
            pPose = newPose;
        }
        safe_str(pPose, *messNormal, &mnptr);
        if (!bSpoof)
        {
            safe_str(pPose, *messNoComtitle, &mncptr);
        }
        break;

    case ';':
        pPose++;
        newPose = modSpeech(bChannelSpeechMod ? ch_obj : user->who, pPose, true, T("channel/pose"));
        if (newPose)
        {
            pPose = newPose;
        }
        safe_str(pPose, *messNormal, &mnptr);
        if (!bSpoof)
        {
            safe_str(pPose, *messNoComtitle, &mncptr);
        }
        break;

    default:
        newPose = modSpeech(bChannelSpeechMod ? ch_obj : user->who, pPose, true, T("channel"));
        if (newPose)
        {
            pPose = newPose;
        }
        saystring = modSpeech(bChannelSayString ? ch_obj : user->who, pPose, false, T("channel"));
        if (saystring)
        {
            safe_chr(' ', *messNormal, &mnptr);
            safe_str(saystring, *messNormal, &mnptr);
            safe_str(T(" \xE2\x80\x9C"), *messNormal, &mnptr);
        }
        else
        {
            safe_str(T(" says, \xE2\x80\x9C"), *messNormal, &mnptr);
        }
        safe_str(pPose, *messNormal, &mnptr);
        safe_str(T("\xE2\x80\x9D"), *messNormal, &mnptr);
        if (!bSpoof)
        {
            if (saystring)
            {
                safe_chr(' ', *messNoComtitle, &mncptr);
                safe_str(saystring, *messNoComtitle, &mncptr);
                safe_str(T(" \xE2\x80\x9C"), *messNoComtitle, &mncptr);
            }
            else
            {
                safe_str(T(" says, \xE2\x80\x9C"), *messNoComtitle, &mncptr);
            }
            safe_str(pPose, *messNoComtitle, &mncptr);
            safe_str(T("\xE2\x80\x9D"), *messNoComtitle, &mncptr);
        }
        break;
    }
    *mnptr = '\0';
    if (!bSpoof)
    {
        *mncptr = '\0';
    }
    if (newPose)
    {
        free_lbuf(newPose);
    }
    if (saystring)
    {
        free_lbuf(saystring);
    }
}

static void do_processcom(dbref player, UTF8* arg1, UTF8* arg2)
{
    if (!*arg2)
    {
        raw_notify(player, T("No message."));
        return;
    }
    if (3500 < strlen(reinterpret_cast<const char*>(arg2)))
    {
        arg2[3500] = '\0';
    }
    struct channel* ch = select_channel(arg1);
    if (!ch)
    {
        raw_notify(player, tprintf(T("Unknown channel %s."), arg1));
        return;
    }
    struct comuser* user = select_user(ch, player);
    if (!user)
    {
        raw_notify(player, T("You are not listed as on that channel.  Delete this alias and readd."));
        return;
    }

    if (Gagged(player)
        && !Wizard(player))
    {
        raw_notify(player, T("GAGGED players may not speak on channels."));
        return;
    }

    if (!strcmp(reinterpret_cast<const char*>(arg2), "on"))
    {
        do_joinchannel(player, ch);
    }
    else if (!strcmp(reinterpret_cast<const char*>(arg2), "off"))
    {
        do_leavechannel(player, ch);
    }
    else if (!user->bUserIsOn)
    {
        raw_notify(player, tprintf(T("You must be on %s to do that."), arg1));
    }
    else if (!strcmp(reinterpret_cast<const char*>(arg2), "who"))
    {
        do_comwho(player, ch);
    }
    else if (!strncmp(reinterpret_cast<const char*>(arg2), "last", 4)
        && (arg2[4] == '\0'
            || (arg2[4] == ' '
                && is_integer(arg2 + 5, nullptr))))
    {
        // Parse optional number after the 'last' command.
        //
        int nRecall = DFLT_RECALL_REQUEST;
        if (arg2[4] == ' ')
        {
            nRecall = mux_atol(arg2 + 5);
        }
        do_comlast(player, ch, nRecall);
    }
    else if (!test_transmit_access(player, ch))
    {
        raw_notify(player, T("That channel type cannot be transmitted on."));
    }
    else
    {
        if (!payfor(player, Guest(player) ? 0 : ch->charge))
        {
            raw_notify(player, tprintf(T("You don\xE2\x80\x99t have enough %s."), mudconf.many_coins));
            return;
        }
        ch->amount_col += ch->charge;
        sqlite_wt_channel(ch);
        giveto(ch->charge_who, ch->charge);

        // Check MOGRIFY`BLOCK on the channel object.  If it returns a
        // non-empty string, send that string to the speaker and suppress
        // the message entirely.
        //
        if (Good_obj(ch->chan_obj))
        {
            UTF8 chattype[2];
            chattype[0] = (arg2[0] == ':' || arg2[0] == ';') ? arg2[0] : '"';
            chattype[1] = '\0';
            UTF8 sdrBuf[32];
            mux_sprintf(sdrBuf, sizeof(sdrBuf), T("#%d"), player);
            const UTF8 *block_args[5] = {
                chattype, ch->name, arg2, (const UTF8 *)Name(player), sdrBuf
            };
            UTF8 *block_result = call_mogrifier(ch->chan_obj, player,
                T("BLOCK"), block_args, 5);
            if (block_result)
            {
                if (*block_result)
                {
                    raw_notify(player, block_result);
                    free_lbuf(block_result);
                    return;
                }
                free_lbuf(block_result);
            }
        }

        // BuildChannelMessage allocates messNormal and messNoComtitle,
        // SendChannelMessage frees them.
        //
        UTF8* messNormal;
        UTF8* messNoComtitle;
        BuildChannelMessage((ch->type & CHANNEL_SPOOF) != 0, ch->header, user,
                            ch->chan_obj, arg2, &messNormal, &messNoComtitle);
        SendChannelMessage(player, ch, messNormal, messNoComtitle);
    }
}

inline void notify_comsys(const dbref target, const dbref sender, const UTF8 *msg)
{
    notify_with_cause_ooc(target, sender, msg, MSG_SRC_COMSYS);
}

// Transmit the given message as appropriate to all listening parties.
// Perform channel message logging, if configured, for the channel.
//
void SendChannelMessage
(
    const dbref executor,
    struct channel* ch,
    UTF8* msgNormal,
    UTF8* msgNoComtitle,
    const bool bJoinLeaveMsg
)
{
    // Transmit messages.
    //
    const bool bSpoof = ((ch->type & CHANNEL_SPOOF) != 0);
    ch->num_messages++;
    sqlite_wt_channel(ch);

    // Look up the CHATFORMAT attribute number once (lazy init).
    //
    static int s_chatformat_atr = 0;
    static bool s_chatformat_init = false;
    if (!s_chatformat_init)
    {
        ATTR *pattr = atr_str(T("CHATFORMAT"));
        s_chatformat_atr = pattr ? pattr->number : 0;
        s_chatformat_init = true;
    }

    // Evaluate mogrifier attributes on the channel object, if present.
    // These run once per message, not per listener.
    //
    // MOGRIFY`MESSAGE — transform the composed message text.
    // MOGRIFY`FORMAT  — channel-wide format (like per-player CHATFORMAT).
    // MOGRIFY`OVERRIDE — if true, skip per-player CHATFORMAT.
    // MOGRIFY`NOBUFFER — if true, skip recall buffer logging.
    //
    UTF8 *mogMsg = nullptr;
    UTF8 *mogFmt = nullptr;
    bool bMogOverride = false;
    bool bMogNobuffer = false;

    if (  Good_obj(ch->chan_obj)
       && !bJoinLeaveMsg)
    {
        UTF8 sdrBuf[32];
        mux_sprintf(sdrBuf, sizeof(sdrBuf), T("#%d"), executor);
        const UTF8 *mog_args[3] = { ch->name, msgNormal, sdrBuf };

        // MOGRIFY`MESSAGE: if non-empty, replaces the message text.
        //
        mogMsg = call_mogrifier(ch->chan_obj, executor,
            T("MESSAGE"), mog_args, 3);

        // MOGRIFY`OVERRIDE: if true (non-zero), skip per-player CHATFORMAT.
        //
        UTF8 *mogOvr = call_mogrifier(ch->chan_obj, executor,
            T("OVERRIDE"), mog_args, 3);
        if (mogOvr)
        {
            bMogOverride = xlate(mogOvr);
            free_lbuf(mogOvr);
        }

        // MOGRIFY`NOBUFFER: if true (non-zero), skip recall buffer.
        //
        UTF8 *mogNB = call_mogrifier(ch->chan_obj, executor,
            T("NOBUFFER"), mog_args, 3);
        if (mogNB)
        {
            bMogNobuffer = xlate(mogNB);
            free_lbuf(mogNB);
        }

        // Update message args if MOGRIFY`MESSAGE replaced the text.
        //
        const UTF8 *fmt_msg = (mogMsg && *mogMsg) ? mogMsg : msgNormal;
        const UTF8 *fmt_args[3] = { ch->name, fmt_msg, sdrBuf };

        // MOGRIFY`FORMAT: channel-wide format applied to the
        // (possibly mogrified) message.
        //
        mogFmt = call_mogrifier(ch->chan_obj, executor,
            T("FORMAT"), fmt_args, 3);
    }

    // Determine effective message after mogrification.
    //
    const UTF8 *effMsgNormal = (mogMsg && *mogMsg) ? mogMsg : msgNormal;
    const UTF8 *effMsgNoComtitle = (mogMsg && *mogMsg) ? mogMsg : msgNoComtitle;

    for (auto &kv : ch->users)
    {
        comuser &user = kv.second;
        if (bJoinLeaveMsg && user.bGagJoinLeave)
        {
            continue;
        }
        if (user.bConnected
            && user.bUserIsOn
            && test_receive_access(user.who, ch))
        {
            // If MOGRIFY`FORMAT produced output, use it (channel-wide
            // formatting takes priority over per-player CHATFORMAT,
            // unless the player has their own CHATFORMAT and OVERRIDE
            // is not set).
            //
            if (mogFmt && *mogFmt && bMogOverride)
            {
                notify_comsys(user.who, executor, mogFmt);
                continue;
            }

            const UTF8 *pMsg;
            if (user.ComTitleStatus
                || bSpoof
                || effMsgNoComtitle == nullptr)
            {
                pMsg = effMsgNormal;
            }
            else
            {
                pMsg = effMsgNoComtitle;
            }

            // Check if the receiver has a CHATFORMAT attribute
            // (unless MOGRIFY`OVERRIDE suppresses it).
            //
            if (  0 < s_chatformat_atr
               && !bMogOverride)
            {
                dbref aowner;
                int aflags;
                UTF8 *chatfmt = atr_pget(user.who, s_chatformat_atr,
                    &aowner, &aflags);
                if ('\0' != chatfmt[0])
                {
                    UTF8 *fmtbuf = alloc_lbuf("chatformat");
                    UTF8 *bp = fmtbuf;
                    UTF8 sdrBuf[32];
                    mux_sprintf(sdrBuf, sizeof(sdrBuf), T("#%d"), executor);
                    const UTF8 *cfa[3] = { ch->name, pMsg, sdrBuf };
                    mux_exec(chatfmt, LBUF_SIZE-1, fmtbuf, &bp,
                        user.who, user.who, executor,
                        AttrTrace(aflags, EV_FCHECK|EV_EVAL|EV_TOP),
                        cfa, 3);
                    *bp = '\0';
                    notify_comsys(user.who, executor, fmtbuf);
                    free_lbuf(fmtbuf);
                    free_lbuf(chatfmt);
                    continue;
                }
                free_lbuf(chatfmt);
            }

            // Use MOGRIFY`FORMAT if available, otherwise the raw message.
            //
            if (mogFmt && *mogFmt)
            {
                notify_comsys(user.who, executor, mogFmt);
            }
            else
            {
                notify_comsys(user.who, executor, pMsg);
            }
        }
    }

    // Handle logging (skip if MOGRIFY`NOBUFFER).
    //
    const dbref obj = ch->chan_obj;
    if (  Good_obj(obj)
       && !bMogNobuffer)
    {
        dbref aowner;
        int aflags;
        int logmax = DFLT_MAX_LOG;
        ATTR* pattr = atr_str(T("MAX_LOG"));
        if (pattr
            && pattr->number)
        {
            UTF8* maxbuf = atr_get("SendChannelMessage.1141", obj, pattr->number, &aowner, &aflags);
            logmax = mux_atol(maxbuf);
            free_lbuf(maxbuf);
        }

        if (0 < logmax)
        {
            if (logmax > MAX_RECALL_REQUEST)
            {
                logmax = MAX_RECALL_REQUEST;
                atr_add(ch->chan_obj, pattr->number, mux_ltoa_t(logmax), GOD,
                        AF_CONST | AF_NOPROG | AF_NOPARSE);
            }
            const UTF8* p = tprintf(T("HISTORY_%d"), iMod(ch->num_messages, logmax));
            const int atr = mkattr(GOD, p);
            if (0 < atr)
            {
                pattr = atr_str(T("LOG_TIMESTAMPS"));
                if (pattr
                    && atr_get_info(obj, pattr->number, &aowner, &aflags))
                {
                    CLinearTimeAbsolute ltaNow;
                    ltaNow.GetLocal();

                    // Save message in history with timestamp.
                    //
                    LBuf temp = LBuf_Src("chan_history");
                    mux_sprintf(temp, LBUF_SIZE, T("[%s] %s"), ltaNow.ReturnDateString(0), msgNormal);
                    atr_add(ch->chan_obj, atr, temp, GOD, AF_CONST | AF_NOPROG | AF_NOPARSE);
                }
                else
                {
                    // Save message in history without timestamp.
                    //
                    atr_add(ch->chan_obj, atr, msgNormal, GOD,
                            AF_CONST | AF_NOPROG | AF_NOPARSE);
                }
            }
        }
    }
    else if (ch->chan_obj != NOTHING)
    {
        ch->chan_obj = NOTHING;
    }

    // Free mogrifier results.
    //
    if (mogMsg)
    {
        free_lbuf(mogMsg);
    }
    if (mogFmt)
    {
        free_lbuf(mogFmt);
    }

    // Since msgNormal and msgNoComTitle are no longer needed, free them here.
    //
    if (msgNormal)
    {
        free_lbuf(msgNormal);
    }
    if (msgNoComtitle
        && msgNoComtitle != msgNormal)
    {
        free_lbuf(msgNoComtitle);
    }
}

static void ChannelMOTD(dbref executor, dbref enactor, int attr)
{
    if (Good_obj(executor))
    {
        dbref aowner;
        int aflags;
        UTF8* q = atr_get("ChannelMOTD.1186", executor, attr, &aowner, &aflags);
        if ('\0' != q[0])
        {
            UTF8* buf = alloc_lbuf("chanmotd");
            UTF8* bp = buf;

            mux_exec(q, LBUF_SIZE - 1, buf, &bp, executor, executor, enactor,
                     AttrTrace(aflags, EV_FCHECK|EV_EVAL|EV_TOP), nullptr, 0);
            *bp = '\0';

            notify_comsys(enactor, executor, buf);

            free_lbuf(buf);
        }
        free_lbuf(q);
    }
}


// Add player to the given channel.  Transmit join messages as appropriate.
//
void do_joinchannel(const dbref player, struct channel* ch)
{
    int attr;

    struct comuser* user = select_user(ch, player);

    if (!user)
    {
        if (static_cast<int>(ch->users.size()) >= MAX_USERS_PER_CHANNEL)
        {
            raw_notify(player, tprintf(T("Too many people on channel %s already."),
                                       ch->name));
            return;
        }

        comuser cu;
        cu.who = player;
        cu.bUserIsOn = true;
        cu.ComTitleStatus = true;

        // if (Connected(player))&&(isPlayer(player))
        //
        if (UNDEAD(player))
        {
            cu.bConnected = true;
        }

        auto result = ch->users.emplace(player, std::move(cu));
        user = &result.first->second;

        sqlite_wt_channel_user(ch->name, user);
        attr = A_COMJOIN;
    }
    else if (!user->bUserIsOn)
    {
        user->bUserIsOn = true;
        sqlite_wt_channel_user(ch->name, user);
        attr = A_COMON;
    }
    else
    {
        raw_notify(player, tprintf(T("You are already on channel %s."), ch->name));
        return;
    }

    if (!Hidden(player))
    {
        UTF8 *messNormal, *messNoComtitle;
        BuildChannelMessage((ch->type & CHANNEL_SPOOF) != 0, ch->header, user,
                            ch->chan_obj, T(":has joined this channel."), &messNormal,
                            &messNoComtitle);
        SendChannelMessage(player, ch, messNormal, messNoComtitle, true);
    }
    ChannelMOTD(ch->chan_obj, user->who, attr);
}

// Process leave channnel request.
//
void do_leavechannel(dbref player, struct channel* ch)
{
    struct comuser* user = select_user(ch, player);
    raw_notify(player, tprintf(T("You have left channel %s."), ch->name));

    if (user->bUserIsOn)
    {
        if (!Hidden(player))
        {
            UTF8 *messNormal, *messNoComtitle;
            BuildChannelMessage((ch->type & CHANNEL_SPOOF) != 0, ch->header, user,
                                ch->chan_obj, T(":has left this channel."), &messNormal,
                                &messNoComtitle);
            SendChannelMessage(player, ch, messNormal, messNoComtitle, true);
        }
        ChannelMOTD(ch->chan_obj, user->who, A_COMOFF);
        user->bUserIsOn = false;
        sqlite_wt_channel_user(ch->name, user);
    }
}

static void do_comwho_line
(
    const dbref player,
    struct channel* ch,
    const struct comuser* user
)
{
    UTF8* msg;
    UTF8* buff = nullptr;

    if (!user->title.empty())
    {
        // There is a comtitle.
        //
        if (Staff(player))
        {
            buff = unparse_object(player, user->who, false);
            if (ch->type & CHANNEL_SPOOF)
            {
                msg = tprintf(T("%s as %s"), buff, reinterpret_cast<const UTF8 *>(user->title.c_str()));
            }
            else
            {
                msg = tprintf(T("%s as %s %s"), buff, reinterpret_cast<const UTF8 *>(user->title.c_str()), buff);
            }
        }
        else
        {
            if (ch->type & CHANNEL_SPOOF)
            {
                msg = const_cast<UTF8 *>(reinterpret_cast<const UTF8 *>(user->title.c_str()));
            }
            else
            {
                buff = unparse_object(player, user->who, false);
                msg = tprintf(T("%s %s"), reinterpret_cast<const UTF8 *>(user->title.c_str()), buff);
            }
        }
    }
    else
    {
        buff = unparse_object(player, user->who, false);
        msg = buff;
    }

    raw_notify(player, msg);
    if (buff)
    {
        free_lbuf(buff);
    }
}

void do_comwho(dbref player, struct channel* ch)
{
    raw_notify(player, T("-- Players --"));
    for (auto &kv : ch->users)
    {
        comuser &user = kv.second;
        if (isPlayer(user.who))
        {
            if (Connected(user.who)
                && (!Hidden(user.who)
                    || Wizard_Who(player)
                    || See_Hidden(player)))
            {
                if (user.bUserIsOn)
                {
                    do_comwho_line(player, ch, &user);
                }
            }
            else if (!Hidden(user.who))
            {
                do_comdisconnectchannel(user.who, ch->name);
            }
        }
    }
    raw_notify(player, T("-- Objects --"));
    for (auto &kv : ch->users)
    {
        comuser &user = kv.second;
        if (!isPlayer(user.who))
        {
            if (Going(user.who)
                && God(Owner(user.who)))
            {
                do_comdisconnectchannel(user.who, ch->name);
            }
            else if (user.bUserIsOn)
            {
                do_comwho_line(player, ch, &user);
            }
        }
    }
    raw_notify(player, tprintf(T("-- %s --"), ch->name));
}

void do_comlast(dbref player, struct channel* ch, int arg)
{
    // Validate the channel object.
    //
    if (!Good_obj(ch->chan_obj))
    {
        raw_notify(player, T("Channel does not have an object."));
        return;
    }

    dbref aowner;
    int aflags;
    const dbref obj = ch->chan_obj;
    int logmax = MAX_RECALL_REQUEST;

    // Lookup depth of logging.
    //
    ATTR* pattr = atr_str(T("MAX_LOG"));
    if (pattr
        && (atr_get_info(obj, pattr->number, &aowner, &aflags)))
    {
        UTF8* maxbuf = atr_get("do_comlast.1408", obj, pattr->number, &aowner, &aflags);
        logmax = mux_atol(maxbuf);
        free_lbuf(maxbuf);
    }

    if (logmax < 1)
    {
        raw_notify(player, T("Channel does not log."));
        return;
    }

    if (arg < MIN_RECALL_REQUEST)
    {
        arg = MIN_RECALL_REQUEST;
    }

    if (arg > logmax)
    {
        arg = logmax;
    }

    int histnum = ch->num_messages - arg;

    raw_notify(player, tprintf(T("%s -- Begin Comsys Recall --"), ch->header));

    for (int count = 0; count < arg; count++)
    {
        histnum++;
        pattr = atr_str(tprintf(T("HISTORY_%d"), iMod(histnum, logmax)));
        if (pattr)
        {
            UTF8* message = atr_get("do_comlast.1436", obj, pattr->number,
                                    &aowner, &aflags);
            raw_notify(player, message);
            free_lbuf(message);
        }
    }

    raw_notify(player, tprintf(T("%s -- End Comsys Recall --"), ch->header));
}

// Turn channel history timestamping on or off for the given channel.
//
static bool do_chanlog_timestamps(dbref player, UTF8* channel, UTF8* arg)
{
    UNUSED_PARAMETER(player);

    // Validate arg.
    //
    int value = 0;
    if (nullptr == arg
        || !is_integer(arg, nullptr)
        || ((value = mux_atol(arg)) != 0
            && value != 1))
    {
        // arg is not "0" and not "1".
        //
        return false;
    }

    struct channel* ch = select_channel(channel);
    if (!Good_obj(ch->chan_obj))
    {
        // No channel object has been set.
        //
        return false;
    }

    dbref aowner;
    int aflags;
    ATTR* pattr = atr_str(T("MAX_LOG"));
    if (nullptr == pattr
        || !atr_get_info(ch->chan_obj, pattr->number, &aowner, &aflags))
    {
        // Logging isn't enabled.
        //
        return false;
    }

    const int atr = mkattr(GOD, T("LOG_TIMESTAMPS"));
    if (atr <= 0)
    {
        return false;
    }

    if (value)
    {
        atr_add(ch->chan_obj, atr, mux_ltoa_t(value), GOD,
                AF_CONST | AF_NOPROG | AF_NOPARSE);
    }
    else
    {
        atr_clr(ch->chan_obj, atr);
    }

    return true;
}

// Set number of entries for channel logging.
//
static bool do_chanlog(dbref player, UTF8* channel, UTF8* arg)
{
    UNUSED_PARAMETER(player);

    int value;
    if (!*arg
        || !is_integer(arg, nullptr)
        || (value = mux_atol(arg)) > MAX_RECALL_REQUEST)
    {
        return false;
    }

    if (value < 0)
    {
        value = 0;
    }

    const struct channel* ch = select_channel(channel);
    if (!Good_obj(ch->chan_obj))
    {
        // No channel object has been set.
        //
        return false;
    }

    const int atr = mkattr(GOD, T("MAX_LOG"));
    if (atr <= 0)
    {
        return false;
    }

    dbref aowner;
    int aflags;
    UTF8* oldvalue = atr_get("do_chanlog.1477", ch->chan_obj, atr, &aowner, &aflags);
    const int oldnum = mux_atol(oldvalue);
    if (value < oldnum)
    {
        for (int count = 0; count <= oldnum; count++)
        {
            ATTR* hist = atr_str(tprintf(T("HISTORY_%d"), count));
            if (hist)
            {
                atr_clr(ch->chan_obj, hist->number);
            }
        }
    }
    free_lbuf(oldvalue);
    atr_add(ch->chan_obj, atr, mux_ltoa_t(value), GOD,
            AF_CONST | AF_NOPROG | AF_NOPARSE);
    return true;
}

// Find struct channel entry by name with the channel_name hash table.
//
struct channel* select_channel(UTF8* channel_name)
{
    // Try exact match first.
    //
    const auto channel_name_length = strlen(reinterpret_cast<char*>(channel_name));
    const vector<UTF8> channel_vector(channel_name, channel_name + channel_name_length);
    const auto it = mudstate.channel_names.find(channel_vector);
    if (it != mudstate.channel_names.end())
    {
        return it->second;
    }

    // Fall back to case-insensitive search.
    //
    for (const auto& entry : mudstate.channel_names)
    {
        if (0 == mux_stricmp(channel_name, entry.second->name))
        {
            return entry.second;
        }
    }
    return nullptr;
}

// Locate player in the user's list for the given channel.
//
struct comuser* select_user(struct channel* ch, const dbref player)
{
    if (!ch)
    {
        return nullptr;
    }

    auto it = ch->users.find(player);
    if (it != ch->users.end())
    {
        return &it->second;
    }
    return nullptr;
}

#define MAX_ALIASES_PER_PLAYER 100

void do_addcom
(
    const dbref executor,
    const dbref caller,
    dbref enactor,
    const int eval,
    const int key,
    const int nargs,
    UTF8* arg1,
    UTF8* channel,
    const UTF8* cargs[],
    int ncargs
)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(key);
    UNUSED_PARAMETER(nargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (nullptr != mudstate.pIComsysControl)
    {
        bool bValidAlias;
        size_t nValidAlias;
        UTF8* pValid = MakeCanonicalComAlias(arg1, &nValidAlias, &bValidAlias);
        if (bValidAlias)
        {
            mudstate.pIComsysControl->AddAlias(executor, pValid, channel);
        }
        else
        {
            raw_notify(executor, T("You need to specify a valid alias."));
        }
        return;
    }

    bool bValidAlias;
    size_t nValidAlias;
    UTF8* pValidAlias = MakeCanonicalComAlias(arg1, &nValidAlias, &bValidAlias);
    if (!bValidAlias)
    {
        raw_notify(executor, T("You need to specify a valid alias."));
        return;
    }
    if ('\0' == channel[0])
    {
        raw_notify(executor, T("You need to specify a channel."));
        return;
    }

    struct channel* ch = select_channel(channel);
    if (!ch)
    {
        UTF8 Buffer[MAX_CHANNEL_LEN + 1];
        StripTabsAndTruncate(channel, Buffer, MAX_CHANNEL_LEN, MAX_CHANNEL_LEN);
        raw_notify(executor, tprintf(T("Channel %s does not exist yet."), Buffer));
        return;
    }
    if (!test_join_access(executor, ch))
    {
        raw_notify(executor, T("Sorry, this channel type does not allow you to join."));
        return;
    }
    comsys_t* c = get_comsys(executor);
    if (static_cast<int>(c->aliases.size()) >= MAX_ALIASES_PER_PLAYER)
    {
        raw_notify(executor, tprintf(T("Sorry, but you have reached the maximum number of aliases allowed.")));
        return;
    }

    // Check if alias already exists.
    //
    string sAlias(reinterpret_cast<const char *>(pValidAlias));
    for (const auto &ca : c->aliases)
    {
        if (ca.alias == sAlias)
        {
            const UTF8* p = tprintf(T("That alias is already in use for channel %s."),
                reinterpret_cast<const UTF8 *>(ca.channel.c_str()));
            raw_notify(executor, p);
            return;
        }
    }

    com_alias newAlias;
    newAlias.alias = sAlias;
    newAlias.channel.assign(reinterpret_cast<const char *>(channel));
    c->aliases.push_back(std::move(newAlias));
    sort_com_aliases(*c);

    sqlite_wt_player_channel(executor, pValidAlias, channel);

    if (!select_user(ch, executor))
    {
        do_joinchannel(executor, ch);
    }

    raw_notify(executor, tprintf(T("Channel %s added with alias %s."), channel, pValidAlias));
}

void do_delcom(dbref executor, dbref caller, dbref enactor, int eval, int key, UTF8* arg1, const UTF8* cargs[],
               int ncargs)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(key);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (nullptr != mudstate.pIComsysControl)
    {
        mudstate.pIComsysControl->DelAlias(executor, arg1);
        return;
    }

    if (!arg1)
    {
        raw_notify(executor, T("Need an alias to delete."));
        return;
    }
    comsys_t* c = get_comsys(executor);

    string sArg(reinterpret_cast<const char *>(arg1));
    for (auto it = c->aliases.begin(); it != c->aliases.end(); ++it)
    {
        if (it->alias == sArg)
        {
            // Count how many aliases map to the same channel.
            //
            int found = 0;
            for (const auto &ca : c->aliases)
            {
                if (ca.channel == it->channel)
                {
                    found++;
                }
            }

            // If we found no other channels, delete it.
            //
            if (found <= 1)
            {
                do_delcomchannel(executor,
                    const_cast<UTF8 *>(reinterpret_cast<const UTF8 *>(it->channel.c_str())),
                    false);
                raw_notify(executor, tprintf(T("Alias %s for channel %s deleted."),
                                             arg1,
                                             reinterpret_cast<const UTF8 *>(it->channel.c_str())));
            }
            else
            {
                raw_notify(executor, tprintf(T("Alias %s for channel %s deleted."),
                                             arg1,
                                             reinterpret_cast<const UTF8 *>(it->channel.c_str())));
            }

            sqlite_wt_delete_player_channel(executor, arg1);
            c->aliases.erase(it);
            return;
        }
    }
    raw_notify(executor, T("Unable to find that alias."));
}

// Process a complete unsubscribe for a player from a particular channel.
//
void do_delcomchannel(dbref player, UTF8* channel, bool bQuiet)
{
    struct channel* ch = select_channel(channel);
    if (!ch)
    {
        raw_notify(player, tprintf(T("Unknown channel %s."), channel));
    }
    else
    {
        auto it = ch->users.find(player);
        if (it != ch->users.end())
        {
            comuser &user = it->second;
            do_comdisconnectchannel(player, channel);
            if (!bQuiet)
            {
                if (user.bUserIsOn
                    && !Hidden(player))
                {
                    UTF8 *messNormal, *messNoComtitle;
                    BuildChannelMessage((ch->type & CHANNEL_SPOOF) != 0,
                                        ch->header, &user, ch->chan_obj,
                                        T(":has left this channel."), &messNormal,
                                        &messNoComtitle);
                    SendChannelMessage(player, ch, messNormal, messNoComtitle, true);
                }
                raw_notify(player, tprintf(T("You have left channel %s."),
                                           channel));
            }

            ChannelMOTD(ch->chan_obj, user.who, A_COMLEAVE);
            sqlite_wt_delete_channel_user(ch->name, player);

            ch->users.erase(it);
        }
    }
}

void do_createchannel(const dbref executor, const dbref caller, dbref enactor, const int eval, const int key, UTF8* channel,
                      const UTF8* cargs[], const int ncargs)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(key);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (nullptr != mudstate.pIComsysControl)
    {
        mudstate.pIComsysControl->CreateChannel(executor, channel);
        return;
    }

    if ('\0' == channel[0])
    {
        raw_notify(executor, T("You must specify a channel to create."));
        return;
    }

    if (!Comm_All(executor))
    {
        raw_notify(executor, NOPERM_MESSAGE);
        return;
    }

    auto newchannel = new ::channel();

    size_t nNameNoANSI;
    UTF8* pNameNoANSI;
    UTF8 Buffer[MAX_HEADER_LEN + 1];
    mux_field fldChannel = StripTabsAndTruncate(channel, Buffer, MAX_HEADER_LEN,
                                                MAX_HEADER_LEN);
    if (fldChannel.m_byte == fldChannel.m_column)
    {
        // The channel name does not contain ANSI, so first, we add some to
        // get the header.
        //
        const size_t nMax = MAX_HEADER_LEN - (sizeof(COLOR_INTENSE) - 1)
            - (sizeof(COLOR_RESET) - 1) - 2;
        size_t nChannel = fldChannel.m_byte;
        if (nMax < nChannel)
        {
            nChannel = nMax;
        }
        Buffer[nChannel] = '\0';
        mux_sprintf(newchannel->header, sizeof(newchannel->header),
                    T("%s[%s]%s"), COLOR_INTENSE, Buffer, COLOR_RESET);

        // Then, we use the non-ANSI part for the name.
        //
        nNameNoANSI = nChannel;
        pNameNoANSI = Buffer;
    }
    else
    {
        // The given channel name does contain color.
        //
        memcpy(newchannel->header, Buffer, fldChannel.m_byte + 1);
        pNameNoANSI = strip_color(Buffer, &nNameNoANSI);
    }

    if (nNameNoANSI > MAX_CHANNEL_LEN)
    {
        raw_notify(executor, T("Channel name is too long."));
        delete newchannel;
        return;
    }

    memcpy(newchannel->name, pNameNoANSI, nNameNoANSI);
    newchannel->name[nNameNoANSI] = '\0';

    if (select_channel(newchannel->name))
    {
        raw_notify(executor, tprintf(T("Channel %s already exists."), newchannel->name));
        delete newchannel;
        return;
    }

    newchannel->type = 127;
    newchannel->temp1 = 0;
    newchannel->temp2 = 0;
    newchannel->charge = 0;
    newchannel->charge_who = executor;
    newchannel->amount_col = 0;
    newchannel->chan_obj = NOTHING;
    newchannel->num_messages = 0;

    const vector<UTF8> channel_name(newchannel->name, newchannel->name + nNameNoANSI);
    mudstate.channel_names.insert(make_pair(channel_name, newchannel));
    sqlite_wt_channel(newchannel);

    // Report the channel creation using non-ANSI name.
    //
    raw_notify(executor, tprintf(T("Channel %s created."), newchannel->name));
}

void do_destroychannel
(
    const dbref executor,
    const dbref caller,
    const dbref enactor,
    const int eval,
    const int key,
    UTF8* channel_name,
    const UTF8* cargs[],
    const int ncargs
)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(key);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (nullptr != mudstate.pIComsysControl)
    {
        mudstate.pIComsysControl->DestroyChannel(executor, channel_name);
        return;
    }

    const auto channel_name_length = strlen(reinterpret_cast<char*>(channel_name));
    const vector<UTF8> channel_name_vector(channel_name, channel_name + channel_name_length);
    const auto it = mudstate.channel_names.find(channel_name_vector);

    if (it != mudstate.channel_names.end())
    {
        raw_notify(executor, tprintf(T("Could not find channel_name %s."), channel_name));
        return;
    }

    auto ch = it->second;

    if (  !Comm_All(executor)
       && !Controls(executor, ch->charge_who))
    {
        raw_notify(executor, NOPERM_MESSAGE);
        return;
    }
    sqlite_wt_delete_channel(ch->name);

    delete ch;
    ch = nullptr;
    mudstate.channel_names.erase(it);
    raw_notify(executor, tprintf(T("Channel %s destroyed."), channel_name));
}


static void do_listchannels(dbref player, UTF8* pattern)
{
    const bool perm = Comm_All(player);
    if (!perm)
    {
        raw_notify(player, T("Warning: Only public channels and your channels will be shown."));
    }

    bool bWild;
    if (nullptr != pattern
        && '\0' != *pattern)
    {
        bWild = true;
    }
    else
    {
        bWild = false;
    }

    raw_notify(player, T("*** Channel       Header          Owner           Access  Users Msgs"));

    for (auto it = mudstate.channel_names.begin(); it != mudstate.channel_names.end(); ++it)
    {
        const auto ch = it->second;

        if (  perm
           || (ch->type & CHANNEL_PUBLIC)
           || Controls(player, ch->charge_who)
           || nullptr != select_user(ch, player))
        {
            if (  !bWild
               || quick_wild(pattern, ch->name))
            {
                // Determine effective access for the querying player
                // based on both flags and locks.
                //
                bool bCanJoin = test_join_access(player, ch);
                bool bCanXmit = test_transmit_access(player, ch);
                bool bCanRecv = test_receive_access(player, ch);

                UTF8* temp = alloc_lbuf("do_listchannels");
                UTF8* bp = temp;

                // PLS flags.
                //
                safe_chr((ch->type & CHANNEL_PUBLIC) ? 'P' : '-', temp, &bp);
                safe_chr((ch->type & CHANNEL_LOUD)   ? 'L' : '-', temp, &bp);
                safe_chr((ch->type & CHANNEL_SPOOF)  ? 'S' : '-', temp, &bp);
                safe_chr(' ', temp, &bp);

                // Channel name (13 cols).
                //
                mux_field iPos(4, 4);
                iPos += StripTabsAndTruncate(ch->name,
                    temp + iPos.m_byte,
                    (LBUF_SIZE - 1) - iPos.m_byte,
                    13);
                bp = temp + iPos.m_byte;
                iPos = PadField(temp, LBUF_SIZE - 1, 18, iPos);
                bp = temp + iPos.m_byte;

                // Header (15 cols).
                //
                const UTF8 *pHeader = ch->header;
                if ('\0' == pHeader[0])
                {
                    pHeader = T("-");
                }
                iPos += StripTabsAndTruncate(pHeader,
                    temp + iPos.m_byte,
                    (LBUF_SIZE - 1) - iPos.m_byte,
                    15);
                bp = temp + iPos.m_byte;
                iPos = PadField(temp, LBUF_SIZE - 1, 34, iPos);
                bp = temp + iPos.m_byte;

                // Owner name (15 cols).
                //
                iPos += StripTabsAndTruncate(Moniker(ch->charge_who),
                    temp + iPos.m_byte,
                    (LBUF_SIZE - 1) - iPos.m_byte,
                    15);
                bp = temp + iPos.m_byte;
                iPos = PadField(temp, LBUF_SIZE - 1, 50, iPos);
                bp = temp + iPos.m_byte;

                // Effective access JXR (3 cols + 2 spaces).
                //
                safe_chr(bCanJoin ? 'J' : '-', temp, &bp);
                safe_chr(bCanXmit ? 'X' : '-', temp, &bp);
                safe_chr(bCanRecv ? 'R' : '-', temp, &bp);
                iPos = mux_field(
                    static_cast<unsigned int>(bp - temp),
                    static_cast<unsigned int>(bp - temp));
                iPos = PadField(temp, LBUF_SIZE - 1, 56, iPos);
                bp = temp + iPos.m_byte;

                // Users and Messages.
                //
                mux_sprintf(bp, (LBUF_SIZE - 1) - (bp - temp),
                    T("%5d %4d"),
                    static_cast<int>(ch->users.size()),
                    ch->num_messages);
                raw_notify(player, temp);
                free_lbuf(temp);
            }
        }
    }
    raw_notify(player, T("-- End of list of Channels --"));
}

void do_comtitle
(
    const dbref executor,
    const dbref caller,
    const dbref enactor,
    int eval,
    const int key,
    const int nargs,
    UTF8* arg1,
    UTF8* arg2,
    const UTF8* cargs[],
    const int ncargs
)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (nullptr != mudstate.pIComsysControl)
    {
        mudstate.pIComsysControl->ComTitle(executor, arg1, arg2, key);
        return;
    }

    if (!*arg1)
    {
        raw_notify(executor, T("Need an alias to do comtitle."));
        return;
    }

    UTF8 channel[MAX_CHANNEL_LEN + 1];
    mux_strncpy(channel, get_channel_from_alias(executor, arg1), MAX_CHANNEL_LEN);

    if (channel[0] == '\0')
    {
        raw_notify(executor, T("Unknown alias."));
        return;
    }
    struct channel* ch = select_channel(channel);
    if (ch)
    {
        if (select_user(ch, executor))
        {
            if (key == COMTITLE_OFF)
            {
                if ((ch->type & CHANNEL_SPOOF) == 0)
                {
                    raw_notify(executor, tprintf(T("Comtitles are now off for channel %s"), channel));
                    do_setcomtitlestatus(executor, ch, false);
                }
                else
                {
                    raw_notify(executor, T("You can not turn off comtitles on that channel."));
                }
            }
            else if (key == COMTITLE_ON)
            {
                raw_notify(executor, tprintf(T("Comtitles are now on for channel %s"), channel));
                do_setcomtitlestatus(executor, ch, true);
            }
            else if (key == COMTITLE_GAG)
            {
                raw_notify(executor, tprintf(T("Join/leave messages are now gagged for channel %s"), channel));
                do_setgagjoinleavestatus(executor, ch, true);
            }
            else if (key == COMTITLE_UNGAG)
            {
                raw_notify(executor, tprintf(T("Join/leave messages are now ungagged for channel %s"), channel));
                do_setgagjoinleavestatus(executor, ch, false);
            }
            else
            {
                UTF8* pValidatedTitleValue = RestrictTitleValue(arg2);
                do_setnewtitle(executor, ch, pValidatedTitleValue);
                raw_notify(executor, tprintf(T("Title set to \xE2\x80\x98%s\xE2\x80\x99 on channel %s."),
                                             pValidatedTitleValue, channel));
            }
        }
    }
    else
    {
        raw_notify(executor, T("Illegal comsys alias, please delete."));
    }
}

void do_comlist
(
    const dbref executor,
    dbref caller,
    const dbref enactor,
    int eval,
    const int key,
    UTF8* pattern,
    const UTF8* cargs[],
    const int ncargs
)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(key);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (nullptr != mudstate.pIComsysControl)
    {
        mudstate.pIComsysControl->ComList(executor, pattern);
        return;
    }

    bool bWild;
    if (nullptr != pattern
        && '\0' != *pattern)
    {
        bWild = true;
    }
    else
    {
        bWild = false;
    }

    raw_notify(executor, T("Alias           Channel            Status   Title"));

    const comsys_t* c = get_comsys(executor);
    for (size_t i = 0; i < c->aliases.size(); i++)
    {
        const UTF8 *chanName = reinterpret_cast<const UTF8 *>(c->aliases[i].channel.c_str());
        struct comuser* user = select_user(select_channel(const_cast<UTF8 *>(chanName)), executor);
        if (user)
        {
            if (!bWild
                || quick_wild(pattern, chanName))
            {
                UTF8* p =
                    tprintf(T("%-15.15s %-18.18s %s %s%s %s"),
                            reinterpret_cast<const UTF8 *>(c->aliases[i].alias.c_str()),
                            chanName,
                            (user->bUserIsOn ? "on " : "off"),
                            (user->ComTitleStatus ? "con " : "coff"),
                            (user->bGagJoinLeave ? " gag" : ""),
                            reinterpret_cast<const UTF8 *>(user->title.c_str()));
                raw_notify(executor, p);
            }
        }
        else
        {
            raw_notify(executor, tprintf(
                           T("Bad Comsys Alias: %s for Channel: %s"),
                           reinterpret_cast<const UTF8 *>(c->aliases[i].alias.c_str()),
                           chanName));
        }
    }
    raw_notify(executor, T("-- End of comlist --"));
}

// Cleanup channels owned by the player.
//
void do_channelnuke(const dbref player)
{
    if (nullptr != mudstate.pIComsysControl)
    {
        return;  // Module handles via IServerEventsSink::data_free.
    }

    bool found = true;
    while (found)
    {
        found = false;
        for (auto it = mudstate.channel_names.begin(); it != mudstate.channel_names.end(); ++it)
        {
            auto ch = it->second;

            if (player == ch->charge_who)
            {
                sqlite_wt_delete_channel(ch->name);

                delete ch;
                ch = nullptr;

                // Removing an element invalidates the iterator, so the search much be restarted.
                //
                mudstate.channel_names.erase(it);
                found = true;
                break;
            }
        }
    }
}

void do_clearcom(const dbref executor, const dbref caller, const dbref enactor, int eval, const int key)
{
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(key);

    if (nullptr != mudstate.pIComsysControl)
    {
        mudstate.pIComsysControl->ClearAliases(executor);
        return;
    }

    const comsys_t* c = get_comsys(executor);

    for (int i = static_cast<int>(c->aliases.size()) - 1; i > -1; --i)
    {
        do_delcom(executor, caller, enactor, 0, 0,
            const_cast<UTF8 *>(reinterpret_cast<const UTF8 *>(c->aliases[i].alias.c_str())),
            nullptr, 0);
    }
}

void do_allcom(dbref executor, dbref caller, dbref enactor, int eval, int key, UTF8* arg1, const UTF8* cargs[],
               int ncargs)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(key);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (nullptr != mudstate.pIComsysControl)
    {
        mudstate.pIComsysControl->AllCom(executor, arg1);
        return;
    }

    if (strcmp(reinterpret_cast<char*>(arg1), "who") != 0
        && strcmp(reinterpret_cast<char*>(arg1), "on") != 0
        && strcmp(reinterpret_cast<char*>(arg1), "off") != 0)
    {
        raw_notify(executor, T("Only options available are: on, off and who."));
        return;
    }

    const comsys_t* c = get_comsys(executor);
    for (size_t i = 0; i < c->aliases.size(); i++)
    {
        do_processcom(executor,
            const_cast<UTF8 *>(reinterpret_cast<const UTF8 *>(c->aliases[i].channel.c_str())),
            arg1);
        if (strcmp(reinterpret_cast<char*>(arg1), "who") == 0)
        {
            raw_notify(executor, T(""));
        }
    }
}

void do_channelwho(const dbref executor, const dbref caller, dbref enactor, const int eval, const int key, UTF8* arg1, const UTF8* cargs[],
                   const int ncargs)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(key);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (nullptr != mudstate.pIComsysControl)
    {
        mudstate.pIComsysControl->ChanWho(executor, arg1);
        return;
    }

    UTF8 channel[MAX_CHANNEL_LEN + 1];
    size_t i = 0;
    while ('\0' != arg1[i]
        && '/' != arg1[i]
        && i < MAX_CHANNEL_LEN)
    {
        channel[i] = arg1[i];
        i++;
    }
    channel[i] = '\0';

    bool bAll = false;
    if ('/' == arg1[i]
        && 'a' == arg1[i + 1])
    {
        bAll = true;
    }

    struct channel* ch = nullptr;
    if (i <= MAX_CHANNEL_LEN)
    {
        ch = select_channel(channel);
    }
    if (nullptr == ch)
    {
        raw_notify(executor, tprintf(T("Unknown channel %s."), channel));
        return;
    }
    if (!(Comm_All(executor)
        || Controls(executor, ch->charge_who)))
    {
        raw_notify(executor, NOPERM_MESSAGE);
        return;
    }

    raw_notify(executor, tprintf(T("-- %s --"), ch->name));
    raw_notify(executor, tprintf(T("%-29.29s %-6.6s %-6.6s"), "Name", "Status", "Player"));
    for (auto &kv : ch->users)
    {
        const comuser &user = kv.second;
        if ((bAll
                || UNDEAD(user.who))
            && (!Hidden(user.who)
                || Wizard_Who(executor)
                || See_Hidden(executor)))
        {
            static UTF8 temp[SBUF_SIZE];
            UTF8* buff = unparse_object(executor, user.who, false);
            mux_sprintf(temp, sizeof(temp), T("%-29.29s %-6.6s %-6.6s"), strip_color(buff),
                        user.bUserIsOn ? "on " : "off",
                        isPlayer(user.who) ? "yes" : "no ");
            raw_notify(executor, temp);
            free_lbuf(buff);
        }
    }
    raw_notify(executor, tprintf(T("-- %s --"), ch->name));
}

// Assemble and transmit player disconnection messages to the player's active
// set of channels.
//
static void do_comdisconnectraw_notify(const dbref player, UTF8* chan)
{
    struct channel* ch = select_channel(chan);
    if (!ch) return;

    struct comuser* cu = select_user(ch, player);
    if (!cu) return;

    if ((ch->type & CHANNEL_LOUD)
        && cu->bUserIsOn
        && !Hidden(player))
    {
        UTF8 *messNormal, *messNoComtitle;
        BuildChannelMessage((ch->type & CHANNEL_SPOOF) != 0, ch->header, cu,
                            ch->chan_obj, T(":has disconnected."), &messNormal,
                            &messNoComtitle);
        SendChannelMessage(player, ch, messNormal, messNoComtitle, true);
    }
}

// Assemble and transmit player connection messages to the player's active
// set of channels.
//
static void do_comconnectraw_notify(const dbref player, UTF8* chan)
{
    struct channel* ch = select_channel(chan);
    if (!ch) return;
    struct comuser* cu = select_user(ch, player);
    if (!cu) return;

    if ((ch->type & CHANNEL_LOUD)
        && cu->bUserIsOn
        && !Hidden(player))
    {
        UTF8 *messNormal, *messNoComtitle;
        BuildChannelMessage((ch->type & CHANNEL_SPOOF) != 0, ch->header, cu,
                            ch->chan_obj, T(":has connected."), &messNormal,
                            &messNoComtitle);
        SendChannelMessage(player, ch, messNormal, messNoComtitle, true);
    }
}

// Set player as connected on the channel.
//
static void do_comconnectchannel(dbref player, UTF8* channel, const string &alias)
{
    struct channel* ch = select_channel(channel);
    if (ch)
    {
        struct comuser* user = select_user(ch, player);
        if (user)
        {
            user->bConnected = true;
        }
        else
        {
            raw_notify(player,
                       tprintf(T("Bad Comsys Alias: %s for Channel: %s"),
                               reinterpret_cast<const UTF8 *>(alias.c_str()), channel));
        }
    }
    else
    {
        raw_notify(player, tprintf(T("Bad Comsys Alias: %s for Channel: %s"),
                                   reinterpret_cast<const UTF8 *>(alias.c_str()), channel));
    }
}

// Check player for any active channels.  If found, mark the player as
// disconnected.  Transmit disconnection messages as needed.
//
void do_comdisconnect(const dbref player)
{
    if (nullptr != mudstate.pIComsysControl)
    {
        return;  // Module handles via IServerEventsSink::disconnect.
    }

    auto it = comsys_table.find(player);
    if (it == comsys_table.end())
    {
        return;
    }
    const comsys_t &c = it->second;

    vector<string> seen;
    for (const auto &ca : c.aliases)
    {
        bool bFound = false;
        for (const auto &s : seen)
        {
            if (s == ca.channel)
            {
                bFound = true;
                break;
            }
        }

        if (!bFound)
        {
            seen.push_back(ca.channel);
            UTF8 *chanName = const_cast<UTF8 *>(reinterpret_cast<const UTF8 *>(ca.channel.c_str()));

            // Process channel removals.
            //
            do_comdisconnectchannel(player, chanName);

            // Send disconnection messages if necessary.
            //
            do_comdisconnectraw_notify(player, chanName);
        }
    }
}

// Locate all active channels for the given player; mark the player as
// connected and send connect notifications as appropriate.
//
void do_comconnect(const dbref player)
{
    if (nullptr != mudstate.pIComsysControl)
    {
        return;  // Module handles via IServerEventsSink::connect.
    }

    auto it = comsys_table.find(player);
    if (it == comsys_table.end())
    {
        return;
    }
    const comsys_t &c = it->second;

    vector<string> seen;
    for (const auto &ca : c.aliases)
    {
        bool bFound = false;
        for (const auto &s : seen)
        {
            if (s == ca.channel)
            {
                bFound = true;
                break;
            }
        }

        if (!bFound)
        {
            seen.push_back(ca.channel);
            UTF8 *chanName = const_cast<UTF8 *>(reinterpret_cast<const UTF8 *>(ca.channel.c_str()));
            do_comconnectchannel(player, chanName, ca.alias);
            do_comconnectraw_notify(player, chanName);
        }
    }
}


// Mark the given player as disconnected on the channel.
//
void do_comdisconnectchannel(const dbref player, UTF8* channel)
{
    struct channel* ch = select_channel(channel);
    if (!ch)
    {
        return;
    }

    struct comuser* user = select_user(ch, player);
    if (user)
    {
        user->bConnected = false;
    }
}

void do_editchannel
(
    dbref executor,
    dbref caller,
    dbref enactor,
    int eval,
    int flag,
    int nargs,
    UTF8* arg1,
    UTF8* arg2,
    const UTF8* cargs[],
    int ncargs
)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (nullptr != mudstate.pIComsysControl)
    {
        mudstate.pIComsysControl->EditChannel(executor, arg1, arg2, flag);
        return;
    }

    struct channel* ch = select_channel(arg1);
    if (!ch)
    {
        raw_notify(executor, tprintf(T("Unknown channel %s."), arg1));
        return;
    }

    if (!(Comm_All(executor)
        || Controls(executor, ch->charge_who)))
    {
        raw_notify(executor, NOPERM_MESSAGE);
        return;
    }

    bool add_remove = true;
    UTF8* s = arg2;
    if (*s == '!')
    {
        add_remove = false;
        s++;
    }

    switch (flag)
    {
    case EDIT_CHANNEL_CCHOWN:
        {
            init_match(executor, arg2, NOTYPE);
            match_everything(0);
            const dbref who = match_result();
            if (Good_obj(who))
            {
                ch->charge_who = who;
                raw_notify(executor, T("Set."));
            }
            else
            {
                raw_notify(executor, T("Invalid player."));
            }
        }
        break;

    case EDIT_CHANNEL_CCHARGE:
        {
            const int c_charge = mux_atol(arg2);
            if (0 <= c_charge
                && c_charge <= MAX_COST)
            {
                ch->charge = c_charge;
                raw_notify(executor, T("Set."));
            }
            else
            {
                raw_notify(executor, T("That is not a reasonable cost."));
            }
        }
        break;

    case EDIT_CHANNEL_CPFLAGS:
        {
            int access = 0;
            if (strcmp(reinterpret_cast<char*>(s), "join") == 0)
            {
                access = CHANNEL_PLAYER_JOIN;
            }
            else if (strcmp(reinterpret_cast<char*>(s), "receive") == 0)
            {
                access = CHANNEL_PLAYER_RECEIVE;
            }
            else if (strcmp(reinterpret_cast<char*>(s), "transmit") == 0)
            {
                access = CHANNEL_PLAYER_TRANSMIT;
            }
            else
            {
                raw_notify(executor, T("@cpflags: Unknown Flag."));
            }

            if (access)
            {
                if (add_remove)
                {
                    ch->type |= access;
                    raw_notify(executor, T("@cpflags: Set."));
                }
                else
                {
                    ch->type &= ~access;
                    raw_notify(executor, T("@cpflags: Cleared."));
                }
            }
        }
        break;

    case EDIT_CHANNEL_COFLAGS:
        {
            int access = 0;
            if (strcmp(reinterpret_cast<char*>(s), "join") == 0)
            {
                access = CHANNEL_OBJECT_JOIN;
            }
            else if (strcmp(reinterpret_cast<char*>(s), "receive") == 0)
            {
                access = CHANNEL_OBJECT_RECEIVE;
            }
            else if (strcmp(reinterpret_cast<char*>(s), "transmit") == 0)
            {
                access = CHANNEL_OBJECT_TRANSMIT;
            }
            else
            {
                raw_notify(executor, T("@coflags: Unknown Flag."));
            }

            if (access)
            {
                if (add_remove)
                {
                    ch->type |= access;
                    raw_notify(executor, T("@coflags: Set."));
                }
                else
                {
                    ch->type &= ~access;
                    raw_notify(executor, T("@coflags: Cleared."));
                }
            }
        }
        break;
    }
    sqlite_wt_channel(ch);
}

bool test_join_access(const dbref player, struct channel* chan)
{
    if (Comm_All(player))
    {
        return true;
    }

    int access;
    if (isPlayer(player))
    {
        access = CHANNEL_PLAYER_JOIN;
    }
    else
    {
        access = CHANNEL_OBJECT_JOIN;
    }
    return ((chan->type & access) != 0
        || could_doit(player, chan->chan_obj, A_LOCK));
}

bool test_transmit_access(const dbref player, struct channel* chan)
{
    if (Comm_All(player))
    {
        return true;
    }

    int access;
    if (isPlayer(player))
    {
        access = CHANNEL_PLAYER_TRANSMIT;
    }
    else
    {
        access = CHANNEL_OBJECT_TRANSMIT;
    }
    return ((chan->type & access) != 0
        || could_doit(player, chan->chan_obj, A_LUSE));
}

bool test_receive_access(const dbref player, struct channel* chan)
{
    if (Comm_All(player))
    {
        return true;
    }

    int access;
    if (isPlayer(player))
    {
        access = CHANNEL_PLAYER_RECEIVE;
    }
    else
    {
        access = CHANNEL_OBJECT_RECEIVE;
    }
    return ((chan->type & access) != 0
        || could_doit(player, chan->chan_obj, A_LENTER));
}

// true means continue, and false means stop.
//
bool do_comsystem(const dbref who, UTF8* cmd)
{
    auto t = reinterpret_cast<UTF8*>(strchr(reinterpret_cast<char*>(cmd), ' '));
    if (!t
        || t - cmd > MAX_ALIAS_LEN
        || t[1] == '\0')
    {
        // Doesn't fit the pattern of "alias message".
        //
        return true;
    }

    UTF8 alias[MAX_ALIAS_LEN + 1];
    memcpy(alias, cmd, t - cmd);
    alias[t - cmd] = '\0';

    const UTF8* ch = get_channel_from_alias(who, alias);
    if (ch[0] == '\0')
    {
        // Not really an alias after all.
        //
        return true;
    }

    t++;
    do_processcom(who, const_cast<UTF8 *>(ch), t);
    return false;
}

void do_cemit
(
    const dbref executor,
    const dbref caller,
    const dbref enactor,
    const int eval,
    const int key,
    const int nargs,
    UTF8* chan,
    UTF8* text,
    const UTF8* cargs[],
    const int ncargs
)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (nullptr != mudstate.pIComsysControl)
    {
        mudstate.pIComsysControl->CEmit(executor, chan, text, key);
        return;
    }

    struct channel* ch = select_channel(chan);
    if (!ch)
    {
        raw_notify(executor, tprintf(T("Channel %s does not exist."), chan));
        return;
    }
    if (!Controls(executor, ch->charge_who)
        && !Comm_All(executor))
    {
        raw_notify(executor, NOPERM_MESSAGE);
        return;
    }
    UTF8* text2 = alloc_lbuf("do_cemit");
    if (key == CEMIT_NOHEADER)
    {
        mux_strncpy(text2, text, LBUF_SIZE - 1);
    }
    else
    {
        mux_strncpy(text2, tprintf(T("%s %s"), ch->header, text), LBUF_SIZE - 1);
    }
    SendChannelMessage(executor, ch, text2, text2);
}

void do_chopen
(
    const dbref executor,
    const dbref caller,
    const dbref enactor,
    const int eval,
    const int key,
    const int nargs,
    UTF8* chan,
    UTF8* value,
    const UTF8* cargs[],
    int ncargs
)
{
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (nullptr != mudstate.pIComsysControl)
    {
        mudstate.pIComsysControl->CSet(executor, chan, value, key);
        return;
    }

    if (key == CSET_LIST)
    {
        do_chanlist(executor, caller, enactor, 0, 1, nullptr, nullptr, 0);
        return;
    }

    const UTF8* msg = nullptr;
    struct channel* ch = select_channel(chan);
    if (!ch)
    {
        msg = tprintf(T("@cset: Channel %s does not exist."), chan);
        raw_notify(executor, msg);
        return;
    }
    if (!Controls(executor, ch->charge_who)
        && !Comm_All(executor))
    {
        raw_notify(executor, NOPERM_MESSAGE);
        return;
    }
    dbref thing;

    switch (key)
    {
    case CSET_PUBLIC:
        ch->type |= CHANNEL_PUBLIC;
        msg = tprintf(T("@cset: Channel %s placed on the public listings."), chan);
        break;

    case CSET_PRIVATE:
        ch->type &= ~CHANNEL_PUBLIC;
        msg = tprintf(T("@cset: Channel %s taken off the public listings."), chan);
        break;

    case CSET_LOUD:
        ch->type |= CHANNEL_LOUD;
        msg = tprintf(T("@cset: Channel %s now sends connect/disconnect msgs."), chan);
        break;

    case CSET_QUIET:
        ch->type &= ~CHANNEL_LOUD;
        msg = tprintf(T("@cset: Channel %s connect/disconnect msgs muted."), chan);
        break;

    case CSET_SPOOF:
        ch->type |= CHANNEL_SPOOF;
        msg = tprintf(T("@cset: Channel %s set spoofable."), chan);
        break;

    case CSET_NOSPOOF:
        ch->type &= ~CHANNEL_SPOOF;
        msg = tprintf(T("@cset: Channel %s set unspoofable."), chan);
        break;

    case CSET_OBJECT:
        init_match(executor, value, NOTYPE);
        match_everything(0);
        thing = match_result();

        if (thing == NOTHING)
        {
            ch->chan_obj = thing;
            msg = tprintf(T("Channel %s is now disassociated from any channel object."), ch->name);
        }
        else if (Good_obj(thing))
        {
            ch->chan_obj = thing;
            UTF8* buff = unparse_object(executor, thing, false);
            msg = tprintf(T("Channel %s is now using %s as channel object."), ch->name, buff);
            free_lbuf(buff);
        }
        else
        {
            msg = tprintf(T("%d is not a valid channel object."), thing);
        }
        break;

    case CSET_HEADER:
        do_cheader(executor, chan, value);
        msg = T("Set.");
        break;

    case CSET_LOG:
        if (do_chanlog(executor, chan, value))
        {
            msg = tprintf(T("@cset: Channel %s maximum history set."), chan);
        }
        else
        {
            msg = tprintf(T("@cset: Maximum history must be a number less than or equal to %d."), MAX_RECALL_REQUEST);
        }
        break;

    case CSET_LOG_TIME:
        if (do_chanlog_timestamps(executor, chan, value))
        {
            msg = tprintf(T("@cset: Channel %s timestamp logging set."), chan);
        }
        else
        {
            msg = tprintf(T("@cset: Failed.  Is logging enabled for %s?"), chan);
        }
    }
    sqlite_wt_channel(ch);
    raw_notify(executor, msg);
}

void do_chboot
(
    const dbref executor,
    const dbref caller,
    const dbref enactor,
    const int eval,
    int key,
    const int nargs,
    UTF8* channel,
    UTF8* victim,
    const UTF8* cargs[],
    int ncargs
)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (nullptr != mudstate.pIComsysControl)
    {
        mudstate.pIComsysControl->CBoot(executor, channel, victim, key);
        return;
    }

    // I sure hope it's not going to be that long.
    //
    struct channel* ch = select_channel(channel);
    if (!ch)
    {
        raw_notify(executor, T("@cboot: Unknown channel."));
        return;
    }
    struct comuser* user = select_user(ch, executor);
    if (!user)
    {
        raw_notify(executor, T("@cboot: You are not on that channel."));
        return;
    }
    if (!Controls(executor, ch->charge_who)
        && !Comm_All(executor))
    {
        raw_notify(executor, T("@cboot: You can\xE2\x80\x99t do that!"));
        return;
    }
    const dbref thing = match_thing(executor, victim);

    if (!Good_obj(thing))
    {
        return;
    }
    struct comuser* vu = select_user(ch, thing);
    if (!vu)
    {
        raw_notify(executor, tprintf(T("@cboot: %s is not on the channel."),
                                     Moniker(thing)));
        return;
    }

    raw_notify(executor, tprintf(T("You boot %s off channel %s."),
                                 Moniker(thing), ch->name));
    raw_notify(thing, tprintf(T("%s boots you off channel %s."),
                              Moniker(thing), ch->name));

    if (!(key & CBOOT_QUIET))
    {
        UTF8 *mess1, *mess1nct;
        UTF8 *mess2, *mess2nct;
        BuildChannelMessage((ch->type & CHANNEL_SPOOF) != 0, ch->header, user,
                            ch->chan_obj, T(":boots"), &mess1, &mess1nct);
        BuildChannelMessage((ch->type & CHANNEL_SPOOF) != 0, nullptr, vu,
                            ch->chan_obj, T(":off the channel."), &mess2,
                            &mess2nct);
        UTF8* messNormal = alloc_lbuf("do_chboot.messnormal");
        UTF8* messNoComtitle = alloc_lbuf("do_chboot.messnocomtitle");
        UTF8* mnp = messNormal;
        UTF8* mnctp = messNoComtitle;
        if (mess1)
        {
            safe_str(mess1, messNormal, &mnp);
            free_lbuf(mess1);
        }
        if (mess2)
        {
            safe_str(mess2, messNormal, &mnp);
            free_lbuf(mess2);
        }
        *mnp = '\0';
        if (mess1nct)
        {
            safe_str(mess1nct, messNoComtitle, &mnctp);
            free_lbuf(mess1nct);
        }
        if (mess2nct)
        {
            safe_str(mess2nct, messNoComtitle, &mnctp);
            free_lbuf(mess2nct);
        }
        *mnctp = '\0';
        SendChannelMessage(executor, ch, messNormal, messNoComtitle);
        do_delcomchannel(thing, channel, false);
    }
    else
    {
        do_delcomchannel(thing, channel, true);
    }
}

// Process a channel header set request.
//
void do_cheader(const dbref player, UTF8* channel, const UTF8* header)
{
    struct channel* ch = select_channel(channel);
    if (!ch)
    {
        raw_notify(player, T("That channel does not exist."));
        return;
    }
    if (!Controls(player, ch->charge_who)
        && !Comm_All(player))
    {
        raw_notify(player, NOPERM_MESSAGE);
        return;
    }

    if ('\0' == header[0])
    {
        // Empty value resets to the default bold [ChannelName] header.
        //
        mux_sprintf(ch->header, sizeof(ch->header),
                    T("%s[%s]%s"), COLOR_INTENSE, ch->name, COLOR_RESET);
    }
    else
    {
        // Optimize/terminate any ANSI in the string.
        //
        UTF8 NewHeader_ANSI[MAX_HEADER_LEN + 1];
        const mux_field nLen = StripTabsAndTruncate(header, NewHeader_ANSI,
                                                    MAX_HEADER_LEN, MAX_HEADER_LEN);
        memcpy(ch->header, NewHeader_ANSI, nLen.m_byte + 1);
    }
    sqlite_wt_channel(ch);
}

struct chanlist_node
{
    UTF8* name;
    struct channel* ptr;
};

static int DCL_CDECL chanlist_comp(const void* a, const void* b)
{
    const auto ca = reinterpret_cast<const chanlist_node *>(a);
    const auto cb = reinterpret_cast<const chanlist_node *>(b);
    return mux_stricmp(ca->name, cb->name);
}

void do_chanlist
(
    const dbref executor,
    const dbref caller,
    const dbref enactor,
    const int eval,
    int key,
    UTF8* pattern,
    const UTF8* cargs[],
    const int ncargs
)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (nullptr != mudstate.pIComsysControl)
    {
        mudstate.pIComsysControl->ChanList(executor, pattern, key);
        return;
    }

    if (key & CLIST_FULL)
    {
        do_listchannels(executor, pattern);
        return;
    }

    dbref owner;
    int flags = 0;

    if (key & CLIST_HEADERS)
    {
        raw_notify(executor, T("*** Channel       Owner           Header"));
    }
    else
    {
        raw_notify(executor, T("*** Channel       Owner           Description"));
    }

    bool bWild;
    if (nullptr != pattern
        && '\0' != *pattern)
    {
        bWild = true;
    }
    else
    {
        bWild = false;
    }

#define MAX_SUPPORTED_NUM_ENTRIES 10000

    auto entries = mudstate.channel_names.size();
    if (MAX_SUPPORTED_NUM_ENTRIES < entries)
    {
        // Nobody should have so many channels.
        //
        entries = MAX_SUPPORTED_NUM_ENTRIES;
    }

    if (0 < entries)
    {
        for (auto it = mudstate.channel_names.begin(); it != mudstate.channel_names.end(); ++it)
        {
            const auto ch = it->second;

            if (!bWild != quick_wild(pattern, ch->name))
            {
                if (Comm_All(executor)
                    || (ch->type & CHANNEL_PUBLIC)
                    || Controls(executor, ch->charge_who)
                    || nullptr != select_user(ch, executor))
                {
                    const UTF8* pBuffer = nullptr;
                    UTF8* atrstr = nullptr;

                    if (key & CLIST_HEADERS)
                    {
                        pBuffer = ch->header;
                    }
                    else
                    {
                        if (NOTHING != ch->chan_obj)
                        {
                            atrstr = atr_pget(ch->chan_obj, A_DESC, &owner, &flags);
                        }

                        if (nullptr != atrstr && '\0' != atrstr[0])
                        {
                            pBuffer = atrstr;
                        }
                        else
                        {
                            pBuffer = T("No description.");
                        }
                    }

                    UTF8* temp = alloc_mbuf("do_chanlist_temp");
                    mux_sprintf(temp, MBUF_SIZE, T("%c%c%c "),
                        (ch->type & (CHANNEL_PUBLIC)) ? 'P' : '-',
                        (ch->type & (CHANNEL_LOUD)) ? 'L' : '-',
                        (ch->type & (CHANNEL_SPOOF)) ? 'S' : '-');
                    mux_field iPos(4, 4);

                    iPos += StripTabsAndTruncate(ch->name,
                        temp + iPos.m_byte,
                        (MBUF_SIZE - 1) - iPos.m_byte,
                        13);
                    iPos = PadField(temp, MBUF_SIZE - 1, 18, iPos);
                    iPos += StripTabsAndTruncate(Moniker(ch->charge_who),
                        temp + iPos.m_byte,
                        (MBUF_SIZE - 1) - iPos.m_byte,
                        15);
                    iPos = PadField(temp, MBUF_SIZE - 1, 34, iPos);
                    iPos += StripTabsAndTruncate(pBuffer,
                        temp + iPos.m_byte,
                        (MBUF_SIZE - 1) - iPos.m_byte,
                        45);
                    iPos = PadField(temp, MBUF_SIZE - 1, 79, iPos);

                    raw_notify(executor, temp);
                    free_mbuf(temp);

                    if (nullptr != atrstr)
                    {
                        free_lbuf(atrstr);
                    }
                }
            }
        }
    }
    raw_notify(executor, T("-- End of list of Channels --"));
}

// Returns a player's comtitle for a named channel.
//
FUNCTION(fun_comtitle)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref victim = lookup_player(executor, fargs[0], true);
    if (!Good_obj(victim))
    {
        init_match(executor, fargs[0], TYPE_THING);
        match_everything(0);
        victim = match_result();
        if (!Good_obj(victim))
        {
            safe_str(T("#-1 OBJECT DOES NOT EXIST"), buff, bufc);
            return;
        }
    }

    struct channel* chn = select_channel(fargs[1]);
    if (!chn)
    {
        safe_str(T("#-1 CHANNEL DOES NOT EXIST"), buff, bufc);
        return;
    }

    comsys_t* c = get_comsys(executor);
    struct comuser* user;

    bool onchannel = false;
    if (Wizard(executor))
    {
        onchannel = true;
    }
    else
    {
        user = select_user(chn, executor);
        if (user)
        {
            onchannel = true;
        }
    }

    if (!onchannel)
    {
        safe_noperm(buff, bufc);
        return;
    }

    user = select_user(chn, victim);
    if (user)
    {
        safe_str(reinterpret_cast<const UTF8 *>(user->title.c_str()), buff, bufc);
        return;
    }
    safe_str(T("#-1 OBJECT NOT ON THAT CHANNEL"), buff, bufc);
}

// Returns a player's comsys alias for a named channel.
//
FUNCTION(fun_comalias)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref victim = lookup_player(executor, fargs[0], true);
    if (!Good_obj(victim))
    {
        init_match(executor, fargs[0], TYPE_THING);
        match_everything(0);
        victim = match_result();
        if (!Good_obj(victim))
        {
            safe_str(T("#-1 OBJECT DOES NOT EXIST"), buff, bufc);
            return;
        }
    }

    struct channel* chn = select_channel(fargs[1]);
    if (!chn)
    {
        safe_str(T("#-1 CHANNEL DOES NOT EXIST"), buff, bufc);
        return;
    }

    // Wizards can get the comalias for anyone. Players and objects can check
    // for themselves. Objects that Inherit can check for their owners.
    //
    if (!Wizard(executor)
        && executor != victim
        && (Owner(executor) != victim
            || !Inherits(executor)))
    {
        safe_noperm(buff, bufc);
        return;
    }

    auto it = comsys_table.find(victim);
    if (it != comsys_table.end())
    {
        const comsys_t &cc = it->second;
        for (const auto &ca : cc.aliases)
        {
            if (ca.channel == reinterpret_cast<const char *>(fargs[1]))
            {
                safe_str(reinterpret_cast<const UTF8 *>(ca.alias.c_str()), buff, bufc);
                return;
            }
        }
    }
    safe_str(T("#-1 OBJECT NOT ON THAT CHANNEL"), buff, bufc);
}

// Returns a list of channels.
//
FUNCTION(fun_channels)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);

    SEP sep;
    if (!OPTIONAL_DELIM(2, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    dbref who = NOTHING;
    if (nfargs >= 1 && fargs[0][0] != '\0')
    {
        who = lookup_player(executor, fargs[0], true);
        if (who == NOTHING
            && mux_stricmp(fargs[0], T("all")) != 0)
        {
            safe_str(T("#-1 PLAYER NOT FOUND"), buff, bufc);
            return;
        }
    }

    int pg_offset = 0;
    int pg_limit  = 0;
    if (nfargs >= 3) pg_offset = mux_atol(fargs[2]);
    if (nfargs >= 4) pg_limit  = mux_atol(fargs[3]);
    if (pg_offset < 0) pg_offset = 0;
    if (pg_limit < 0)  pg_limit = 0;

    int pos = 0;
    int count = 0;
    bool bFirst = true;
    for (auto it = mudstate.channel_names.begin(); it != mudstate.channel_names.end(); ++it)
    {
        const auto ch = it->second;

        if (  (Comm_All(executor)
                || (ch->type & CHANNEL_PUBLIC)
                || Controls(executor, ch->charge_who)
                || nullptr != select_user(ch, executor))
            && (who == NOTHING
                || Controls(who, ch->charge_who)))
        {
            if (pos < pg_offset)
            {
                pos++;
                continue;
            }
            if (pg_limit > 0 && count >= pg_limit)
                break;

            if (!bFirst)
            {
                print_sep(sep, buff, bufc);
            }
            safe_str(ch->name, buff, bufc);
            bFirst = false;
            count++;
            pos++;
        }
    }
}

FUNCTION(fun_chanobj)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    struct channel* ch = select_channel(fargs[0]);
    if (nullptr == ch)
    {
        safe_str(T("#-1 CHANNEL NOT FOUND"), buff, bufc);
        return;
    }

    const dbref obj = ch->chan_obj;
    if (Good_obj(obj))
    {
        safe_str(tprintf(T("#%d"), obj), buff, bufc);
    }
    else
    {
        safe_str(T("#-1"), buff, bufc);
    }
}

// ---------------------------------------------------------------------------
// Channel query functions — PennMUSH-compatible.
// ---------------------------------------------------------------------------

FUNCTION(fun_cowner)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    struct channel *ch = select_channel(fargs[0]);
    if (nullptr == ch)
    {
        safe_str(T("#-1 CHANNEL NOT FOUND"), buff, bufc);
        return;
    }

    // Visibility check.
    //
    if (  !(ch->type & CHANNEL_PUBLIC)
       && !Comm_All(executor)
       && !Controls(executor, ch->charge_who))
    {
        safe_str(T("#-1 CHANNEL NOT FOUND"), buff, bufc);
        return;
    }

    safe_tprintf_str(buff, bufc, T("#%d"), ch->charge_who);
}

// cmogrifier(<channel>) — Returns the dbref of the channel object (which
// is also the mogrifier object in TinyMUX).  Returns #-1 if no channel
// object is set.  PennMUSH compatibility.
//
FUNCTION(fun_cmogrifier)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    struct channel *ch = select_channel(fargs[0]);
    if (nullptr == ch)
    {
        safe_str(T("#-1 CHANNEL NOT FOUND"), buff, bufc);
        return;
    }

    if (  !(ch->type & CHANNEL_PUBLIC)
       && !Comm_All(executor)
       && !Controls(executor, ch->charge_who))
    {
        safe_str(T("#-1 CHANNEL NOT FOUND"), buff, bufc);
        return;
    }

    if (Good_obj(ch->chan_obj))
    {
        safe_tprintf_str(buff, bufc, T("#%d"), ch->chan_obj);
    }
    else
    {
        safe_str(T("#-1"), buff, bufc);
    }
}

FUNCTION(fun_cusers)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    struct channel *ch = select_channel(fargs[0]);
    if (nullptr == ch)
    {
        safe_str(T("#-1 CHANNEL NOT FOUND"), buff, bufc);
        return;
    }
    if (  !(ch->type & CHANNEL_PUBLIC)
       && !Comm_All(executor)
       && !Controls(executor, ch->charge_who))
    {
        safe_str(T("#-1 CHANNEL NOT FOUND"), buff, bufc);
        return;
    }

    safe_str(mux_ltoa_t(static_cast<int>(ch->users.size())), buff, bufc);
}

FUNCTION(fun_cmsgs)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    struct channel *ch = select_channel(fargs[0]);
    if (nullptr == ch)
    {
        safe_str(T("#-1 CHANNEL NOT FOUND"), buff, bufc);
        return;
    }
    if (  !(ch->type & CHANNEL_PUBLIC)
       && !Comm_All(executor)
       && !Controls(executor, ch->charge_who))
    {
        safe_str(T("#-1 CHANNEL NOT FOUND"), buff, bufc);
        return;
    }

    safe_str(mux_ltoa_t(ch->num_messages), buff, bufc);
}

FUNCTION(fun_cbuffer)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    struct channel *ch = select_channel(fargs[0]);
    if (nullptr == ch)
    {
        safe_str(T("#-1 CHANNEL NOT FOUND"), buff, bufc);
        return;
    }
    if (  !(ch->type & CHANNEL_PUBLIC)
       && !Comm_All(executor)
       && !Controls(executor, ch->charge_who))
    {
        safe_str(T("#-1 CHANNEL NOT FOUND"), buff, bufc);
        return;
    }

    // Buffer size is the MAX_LOG attribute on the channel object.
    //
    int logmax = 0;
    if (Good_obj(ch->chan_obj))
    {
        ATTR *pattr = atr_str(T("MAX_LOG"));
        if (pattr && pattr->number)
        {
            dbref aowner;
            int aflags;
            UTF8 *maxbuf = atr_get("fun_cbuffer", ch->chan_obj,
                pattr->number, &aowner, &aflags);
            logmax = mux_atol(maxbuf);
            free_lbuf(maxbuf);
        }
    }
    safe_str(mux_ltoa_t(logmax), buff, bufc);
}

FUNCTION(fun_cdesc)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    struct channel *ch = select_channel(fargs[0]);
    if (nullptr == ch)
    {
        safe_str(T("#-1 CHANNEL NOT FOUND"), buff, bufc);
        return;
    }
    if (  !(ch->type & CHANNEL_PUBLIC)
       && !Comm_All(executor)
       && !Controls(executor, ch->charge_who))
    {
        safe_str(T("#-1 CHANNEL NOT FOUND"), buff, bufc);
        return;
    }

    // Description is the DESC attribute on the channel object.
    //
    if (Good_obj(ch->chan_obj))
    {
        dbref aowner;
        int aflags;
        UTF8 *desc = atr_pget(ch->chan_obj, A_DESC, &aowner, &aflags);
        if ('\0' != desc[0])
        {
            safe_str(desc, buff, bufc);
        }
        free_lbuf(desc);
    }
}

FUNCTION(fun_cflags)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    struct channel *ch = select_channel(fargs[0]);
    if (nullptr == ch)
    {
        safe_str(T("#-1 CHANNEL NOT FOUND"), buff, bufc);
        return;
    }
    if (  !(ch->type & CHANNEL_PUBLIC)
       && !Comm_All(executor)
       && !Controls(executor, ch->charge_who))
    {
        safe_str(T("#-1 CHANNEL NOT FOUND"), buff, bufc);
        return;
    }

    if (nfargs >= 2)
    {
        // cflags(channel, player) — per-user flags.
        //
        dbref target = match_thing(executor, fargs[1]);
        if (!Good_obj(target))
        {
            safe_str(T("#-1 NO MATCH"), buff, bufc);
            return;
        }
        struct comuser *user = select_user(ch, target);
        if (nullptr == user)
        {
            safe_str(T("#-1 NOT ON CHANNEL"), buff, bufc);
            return;
        }
        // Return user flags: on/off, gag, comtitle status.
        //
        if (!user->bUserIsOn)
        {
            safe_chr('O', buff, bufc);
        }
        if (user->bGagJoinLeave)
        {
            safe_chr('G', buff, bufc);
        }
        if (!user->ComTitleStatus)
        {
            safe_chr('Q', buff, bufc);
        }
    }
    else
    {
        // cflags(channel) — channel flags.
        //
        if (ch->type & CHANNEL_PUBLIC)    safe_chr('P', buff, bufc);
        if (ch->type & CHANNEL_LOUD)     safe_chr('L', buff, bufc);
        if (ch->type & CHANNEL_SPOOF)    safe_chr('S', buff, bufc);
    }
}

FUNCTION(fun_cstatus)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    struct channel *ch = select_channel(fargs[0]);
    if (nullptr == ch)
    {
        safe_str(T("#-1 CHANNEL NOT FOUND"), buff, bufc);
        return;
    }
    if (  !(ch->type & CHANNEL_PUBLIC)
       && !Comm_All(executor)
       && !Controls(executor, ch->charge_who))
    {
        safe_str(T("#-1 CHANNEL NOT FOUND"), buff, bufc);
        return;
    }

    dbref target = match_thing(executor, fargs[1]);
    if (!Good_obj(target))
    {
        safe_str(T("#-1 NO MATCH"), buff, bufc);
        return;
    }

    struct comuser *user = select_user(ch, target);
    if (nullptr == user)
    {
        safe_str(T("Off"), buff, bufc);
        return;
    }

    if (!user->bUserIsOn)
    {
        safe_str(T("Off"), buff, bufc);
    }
    else if (user->bGagJoinLeave)
    {
        safe_str(T("Gag"), buff, bufc);
    }
    else
    {
        safe_str(T("On"), buff, bufc);
    }
}

FUNCTION(fun_crecall)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    struct channel *ch = select_channel(fargs[0]);
    if (nullptr == ch)
    {
        safe_str(T("#-1 CHANNEL NOT FOUND"), buff, bufc);
        return;
    }
    if (  !(ch->type & CHANNEL_PUBLIC)
       && !Comm_All(executor)
       && !Controls(executor, ch->charge_who))
    {
        safe_str(T("#-1 CHANNEL NOT FOUND"), buff, bufc);
        return;
    }

    // Must be on the channel.
    //
    struct comuser *user = select_user(ch, executor);
    if (nullptr == user || !user->bUserIsOn)
    {
        safe_str(T("#-1 NOT ON CHANNEL"), buff, bufc);
        return;
    }

    // Get buffer depth from channel object.
    //
    if (!Good_obj(ch->chan_obj))
    {
        return;
    }

    ATTR *pattr = atr_str(T("MAX_LOG"));
    int logmax = 0;
    if (pattr && pattr->number)
    {
        dbref aowner;
        int aflags;
        UTF8 *maxbuf = atr_get("fun_crecall", ch->chan_obj,
            pattr->number, &aowner, &aflags);
        logmax = mux_atol(maxbuf);
        free_lbuf(maxbuf);
    }
    if (logmax < 1)
    {
        return;
    }

    // Number of lines to recall (default 1, max logmax).
    //
    int nLines = 1;
    if (nfargs >= 2 && fargs[1][0] != '\0')
    {
        nLines = mux_atol(fargs[1]);
        if (nLines < 1) nLines = 1;
        if (nLines > logmax) nLines = logmax;
    }

    // Output separator (default newline).
    //
    SEP sep;
    sep.n = 1;
    sep.str[0] = '\r';
    sep.str[1] = '\0';
    if (nfargs >= 3)
    {
        memcpy(sep.str, fargs[2], strlen(reinterpret_cast<char *>(fargs[2])) + 1);
        sep.n = strlen(reinterpret_cast<char *>(fargs[2]));
    }

    int histnum = ch->num_messages - nLines;
    bool bFirst = true;
    for (int count = 0; count < nLines; count++)
    {
        histnum++;
        const UTF8 *attrname = tprintf(T("HISTORY_%d"),
            ((histnum % logmax) + logmax) % logmax);
        int atr = mkattr(GOD, attrname);
        if (0 < atr)
        {
            dbref aowner;
            int aflags;
            UTF8 *msg = atr_get("fun_crecall", ch->chan_obj,
                atr, &aowner, &aflags);
            if ('\0' != msg[0])
            {
                if (!bFirst)
                {
                    safe_copy_buf(sep.str, sep.n, buff, bufc);
                }
                safe_str(msg, buff, bufc);
                bFirst = false;
            }
            free_lbuf(msg);
        }
    }
}

// ---------------------------------------------------------------------------
// chaninfo(channel, field) — generic channel metadata accessor.
// ---------------------------------------------------------------------------
//
// Visibility: PUBLIC, or subscriber, or Comm_All, or Controls(charge_who).
// The "object" field additionally requires Wizard.
//
FUNCTION(fun_chaninfo)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    struct channel *ch = select_channel(fargs[0]);
    if (nullptr == ch)
    {
        safe_str(T("#-1 CHANNEL NOT FOUND"), buff, bufc);
        return;
    }

    if (  !(ch->type & CHANNEL_PUBLIC)
       && !Comm_All(executor)
       && !Controls(executor, ch->charge_who)
       && nullptr == select_user(ch, executor))
    {
        safe_str(T("#-1 CHANNEL NOT FOUND"), buff, bufc);
        return;
    }

    const UTF8 *field = fargs[1];

    if (0 == mux_stricmp(field, T("name")))
    {
        safe_str(ch->name, buff, bufc);
    }
    else if (0 == mux_stricmp(field, T("header")))
    {
        safe_str(ch->header, buff, bufc);
    }
    else if (0 == mux_stricmp(field, T("owner")))
    {
        safe_tprintf_str(buff, bufc, T("#%d"), ch->charge_who);
    }
    else if (0 == mux_stricmp(field, T("object")))
    {
        if (!Wizard(executor))
        {
            safe_noperm(buff, bufc);
            return;
        }
        if (Good_obj(ch->chan_obj))
        {
            safe_tprintf_str(buff, bufc, T("#%d"), ch->chan_obj);
        }
        else
        {
            safe_str(T("#-1"), buff, bufc);
        }
    }
    else if (0 == mux_stricmp(field, T("type")))
    {
        safe_str(mux_ltoa_t(ch->type), buff, bufc);
    }
    else if (0 == mux_stricmp(field, T("flags")))
    {
        if (ch->type & CHANNEL_PUBLIC)  safe_chr('P', buff, bufc);
        if (ch->type & CHANNEL_LOUD)    safe_chr('L', buff, bufc);
        if (ch->type & CHANNEL_SPOOF)   safe_chr('S', buff, bufc);
    }
    else if (0 == mux_stricmp(field, T("charge")))
    {
        safe_str(mux_ltoa_t(ch->charge), buff, bufc);
    }
    else if (0 == mux_stricmp(field, T("users")))
    {
        safe_str(mux_ltoa_t(static_cast<int>(ch->users.size())), buff, bufc);
    }
    else if (0 == mux_stricmp(field, T("msgs")))
    {
        safe_str(mux_ltoa_t(ch->num_messages), buff, bufc);
    }
    else if (0 == mux_stricmp(field, T("desc")))
    {
        if (Good_obj(ch->chan_obj))
        {
            dbref aowner;
            int aflags;
            UTF8 *desc = atr_pget(ch->chan_obj, A_DESC, &aowner, &aflags);
            if ('\0' != desc[0])
            {
                safe_str(desc, buff, bufc);
            }
            free_lbuf(desc);
        }
    }
    else if (0 == mux_stricmp(field, T("buffer")))
    {
        int logmax = 0;
        if (Good_obj(ch->chan_obj))
        {
            ATTR *pattr = atr_str(T("MAX_LOG"));
            if (pattr && pattr->number)
            {
                dbref aowner;
                int aflags;
                UTF8 *maxbuf = atr_get("fun_chaninfo", ch->chan_obj,
                    pattr->number, &aowner, &aflags);
                logmax = mux_atol(maxbuf);
                free_lbuf(maxbuf);
            }
        }
        safe_str(mux_ltoa_t(logmax), buff, bufc);
    }
    else if (0 == mux_stricmp(field, T("canjoin")))
    {
        safe_chr(test_join_access(executor, ch) ? '1' : '0', buff, bufc);
    }
    else if (0 == mux_stricmp(field, T("cantransmit")))
    {
        safe_chr(test_transmit_access(executor, ch) ? '1' : '0', buff, bufc);
    }
    else if (0 == mux_stricmp(field, T("canreceive")))
    {
        safe_chr(test_receive_access(executor, ch) ? '1' : '0', buff, bufc);
    }
    else
    {
        safe_str(T("#-1 INVALID FIELD"), buff, bufc);
    }
}

// ---------------------------------------------------------------------------
// chanfind(header) — reverse-lookup channel name from header string.
// ---------------------------------------------------------------------------
//
// Given a channel header (display name), returns the canonical channel name.
// Useful when players see e.g. "<PublicServices>" in chat and need to find
// the internal name "PubServ".  Case-insensitive match.
//
// Visibility: same subscriber-aware model as chaninfo.
//
FUNCTION(fun_chanfind)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    const UTF8 *target = fargs[0];

    for (auto it = mudstate.channel_names.begin();
         it != mudstate.channel_names.end(); ++it)
    {
        const auto ch = it->second;

        if (0 == mux_stricmp(target, ch->header))
        {
            // Visibility check.
            //
            if (  (ch->type & CHANNEL_PUBLIC)
               || Comm_All(executor)
               || Controls(executor, ch->charge_who)
               || nullptr != select_user(ch, executor))
            {
                safe_str(ch->name, buff, bufc);
                return;
            }
        }
    }
    safe_str(T("#-1 CHANNEL NOT FOUND"), buff, bufc);
}

// ---------------------------------------------------------------------------
// chanusers(channel[, separator[, field]]) — list subscriber data.
// ---------------------------------------------------------------------------
//
// With no field arg (or empty), returns delimited dbrefs.
// With a field arg, returns that field for each subscriber:
//   title, status, flags, gagjoin, comtitles, alias, name.
//
// "alias" requires Wizard (exposes other players' aliases).
// Other fields follow the co-member rule (executor must be visible).
//
// Visibility: PUBLIC, or subscriber, or Comm_All, or Controls(charge_who).
//
FUNCTION(fun_chanusers)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);

    SEP sep;
    if (!OPTIONAL_DELIM(2, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    struct channel *ch = select_channel(fargs[0]);
    if (nullptr == ch)
    {
        safe_str(T("#-1 CHANNEL NOT FOUND"), buff, bufc);
        return;
    }

    if (  !(ch->type & CHANNEL_PUBLIC)
       && !Comm_All(executor)
       && !Controls(executor, ch->charge_who)
       && nullptr == select_user(ch, executor))
    {
        safe_str(T("#-1 CHANNEL NOT FOUND"), buff, bufc);
        return;
    }

    // Determine output field (default: dbref).
    //
    enum FieldType { FT_DBREF, FT_TITLE, FT_STATUS, FT_FLAGS,
                     FT_GAGJOIN, FT_COMTITLES, FT_ALIAS, FT_NAME };
    FieldType ft = FT_DBREF;

    if (nfargs >= 3 && fargs[2][0] != '\0')
    {
        const UTF8 *field = fargs[2];
        if (0 == mux_stricmp(field, T("title")))
        {
            ft = FT_TITLE;
        }
        else if (0 == mux_stricmp(field, T("status")))
        {
            ft = FT_STATUS;
        }
        else if (0 == mux_stricmp(field, T("flags")))
        {
            ft = FT_FLAGS;
        }
        else if (0 == mux_stricmp(field, T("gagjoin")))
        {
            ft = FT_GAGJOIN;
        }
        else if (0 == mux_stricmp(field, T("comtitles")))
        {
            ft = FT_COMTITLES;
        }
        else if (0 == mux_stricmp(field, T("alias")))
        {
            if (!Wizard(executor))
            {
                safe_noperm(buff, bufc);
                return;
            }
            ft = FT_ALIAS;
        }
        else if (0 == mux_stricmp(field, T("name")))
        {
            ft = FT_NAME;
        }
        else
        {
            safe_str(T("#-1 INVALID FIELD"), buff, bufc);
            return;
        }
    }

    int pg_offset = 0;
    int pg_limit  = 0;
    if (nfargs >= 4) pg_offset = mux_atol(fargs[3]);
    if (nfargs >= 5) pg_limit  = mux_atol(fargs[4]);
    if (pg_offset < 0) pg_offset = 0;
    if (pg_limit < 0)  pg_limit = 0;

    int pos = 0;
    int count = 0;
    bool bFirst = true;
    for (const auto &kv : ch->users)
    {
        if (pos < pg_offset)
        {
            pos++;
            continue;
        }
        if (pg_limit > 0 && count >= pg_limit)
            break;

        if (!bFirst)
        {
            print_sep(sep, buff, bufc);
        }
        bFirst = false;

        const comuser &user = kv.second;

        switch (ft)
        {
        case FT_DBREF:
            safe_tprintf_str(buff, bufc, T("#%d"), kv.first);
            break;

        case FT_NAME:
            safe_str(Moniker(kv.first), buff, bufc);
            break;

        case FT_TITLE:
            safe_str(reinterpret_cast<const UTF8 *>(user.title.c_str()),
                buff, bufc);
            break;

        case FT_STATUS:
            safe_str(user.bUserIsOn ? T("On") : T("Off"), buff, bufc);
            break;

        case FT_FLAGS:
            if (!user.bUserIsOn)     safe_chr('O', buff, bufc);
            if (user.bGagJoinLeave)  safe_chr('G', buff, bufc);
            if (!user.ComTitleStatus) safe_chr('Q', buff, bufc);
            break;

        case FT_GAGJOIN:
            safe_chr(user.bGagJoinLeave ? '1' : '0', buff, bufc);
            break;

        case FT_COMTITLES:
            safe_chr(user.ComTitleStatus ? '1' : '0', buff, bufc);
            break;

        case FT_ALIAS:
            {
                auto it = comsys_table.find(kv.first);
                if (it != comsys_table.end())
                {
                    const comsys_t &cc = it->second;
                    for (const auto &ca : cc.aliases)
                    {
                        if (0 == mux_stricmp(
                            reinterpret_cast<const UTF8 *>(ca.channel.c_str()),
                            ch->name))
                        {
                            safe_str(reinterpret_cast<const UTF8 *>(
                                ca.alias.c_str()), buff, bufc);
                            break;
                        }
                    }
                }
            }
            break;
        }
        count++;
        pos++;
    }
}

// ---------------------------------------------------------------------------
// chanuser(channel, player, field) — per-user channel data.
// ---------------------------------------------------------------------------
//
// Per-field permissions:
//   alias:    self, or Owner(executor)==victim && Inherits(executor), or Wizard
//   title, status, flags, gagjoin, comtitles:
//             self, or both executor and victim on channel, or Wizard
//
FUNCTION(fun_chanuser)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    struct channel *ch = select_channel(fargs[0]);
    if (nullptr == ch)
    {
        safe_str(T("#-1 CHANNEL NOT FOUND"), buff, bufc);
        return;
    }

    // Channel visibility: subscriber-aware.
    //
    if (  !(ch->type & CHANNEL_PUBLIC)
       && !Comm_All(executor)
       && !Controls(executor, ch->charge_who)
       && nullptr == select_user(ch, executor))
    {
        safe_str(T("#-1 CHANNEL NOT FOUND"), buff, bufc);
        return;
    }

    // Resolve target player/object.
    //
    dbref victim = lookup_player(executor, fargs[1], true);
    if (!Good_obj(victim))
    {
        init_match(executor, fargs[1], TYPE_THING);
        match_everything(0);
        victim = match_result();
        if (!Good_obj(victim))
        {
            safe_str(T("#-1 PLAYER NOT FOUND"), buff, bufc);
            return;
        }
    }

    const UTF8 *field = fargs[2];

    // The "alias" field has its own permission rule.
    //
    if (0 == mux_stricmp(field, T("alias")))
    {
        if (  !Wizard(executor)
           && executor != victim
           && (Owner(executor) != victim
               || !Inherits(executor)))
        {
            safe_noperm(buff, bufc);
            return;
        }

        auto it = comsys_table.find(victim);
        if (it != comsys_table.end())
        {
            const comsys_t &cc = it->second;
            for (const auto &ca : cc.aliases)
            {
                if (0 == mux_stricmp(
                    reinterpret_cast<const UTF8 *>(ca.channel.c_str()),
                    fargs[0]))
                {
                    safe_str(reinterpret_cast<const UTF8 *>(ca.alias.c_str()),
                        buff, bufc);
                    return;
                }
            }
        }
        safe_str(T("#-1 NOT ON CHANNEL"), buff, bufc);
        return;
    }

    // All other fields: self, or co-member, or Wizard.
    //
    if (  !Wizard(executor)
       && executor != victim)
    {
        struct comuser *executor_user = select_user(ch, executor);
        if (nullptr == executor_user)
        {
            safe_noperm(buff, bufc);
            return;
        }
    }

    // Look up the target's user record on this channel.
    //
    struct comuser *user = select_user(ch, victim);
    if (nullptr == user)
    {
        safe_str(T("#-1 NOT ON CHANNEL"), buff, bufc);
        return;
    }

    if (0 == mux_stricmp(field, T("title")))
    {
        safe_str(reinterpret_cast<const UTF8 *>(user->title.c_str()),
            buff, bufc);
    }
    else if (0 == mux_stricmp(field, T("status")))
    {
        if (user->bUserIsOn)
        {
            safe_str(T("On"), buff, bufc);
        }
        else
        {
            safe_str(T("Off"), buff, bufc);
        }
    }
    else if (0 == mux_stricmp(field, T("flags")))
    {
        if (!user->bUserIsOn)
        {
            safe_chr('O', buff, bufc);
        }
        if (user->bGagJoinLeave)
        {
            safe_chr('G', buff, bufc);
        }
        if (!user->ComTitleStatus)
        {
            safe_chr('Q', buff, bufc);
        }
    }
    else if (0 == mux_stricmp(field, T("gagjoin")))
    {
        safe_chr(user->bGagJoinLeave ? '1' : '0', buff, bufc);
    }
    else if (0 == mux_stricmp(field, T("comtitles")))
    {
        safe_chr(user->ComTitleStatus ? '1' : '0', buff, bufc);
    }
    else
    {
        safe_str(T("#-1 INVALID FIELD"), buff, bufc);
    }
}

// ---------------------------------------------------------------------------
// SQLite comsys bulk sync and load.
// ---------------------------------------------------------------------------

#include "sqlite_backend.h"

static void clear_runtime_comsys_data(void)
{
    for (auto it = mudstate.channel_names.begin(); it != mudstate.channel_names.end(); ++it)
    {
        channel *ch = it->second;
        delete ch;
    }
    mudstate.channel_names.clear();

    comsys_table.clear();
}

bool sqlite_sync_comsys(void)
{
    CSQLiteDB &sqldb = g_pSQLiteBackend->GetDB();
    if (!sqldb.Begin())
    {
        return false;
    }
    if (!sqldb.ClearComsysTables())
    {
        sqldb.Rollback();
        return false;
    }

    // Sync channels and their users.
    //
    for (auto it = mudstate.channel_names.begin(); it != mudstate.channel_names.end(); ++it)
    {
        struct channel *ch = it->second;
        if (!sqldb.SyncChannel(ch->name, ch->header,
            ch->type, ch->temp1, ch->temp2,
            ch->charge, ch->charge_who, ch->amount_col,
            ch->num_messages, ch->chan_obj))
        {
            sqldb.Rollback();
            return false;
        }

        for (auto &kv : ch->users)
        {
            comuser &user = kv.second;
            if (user.who >= 0 && user.who < mudstate.db_top)
            {
                if (!sqldb.SyncChannelUser(ch->name, user.who,
                    user.bUserIsOn, user.ComTitleStatus,
                    user.bGagJoinLeave,
                    reinterpret_cast<const UTF8 *>(user.title.c_str())))
                {
                    sqldb.Rollback();
                    return false;
                }
            }
        }
    }

    // Sync player channel aliases.
    //
    for (auto &kv : comsys_table)
    {
        const comsys_t &c = kv.second;
        for (const auto &ca : c.aliases)
        {
            // Skip orphaned aliases for channels that no longer exist.
            //
            if (nullptr == select_channel(
                const_cast<UTF8 *>(reinterpret_cast<const UTF8 *>(ca.channel.c_str()))))
            {
                continue;
            }
            if (!sqldb.SyncPlayerChannel(c.who,
                reinterpret_cast<const UTF8 *>(ca.alias.c_str()),
                reinterpret_cast<const UTF8 *>(ca.channel.c_str())))
            {
                sqldb.Rollback();
                return false;
            }
        }
    }

    if (!sqldb.PutMeta("has_comsys", 1))
    {
        sqldb.Rollback();
        return false;
    }
    if (!sqldb.Commit())
    {
        sqldb.Rollback();
        return false;
    }
    return true;
}

int sqlite_load_comsys(void)
{
    mudstate.bSQLiteLoading = true;
    CSQLiteDB &sqldb = g_pSQLiteBackend->GetDB();

    int has_comsys = 0;
    CSQLiteDB::MetaGetResult has_comsys_meta = sqldb.GetMetaEx("has_comsys", &has_comsys);
    if (CSQLiteDB::MetaGetResult::Error == has_comsys_meta)
    {
        mudstate.bSQLiteLoading = false;
        return -1;
    }
    if (CSQLiteDB::MetaGetResult::Found != has_comsys_meta || 0 == has_comsys)
    {
        mudstate.bSQLiteLoading = false;
        return 0;
    }

    // Reset in-memory structures before repopulating.
    //
    clear_runtime_comsys_data();

    // Load channels.
    //
    if (!sqldb.LoadAllChannels([](const UTF8 *name, const UTF8 *header,
        int type, int temp1, int temp2, int charge, int charge_who,
        int amount_col, int num_messages, int chan_obj)
    {
        auto ch = new channel();

        mux_strncpy(ch->name, name, MAX_CHANNEL_LEN);
        mux_strncpy(ch->header, header, MAX_HEADER_LEN);
        ch->type = type;
        ch->temp1 = temp1;
        ch->temp2 = temp2;
        ch->charge = charge;
        ch->charge_who = charge_who;
        ch->amount_col = amount_col;
        ch->num_messages = num_messages;
        ch->chan_obj = chan_obj;

        size_t nName = strlen(reinterpret_cast<const char *>(ch->name));
        vector<UTF8> channel_name_vector(ch->name, ch->name + nName);
        mudstate.channel_names.insert(make_pair(channel_name_vector, ch));
    }))
    {
        clear_runtime_comsys_data();
        mudstate.bSQLiteLoading = false;
        return -1;
    }

    // Load channel users.
    //
    if (!sqldb.LoadAllChannelUsers([](const UTF8 *channel_name, int who,
        bool is_on, bool comtitle_status, bool gag_join_leave,
        const UTF8 *title)
    {
        auto ch_name = const_cast<UTF8 *>(channel_name);
        struct channel *ch = select_channel(ch_name);
        if (!ch)
        {
            return;
        }

        if (!Good_dbref(who) || isGarbage(who))
        {
            return;
        }

        comuser cu;
        cu.who = who;
        cu.bUserIsOn = is_on;
        cu.ComTitleStatus = comtitle_status;
        cu.bGagJoinLeave = gag_join_leave;
        cu.title.assign(reinterpret_cast<const char *>(title));

        auto result = ch->users.emplace(who, std::move(cu));
        comuser &user = result.first->second;

        if (!(isPlayer(user.who))
            && !(Going(user.who)
                && (God(Owner(user.who)))))
        {
            do_joinchannel(user.who, ch);
        }
        user.bConnected = true;
    }))
    {
        clear_runtime_comsys_data();
        mudstate.bSQLiteLoading = false;
        return -1;
    }

    // Load player channel aliases.
    //
    // We need to group by player. Use a map to collect aliases per player,
    // then create comsys_t entries.
    //
    struct PlayerAliasEntry
    {
        string alias;
        string channel_name;
    };
    std::map<int, std::vector<PlayerAliasEntry>> player_aliases;

    if (!sqldb.LoadAllPlayerChannels([&player_aliases](int who, const UTF8 *alias,
        const UTF8 *channel_name)
    {
        PlayerAliasEntry entry;
        entry.alias.assign(reinterpret_cast<const char *>(alias));
        entry.channel_name.assign(reinterpret_cast<const char *>(channel_name));
        player_aliases[who].push_back(std::move(entry));
    }))
    {
        player_aliases.clear();
        clear_runtime_comsys_data();
        mudstate.bSQLiteLoading = false;
        return -1;
    }

    for (auto &pa : player_aliases)
    {
        int who = pa.first;
        auto &entries = pa.second;

        if (!Good_obj(who))
        {
            continue;
        }

        comsys_t c;
        c.who = who;

        for (auto &e : entries)
        {
            com_alias ca;
            ca.alias = std::move(e.alias);
            ca.channel = std::move(e.channel_name);
            c.aliases.push_back(std::move(ca));
        }
        sort_com_aliases(c);
        comsys_table[who] = std::move(c);
    }

    mudstate.bSQLiteLoading = false;
    return 1;
}
