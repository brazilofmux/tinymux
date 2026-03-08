
#line 1 "ast_scan.rl"
/*! \file ast_scan.rl
 * \brief Ragel-generated scanner for MUX expression tokenizer.
 *
 * This file is processed by Ragel to generate ast_scan.cpp.
 * Do not edit ast_scan.cpp directly.
 *
 * Build: ragel -G2 -o ast_scan.cpp ast_scan.rl
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "ast.h"

#include <string_view>
#include <vector>

// Helper: construct token text view from Ragel ts/te pointers.
// Views point directly into the input buffer — no allocation.
//
#define TOK_TEXT() std::string_view(reinterpret_cast<const char *>(ts), \
                                    static_cast<size_t>(te - ts))


#line 177 "ast_scan.rl"


// Ragel state table data.
//

#line 31 "ast_scan.cpp"
static const int ast_scanner_start = 2;
static const int ast_scanner_error = 0;

static const int ast_scanner_en_main = 2;


#line 182 "ast_scan.rl"

std::vector<ASTToken> ast_tokenize(const UTF8 *input, size_t nLen)
{
    std::vector<ASTToken> tokens;

    // Find actual length (stop at NUL or nLen).
    //
    size_t actualLen = 0;
    while (actualLen < nLen && input[actualLen] != '\0')
    {
        actualLen++;
    }

    if (0 == actualLen)
    {
        tokens.push_back({ASTTOK_EOF, ""});
        return tokens;
    }

    // Ragel scanner variables.
    //
    const UTF8 *p   = input;
    const UTF8 *pe  = input + actualLen;
    const UTF8 *eof = pe;
    const UTF8 *ts;
    const UTF8 *te;
    int cs;
    int act;

    
#line 65 "ast_scan.cpp"
	{
	cs = ast_scanner_start;
	ts = 0;
	te = 0;
	act = 0;
	}

#line 212 "ast_scan.rl"
    
#line 71 "ast_scan.cpp"
	{
	if ( p == pe )
		goto _test_eof;
	switch ( cs )
	{
tr0:
#line 164 "ast_scan.rl"
	{{p = ((te))-1;}{
            tokens.push_back({ASTTOK_LIT, TOK_TEXT()});
        }}
	goto st2;
tr6:
#line 138 "ast_scan.rl"
	{te = p+1;{
            if (  !tokens.empty()
               && tokens.back().type == ASTTOK_LIT)
            {
                tokens.back().type = ASTTOK_FUNC;
            }
            tokens.push_back({ASTTOK_LPAREN, TOK_TEXT()});
        }}
	goto st2;
tr7:
#line 146 "ast_scan.rl"
	{te = p+1;{
            tokens.push_back({ASTTOK_RPAREN, TOK_TEXT()});
        }}
	goto st2;
tr8:
#line 149 "ast_scan.rl"
	{te = p+1;{
            tokens.push_back({ASTTOK_COMMA, TOK_TEXT()});
        }}
	goto st2;
tr9:
#line 152 "ast_scan.rl"
	{te = p+1;{
            tokens.push_back({ASTTOK_SEMI, TOK_TEXT()});
        }}
	goto st2;
tr10:
#line 126 "ast_scan.rl"
	{te = p+1;{
            tokens.push_back({ASTTOK_LBRACK, TOK_TEXT()});
        }}
	goto st2;
tr12:
#line 129 "ast_scan.rl"
	{te = p+1;{
            tokens.push_back({ASTTOK_RBRACK, TOK_TEXT()});
        }}
	goto st2;
tr13:
#line 132 "ast_scan.rl"
	{te = p+1;{
            tokens.push_back({ASTTOK_LBRACE, TOK_TEXT()});
        }}
	goto st2;
tr14:
#line 135 "ast_scan.rl"
	{te = p+1;{
            tokens.push_back({ASTTOK_RBRACE, TOK_TEXT()});
        }}
	goto st2;
tr15:
#line 164 "ast_scan.rl"
	{te = p;p--;{
            tokens.push_back({ASTTOK_LIT, TOK_TEXT()});
        }}
	goto st2;
tr17:
#line 158 "ast_scan.rl"
	{te = p;p--;{
            tokens.push_back({ASTTOK_SPACE, TOK_TEXT()});
        }}
	goto st2;
tr18:
#line 172 "ast_scan.rl"
	{te = p;p--;{
            tokens.push_back({ASTTOK_LIT, TOK_TEXT()});
        }}
	goto st2;
tr19:
#line 111 "ast_scan.rl"
	{te = p+1;{
            tokens.push_back({ASTTOK_PCT, TOK_TEXT()});
        }}
	goto st2;
tr20:
#line 105 "ast_scan.rl"
	{te = p;p--;{
            tokens.push_back({ASTTOK_PCT, TOK_TEXT()});
        }}
	goto st2;
tr21:
#line 102 "ast_scan.rl"
	{te = p+1;{
            tokens.push_back({ASTTOK_PCT, TOK_TEXT()});
        }}
	goto st2;
tr22:
#line 84 "ast_scan.rl"
	{te = p+1;{
            tokens.push_back({ASTTOK_PCT, TOK_TEXT()});
        }}
	goto st2;
tr28:
#line 99 "ast_scan.rl"
	{te = p;p--;{
            tokens.push_back({ASTTOK_PCT, TOK_TEXT()});
        }}
	goto st2;
tr30:
#line 78 "ast_scan.rl"
	{te = p;p--;{
            tokens.push_back({ASTTOK_PCT, TOK_TEXT()});
        }}
	goto st2;
tr31:
#line 75 "ast_scan.rl"
	{te = p+1;{
            tokens.push_back({ASTTOK_PCT, TOK_TEXT()});
        }}
	goto st2;
tr32:
#line 102 "ast_scan.rl"
	{te = p;p--;{
            tokens.push_back({ASTTOK_PCT, TOK_TEXT()});
        }}
	goto st2;
tr33:
#line 93 "ast_scan.rl"
	{te = p+1;{
            tokens.push_back({ASTTOK_PCT, TOK_TEXT()});
        }}
	goto st2;
tr35:
#line 72 "ast_scan.rl"
	{te = p;p--;{
            tokens.push_back({ASTTOK_PCT, TOK_TEXT()});
        }}
	goto st2;
tr36:
#line 69 "ast_scan.rl"
	{te = p+1;{
            tokens.push_back({ASTTOK_PCT, TOK_TEXT()});
        }}
	goto st2;
tr37:
#line 96 "ast_scan.rl"
	{te = p+1;{
            tokens.push_back({ASTTOK_PCT, TOK_TEXT()});
        }}
	goto st2;
tr38:
#line 87 "ast_scan.rl"
	{te = p+1;{
            tokens.push_back({ASTTOK_PCT, TOK_TEXT()});
        }}
	goto st2;
tr40:
#line 66 "ast_scan.rl"
	{te = p;p--;{
            tokens.push_back({ASTTOK_PCT, TOK_TEXT()});
        }}
	goto st2;
tr41:
#line 63 "ast_scan.rl"
	{te = p+1;{
            tokens.push_back({ASTTOK_PCT, TOK_TEXT()});
        }}
	goto st2;
tr42:
#line 90 "ast_scan.rl"
	{te = p+1;{
            tokens.push_back({ASTTOK_PCT, TOK_TEXT()});
        }}
	goto st2;
tr43:
#line 120 "ast_scan.rl"
	{te = p;p--;{
            tokens.push_back({ASTTOK_ESC, TOK_TEXT()});
        }}
	goto st2;
tr44:
#line 117 "ast_scan.rl"
	{te = p+1;{
            tokens.push_back({ASTTOK_ESC, TOK_TEXT()});
        }}
	goto st2;
st2:
#line 1 "NONE"
	{ts = 0;}
	if ( ++p == pe )
		goto _test_eof2;
case 2:
#line 1 "NONE"
	{ts = p;}
#line 237 "ast_scan.cpp"
	switch( (*p) ) {
		case 0u: goto st0;
		case 32u: goto st4;
		case 35u: goto st5;
		case 37u: goto st6;
		case 40u: goto tr6;
		case 41u: goto tr7;
		case 44u: goto tr8;
		case 59u: goto tr9;
		case 91u: goto tr10;
		case 92u: goto st15;
		case 93u: goto tr12;
		case 123u: goto tr13;
		case 125u: goto tr14;
	}
	goto tr1;
st0:
cs = 0;
	goto _out;
tr1:
#line 1 "NONE"
	{te = p+1;}
	goto st3;
st3:
	if ( ++p == pe )
		goto _test_eof3;
case 3:
#line 263 "ast_scan.cpp"
	switch( (*p) ) {
		case 0u: goto tr15;
		case 32u: goto tr15;
		case 35u: goto st1;
		case 37u: goto tr15;
		case 44u: goto tr15;
		case 59u: goto tr15;
		case 123u: goto tr15;
		case 125u: goto tr15;
	}
	if ( (*p) > 41u ) {
		if ( 91u <= (*p) && (*p) <= 93u )
			goto tr15;
	} else if ( (*p) >= 40u )
		goto tr15;
	goto tr1;
st1:
	if ( ++p == pe )
		goto _test_eof1;
case 1:
	switch( (*p) ) {
		case 0u: goto tr0;
		case 32u: goto tr0;
		case 44u: goto tr0;
		case 59u: goto tr0;
		case 64u: goto tr0;
		case 123u: goto tr0;
		case 125u: goto tr0;
	}
	if ( (*p) < 40u ) {
		if ( 35u <= (*p) && (*p) <= 37u )
			goto tr0;
	} else if ( (*p) > 41u ) {
		if ( 91u <= (*p) && (*p) <= 93u )
			goto tr0;
	} else
		goto tr0;
	goto tr1;
st4:
	if ( ++p == pe )
		goto _test_eof4;
case 4:
	if ( (*p) == 32u )
		goto st4;
	goto tr17;
st5:
	if ( ++p == pe )
		goto _test_eof5;
case 5:
	switch( (*p) ) {
		case 0u: goto tr18;
		case 32u: goto tr18;
		case 37u: goto tr18;
		case 44u: goto tr18;
		case 59u: goto tr18;
		case 64u: goto tr19;
		case 123u: goto tr18;
		case 125u: goto tr18;
	}
	if ( (*p) < 40u ) {
		if ( 35u <= (*p) && (*p) <= 36u )
			goto tr19;
	} else if ( (*p) > 41u ) {
		if ( 91u <= (*p) && (*p) <= 93u )
			goto tr18;
	} else
		goto tr18;
	goto tr1;
st6:
	if ( ++p == pe )
		goto _test_eof6;
case 6:
	switch( (*p) ) {
		case 61u: goto st7;
		case 67u: goto st9;
		case 73u: goto st11;
		case 81u: goto st12;
		case 86u: goto st14;
		case 88u: goto st9;
		case 99u: goto st9;
		case 105u: goto st11;
		case 113u: goto st12;
		case 118u: goto st14;
		case 120u: goto st9;
	}
	if ( 48u <= (*p) && (*p) <= 57u )
		goto tr22;
	goto tr21;
st7:
	if ( ++p == pe )
		goto _test_eof7;
case 7:
	if ( (*p) == 60u )
		goto st8;
	goto tr28;
st8:
	if ( ++p == pe )
		goto _test_eof8;
case 8:
	switch( (*p) ) {
		case 0u: goto tr30;
		case 41u: goto tr30;
		case 44u: goto tr30;
		case 62u: goto tr31;
		case 91u: goto tr30;
		case 93u: goto tr30;
		case 123u: goto tr30;
		case 125u: goto tr30;
	}
	goto st8;
st9:
	if ( ++p == pe )
		goto _test_eof9;
case 9:
	if ( (*p) == 60u )
		goto st10;
	goto tr33;
st10:
	if ( ++p == pe )
		goto _test_eof10;
case 10:
	switch( (*p) ) {
		case 0u: goto tr35;
		case 41u: goto tr35;
		case 44u: goto tr35;
		case 62u: goto tr36;
		case 91u: goto tr35;
		case 93u: goto tr35;
		case 123u: goto tr35;
		case 125u: goto tr35;
	}
	goto st10;
st11:
	if ( ++p == pe )
		goto _test_eof11;
case 11:
	if ( 48u <= (*p) && (*p) <= 57u )
		goto tr37;
	goto tr32;
st12:
	if ( ++p == pe )
		goto _test_eof12;
case 12:
	if ( (*p) == 60u )
		goto st13;
	goto tr38;
st13:
	if ( ++p == pe )
		goto _test_eof13;
case 13:
	switch( (*p) ) {
		case 0u: goto tr40;
		case 41u: goto tr40;
		case 44u: goto tr40;
		case 62u: goto tr41;
		case 91u: goto tr40;
		case 93u: goto tr40;
		case 123u: goto tr40;
		case 125u: goto tr40;
	}
	goto st13;
st14:
	if ( ++p == pe )
		goto _test_eof14;
case 14:
	if ( (*p) > 90u ) {
		if ( 97u <= (*p) && (*p) <= 122u )
			goto tr42;
	} else if ( (*p) >= 65u )
		goto tr42;
	goto tr32;
st15:
	if ( ++p == pe )
		goto _test_eof15;
case 15:
	goto tr44;
	}
	_test_eof2: cs = 2; goto _test_eof; 
	_test_eof3: cs = 3; goto _test_eof; 
	_test_eof1: cs = 1; goto _test_eof; 
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

	_test_eof: {}
	if ( p == eof )
	{
	switch ( cs ) {
	case 3: goto tr15;
	case 1: goto tr0;
	case 4: goto tr17;
	case 5: goto tr18;
	case 6: goto tr20;
	case 7: goto tr28;
	case 8: goto tr30;
	case 9: goto tr32;
	case 10: goto tr35;
	case 11: goto tr32;
	case 12: goto tr32;
	case 13: goto tr40;
	case 14: goto tr32;
	case 15: goto tr43;
	}
	}

	_out: {}
	}

#line 213 "ast_scan.rl"

    tokens.push_back({ASTTOK_EOF, ""});
    return tokens;
}
