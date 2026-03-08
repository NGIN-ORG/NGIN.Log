#pragma once

#include <NGIN/Log/Config.hpp>
#include <NGIN/Log/Sink.hpp>
#include <NGIN/Log/Types.hpp>
#include <NGIN/Memory/SmartPointers.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <source_location>
#include <span>
#include <string_view>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>

namespace NGIN::Log
{
    /// @brief Asynchronous sink wrapper with bounded MPSC queue and drop-on-full policy.
    /// @details Producer-side `Write` performs fixed-size copies only and does not allocate.
    /// @tparam TSink Wrapped sink type.
    /// @tparam QueueCapacity Queue capacity (must be power of two).
    /// @tparam BatchSize Max records drained per worker iteration.
    template<class TSink, std::size_t QueueCapacity = Config::AsyncQueueCapacity, std::size_t BatchSize = 64>
        requires std::derived_from<TSink, ILogSink>
    class AsyncSink final : public ILogSink
    {
        static_assert(QueueCapacity > 1, "QueueCapacity must be greater than 1");
        static_assert((QueueCapacity & (QueueCapacity - 1)) == 0, "QueueCapacity must be a power-of-two");
        static_assert(BatchSize > 0, "BatchSize must be greater than zero");

    public:
        /// @brief Construct async wrapper with owned wrapped sink.
        explicit AsyncSink(NGIN::Memory::Scoped<TSink> sink) noexcept
            : m_sink(std::move(sink))
        {
            m_worker = std::thread([this] { ConsumeLoop(); });
        }

        /// @brief Shutdown and drain.
        ~AsyncSink() override
        {
            Shutdown();
        }

        AsyncSink(const AsyncSink&) = delete;
        auto operator=(const AsyncSink&) -> AsyncSink& = delete;

        /// @brief Enqueue a record for asynchronous delivery without producer-side heap allocation.
        void Write(const LogRecordView& record) noexcept override
        {
            try
            {
                auto owned = ToOwned(record);
                if (!m_queue.TryEnqueue(std::move(owned)))
                {
                    m_totalDropped.fetch_add(1, std::memory_order_relaxed);
                    m_unreportedDropped.fetch_add(1, std::memory_order_relaxed);
                    return;
                }

                m_totalEnqueued.fetch_add(1, std::memory_order_relaxed);
                m_pending.fetch_add(1, std::memory_order_release);
                m_wakeCondition.notify_one();
            }
            catch (...)
            {
                m_errorCount.fetch_add(1, std::memory_order_relaxed);
            }
        }

        /// @brief Block until queue is drained and wrapped sink is flushed.
        void Flush() noexcept override
        {
            {
                std::unique_lock lock(m_flushMutex);
                m_flushCondition.wait(lock, [this] {
                    return m_pending.load(std::memory_order_acquire) == 0 && m_queue.IsEmpty();
                });
            }

            if (m_sink)
            {
                try
                {
                    m_sink->Flush();
                }
                catch (...)
                {
                    m_errorCount.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }

        /// @brief Total number of dropped records.
        [[nodiscard]] auto GetDroppedCount() const noexcept -> NGIN::UInt64
        {
            return m_totalDropped.load(std::memory_order_relaxed);
        }

        /// @brief Total number of sink errors.
        [[nodiscard]] auto GetErrorCount() const noexcept -> NGIN::UInt64
        {
            return m_errorCount.load(std::memory_order_relaxed);
        }

        /// @brief Total number of successfully enqueued records.
        [[nodiscard]] auto GetEnqueuedCount() const noexcept -> NGIN::UInt64
        {
            return m_totalEnqueued.load(std::memory_order_relaxed);
        }

    private:
        class MpscBoundedQueue
        {
        private:
            struct Slot
            {
                std::atomic<std::size_t> sequence {0};
                OwnedLogRecord           record {};
            };

        public:
            MpscBoundedQueue() noexcept
            {
                for (std::size_t i = 0; i < QueueCapacity; ++i)
                {
                    m_slots[i].sequence.store(i, std::memory_order_relaxed);
                }
            }

            [[nodiscard]] bool TryEnqueue(OwnedLogRecord&& record) noexcept
            {
                Slot*       slot = nullptr;
                std::size_t pos  = m_enqueuePosition.load(std::memory_order_relaxed);

                for (;;)
                {
                    slot = &m_slots[pos & (QueueCapacity - 1)];
                    const auto sequence = slot->sequence.load(std::memory_order_acquire);
                    const auto diff = static_cast<std::intptr_t>(sequence) - static_cast<std::intptr_t>(pos);

                    if (diff == 0)
                    {
                        if (m_enqueuePosition.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
                        {
                            break;
                        }
                    }
                    else if (diff < 0)
                    {
                        return false;
                    }
                    else
                    {
                        pos = m_enqueuePosition.load(std::memory_order_relaxed);
                    }
                }

                slot->record = std::move(record);
                slot->sequence.store(pos + 1, std::memory_order_release);
                return true;
            }

            [[nodiscard]] bool TryDequeue(OwnedLogRecord& outRecord) noexcept
            {
                const auto pos = m_dequeuePosition.load(std::memory_order_relaxed);
                auto&      slot = m_slots[pos & (QueueCapacity - 1)];
                const auto sequence = slot.sequence.load(std::memory_order_acquire);
                const auto diff = static_cast<std::intptr_t>(sequence) - static_cast<std::intptr_t>(pos + 1);

                if (diff != 0)
                {
                    return false;
                }

                m_dequeuePosition.store(pos + 1, std::memory_order_relaxed);
                outRecord = std::move(slot.record);
                slot.sequence.store(pos + QueueCapacity, std::memory_order_release);
                return true;
            }

            [[nodiscard]] bool IsEmpty() const noexcept
            {
                return m_dequeuePosition.load(std::memory_order_acquire) ==
                       m_enqueuePosition.load(std::memory_order_acquire);
            }

        private:
            std::array<Slot, QueueCapacity> m_slots {};
            std::atomic<std::size_t>        m_enqueuePosition {0};
            std::atomic<std::size_t>        m_dequeuePosition {0};
        };

        [[nodiscard]] static auto ToOwned(const LogRecordView& record) noexcept -> OwnedLogRecord
        {
            OwnedLogRecord out;
            out.timestampEpochNanoseconds = record.timestampEpochNanoseconds;
            out.level = record.level;
            out.loggerName.Assign(record.loggerName, out.truncatedBytes);
            out.message.Assign(record.message, out.truncatedBytes);
            out.source = record.source;
            out.threadIdHash = record.threadIdHash;
            out.truncatedAttributeCount = record.truncatedAttributeCount;
            out.truncatedBytes += record.truncatedBytes;

            const auto count = std::min<std::size_t>(record.attributes.size(), Config::MaxAttributes);
            out.attributeCount = count;

            if (record.attributes.size() > count)
            {
                out.truncatedAttributeCount += static_cast<NGIN::UInt32>(record.attributes.size() - count);
            }

            for (std::size_t i = 0; i < count; ++i)
            {
                const auto& inAttr = record.attributes[i];
                auto&       outAttr = out.attributes[i];

                outAttr.key.Assign(inAttr.key, out.truncatedBytes);

                std::visit(
                    [&](const auto& value) {
                        using TValue = std::decay_t<decltype(value)>;
                        if constexpr (std::is_same_v<TValue, std::string_view>)
                        {
                            InlineText<Config::MaxAttrTextBytes> text;
                            text.Assign(value, out.truncatedBytes);
                            outAttr.value = text;
                        }
                        else
                        {
                            outAttr.value = value;
                        }
                    },
                    inAttr.value);
            }

            return out;
        }

        [[nodiscard]] static auto ToViewValue(const OwnedAttributeValue& value) noexcept -> AttributeValue
        {
            return std::visit(
                [](const auto& sourceValue) -> AttributeValue {
                    using TValue = std::decay_t<decltype(sourceValue)>;
                    if constexpr (std::is_same_v<TValue, std::monostate>)
                    {
                        return NGIN::Int64 {0};
                    }
                    else if constexpr (std::is_same_v<TValue, InlineText<Config::MaxAttrTextBytes>>)
                    {
                        return sourceValue.View();
                    }
                    else
                    {
                        return sourceValue;
                    }
                },
                value);
        }

        [[nodiscard]] static auto CurrentThreadHash() noexcept -> NGIN::UInt64
        {
            return static_cast<NGIN::UInt64>(std::hash<std::thread::id> {}(std::this_thread::get_id()));
        }

        [[nodiscard]] static auto NowEpochNanoseconds() noexcept -> NGIN::UInt64
        {
            using namespace std::chrono;
            const auto now = system_clock::now().time_since_epoch();
            return static_cast<NGIN::UInt64>(duration_cast<nanoseconds>(now).count());
        }

        void EmitDropReportIfNeeded() noexcept
        {
            const auto dropped = m_unreportedDropped.exchange(0, std::memory_order_acq_rel);
            if (dropped == 0 || !m_sink)
            {
                return;
            }

            LogRecordView report;
            report.timestampEpochNanoseconds = NowEpochNanoseconds();
            report.level = LogLevel::Warn;
            report.loggerName = "AsyncSink";

            const auto prefix = std::string_view("Dropped ");
            const auto suffix = std::string_view(" log records due to full async queue.");

            std::size_t writeOffset = 0;
            auto        append = [&](const std::string_view part) noexcept {
                const auto available = m_dropReportBuffer.size() - 1 - writeOffset;
                const auto copyCount = std::min(available, part.size());
                if (copyCount > 0)
                {
                    std::memcpy(m_dropReportBuffer.data() + writeOffset, part.data(), copyCount);
                    writeOffset += copyCount;
                }
            };

            append(prefix);

            std::array<char, 32> droppedChars {};
            const auto [ptr, ec] = std::to_chars(droppedChars.data(), droppedChars.data() + droppedChars.size(), dropped);
            if (ec == std::errc {})
            {
                append(std::string_view(droppedChars.data(), static_cast<std::size_t>(ptr - droppedChars.data())));
            }
            else
            {
                append("?");
            }

            append(suffix);
            m_dropReportBuffer[writeOffset] = '\0';

            report.message = std::string_view(m_dropReportBuffer.data(), writeOffset);
            report.source = std::source_location::current();
            report.threadIdHash = CurrentThreadHash();
            report.attributes = {};

            try
            {
                m_sink->Write(report);
            }
            catch (...)
            {
                m_errorCount.fetch_add(1, std::memory_order_relaxed);
            }
        }

        void ConsumeLoop() noexcept
        {
            while (m_running.load(std::memory_order_acquire) || !m_queue.IsEmpty())
            {
                std::size_t drained = 0;
                for (; drained < BatchSize; ++drained)
                {
                    OwnedLogRecord owned;
                    if (!m_queue.TryDequeue(owned))
                    {
                        break;
                    }

                    Deliver(owned);
                    m_pending.fetch_sub(1, std::memory_order_release);
                }

                EmitDropReportIfNeeded();
                NotifyFlushWaiters();

                if (drained == 0)
                {
                    std::unique_lock lock(m_wakeMutex);
                    m_wakeCondition.wait_for(lock, std::chrono::milliseconds(2), [this] {
                        return !m_running.load(std::memory_order_acquire) || !m_queue.IsEmpty();
                    });
                }
            }

            EmitDropReportIfNeeded();
            if (m_sink)
            {
                try
                {
                    m_sink->Flush();
                }
                catch (...)
                {
                    m_errorCount.fetch_add(1, std::memory_order_relaxed);
                }
            }
            NotifyFlushWaiters();
        }

        void Deliver(const OwnedLogRecord& owned) noexcept
        {
            if (!m_sink)
            {
                return;
            }

            std::array<LogAttribute, Config::MaxAttributes> attrs {};
            for (std::size_t i = 0; i < owned.attributeCount; ++i)
            {
                attrs[i].key = owned.attributes[i].key.View();
                attrs[i].value = ToViewValue(owned.attributes[i].value);
            }

            const LogRecordView record {
                .timestampEpochNanoseconds = owned.timestampEpochNanoseconds,
                .level = owned.level,
                .loggerName = owned.loggerName.View(),
                .message = owned.message.View(),
                .source = owned.source,
                .threadIdHash = owned.threadIdHash,
                .attributes = std::span<const LogAttribute>(attrs.data(), owned.attributeCount),
                .truncatedAttributeCount = owned.truncatedAttributeCount,
                .truncatedBytes = owned.truncatedBytes,
            };

            try
            {
                m_sink->Write(record);
            }
            catch (...)
            {
                m_errorCount.fetch_add(1, std::memory_order_relaxed);
            }
        }

        void NotifyFlushWaiters() noexcept
        {
            if (m_pending.load(std::memory_order_acquire) == 0 && m_queue.IsEmpty())
            {
                std::lock_guard lock(m_flushMutex);
                m_flushCondition.notify_all();
            }
        }

        void Shutdown() noexcept
        {
            const auto wasRunning = m_running.exchange(false, std::memory_order_acq_rel);
            if (!wasRunning)
            {
                return;
            }

            m_wakeCondition.notify_all();
            if (m_worker.joinable())
            {
                m_worker.join();
            }

            Flush();
        }

        NGIN::Memory::Scoped<TSink> m_sink;
        MpscBoundedQueue            m_queue {};

        std::atomic<bool>           m_running {true};
        std::thread                 m_worker {};

        std::mutex                  m_wakeMutex {};
        std::condition_variable     m_wakeCondition {};

        std::atomic<NGIN::UInt64>   m_totalDropped {0};
        std::atomic<NGIN::UInt64>   m_unreportedDropped {0};
        std::atomic<NGIN::UInt64>   m_totalEnqueued {0};
        std::atomic<NGIN::UInt64>   m_errorCount {0};

        std::atomic<std::size_t>    m_pending {0};
        std::mutex                  m_flushMutex {};
        std::condition_variable     m_flushCondition {};

        std::array<char, Config::MaxMessageBytes + 1> m_dropReportBuffer {};
    };
}
