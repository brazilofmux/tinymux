// timer.h -- Simple timer system (/repeat).
#ifndef TIMER_H
#define TIMER_H

#include <string>
#include <vector>
#include <chrono>

struct Timer {
    std::string name;
    std::string command;
    int         interval_ms;
    int         shots;       // -1 = infinite, 0 = expired
    std::chrono::steady_clock::time_point next_fire;
};

class TimerDB {
public:
    void add(const std::string& name, const std::string& command,
             int interval_ms, int shots = -1);
    bool remove(const std::string& name);
    const std::vector<Timer>& all() const { return timers_; }

    // Check for expired timers. Returns commands to execute.
    std::vector<std::string> check_and_fire();

    // Milliseconds until next timer fires, or -1 if none.
    int ms_until_next() const;

private:
    std::vector<Timer> timers_;
};

#endif // TIMER_H
