// input_lex.rl — Ragel -G2 terminal input scanner.
//
// Single-pass goto-driven DFA that tokenizes raw terminal bytes into
// InputEvent sequences.  Uses Ragel's scanner (|* *|) for longest-match
// tokenization with automatic backtracking.
//
// Build: ragel -G2 -o input_lex.cpp input_lex.rl

#include "input.h"
#include <cstring>

%%{
    machine input_lex;
    alphtype unsigned char;

    # --- UTF-8 byte patterns (strict RFC 3629) ---
    cont   = 0x80..0xBF ;
    utf8_2 = 0xC2..0xDF cont ;
    utf8_3 = (0xE0 0xA0..0xBF | 0xE1..0xEC 0x80..0xBF | 0xED 0x80..0x9F | 0xEE..0xEF 0x80..0xBF) cont ;
    utf8_4 = (0xF0 0x90..0xBF | 0xF1..0xF3 0x80..0xBF | 0xF4 0x80..0x8F) cont cont ;

    # --- CSI sequence structure ---
    # ESC [ (parameter bytes)* (intermediate bytes)* (final byte)
    csi_pbyte = 0x30..0x3F ;
    csi_ibyte = 0x20..0x2F ;
    csi_final = 0x40..0x7E ;
    csi_seq   = 0x1B '[' csi_pbyte* csi_ibyte* csi_final ;

    # --- SS3 sequence: ESC O followed by a single char ---
    ss3_seq = 0x1B 'O' 0x40..0x7E ;

    write data nofinal;

    main := |*

        # CSI sequence — ts points to ESC, te points past final byte
        csi_seq => {
            dispatch_csi(ts + 2, te - 1);
        };

        # SS3 sequence — final byte is at te-1
        ss3_seq => {
            dispatch_ss3(*(te - 1));
        };

        # UTF-8 multibyte characters — ts points to first byte
        utf8_2 => {
            uint32_t cp = ((uint32_t)(ts[0] & 0x1F) << 6)
                        |  (uint32_t)(ts[1] & 0x3F);
            emit_char(cp);
        };
        utf8_3 => {
            uint32_t cp = ((uint32_t)(ts[0] & 0x0F) << 12)
                        | ((uint32_t)(ts[1] & 0x3F) << 6)
                        |  (uint32_t)(ts[2] & 0x3F);
            emit_char(cp);
        };
        utf8_4 => {
            uint32_t cp = ((uint32_t)(ts[0] & 0x07) << 18)
                        | ((uint32_t)(ts[1] & 0x3F) << 12)
                        | ((uint32_t)(ts[2] & 0x3F) << 6)
                        |  (uint32_t)(ts[3] & 0x3F);
            emit_char(cp);
        };

        # Printable ASCII
        0x20..0x7E => {
            emit_char((uint32_t)(*ts));
        };

        # Control characters with specific mappings
        0x0D => { emit(Key::ENTER); };
        0x0A => { emit(Key::ENTER); };
        0x09 => { emit(Key::TAB); };
        0x7F => { emit(Key::BACKSPACE); };
        0x08 => { emit(Key::BACKSPACE); };

        # Ctrl-letter (0x01-0x1A), excluding those mapped above
        0x01 => { emit(Key::CTRL_A); };
        0x02 => { emit(Key::CTRL_B); };
        0x03 => { emit(Key::CTRL_C); };
        0x04 => { emit(Key::CTRL_D); };
        0x05 => { emit(Key::CTRL_E); };
        0x06 => { emit(Key::CTRL_F); };
        0x07 => { emit(Key::CTRL_G); };
        0x0B => { emit(Key::CTRL_K); };
        0x0C => { emit(Key::CTRL_L); };
        0x0E => { emit(Key::CTRL_N); };
        0x0F => { emit(Key::CTRL_O); };
        0x10 => { emit(Key::CTRL_P); };
        0x11 => { emit(Key::CTRL_Q); };
        0x12 => { emit(Key::CTRL_R); };
        0x13 => { emit(Key::CTRL_S); };
        0x14 => { emit(Key::CTRL_T); };
        0x15 => { emit(Key::CTRL_U); };
        0x16 => { emit(Key::CTRL_V); };
        0x17 => { emit(Key::CTRL_W); };
        0x18 => { emit(Key::CTRL_X); };
        0x19 => { emit(Key::CTRL_Y); };
        0x1A => { emit(Key::CTRL_Z); };

        # Bare ESC (normally held back by feed(), but catch as safety)
        0x1B => { emit(Key::ESCAPE); };

        # Catch stray bytes
        any => { /* discard */ };

    *|;
}%%

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

    // Modifier in params[1]: 2=Shift, 3=Alt, 4=Alt+Shift, 5=Ctrl, 6=Ctrl+Shift
    int modifier = (nparam >= 2) ? params[1] : 0;
    bool ctrl = (modifier == 5 || modifier == 6);
    bool alt  = (modifier == 3 || modifier == 4);

    // Alt/Meta modifier: emit ESCAPE prefix so multi-key bindings
    // like "Esc Left" match when the terminal sends CSI 1;3 D.
    if (alt) emit(Key::ESCAPE);

    switch (final_byte) {
        case 'A': emit(ctrl ? Key::CTRL_UP   : Key::UP);    break;
        case 'B': emit(ctrl ? Key::CTRL_DOWN : Key::DOWN);  break;
        case 'C': emit(ctrl ? Key::CTRL_RIGHT : Key::RIGHT); break;
        case 'D': emit(ctrl ? Key::CTRL_LEFT  : Key::LEFT);  break;
        case 'H': emit(ctrl ? Key::CTRL_HOME : Key::HOME);  break;
        case 'F': emit(ctrl ? Key::CTRL_END  : Key::END);   break;
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

    %% write init;
    %% write exec;
}
