#ifndef HYDRA_LOG_H
#define HYDRA_LOG_H

#include <string>

enum class LogLevel {
    Debug,
    Info,
    Warn,
    Error,
};

// Initialize logging.  Opens the log file and sets the level.
bool logInit(const std::string& filePath, const std::string& level);

// Close the log file.
void logShutdown();

// Reopen the log file (for logrotate integration via SIGHUP).
void logReopen();

// Log a message at the given level.
void logMessage(LogLevel level, const char* fmt, ...)
    __attribute__((format(printf, 2, 3)));

// Convenience macros.
#define LOG_DEBUG(fmt, ...) logMessage(LogLevel::Debug, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  logMessage(LogLevel::Info,  fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  logMessage(LogLevel::Warn,  fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) logMessage(LogLevel::Error, fmt, ##__VA_ARGS__)

// GANL log callback (registered via ganl::setLogger).
void hydraGanlLogCallback(const char* message);

#endif // HYDRA_LOG_H
