// timer.cpp -- Timer system implementation.
#include "timer.h"
#include <algorithm>

void TimerDB::add(const std::string& name, const std::string& command,
                  int interval_ms, int shots) {
    // Replace existing
    remove(name);
    Timer t;
    t.name = name;
    t.command = command;
    t.interval_ms = interval_ms;
    t.shots = shots;
    t.next_fire = std::chrono::steady_clock::now() +
                  std::chrono::milliseconds(interval_ms);
    timers_.push_back(std::move(t));
}

bool TimerDB::remove(const std::string& name) {
    for (auto it = timers_.begin(); it != timers_.end(); ++it) {
        if (it->name == name) {
            timers_.erase(it);
            return true;
        }
    }
    return false;
}

std::vector<std::string> TimerDB::check_and_fire() {
    std::vector<std::string> commands;
    auto now = std::chrono::steady_clock::now();

    for (auto it = timers_.begin(); it != timers_.end(); ) {
        if (now >= it->next_fire) {
            commands.push_back(it->command);
            if (it->shots > 0) it->shots--;
            if (it->shots == 0) {
                it = timers_.erase(it);
                continue;
            }
            it->next_fire = now + std::chrono::milliseconds(it->interval_ms);
        }
        ++it;
    }
    return commands;
}

int TimerDB::ms_until_next() const {
    if (timers_.empty()) return -1;
    auto now = std::chrono::steady_clock::now();
    int min_ms = INT_MAX;
    for (auto& t : timers_) {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            t.next_fire - now).count();
        if (ms < 0) ms = 0;
        if ((int)ms < min_ms) min_ms = (int)ms;
    }
    return min_ms;
}
