#pragma once

#include <NGIN/Primitives.hpp>

#include <string_view>

namespace NGIN::Log
{
    /// @brief Logging severity levels.
    enum class LogLevel : NGIN::UInt8
    {
        Trace = 0,
        Debug = 1,
        Info  = 2,
        Warn  = 3,
        Error = 4,
        Fatal = 5,
        Off   = 6,
    };

    /// @brief Convert level enum to printable name.
    [[nodiscard]] constexpr auto ToString(const LogLevel level) noexcept -> std::string_view
    {
        switch (level)
        {
            case LogLevel::Trace: return "Trace";
            case LogLevel::Debug: return "Debug";
            case LogLevel::Info: return "Info";
            case LogLevel::Warn: return "Warn";
            case LogLevel::Error: return "Error";
            case LogLevel::Fatal: return "Fatal";
            case LogLevel::Off: return "Off";
            default: return "Unknown";
        }
    }
}
