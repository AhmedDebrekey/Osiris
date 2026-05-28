//
// Created by ahtal on 28/05/2026.
//

#include "Log.h"

#include <spdlog/sinks/stdout_color_sinks.h>

namespace Osiris {
    std::shared_ptr<spdlog::logger> Log::s_CoreLogger;
    std::shared_ptr<spdlog::logger> Log::s_ClientLogger;

    bool Log::Initialize()
    {
        // Pattern: [HH:MM:SS] [LEVEL] [LOGGER] message
        spdlog::set_pattern("%^[%T] [%l] %n: %v%$");

        // Core engine logger — outputs to console
        s_CoreLogger = spdlog::stdout_color_mt("OSIRIS");
        s_CoreLogger->set_level(spdlog::level::trace);

        // Client/game logger — outputs to console
        s_ClientLogger = spdlog::stdout_color_mt("APP");
        s_ClientLogger->set_level(spdlog::level::trace);

        OSIRIS_INFO("Logger initialised");
        return true;
    }
} // Osiris