// hook.cpp -- Event hook implementation.
#include "hook.h"
#include <algorithm>

void HookDB::add(Hook h) {
    for (auto& existing : hooks_) {
        if (existing.name == h.name) {
            existing = std::move(h);
            return;
        }
    }
    hooks_.push_back(std::move(h));
}

bool HookDB::remove(const std::string& name) {
    auto it = std::find_if(hooks_.begin(), hooks_.end(),
                           [&](const Hook& h) { return h.name == name; });
    if (it == hooks_.end()) return false;
    hooks_.erase(it);
    return true;
}

std::vector<std::string> HookDB::fire_event(const std::string& event) const {
    std::vector<std::string> commands;
    for (auto& h : hooks_) {
        if (!h.enabled) continue;
        // Case-insensitive compare
        std::string upper_event = event;
        std::string upper_hook = h.event;
        std::transform(upper_event.begin(), upper_event.end(), upper_event.begin(), ::toupper);
        std::transform(upper_hook.begin(), upper_hook.end(), upper_hook.begin(), ::toupper);
        if (upper_event == upper_hook) {
            commands.push_back(h.body);
        }
    }
    return commands;
}
