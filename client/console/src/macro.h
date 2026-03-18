// macro.h -- Trigger and macro definitions for the console client.
#ifndef MACRO_H
#define MACRO_H

#include <string>
#include <vector>
#include <regex>

struct App;

struct Macro {
    std::string name;
    std::string body;           // command(s) to execute
    std::string trigger;        // -t pattern (regex)
    int         priority = 0;   // -p (higher fires first)
    int         shots = -1;     // -n (negative = unlimited, 0 = dead)
    bool        gag = false;    // suppress matched line from display
    bool        hilite = false; // highlight matched text

    // Compiled regex (from trigger)
    std::regex  trigger_re;
    bool        compiled = false;

    void compile();
};

class MacroDB {
public:
    void define(Macro m);
    bool undef(const std::string& name);
    Macro* find(const std::string& name);
    const std::vector<Macro>& all() const { return macros_; }

    // Check a line against all triggers. Returns matching macros.
    std::vector<Macro*> match_triggers(const std::string& text);

private:
    std::vector<Macro> macros_;
};

// Result of checking triggers against a line.
struct TriggerResult {
    bool matched = false;
    bool gagged  = false;
};

// Check incoming text against triggers, execute bodies, return result.
TriggerResult check_triggers(App& app, std::string& text);

// Parse /def flags. Returns true on success.
bool parse_def(const std::string& args, Macro& out, std::string& error);

#endif // MACRO_H
