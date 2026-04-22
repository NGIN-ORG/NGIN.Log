#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/Log/Context.hpp>
#include <NGIN/Log/LogLevel.hpp>
#include <NGIN/Log/RecordBuilder.hpp>
#include <NGIN/Log/Sink.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <concepts>
#include <functional>
#include <memory>
#include <source_location>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace NGIN::Log
{
    template<LogLevel CompileTimeMin>
    class Logger
    {
    public:
        using SinkSet         = std::vector<SinkPtr>;

        explicit Logger(
            std::string name,
            const LogLevel runtimeMin = LogLevel::Info,
            SinkSet sinks = {})
            : m_name(std::move(name))
            , m_runtimeMin(static_cast<NGIN::UInt8>(runtimeMin))
        {
            PublishSinkSnapshot(std::move(sinks));
        }

        void SetRuntimeMin(const LogLevel level) noexcept
        {
            m_runtimeMin.store(static_cast<NGIN::UInt8>(level), std::memory_order_release);
        }

        [[nodiscard]] auto GetRuntimeMin() const noexcept -> LogLevel
        {
            return static_cast<LogLevel>(m_runtimeMin.load(std::memory_order_acquire));
        }

        void SetSinks(SinkSet sinks) noexcept
        {
            PublishSinkSnapshot(std::move(sinks));
        }

        [[nodiscard]] auto GetSinksSnapshot() const -> SinkSet
        {
            if (const auto snapshot = GetActiveSinkSnapshot())
            {
                return *snapshot;
            }

            return {};
        }

        [[nodiscard]] auto GetName() const noexcept -> std::string_view
        {
            return m_name;
        }

        [[nodiscard]] auto GetSinkErrorCount() const noexcept -> NGIN::UInt64
        {
            return m_sinkErrorCount.load(std::memory_order_relaxed);
        }

        void Flush() noexcept
        {
            const auto sinks = GetActiveSinkSnapshot();
            if (!sinks)
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

        NGIN_FORCEINLINE void Trace(const std::string_view message, const std::source_location source = std::source_location::current()) noexcept
        {
            Log<LogLevel::Trace>([message](RecordBuilder& builder) { builder.Message(message); }, source);
        }

        template<class Fn>
            requires(std::is_invocable_v<Fn&, RecordBuilder&> || std::is_invocable_v<Fn&>)
        NGIN_FORCEINLINE void Trace(Fn&& fn, const std::source_location source = std::source_location::current()) noexcept
        {
            Log<LogLevel::Trace>(std::forward<Fn>(fn), source);
        }

        NGIN_FORCEINLINE void Debug(const std::string_view message, const std::source_location source = std::source_location::current()) noexcept
        {
            Log<LogLevel::Debug>([message](RecordBuilder& builder) { builder.Message(message); }, source);
        }

        template<class Fn>
            requires(std::is_invocable_v<Fn&, RecordBuilder&> || std::is_invocable_v<Fn&>)
        NGIN_FORCEINLINE void Debug(Fn&& fn, const std::source_location source = std::source_location::current()) noexcept
        {
            Log<LogLevel::Debug>(std::forward<Fn>(fn), source);
        }

        NGIN_FORCEINLINE void Info(const std::string_view message, const std::source_location source = std::source_location::current()) noexcept
        {
            Log<LogLevel::Info>([message](RecordBuilder& builder) { builder.Message(message); }, source);
        }

        template<class Fn>
            requires(std::is_invocable_v<Fn&, RecordBuilder&> || std::is_invocable_v<Fn&>)
        NGIN_FORCEINLINE void Info(Fn&& fn, const std::source_location source = std::source_location::current()) noexcept
        {
            Log<LogLevel::Info>(std::forward<Fn>(fn), source);
        }

        NGIN_FORCEINLINE void Warn(const std::string_view message, const std::source_location source = std::source_location::current()) noexcept
        {
            Log<LogLevel::Warn>([message](RecordBuilder& builder) { builder.Message(message); }, source);
        }

        template<class Fn>
            requires(std::is_invocable_v<Fn&, RecordBuilder&> || std::is_invocable_v<Fn&>)
        NGIN_FORCEINLINE void Warn(Fn&& fn, const std::source_location source = std::source_location::current()) noexcept
        {
            Log<LogLevel::Warn>(std::forward<Fn>(fn), source);
        }

        NGIN_FORCEINLINE void Error(const std::string_view message, const std::source_location source = std::source_location::current()) noexcept
        {
            Log<LogLevel::Error>([message](RecordBuilder& builder) { builder.Message(message); }, source);
        }

        template<class Fn>
            requires(std::is_invocable_v<Fn&, RecordBuilder&> || std::is_invocable_v<Fn&>)
        NGIN_FORCEINLINE void Error(Fn&& fn, const std::source_location source = std::source_location::current()) noexcept
        {
            Log<LogLevel::Error>(std::forward<Fn>(fn), source);
        }

        NGIN_FORCEINLINE void Fatal(const std::string_view message, const std::source_location source = std::source_location::current()) noexcept
        {
            Log<LogLevel::Fatal>([message](RecordBuilder& builder) { builder.Message(message); }, source);
        }

        template<class Fn>
            requires(std::is_invocable_v<Fn&, RecordBuilder&> || std::is_invocable_v<Fn&>)
        NGIN_FORCEINLINE void Fatal(Fn&& fn, const std::source_location source = std::source_location::current()) noexcept
        {
            Log<LogLevel::Fatal>(std::forward<Fn>(fn), source);
        }

    private:
        struct DispatchRecursionGuard
        {
            explicit DispatchRecursionGuard(bool& flag) noexcept
                : m_flag(flag)
            {
                m_flag = true;
            }

            ~DispatchRecursionGuard()
            {
                m_flag = false;
            }

            bool& m_flag;
        };

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

        void PublishSinkSnapshot(SinkSet sinks) noexcept
        {
            try
            {
                m_activeSinks.store(std::make_shared<const SinkSet>(std::move(sinks)), std::memory_order_release);
            }
            catch (...)
            {
                m_sinkErrorCount.fetch_add(1, std::memory_order_relaxed);
            }
        }

        [[nodiscard]] auto GetActiveSinkSnapshot() const noexcept -> std::shared_ptr<const SinkSet>
        {
            return m_activeSinks.load(std::memory_order_acquire);
        }

        [[nodiscard]] static auto CopyContextText(
            const std::string_view input,
            std::array<char, Config::MaxAttrTextBytes * 2>& storage,
            std::size_t& storageUsed,
            NGIN::UInt32& truncatedBytes) noexcept -> std::string_view
        {
            if (input.empty())
            {
                return {};
            }

            const auto available = storage.size() - storageUsed;
            if (available == 0)
            {
                truncatedBytes += static_cast<NGIN::UInt32>(input.size());
                return {};
            }

            const auto copyLength = std::min(input.size(), available);
            std::memcpy(storage.data() + storageUsed, input.data(), copyLength);
            const auto view = std::string_view(storage.data() + storageUsed, copyLength);
            storageUsed += copyLength;

            if (input.size() > copyLength)
            {
                truncatedBytes += static_cast<NGIN::UInt32>(input.size() - copyLength);
            }

            return view;
        }

        [[nodiscard]] static auto CopyContextValue(
            const ContextValue& value,
            std::array<char, Config::MaxAttrTextBytes * 2>& storage,
            std::size_t& storageUsed,
            NGIN::UInt32& truncatedBytes) noexcept -> AttributeValue
        {
            return std::visit(
                [&](const auto& contextValue) -> AttributeValue {
                    using ContextType = std::decay_t<decltype(contextValue)>;
                    if constexpr (std::same_as<ContextType, std::string>)
                    {
                        return CopyContextText(contextValue, storage, storageUsed, truncatedBytes);
                    }
                    else
                    {
                        return contextValue;
                    }
                },
                value);
        }

        void Dispatch(const LogLevel level, const RecordBuilder& builder, const std::source_location source) noexcept
        {
            const auto sinks = GetActiveSinkSnapshot();
            if (!sinks || sinks->empty())
            {
                return;
            }

            if (s_inDispatch)
            {
                m_sinkErrorCount.fetch_add(1, std::memory_order_relaxed);
                return;
            }

            DispatchRecursionGuard recursionGuard(s_inDispatch);

            std::array<LogAttribute, Config::MaxAttributes> mergedAttributes {};
            std::array<char, Config::MaxAttrTextBytes * 2>  contextTextStorage {};
            std::size_t                                     mergedAttributeCount = 0;
            std::size_t                                     contextTextUsed = 0;
            NGIN::UInt32                                    truncatedAttributeCount = builder.GetTruncatedAttributeCount();
            NGIN::UInt32                                    truncatedBytes = builder.GetTruncatedBytes();

            const auto appendOrReplace = [&](const LogAttribute attribute) noexcept {
                for (std::size_t i = 0; i < mergedAttributeCount; ++i)
                {
                    if (mergedAttributes[i].key == attribute.key)
                    {
                        mergedAttributes[i].value = attribute.value;
                        return;
                    }
                }

                if (mergedAttributeCount >= Config::MaxAttributes)
                {
                    ++truncatedAttributeCount;
                    return;
                }

                mergedAttributes[mergedAttributeCount++] = attribute;
            };

            for (const auto& contextAttribute : detail::GetLogContextView())
            {
                appendOrReplace(LogAttribute {
                    .key = CopyContextText(contextAttribute.key, contextTextStorage, contextTextUsed, truncatedBytes),
                    .value = CopyContextValue(contextAttribute.value, contextTextStorage, contextTextUsed, truncatedBytes),
                });
            }

            for (const auto& attribute : builder.GetAttributes())
            {
                appendOrReplace(attribute);
            }

            const LogRecordView record {
                .timestampEpochNanoseconds = NowEpochNanoseconds(),
                .level = level,
                .loggerName = m_name,
                .message = builder.GetMessage(),
                .source = source,
                .threadIdHash = CurrentThreadHash(),
                .attributes = std::span<const LogAttribute>(mergedAttributes.data(), mergedAttributeCount),
                .truncatedAttributeCount = truncatedAttributeCount,
                .truncatedBytes = truncatedBytes,
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
        }

        inline static thread_local bool s_inDispatch = false;

        std::string                       m_name;
        std::atomic<NGIN::UInt8>          m_runtimeMin {static_cast<NGIN::UInt8>(LogLevel::Info)};
        mutable std::atomic<std::shared_ptr<const SinkSet>> m_activeSinks {};
        std::atomic<NGIN::UInt64>                        m_sinkErrorCount {0};
    };
}
