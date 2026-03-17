
#line 1 "input_lex.rl"
// input_lex.rl — Ragel -G2 terminal input scanner.
//
// Single-pass goto-driven DFA that tokenizes raw terminal bytes into
// InputEvent sequences.  Uses Ragel's scanner (|* *|) for longest-match
// tokenization with automatic backtracking.
//
// Build: ragel -G2 -o input_lex.cpp input_lex.rl

#include "input.h"
#include <cstring>


#line 13 "input_lex.cpp"
static const int input_lex_start = 6;
static const int input_lex_error = -1;

static const int input_lex_en_main = 6;


#line 109 "input_lex.rl"


// --- C++ implementation ---

InputLexer::InputLexer() {
    (void)input_lex_en_main;   // suppress unused warning
}

void InputLexer::emit(Key k) {
    events_.push_back({k, 0});
}

void InputLexer::emit_char(uint32_t cp) {
    events_.push_back({Key::CHAR, cp});
}

void InputLexer::flush_pending_esc() {
    if (pending_esc_) {
        pending_esc_ = false;
        emit(Key::ESCAPE);
    }
}

// Dispatch CSI: `start` = first param byte (after ESC [), `final_p` = final byte.
void InputLexer::dispatch_csi(const unsigned char* start, const unsigned char* final_p) {
    int params[8] = {};
    int nparam = 0;
    int cur = 0;
    bool has = false;

    unsigned char final_byte = *final_p;

    for (const unsigned char* q = start; q < final_p && nparam < 8; q++) {
        if (*q >= '0' && *q <= '9') {
            cur = cur * 10 + (*q - '0');
            has = true;
        } else if (*q == ';') {
            params[nparam++] = has ? cur : 0;
            cur = 0;
            has = false;
        } else {
            break;  // intermediate or private byte — stop param parsing
        }
    }
    if (has && nparam < 8) params[nparam++] = cur;

    // Modifier in params[1]: 2=Shift, 3=Alt, 5=Ctrl, 6=Ctrl+Shift
    bool ctrl = (nparam >= 2 && (params[1] == 5 || params[1] == 6));

    switch (final_byte) {
        case 'A': emit(Key::UP);    break;
        case 'B': emit(Key::DOWN);  break;
        case 'C': emit(ctrl ? Key::CTRL_RIGHT : Key::RIGHT); break;
        case 'D': emit(ctrl ? Key::CTRL_LEFT  : Key::LEFT);  break;
        case 'H': emit(Key::HOME);  break;
        case 'F': emit(Key::END);   break;
        case '~':
            switch (params[0]) {
                case 1:  emit(Key::HOME);       break;
                case 2:  emit(Key::INSERT);     break;
                case 3:  emit(Key::DELETE_KEY);  break;
                case 4:  emit(Key::END);        break;
                case 5:  emit(Key::PAGE_UP);    break;
                case 6:  emit(Key::PAGE_DOWN);  break;
                case 11: emit(Key::F1);         break;
                case 12: emit(Key::F2);         break;
                case 13: emit(Key::F3);         break;
                case 14: emit(Key::F4);         break;
                case 15: emit(Key::F5);         break;
                case 17: emit(Key::F6);         break;
                case 18: emit(Key::F7);         break;
                case 19: emit(Key::F8);         break;
                case 20: emit(Key::F9);         break;
                case 21: emit(Key::F10);        break;
                case 23: emit(Key::F11);        break;
                case 24: emit(Key::F12);        break;
                default: emit(Key::UNKNOWN);    break;
            }
            break;
        default:
            emit(Key::UNKNOWN);
            break;
    }
}

void InputLexer::dispatch_ss3(unsigned char ch) {
    switch (ch) {
        case 'P': emit(Key::F1);   break;
        case 'Q': emit(Key::F2);   break;
        case 'R': emit(Key::F3);   break;
        case 'S': emit(Key::F4);   break;
        case 'H': emit(Key::HOME); break;
        case 'F': emit(Key::END);  break;
        default:  emit(Key::UNKNOWN); break;
    }
}

void InputLexer::feed(const unsigned char* data, size_t len) {
    if (len == 0 && !pending_esc_) return;

    // Prepend held-back ESC if new data arrived.
    std::vector<unsigned char> combined;
    const unsigned char* real_data = data;
    size_t real_len = len;

    if (pending_esc_ && len > 0) {
        pending_esc_ = false;
        combined.reserve(1 + len);
        combined.push_back(0x1B);
        combined.insert(combined.end(), data, data + len);
        real_data = combined.data();
        real_len = combined.size();
    }

    // Hold back a trailing bare ESC for disambiguation.
    if (real_len > 0 && real_data[real_len - 1] == 0x1B) {
        if (real_len == 1) {
            pending_esc_ = true;
            return;
        }
        if (real_data[real_len - 2] != 0x1B) {
            pending_esc_ = true;
            real_len--;
        }
    }

    if (real_len == 0) return;

    // Run the Ragel scanner.
    const unsigned char* p = real_data;
    const unsigned char* pe = real_data + real_len;
    const unsigned char* eof = nullptr;
    const unsigned char* ts;
    const unsigned char* te;
    int cs, act;

    
#line 154 "input_lex.cpp"
	{
	cs = input_lex_start;
	ts = 0;
	te = 0;
	act = 0;
	}

#line 246 "input_lex.rl"
    
#line 160 "input_lex.cpp"
	{
	if ( p == pe )
		goto _test_eof;
	switch ( cs )
	{
tr0:
#line 103 "input_lex.rl"
	{{p = ((te))-1;}{ emit(Key::ESCAPE); }}
	goto st6;
tr1:
#line 42 "input_lex.rl"
	{te = p+1;{
            dispatch_ss3(*(te - 1));
        }}
	goto st6;
tr4:
#line 37 "input_lex.rl"
	{te = p+1;{
            dispatch_csi(ts + 2, te - 1);
        }}
	goto st6;
tr5:
#line 106 "input_lex.rl"
	{{p = ((te))-1;}{ /* discard */ }}
	goto st6;
tr6:
#line 52 "input_lex.rl"
	{te = p+1;{
            uint32_t cp = ((uint32_t)(ts[0] & 0x0F) << 12)
                        | ((uint32_t)(ts[1] & 0x3F) << 6)
                        |  (uint32_t)(ts[2] & 0x3F);
            emit_char(cp);
        }}
	goto st6;
tr8:
#line 58 "input_lex.rl"
	{te = p+1;{
            uint32_t cp = ((uint32_t)(ts[0] & 0x07) << 18)
                        | ((uint32_t)(ts[1] & 0x3F) << 12)
                        | ((uint32_t)(ts[2] & 0x3F) << 6)
                        |  (uint32_t)(ts[3] & 0x3F);
            emit_char(cp);
        }}
	goto st6;
tr9:
#line 106 "input_lex.rl"
	{te = p+1;{ /* discard */ }}
	goto st6;
tr10:
#line 79 "input_lex.rl"
	{te = p+1;{ emit(Key::CTRL_A); }}
	goto st6;
tr11:
#line 80 "input_lex.rl"
	{te = p+1;{ emit(Key::CTRL_B); }}
	goto st6;
tr12:
#line 81 "input_lex.rl"
	{te = p+1;{ emit(Key::CTRL_C); }}
	goto st6;
tr13:
#line 82 "input_lex.rl"
	{te = p+1;{ emit(Key::CTRL_D); }}
	goto st6;
tr14:
#line 83 "input_lex.rl"
	{te = p+1;{ emit(Key::CTRL_E); }}
	goto st6;
tr15:
#line 84 "input_lex.rl"
	{te = p+1;{ emit(Key::CTRL_F); }}
	goto st6;
tr16:
#line 85 "input_lex.rl"
	{te = p+1;{ emit(Key::CTRL_G); }}
	goto st6;
tr17:
#line 76 "input_lex.rl"
	{te = p+1;{ emit(Key::BACKSPACE); }}
	goto st6;
tr18:
#line 74 "input_lex.rl"
	{te = p+1;{ emit(Key::TAB); }}
	goto st6;
tr19:
#line 73 "input_lex.rl"
	{te = p+1;{ emit(Key::ENTER); }}
	goto st6;
tr20:
#line 86 "input_lex.rl"
	{te = p+1;{ emit(Key::CTRL_K); }}
	goto st6;
tr21:
#line 87 "input_lex.rl"
	{te = p+1;{ emit(Key::CTRL_L); }}
	goto st6;
tr22:
#line 72 "input_lex.rl"
	{te = p+1;{ emit(Key::ENTER); }}
	goto st6;
tr23:
#line 88 "input_lex.rl"
	{te = p+1;{ emit(Key::CTRL_N); }}
	goto st6;
tr24:
#line 89 "input_lex.rl"
	{te = p+1;{ emit(Key::CTRL_O); }}
	goto st6;
tr25:
#line 90 "input_lex.rl"
	{te = p+1;{ emit(Key::CTRL_P); }}
	goto st6;
tr26:
#line 91 "input_lex.rl"
	{te = p+1;{ emit(Key::CTRL_Q); }}
	goto st6;
tr27:
#line 92 "input_lex.rl"
	{te = p+1;{ emit(Key::CTRL_R); }}
	goto st6;
tr28:
#line 93 "input_lex.rl"
	{te = p+1;{ emit(Key::CTRL_S); }}
	goto st6;
tr29:
#line 94 "input_lex.rl"
	{te = p+1;{ emit(Key::CTRL_T); }}
	goto st6;
tr30:
#line 95 "input_lex.rl"
	{te = p+1;{ emit(Key::CTRL_U); }}
	goto st6;
tr31:
#line 96 "input_lex.rl"
	{te = p+1;{ emit(Key::CTRL_V); }}
	goto st6;
tr32:
#line 97 "input_lex.rl"
	{te = p+1;{ emit(Key::CTRL_W); }}
	goto st6;
tr33:
#line 98 "input_lex.rl"
	{te = p+1;{ emit(Key::CTRL_X); }}
	goto st6;
tr34:
#line 99 "input_lex.rl"
	{te = p+1;{ emit(Key::CTRL_Y); }}
	goto st6;
tr35:
#line 100 "input_lex.rl"
	{te = p+1;{ emit(Key::CTRL_Z); }}
	goto st6;
tr37:
#line 67 "input_lex.rl"
	{te = p+1;{
            emit_char((uint32_t)(*ts));
        }}
	goto st6;
tr38:
#line 75 "input_lex.rl"
	{te = p+1;{ emit(Key::BACKSPACE); }}
	goto st6;
tr46:
#line 103 "input_lex.rl"
	{te = p;p--;{ emit(Key::ESCAPE); }}
	goto st6;
tr48:
#line 106 "input_lex.rl"
	{te = p;p--;{ /* discard */ }}
	goto st6;
tr49:
#line 47 "input_lex.rl"
	{te = p+1;{
            uint32_t cp = ((uint32_t)(ts[0] & 0x1F) << 6)
                        |  (uint32_t)(ts[1] & 0x3F);
            emit_char(cp);
        }}
	goto st6;
st6:
#line 1 "NONE"
	{ts = 0;}
	if ( ++p == pe )
		goto _test_eof6;
case 6:
#line 1 "NONE"
	{ts = p;}
#line 306 "input_lex.cpp"
	switch( (*p) ) {
		case 1u: goto tr10;
		case 2u: goto tr11;
		case 3u: goto tr12;
		case 4u: goto tr13;
		case 5u: goto tr14;
		case 6u: goto tr15;
		case 7u: goto tr16;
		case 8u: goto tr17;
		case 9u: goto tr18;
		case 10u: goto tr19;
		case 11u: goto tr20;
		case 12u: goto tr21;
		case 13u: goto tr22;
		case 14u: goto tr23;
		case 15u: goto tr24;
		case 16u: goto tr25;
		case 17u: goto tr26;
		case 18u: goto tr27;
		case 19u: goto tr28;
		case 20u: goto tr29;
		case 21u: goto tr30;
		case 22u: goto tr31;
		case 23u: goto tr32;
		case 24u: goto tr33;
		case 25u: goto tr34;
		case 26u: goto tr35;
		case 27u: goto tr36;
		case 127u: goto tr38;
		case 224u: goto tr40;
		case 237u: goto tr42;
		case 240u: goto tr43;
		case 244u: goto tr45;
	}
	if ( (*p) < 194u ) {
		if ( (*p) > 31u ) {
			if ( 128u <= (*p) && (*p) <= 193u )
				goto tr9;
		} else
			goto tr9;
	} else if ( (*p) > 223u ) {
		if ( (*p) < 241u ) {
			if ( 225u <= (*p) && (*p) <= 239u )
				goto tr41;
		} else if ( (*p) > 243u ) {
			if ( 245u <= (*p) )
				goto tr9;
		} else
			goto tr44;
	} else
		goto st8;
	goto tr37;
tr36:
#line 1 "NONE"
	{te = p+1;}
	goto st7;
st7:
	if ( ++p == pe )
		goto _test_eof7;
case 7:
#line 365 "input_lex.cpp"
	switch( (*p) ) {
		case 79u: goto st0;
		case 91u: goto st1;
	}
	goto tr46;
st0:
	if ( ++p == pe )
		goto _test_eof0;
case 0:
	if ( 64u <= (*p) && (*p) <= 126u )
		goto tr1;
	goto tr0;
st1:
	if ( ++p == pe )
		goto _test_eof1;
case 1:
	if ( (*p) < 48u ) {
		if ( 32u <= (*p) && (*p) <= 47u )
			goto st2;
	} else if ( (*p) > 63u ) {
		if ( 64u <= (*p) && (*p) <= 126u )
			goto tr4;
	} else
		goto st1;
	goto tr0;
st2:
	if ( ++p == pe )
		goto _test_eof2;
case 2:
	if ( (*p) > 47u ) {
		if ( 64u <= (*p) && (*p) <= 126u )
			goto tr4;
	} else if ( (*p) >= 32u )
		goto st2;
	goto tr0;
st8:
	if ( ++p == pe )
		goto _test_eof8;
case 8:
	if ( 128u <= (*p) && (*p) <= 191u )
		goto tr49;
	goto tr48;
tr40:
#line 1 "NONE"
	{te = p+1;}
	goto st9;
st9:
	if ( ++p == pe )
		goto _test_eof9;
case 9:
#line 414 "input_lex.cpp"
	if ( 160u <= (*p) && (*p) <= 191u )
		goto st3;
	goto tr48;
st3:
	if ( ++p == pe )
		goto _test_eof3;
case 3:
	if ( 128u <= (*p) && (*p) <= 191u )
		goto tr6;
	goto tr5;
tr41:
#line 1 "NONE"
	{te = p+1;}
	goto st10;
st10:
	if ( ++p == pe )
		goto _test_eof10;
case 10:
#line 431 "input_lex.cpp"
	if ( 128u <= (*p) && (*p) <= 191u )
		goto st3;
	goto tr48;
tr42:
#line 1 "NONE"
	{te = p+1;}
	goto st11;
st11:
	if ( ++p == pe )
		goto _test_eof11;
case 11:
#line 441 "input_lex.cpp"
	if ( 128u <= (*p) && (*p) <= 159u )
		goto st3;
	goto tr48;
tr43:
#line 1 "NONE"
	{te = p+1;}
	goto st12;
st12:
	if ( ++p == pe )
		goto _test_eof12;
case 12:
#line 451 "input_lex.cpp"
	if ( 144u <= (*p) && (*p) <= 191u )
		goto st4;
	goto tr48;
st4:
	if ( ++p == pe )
		goto _test_eof4;
case 4:
	if ( 128u <= (*p) && (*p) <= 191u )
		goto st5;
	goto tr5;
st5:
	if ( ++p == pe )
		goto _test_eof5;
case 5:
	if ( 128u <= (*p) && (*p) <= 191u )
		goto tr8;
	goto tr5;
tr44:
#line 1 "NONE"
	{te = p+1;}
	goto st13;
st13:
	if ( ++p == pe )
		goto _test_eof13;
case 13:
#line 475 "input_lex.cpp"
	if ( 128u <= (*p) && (*p) <= 191u )
		goto st4;
	goto tr48;
tr45:
#line 1 "NONE"
	{te = p+1;}
	goto st14;
st14:
	if ( ++p == pe )
		goto _test_eof14;
case 14:
#line 485 "input_lex.cpp"
	if ( 128u <= (*p) && (*p) <= 143u )
		goto st4;
	goto tr48;
	}
	_test_eof6: cs = 6; goto _test_eof; 
	_test_eof7: cs = 7; goto _test_eof; 
	_test_eof0: cs = 0; goto _test_eof; 
	_test_eof1: cs = 1; goto _test_eof; 
	_test_eof2: cs = 2; goto _test_eof; 
	_test_eof8: cs = 8; goto _test_eof; 
	_test_eof9: cs = 9; goto _test_eof; 
	_test_eof3: cs = 3; goto _test_eof; 
	_test_eof10: cs = 10; goto _test_eof; 
	_test_eof11: cs = 11; goto _test_eof; 
	_test_eof12: cs = 12; goto _test_eof; 
	_test_eof4: cs = 4; goto _test_eof; 
	_test_eof5: cs = 5; goto _test_eof; 
	_test_eof13: cs = 13; goto _test_eof; 
	_test_eof14: cs = 14; goto _test_eof; 

	_test_eof: {}
	if ( p == eof )
	{
	switch ( cs ) {
	case 7: goto tr46;
	case 0: goto tr0;
	case 1: goto tr0;
	case 2: goto tr0;
	case 8: goto tr48;
	case 9: goto tr48;
	case 3: goto tr5;
	case 10: goto tr48;
	case 11: goto tr48;
	case 12: goto tr48;
	case 4: goto tr5;
	case 5: goto tr5;
	case 13: goto tr48;
	case 14: goto tr48;
	}
	}

	}

#line 247 "input_lex.rl"
}
