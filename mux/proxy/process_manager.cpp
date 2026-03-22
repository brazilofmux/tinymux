#include "process_manager.h"
#include "hydra_log.h"
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>

ProcessManager::ProcessManager() {
}

ProcessManager::~ProcessManager() {
    // Kill all managed processes on destruction
    for (auto& [name, proc] : processes_) {
        if (proc.pid > 0) {
            kill(proc.pid, SIGTERM);
        }
    }
}

bool ProcessManager::startGame(const GameConfig& game, std::string& errorMsg) {
    if (game.type != GameType::Local) {
        errorMsg = "game '" + game.name + "' is not a local game";
        return false;
    }

    if (game.binary.empty()) {
        errorMsg = "no binary configured for game '" + game.name + "'";
        return false;
    }

    // Already running?
    auto it = processes_.find(game.name);
    if (it != processes_.end() && it->second.pid > 0) {
        // Check if still alive
        int status = 0;
        pid_t ret = waitpid(it->second.pid, &status, WNOHANG);
        if (ret == 0) {
            errorMsg = "game '" + game.name + "' is already running (pid "
                       + std::to_string(it->second.pid) + ")";
            return false;
        }
        // Already exited — clean up
        processes_.erase(it);
    }

    pid_t pid = fork();
    if (pid < 0) {
        errorMsg = "fork failed: " + std::string(strerror(errno));
        return false;
    }

    if (pid == 0) {
        // Child process
        if (!game.workdir.empty()) {
            if (chdir(game.workdir.c_str()) != 0) {
                _exit(127);
            }
        }

        // Close stdin, redirect stdout/stderr to /dev/null
        // (game's own logging handles output)
        close(STDIN_FILENO);
        open("/dev/null", 0);  // fd 0

        // Create new session so the game doesn't get our signals
        setsid();

        execl(game.binary.c_str(), game.binary.c_str(), nullptr);
        _exit(127);  // exec failed
    }

    // Parent
    ManagedProcess proc;
    proc.pid = pid;
    proc.gameName = game.name;
    proc.binary = game.binary;
    proc.workdir = game.workdir;
    proc.started = time(nullptr);
    processes_[game.name] = proc;

    LOG_INFO("Started game '%s' (pid %d)", game.name.c_str(), pid);
    return true;
}

bool ProcessManager::stopGame(const std::string& gameName) {
    auto it = processes_.find(gameName);
    if (it == processes_.end() || it->second.pid <= 0) return false;

    ManagedProcess& proc = it->second;

    // Check still alive
    int status = 0;
    pid_t ret = waitpid(proc.pid, &status, WNOHANG);
    if (ret != 0) {
        // Already dead
        processes_.erase(it);
        return false;
    }

    kill(proc.pid, SIGTERM);
    proc.stopping = true;
    proc.stopSentAt = time(nullptr);

    LOG_INFO("Sent SIGTERM to game '%s' (pid %d)",
             gameName.c_str(), proc.pid);
    return true;
}

bool ProcessManager::restartGame(const GameConfig& game,
                                 std::string& errorMsg) {
    stopGame(game.name);
    // Give it a moment to exit (non-blocking check)
    auto it = processes_.find(game.name);
    if (it != processes_.end()) {
        int status = 0;
        pid_t ret = waitpid(it->second.pid, &status, WNOHANG);
        if (ret != 0) {
            processes_.erase(it);
        }
        // If still alive, start will fail with "already running"
    }
    return startGame(game, errorMsg);
}

bool ProcessManager::isRunning(const std::string& gameName) const {
    auto it = processes_.find(gameName);
    if (it == processes_.end() || it->second.pid <= 0) return false;

    int status = 0;
    pid_t ret = waitpid(it->second.pid, &status, WNOHANG);
    return ret == 0;  // 0 = still running
}

pid_t ProcessManager::getPid(const std::string& gameName) const {
    auto it = processes_.find(gameName);
    if (it == processes_.end()) return 0;
    return it->second.pid;
}

std::vector<std::string> ProcessManager::reapChildren() {
    std::vector<std::string> exited;

    for (auto it = processes_.begin(); it != processes_.end(); ) {
        ManagedProcess& proc = it->second;
        if (proc.pid <= 0) {
            it = processes_.erase(it);
            continue;
        }

        int status = 0;
        pid_t ret = waitpid(proc.pid, &status, WNOHANG);
        if (ret > 0) {
            if (WIFEXITED(status)) {
                LOG_INFO("Game '%s' (pid %d) exited with status %d",
                         proc.gameName.c_str(), proc.pid,
                         WEXITSTATUS(status));
            } else if (WIFSIGNALED(status)) {
                LOG_INFO("Game '%s' (pid %d) killed by signal %d",
                         proc.gameName.c_str(), proc.pid,
                         WTERMSIG(status));
            }
            exited.push_back(proc.gameName);
            it = processes_.erase(it);
        } else {
            // Check for stuck stop — SIGKILL after 10 seconds
            if (proc.stopping &&
                time(nullptr) - proc.stopSentAt > 10) {
                LOG_WARN("Game '%s' (pid %d) didn't exit after SIGTERM, sending SIGKILL",
                         proc.gameName.c_str(), proc.pid);
                kill(proc.pid, SIGKILL);
                proc.stopSentAt = time(nullptr);  // reset timer
            }
            ++it;
        }
    }

    return exited;
}
