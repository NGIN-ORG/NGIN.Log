#include <NGIN/Log/LoggerRegistry.hpp>

#include <algorithm>
#include <mutex>

namespace NGIN::Log
{
    LoggerRegistry::LoggerRegistry() = default;

    auto LoggerRegistry::GetOrCreate(std::string name, const LogLevel runtimeMin) -> LoggerPtr
    {
        {
            std::shared_lock lock(m_mutex);
            const auto it = m_loggers.find(name);
            if (it != m_loggers.end())
            {
                return it->second;
            }
        }

        std::unique_lock lock(m_mutex);
        if (const auto it = m_loggers.find(name); it != m_loggers.end())
        {
            return it->second;
        }

        auto logger = NGIN::Memory::MakeShared<LoggerType>(std::move(name), runtimeMin, m_defaultSinks);
        m_loggers.emplace(std::string(logger->GetName()), logger);
        return logger;
    }

    auto LoggerRegistry::Get(const std::string_view name) const noexcept -> LoggerPtr
    {
        std::shared_lock lock(m_mutex);
        const auto       it = m_loggers.find(std::string(name));
        if (it == m_loggers.end())
        {
            return {};
        }
        return it->second;
    }

    void LoggerRegistry::SetDefaultSinks(SinkSet sinks) noexcept
    {
        std::unique_lock lock(m_mutex);
        m_defaultSinks = std::move(sinks);
    }

    auto LoggerRegistry::GetDefaultSinks() const -> SinkSet
    {
        std::shared_lock lock(m_mutex);
        return m_defaultSinks;
    }

    void LoggerRegistry::SetLoggerRuntimeMin(const std::string_view name, const LogLevel runtimeMin) noexcept
    {
        LoggerPtr logger;
        {
            std::shared_lock lock(m_mutex);
            const auto it = m_loggers.find(std::string(name));
            if (it == m_loggers.end())
            {
                return;
            }
            logger = it->second;
        }

        if (logger)
        {
            logger->SetRuntimeMin(runtimeMin);
        }
    }

    void LoggerRegistry::ReplaceLoggerSinks(std::string_view name, SinkSet sinks) noexcept
    {
        LoggerPtr logger;
        {
            std::shared_lock lock(m_mutex);
            const auto it = m_loggers.find(std::string(name));
            if (it == m_loggers.end())
            {
                return;
            }
            logger = it->second;
        }

        if (logger)
        {
            logger->SetSinks(std::move(sinks));
        }
    }

    auto LoggerRegistry::GetLoggerNames() const -> std::vector<std::string>
    {
        std::vector<std::string> names;

        std::shared_lock lock(m_mutex);
        names.reserve(m_loggers.size());
        for (const auto& [name, _] : m_loggers)
        {
            names.push_back(name);
        }

        std::sort(names.begin(), names.end());
        return names;
    }
}
