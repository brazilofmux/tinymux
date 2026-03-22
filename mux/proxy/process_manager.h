#ifndef HYDRA_PROCESS_MANAGER_H
#define HYDRA_PROCESS_MANAGER_H

#include "config.h"
#include <map>
#include <string>
#include <sys/types.h>

struct ManagedProcess {
    pid_t       pid{0};
    std::string gameName;
    std::string binary;
    std::string workdir;
    time_t      started{0};
    bool        stopping{false};    // SIGTERM sent, waiting for exit
    time_t      stopSentAt{0};
};

class ProcessManager {
public:
    ProcessManager();
    ~ProcessManager();

    // Start a local game process. Returns true on success.
    bool startGame(const GameConfig& game, std::string& errorMsg);

    // Send SIGTERM to a game process. Returns true if process was running.
    bool stopGame(const std::string& gameName);

    // Stop then start.
    bool restartGame(const GameConfig& game, std::string& errorMsg);

    // Is a game process currently running?
    bool isRunning(const std::string& gameName) const;

    // Get the PID of a running game (0 if not running).
    pid_t getPid(const std::string& gameName) const;

    // Reap finished child processes. Call from the event loop.
    // Returns names of games that exited.
    std::vector<std::string> reapChildren();

private:
    std::map<std::string, ManagedProcess> processes_;
};

#endif // HYDRA_PROCESS_MANAGER_H
