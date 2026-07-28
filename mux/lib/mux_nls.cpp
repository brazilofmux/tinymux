/*! \file mux_nls.cpp
 * \brief Optional gettext domain binding for server messages (#1419).
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"

// One implementation lives below: the built-in MO catalog reader.
//
// There used to be two, selected by whether GNU gettext was available --
// thin wrappers over gettext(3) where <libintl.h> existed, and this reader
// on MSVC, which has none (#1419).  They disagreed about how a language is
// chosen (libintl reads LANGUAGE; this reader never did) and about plural
// forms (libintl evaluated the catalogue's rule; this reader returned
// English), so the same catalogue produced different output per platform
// and the documented way to select a language did nothing on Windows.
//
// #1702 removed the split.  This reader now serves every platform and
// implements plural forms, so `language ko` in netmux.conf means one thing
// everywhere.
//
#if defined(HAVE_NLS)
// One set of includes: since #1702 there is one reader on every platform,
// and <libintl.h> is no longer among them -- nothing here calls gettext.
// setlocale is still used for LC_CTYPE, which is not catalogue selection.
//
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <new>
#endif

#if defined(HAVE_NLS)
// ---------------------------------------------------------------------------
// Built-in MO catalog reader (#1419, #1702).
//
// Implements just enough of gettext for M_() and MN_(): one domain, one
// language, plural forms, no context.
//
// This is now the ONLY lookup path, on every platform (#1702).  It used to
// be the MSVC fallback while libintl handled Unix, and the two disagreed in
// ways that were nobody's bug and everybody's problem: libintl selects a
// catalogue with LANGUAGE and this reader never read LANGUAGE, so the
// documented way to choose a language did nothing on Windows.  Having one
// reader is what makes `language ko` in netmux.conf mean the same thing
// everywhere.
//
// It also removes gettext's rule that the C/POSIX locale suppresses
// translation outright: this reader opens a file by path and does not
// consult the locale at all, so a server started by an init system with a
// bare environment still gets the language its configuration asked for.
//
// The cost is that plural selection had to be implemented here rather than
// borrowed -- see plural_eval below.  That is the piece libintl was really
// providing.
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

    // Plural-Forms state, and its loader.  Declared here because mo_load()
    // and mo_unload() above touch them and are defined first (#1702).
    //
    unsigned long s_nplurals    = 1;
    const char   *s_plural_expr = nullptr;   // into s_plural_buf, or null
    char          s_plural_buf[512];
    void plural_load(void);

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

    // As mo_entry, but also hands back the recorded length.  Plural
    // translations are NUL-separated forms inside one entry, so walking them
    // safely needs the length the file declares rather than a scan (#1702).
    //
    bool mo_entry_len(uint32_t tbl_off, uint32_t i, const char **str,
                      uint32_t *plen)
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
        *plen = len;
        return true;
    }

    void mo_unload(void)
    {
        delete [] s_catalog;
        s_catalog = nullptr;
        s_catalog_len = 0;
        s_count = 0;
        s_nplurals = 1;
        s_plural_expr = nullptr;
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
        plural_load();
        return true;
    }

    // ---- Plural-Forms (#1702) ----
    //
    // A catalogue header carries a rule such as
    //
    //   Plural-Forms: nplurals=2; plural=(n != 1);
    //   Plural-Forms: nplurals=1; plural=0;                    (ko, ja, zh)
    //   Plural-Forms: nplurals=3; plural=(n%10==1 && n%100!=11 ? 0 : ...);
    //
    // and the translation of a plural entry is nplurals NUL-separated forms.
    // Picking a form means evaluating that expression, which is why this
    // reader previously refused to do plurals at all and returned English.
    //
    // The grammar is a small C expression over one variable, n: ?: || && ==
    // != < > <= >= + - * / % ! and parentheses, unsigned integers only.  A
    // recursive-descent evaluator covers it exactly; there is no need for a
    // general expression engine, and anything outside the grammar is a
    // catalogue we should not trust.
    //
    // Evaluation is bounded and total: no allocation, no recursion beyond
    // the grammar's depth, division and modulo by zero yield 0 rather than
    // trapping.  A hostile .mo cannot do worse than return a wrong form.
    //
    struct PluralParser
    {
        const char   *p;
        unsigned long n;
        bool          bad;

        void skip(void) { while (' ' == *p || '\t' == *p) p++; }

        // Lowest precedence first; each level calls the next.
        unsigned long cond(void)
        {
            const unsigned long c = orx();
            skip();
            if ('?' != *p)
            {
                return c;
            }
            p++;
            const unsigned long a = cond();
            skip();
            if (':' != *p)
            {
                bad = true;
                return 0;
            }
            p++;
            const unsigned long b = cond();
            return c ? a : b;
        }

        unsigned long orx(void)
        {
            unsigned long v = andx();
            for (;;)
            {
                skip();
                if ('|' == p[0] && '|' == p[1])
                {
                    p += 2;
                    const unsigned long r = andx();
                    v = (v || r) ? 1u : 0u;
                }
                else
                {
                    return v;
                }
            }
        }

        unsigned long andx(void)
        {
            unsigned long v = equality();
            for (;;)
            {
                skip();
                if ('&' == p[0] && '&' == p[1])
                {
                    p += 2;
                    const unsigned long r = equality();
                    v = (v && r) ? 1u : 0u;
                }
                else
                {
                    return v;
                }
            }
        }

        unsigned long equality(void)
        {
            unsigned long v = relational();
            for (;;)
            {
                skip();
                if ('=' == p[0] && '=' == p[1])
                {
                    p += 2;
                    v = (v == relational()) ? 1u : 0u;
                }
                else if ('!' == p[0] && '=' == p[1])
                {
                    p += 2;
                    v = (v != relational()) ? 1u : 0u;
                }
                else
                {
                    return v;
                }
            }
        }

        unsigned long relational(void)
        {
            unsigned long v = additive();
            for (;;)
            {
                skip();
                if ('<' == p[0] && '=' == p[1])
                {
                    p += 2;
                    v = (v <= additive()) ? 1u : 0u;
                }
                else if ('>' == p[0] && '=' == p[1])
                {
                    p += 2;
                    v = (v >= additive()) ? 1u : 0u;
                }
                else if ('<' == p[0])
                {
                    p += 1;
                    v = (v < additive()) ? 1u : 0u;
                }
                else if ('>' == p[0])
                {
                    p += 1;
                    v = (v > additive()) ? 1u : 0u;
                }
                else
                {
                    return v;
                }
            }
        }

        unsigned long additive(void)
        {
            unsigned long v = multiplicative();
            for (;;)
            {
                skip();
                if ('+' == p[0])
                {
                    p++;
                    v = v + multiplicative();
                }
                else if ('-' == p[0])
                {
                    p++;
                    v = v - multiplicative();
                }
                else
                {
                    return v;
                }
            }
        }

        unsigned long multiplicative(void)
        {
            unsigned long v = unary();
            for (;;)
            {
                skip();
                if ('*' == p[0])
                {
                    p++;
                    v = v * unary();
                }
                else if ('/' == p[0] && '/' != p[1])
                {
                    p++;
                    const unsigned long d = unary();
                    v = (0 != d) ? (v / d) : 0u;
                }
                else if ('%' == p[0])
                {
                    p++;
                    const unsigned long d = unary();
                    v = (0 != d) ? (v % d) : 0u;
                }
                else
                {
                    return v;
                }
            }
        }

        unsigned long unary(void)
        {
            skip();
            if ('!' == p[0] && '=' != p[1])
            {
                p++;
                return unary() ? 0u : 1u;
            }
            return primary();
        }

        unsigned long primary(void)
        {
            skip();
            if ('(' == *p)
            {
                p++;
                const unsigned long v = cond();
                skip();
                if (')' != *p)
                {
                    bad = true;
                    return 0;
                }
                p++;
                return v;
            }
            if ('n' == *p)
            {
                p++;
                return n;
            }
            if (*p >= '0' && *p <= '9')
            {
                unsigned long v = 0;
                while (*p >= '0' && *p <= '9')
                {
                    v = v * 10u + static_cast<unsigned long>(*p - '0');
                    p++;
                }
                return v;
            }
            bad = true;
            return 0;
        }
    };

    // Which plural form does this catalogue want for n?  Falls back to the
    // English rule when there is no usable expression, which is what the
    // .pot itself declares and is right for the untranslated case.
    //
    unsigned long plural_eval(unsigned long n)
    {
        if (nullptr == s_plural_expr)
        {
            return (1 == n) ? 0u : 1u;
        }
        PluralParser pp;
        pp.p = s_plural_expr;
        pp.n = n;
        pp.bad = false;
        const unsigned long form = pp.cond();
        if (  pp.bad
           || form >= s_nplurals)
        {
            // A rule we cannot parse, or one that selected a form the
            // catalogue does not carry.  Form 0 is the safest answer: it is
            // the singular in every plural scheme.
            //
            return 0u;
        }
        return form;
    }

    // Pull nplurals / plural= out of the header entry (msgid "").
    //
    void plural_load(void)
    {
        s_nplurals = 1;
        s_plural_expr = nullptr;

        const char *hdr = nullptr;
        if (  0 == s_count
           || !mo_entry(s_trans_off, 0, &hdr))
        {
            return;
        }
        const char *pf = strstr(hdr, "Plural-Forms:");
        if (nullptr == pf)
        {
            return;
        }
        const char *np = strstr(pf, "nplurals=");
        if (nullptr != np)
        {
            s_nplurals = strtoul(np + 9, nullptr, 10);
        }
        if (  0 == s_nplurals
           || s_nplurals > 64)
        {
            s_nplurals = 1;
            return;
        }
        const char *pl = strstr(pf, "plural=");
        if (nullptr == pl)
        {
            return;
        }
        pl += 7;

        // Copy to a bounded buffer, stopping at the statement or line end,
        // so the evaluator never walks off the catalogue.
        //
        size_t i = 0;
        while (  '\0' != pl[i]
              && ';'  != pl[i]
              && '\n' != pl[i]
              && i + 1 < sizeof(s_plural_buf))
        {
            s_plural_buf[i] = pl[i];
            i++;
        }
        s_plural_buf[i] = '\0';
        s_plural_expr = s_plural_buf;
    }

    // LC_ALL, then LC_MESSAGES, then LANG -- the order gettext uses.
    // "xx.UTF-8@mod" narrows to "xx"; the catalogs in game/locale are
    // named by bare language code.
    //
    bool mo_language(char *out, size_t outlen)
    {
        // LANGUAGE first, then the LC_* ladder -- gettext's documented
        // precedence.  This reader used to omit LANGUAGE entirely, which is
        // why `export LANGUAGE=ko` selected nothing on Windows (#1702).  Now
        // that it serves every platform, omitting it would have broken the
        // recipe mux/po/README.md documents for Unix as well: caught by the
        // selection matrix, which is the only reason it is here.
        //
        const char *v = nullptr;
        for (const char *name : { "LANGUAGE", "LC_ALL", "LC_MESSAGES", "LANG" })
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

        // "de:fr" is a priority list; we load one catalogue, so take the
        // first.  ".UTF-8" and "@variant" are stripped as before -- catalogues
        // in game/locale are named by bare language code.
        //
        size_t n = 0;
        while (  '\0' != v[n]
              && '.' != v[n]
              && '@' != v[n]
              && ':' != v[n]
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

// Plural-aware lookup (#1622, #1702).
//
// A .mo stores a plural entry's original as "singular\0plural" and its
// translation as "form0\0form1\0..." -- nplurals forms, chosen by the
// catalogue's own Plural-Forms rule.  That rule is why this used to return
// English here and defer to libintl: guessing form 0 is right for the
// nplurals=1 languages (ko, ja, zh) and quietly wrong for Russian, which
// wants the singular only for 1, 21, 31...  Wrong English is a bug a
// translator reports; a wrong Russian form is one they never see.
//
// plural_eval() now evaluates the rule, so the count crosses the boundary
// and the catalogue decides -- on every platform, rather than only where
// libintl happened to exist.
//
LIBMUX_API const UTF8 *mux_ngettext(const UTF8 *msgid,
                                    const UTF8 *msgid_plural,
                                    unsigned long n)
{
    if (  nullptr == msgid
       || '\0' == msgid[0])
    {
        return msgid;
    }

    // English fallback, used whenever the catalogue cannot answer.
    //
    const UTF8 *pFallback = (1 == n) ? msgid : msgid_plural;

    if (  nullptr == s_catalog
       || 0 == s_count)
    {
        return pFallback;
    }

    const char *key = reinterpret_cast<const char *>(msgid);

    uint32_t lo = 0;
    uint32_t hi = s_count;
    while (lo < hi)
    {
        const uint32_t mid = lo + (hi - lo) / 2;
        const char *cand = nullptr;
        if (!mo_entry(s_orig_off, mid, &cand))
        {
            return pFallback;
        }
        const int cmp = strcmp(key, cand);
        if (0 == cmp)
        {
            // The originals table is sorted over the WHOLE original, which
            // for a plural entry is "singular\0plural".  strcmp stops at the
            // singular's NUL, so this lands on the entry whose singular
            // matches; that is the one gettext would pick too.
            //
            const char *tr = nullptr;
            uint32_t tlen = 0;
            if (!mo_entry_len(s_trans_off, mid, &tr, &tlen))
            {
                return pFallback;
            }

            const unsigned long want = plural_eval(n);

            // Walk the NUL-separated forms.  Bounded by the entry length
            // recorded in the file, which mo_entry_len has already checked
            // lies inside the catalogue -- a truncated or malformed entry
            // runs out of forms and falls back rather than reading on.
            //
            const char *q = tr;
            const char *qe = tr + tlen;
            for (unsigned long i = 0; i < want; i++)
            {
                const size_t len = strlen(q);
                q += len + 1;
                if (q >= qe)
                {
                    return pFallback;
                }
            }
            if ('\0' == q[0])
            {
                // Untranslated form; blanking the message would be worse.
                //
                return pFallback;
            }
            return reinterpret_cast<const UTF8 *>(q);
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
    return pFallback;
}
#endif

LIBMUX_API void mux_nls_init(const UTF8 *locale_dir, const UTF8 *language)
{
#if defined(HAVE_NLS)
    // LC_CTYPE and friends still come from the environment.  This no longer
    // selects the message catalogue -- see below.
    //
    setlocale(LC_ALL, "");

    if (  nullptr == locale_dir
       || '\0' == locale_dir[0])
    {
        return;
    }

    // One reader, one selection rule, every platform (#1702).
    //
    // The `language` directive wins when set; otherwise fall back to the
    // environment exactly as before.  Note what is NOT here any more: no
    // bindtextdomain, no LANGUAGE, and no dependency on the process locale.
    // gettext suppresses translation entirely under C/POSIX, so via libintl
    // a directive could not take effect in a bare service environment and
    // could only report that and carry on in English.  Opening a file by
    // path has no such rule, so `language ko` works where servers actually
    // run.
    //
    mo_unload();

    char lang[64];
    const bool bDirective = (  nullptr != language
                            && '\0' != language[0]);
    if (bDirective)
    {
        // Catalogue name only: [A-Za-z0-9_-]+.  Reject path separators so a
        // god-set `language` cannot escape locale_dir via `..` or `/`
        // (#1702).
        //
        size_t i = 0;
        while ('\0' != language[i] && i + 1 < sizeof(lang))
        {
            const unsigned char c = language[i];
            if (  !(  (c >= 'A' && c <= 'Z')
                   || (c >= 'a' && c <= 'z')
                   || (c >= '0' && c <= '9')
                   || '_' == c
                   || '-' == c))
            {
                fprintf(stderr,
                    "NLS: 'language %s' is not a valid catalogue name"
                    " (use a bare code such as ko or xx)."
                    "  Continuing in English.\n",
                    reinterpret_cast<const char *>(language));
                return;
            }
            lang[i] = static_cast<char>(c);
            i++;
        }
        if (0 == i || '\0' != language[i])
        {
            fprintf(stderr,
                "NLS: 'language %s' is empty or too long."
                "  Continuing in English.\n",
                reinterpret_cast<const char *>(language));
            return;
        }
        lang[i] = '\0';
    }
    else if (!mo_language(lang, sizeof(lang)))
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

    if (!mo_load(path))
    {
        // A catalogue the operator NAMED and that will not load is worth
        // reporting: the alternative is a server silently speaking the wrong
        // language.  An absent environment-derived one is not -- that is
        // just an untranslated server, which is the normal case.
        //
        if (bDirective)
        {
            fprintf(stderr,
                "NLS: 'language %s' selected, but %s could not be loaded."
                "  Continuing in English.\n",
                reinterpret_cast<const char *>(language), path);
        }
        mo_unload();
    }
#else
    UNUSED_PARAMETER(locale_dir);
    UNUSED_PARAMETER(language);
#endif
}
