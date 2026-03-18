// macro.cpp -- Trigger and macro implementation.
#include "macro.h"
#include "app.h"
#include <algorithm>
#include <sstream>

void Macro::compile() {
    if (trigger.empty()) {
        compiled = false;
        return;
    }
    try {
        trigger_re = std::regex(trigger, std::regex::ECMAScript | std::regex::icase);
        compiled = true;
    } catch (...) {
        compiled = false;
    }
}

void MacroDB::define(Macro m) {
    m.compile();
    // Replace existing macro with same name
    for (auto& existing : macros_) {
        if (existing.name == m.name) {
            existing = std::move(m);
            return;
        }
    }
    macros_.push_back(std::move(m));
}

bool MacroDB::undef(const std::string& name) {
    for (auto it = macros_.begin(); it != macros_.end(); ++it) {
        if (it->name == name) {
            macros_.erase(it);
            return true;
        }
    }
    return false;
}

Macro* MacroDB::find(const std::string& name) {
    for (auto& m : macros_) {
        if (m.name == name) return &m;
    }
    return nullptr;
}

std::vector<Macro*> MacroDB::match_triggers(const std::string& text) {
    std::vector<Macro*> result;
    for (auto& m : macros_) {
        if (!m.compiled || m.trigger.empty()) continue;
        if (m.shots == 0) continue;
        try {
            if (std::regex_search(text, m.trigger_re)) {
                result.push_back(&m);
            }
        } catch (...) {}
    }
    // Sort by priority (higher first)
    std::sort(result.begin(), result.end(),
              [](const Macro* a, const Macro* b) {
                  return a->priority > b->priority;
              });
    return result;
}

TriggerResult check_triggers(App& app, std::string& text) {
    TriggerResult result;
    auto matches = app.macros.match_triggers(text);
    for (auto* m : matches) {
        result.matched = true;
        if (m->gag) result.gagged = true;

        // Execute the macro body as a command
        if (!m->body.empty()) {
            if (m->body[0] == '/') {
                app.commands.dispatch(app, m->body);
            } else if (app.fg) {
                app_send_line(app, app.fg, m->body);
            }
        }

        // Decrement shots
        if (m->shots > 0) {
            m->shots--;
        }
    }
    return result;
}

// Parse /def flags:
//   /def [name] -t'pattern' [-p priority] [-n shots] [-g] [-h] body
bool parse_def(const std::string& args, Macro& out, std::string& error) {
    // Simple parser: tokenize by spaces, handle -flags
    std::istringstream ss(args);
    std::string token;
    std::vector<std::string> body_parts;
    bool have_name = false;

    while (ss >> token) {
        if (token[0] == '-' && token.size() >= 2) {
            char flag = token[1];
            switch (flag) {
            case 't': {
                // -t'pattern' or -t pattern
                std::string pattern;
                if (token.size() > 2) {
                    pattern = token.substr(2);
                    // Strip surrounding quotes
                    if (!pattern.empty() && (pattern[0] == '\'' || pattern[0] == '"')) {
                        char q = pattern[0];
                        pattern = pattern.substr(1);
                        size_t end = pattern.find(q);
                        if (end != std::string::npos) pattern = pattern.substr(0, end);
                    }
                } else {
                    ss >> pattern;
                }
                out.trigger = pattern;
                break;
            }
            case 'p':
                if (token.size() > 2) {
                    try { out.priority = std::stoi(token.substr(2)); } catch (...) {}
                } else {
                    std::string val;
                    ss >> val;
                    try { out.priority = std::stoi(val); } catch (...) {}
                }
                break;
            case 'n':
                if (token.size() > 2) {
                    try { out.shots = std::stoi(token.substr(2)); } catch (...) {}
                } else {
                    std::string val;
                    ss >> val;
                    try { out.shots = std::stoi(val); } catch (...) {}
                }
                break;
            case 'g':
                out.gag = true;
                break;
            case 'h':
                out.hilite = true;
                break;
            default:
                error = "Unknown flag: -" + std::string(1, flag);
                return false;
            }
        } else if (!have_name) {
            out.name = token;
            have_name = true;
        } else {
            // Rest is body
            body_parts.push_back(token);
            std::string rest;
            std::getline(ss, rest);
            if (!rest.empty()) body_parts.push_back(rest);
            break;
        }
    }

    // Join body parts
    for (size_t i = 0; i < body_parts.size(); i++) {
        if (i > 0) out.body += " ";
        out.body += body_parts[i];
    }
    // Trim leading space
    if (!out.body.empty() && out.body[0] == ' ') {
        out.body = out.body.substr(1);
    }

    // Auto-generate name if not provided
    if (out.name.empty()) {
        static int auto_id = 0;
        out.name = "_trig_" + std::to_string(++auto_id);
    }

    if (out.trigger.empty() && out.body.empty()) {
        error = "Need at least a trigger (-t) or body";
        return false;
    }
    return true;
}
