#include "slave_spawn_posix.h"

#ifndef _WIN32

#include <vector>
#include <string>
#include <csignal>
#include <cerrno>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <fcntl.h>

extern char** environ;

namespace ganl {

namespace {

void closeQuietly(int fd) {
    if (fd >= 0) {
        while (::close(fd) == -1 && errno == EINTR) {
        }
    }
}

void buildArgv(const SlaveSpawnOptions& options, std::vector<char*>& argvStorage) {
    if (options.arguments.empty()) {
        argvStorage.push_back(const_cast<char*>(options.executable.c_str()));
    } else {
        for (const auto& arg : options.arguments) {
            argvStorage.push_back(const_cast<char*>(arg.c_str()));
        }
    }
    argvStorage.push_back(nullptr);
}

char** buildEnvp(const SlaveSpawnOptions& options, std::vector<char*>& envStorage) {
    if (options.environment.empty()) {
        return environ;
    }
    envStorage.reserve(options.environment.size() + 1);
    for (const auto& entry : options.environment) {
        envStorage.push_back(const_cast<char*>(entry.c_str()));
    }
    envStorage.push_back(nullptr);
    return envStorage.data();
}

void ensureClearedCloexec(int fd) {
    int flags = fcntl(fd, F_GETFD, 0);
    if (flags != -1) {
        fcntl(fd, F_SETFD, flags & ~FD_CLOEXEC);
    }
}

} // namespace

ConnectionHandle spawnSlavePosix(NetworkEngine& engine, const SlaveSpawnOptions& options, ErrorCode& error) {
    error = 0;

    if (options.executable.empty()) {
        error = EINVAL;
        return InvalidConnectionHandle;
    }

    int sockets[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == -1) {
        error = errno;
        return InvalidConnectionHandle;
    }

    // Parent endpoint should not leak into child exec.
    int parentFd = sockets[0];
    int childFd = sockets[1];

    // Ensure parent end is close-on-exec (will be reconfigured during adoptConnection).
    int parentFlags = fcntl(parentFd, F_GETFD, 0);
    if (parentFlags != -1) {
        fcntl(parentFd, F_SETFD, parentFlags | FD_CLOEXEC);
    }

    pid_t pid = ::fork();
    if (pid == -1) {
        error = errno;
        closeQuietly(parentFd);
        closeQuietly(childFd);
        return InvalidConnectionHandle;
    }

    if (pid == 0) {
        // Child process.
        closeQuietly(parentFd);

        ensureClearedCloexec(childFd);

        int commFd = childFd;
        if (options.communicationFd >= 0 && options.communicationFd != childFd) {
            if (::dup2(childFd, options.communicationFd) == -1) {
                _exit(126);
            }
            closeQuietly(childFd);
            commFd = options.communicationFd;
            ensureClearedCloexec(commFd);
        }

        if (options.attachToStandardIO) {
            if (::dup2(commFd, STDIN_FILENO) == -1 ||
                ::dup2(commFd, STDOUT_FILENO) == -1 ||
                ::dup2(commFd, STDERR_FILENO) == -1) {
                _exit(126);
            }
        }

        std::vector<char*> argvStorage;
        buildArgv(options, argvStorage);
        std::vector<char*> envStorage;
        char** envp = buildEnvp(options, envStorage);

        ::execve(options.executable.c_str(), argvStorage.data(), envp);
        _exit(127);
    }

    // Parent path.
    closeQuietly(childFd);

    if (options.childPidOut) {
        *options.childPidOut = static_cast<int>(pid);
    }

    ConnectionHandle handle = engine.adoptConnection(parentFd, options.connectionContext, error);
    if (handle == InvalidConnectionHandle) {
        closeQuietly(parentFd);
        ::kill(pid, SIGKILL);
        int status = 0;
        while (::waitpid(pid, &status, 0) == -1 && errno == EINTR) {
        }
        return InvalidConnectionHandle;
    }

    return handle;
}

} // namespace ganl

#endif // !_WIN32
