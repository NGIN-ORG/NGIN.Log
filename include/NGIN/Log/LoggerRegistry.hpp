#pragma once

#include <NGIN/Log/Export.hpp>
#include <NGIN/Log/Logger.hpp>

#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace NGIN::Log
{
    /// @brief Registry for named logger instances and default sink/runtime-level configuration.
    class NGIN_LOG_API LoggerRegistry
    {
    public:
        using LoggerType = Logger<LogLevel::Trace>;
        using LoggerPtr  = NGIN::Memory::Shared<LoggerType>;
        using SinkSet    = typename LoggerType::SinkSet;

        /// @brief Construct an empty registry.
        LoggerRegistry();

        /// @brief Get or create a named logger.
        /// @param name Logger name.
        /// @param runtimeMin Runtime level used only when creating a new logger.
        [[nodiscard]] auto GetOrCreate(std::string name, LogLevel runtimeMin = LogLevel::Info) -> LoggerPtr;

        /// @brief Get an existing logger by name.
        [[nodiscard]] auto Get(std::string_view name) const noexcept -> LoggerPtr;

        /// @brief Set default sinks used by subsequently created loggers.
        void SetDefaultSinks(SinkSet sinks) noexcept;

        /// @brief Get a copy of the default sink set snapshot.
        [[nodiscard]] auto GetDefaultSinks() const -> SinkSet;

        /// @brief Update runtime minimum level for an existing logger.
        void SetLoggerRuntimeMin(std::string_view name, LogLevel runtimeMin) noexcept;

        /// @brief Replace sink snapshot for an existing logger.
        void ReplaceLoggerSinks(std::string_view name, SinkSet sinks) noexcept;

        /// @brief Enumerate known logger names.
        [[nodiscard]] auto GetLoggerNames() const -> std::vector<std::string>;

    private:
        mutable std::shared_mutex                         m_mutex;
        std::unordered_map<std::string, LoggerPtr> m_loggers;
        SinkSet                                      m_defaultSinks;
    };
}
