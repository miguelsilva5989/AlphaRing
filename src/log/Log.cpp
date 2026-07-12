#include "Log.h"

#include "spdlog.h"
#include "sinks/rotating_file_sink.h"

#include "common.h"

#include <filesystem>

namespace AlphaRing::Log {
    std::shared_ptr<spdlog::logger> default_logger;
    static bool console_allocated = false;

    static bool ConsoleLoggingEnabled() {
        char value[8] = {};
        if (!GetEnvironmentVariableA("ALPHARING_CONSOLE", value, sizeof(value)))
            return false;

        return value[0] == '1' ||
               value[0] == 'y' ||
               value[0] == 'Y' ||
               value[0] == 't' ||
               value[0] == 'T';
    }

    bool Init() {
        try {
            const std::filesystem::path logPath = AlphaRing::Filesystem::DataPath("alpharing.log");

            // Create sinks - file sink is primary, console is optional
            std::vector<spdlog::sink_ptr> sinks;

            auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                    logPath.string(),
                    5 * 1024 * 1024,
                    3,
                    false
            );
            file_sink->set_level(spdlog::level::debug);
            sinks.push_back(file_sink);

            // Console sink - opt in because Proton gives AllocConsole its own focusable window.
            if (ConsoleLoggingEnabled() && AllocConsole()) {
                console_allocated = true;
                freopen("CONIN$", "r", stdin);
                freopen("CONOUT$", "w", stdout);
                freopen("CONOUT$", "w", stderr);

                auto console_sink = std::make_shared<spdlog::sinks::wincolor_stdout_sink_mt>();
                console_sink->set_level(spdlog::level::info);
                sinks.push_back(console_sink);
            }

            default_logger = std::make_shared<spdlog::logger>("default", sinks.begin(), sinks.end());
            default_logger->set_level(spdlog::level::debug);
            default_logger->flush_on(spdlog::level::warn);

            spdlog::register_logger(default_logger);

            LOG_INFO("=== AlphaRing Started ===");
            LOG_INFO("Log file: {}", logPath.string());

            return true;
        } catch (const std::exception& e) {
            // Fallback if file logging fails
            OutputDebugStringA("AlphaRing: Failed to initialize logging: ");
            OutputDebugStringA(e.what());
            return false;
        }
    }

    bool Shutdown() {
        LOG_INFO("=== AlphaRing Shutdown ===");

        if (default_logger) {
            default_logger->flush();
        }

        if (console_allocated) {
            fclose(stdin);
            fclose(stdout);
            fclose(stderr);
            FreeConsole();
        }

        return true;
    }
}
