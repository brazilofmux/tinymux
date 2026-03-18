#include "macro.h"
#include "app.h"
#include "script.h"
#include <algorithm>
#include <sstream>
#include <fnmatch.h>

// ---- Macro trigger compilation ----

static std::string glob_to_regex(const std::string& glob) {
    std::string re;
    for (char c : glob) {
        switch (c) {
            case '*': re += ".*"; break;
            case '?': re += '.'; break;
            case '.': case '(': case ')': case '+':
            case '^': case '$': case '|': case '\\':
            case '{': case '}': case '[': case ']':
                re += '\\'; re += c; break;
            default: re += c; break;
        }
    }
    return re;
}

void Macro::compile_trigger() {
    if (trigger.empty()) { trigger_compiled = false; return; }
    std::string pattern;
    if (match_type == "regexp") {
        pattern = trigger;
    } else if (match_type == "simple" || match_type == "substr") {
        pattern = regex_escape(trigger);
    } else {
        // Default: glob
        pattern = glob_to_regex(trigger);
    }
    trigger_compiled = trigger_re.compile(pattern);
}

// ---- MacroDB ----

void MacroDB::define(Macro m) {
    m.compile_trigger();
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
    auto it = std::find_if(macros_.begin(), macros_.end(),
        [&](const Macro& m) { return m.name == name; });
    if (it != macros_.end()) { macros_.erase(it); return true; }
    return false;
}

Macro* MacroDB::find(const std::string& name) {
    for (auto& m : macros_) {
        if (m.name == name) return &m;
    }
    return nullptr;
}

static bool cmp_priority(Macro* a, Macro* b) {
    return a->priority > b->priority;
}

std::vector<Macro*> MacroDB::match_triggers(const std::string& text) {
    std::vector<Macro*> result;
    for (auto& m : macros_) {
        if (!m.trigger_compiled || m.shots == 0) continue;
        if (m.trigger_re.search(text)) {
            result.push_back(&m);
        }
    }
    std::sort(result.begin(), result.end(), cmp_priority);
    return result;
}

std::vector<Macro*> MacroDB::match_hooks(const std::string& hook_type) {
    std::vector<Macro*> result;
    for (auto& m : macros_) {
        if (m.shots == 0) continue;
        if (m.hook == hook_type) {
            result.push_back(&m);
        }
    }
    std::sort(result.begin(), result.end(), cmp_priority);
    return result;
}

// ---- /def parser ----
// Syntax: /def [flags] name = body
// Flags: -t'pattern' -h'HOOK' -p<priority> -n<shots> -m<matchtype> -F -q -1

bool parse_def(const std::string& args, Macro& out, std::string& error) {
    out = Macro{};
    out.match_type = "glob";
    size_t i = 0;

    auto skip_ws = [&]() { while (i < args.size() && args[i] == ' ') i++; };

    // Parse flags
    while (i < args.size()) {
        skip_ws();
        if (i >= args.size() || args[i] != '-') break;
        i++; // skip '-'
        if (i >= args.size()) { error = "Incomplete flag"; return false; }

        char flag = args[i++];

        // Flags that take a quoted or unquoted argument
        auto read_arg = [&]() -> std::string {
            std::string val;
            if (i < args.size() && (args[i] == '\'' || args[i] == '"')) {
                char q = args[i++];
                while (i < args.size() && args[i] != q) {
                    val += args[i++];
                }
                if (i < args.size()) i++; // skip closing quote
            } else {
                while (i < args.size() && args[i] != ' ') val += args[i++];
            }
            return val;
        };

        switch (flag) {
            case 't': out.trigger = read_arg(); break;
            case 'h': out.hook = read_arg(); break;
            case 'p': out.priority = std::atoi(read_arg().c_str()); break;
            case 'n': out.shots = std::atoi(read_arg().c_str()); break;
            case 'm': out.match_type = read_arg(); break;
            case 'F': out.fallthrough = true; break;
            case 'q': out.quiet = true; break;
            case '1': out.shots = 1; break;
            case 'a': {
                // -a<attr>: g=gag, h=hilite (TF attribute flags)
                std::string attr = read_arg();
                for (char c : attr) {
                    if (c == 'g' || c == 'G') out.gag = true;
                    if (c == 'h' || c == 'H') out.hilite = true;
                }
                break;
            }
            default: break; // ignore unknown flags
        }
    }

    skip_ws();

    // Parse name
    size_t name_start = i;
    while (i < args.size() && args[i] != ' ' && args[i] != '=') i++;
    out.name = args.substr(name_start, i - name_start);
    if (out.name.empty()) { error = "Missing macro name"; return false; }

    skip_ws();

    // Expect '='
    if (i < args.size() && args[i] == '=') {
        i++;
        skip_ws();
    }

    // Body is everything remaining
    out.body = args.substr(i);

    return true;
}

// ---- Statement executor ----
// Splits body on %; and executes statements with control flow.

enum class ExecResult { EXEC_OK, EXEC_BREAK, EXEC_RETURN };

// Forward declare
static ExecResult exec_stmts(App& app, std::vector<std::string>& stmts,
                              size_t& pos, ScriptEnv& env);

static std::vector<std::string> split_body(const std::string& body) {
    std::vector<std::string> stmts;
    std::string cur;
    for (size_t i = 0; i < body.size(); i++) {
        if (body[i] == '%' && i + 1 < body.size() && body[i + 1] == ';') {
            // Trim the statement
            while (!cur.empty() && cur.front() == ' ') cur.erase(0, 1);
            while (!cur.empty() && cur.back() == ' ') cur.pop_back();
            if (!cur.empty()) stmts.push_back(cur);
            cur.clear();
            i++; // skip ';'
        } else if (body[i] == '\n') {
            while (!cur.empty() && cur.front() == ' ') cur.erase(0, 1);
            while (!cur.empty() && cur.back() == ' ') cur.pop_back();
            if (!cur.empty()) stmts.push_back(cur);
            cur.clear();
        } else {
            cur += body[i];
        }
    }
    while (!cur.empty() && cur.front() == ' ') cur.erase(0, 1);
    while (!cur.empty() && cur.back() == ' ') cur.pop_back();
    if (!cur.empty()) stmts.push_back(cur);
    return stmts;
}

static std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && s[a] == ' ') a++;
    while (b > a && s[b-1] == ' ') b--;
    return s.substr(a, b - a);
}

// Extract the first word (command name) from a statement.
static std::string first_word(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && s[i] == ' ') i++;
    size_t start = i;
    while (i < s.size() && s[i] != ' ') i++;
    return s.substr(start, i - start);
}

static void shift_env_positional(ScriptEnv& env, int count = 1) {
    if (count <= 0) return;
    while (count-- > 0) {
        int n = 0;
        while (env.vars().count(std::to_string(n + 1))) n++;
        if (n <= 0) return;

        for (int i = 1; i < n; i++) {
            env.vars()[std::to_string(i)] = env.vars()[std::to_string(i + 1)];
        }
        env.vars().erase(std::to_string(n));

        auto it_count = env.vars().find("#");
        if (it_count != env.vars().end()) {
            int argc = std::atoi(it_count->second.c_str());
            if (argc > 0) it_count->second = std::to_string(argc - 1);
        }

        std::string all;
        for (int i = 1;; i++) {
            auto it = env.vars().find(std::to_string(i));
            if (it == env.vars().end()) break;
            if (!all.empty()) all += ' ';
            all += it->second;
        }
        env.vars()["*"] = all;
    }
}

// Execute a single statement.
static void exec_one(App& app, const std::string& stmt, ScriptEnv& env);

// Find matching /endif or /else/elseif at the same nesting depth.
static size_t find_else_or_endif(std::vector<std::string>& stmts, size_t pos) {
    int depth = 1;
    while (pos < stmts.size()) {
        std::string word = first_word(stmts[pos]);
        if (word == "/if") depth++;
        else if (word == "/endif") { depth--; if (depth == 0) return pos; }
        else if (depth == 1 && (word == "/else" || word == "/elseif")) return pos;
        pos++;
    }
    return pos;
}

// Find matching /done at the same nesting depth.
static size_t find_done(std::vector<std::string>& stmts, size_t pos) {
    int depth = 1;
    while (pos < stmts.size()) {
        std::string word = first_word(stmts[pos]);
        if (word == "/while") depth++;
        else if (word == "/done") { depth--; if (depth == 0) return pos; }
        pos++;
    }
    return pos;
}

// Parse condition from /if or /while: "/if (expr)" or "/if expr"
static std::string extract_condition(const std::string& stmt) {
    // Skip the keyword
    size_t i = 0;
    while (i < stmt.size() && stmt[i] != ' ') i++;
    while (i < stmt.size() && stmt[i] == ' ') i++;
    std::string cond = stmt.substr(i);

    // Strip /then if present at the end
    size_t then_pos = cond.rfind("/then");
    if (then_pos != std::string::npos) {
        cond = cond.substr(0, then_pos);
    }

    // Strip surrounding parens if present
    cond = trim(cond);
    if (!cond.empty() && cond.front() == '(' && cond.back() == ')') {
        cond = cond.substr(1, cond.size() - 2);
    }
    return trim(cond);
}

static ExecResult exec_stmts(App& app, std::vector<std::string>& stmts,
                              size_t& pos, ScriptEnv& env) {
    while (pos < stmts.size()) {
        std::string raw = stmts[pos];
        std::string word = first_word(raw);

        // Control flow keywords
        if (word == "/if") {
            std::string cond = extract_condition(raw);
            Value cv = eval_expr(cond, env);
            bool taken = cv.as_bool();
            pos++; // move past /if

            if (taken) {
                // Execute until /else, /elseif, or /endif
                auto r = exec_stmts(app, stmts, pos, env);
                if (r == ExecResult::EXEC_BREAK || r == ExecResult::EXEC_RETURN) return r;
                // Skip remaining branches
                while (pos < stmts.size()) {
                    word = first_word(stmts[pos]);
                    if (word == "/endif") { pos++; break; }
                    if (word == "/else" || word == "/elseif") {
                        pos++;
                        // Skip to /endif
                        size_t end = find_else_or_endif(stmts, pos);
                        pos = end;
                    } else {
                        pos++;
                    }
                }
            } else {
                // Skip to /else, /elseif, or /endif
                size_t target = find_else_or_endif(stmts, pos);
                pos = target;
                if (pos < stmts.size()) {
                    word = first_word(stmts[pos]);
                    if (word == "/elseif") {
                        // Rewrite as /if and re-process
                        stmts[pos] = "/if " + stmts[pos].substr(8);
                        continue; // don't advance pos
                    } else if (word == "/else") {
                        pos++; // skip /else
                        auto r = exec_stmts(app, stmts, pos, env);
                        if (r == ExecResult::EXEC_BREAK || r == ExecResult::EXEC_RETURN) return r;
                        // Find /endif
                        while (pos < stmts.size() && first_word(stmts[pos]) != "/endif") pos++;
                        if (pos < stmts.size()) pos++; // skip /endif
                    } else if (word == "/endif") {
                        pos++; // skip /endif
                    }
                }
            }
            continue;
        }

        if (word == "/while") {
            std::string cond = extract_condition(raw);
            size_t loop_start = pos + 1;

            while (true) {
                Value cv = eval_expr(cond, env);
                if (!cv.as_bool()) break;

                size_t p = loop_start;
                auto r = exec_stmts(app, stmts, p, env);
                if (r == ExecResult::EXEC_BREAK) break;
                if (r == ExecResult::EXEC_RETURN) return ExecResult::EXEC_RETURN;
            }

            // Skip past /done
            pos = loop_start;
            size_t done_pos = find_done(stmts, pos);
            pos = (done_pos < stmts.size()) ? done_pos + 1 : done_pos;
            continue;
        }

        // /then is a no-op (just a readability marker)
        if (word == "/then") { pos++; continue; }

        // Block terminators — return to caller
        if (word == "/endif" || word == "/else" || word == "/elseif" ||
            word == "/done") {
            return ExecResult::EXEC_OK;
        }

        if (word == "/break") { pos++; return ExecResult::EXEC_BREAK; }
        if (word == "/return") { pos++; return ExecResult::EXEC_RETURN; }
        if (word == "/result") {
            // Set user_result variable
            std::string val = trim(raw.substr(7));
            Value v = eval_expr(val, env);
            env.set("?", v);
            pos++;
            return ExecResult::EXEC_RETURN;
        }

        // Normal statement — expand and execute
        std::string expanded = expand_subs(raw, env);
        exec_one(app, expanded, env);
        pos++;
    }
    return ExecResult::EXEC_OK;
}

// Decrement shots for a macro by name.  Removes it if shots reach 0.
// Safe to call even if the macro was already removed.
static void decrement_shots(App& app, const std::string& name) {
    Macro* m = app.macros.find(name);
    if (m && m->shots > 0) {
        m->shots--;
        if (m->shots == 0) app.macros.undef(name);
    }
}

static void exec_one(App& app, const std::string& stmt, ScriptEnv& env) {
    if (stmt.empty()) return;

    if (stmt[0] == '/') {
        // Check if it's a user-defined macro
        size_t sp = stmt.find(' ');
        std::string cmd = (sp != std::string::npos) ? stmt.substr(1, sp - 1) : stmt.substr(1);
        std::string args = (sp != std::string::npos) ? stmt.substr(sp + 1) : "";

        if (cmd == "shift") {
            int count = 1;
            if (!args.empty()) {
                count = std::max(1, std::atoi(args.c_str()));
            }
            shift_env_positional(env, count);
            return;
        }

        Macro* m = app.macros.find(cmd);
        if (m) {
            // Copy what we need before exec_body — it may invalidate m
            std::string macro_name = m->name;
            std::string body = m->body;

            std::vector<std::string> params;
            std::istringstream iss(args);
            std::string token;
            while (iss >> token) params.push_back(token);

            exec_body(app, body, params);
            decrement_shots(app, macro_name);
            return;
        }

        // Built-in command
        ScriptEnv* previous_env = app.current_env;
        app.current_env = &env;
        app.commands.dispatch(app, stmt);
        app.current_env = previous_env;
    } else {
        // Text — send to foreground
        if (app.fg && app.fg->is_connected()) {
            app.terminal.clear_prompt();
            app_send_line(app, app.fg, stmt);
        }
    }
}

// ---- Public API ----

void exec_body_in_env(App& app, const std::string& body, ScriptEnv& env) {
    auto stmts = split_body(body);
    if (stmts.empty()) return;

    ScriptEnv* previous_env = app.current_env;
    app.current_env = &env;
    size_t pos = 0;
    exec_stmts(app, stmts, pos, env);
    app.current_env = previous_env;
}

void exec_body(App& app, const std::string& body,
               const std::vector<std::string>& args) {
    auto stmts = split_body(body);
    if (stmts.empty()) return;

    VarMap local_vars;
    ScriptEnv env(app.vars, &app, &local_vars);

    // Set positional parameters
    for (size_t i = 0; i < args.size(); i++) {
        env.set(std::to_string(i + 1), Value::make_str(args[i]));
    }
    env.set("*", Value::make_str([&]() {
        std::string all;
        for (size_t i = 0; i < args.size(); i++) {
            if (i) all += ' ';
            all += args[i];
        }
        return all;
    }()));
    env.set("#", Value::make_int((int64_t)args.size()));

    ScriptEnv* previous_env = app.current_env;
    app.current_env = &env;
    size_t pos = 0;
    exec_stmts(app, stmts, pos, env);
    app.current_env = previous_env;
}

void fire_hook(App& app, const char* hook_type, const std::string& arg) {
    auto matches = app.macros.match_hooks(hook_type);
    for (auto* m : matches) {
        // Copy before exec_body — it may invalidate m via undef/redef
        std::string macro_name = m->name;
        std::string body = m->body;
        bool ft = m->fallthrough;

        std::vector<std::string> args;
        if (!arg.empty()) args.push_back(arg);
        exec_body(app, body, args);
        decrement_shots(app, macro_name);

        if (!ft) break;
    }
}

TriggerResult check_triggers(App& app, std::string& text) {
    auto matches = app.macros.match_triggers(text);
    TriggerResult result;

    for (auto* m : matches) {
        result.matched = true;

        // Copy everything we need before exec_body invalidates m
        std::string macro_name = m->name;
        std::string body = m->body;
        bool ft = m->fallthrough;
        bool compiled = m->trigger_compiled;
        bool is_gag = m->gag;
        bool is_hilite = m->hilite;

        // Extract regex captures as positional args
        std::vector<std::string> args;
        std::vector<std::string> captures;
        if (compiled) {
            if (m->trigger_re.search(text, captures)) {
                for (const auto& capture : captures) {
                    args.push_back(capture);
                }
            }
        }

        // Gag: mark for suppression
        if (is_gag) {
            result.gagged = true;
        }

        // Hilite: wrap the matched portion in bold ANSI
        if (is_hilite && compiled && !captures.empty() && !captures[0].empty()) {
            std::string::size_type match_pos = text.find(captures[0]);
            if (match_pos != std::string::npos) {
                std::string before = text.substr(0, match_pos);
                std::string match = captures[0];
                std::string after = text.substr(match_pos + match.size());
                text = before + "\033[1m" + match + "\033[0m" + after;
            }
        }

        // Execute body if non-empty
        if (!body.empty()) {
            exec_body(app, body, args);
        }
        decrement_shots(app, macro_name);

        if (!ft) break;
    }
    return result;
}
