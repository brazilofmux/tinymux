#include "hydra_log.h"
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>

static FILE* s_logFile = nullptr;
static LogLevel s_logLevel = LogLevel::Info;
static std::mutex s_logMutex;

static LogLevel parseLevel(const std::string& level) {
    if (level == "debug") return LogLevel::Debug;
    if (level == "info")  return LogLevel::Info;
    if (level == "warn")  return LogLevel::Warn;
    if (level == "error") return LogLevel::Error;
    return LogLevel::Info;
}

static const char* levelString(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "INFO";
}

bool logInit(const std::string& filePath, const std::string& level) {
    s_logLevel = parseLevel(level);

    if (filePath.empty() || filePath == "-") {
        s_logFile = stderr;
        return true;
    }

    s_logFile = fopen(filePath.c_str(), "a");
    if (!s_logFile) {
        fprintf(stderr, "HYDRA: cannot open log file: %s\n", filePath.c_str());
        s_logFile = stderr;
        return false;
    }
    return true;
}

void logShutdown() {
    if (s_logFile && s_logFile != stderr) {
        fclose(s_logFile);
    }
    s_logFile = nullptr;
}

void logMessage(LogLevel level, const char* fmt, ...) {
    if (level < s_logLevel) return;
    if (!s_logFile) return;

    time_t now = time(nullptr);
    struct tm tm;
#if defined(_WIN32)
    _localtime64_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif

    char timebuf[32];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tm);

    std::lock_guard<std::mutex> lock(s_logMutex);

    fprintf(s_logFile, "%s [%s] ", timebuf, levelString(level));

    va_list args;
    va_start(args, fmt);
    vfprintf(s_logFile, fmt, args);
    va_end(args);

    fprintf(s_logFile, "\n");
    fflush(s_logFile);
}

void hydraGanlLogCallback(const char* message) {
    logMessage(LogLevel::Debug, "GANL: %s", message);
}
