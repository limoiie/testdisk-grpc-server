#pragma once

#include <string>
#include <mutex>

namespace photorec
{
    /**
     * @brief Log levels for the PhotoRec gRPC server
     */
    enum class LogLevel
    {
        DEBUG = 0,
        INFO = 1,
        WARNING = 2,
        ERROR = 3
    };

    /**
     * @brief Simple logger class for PhotoRec gRPC server
     */
    class Logger
    {
    public:
        static Logger& Instance()
        {
            static Logger instance;
            return instance;
        }

        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;

        void SetLogLevel(LogLevel level) { current_level_ = level; }
        [[nodiscard]] LogLevel GetLogLevel() const { return current_level_; }

        void Log(LogLevel level, const std::string& message,
                 const std::string& function = "",
                 const std::string& file = "", int line = 0);

        // Convenience methods
        void Debug(const std::string& message, const std::string& function = "",
                   const std::string& file = "", int line = 0);
        void Info(const std::string& message, const std::string& function = "",
                  const std::string& file = "", int line = 0);
        void Warning(const std::string& message, const std::string& function = "",
                     const std::string& file = "", int line = 0);
        void Error(const std::string& message, const std::string& function = "",
                   const std::string& file = "", int line = 0);

    private:
        Logger() = default;
        ~Logger() = default;

        static std::string GetTimestamp();
        static std::string GetLevelString(LogLevel level);
        static std::string GetLocationString(const std::string& function,
                                             const std::string& file, int line);

        LogLevel current_level_ = LogLevel::INFO;
        std::mutex log_mutex_ = std::mutex();
    };

    // Convenience macros for logging
#define LOG_DEBUG(msg) Logger::Instance().Debug(msg, __FUNCTION__, __FILE__, __LINE__)
#define LOG_INFO(msg) Logger::Instance().Info(msg, __FUNCTION__, __FILE__, __LINE__)
#define LOG_WARNING(msg) Logger::Instance().Warning(msg, __FUNCTION__, __FILE__, __LINE__)
#define LOG_ERROR(msg) Logger::Instance().Error(msg, __FUNCTION__, __FILE__, __LINE__)
} // namespace photorec
