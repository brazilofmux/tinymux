
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


#line 113 "art_scan.rl"


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

#line 137 "art_scan.rl"
    
#line 55 "art_scan.cpp"
	{
	if ( p == pe )
		goto _test_eof;
	switch ( cs )
	{
tr9:
#line 108 "art_scan.rl"
	{te = p;p--;{ use_an = false; }}
	goto st0;
tr12:
#line 77 "art_scan.rl"
	{te = p;p--;{ use_an = true; }}
	goto st0;
tr15:
#line 78 "art_scan.rl"
	{te = p;p--;{ use_an = true; }}
	goto st0;
tr18:
#line 61 "art_scan.rl"
	{te = p;p--;{ use_an = true; }}
	goto st0;
tr19:
#line 104 "art_scan.rl"
	{te = p;p--;{ use_an = true; }}
	goto st0;
tr22:
#line 83 "art_scan.rl"
	{te = p;p--;{ use_an = false; }}
	goto st0;
tr24:
#line 86 "art_scan.rl"
	{te = p;p--;{ use_an = false; }}
	goto st0;
tr30:
#line 47 "art_scan.rl"
	{te = p;p--;{ use_an = true; }}
	goto st0;
tr36:
#line 50 "art_scan.rl"
	{te = p;p--;{ use_an = true; }}
	goto st0;
tr42:
#line 89 "art_scan.rl"
	{te = p;p--;{ use_an = false; }}
	goto st0;
tr46:
#line 99 "art_scan.rl"
	{te = p;p--;{ use_an = false; }}
	goto st0;
tr56:
#line 92 "art_scan.rl"
	{te = p;p--;{ use_an = false; }}
	goto st0;
tr61:
#line 96 "art_scan.rl"
	{te = p;p--;{ use_an = false; }}
	goto st0;
tr66:
#line 53 "art_scan.rl"
	{te = p;p--;{ use_an = true; }}
	goto st0;
st0:
#line 1 "NONE"
	{ts = 0;}
	if ( ++p == pe )
		goto _test_eof0;
case 0:
#line 1 "NONE"
	{ts = p;}
#line 108 "art_scan.cpp"
	switch( (*p) ) {
		case 49u: goto st2;
		case 56u: goto st9;
		case 97u: goto st10;
		case 101u: goto st13;
		case 104u: goto st15;
		case 105u: goto st10;
		case 111u: goto st28;
		case 117u: goto st32;
		case 121u: goto st52;
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
	switch( (*p) ) {
		case 49u: goto st3;
		case 56u: goto st6;
	}
	goto st1;
st3:
	if ( ++p == pe )
		goto _test_eof3;
case 3:
	if ( 48u <= (*p) && (*p) <= 57u )
		goto st4;
	goto st1;
st4:
	if ( ++p == pe )
		goto _test_eof4;
case 4:
	if ( 48u <= (*p) && (*p) <= 57u )
		goto st5;
	goto st1;
st5:
	if ( ++p == pe )
		goto _test_eof5;
case 5:
	if ( 48u <= (*p) && (*p) <= 57u )
		goto st3;
	goto st1;
st6:
	if ( ++p == pe )
		goto _test_eof6;
case 6:
	if ( 48u <= (*p) && (*p) <= 57u )
		goto st7;
	goto st1;
st7:
	if ( ++p == pe )
		goto _test_eof7;
case 7:
	if ( 48u <= (*p) && (*p) <= 57u )
		goto st8;
	goto st1;
st8:
	if ( ++p == pe )
		goto _test_eof8;
case 8:
	if ( 48u <= (*p) && (*p) <= 57u )
		goto st6;
	goto st1;
st9:
	if ( ++p == pe )
		goto _test_eof9;
case 9:
	goto st9;
st10:
	if ( ++p == pe )
		goto _test_eof10;
case 10:
	if ( 45u <= (*p) && (*p) <= 46u )
		goto st12;
	goto st11;
st11:
	if ( ++p == pe )
		goto _test_eof11;
case 11:
	goto st11;
st12:
	if ( ++p == pe )
		goto _test_eof12;
case 12:
	goto st12;
st13:
	if ( ++p == pe )
		goto _test_eof13;
case 13:
	switch( (*p) ) {
		case 117u: goto st14;
		case 119u: goto st14;
	}
	if ( 45u <= (*p) && (*p) <= 46u )
		goto st12;
	goto st11;
st14:
	if ( ++p == pe )
		goto _test_eof14;
case 14:
	goto st14;
st15:
	if ( ++p == pe )
		goto _test_eof15;
case 15:
	switch( (*p) ) {
		case 101u: goto st16;
		case 111u: goto st20;
	}
	goto st1;
st16:
	if ( ++p == pe )
		goto _test_eof16;
case 16:
	switch( (*p) ) {
		case 105u: goto st17;
		case 114u: goto st19;
	}
	goto st1;
st17:
	if ( ++p == pe )
		goto _test_eof17;
case 17:
	if ( (*p) == 114u )
		goto st18;
	goto st1;
st18:
	if ( ++p == pe )
		goto _test_eof18;
case 18:
	goto st18;
st19:
	if ( ++p == pe )
		goto _test_eof19;
case 19:
	if ( (*p) == 98u )
		goto st18;
	goto st1;
st20:
	if ( ++p == pe )
		goto _test_eof20;
case 20:
	switch( (*p) ) {
		case 109u: goto st21;
		case 110u: goto st24;
		case 117u: goto st27;
	}
	goto st1;
st21:
	if ( ++p == pe )
		goto _test_eof21;
case 21:
	if ( (*p) == 97u )
		goto st22;
	goto st1;
st22:
	if ( ++p == pe )
		goto _test_eof22;
case 22:
	if ( (*p) == 103u )
		goto st23;
	goto st1;
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
		case 101u: goto st25;
		case 111u: goto st23;
	}
	goto st1;
st25:
	if ( ++p == pe )
		goto _test_eof25;
case 25:
	if ( (*p) == 115u )
		goto st26;
	goto st1;
st26:
	if ( ++p == pe )
		goto _test_eof26;
case 26:
	if ( (*p) == 116u )
		goto st23;
	goto st1;
st27:
	if ( ++p == pe )
		goto _test_eof27;
case 27:
	if ( (*p) == 114u )
		goto st23;
	goto st1;
st28:
	if ( ++p == pe )
		goto _test_eof28;
case 28:
	if ( (*p) == 110u )
		goto st29;
	if ( 45u <= (*p) && (*p) <= 46u )
		goto st12;
	goto st11;
st29:
	if ( ++p == pe )
		goto _test_eof29;
case 29:
	switch( (*p) ) {
		case 99u: goto st30;
		case 101u: goto st31;
	}
	goto st11;
st30:
	if ( ++p == pe )
		goto _test_eof30;
case 30:
	if ( (*p) == 101u )
		goto st31;
	goto st11;
st31:
	if ( ++p == pe )
		goto _test_eof31;
case 31:
	goto st31;
st32:
	if ( ++p == pe )
		goto _test_eof32;
case 32:
	switch( (*p) ) {
		case 102u: goto st33;
		case 104u: goto st33;
		case 110u: goto st35;
	}
	if ( (*p) < 98u ) {
		if ( 45u <= (*p) && (*p) <= 46u )
			goto st12;
	} else if ( (*p) > 99u ) {
		if ( (*p) > 107u ) {
			if ( 113u <= (*p) && (*p) <= 116u )
				goto st33;
		} else if ( (*p) >= 106u )
			goto st33;
	} else
		goto st33;
	goto st11;
st33:
	if ( ++p == pe )
		goto _test_eof33;
case 33:
	switch( (*p) ) {
		case 97u: goto st34;
		case 101u: goto st34;
		case 105u: goto st34;
		case 111u: goto st34;
		case 117u: goto st34;
	}
	goto st11;
st34:
	if ( ++p == pe )
		goto _test_eof34;
case 34:
	goto st34;
st35:
	if ( ++p == pe )
		goto _test_eof35;
case 35:
	switch( (*p) ) {
		case 97u: goto st36;
		case 105u: goto st45;
	}
	goto st11;
st36:
	if ( ++p == pe )
		goto _test_eof36;
case 36:
	if ( (*p) == 110u )
		goto st37;
	goto st11;
st37:
	if ( ++p == pe )
		goto _test_eof37;
case 37:
	if ( (*p) == 105u )
		goto st38;
	goto st11;
st38:
	if ( ++p == pe )
		goto _test_eof38;
case 38:
	if ( (*p) == 109u )
		goto st39;
	goto st11;
st39:
	if ( ++p == pe )
		goto _test_eof39;
case 39:
	switch( (*p) ) {
		case 105u: goto st40;
		case 111u: goto st43;
	}
	goto st11;
st40:
	if ( ++p == pe )
		goto _test_eof40;
case 40:
	if ( (*p) == 116u )
		goto st41;
	goto st11;
st41:
	if ( ++p == pe )
		goto _test_eof41;
case 41:
	if ( (*p) == 121u )
		goto st42;
	goto st11;
st42:
	if ( ++p == pe )
		goto _test_eof42;
case 42:
	goto st42;
st43:
	if ( ++p == pe )
		goto _test_eof43;
case 43:
	if ( (*p) == 117u )
		goto st44;
	goto st11;
st44:
	if ( ++p == pe )
		goto _test_eof44;
case 44:
	if ( (*p) == 115u )
		goto st42;
	goto st11;
st45:
	if ( ++p == pe )
		goto _test_eof45;
case 45:
	switch( (*p) ) {
		case 97u: goto st46;
		case 99u: goto st46;
		case 100u: goto st47;
		case 102u: goto st46;
		case 108u: goto st46;
		case 115u: goto st49;
		case 116u: goto st46;
		case 118u: goto st46;
		case 120u: goto st46;
	}
	if ( 111u <= (*p) && (*p) <= 113u )
		goto st46;
	goto st11;
st46:
	if ( ++p == pe )
		goto _test_eof46;
case 46:
	goto st46;
st47:
	if ( ++p == pe )
		goto _test_eof47;
case 47:
	if ( (*p) == 105u )
		goto st48;
	goto st11;
st48:
	if ( ++p == pe )
		goto _test_eof48;
case 48:
	switch( (*p) ) {
		case 109u: goto st46;
		case 114u: goto st46;
	}
	goto st11;
st49:
	if ( ++p == pe )
		goto _test_eof49;
case 49:
	switch( (*p) ) {
		case 101u: goto st50;
		case 111u: goto st51;
	}
	goto st11;
st50:
	if ( ++p == pe )
		goto _test_eof50;
case 50:
	if ( (*p) == 120u )
		goto st46;
	goto st11;
st51:
	if ( ++p == pe )
		goto _test_eof51;
case 51:
	if ( (*p) == 110u )
		goto st46;
	goto st11;
st52:
	if ( ++p == pe )
		goto _test_eof52;
case 52:
	switch( (*p) ) {
		case 108u: goto st53;
		case 116u: goto st53;
	}
	goto st1;
st53:
	if ( ++p == pe )
		goto _test_eof53;
case 53:
	goto st53;
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
	_test_eof46: cs = 46; goto _test_eof; 
	_test_eof47: cs = 47; goto _test_eof; 
	_test_eof48: cs = 48; goto _test_eof; 
	_test_eof49: cs = 49; goto _test_eof; 
	_test_eof50: cs = 50; goto _test_eof; 
	_test_eof51: cs = 51; goto _test_eof; 
	_test_eof52: cs = 52; goto _test_eof; 
	_test_eof53: cs = 53; goto _test_eof; 

	_test_eof: {}
	if ( p == eof )
	{
	switch ( cs ) {
	case 1: goto tr9;
	case 2: goto tr9;
	case 3: goto tr12;
	case 4: goto tr9;
	case 5: goto tr9;
	case 6: goto tr15;
	case 7: goto tr9;
	case 8: goto tr9;
	case 9: goto tr18;
	case 10: goto tr19;
	case 11: goto tr19;
	case 12: goto tr22;
	case 13: goto tr19;
	case 14: goto tr24;
	case 15: goto tr9;
	case 16: goto tr9;
	case 17: goto tr9;
	case 18: goto tr30;
	case 19: goto tr9;
	case 20: goto tr9;
	case 21: goto tr9;
	case 22: goto tr9;
	case 23: goto tr36;
	case 24: goto tr9;
	case 25: goto tr9;
	case 26: goto tr9;
	case 27: goto tr9;
	case 28: goto tr19;
	case 29: goto tr19;
	case 30: goto tr19;
	case 31: goto tr42;
	case 32: goto tr19;
	case 33: goto tr19;
	case 34: goto tr46;
	case 35: goto tr19;
	case 36: goto tr19;
	case 37: goto tr19;
	case 38: goto tr19;
	case 39: goto tr19;
	case 40: goto tr19;
	case 41: goto tr19;
	case 42: goto tr56;
	case 43: goto tr19;
	case 44: goto tr19;
	case 45: goto tr19;
	case 46: goto tr61;
	case 47: goto tr19;
	case 48: goto tr19;
	case 49: goto tr19;
	case 50: goto tr19;
	case 51: goto tr19;
	case 52: goto tr9;
	case 53: goto tr66;
	}
	}

	}

#line 138 "art_scan.rl"

    return use_an;
}
