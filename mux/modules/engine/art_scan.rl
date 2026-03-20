/*! \file art_scan.rl
 * \brief Ragel-generated scanner for English article selection (a/an).
 *
 * This file is processed by Ragel to generate art_scan.cpp.
 * Do not edit art_scan.cpp directly.
 *
 * Build: ragel -G2 -o art_scan.cpp art_scan.rl
 *
 * The rules hardcoded here replicate the traditional article_rule set
 * from netmux.conf.  Nobody ever changed those rules, so there is no
 * reason to keep them configurable.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "art_scan.h"

%%{
    machine art_scan;

    alphtype unsigned char;

    # ---------------------------------------------------------------
    # English article scanner.
    #
    # Every pattern ends with  any*  so every alternative consumes
    # the full input.  When all alternatives match the same length
    # the Ragel scanner picks the LAST listed pattern (highest
    # priority).  Patterns are ordered general → specific so that
    # specific exceptions override the general rules.
    # ---------------------------------------------------------------

    main := |*

        # 1. Default — everything gets "a".
        any+ => { use_an = false; };

        # 2. Vowel start → "an".
        [aeiou] any* => { use_an = true; };

        # 3. Vowel + [.-] → "a"  (abbreviations: "a E.T.", "a I-beam").
        [aeiou] [.\-] any* => { use_an = false; };

        # 4. he(ir|rb) → "an"  (heir, herb).
        'he' ('ir' | 'rb') any* => { use_an = true; };

        # 5. ho(mag|nest|no|ur) → "an"  (homage, honest, honor, hour).
        'ho' ('mag' | 'nest' | 'no' | 'ur') any* => { use_an = true; };

        # 6. e[uw] → "a"  (eucalyptus, ewe).
        'e' [uw] any* => { use_an = false; };

        # 7. onc?e → "a"  (once, one).
        'on' 'c'? 'e' any* => { use_an = false; };

        # 8. u[bcfhjkqrst][aeiou] → "a"  (ubiquitous, use, ...).
        'u' [bcfhjkqrst] [aeiou] any* => { use_an = false; };

        # 9. uni(vowel-class|dim|dir|sex|son) → "a"  (unicorn, uniform, ...).
        'uni' ([acflopqtvx] | 'dim' | 'dir' | 'sex' | 'son') any* =>
            { use_an = false; };

        # 10. unanim(ous|ity) → "a".
        'unanim' ('ous' | 'ity') any* => { use_an = false; };

        # 11. y[lt] → "an"  (ylang-ylang, yttrium).
        'y' [lt] any* => { use_an = true; };

    *|;

    write data noerror nofinal;
}%%

// art_should_use_an — returns true when the article should be "an".
//
// The caller is expected to pass a *lowercased* string (the same
// convention the old PCRE-based art() used).
//
bool art_should_use_an(const UTF8 *data, size_t len)
{
    if (len == 0)
    {
        return false;   // empty string → "a"
    }

    bool use_an = false;

    const unsigned char *p   = data;
    const unsigned char *pe  = data + len;
    const unsigned char *eof = pe;
    const unsigned char *ts;
    const unsigned char *te;
    int cs, act;

    %% write init;
    %% write exec;

    return use_an;
}
