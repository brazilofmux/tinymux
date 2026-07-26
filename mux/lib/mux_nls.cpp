/*! \file mux_nls.cpp
 * \brief Optional gettext domain binding for server messages (#1419).
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"

#if defined(HAVE_NLS)
#include <libintl.h>
#include <locale.h>
#endif

#if defined(HAVE_NLS)
// gettext("") is not identity: the empty msgid stores the .mo header
// (Project-Id-Version, …). T("") is idiomatic empty-UTF8 throughout
// the tree (~80 sites); those must stay empty (#1443).
//
LIBMUX_API const UTF8 *mux_gettext(const UTF8 *msgid)
{
    if (  nullptr == msgid
       || '\0' == msgid[0])
    {
        // Preserve pointer identity for nullptr / empty-string literals
        // so emptiness tests and StringClone(T("")) keep working.
        //
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

    bindtextdomain("tinymux", reinterpret_cast<const char *>(locale_dir));
    textdomain("tinymux");
#if defined(HAVE_BIND_TEXTDOMAIN_CODESET)
    bind_textdomain_codeset("tinymux", "UTF-8");
#endif
#else
    UNUSED_PARAMETER(locale_dir);
#endif
}
