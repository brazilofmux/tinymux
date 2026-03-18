// keybind.cpp -- Key binding implementation.
#include "keybind.h"
#include <algorithm>
#include <cctype>

void KeyBindings::bind(const BindKey& key, const std::string& command) {
    bindings_[key] = command;
}

bool KeyBindings::unbind(const BindKey& key) {
    return bindings_.erase(key) > 0;
}

const std::string* KeyBindings::find(const BindKey& key) const {
    auto it = bindings_.find(key);
    return it != bindings_.end() ? &it->second : nullptr;
}

BindKey event_to_bindkey(const InputEvent& ev) {
    BindKey bk;
    bk.type = ev.type;
    bk.cp = ev.codepoint;
    bk.ctrl = ev.ctrl;
    bk.alt = ev.alt;
    return bk;
}

BindKey parse_key_name(const std::string& name) {
    BindKey bk;
    std::string s = name;

    // Check for Ctrl- prefix
    if (s.size() > 5 && (s.substr(0, 5) == "Ctrl-" || s.substr(0, 5) == "ctrl-")) {
        bk.ctrl = true;
        s = s.substr(5);
    }
    // Check for Alt- prefix
    if (s.size() > 4 && (s.substr(0, 4) == "Alt-" || s.substr(0, 4) == "alt-")) {
        bk.alt = true;
        s = s.substr(4);
    }
    // Check for ^X notation
    if (s.size() == 2 && s[0] == '^') {
        bk.ctrl = true;
        bk.type = InputEvent::Char;
        bk.cp = std::toupper((unsigned char)s[1]) - 'A' + 1;
        return bk;
    }

    // Named keys
    std::string lower = s;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower == "up")        { bk.type = InputEvent::Key_Up; }
    else if (lower == "down")      { bk.type = InputEvent::Key_Down; }
    else if (lower == "left")      { bk.type = InputEvent::Key_Left; }
    else if (lower == "right")     { bk.type = InputEvent::Key_Right; }
    else if (lower == "home")      { bk.type = InputEvent::Key_Home; }
    else if (lower == "end")       { bk.type = InputEvent::Key_End; }
    else if (lower == "pgup" || lower == "pageup")   { bk.type = InputEvent::Key_PageUp; }
    else if (lower == "pgdn" || lower == "pagedown") { bk.type = InputEvent::Key_PageDown; }
    else if (lower == "backspace") { bk.type = InputEvent::Key_Backspace; }
    else if (lower == "delete" || lower == "del") { bk.type = InputEvent::Key_Delete; }
    else if (lower == "enter")     { bk.type = InputEvent::Key_Enter; }
    else if (lower == "tab")       { bk.type = InputEvent::Key_Tab; }
    else if (lower == "escape" || lower == "esc") { bk.type = InputEvent::Key_Escape; }
    else if (lower == "f1")  { bk.type = InputEvent::Key_F1; }
    else if (lower == "f2")  { bk.type = InputEvent::Key_F2; }
    else if (lower == "f3")  { bk.type = InputEvent::Key_F3; }
    else if (lower == "f4")  { bk.type = InputEvent::Key_F4; }
    else if (lower == "f5")  { bk.type = InputEvent::Key_F5; }
    else if (lower == "f6")  { bk.type = InputEvent::Key_F6; }
    else if (lower == "f7")  { bk.type = InputEvent::Key_F7; }
    else if (lower == "f8")  { bk.type = InputEvent::Key_F8; }
    else if (lower == "f9")  { bk.type = InputEvent::Key_F9; }
    else if (lower == "f10") { bk.type = InputEvent::Key_F10; }
    else if (lower == "f11") { bk.type = InputEvent::Key_F11; }
    else if (lower == "f12") { bk.type = InputEvent::Key_F12; }
    else if (s.size() == 1) {
        bk.type = InputEvent::Char;
        bk.cp = (unsigned char)s[0];
    }

    return bk;
}

std::string format_key_name(const BindKey& key) {
    std::string result;
    if (key.ctrl) result += "Ctrl-";
    if (key.alt) result += "Alt-";

    switch (key.type) {
    case InputEvent::Key_Up:        result += "Up"; break;
    case InputEvent::Key_Down:      result += "Down"; break;
    case InputEvent::Key_Left:      result += "Left"; break;
    case InputEvent::Key_Right:     result += "Right"; break;
    case InputEvent::Key_Home:      result += "Home"; break;
    case InputEvent::Key_End:       result += "End"; break;
    case InputEvent::Key_PageUp:    result += "PgUp"; break;
    case InputEvent::Key_PageDown:  result += "PgDn"; break;
    case InputEvent::Key_Backspace: result += "Backspace"; break;
    case InputEvent::Key_Delete:    result += "Delete"; break;
    case InputEvent::Key_Enter:     result += "Enter"; break;
    case InputEvent::Key_Tab:       result += "Tab"; break;
    case InputEvent::Key_Escape:    result += "Esc"; break;
    case InputEvent::Key_F1:  result += "F1"; break;
    case InputEvent::Key_F2:  result += "F2"; break;
    case InputEvent::Key_F3:  result += "F3"; break;
    case InputEvent::Key_F4:  result += "F4"; break;
    case InputEvent::Key_F5:  result += "F5"; break;
    case InputEvent::Key_F6:  result += "F6"; break;
    case InputEvent::Key_F7:  result += "F7"; break;
    case InputEvent::Key_F8:  result += "F8"; break;
    case InputEvent::Key_F9:  result += "F9"; break;
    case InputEvent::Key_F10: result += "F10"; break;
    case InputEvent::Key_F11: result += "F11"; break;
    case InputEvent::Key_F12: result += "F12"; break;
    case InputEvent::Char:
        if (key.ctrl && key.cp >= 1 && key.cp <= 26) {
            result = "^";
            result += (char)('A' + key.cp - 1);
        } else if (key.cp < 128) {
            result += (char)key.cp;
        } else {
            result += "U+" + std::to_string(key.cp);
        }
        break;
    default:
        result += "?";
        break;
    }
    return result;
}
