#include "Logger.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>

namespace archi
{
    namespace
    {
        std::filesystem::path MakeSiblingLogPath(
            const std::filesystem::path& basePath,
            const std::string& suffix)
        {
            const std::filesystem::path parentPath = basePath.parent_path();
            const std::string fileName =
                basePath.stem().string() + suffix + basePath.extension().string();
            return parentPath.empty() ? std::filesystem::path(fileName) : (parentPath / fileName);
        }
    }

    static std::string NowTimestamp()
    {
        using namespace std::chrono;

        const auto now = system_clock::now();
        const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

        std::time_t t = system_clock::to_time_t(now);
        std::tm tm{};
#if defined(_WIN32)
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif

        std::ostringstream oss;
        oss << std::put_time(&tm, "%H:%M:%S") << '.' << std::setw(3) << std::setfill('0') << ms.count();
        return oss.str();
    }

    void Logger::Init(const std::string& logFilePath)
    {
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            if (s_initialized)
                return;

            s_logFilePath = std::filesystem::path(logFilePath);
            s_traceFilePath = MakeSiblingLogPath(s_logFilePath, "_trace");
            s_runtimeFilePath = MakeSiblingLogPath(s_logFilePath, "_runtime");

            s_file.open(s_logFilePath, std::ios::out | std::ios::app);
            s_traceFile.open(s_traceFilePath, std::ios::out | std::ios::app);
            s_runtimeFile.open(s_runtimeFilePath, std::ios::out | std::ios::app);
            s_initialized = true;
        }

        // Log outside the mutex to avoid self-deadlock.
        Logger::Info(
            "Logger initialized. Combined log: ",
            s_logFilePath.string(),
            " | runtime log: ",
            s_runtimeFilePath.string(),
            " | trace log: ",
            s_traceFilePath.string());
    }

    void Logger::Shutdown()
    {
        bool wasInitialized = false;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            wasInitialized = s_initialized;
        }
        if (!wasInitialized)
            return;

        // Log outside the mutex to avoid self-deadlock.
        Logger::Info("Logger shutdown.");

        {
            std::lock_guard<std::mutex> lock(s_mutex);
            if (s_file.is_open())
                s_file.close();
            if (s_traceFile.is_open())
                s_traceFile.close();
            if (s_runtimeFile.is_open())
                s_runtimeFile.close();

            s_logFilePath.clear();
            s_traceFilePath.clear();
            s_runtimeFilePath.clear();
            s_initialized = false;
        }
    }

    void Logger::SetMinLevel(LogLevel level)
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_minLevel = level;
    }

    const char* Logger::ToString(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::Trace:
            return "TRACE";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warn:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
        default:
            return "UNKNOWN";
        }
    }

    void Logger::LogImpl(LogLevel level, const std::string& message)
    {
        std::lock_guard<std::mutex> lock(s_mutex);

        const std::string line = "[" + NowTimestamp() + "][" + ToString(level) + "] " + message;

        if (level == LogLevel::Trace)
        {
            if (s_traceFile.is_open())
                s_traceFile << line << std::endl;
        }
        else
        {
            std::cout << line << std::endl;
            if (s_runtimeFile.is_open())
                s_runtimeFile << line << std::endl;
        }

        if (s_file.is_open())
            s_file << line << std::endl;
    }
}

