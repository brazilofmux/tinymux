// command.h -- Command dispatcher for built-in / commands.
#ifndef COMMAND_H
#define COMMAND_H

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

struct App;

using CmdFunc = std::function<void(App& app, const std::vector<std::string>& args)>;

class CommandDispatcher {
public:
    void register_cmd(const std::string& name, CmdFunc func, const std::string& help);
    bool dispatch(App& app, const std::string& input);
    std::vector<std::pair<std::string, std::string>> help_list() const;

private:
    struct CmdEntry {
        CmdFunc func;
        std::string help;
    };
    std::unordered_map<std::string, CmdEntry> cmds_;
};

// Register all built-in commands.
void register_builtin_commands(App& app);

#endif // COMMAND_H
