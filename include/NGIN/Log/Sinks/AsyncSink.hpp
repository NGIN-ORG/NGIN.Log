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
#include <functional>
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
    enum class AsyncOverflowPolicy : NGIN::UInt8
    {
        DropNewest,
        Block,
        BlockForTimeout,
        SyncFallback,
    };

    struct AsyncSinkOptions
    {
        AsyncOverflowPolicy                    overflowPolicy {AsyncOverflowPolicy::DropNewest};
        std::chrono::milliseconds              blockTimeout {0};
        bool                                   emitDropReports {true};
        std::function<void(std::string_view)>  notifyOnError {};
    };

    struct AsyncSinkStats
    {
        NGIN::UInt64 enqueued {0};
        NGIN::UInt64 delivered {0};
        NGIN::UInt64 dropped {0};
        NGIN::UInt64 timeoutDropped {0};
        NGIN::UInt64 fallbackWrites {0};
        NGIN::UInt64 errors {0};
        NGIN::UInt64 queueHighWatermark {0};
        NGIN::UInt64 currentApproxDepth {0};
    };

    template<class TSink, std::size_t QueueCapacity = Config::AsyncQueueCapacity, std::size_t BatchSize = 64>
        requires std::derived_from<TSink, ILogSink>
    class AsyncSink final : public ILogSink
    {
        static_assert(QueueCapacity > 1, "QueueCapacity must be greater than 1");
        static_assert((QueueCapacity & (QueueCapacity - 1)) == 0, "QueueCapacity must be a power-of-two");
        static_assert(BatchSize > 0, "BatchSize must be greater than zero");

    public:
        explicit AsyncSink(NGIN::Memory::Scoped<TSink> sink, AsyncSinkOptions options = {}) noexcept
            : m_sink(std::move(sink))
            , m_options(std::move(options))
        {
            m_worker = std::thread([this] { ConsumeLoop(); });
        }

        ~AsyncSink() override
        {
            Shutdown();
        }

        AsyncSink(const AsyncSink&) = delete;
        auto operator=(const AsyncSink&) -> AsyncSink& = delete;

        void Write(const LogRecordView& record) noexcept override
        {
            if (!m_running.load(std::memory_order_acquire))
            {
                RecordDrop();
                return;
            }

            try
            {
                auto owned = ToOwned(record);
                switch (m_options.overflowPolicy)
                {
                    case AsyncOverflowPolicy::DropNewest: EnqueueDropNewest(std::move(owned)); break;
                    case AsyncOverflowPolicy::Block: EnqueueBlocking(std::move(owned)); break;
                    case AsyncOverflowPolicy::BlockForTimeout: EnqueueBlockingWithTimeout(std::move(owned)); break;
                    case AsyncOverflowPolicy::SyncFallback: EnqueueWithSyncFallback(std::move(owned)); break;
                }
            }
            catch (...)
            {
                RecordError("async-write-failed");
            }
        }

        void Flush() noexcept override
        {
            {
                std::unique_lock lock(m_flushMutex);
                m_flushCondition.wait(lock, [this] {
                    return m_outstanding.load(std::memory_order_acquire) == 0 && m_queue.IsEmpty();
                });
            }

            if (m_options.emitDropReports)
            {
                std::lock_guard deliveryLock(m_deliveryMutex);
                EmitDropReportIfNeededLocked();
            }

            FlushWrappedSink();
        }

        [[nodiscard]] auto GetStats() const noexcept -> AsyncSinkStats
        {
            return AsyncSinkStats {
                .enqueued = m_totalEnqueued.load(std::memory_order_relaxed),
                .delivered = m_totalDelivered.load(std::memory_order_relaxed),
                .dropped = m_totalDropped.load(std::memory_order_relaxed),
                .timeoutDropped = m_timeoutDropped.load(std::memory_order_relaxed),
                .fallbackWrites = m_fallbackWrites.load(std::memory_order_relaxed),
                .errors = m_errorCount.load(std::memory_order_relaxed),
                .queueHighWatermark = m_queueHighWatermark.load(std::memory_order_relaxed),
                .currentApproxDepth = static_cast<NGIN::UInt64>(m_queue.ApproxDepth()),
            };
        }

        [[nodiscard]] auto GetDroppedCount() const noexcept -> NGIN::UInt64
        {
            return GetStats().dropped;
        }

        [[nodiscard]] auto GetErrorCount() const noexcept -> NGIN::UInt64
        {
            return GetStats().errors;
        }

        [[nodiscard]] auto GetEnqueuedCount() const noexcept -> NGIN::UInt64
        {
            return GetStats().enqueued;
        }

    private:
        class MpscBoundedQueue
        {
        public:
            [[nodiscard]] bool TryEnqueue(OwnedLogRecord&& record) noexcept
            {
                std::lock_guard lock(m_mutex);
                if (m_size >= QueueCapacity)
                {
                    return false;
                }

                m_slots[m_enqueueIndex] = std::move(record);
                m_enqueueIndex = (m_enqueueIndex + 1) & (QueueCapacity - 1);
                ++m_size;
                return true;
            }

            [[nodiscard]] bool TryDequeue(OwnedLogRecord& outRecord) noexcept
            {
                std::lock_guard lock(m_mutex);
                if (m_size == 0)
                {
                    return false;
                }

                outRecord = std::move(m_slots[m_dequeueIndex]);
                m_dequeueIndex = (m_dequeueIndex + 1) & (QueueCapacity - 1);
                --m_size;
                return true;
            }

            [[nodiscard]] auto ApproxDepth() const noexcept -> std::size_t
            {
                std::lock_guard lock(m_mutex);
                return m_size;
            }

            [[nodiscard]] bool CanEnqueue() const noexcept
            {
                return ApproxDepth() < QueueCapacity;
            }

            [[nodiscard]] bool IsEmpty() const noexcept
            {
                return ApproxDepth() == 0;
            }

        private:
            mutable std::mutex                  m_mutex {};
            std::array<OwnedLogRecord, QueueCapacity> m_slots {};
            std::size_t                         m_enqueueIndex {0};
            std::size_t                         m_dequeueIndex {0};
            std::size_t                         m_size {0};
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

        void RecordError(std::string_view reason) noexcept
        {
            m_errorCount.fetch_add(1, std::memory_order_relaxed);
            if (m_options.notifyOnError)
            {
                try
                {
                    m_options.notifyOnError(reason);
                }
                catch (...)
                {
                }
            }
        }

        void RecordDrop() noexcept
        {
            m_totalDropped.fetch_add(1, std::memory_order_relaxed);
            m_unreportedDropped.fetch_add(1, std::memory_order_relaxed);
        }

        void RecordTimeoutDrop() noexcept
        {
            RecordDrop();
            m_timeoutDropped.fetch_add(1, std::memory_order_relaxed);
        }

        [[nodiscard]] bool TryEnqueueOwned(OwnedLogRecord&& owned) noexcept
        {
            m_outstanding.fetch_add(1, std::memory_order_acq_rel);
            if (!m_queue.TryEnqueue(std::move(owned)))
            {
                m_outstanding.fetch_sub(1, std::memory_order_acq_rel);
                NotifyFlushWaiters();
                return false;
            }

            m_totalEnqueued.fetch_add(1, std::memory_order_relaxed);
            UpdateHighWatermark(static_cast<NGIN::UInt64>(m_queue.ApproxDepth()));
            m_wakeCondition.notify_one();
            return true;
        }

        void EnqueueDropNewest(OwnedLogRecord&& owned) noexcept
        {
            if (!TryEnqueueOwned(std::move(owned)))
            {
                RecordDrop();
            }
        }

        void EnqueueBlocking(OwnedLogRecord&& owned) noexcept
        {
            while (m_running.load(std::memory_order_acquire))
            {
                if (TryEnqueueOwned(std::move(owned)))
                {
                    return;
                }

                std::unique_lock lock(m_spaceMutex);
                m_spaceCondition.wait(lock, [this] {
                    return !m_running.load(std::memory_order_acquire) || m_queue.CanEnqueue();
                });
            }

            RecordDrop();
        }

        void EnqueueBlockingWithTimeout(OwnedLogRecord&& owned) noexcept
        {
            auto deadline = std::chrono::steady_clock::now() + m_options.blockTimeout;

            while (m_running.load(std::memory_order_acquire))
            {
                if (TryEnqueueOwned(std::move(owned)))
                {
                    return;
                }

                std::unique_lock lock(m_spaceMutex);
                if (!m_spaceCondition.wait_until(lock, deadline, [this] {
                        return !m_running.load(std::memory_order_acquire) || m_queue.CanEnqueue();
                    }))
                {
                    RecordTimeoutDrop();
                    return;
                }
            }

            RecordTimeoutDrop();
        }

        void EnqueueWithSyncFallback(OwnedLogRecord&& owned) noexcept
        {
            if (TryEnqueueOwned(std::move(owned)))
            {
                return;
            }

            m_outstanding.fetch_add(1, std::memory_order_acq_rel);
            m_fallbackWrites.fetch_add(1, std::memory_order_relaxed);
            Deliver(owned);
            m_outstanding.fetch_sub(1, std::memory_order_acq_rel);
            NotifyFlushWaiters();
        }

        void UpdateHighWatermark(const NGIN::UInt64 depth) noexcept
        {
            auto current = m_queueHighWatermark.load(std::memory_order_relaxed);
            while (depth > current &&
                   !m_queueHighWatermark.compare_exchange_weak(current, depth, std::memory_order_relaxed))
            {
            }
        }

        void EmitDropReportIfNeededLocked() noexcept
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
            const auto suffix = std::string_view(" log records due to async overflow.");

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
            append(ec == std::errc {} ? std::string_view(droppedChars.data(), static_cast<std::size_t>(ptr - droppedChars.data()))
                                      : std::string_view("?"));
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
                RecordError("async-drop-report-write-failed");
            }
        }

        void FlushWrappedSink() noexcept
        {
            if (!m_sink)
            {
                return;
            }

            std::lock_guard lock(m_deliveryMutex);
            try
            {
                m_sink->Flush();
            }
            catch (...)
            {
                RecordError("async-flush-failed");
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
                    m_outstanding.fetch_sub(1, std::memory_order_acq_rel);
                    m_spaceCondition.notify_all();
                }

                if (m_options.emitDropReports)
                {
                    std::lock_guard deliveryLock(m_deliveryMutex);
                    EmitDropReportIfNeededLocked();
                }

                NotifyFlushWaiters();

                if (drained == 0)
                {
                    std::unique_lock lock(m_wakeMutex);
                    m_wakeCondition.wait_for(lock, std::chrono::milliseconds(2), [this] {
                        return !m_running.load(std::memory_order_acquire) || !m_queue.IsEmpty();
                    });
                }
            }

            if (m_options.emitDropReports)
            {
                std::lock_guard deliveryLock(m_deliveryMutex);
                EmitDropReportIfNeededLocked();
            }

            FlushWrappedSink();
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

            std::lock_guard lock(m_deliveryMutex);
            try
            {
                m_sink->Write(record);
                m_totalDelivered.fetch_add(1, std::memory_order_relaxed);
            }
            catch (...)
            {
                RecordError("async-deliver-failed");
            }
        }

        void NotifyFlushWaiters() noexcept
        {
            if (m_outstanding.load(std::memory_order_acquire) == 0 && m_queue.IsEmpty())
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
            m_spaceCondition.notify_all();
            if (m_worker.joinable())
            {
                m_worker.join();
            }
        }

        NGIN::Memory::Scoped<TSink> m_sink;
        AsyncSinkOptions            m_options {};
        MpscBoundedQueue            m_queue {};

        std::atomic<bool>           m_running {true};
        std::thread                 m_worker {};

        std::mutex                  m_wakeMutex {};
        std::condition_variable     m_wakeCondition {};

        std::mutex                  m_spaceMutex {};
        std::condition_variable     m_spaceCondition {};

        std::mutex                  m_deliveryMutex {};

        std::atomic<NGIN::UInt64>   m_totalEnqueued {0};
        std::atomic<NGIN::UInt64>   m_totalDelivered {0};
        std::atomic<NGIN::UInt64>   m_totalDropped {0};
        std::atomic<NGIN::UInt64>   m_timeoutDropped {0};
        std::atomic<NGIN::UInt64>   m_fallbackWrites {0};
        std::atomic<NGIN::UInt64>   m_errorCount {0};
        std::atomic<NGIN::UInt64>   m_unreportedDropped {0};
        std::atomic<NGIN::UInt64>   m_queueHighWatermark {0};

        std::atomic<std::size_t>    m_outstanding {0};
        std::mutex                  m_flushMutex {};
        std::condition_variable     m_flushCondition {};

        std::array<char, Config::MaxMessageBytes + 1> m_dropReportBuffer {};
    };
}
