#include "timer.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>

using Clock = std::chrono::steady_clock;
using Ms = std::chrono::milliseconds;

int TimerDB::add(const std::string& command, int count, Ms interval) {
    Timer t;
    t.id = next_id_++;
    t.command = command;
    t.remaining = count;
    t.interval = interval;
    t.next_fire = Clock::now() + interval;
    timers_.push_back(std::move(t));
    return t.id;
}

bool TimerDB::kill(int id) {
    auto it = std::find_if(timers_.begin(), timers_.end(),
        [id](const Timer& t) { return t.id == id; });
    if (it != timers_.end()) {
        timers_.erase(it);
        return true;
    }
    return false;
}

void TimerDB::kill_all() {
    timers_.clear();
}

int TimerDB::ms_until_next() const {
    if (timers_.empty()) return -1;
    auto now = Clock::now();
    auto earliest = timers_[0].next_fire;
    for (size_t i = 1; i < timers_.size(); i++) {
        if (timers_[i].next_fire < earliest)
            earliest = timers_[i].next_fire;
    }
    auto delta = std::chrono::duration_cast<Ms>(earliest - now);
    if (delta.count() <= 0) return 0;
    return (int)delta.count();
}

std::vector<std::string> TimerDB::fire_due() {
    std::vector<std::string> cmds;
    auto now = Clock::now();

    // Process in-place, removing dead timers after
    for (auto& t : timers_) {
        if (t.next_fire <= now) {
            cmds.push_back(t.command);
            if (t.remaining > 0) t.remaining--;
            t.next_fire = now + t.interval;
        }
    }

    // Remove expired timers (remaining == 0)
    timers_.erase(
        std::remove_if(timers_.begin(), timers_.end(),
            [](const Timer& t) { return t.remaining == 0; }),
        timers_.end());

    return cmds;
}

// ---- /repeat parser ----
// TF syntax:  /repeat -<count> [-<seconds>] <command>
// Also:       /repeat <count> <seconds> <command>
// count=0 means infinite.  seconds can be fractional.

bool parse_repeat(const std::string& args, int& count,
                  std::chrono::milliseconds& interval,
                  std::string& command, std::string& error) {
    count = 1;
    interval = Ms(0);
    command.clear();

    size_t i = 0;
    auto skip_ws = [&]() { while (i < args.size() && args[i] == ' ') i++; };

    skip_ws();
    if (i >= args.size()) { error = "Usage: /repeat [-count] [-seconds] command"; return false; }

    // Parse flags: -<number> tokens before the command
    // First -<number> is count, second -<number> is seconds
    bool got_count = false;
    bool got_interval = false;

    while (i < args.size() && args[i] == '-') {
        i++; // skip '-'
        size_t num_start = i;
        // Allow digits and '.' for fractional seconds
        while (i < args.size() && (args[i] >= '0' && args[i] <= '9' || args[i] == '.'))
            i++;
        if (i == num_start) { error = "Expected number after -"; return false; }
        std::string num_str = args.substr(num_start, i - num_start);
        skip_ws();

        if (!got_count) {
            count = std::atoi(num_str.c_str());
            if (count == 0) count = -1; // TF: -0 means infinite
            got_count = true;
        } else if (!got_interval) {
            double secs = std::atof(num_str.c_str());
            interval = Ms((int)(secs * 1000));
            got_interval = true;
        }
    }

    // If only one number given and no interval, treat it as count with 1s interval
    if (got_count && !got_interval) {
        interval = Ms(1000);
    }

    // Remaining text is the command
    command = args.substr(i);
    // Trim leading whitespace
    while (!command.empty() && command.front() == ' ') command.erase(0, 1);

    if (command.empty()) { error = "Missing command"; return false; }
    return true;
}
