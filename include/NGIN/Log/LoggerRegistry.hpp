#pragma once

#include <NGIN/Log/Export.hpp>
#include <NGIN/Log/Logger.hpp>

#include <algorithm>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace NGIN::Log
{
    struct LoggerConfig
    {
        LogLevel              runtimeMin {LogLevel::Info};
        std::vector<SinkPtr>  sinks {};
    };

    struct LoggerRule
    {
        std::string                     prefix {};
        std::optional<LogLevel>         runtimeMin {};
        std::optional<std::vector<SinkPtr>> sinks {};
    };

    namespace detail
    {
        [[nodiscard]] inline auto LoggerNameMatchesPrefix(const std::string_view name, const std::string_view prefix) noexcept -> bool
        {
            if (prefix.empty())
            {
                return true;
            }

            if (name == prefix)
            {
                return true;
            }

            return name.size() > prefix.size() && name.starts_with(prefix) && name[prefix.size()] == '.';
        }
    }

    template<LogLevel CompileTimeMin = LogLevel::Trace, class FormatterPolicy = StdFormatter>
    class BasicLoggerRegistry
    {
    public:
        using LoggerType = Logger<CompileTimeMin, FormatterPolicy>;
        using LoggerPtr  = NGIN::Memory::Shared<LoggerType>;
        using SinkSet    = typename LoggerType::SinkSet;

        [[nodiscard]] auto GetOrCreate(std::string name, const LogLevel runtimeMin = LogLevel::Info) -> LoggerPtr
        {
            {
                std::shared_lock lock(m_mutex);
                if (const auto it = m_loggers.find(name); it != m_loggers.end())
                {
                    return it->second;
                }
            }

            std::unique_lock lock(m_mutex);
            if (const auto it = m_loggers.find(name); it != m_loggers.end())
            {
                return it->second;
            }

            LoggerConfig config = ComputeEffectiveConfigLocked(name);
            if (config.runtimeMin == m_defaultRuntimeMin && runtimeMin != LogLevel::Info)
            {
                config.runtimeMin = runtimeMin;
            }

            auto logger = NGIN::Memory::MakeShared<LoggerType>(name, config.runtimeMin, config.sinks);
            m_loggers.emplace(std::move(name), logger);
            return logger;
        }

        [[nodiscard]] auto Get(const std::string_view name) const noexcept -> LoggerPtr
        {
            std::shared_lock lock(m_mutex);
            const auto       it = m_loggers.find(std::string(name));
            if (it == m_loggers.end())
            {
                return {};
            }

            return it->second;
        }

        void SetDefaultRuntimeMin(const LogLevel runtimeMin) noexcept
        {
            std::unique_lock lock(m_mutex);
            m_defaultRuntimeMin = runtimeMin;
            ApplyConfigUpdatesLocked();
        }

        void SetDefaultSinks(SinkSet sinks) noexcept
        {
            std::unique_lock lock(m_mutex);
            m_defaultSinks = std::move(sinks);
            ApplyConfigUpdatesLocked();
        }

        [[nodiscard]] auto GetDefaultSinks() const -> SinkSet
        {
            std::shared_lock lock(m_mutex);
            return m_defaultSinks;
        }

        [[nodiscard]] auto GetDefaultRuntimeMin() const noexcept -> LogLevel
        {
            std::shared_lock lock(m_mutex);
            return m_defaultRuntimeMin;
        }

        void SetLoggerRuntimeMin(const std::string_view name, const LogLevel runtimeMin) noexcept
        {
            if (const auto logger = Get(name))
            {
                logger->SetRuntimeMin(runtimeMin);
            }
        }

        void ReplaceLoggerSinks(const std::string_view name, SinkSet sinks) noexcept
        {
            if (const auto logger = Get(name))
            {
                logger->SetSinks(std::move(sinks));
            }
        }

        void UpsertRule(LoggerRule rule) noexcept
        {
            std::unique_lock lock(m_mutex);
            for (auto& existingRule : m_rules)
            {
                if (existingRule.prefix == rule.prefix)
                {
                    existingRule = std::move(rule);
                    ApplyConfigUpdatesLocked();
                    return;
                }
            }

            m_rules.push_back(std::move(rule));
            ApplyConfigUpdatesLocked();
        }

        void RemoveRule(const std::string_view prefix) noexcept
        {
            std::unique_lock lock(m_mutex);
            m_rules.erase(
                std::remove_if(
                    m_rules.begin(),
                    m_rules.end(),
                    [&](const LoggerRule& rule) {
                        return rule.prefix == prefix;
                    }),
                m_rules.end());
            ApplyConfigUpdatesLocked();
        }

        [[nodiscard]] auto GetEffectiveConfig(const std::string_view name) const -> LoggerConfig
        {
            std::shared_lock lock(m_mutex);
            return ComputeEffectiveConfigLocked(name);
        }

        [[nodiscard]] auto GetLoggerNames() const -> std::vector<std::string>
        {
            std::vector<std::string> names;
            std::shared_lock         lock(m_mutex);
            names.reserve(m_loggers.size());
            for (const auto& [name, _] : m_loggers)
            {
                names.push_back(name);
            }

            std::sort(names.begin(), names.end());
            return names;
        }

    private:
        [[nodiscard]] auto ComputeEffectiveConfigLocked(const std::string_view name) const -> LoggerConfig
        {
            LoggerConfig config {
                .runtimeMin = m_defaultRuntimeMin,
                .sinks = m_defaultSinks,
            };

            const LoggerRule* bestRule = nullptr;
            for (const auto& rule : m_rules)
            {
                if (!detail::LoggerNameMatchesPrefix(name, rule.prefix))
                {
                    continue;
                }

                if (bestRule == nullptr || rule.prefix.size() > bestRule->prefix.size())
                {
                    bestRule = &rule;
                }
            }

            if (bestRule != nullptr)
            {
                if (bestRule->runtimeMin.has_value())
                {
                    config.runtimeMin = *bestRule->runtimeMin;
                }
                if (bestRule->sinks.has_value())
                {
                    config.sinks = *bestRule->sinks;
                }
            }

            return config;
        }

        void ApplyConfigUpdatesLocked() noexcept
        {
            for (const auto& [name, logger] : m_loggers)
            {
                if (!logger)
                {
                    continue;
                }

                const auto config = ComputeEffectiveConfigLocked(name);
                logger->SetRuntimeMin(config.runtimeMin);
                logger->SetSinks(config.sinks);
            }
        }

        mutable std::shared_mutex              m_mutex {};
        std::unordered_map<std::string, LoggerPtr> m_loggers {};
        SinkSet                               m_defaultSinks {};
        LogLevel                              m_defaultRuntimeMin {LogLevel::Info};
        std::vector<LoggerRule>               m_rules {};
    };

    using LoggerRegistry = BasicLoggerRegistry<LogLevel::Trace, StdFormatter>;
}
