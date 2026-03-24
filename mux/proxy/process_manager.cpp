#include "process_manager.h"
#include "hydra_log.h"
#include <cerrno>
#include <cstring>
#include <cstdlib>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#endif

ProcessManager::ProcessManager() {
}

ProcessManager::~ProcessManager() {
    // Terminate all managed processes on destruction
    for (auto& [name, proc] : processes_) {
#if defined(_WIN32)
        if (proc.hProcess) {
            TerminateProcess(proc.hProcess, 1);
            CloseHandle(proc.hProcess);
        }
#else
        if (proc.pid > 0) {
            kill(proc.pid, SIGTERM);
        }
#endif
    }
}

#if defined(_WIN32)

// Check if a process handle is still running (non-blocking).
static bool isProcessAlive(HANDLE h) {
    if (!h) return false;
    DWORD exitCode = 0;
    if (!GetExitCodeProcess(h, &exitCode)) return false;
    return exitCode == STILL_ACTIVE;
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
    if (it != processes_.end() && it->second.hProcess) {
        if (isProcessAlive(it->second.hProcess)) {
            errorMsg = "game '" + game.name + "' is already running (pid "
                       + std::to_string(it->second.pid) + ")";
            return false;
        }
        // Already exited — clean up
        CloseHandle(it->second.hProcess);
        processes_.erase(it);
    }

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));

    // Build command line (just the binary for now)
    std::string cmdLine = game.binary;

    const char* workDir = game.workdir.empty() ? nullptr : game.workdir.c_str();

    if (!CreateProcessA(
            nullptr,                            // lpApplicationName
            const_cast<char*>(cmdLine.c_str()), // lpCommandLine
            nullptr,                            // lpProcessAttributes
            nullptr,                            // lpThreadAttributes
            FALSE,                              // bInheritHandles
            CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS,
            nullptr,                            // lpEnvironment
            workDir,                            // lpCurrentDirectory
            &si, &pi)) {
        errorMsg = "CreateProcess failed: error " + std::to_string(GetLastError());
        return false;
    }

    // Don't need the thread handle
    CloseHandle(pi.hThread);

    ManagedProcess proc;
    proc.hProcess = pi.hProcess;
    proc.pid = pi.dwProcessId;
    proc.gameName = game.name;
    proc.binary = game.binary;
    proc.workdir = game.workdir;
    proc.started = time(nullptr);
    processes_[game.name] = proc;

    LOG_INFO("Started game '%s' (pid %lu)", game.name.c_str(),
             static_cast<unsigned long>(proc.pid));
    return true;
}

bool ProcessManager::stopGame(const std::string& gameName) {
    auto it = processes_.find(gameName);
    if (it == processes_.end() || !it->second.hProcess) return false;

    ManagedProcess& proc = it->second;

    if (!isProcessAlive(proc.hProcess)) {
        CloseHandle(proc.hProcess);
        processes_.erase(it);
        return false;
    }

    // Send Ctrl+Break to the process group (graceful shutdown)
    GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, proc.pid);
    proc.stopping = true;
    proc.stopSentAt = time(nullptr);

    LOG_INFO("Sent CTRL_BREAK to game '%s' (pid %lu)",
             gameName.c_str(), static_cast<unsigned long>(proc.pid));
    return true;
}

bool ProcessManager::restartGame(const GameConfig& game,
                                 std::string& errorMsg) {
    stopGame(game.name);
    auto it = processes_.find(game.name);
    if (it != processes_.end()) {
        if (!isProcessAlive(it->second.hProcess)) {
            CloseHandle(it->second.hProcess);
            processes_.erase(it);
        }
    }
    return startGame(game, errorMsg);
}

bool ProcessManager::isRunning(const std::string& gameName) const {
    auto it = processes_.find(gameName);
    if (it == processes_.end() || !it->second.hProcess) return false;
    return isProcessAlive(it->second.hProcess);
}

DWORD ProcessManager::getPid(const std::string& gameName) const {
    auto it = processes_.find(gameName);
    if (it == processes_.end()) return 0;
    return it->second.pid;
}

std::vector<std::string> ProcessManager::reapChildren() {
    std::vector<std::string> exited;

    for (auto it = processes_.begin(); it != processes_.end(); ) {
        ManagedProcess& proc = it->second;
        if (!proc.hProcess) {
            it = processes_.erase(it);
            continue;
        }

        if (!isProcessAlive(proc.hProcess)) {
            DWORD exitCode = 0;
            GetExitCodeProcess(proc.hProcess, &exitCode);
            LOG_INFO("Game '%s' (pid %lu) exited with code %lu",
                     proc.gameName.c_str(),
                     static_cast<unsigned long>(proc.pid),
                     static_cast<unsigned long>(exitCode));
            CloseHandle(proc.hProcess);
            exited.push_back(proc.gameName);
            it = processes_.erase(it);
        } else {
            // Force-kill after 10 seconds if stop was requested
            if (proc.stopping &&
                time(nullptr) - proc.stopSentAt > 10) {
                LOG_WARN("Game '%s' (pid %lu) didn't exit after CTRL_BREAK, "
                         "terminating",
                         proc.gameName.c_str(),
                         static_cast<unsigned long>(proc.pid));
                TerminateProcess(proc.hProcess, 1);
                proc.stopSentAt = time(nullptr);
            }
            ++it;
        }
    }

    return exited;
}

#else // POSIX

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

#endif // _WIN32
