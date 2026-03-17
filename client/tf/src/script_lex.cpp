
#line 1 "script_lex.rl"
// script_lex.rl — Ragel -G2 expression tokenizer.
//
// Tokenizes TF expression language: numeric/string literals, identifiers,
// operators (arithmetic, comparison, logical, assignment, string),
// grouping, and substitution markers.
//
// Build: ragel -G2 -o script_lex.cpp script_lex.rl

#include "script.h"
#include <cstdlib>
#include <cstring>


#line 14 "script_lex.cpp"
static const int script_lex_start = 7;
static const int script_lex_error = -1;

static const int script_lex_en_main = 7;


#line 102 "script_lex.rl"


void ScriptLexer::emit(Tok t) {
    tokens_.push_back({t, {}, 0, 0.0});
}

void ScriptLexer::emit_int(const char* ts, const char* te) {
    Token tok;
    tok.type = Tok::INT_LIT;
    tok.ival = strtoll(ts, nullptr, 0);  // handles 0x prefix
    tokens_.push_back(tok);
}

void ScriptLexer::emit_float(const char* ts, const char* te) {
    Token tok;
    tok.type = Tok::FLOAT_LIT;
    tok.fval = strtod(ts, nullptr);
    tokens_.push_back(tok);
}

void ScriptLexer::emit_string(const char* ts, const char* te) {
    // Strip quotes, process escape sequences
    Token tok;
    tok.type = Tok::STRING_LIT;
    const char* p = ts + 1;       // skip opening quote
    const char* end = te - 1;     // before closing quote
    std::string& s = tok.sval;
    s.reserve(end - p);
    while (p < end) {
        if (*p == '\\' && p + 1 < end) {
            p++;
            switch (*p) {
                case 'n':  s += '\n'; break;
                case 't':  s += '\t'; break;
                case 'r':  s += '\r'; break;
                case '\\': s += '\\'; break;
                case '"':  s += '"';  break;
                case '\'': s += '\''; break;
                default:   s += '\\'; s += *p; break;
            }
        } else {
            s += *p;
        }
        p++;
    }
    tokens_.push_back(std::move(tok));
}

void ScriptLexer::emit_ident(const char* ts, const char* te) {
    Token tok;
    tok.type = Tok::IDENT;
    tok.sval.assign(ts, te - ts);
    tokens_.push_back(std::move(tok));
}

void ScriptLexer::tokenize(const char* data, size_t len) {
    tokens_.clear();

    if (len == 0) {
        tokens_.push_back({Tok::END, {}, 0, 0.0});
        return;
    }

    const char* p = data;
    const char* pe = data + len;
    const char* eof = pe;
    const char* ts;
    const char* te;
    int cs, act;

    (void)script_lex_en_main;  // suppress unused warning

    
#line 91 "script_lex.cpp"
	{
	cs = script_lex_start;
	ts = 0;
	te = 0;
	act = 0;
	}

#line 175 "script_lex.rl"
    
#line 97 "script_lex.cpp"
	{
	if ( p == pe )
		goto _test_eof;
	switch ( cs )
	{
tr0:
#line 99 "script_lex.rl"
	{{p = ((te))-1;}{ emit(Tok::ERROR); }}
	goto st7;
tr2:
#line 52 "script_lex.rl"
	{te = p+1;{ emit_string(ts, te); }}
	goto st7;
tr5:
#line 53 "script_lex.rl"
	{te = p+1;{ emit_string(ts, te); }}
	goto st7;
tr7:
#line 1 "NONE"
	{	switch( act ) {
	case 2:
	{{p = ((te))-1;} emit_float(ts, te); }
	break;
	case 4:
	{{p = ((te))-1;} emit_int(ts, te); }
	break;
	}
	}
	goto st7;
tr10:
#line 49 "script_lex.rl"
	{{p = ((te))-1;}{ emit_int(ts, te); }}
	goto st7;
tr12:
#line 99 "script_lex.rl"
	{te = p+1;{ emit(Tok::ERROR); }}
	goto st7;
tr17:
#line 82 "script_lex.rl"
	{te = p+1;{ emit(Tok::AND); }}
	goto st7;
tr19:
#line 85 "script_lex.rl"
	{te = p+1;{ emit(Tok::LPAREN); }}
	goto st7;
tr20:
#line 86 "script_lex.rl"
	{te = p+1;{ emit(Tok::RPAREN); }}
	goto st7;
tr23:
#line 88 "script_lex.rl"
	{te = p+1;{ emit(Tok::COMMA); }}
	goto st7;
tr33:
#line 89 "script_lex.rl"
	{te = p+1;{ emit(Tok::QUESTION); }}
	goto st7;
tr35:
#line 83 "script_lex.rl"
	{te = p+1;{ emit(Tok::OR); }}
	goto st7;
tr36:
#line 87 "script_lex.rl"
	{te = p+1;{ emit(Tok::RBRACE); }}
	goto st7;
tr37:
#line 44 "script_lex.rl"
	{te = p;p--;{ /* skip */ }}
	goto st7;
tr38:
#line 84 "script_lex.rl"
	{te = p;p--;{ emit(Tok::NOT); }}
	goto st7;
tr39:
#line 68 "script_lex.rl"
	{te = p+1;{ emit(Tok::NMATCH); }}
	goto st7;
tr40:
#line 62 "script_lex.rl"
	{te = p+1;{ emit(Tok::NE); }}
	goto st7;
tr41:
#line 66 "script_lex.rl"
	{te = p+1;{ emit(Tok::STRNE); }}
	goto st7;
tr42:
#line 99 "script_lex.rl"
	{te = p;p--;{ emit(Tok::ERROR); }}
	goto st7;
tr43:
#line 73 "script_lex.rl"
	{te = p+1;{ emit(Tok::PERCENT_LBRACE); }}
	goto st7;
tr44:
#line 78 "script_lex.rl"
	{te = p;p--;{ emit(Tok::STAR); }}
	goto st7;
tr45:
#line 59 "script_lex.rl"
	{te = p+1;{ emit(Tok::STAR_ASSIGN); }}
	goto st7;
tr46:
#line 76 "script_lex.rl"
	{te = p;p--;{ emit(Tok::PLUS); }}
	goto st7;
tr47:
#line 69 "script_lex.rl"
	{te = p+1;{ emit(Tok::INC); }}
	goto st7;
tr48:
#line 57 "script_lex.rl"
	{te = p+1;{ emit(Tok::PLUS_ASSIGN); }}
	goto st7;
tr49:
#line 77 "script_lex.rl"
	{te = p;p--;{ emit(Tok::MINUS); }}
	goto st7;
tr50:
#line 70 "script_lex.rl"
	{te = p+1;{ emit(Tok::DEC); }}
	goto st7;
tr51:
#line 58 "script_lex.rl"
	{te = p+1;{ emit(Tok::MINUS_ASSIGN); }}
	goto st7;
tr53:
#line 47 "script_lex.rl"
	{te = p;p--;{ emit_float(ts, te); }}
	goto st7;
tr55:
#line 79 "script_lex.rl"
	{te = p;p--;{ emit(Tok::SLASH); }}
	goto st7;
tr56:
#line 60 "script_lex.rl"
	{te = p+1;{ emit(Tok::SLASH_ASSIGN); }}
	goto st7;
tr57:
#line 49 "script_lex.rl"
	{te = p;p--;{ emit_int(ts, te); }}
	goto st7;
tr59:
#line 48 "script_lex.rl"
	{te = p;p--;{ emit_int(ts, te); }}
	goto st7;
tr60:
#line 90 "script_lex.rl"
	{te = p;p--;{ emit(Tok::COLON); }}
	goto st7;
tr61:
#line 56 "script_lex.rl"
	{te = p+1;{ emit(Tok::ASSIGN); }}
	goto st7;
tr62:
#line 80 "script_lex.rl"
	{te = p;p--;{ emit(Tok::LT); }}
	goto st7;
tr63:
#line 63 "script_lex.rl"
	{te = p+1;{ emit(Tok::LE); }}
	goto st7;
tr64:
#line 93 "script_lex.rl"
	{te = p;p--;{ emit(Tok::EQ); }}
	goto st7;
tr65:
#line 67 "script_lex.rl"
	{te = p+1;{ emit(Tok::MATCH); }}
	goto st7;
tr66:
#line 61 "script_lex.rl"
	{te = p+1;{ emit(Tok::EQ); }}
	goto st7;
tr67:
#line 65 "script_lex.rl"
	{te = p+1;{ emit(Tok::STREQ); }}
	goto st7;
tr68:
#line 81 "script_lex.rl"
	{te = p;p--;{ emit(Tok::GT); }}
	goto st7;
tr69:
#line 64 "script_lex.rl"
	{te = p+1;{ emit(Tok::GE); }}
	goto st7;
tr70:
#line 96 "script_lex.rl"
	{te = p;p--;{ emit_ident(ts, te); }}
	goto st7;
st7:
#line 1 "NONE"
	{ts = 0;}
	if ( ++p == pe )
		goto _test_eof7;
case 7:
#line 1 "NONE"
	{ts = p;}
#line 248 "script_lex.cpp"
	switch( (*p) ) {
		case 13: goto st8;
		case 32: goto st8;
		case 33: goto st9;
		case 34: goto tr15;
		case 37: goto st11;
		case 38: goto tr17;
		case 39: goto tr18;
		case 40: goto tr19;
		case 41: goto tr20;
		case 42: goto st13;
		case 43: goto st14;
		case 44: goto tr23;
		case 45: goto st15;
		case 46: goto st16;
		case 47: goto st19;
		case 48: goto tr27;
		case 58: goto st23;
		case 60: goto st24;
		case 61: goto st25;
		case 62: goto st26;
		case 63: goto tr33;
		case 95: goto st27;
		case 124: goto tr35;
		case 125: goto tr36;
	}
	if ( (*p) < 49 ) {
		if ( 9 <= (*p) && (*p) <= 10 )
			goto st8;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st27;
		} else if ( (*p) >= 65 )
			goto st27;
	} else
		goto tr28;
	goto tr12;
st8:
	if ( ++p == pe )
		goto _test_eof8;
case 8:
	switch( (*p) ) {
		case 13: goto st8;
		case 32: goto st8;
	}
	if ( 9 <= (*p) && (*p) <= 10 )
		goto st8;
	goto tr37;
st9:
	if ( ++p == pe )
		goto _test_eof9;
case 9:
	switch( (*p) ) {
		case 47: goto tr39;
		case 61: goto tr40;
		case 126: goto tr41;
	}
	goto tr38;
tr15:
#line 1 "NONE"
	{te = p+1;}
	goto st10;
st10:
	if ( ++p == pe )
		goto _test_eof10;
case 10:
#line 314 "script_lex.cpp"
	switch( (*p) ) {
		case 34: goto tr2;
		case 92: goto st1;
	}
	goto st0;
st0:
	if ( ++p == pe )
		goto _test_eof0;
case 0:
	switch( (*p) ) {
		case 34: goto tr2;
		case 92: goto st1;
	}
	goto st0;
st1:
	if ( ++p == pe )
		goto _test_eof1;
case 1:
	goto st0;
st11:
	if ( ++p == pe )
		goto _test_eof11;
case 11:
	if ( (*p) == 123 )
		goto tr43;
	goto tr42;
tr18:
#line 1 "NONE"
	{te = p+1;}
	goto st12;
st12:
	if ( ++p == pe )
		goto _test_eof12;
case 12:
#line 347 "script_lex.cpp"
	switch( (*p) ) {
		case 39: goto tr5;
		case 92: goto st3;
	}
	goto st2;
st2:
	if ( ++p == pe )
		goto _test_eof2;
case 2:
	switch( (*p) ) {
		case 39: goto tr5;
		case 92: goto st3;
	}
	goto st2;
st3:
	if ( ++p == pe )
		goto _test_eof3;
case 3:
	goto st2;
st13:
	if ( ++p == pe )
		goto _test_eof13;
case 13:
	if ( (*p) == 61 )
		goto tr45;
	goto tr44;
st14:
	if ( ++p == pe )
		goto _test_eof14;
case 14:
	switch( (*p) ) {
		case 43: goto tr47;
		case 61: goto tr48;
	}
	goto tr46;
st15:
	if ( ++p == pe )
		goto _test_eof15;
case 15:
	switch( (*p) ) {
		case 45: goto tr50;
		case 61: goto tr51;
	}
	goto tr49;
st16:
	if ( ++p == pe )
		goto _test_eof16;
case 16:
	if ( 48 <= (*p) && (*p) <= 57 )
		goto tr52;
	goto tr42;
tr52:
#line 1 "NONE"
	{te = p+1;}
#line 47 "script_lex.rl"
	{act = 2;}
	goto st17;
st17:
	if ( ++p == pe )
		goto _test_eof17;
case 17:
#line 406 "script_lex.cpp"
	switch( (*p) ) {
		case 69: goto st4;
		case 101: goto st4;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto tr52;
	goto tr53;
st4:
	if ( ++p == pe )
		goto _test_eof4;
case 4:
	switch( (*p) ) {
		case 43: goto st5;
		case 45: goto st5;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st18;
	goto tr7;
st5:
	if ( ++p == pe )
		goto _test_eof5;
case 5:
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st18;
	goto tr7;
st18:
	if ( ++p == pe )
		goto _test_eof18;
case 18:
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st18;
	goto tr53;
st19:
	if ( ++p == pe )
		goto _test_eof19;
case 19:
	if ( (*p) == 61 )
		goto tr56;
	goto tr55;
tr27:
#line 1 "NONE"
	{te = p+1;}
#line 49 "script_lex.rl"
	{act = 4;}
	goto st20;
st20:
	if ( ++p == pe )
		goto _test_eof20;
case 20:
#line 453 "script_lex.cpp"
	switch( (*p) ) {
		case 46: goto tr52;
		case 69: goto st4;
		case 88: goto st6;
		case 101: goto st4;
		case 120: goto st6;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto tr28;
	goto tr57;
tr28:
#line 1 "NONE"
	{te = p+1;}
#line 49 "script_lex.rl"
	{act = 4;}
	goto st21;
st21:
	if ( ++p == pe )
		goto _test_eof21;
case 21:
#line 471 "script_lex.cpp"
	switch( (*p) ) {
		case 46: goto tr52;
		case 69: goto st4;
		case 101: goto st4;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto tr28;
	goto tr57;
st6:
	if ( ++p == pe )
		goto _test_eof6;
case 6:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st22;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st22;
	} else
		goto st22;
	goto tr10;
st22:
	if ( ++p == pe )
		goto _test_eof22;
case 22:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st22;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st22;
	} else
		goto st22;
	goto tr59;
st23:
	if ( ++p == pe )
		goto _test_eof23;
case 23:
	if ( (*p) == 61 )
		goto tr61;
	goto tr60;
st24:
	if ( ++p == pe )
		goto _test_eof24;
case 24:
	if ( (*p) == 61 )
		goto tr63;
	goto tr62;
st25:
	if ( ++p == pe )
		goto _test_eof25;
case 25:
	switch( (*p) ) {
		case 47: goto tr65;
		case 61: goto tr66;
		case 126: goto tr67;
	}
	goto tr64;
st26:
	if ( ++p == pe )
		goto _test_eof26;
case 26:
	if ( (*p) == 61 )
		goto tr69;
	goto tr68;
st27:
	if ( ++p == pe )
		goto _test_eof27;
case 27:
	if ( (*p) == 95 )
		goto st27;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st27;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st27;
	} else
		goto st27;
	goto tr70;
	}
	_test_eof7: cs = 7; goto _test_eof; 
	_test_eof8: cs = 8; goto _test_eof; 
	_test_eof9: cs = 9; goto _test_eof; 
	_test_eof10: cs = 10; goto _test_eof; 
	_test_eof0: cs = 0; goto _test_eof; 
	_test_eof1: cs = 1; goto _test_eof; 
	_test_eof11: cs = 11; goto _test_eof; 
	_test_eof12: cs = 12; goto _test_eof; 
	_test_eof2: cs = 2; goto _test_eof; 
	_test_eof3: cs = 3; goto _test_eof; 
	_test_eof13: cs = 13; goto _test_eof; 
	_test_eof14: cs = 14; goto _test_eof; 
	_test_eof15: cs = 15; goto _test_eof; 
	_test_eof16: cs = 16; goto _test_eof; 
	_test_eof17: cs = 17; goto _test_eof; 
	_test_eof4: cs = 4; goto _test_eof; 
	_test_eof5: cs = 5; goto _test_eof; 
	_test_eof18: cs = 18; goto _test_eof; 
	_test_eof19: cs = 19; goto _test_eof; 
	_test_eof20: cs = 20; goto _test_eof; 
	_test_eof21: cs = 21; goto _test_eof; 
	_test_eof6: cs = 6; goto _test_eof; 
	_test_eof22: cs = 22; goto _test_eof; 
	_test_eof23: cs = 23; goto _test_eof; 
	_test_eof24: cs = 24; goto _test_eof; 
	_test_eof25: cs = 25; goto _test_eof; 
	_test_eof26: cs = 26; goto _test_eof; 
	_test_eof27: cs = 27; goto _test_eof; 

	_test_eof: {}
	if ( p == eof )
	{
	switch ( cs ) {
	case 8: goto tr37;
	case 9: goto tr38;
	case 10: goto tr42;
	case 0: goto tr0;
	case 1: goto tr0;
	case 11: goto tr42;
	case 12: goto tr42;
	case 2: goto tr0;
	case 3: goto tr0;
	case 13: goto tr44;
	case 14: goto tr46;
	case 15: goto tr49;
	case 16: goto tr42;
	case 17: goto tr53;
	case 4: goto tr7;
	case 5: goto tr7;
	case 18: goto tr53;
	case 19: goto tr55;
	case 20: goto tr57;
	case 21: goto tr57;
	case 6: goto tr10;
	case 22: goto tr59;
	case 23: goto tr60;
	case 24: goto tr62;
	case 25: goto tr64;
	case 26: goto tr68;
	case 27: goto tr70;
	}
	}

	}

#line 176 "script_lex.rl"

    // Append END sentinel
    tokens_.push_back({Tok::END, {}, 0, 0.0});
}
