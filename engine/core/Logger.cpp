
#include <cstdio>
#include <chrono>
#include "engine/core/Logger.h"

namespace cge{
    static const auto s_startTime = std::chrono::steady_clock::now();

    void Logger::log(LogLevel level, const std::string& msg)
    {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - s_startTime).count();
        const char* label = "?";
        const char* color = "";
        switch (level) {
            case LogLevel::Info:  label = "INFO "; color = "\033[92m"; break;
            case LogLevel::Warn:  label = "WARN "; color = "\033[93m"; break;
            case LogLevel::Error: label = "ERROR"; color = "\033[91m"; break;
        }
        std::printf("[%8lld ms] %s[%s]\033[0m %s\n", static_cast<long long>(ms),
                color, label, msg.c_str());
    }

}


