#include "logger.h"
#include <iostream>
#include <sstream>
#include <iomanip>

namespace photorec
{
    void Logger::Log(LogLevel level, const std::string& message,
                     const std::string& function,
                     const std::string& file, const int line)
    {
        if (level < current_level_)
            return;

        std::lock_guard<std::mutex> lock(log_mutex_);

        std::cout << GetTimestamp() << " [" << GetLevelString(level) << "] "
            << GetLocationString(function, file, line) << ": " << message << std::endl;
        std::flush(std::cout);
    }

    void Logger::Debug(const std::string& message, const std::string& function,
                       const std::string& file, const int line)
    {
        Log(LogLevel::DEBUG, message, function, file, line);
    }

    void Logger::Info(const std::string& message, const std::string& function,
                      const std::string& file, const int line)
    {
        Log(LogLevel::INFO, message, function, file, line);
    }

    void Logger::Warning(const std::string& message, const std::string& function,
                         const std::string& file, const int line)
    {
        Log(LogLevel::WARNING, message, function, file, line);
    }

    void Logger::Error(const std::string& message, const std::string& function,
                       const std::string& file, const int line)
    {
        Log(LogLevel::ERROR, message, function, file, line);
    }

    std::string Logger::GetTimestamp()
    {
        const auto now = std::chrono::system_clock::now();
        const auto time_t = std::chrono::system_clock::to_time_t(now);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return ss.str();
    }

    std::string Logger::GetLevelString(const LogLevel level)
    {
        switch (level)
        {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO: return "INFO ";
        case LogLevel::WARNING: return "WARN ";
        case LogLevel::ERROR: return "ERROR";
        default: return "UNKNOWN";
        }
    }

    std::string Logger::GetLocationString(const std::string& function,
                                          const std::string& file, const int line)
    {
        if (function.empty() && file.empty() && line == 0)
            return "";

        std::stringstream ss;
        if (!function.empty())
            ss << function;
        if (!file.empty())
        {
            if (!function.empty())
                ss << " ";
            // Extract just the filename from the path
            size_t last_slash = file.find_last_of("/\\");
            std::string filename = (last_slash != std::string::npos)
                                       ? file.substr(last_slash + 1)
                                       : file;
            ss << "(" << filename;
            if (line > 0)
                ss << ":" << line;
            ss << ")";
        }
        return ss.str();
    }
} // namespace photorec
