//
// Created by Debreky on 28/05/2026.
//

#ifndef OSIRIS_LOG_H
#define OSIRIS_LOG_H

#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>

namespace Osiris {
    class Log {
    public:
        Log() = default;
        ~Log() = default;

        bool Initialize();

        static std::shared_ptr<spdlog::logger>& GetCoreLogger()   { return s_CoreLogger; }
        static std::shared_ptr<spdlog::logger>& GetClientLogger() { return s_ClientLogger;}

    private:
        static std::shared_ptr<spdlog::logger> s_CoreLogger;
        static std::shared_ptr<spdlog::logger> s_ClientLogger;
    };
} // Osiris

// Core engine log macros
#define OSIRIS_TRACE(...)    ::Osiris::Log::GetCoreLogger()->trace(__VA_ARGS__)
#define OSIRIS_INFO(...)     ::Osiris::Log::GetCoreLogger()->info(__VA_ARGS__)
#define OSIRIS_WARN(...)     ::Osiris::Log::GetCoreLogger()->warn(__VA_ARGS__)
#define OSIRIS_ERROR(...)    ::Osiris::Log::GetCoreLogger()->error(__VA_ARGS__)
#define OSIRIS_FATAL(...)    ::Osiris::Log::GetCoreLogger()->critical(__VA_ARGS__)

// Game/client log macros
#define APP_TRACE(...)       ::Osiris::Log::GetClientLogger()->trace(__VA_ARGS__)
#define APP_INFO(...)        ::Osiris::Log::GetClientLogger()->info(__VA_ARGS__)
#define APP_WARN(...)        ::Osiris::Log::GetClientLogger()->warn(__VA_ARGS__)
#define APP_ERROR(...)       ::Osiris::Log::GetClientLogger()->error(__VA_ARGS__)
#define APP_FATAL(...)       ::Osiris::Log::GetClientLogger()->critical(__VA_ARGS__)

#endif //OSIRIS_LOG_H