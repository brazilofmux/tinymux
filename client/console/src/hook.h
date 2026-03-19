// hook.h -- Event hook system for the console client.
#ifndef HOOK_H
#define HOOK_H

#include <string>
#include <vector>

struct Hook {
    std::string name;
    std::string event;   // CONNECT, DISCONNECT, ACTIVITY
    std::string body;
    bool        enabled = true;
};

class HookDB {
public:
    void add(Hook h);
    bool remove(const std::string& name);
    const std::vector<Hook>& all() const { return hooks_; }
    std::vector<std::string> fire_event(const std::string& event) const;

private:
    std::vector<Hook> hooks_;
};

#endif // HOOK_H
