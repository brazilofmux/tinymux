#ifndef HYDRA_PROCESS_MANAGER_H
#define HYDRA_PROCESS_MANAGER_H

#include "config.h"
#include <map>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/types.h>
#endif

struct ManagedProcess {
#if defined(_WIN32)
    HANDLE      hProcess{nullptr};
    DWORD       pid{0};
#else
    pid_t       pid{0};
#endif
    std::string gameName;
    std::string binary;
    std::string workdir;
    time_t      started{0};
    bool        stopping{false};    // Termination requested, waiting for exit
    time_t      stopSentAt{0};
};

class ProcessManager {
public:
    ProcessManager();
    ~ProcessManager();

    // Start a local game process. Returns true on success.
    bool startGame(const GameConfig& game, std::string& errorMsg);

    // Request a game process to stop. Returns true if process was running.
    bool stopGame(const std::string& gameName);

    // Stop then start.
    bool restartGame(const GameConfig& game, std::string& errorMsg);

    // Is a game process currently running?
    bool isRunning(const std::string& gameName) const;

    // Get the PID of a running game (0 if not running).
#if defined(_WIN32)
    DWORD getPid(const std::string& gameName) const;
#else
    pid_t getPid(const std::string& gameName) const;
#endif

    // Reap finished child processes. Call from the event loop.
    // Returns names of games that exited.
    std::vector<std::string> reapChildren();

private:
    std::map<std::string, ManagedProcess> processes_;
};

#endif // HYDRA_PROCESS_MANAGER_H
