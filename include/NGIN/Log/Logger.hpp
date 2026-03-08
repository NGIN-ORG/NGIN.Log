#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/Log/FormatterPolicy.hpp>
#include <NGIN/Log/LogLevel.hpp>
#include <NGIN/Log/RecordBuilder.hpp>
#include <NGIN/Log/Sink.hpp>

#include <atomic>
#include <chrono>
#include <concepts>
#include <deque>
#include <format>
#include <functional>
#include <mutex>
#include <source_location>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace NGIN::Log
{
    /// @brief Logger with compile-time and runtime filtering and lock-free sink read fan-out.
    /// @details Sink dispatch reads are lock-free; sink reconfiguration synchronizes on write path.
    /// @tparam CompileTimeMin Compile-time minimum level.
    /// @tparam TFormatterPolicy Formatting policy backend.
    template<LogLevel CompileTimeMin, class TFormatterPolicy = StdFormatter>
    class Logger
    {
    public:
        using FormatterPolicy = TFormatterPolicy;
        using SinkSet         = std::vector<SinkPtr>;

        /// @brief Construct a logger with initial runtime level and sink set.
        /// @param name Logger name.
        /// @param runtimeMin Runtime minimum enabled level.
        /// @param sinks Initial sinks.
        explicit Logger(
            std::string name,
            const LogLevel runtimeMin = LogLevel::Info,
            SinkSet sinks = {})
            : m_name(std::move(name))
            , m_runtimeMin(static_cast<NGIN::UInt8>(runtimeMin))
        {
            m_sinkGenerations.emplace_back(std::move(sinks));
            m_activeSinks.store(&m_sinkGenerations.back(), std::memory_order_release);
        }

        /// @brief Set runtime minimum level.
        void SetRuntimeMin(const LogLevel level) noexcept
        {
            m_runtimeMin.store(static_cast<NGIN::UInt8>(level), std::memory_order_release);
        }

        /// @brief Read runtime minimum level.
        [[nodiscard]] auto GetRuntimeMin() const noexcept -> LogLevel
        {
            return static_cast<LogLevel>(m_runtimeMin.load(std::memory_order_acquire));
        }

        /// @brief Replace sink set by publishing a new immutable generation.
        /// @param sinks New sink set snapshot.
        void SetSinks(SinkSet sinks) noexcept
        {
            try
            {
                std::lock_guard lock(m_sinkMutationMutex);
                m_sinkGenerations.emplace_back(std::move(sinks));
                m_activeSinks.store(&m_sinkGenerations.back(), std::memory_order_release);
            }
            catch (...)
            {
                m_sinkErrorCount.fetch_add(1, std::memory_order_relaxed);
            }
        }

        /// @brief Get a copy of the currently active sink set.
        [[nodiscard]] auto GetSinksSnapshot() const -> SinkSet
        {
            const auto* sinks = m_activeSinks.load(std::memory_order_acquire);
            if (sinks == nullptr)
            {
                return {};
            }

            return *sinks;
        }

        /// @brief Logger name.
        [[nodiscard]] auto GetName() const noexcept -> std::string_view
        {
            return m_name;
        }

        /// @brief Number of sink-dispatch failures observed by this logger.
        [[nodiscard]] auto GetSinkErrorCount() const noexcept -> NGIN::UInt64
        {
            return m_sinkErrorCount.load(std::memory_order_relaxed);
        }

        /// @brief Flush all currently active sinks.
        void Flush() noexcept
        {
            const auto* sinks = m_activeSinks.load(std::memory_order_acquire);
            if (sinks == nullptr)
            {
                return;
            }

            for (const auto& sink : *sinks)
            {
                if (!sink)
                {
                    continue;
                }

                try
                {
                    sink->Flush();
                }
                catch (...)
                {
                    m_sinkErrorCount.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }

        /// @brief Primary zero-overhead lazy API.
        /// @tparam L Call-site static log level.
        /// @tparam Fn Callable invoked only when level is enabled.
        /// @param fn Callable accepting `RecordBuilder&` or zero args.
        /// @param source Source location captured at call site.
        /// @details Calls below `CompileTimeMin` are compiled out via `if constexpr`.
        /// Calls at/above `CompileTimeMin` are additionally filtered by runtime minimum level.
        template<LogLevel L, class Fn>
        NGIN_FORCEINLINE void Log(Fn&& fn, const std::source_location source = std::source_location::current()) noexcept
        {
            if constexpr (static_cast<NGIN::UInt8>(L) >= static_cast<NGIN::UInt8>(CompileTimeMin))
            {
                if (!IsRuntimeEnabled(L))
                {
                    return;
                }

                RecordBuilder builder;

                try
                {
                    if constexpr (std::is_invocable_v<Fn&, RecordBuilder&>)
                    {
                        std::invoke(fn, builder);
                    }
                    else if constexpr (std::is_invocable_v<Fn&>)
                    {
                        std::invoke(fn);
                    }
                }
                catch (...)
                {
                    builder.Message("[log-builder-threw]");
                    m_sinkErrorCount.fetch_add(1, std::memory_order_relaxed);
                }

                Dispatch(L, builder, source);
            }
        }

        /// @brief Formatting API with explicit source location.
        /// @note `*f` APIs evaluate formatting arguments regardless of level checks.
        /// @note Explicit source is required for accurate caller metadata in macro-free code.
        template<LogLevel L, class... Args>
        NGIN_FORCEINLINE void Logf(
            const std::source_location source,
            std::format_string<Args...> fmt,
            Args&&... args) noexcept
        {
            Log<L>(
                [&](RecordBuilder& builder) {
                    FormatterPolicy::Format(builder, fmt, std::forward<Args>(args)...);
                },
                source);
        }

        /// @brief Runtime-format API with explicit source location.
        /// @note This dynamic-format path may allocate inside `std::vformat`.
        template<LogLevel L>
        NGIN_FORCEINLINE void Logfv(
            const std::source_location source,
            const std::string_view fmt,
            const std::format_args args) noexcept
        {
            Log<L>(
                [&](RecordBuilder& builder) {
                    FormatterPolicy::FormatV(builder, fmt, args);
                },
                source);
        }

        /// @brief Trace-level lazy logging.
        template<class Fn>
        NGIN_FORCEINLINE void Trace(Fn&& fn, const std::source_location source = std::source_location::current()) noexcept
        {
            Log<LogLevel::Trace>(std::forward<Fn>(fn), source);
        }

        /// @brief Debug-level lazy logging.
        template<class Fn>
        NGIN_FORCEINLINE void Debug(Fn&& fn, const std::source_location source = std::source_location::current()) noexcept
        {
            Log<LogLevel::Debug>(std::forward<Fn>(fn), source);
        }

        /// @brief Info-level lazy logging.
        template<class Fn>
        NGIN_FORCEINLINE void Info(Fn&& fn, const std::source_location source = std::source_location::current()) noexcept
        {
            Log<LogLevel::Info>(std::forward<Fn>(fn), source);
        }

        /// @brief Warn-level lazy logging.
        template<class Fn>
        NGIN_FORCEINLINE void Warn(Fn&& fn, const std::source_location source = std::source_location::current()) noexcept
        {
            Log<LogLevel::Warn>(std::forward<Fn>(fn), source);
        }

        /// @brief Error-level lazy logging.
        template<class Fn>
        NGIN_FORCEINLINE void Error(Fn&& fn, const std::source_location source = std::source_location::current()) noexcept
        {
            Log<LogLevel::Error>(std::forward<Fn>(fn), source);
        }

        /// @brief Fatal-level lazy logging.
        template<class Fn>
        NGIN_FORCEINLINE void Fatal(Fn&& fn, const std::source_location source = std::source_location::current()) noexcept
        {
            Log<LogLevel::Fatal>(std::forward<Fn>(fn), source);
        }

        /// @brief Trace-level formatting helper with explicit source.
        template<class... Args>
        NGIN_FORCEINLINE void Tracef(
            const std::source_location source,
            std::format_string<Args...> fmt,
            Args&&... args) noexcept
        {
            Logf<LogLevel::Trace>(source, fmt, std::forward<Args>(args)...);
        }

        /// @brief Trace-level runtime-format helper with explicit source.
        NGIN_FORCEINLINE void Tracefv(
            const std::source_location source,
            const std::string_view fmt,
            const std::format_args args) noexcept
        {
            Logfv<LogLevel::Trace>(source, fmt, args);
        }

        /// @brief Debug-level formatting helper with explicit source.
        template<class... Args>
        NGIN_FORCEINLINE void Debugf(
            const std::source_location source,
            std::format_string<Args...> fmt,
            Args&&... args) noexcept
        {
            Logf<LogLevel::Debug>(source, fmt, std::forward<Args>(args)...);
        }

        /// @brief Debug-level runtime-format helper with explicit source.
        NGIN_FORCEINLINE void Debugfv(
            const std::source_location source,
            const std::string_view fmt,
            const std::format_args args) noexcept
        {
            Logfv<LogLevel::Debug>(source, fmt, args);
        }

        /// @brief Info-level formatting helper with explicit source.
        template<class... Args>
        NGIN_FORCEINLINE void Infof(
            const std::source_location source,
            std::format_string<Args...> fmt,
            Args&&... args) noexcept
        {
            Logf<LogLevel::Info>(source, fmt, std::forward<Args>(args)...);
        }

        /// @brief Info-level runtime-format helper with explicit source.
        NGIN_FORCEINLINE void Infofv(
            const std::source_location source,
            const std::string_view fmt,
            const std::format_args args) noexcept
        {
            Logfv<LogLevel::Info>(source, fmt, args);
        }

        /// @brief Warn-level formatting helper with explicit source.
        template<class... Args>
        NGIN_FORCEINLINE void Warnf(
            const std::source_location source,
            std::format_string<Args...> fmt,
            Args&&... args) noexcept
        {
            Logf<LogLevel::Warn>(source, fmt, std::forward<Args>(args)...);
        }

        /// @brief Warn-level runtime-format helper with explicit source.
        NGIN_FORCEINLINE void Warnfv(
            const std::source_location source,
            const std::string_view fmt,
            const std::format_args args) noexcept
        {
            Logfv<LogLevel::Warn>(source, fmt, args);
        }

        /// @brief Error-level formatting helper with explicit source.
        template<class... Args>
        NGIN_FORCEINLINE void Errorf(
            const std::source_location source,
            std::format_string<Args...> fmt,
            Args&&... args) noexcept
        {
            Logf<LogLevel::Error>(source, fmt, std::forward<Args>(args)...);
        }

        /// @brief Error-level runtime-format helper with explicit source.
        NGIN_FORCEINLINE void Errorfv(
            const std::source_location source,
            const std::string_view fmt,
            const std::format_args args) noexcept
        {
            Logfv<LogLevel::Error>(source, fmt, args);
        }

        /// @brief Fatal-level formatting helper with explicit source.
        template<class... Args>
        NGIN_FORCEINLINE void Fatalf(
            const std::source_location source,
            std::format_string<Args...> fmt,
            Args&&... args) noexcept
        {
            Logf<LogLevel::Fatal>(source, fmt, std::forward<Args>(args)...);
        }

        /// @brief Fatal-level runtime-format helper with explicit source.
        NGIN_FORCEINLINE void Fatalfv(
            const std::source_location source,
            const std::string_view fmt,
            const std::format_args args) noexcept
        {
            Logfv<LogLevel::Fatal>(source, fmt, args);
        }

    private:
        [[nodiscard]] NGIN_FORCEINLINE auto IsRuntimeEnabled(const LogLevel level) const noexcept -> bool
        {
            const auto runtimeMin = m_runtimeMin.load(std::memory_order_acquire);
            return static_cast<NGIN::UInt8>(level) >= runtimeMin && level != LogLevel::Off;
        }

        [[nodiscard]] static auto NowEpochNanoseconds() noexcept -> NGIN::UInt64
        {
            using namespace std::chrono;
            const auto now = system_clock::now().time_since_epoch();
            return static_cast<NGIN::UInt64>(duration_cast<nanoseconds>(now).count());
        }

        [[nodiscard]] static auto CurrentThreadHash() noexcept -> NGIN::UInt64
        {
            return static_cast<NGIN::UInt64>(std::hash<std::thread::id> {}(std::this_thread::get_id()));
        }

        void Dispatch(const LogLevel level, const RecordBuilder& builder, const std::source_location source) noexcept
        {
            const auto* sinks = m_activeSinks.load(std::memory_order_acquire);
            if (sinks == nullptr || sinks->empty())
            {
                return;
            }

            if (s_inDispatch)
            {
                m_sinkErrorCount.fetch_add(1, std::memory_order_relaxed);
                return;
            }

            s_inDispatch = true;

            const LogRecordView record {
                .timestampEpochNanoseconds = NowEpochNanoseconds(),
                .level = level,
                .loggerName = m_name,
                .message = builder.GetMessage(),
                .source = source,
                .threadIdHash = CurrentThreadHash(),
                .attributes = builder.GetAttributes(),
                .truncatedAttributeCount = builder.GetTruncatedAttributeCount(),
                .truncatedBytes = builder.GetTruncatedBytes(),
            };

            for (const auto& sink : *sinks)
            {
                if (!sink)
                {
                    continue;
                }

                try
                {
                    sink->Write(record);
                }
                catch (...)
                {
                    m_sinkErrorCount.fetch_add(1, std::memory_order_relaxed);
                }
            }

            s_inDispatch = false;
        }

        inline static thread_local bool s_inDispatch = false;

        std::string                   m_name;
        std::atomic<NGIN::UInt8>      m_runtimeMin {static_cast<NGIN::UInt8>(LogLevel::Info)};
        std::atomic<const SinkSet*>   m_activeSinks {nullptr};

        // Write-side generation ownership to keep published sink pointers valid for reader hot path.
        mutable std::mutex            m_sinkMutationMutex {};
        std::deque<SinkSet>           m_sinkGenerations {};

        std::atomic<NGIN::UInt64>     m_sinkErrorCount {0};
    };
}
