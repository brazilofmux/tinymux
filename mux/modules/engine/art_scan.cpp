
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


#line 118 "art_scan.rl"


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

#line 142 "art_scan.rl"
    
#line 55 "art_scan.cpp"
	{
	if ( p == pe )
		goto _test_eof;
	switch ( cs )
	{
tr9:
#line 113 "art_scan.rl"
	{te = p;p--;{ use_an = false; }}
	goto st0;
tr12:
#line 82 "art_scan.rl"
	{te = p;p--;{ use_an = true; }}
	goto st0;
tr16:
#line 83 "art_scan.rl"
	{te = p;p--;{ use_an = true; }}
	goto st0;
tr20:
#line 61 "art_scan.rl"
	{te = p;p--;{ use_an = true; }}
	goto st0;
tr21:
#line 109 "art_scan.rl"
	{te = p;p--;{ use_an = true; }}
	goto st0;
tr24:
#line 88 "art_scan.rl"
	{te = p;p--;{ use_an = false; }}
	goto st0;
tr26:
#line 91 "art_scan.rl"
	{te = p;p--;{ use_an = false; }}
	goto st0;
tr32:
#line 47 "art_scan.rl"
	{te = p;p--;{ use_an = true; }}
	goto st0;
tr38:
#line 50 "art_scan.rl"
	{te = p;p--;{ use_an = true; }}
	goto st0;
tr44:
#line 94 "art_scan.rl"
	{te = p;p--;{ use_an = false; }}
	goto st0;
tr48:
#line 104 "art_scan.rl"
	{te = p;p--;{ use_an = false; }}
	goto st0;
tr58:
#line 97 "art_scan.rl"
	{te = p;p--;{ use_an = false; }}
	goto st0;
tr63:
#line 101 "art_scan.rl"
	{te = p;p--;{ use_an = false; }}
	goto st0;
tr68:
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
		case 56u: goto st11;
		case 97u: goto st12;
		case 101u: goto st15;
		case 104u: goto st17;
		case 105u: goto st12;
		case 111u: goto st30;
		case 117u: goto st34;
		case 121u: goto st54;
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
		case 56u: goto st7;
	}
	goto st1;
st3:
	if ( ++p == pe )
		goto _test_eof3;
case 3:
	if ( 48u <= (*p) && (*p) <= 57u )
		goto st5;
	goto st4;
st4:
	if ( ++p == pe )
		goto _test_eof4;
case 4:
	goto st4;
st5:
	if ( ++p == pe )
		goto _test_eof5;
case 5:
	if ( 48u <= (*p) && (*p) <= 57u )
		goto st6;
	goto st1;
st6:
	if ( ++p == pe )
		goto _test_eof6;
case 6:
	if ( 48u <= (*p) && (*p) <= 57u )
		goto st3;
	goto st1;
st7:
	if ( ++p == pe )
		goto _test_eof7;
case 7:
	if ( 48u <= (*p) && (*p) <= 57u )
		goto st9;
	goto st8;
st8:
	if ( ++p == pe )
		goto _test_eof8;
case 8:
	goto st8;
st9:
	if ( ++p == pe )
		goto _test_eof9;
case 9:
	if ( 48u <= (*p) && (*p) <= 57u )
		goto st10;
	goto st1;
st10:
	if ( ++p == pe )
		goto _test_eof10;
case 10:
	if ( 48u <= (*p) && (*p) <= 57u )
		goto st7;
	goto st1;
st11:
	if ( ++p == pe )
		goto _test_eof11;
case 11:
	goto st11;
st12:
	if ( ++p == pe )
		goto _test_eof12;
case 12:
	if ( 45u <= (*p) && (*p) <= 46u )
		goto st14;
	goto st13;
st13:
	if ( ++p == pe )
		goto _test_eof13;
case 13:
	goto st13;
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
		case 117u: goto st16;
		case 119u: goto st16;
	}
	if ( 45u <= (*p) && (*p) <= 46u )
		goto st14;
	goto st13;
st16:
	if ( ++p == pe )
		goto _test_eof16;
case 16:
	goto st16;
st17:
	if ( ++p == pe )
		goto _test_eof17;
case 17:
	switch( (*p) ) {
		case 101u: goto st18;
		case 111u: goto st22;
	}
	goto st1;
st18:
	if ( ++p == pe )
		goto _test_eof18;
case 18:
	switch( (*p) ) {
		case 105u: goto st19;
		case 114u: goto st21;
	}
	goto st1;
st19:
	if ( ++p == pe )
		goto _test_eof19;
case 19:
	if ( (*p) == 114u )
		goto st20;
	goto st1;
st20:
	if ( ++p == pe )
		goto _test_eof20;
case 20:
	goto st20;
st21:
	if ( ++p == pe )
		goto _test_eof21;
case 21:
	if ( (*p) == 98u )
		goto st20;
	goto st1;
st22:
	if ( ++p == pe )
		goto _test_eof22;
case 22:
	switch( (*p) ) {
		case 109u: goto st23;
		case 110u: goto st26;
		case 117u: goto st29;
	}
	goto st1;
st23:
	if ( ++p == pe )
		goto _test_eof23;
case 23:
	if ( (*p) == 97u )
		goto st24;
	goto st1;
st24:
	if ( ++p == pe )
		goto _test_eof24;
case 24:
	if ( (*p) == 103u )
		goto st25;
	goto st1;
st25:
	if ( ++p == pe )
		goto _test_eof25;
case 25:
	goto st25;
st26:
	if ( ++p == pe )
		goto _test_eof26;
case 26:
	switch( (*p) ) {
		case 101u: goto st27;
		case 111u: goto st25;
	}
	goto st1;
st27:
	if ( ++p == pe )
		goto _test_eof27;
case 27:
	if ( (*p) == 115u )
		goto st28;
	goto st1;
st28:
	if ( ++p == pe )
		goto _test_eof28;
case 28:
	if ( (*p) == 116u )
		goto st25;
	goto st1;
st29:
	if ( ++p == pe )
		goto _test_eof29;
case 29:
	if ( (*p) == 114u )
		goto st25;
	goto st1;
st30:
	if ( ++p == pe )
		goto _test_eof30;
case 30:
	if ( (*p) == 110u )
		goto st31;
	if ( 45u <= (*p) && (*p) <= 46u )
		goto st14;
	goto st13;
st31:
	if ( ++p == pe )
		goto _test_eof31;
case 31:
	switch( (*p) ) {
		case 99u: goto st32;
		case 101u: goto st33;
	}
	goto st13;
st32:
	if ( ++p == pe )
		goto _test_eof32;
case 32:
	if ( (*p) == 101u )
		goto st33;
	goto st13;
st33:
	if ( ++p == pe )
		goto _test_eof33;
case 33:
	goto st33;
st34:
	if ( ++p == pe )
		goto _test_eof34;
case 34:
	switch( (*p) ) {
		case 102u: goto st35;
		case 104u: goto st35;
		case 110u: goto st37;
	}
	if ( (*p) < 98u ) {
		if ( 45u <= (*p) && (*p) <= 46u )
			goto st14;
	} else if ( (*p) > 99u ) {
		if ( (*p) > 107u ) {
			if ( 113u <= (*p) && (*p) <= 116u )
				goto st35;
		} else if ( (*p) >= 106u )
			goto st35;
	} else
		goto st35;
	goto st13;
st35:
	if ( ++p == pe )
		goto _test_eof35;
case 35:
	switch( (*p) ) {
		case 97u: goto st36;
		case 101u: goto st36;
		case 105u: goto st36;
		case 111u: goto st36;
		case 117u: goto st36;
	}
	goto st13;
st36:
	if ( ++p == pe )
		goto _test_eof36;
case 36:
	goto st36;
st37:
	if ( ++p == pe )
		goto _test_eof37;
case 37:
	switch( (*p) ) {
		case 97u: goto st38;
		case 105u: goto st47;
	}
	goto st13;
st38:
	if ( ++p == pe )
		goto _test_eof38;
case 38:
	if ( (*p) == 110u )
		goto st39;
	goto st13;
st39:
	if ( ++p == pe )
		goto _test_eof39;
case 39:
	if ( (*p) == 105u )
		goto st40;
	goto st13;
st40:
	if ( ++p == pe )
		goto _test_eof40;
case 40:
	if ( (*p) == 109u )
		goto st41;
	goto st13;
st41:
	if ( ++p == pe )
		goto _test_eof41;
case 41:
	switch( (*p) ) {
		case 105u: goto st42;
		case 111u: goto st45;
	}
	goto st13;
st42:
	if ( ++p == pe )
		goto _test_eof42;
case 42:
	if ( (*p) == 116u )
		goto st43;
	goto st13;
st43:
	if ( ++p == pe )
		goto _test_eof43;
case 43:
	if ( (*p) == 121u )
		goto st44;
	goto st13;
st44:
	if ( ++p == pe )
		goto _test_eof44;
case 44:
	goto st44;
st45:
	if ( ++p == pe )
		goto _test_eof45;
case 45:
	if ( (*p) == 117u )
		goto st46;
	goto st13;
st46:
	if ( ++p == pe )
		goto _test_eof46;
case 46:
	if ( (*p) == 115u )
		goto st44;
	goto st13;
st47:
	if ( ++p == pe )
		goto _test_eof47;
case 47:
	switch( (*p) ) {
		case 97u: goto st48;
		case 99u: goto st48;
		case 100u: goto st49;
		case 102u: goto st48;
		case 108u: goto st48;
		case 115u: goto st51;
		case 116u: goto st48;
		case 118u: goto st48;
		case 120u: goto st48;
	}
	if ( 111u <= (*p) && (*p) <= 113u )
		goto st48;
	goto st13;
st48:
	if ( ++p == pe )
		goto _test_eof48;
case 48:
	goto st48;
st49:
	if ( ++p == pe )
		goto _test_eof49;
case 49:
	if ( (*p) == 105u )
		goto st50;
	goto st13;
st50:
	if ( ++p == pe )
		goto _test_eof50;
case 50:
	switch( (*p) ) {
		case 109u: goto st48;
		case 114u: goto st48;
	}
	goto st13;
st51:
	if ( ++p == pe )
		goto _test_eof51;
case 51:
	switch( (*p) ) {
		case 101u: goto st52;
		case 111u: goto st53;
	}
	goto st13;
st52:
	if ( ++p == pe )
		goto _test_eof52;
case 52:
	if ( (*p) == 120u )
		goto st48;
	goto st13;
st53:
	if ( ++p == pe )
		goto _test_eof53;
case 53:
	if ( (*p) == 110u )
		goto st48;
	goto st13;
st54:
	if ( ++p == pe )
		goto _test_eof54;
case 54:
	switch( (*p) ) {
		case 108u: goto st55;
		case 116u: goto st55;
	}
	goto st1;
st55:
	if ( ++p == pe )
		goto _test_eof55;
case 55:
	goto st55;
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
	_test_eof54: cs = 54; goto _test_eof; 
	_test_eof55: cs = 55; goto _test_eof; 

	_test_eof: {}
	if ( p == eof )
	{
	switch ( cs ) {
	case 1: goto tr9;
	case 2: goto tr9;
	case 3: goto tr12;
	case 4: goto tr12;
	case 5: goto tr9;
	case 6: goto tr9;
	case 7: goto tr16;
	case 8: goto tr16;
	case 9: goto tr9;
	case 10: goto tr9;
	case 11: goto tr20;
	case 12: goto tr21;
	case 13: goto tr21;
	case 14: goto tr24;
	case 15: goto tr21;
	case 16: goto tr26;
	case 17: goto tr9;
	case 18: goto tr9;
	case 19: goto tr9;
	case 20: goto tr32;
	case 21: goto tr9;
	case 22: goto tr9;
	case 23: goto tr9;
	case 24: goto tr9;
	case 25: goto tr38;
	case 26: goto tr9;
	case 27: goto tr9;
	case 28: goto tr9;
	case 29: goto tr9;
	case 30: goto tr21;
	case 31: goto tr21;
	case 32: goto tr21;
	case 33: goto tr44;
	case 34: goto tr21;
	case 35: goto tr21;
	case 36: goto tr48;
	case 37: goto tr21;
	case 38: goto tr21;
	case 39: goto tr21;
	case 40: goto tr21;
	case 41: goto tr21;
	case 42: goto tr21;
	case 43: goto tr21;
	case 44: goto tr58;
	case 45: goto tr21;
	case 46: goto tr21;
	case 47: goto tr21;
	case 48: goto tr63;
	case 49: goto tr21;
	case 50: goto tr21;
	case 51: goto tr21;
	case 52: goto tr21;
	case 53: goto tr21;
	case 54: goto tr9;
	case 55: goto tr68;
	}
	}

	}

#line 143 "art_scan.rl"

    return use_an;
}
