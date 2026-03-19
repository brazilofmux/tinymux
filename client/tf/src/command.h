#ifndef TF_COMMAND_H
#define TF_COMMAND_H

#include <string>
#include <unordered_map>
#include <functional>

// Forward declaration
struct App;

class CommandDispatcher {
public:
    using Handler = std::function<void(App& app, const std::string& args)>;

    CommandDispatcher();

    // Dispatch a command line (starting with /). Returns true if recognized.
    bool dispatch(App& app, const std::string& line);

private:
    void register_commands();

    std::unordered_map<std::string, Handler> commands_;
};

// Built-in command handlers
void cmd_quit(App& app, const std::string& args);
void cmd_connect(App& app, const std::string& args);
void cmd_dc(App& app, const std::string& args);
void cmd_fg(App& app, const std::string& args);
void cmd_fg_next(App& app, const std::string& args);
void cmd_fg_prev(App& app, const std::string& args);
void cmd_world(App& app, const std::string& args);
void cmd_set(App& app, const std::string& args);
void cmd_load(App& app, const std::string& args);
void cmd_recall(App& app, const std::string& args);
void cmd_help(App& app, const std::string& args);
void cmd_test(App& app, const std::string& args);
void cmd_expr(App& app, const std::string& args);
void cmd_echo(App& app, const std::string& args);
void cmd_let(App& app, const std::string& args);
void cmd_def(App& app, const std::string& args);
void cmd_undef(App& app, const std::string& args);
void cmd_list(App& app, const std::string& args);
void cmd_if(App& app, const std::string& args);
void cmd_while_cmd(App& app, const std::string& args);
void cmd_repeat(App& app, const std::string& args);
void cmd_kill(App& app, const std::string& args);
void cmd_ps(App& app, const std::string& args);
void cmd_quote(App& app, const std::string& args);
void cmd_sh(App& app, const std::string& args);
void cmd_sys(App& app, const std::string& args);
void cmd_listworlds(App& app, const std::string& args);
void cmd_listsockets(App& app, const std::string& args);
void cmd_gag(App& app, const std::string& args);
void cmd_hilite(App& app, const std::string& args);
void cmd_trigger(App& app, const std::string& args);
void cmd_purge(App& app, const std::string& args);
void cmd_save(App& app, const std::string& args);
void cmd_log(App& app, const std::string& args);
void cmd_version(App& app, const std::string& args);
void cmd_unworld(App& app, const std::string& args);
void cmd_eval(App& app, const std::string& args);
void cmd_bind(App& app, const std::string& args);
void cmd_unbind(App& app, const std::string& args);
void cmd_dokey(App& app, const std::string& args);
void cmd_listvar(App& app, const std::string& args);
void cmd_export(App& app, const std::string& args);
void cmd_setenv(App& app, const std::string& args);
void cmd_shift(App& app, const std::string& args);
void cmd_hook(App& app, const std::string& args);
void cmd_beep(App& app, const std::string& args);
void cmd_suspend(App& app, const std::string& args);
void cmd_features(App& app, const std::string& args);
void cmd_saveworld(App& app, const std::string& args);
void cmd_recordline(App& app, const std::string& args);
void cmd_lcd(App& app, const std::string& args);
void cmd_limit(App& app, const std::string& args);
void cmd_unlimit(App& app, const std::string& args);
void cmd_relimit(App& app, const std::string& args);
void cmd_input(App& app, const std::string& args);
void cmd_core(App& app, const std::string& args);
void cmd_edit(App& app, const std::string& args);
void cmd_histsize(App& app, const std::string& args);
void cmd_liststreams(App& app, const std::string& args);
void cmd_localecho(App& app, const std::string& args);
void cmd_restrict(App& app, const std::string& args);
void cmd_status_add(App& app, const std::string& args);
void cmd_status_edit(App& app, const std::string& args);
void cmd_status_rm(App& app, const std::string& args);
void cmd_trigpc(App& app, const std::string& args);
void cmd_undefn(App& app, const std::string& args);
void cmd_unset(App& app, const std::string& args);
void cmd_watchdog(App& app, const std::string& args);
void cmd_watchname(App& app, const std::string& args);
void cmd_restart(App& app, const std::string& args);
void cmd_update(App& app, const std::string& args);

#endif // TF_COMMAND_H
