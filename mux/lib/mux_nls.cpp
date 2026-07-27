/*! \file mux_nls.cpp
 * \brief Optional gettext domain binding for server messages (#1419).
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"

// Two implementations live below, selected by whether a GNU gettext is
// available:
//
//   HAVE_NLS && HAVE_LIBINTL_H   -> thin wrappers over gettext(3).
//   HAVE_NLS && !HAVE_LIBINTL_H  -> the built-in catalog reader further
//                                   down, for toolchains with no libintl.
//                                   MSVC is the case that motivated it
//                                   (#1419): the Windows SDK ships no
//                                   <libintl.h>, so every M_() string
//                                   stayed untranslated there no matter
//                                   how much of the tree was marked up.
//
#if defined(HAVE_NLS)
#if defined(HAVE_LIBINTL_H)
#include <libintl.h>
#include <locale.h>
#else
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <new>
#endif
#endif

#if defined(HAVE_NLS) && !defined(HAVE_LIBINTL_H)
// ---------------------------------------------------------------------------
// Built-in MO catalog reader (#1419).
//
// Implements just enough of gettext for M_(): one domain, one language, no
// plural forms, no context.  That is the entire surface mux_nls.h exposes,
// which is why this is ~120 lines rather than a port of libintl.
//
// The MO format is fixed and documented: a header, then two parallel
// tables of (length, offset) pairs -- original strings and translations --
// with the originals sorted bytewise by msgid so lookup is a binary
// search.  The hash table in the file is an optimisation we do not need.
//
// Everything here treats the file as untrusted: every offset and length is
// range-checked against the mapped size before use, and a catalog that
// fails any check is rejected whole rather than partially trusted.  A
// rejected or missing catalog leaves s_catalog null, and mux_gettext then
// returns msgids unchanged -- the same observable behaviour as running
// without NLS at all.
//
namespace
{
    constexpr uint32_t MO_MAGIC    = 0x950412deu;
    constexpr uint32_t MO_MAGIC_SW = 0xde120495u;

    unsigned char *s_catalog     = nullptr;   // owned, process lifetime
    size_t         s_catalog_len = 0;
    uint32_t       s_count       = 0;
    uint32_t       s_orig_off    = 0;
    uint32_t       s_trans_off   = 0;
    bool           s_swapped     = false;

    inline uint32_t mo_u32(const unsigned char *p)
    {
        const uint32_t v = static_cast<uint32_t>(p[0])
                         | (static_cast<uint32_t>(p[1]) << 8)
                         | (static_cast<uint32_t>(p[2]) << 16)
                         | (static_cast<uint32_t>(p[3]) << 24);
        if (!s_swapped)
        {
            return v;
        }
        return   ((v & 0x000000ffu) << 24)
               | ((v & 0x0000ff00u) << 8)
               | ((v & 0x00ff0000u) >> 8)
               | ((v & 0xff000000u) >> 24);
    }

    // Read entry i from the table at tbl_off; false if it does not lie
    // wholly inside the file or is not NUL-terminated where we expect.
    //
    bool mo_entry(uint32_t tbl_off, uint32_t i, const char **str)
    {
        const size_t rec = static_cast<size_t>(tbl_off) + 8u * i;
        if (rec + 8u > s_catalog_len)
        {
            return false;
        }
        const uint32_t len = mo_u32(s_catalog + rec);
        const uint32_t off = mo_u32(s_catalog + rec + 4);
        if (  off > s_catalog_len
           || len > s_catalog_len - off
           || '\0' != s_catalog[static_cast<size_t>(off) + len])
        {
            return false;
        }
        *str = reinterpret_cast<const char *>(s_catalog + off);
        return true;
    }

    void mo_unload(void)
    {
        delete [] s_catalog;
        s_catalog = nullptr;
        s_catalog_len = 0;
        s_count = 0;
    }

    bool mo_load(const char *path)
    {
        FILE *fp = fopen(path, "rb");
        if (nullptr == fp)
        {
            return false;
        }

        if (0 != fseek(fp, 0, SEEK_END))
        {
            fclose(fp);
            return false;
        }
        const long size = ftell(fp);
        // 28 bytes is the fixed header; anything smaller cannot describe a
        // catalog.  The upper bound keeps a corrupt or hostile file from
        // asking for an unreasonable allocation.
        //
        if (  size < 28
           || size > 64L * 1024L * 1024L)
        {
            fclose(fp);
            return false;
        }
        rewind(fp);

        unsigned char *buf = new (std::nothrow) unsigned char[
            static_cast<size_t>(size) + 1];
        if (nullptr == buf)
        {
            fclose(fp);
            return false;
        }
        const size_t got = fread(buf, 1, static_cast<size_t>(size), fp);
        fclose(fp);
        if (got != static_cast<size_t>(size))
        {
            delete [] buf;
            return false;
        }
        buf[got] = '\0';

        s_catalog = buf;
        s_catalog_len = got;

        const uint32_t magic = static_cast<uint32_t>(buf[0])
                             | (static_cast<uint32_t>(buf[1]) << 8)
                             | (static_cast<uint32_t>(buf[2]) << 16)
                             | (static_cast<uint32_t>(buf[3]) << 24);
        if (MO_MAGIC == magic)
        {
            s_swapped = false;
        }
        else if (MO_MAGIC_SW == magic)
        {
            s_swapped = true;
        }
        else
        {
            mo_unload();
            return false;
        }

        s_count     = mo_u32(buf + 8);
        s_orig_off  = mo_u32(buf + 12);
        s_trans_off = mo_u32(buf + 16);

        // Both tables must lie wholly inside the file.
        //
        const uint64_t need = 8ull * s_count;
        if (  need > s_catalog_len
           || s_orig_off  > s_catalog_len - need
           || s_trans_off > s_catalog_len - need)
        {
            mo_unload();
            return false;
        }
        return true;
    }

    // LC_ALL, then LC_MESSAGES, then LANG -- the order gettext uses.
    // "xx.UTF-8@mod" narrows to "xx"; the catalogs in game/locale are
    // named by bare language code.
    //
    bool mo_language(char *out, size_t outlen)
    {
        const char *v = nullptr;
        for (const char *name : { "LC_ALL", "LC_MESSAGES", "LANG" })
        {
            const char *e = getenv(name);
            if (  nullptr != e
               && '\0' != e[0])
            {
                v = e;
                break;
            }
        }
        if (  nullptr == v
           || 0 == strcmp(v, "C")
           || 0 == strcmp(v, "POSIX"))
        {
            return false;
        }

        size_t n = 0;
        while (  '\0' != v[n]
              && '.' != v[n]
              && '@' != v[n]
              && n + 1 < outlen)
        {
            out[n] = v[n];
            n++;
        }
        out[n] = '\0';
        return 0 != n;
    }
}

// Used by M_() only. gettext("") is not identity: the empty msgid stores
// the .mo header (Project-Id-Version, …). Guard so a mistaken M_("")
// cannot inject that blob (#1443). T("") remains a pure cast and is
// never routed here.
//
LIBMUX_API const UTF8 *mux_gettext(const UTF8 *msgid)
{
    if (  nullptr == msgid
       || '\0' == msgid[0])
    {
        return msgid;
    }
    if (  nullptr == s_catalog
       || 0 == s_count)
    {
        return msgid;
    }

    const char *key = reinterpret_cast<const char *>(msgid);

    // Originals are sorted bytewise, which is what makes this a search
    // rather than a scan.
    //
    uint32_t lo = 0;
    uint32_t hi = s_count;
    while (lo < hi)
    {
        const uint32_t mid = lo + (hi - lo) / 2;
        const char *cand = nullptr;
        if (!mo_entry(s_orig_off, mid, &cand))
        {
            return msgid;
        }
        const int cmp = strcmp(key, cand);
        if (0 == cmp)
        {
            const char *tr = nullptr;
            if (  !mo_entry(s_trans_off, mid, &tr)
               || '\0' == tr[0])
            {
                // An empty translation means "not translated"; returning it
                // would blank the message.
                //
                return msgid;
            }
            return reinterpret_cast<const UTF8 *>(tr);
        }
        if (cmp < 0)
        {
            hi = mid;
        }
        else
        {
            lo = mid + 1;
        }
    }
    return msgid;
}

#elif defined(HAVE_NLS)
// Used by M_() only. gettext("") is not identity: the empty msgid stores
// the .mo header (Project-Id-Version, …). Guard so a mistaken M_("")
// cannot inject that blob (#1443). T("") remains a pure cast and is
// never routed here.
//
LIBMUX_API const UTF8 *mux_gettext(const UTF8 *msgid)
{
    if (  nullptr == msgid
       || '\0' == msgid[0])
    {
        return msgid;
    }

    return reinterpret_cast<const UTF8 *>(
        gettext(reinterpret_cast<const char *>(msgid)));
}
#endif

LIBMUX_API void mux_nls_init(const UTF8 *locale_dir)
{
#if defined(HAVE_NLS)
    // Leave LC_* to the environment (LANG / LC_MESSAGES).  Operators who
    // want English softcode + English suite should keep C/en_US.UTF-8.
    //
    setlocale(LC_ALL, "");

    if (  nullptr == locale_dir
       || '\0' == locale_dir[0])
    {
        return;
    }

#if defined(HAVE_LIBINTL_H)
    bindtextdomain("tinymux", reinterpret_cast<const char *>(locale_dir));
    textdomain("tinymux");
#if defined(HAVE_BIND_TEXTDOMAIN_CODESET)
    bind_textdomain_codeset("tinymux", "UTF-8");
#endif
#else
    // Built-in reader: resolve the same path libintl would and load it
    // once.  <locale_dir>/<lang>/LC_MESSAGES/tinymux.mo.
    //
    // No codeset conversion is performed or needed.  The catalogs in this
    // tree are UTF-8 and the server is UTF-8 throughout, which is the case
    // bind_textdomain_codeset would be asserting anyway.
    //
    mo_unload();

    char lang[64];
    if (!mo_language(lang, sizeof(lang)))
    {
        // No usable LANG/LC_*, or C/POSIX.  Untranslated, as gettext would.
        //
        return;
    }

    char path[1024];
    const int n = snprintf(path, sizeof(path), "%s/%s/LC_MESSAGES/tinymux.mo",
                           reinterpret_cast<const char *>(locale_dir), lang);
    if (  n <= 0
       || static_cast<size_t>(n) >= sizeof(path))
    {
        return;
    }

    // A missing or malformed catalog is not an error: it means this build
    // has no translation for the requested language, and every M_() then
    // returns its msgid.
    //
    (void)mo_load(path);
#endif
#else
    UNUSED_PARAMETER(locale_dir);
#endif
}
