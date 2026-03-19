#ifndef TF_MACRO_H
#define TF_MACRO_H

#include <string>
#include <vector>
#include "regex_utils.h"

struct App;
class ScriptEnv;

// A single macro definition.
struct Macro {
    std::string name;
    std::string body;           // command(s), separated by %;
    std::string trigger;        // -t pattern
    std::string hook;           // -h HOOK
    int         priority = 0;   // -p (higher = fires first)
    int         shots = -1;     // -n (negative = unlimited, 0 = dead)
    std::string match_type;     // "simple", "glob", "regexp", "substr"
    bool        fallthrough = false;  // -F
    bool        quiet = false;        // -q
    bool        gag = false;          // suppress matched line from display
    bool        hilite = false;       // highlight matched text in output

    // Compiled trigger regex (built from trigger + match_type)
    RegexPattern trigger_re;
    bool        trigger_compiled = false;

    void compile_trigger();
};

// Storage and lookup for all macros.
class MacroDB {
public:
    void define(Macro m);
    bool undef(const std::string& name);
    Macro* find(const std::string& name);
    std::vector<Macro*> match_triggers(const std::string& text);
    std::vector<Macro*> match_hooks(const std::string& hook_type);
    const std::vector<Macro>& all() const { return macros_; }

private:
    std::vector<Macro> macros_;
};

// Hook type constants.
namespace Hook {
    constexpr const char* CONNECT    = "CONNECT";
    constexpr const char* DISCONNECT = "DISCONNECT";
    constexpr const char* LOGIN      = "LOGIN";
    constexpr const char* SEND       = "SEND";
    constexpr const char* LOAD       = "LOAD";
    constexpr const char* ACTIVITY   = "ACTIVITY";
    constexpr const char* BGTEXT     = "BGTEXT";
    constexpr const char* RESIZE     = "RESIZE";
    constexpr const char* PROMPT     = "PROMPT";
    constexpr const char* RESTART    = "RESTART";
}

// Execute a macro body.  Statements separated by %;
void exec_body(App& app, const std::string& body,
               const std::vector<std::string>& args = {});
void exec_body_in_env(App& app, const std::string& body, ScriptEnv& env);

// Fire all hook macros of a given type.
void fire_hook(App& app, const char* hook_type, const std::string& arg = "");

// Result of checking triggers against a line.
struct TriggerResult {
    bool matched = false;   // any trigger matched
    bool gagged  = false;   // a gag trigger matched — suppress display
};

// Check incoming text against triggers.  May modify `text` for hilite.
TriggerResult check_triggers(App& app, std::string& text);

// Parse /def flags and body.  Returns true on success.
bool parse_def(const std::string& args, Macro& out, std::string& error);

#endif // TF_MACRO_H
