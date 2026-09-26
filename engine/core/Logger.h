#pragma once
#include <string>

namespace cge {

enum class LogLevel { Info, Warn, Error };

class Logger {
public:
    static void log(LogLevel level, const std::string& msg);
};

} // namespace cge

#define CGE_LOG_INFO(msg)  do { ::cge::Logger::log(::cge::LogLevel::Info,  msg); } while (0)
#define CGE_LOG_WARN(msg)  do { ::cge::Logger::log(::cge::LogLevel::Warn,  msg); } while (0)
#define CGE_LOG_ERROR(msg) do { ::cge::Logger::log(::cge::LogLevel::Error, msg); } while (0)