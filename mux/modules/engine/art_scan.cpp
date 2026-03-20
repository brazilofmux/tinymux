
#line 1 "art_scan.rl"
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


#line 22 "art_scan.cpp"
static const int art_scan_start = 0;

static const int art_scan_en_main = 0;


#line 75 "art_scan.rl"


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

    
#line 49 "art_scan.cpp"
	{
	cs = art_scan_start;
	ts = 0;
	te = 0;
	act = 0;
	}

#line 99 "art_scan.rl"
    
#line 55 "art_scan.cpp"
	{
	if ( p == pe )
		goto _test_eof;
	switch ( cs )
	{
tr7:
#line 39 "art_scan.rl"
	{te = p;p--;{ use_an = false; }}
	goto st0;
st0:
#line 1 "NONE"
	{ts = 0;}
	if ( ++p == pe )
		goto _test_eof0;
case 0:
#line 1 "NONE"
	{ts = p;}
#line 69 "art_scan.cpp"
	switch( (*p) ) {
		case 97u: goto st2;
		case 101u: goto st5;
		case 104u: goto st7;
		case 105u: goto st2;
		case 111u: goto st20;
		case 117u: goto st24;
		case 121u: goto st44;
	}
	goto st1;
st1:
	if ( ++p == pe )
		goto _test_eof1;
case 1:
	goto st1;
st2:
	if ( ++p == pe )
		goto _test_eof2;
case 2:
	if ( 45u <= (*p) && (*p) <= 46u )
		goto st4;
	goto st3;
st3:
	if ( ++p == pe )
		goto _test_eof3;
case 3:
	goto st3;
st4:
	if ( ++p == pe )
		goto _test_eof4;
case 4:
	goto st4;
st5:
	if ( ++p == pe )
		goto _test_eof5;
case 5:
	switch( (*p) ) {
		case 117u: goto st6;
		case 119u: goto st6;
	}
	if ( 45u <= (*p) && (*p) <= 46u )
		goto st4;
	goto st3;
st6:
	if ( ++p == pe )
		goto _test_eof6;
case 6:
	goto st6;
st7:
	if ( ++p == pe )
		goto _test_eof7;
case 7:
	switch( (*p) ) {
		case 101u: goto st8;
		case 111u: goto st12;
	}
	goto st1;
st8:
	if ( ++p == pe )
		goto _test_eof8;
case 8:
	switch( (*p) ) {
		case 105u: goto st9;
		case 114u: goto st11;
	}
	goto st1;
st9:
	if ( ++p == pe )
		goto _test_eof9;
case 9:
	if ( (*p) == 114u )
		goto st10;
	goto st1;
st10:
	if ( ++p == pe )
		goto _test_eof10;
case 10:
	goto st10;
st11:
	if ( ++p == pe )
		goto _test_eof11;
case 11:
	if ( (*p) == 98u )
		goto st10;
	goto st1;
st12:
	if ( ++p == pe )
		goto _test_eof12;
case 12:
	switch( (*p) ) {
		case 109u: goto st13;
		case 110u: goto st16;
		case 117u: goto st19;
	}
	goto st1;
st13:
	if ( ++p == pe )
		goto _test_eof13;
case 13:
	if ( (*p) == 97u )
		goto st14;
	goto st1;
st14:
	if ( ++p == pe )
		goto _test_eof14;
case 14:
	if ( (*p) == 103u )
		goto st15;
	goto st1;
st15:
	if ( ++p == pe )
		goto _test_eof15;
case 15:
	goto st15;
st16:
	if ( ++p == pe )
		goto _test_eof16;
case 16:
	switch( (*p) ) {
		case 101u: goto st17;
		case 111u: goto st15;
	}
	goto st1;
st17:
	if ( ++p == pe )
		goto _test_eof17;
case 17:
	if ( (*p) == 115u )
		goto st18;
	goto st1;
st18:
	if ( ++p == pe )
		goto _test_eof18;
case 18:
	if ( (*p) == 116u )
		goto st15;
	goto st1;
st19:
	if ( ++p == pe )
		goto _test_eof19;
case 19:
	if ( (*p) == 114u )
		goto st15;
	goto st1;
st20:
	if ( ++p == pe )
		goto _test_eof20;
case 20:
	if ( (*p) == 110u )
		goto st21;
	if ( 45u <= (*p) && (*p) <= 46u )
		goto st4;
	goto st3;
st21:
	if ( ++p == pe )
		goto _test_eof21;
case 21:
	switch( (*p) ) {
		case 99u: goto st22;
		case 101u: goto st23;
	}
	goto st3;
st22:
	if ( ++p == pe )
		goto _test_eof22;
case 22:
	if ( (*p) == 101u )
		goto st23;
	goto st3;
st23:
	if ( ++p == pe )
		goto _test_eof23;
case 23:
	goto st23;
st24:
	if ( ++p == pe )
		goto _test_eof24;
case 24:
	switch( (*p) ) {
		case 102u: goto st25;
		case 104u: goto st25;
		case 110u: goto st27;
	}
	if ( (*p) < 98u ) {
		if ( 45u <= (*p) && (*p) <= 46u )
			goto st4;
	} else if ( (*p) > 99u ) {
		if ( (*p) > 107u ) {
			if ( 113u <= (*p) && (*p) <= 116u )
				goto st25;
		} else if ( (*p) >= 106u )
			goto st25;
	} else
		goto st25;
	goto st3;
st25:
	if ( ++p == pe )
		goto _test_eof25;
case 25:
	switch( (*p) ) {
		case 97u: goto st26;
		case 101u: goto st26;
		case 105u: goto st26;
		case 111u: goto st26;
		case 117u: goto st26;
	}
	goto st3;
st26:
	if ( ++p == pe )
		goto _test_eof26;
case 26:
	goto st26;
st27:
	if ( ++p == pe )
		goto _test_eof27;
case 27:
	switch( (*p) ) {
		case 97u: goto st28;
		case 105u: goto st37;
	}
	goto st3;
st28:
	if ( ++p == pe )
		goto _test_eof28;
case 28:
	if ( (*p) == 110u )
		goto st29;
	goto st3;
st29:
	if ( ++p == pe )
		goto _test_eof29;
case 29:
	if ( (*p) == 105u )
		goto st30;
	goto st3;
st30:
	if ( ++p == pe )
		goto _test_eof30;
case 30:
	if ( (*p) == 109u )
		goto st31;
	goto st3;
st31:
	if ( ++p == pe )
		goto _test_eof31;
case 31:
	switch( (*p) ) {
		case 105u: goto st32;
		case 111u: goto st35;
	}
	goto st3;
st32:
	if ( ++p == pe )
		goto _test_eof32;
case 32:
	if ( (*p) == 116u )
		goto st33;
	goto st3;
st33:
	if ( ++p == pe )
		goto _test_eof33;
case 33:
	if ( (*p) == 121u )
		goto st34;
	goto st3;
st34:
	if ( ++p == pe )
		goto _test_eof34;
case 34:
	goto st34;
st35:
	if ( ++p == pe )
		goto _test_eof35;
case 35:
	if ( (*p) == 117u )
		goto st36;
	goto st3;
st36:
	if ( ++p == pe )
		goto _test_eof36;
case 36:
	if ( (*p) == 115u )
		goto st34;
	goto st3;
st37:
	if ( ++p == pe )
		goto _test_eof37;
case 37:
	switch( (*p) ) {
		case 97u: goto st38;
		case 99u: goto st38;
		case 100u: goto st39;
		case 102u: goto st38;
		case 108u: goto st38;
		case 115u: goto st41;
		case 116u: goto st38;
		case 118u: goto st38;
		case 120u: goto st38;
	}
	if ( 111u <= (*p) && (*p) <= 113u )
		goto st38;
	goto st3;
st38:
	if ( ++p == pe )
		goto _test_eof38;
case 38:
	goto st38;
st39:
	if ( ++p == pe )
		goto _test_eof39;
case 39:
	if ( (*p) == 105u )
		goto st40;
	goto st3;
st40:
	if ( ++p == pe )
		goto _test_eof40;
case 40:
	switch( (*p) ) {
		case 109u: goto st38;
		case 114u: goto st38;
	}
	goto st3;
st41:
	if ( ++p == pe )
		goto _test_eof41;
case 41:
	switch( (*p) ) {
		case 101u: goto st42;
		case 111u: goto st43;
	}
	goto st3;
st42:
	if ( ++p == pe )
		goto _test_eof42;
case 42:
	if ( (*p) == 120u )
		goto st38;
	goto st3;
st43:
	if ( ++p == pe )
		goto _test_eof43;
case 43:
	if ( (*p) == 110u )
		goto st38;
	goto st3;
st44:
	if ( ++p == pe )
		goto _test_eof44;
case 44:
	switch( (*p) ) {
		case 108u: goto st45;
		case 116u: goto st45;
	}
	goto st1;
st45:
	if ( ++p == pe )
		goto _test_eof45;
case 45:
	goto st45;
	}
	_test_eof0: cs = 0; goto _test_eof; 
	_test_eof1: cs = 1; goto _test_eof; 
	_test_eof2: cs = 2; goto _test_eof; 
	_test_eof3: cs = 3; goto _test_eof; 
	_test_eof4: cs = 4; goto _test_eof; 
	_test_eof5: cs = 5; goto _test_eof; 
	_test_eof6: cs = 6; goto _test_eof; 
	_test_eof7: cs = 7; goto _test_eof; 
	_test_eof8: cs = 8; goto _test_eof; 
	_test_eof9: cs = 9; goto _test_eof; 
	_test_eof10: cs = 10; goto _test_eof; 
	_test_eof11: cs = 11; goto _test_eof; 
	_test_eof12: cs = 12; goto _test_eof; 
	_test_eof13: cs = 13; goto _test_eof; 
	_test_eof14: cs = 14; goto _test_eof; 
	_test_eof15: cs = 15; goto _test_eof; 
	_test_eof16: cs = 16; goto _test_eof; 
	_test_eof17: cs = 17; goto _test_eof; 
	_test_eof18: cs = 18; goto _test_eof; 
	_test_eof19: cs = 19; goto _test_eof; 
	_test_eof20: cs = 20; goto _test_eof; 
	_test_eof21: cs = 21; goto _test_eof; 
	_test_eof22: cs = 22; goto _test_eof; 
	_test_eof23: cs = 23; goto _test_eof; 
	_test_eof24: cs = 24; goto _test_eof; 
	_test_eof25: cs = 25; goto _test_eof; 
	_test_eof26: cs = 26; goto _test_eof; 
	_test_eof27: cs = 27; goto _test_eof; 
	_test_eof28: cs = 28; goto _test_eof; 
	_test_eof29: cs = 29; goto _test_eof; 
	_test_eof30: cs = 30; goto _test_eof; 
	_test_eof31: cs = 31; goto _test_eof; 
	_test_eof32: cs = 32; goto _test_eof; 
	_test_eof33: cs = 33; goto _test_eof; 
	_test_eof34: cs = 34; goto _test_eof; 
	_test_eof35: cs = 35; goto _test_eof; 
	_test_eof36: cs = 36; goto _test_eof; 
	_test_eof37: cs = 37; goto _test_eof; 
	_test_eof38: cs = 38; goto _test_eof; 
	_test_eof39: cs = 39; goto _test_eof; 
	_test_eof40: cs = 40; goto _test_eof; 
	_test_eof41: cs = 41; goto _test_eof; 
	_test_eof42: cs = 42; goto _test_eof; 
	_test_eof43: cs = 43; goto _test_eof; 
	_test_eof44: cs = 44; goto _test_eof; 
	_test_eof45: cs = 45; goto _test_eof; 

	_test_eof: {}
	if ( p == eof )
	{
	switch ( cs ) {
	case 1: goto tr7;
	case 2: goto tr7;
	case 3: goto tr7;
	case 4: goto tr7;
	case 5: goto tr7;
	case 6: goto tr7;
	case 7: goto tr7;
	case 8: goto tr7;
	case 9: goto tr7;
	case 10: goto tr7;
	case 11: goto tr7;
	case 12: goto tr7;
	case 13: goto tr7;
	case 14: goto tr7;
	case 15: goto tr7;
	case 16: goto tr7;
	case 17: goto tr7;
	case 18: goto tr7;
	case 19: goto tr7;
	case 20: goto tr7;
	case 21: goto tr7;
	case 22: goto tr7;
	case 23: goto tr7;
	case 24: goto tr7;
	case 25: goto tr7;
	case 26: goto tr7;
	case 27: goto tr7;
	case 28: goto tr7;
	case 29: goto tr7;
	case 30: goto tr7;
	case 31: goto tr7;
	case 32: goto tr7;
	case 33: goto tr7;
	case 34: goto tr7;
	case 35: goto tr7;
	case 36: goto tr7;
	case 37: goto tr7;
	case 38: goto tr7;
	case 39: goto tr7;
	case 40: goto tr7;
	case 41: goto tr7;
	case 42: goto tr7;
	case 43: goto tr7;
	case 44: goto tr7;
	case 45: goto tr7;
	}
	}

	}

#line 100 "art_scan.rl"

    return use_an;
}
