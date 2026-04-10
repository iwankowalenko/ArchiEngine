#pragma once

#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>

namespace archi
{
    enum class LogLevel
    {
        Trace = 0,
        Info,
        Warn,
        Error
    };

    class Logger final
    {
    public:
        static void Init(const std::string& logFilePath = "archi.log");
        static void Shutdown();

        static void SetMinLevel(LogLevel level);

        template <typename... Ts>
        static void Trace(Ts&&... ts)
        {
            Log(LogLevel::Trace, std::forward<Ts>(ts)...);
        }

        template <typename... Ts>
        static void Info(Ts&&... ts)
        {
            Log(LogLevel::Info, std::forward<Ts>(ts)...);
        }

        template <typename... Ts>
        static void Warn(Ts&&... ts)
        {
            Log(LogLevel::Warn, std::forward<Ts>(ts)...);
        }

        template <typename... Ts>
        static void Error(Ts&&... ts)
        {
            Log(LogLevel::Error, std::forward<Ts>(ts)...);
        }

        template <typename... Ts>
        static void Log(LogLevel level, Ts&&... ts)
        {
            if (static_cast<int>(level) < static_cast<int>(s_minLevel))
                return;

            std::ostringstream oss;
            (oss << ... << std::forward<Ts>(ts));
            LogImpl(level, oss.str());
        }

    private:
        static void LogImpl(LogLevel level, const std::string& message);
        static const char* ToString(LogLevel level);

    private:
        static inline std::mutex s_mutex{};
        static inline std::ofstream s_file{};
        static inline std::ofstream s_traceFile{};
        static inline std::ofstream s_runtimeFile{};
        static inline std::filesystem::path s_logFilePath{};
        static inline std::filesystem::path s_traceFilePath{};
        static inline std::filesystem::path s_runtimeFilePath{};
        static inline LogLevel s_minLevel = LogLevel::Trace;
        static inline bool s_initialized = false;
    };
}

