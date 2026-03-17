#ifndef TIMER_H
#define TIMER_H

#include <string>
#include <vector>
#include <chrono>
#include <cstdint>

struct App;

struct Timer {
    int         id;             // unique process ID
    std::string command;        // command to execute each firing
    int         remaining;      // firings left (-1 = infinite)
    std::chrono::steady_clock::time_point next_fire;
    std::chrono::milliseconds   interval;
    std::string world;          // optional world context (unused for now)
};

class TimerDB {
public:
    // Add a timer.  Returns its process ID.
    int add(const std::string& command, int count,
            std::chrono::milliseconds interval);

    // Kill a timer by ID.  Returns true if found.
    bool kill(int id);

    // Kill all timers.
    void kill_all();

    // Time until the next timer fires, or -1 if no timers.
    // Used to compute the select() timeout.
    int ms_until_next() const;

    // Fire all timers whose deadline has passed.
    // Returns a list of commands to execute.
    std::vector<std::string> fire_due();

    const std::vector<Timer>& all() const { return timers_; }
    bool empty() const { return timers_.empty(); }

private:
    std::vector<Timer> timers_;
    int next_id_ = 1;
};

// Parse /repeat arguments.  TF syntax:
//   /repeat -<count> [-<seconds>] <command>
//   /repeat -0 -<seconds> <command>       (infinite repeat)
//   /repeat <count> <seconds> <command>   (alternate form)
bool parse_repeat(const std::string& args, int& count,
                  std::chrono::milliseconds& interval,
                  std::string& command, std::string& error);

#endif // TIMER_H
