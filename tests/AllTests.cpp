#include <NGIN/Log/Log.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <new>
#include <source_location>
#include <string>
#include <string_view>
#include <thread>

namespace
{
    std::atomic<std::size_t> g_allocationCount {0};
}

void* operator new(std::size_t size)
{
    g_allocationCount.fetch_add(1, std::memory_order_relaxed);
    if (void* ptr = std::malloc(size))
    {
        return ptr;
    }
    throw std::bad_alloc {};
}

void operator delete(void* ptr) noexcept
{
    std::free(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept
{
    std::free(ptr);
}

void* operator new[](std::size_t size)
{
    g_allocationCount.fetch_add(1, std::memory_order_relaxed);
    if (void* ptr = std::malloc(size))
    {
        return ptr;
    }
    throw std::bad_alloc {};
}

void operator delete[](void* ptr) noexcept
{
    std::free(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept
{
    std::free(ptr);
}

namespace
{
    struct CaptureState
    {
        std::atomic<std::size_t> count {0};
        std::mutex               mutex {};
        NGIN::Log::LogRecordView lastRecord {};
        std::string              lastMessage {};
        std::string              lastLogger {};
    };

    class CaptureSink final : public NGIN::Log::ILogSink
    {
    public:
        explicit CaptureSink(CaptureState* state) noexcept
            : m_state(state)
        {
        }

        void Write(const NGIN::Log::LogRecordView& record) noexcept override
        {
            if (!m_state)
            {
                return;
            }

            std::lock_guard lock(m_state->mutex);
            m_state->count.fetch_add(1, std::memory_order_relaxed);
            m_state->lastRecord = record;
            m_state->lastMessage.assign(record.message.data(), record.message.size());
            m_state->lastLogger.assign(record.loggerName.data(), record.loggerName.size());
        }

        void Flush() noexcept override {}

    private:
        CaptureState* m_state {nullptr};
    };

    struct SlowState
    {
        std::atomic<std::size_t> count {0};
    };

    class SlowSink final : public NGIN::Log::ILogSink
    {
    public:
        explicit SlowSink(SlowState* state) noexcept
            : m_state(state)
        {
        }

        void Write(const NGIN::Log::LogRecordView&) noexcept override
        {
            if (m_state)
            {
                m_state->count.fetch_add(1, std::memory_order_relaxed);
            }
        }

        void Flush() noexcept override {}

    private:
        SlowState* m_state {nullptr};
    };

    template<class TLogger>
    concept HasDebugfNoSource = requires(TLogger& logger) {
        logger.Debugf("value={}", 1);
    };

    template<class TLogger>
    concept HasDebugfWithSource = requires(TLogger& logger) {
        logger.Debugf(std::source_location::current(), "value={}", 1);
    };
}

using SignatureLogger = NGIN::Log::Logger<NGIN::Log::LogLevel::Trace>;
static_assert(!HasDebugfNoSource<SignatureLogger>, "No-source Debugf overload must not be callable");
static_assert(HasDebugfWithSource<SignatureLogger>, "Explicit-source Debugf overload must be callable");

TEST_CASE("Compile-time filtering does not execute disabled builder", "[log][filter]")
{
    using LoggerType = NGIN::Log::Logger<NGIN::Log::LogLevel::Error>;

    CaptureState        state {};
    LoggerType::SinkSet sinks {};
    sinks.push_back(NGIN::Log::MakeSink<CaptureSink>(&state));

    LoggerType logger("CompileTime", NGIN::Log::LogLevel::Trace, std::move(sinks));

    std::atomic<int> sideEffects {0};
    logger.Debug([&](NGIN::Log::RecordBuilder&) {
        sideEffects.fetch_add(1, std::memory_order_relaxed);
    });

    REQUIRE(sideEffects.load(std::memory_order_relaxed) == 0);
    REQUIRE(state.count.load(std::memory_order_relaxed) == 0);
}

TEST_CASE("Runtime filtering gates enabled levels", "[log][filter]")
{
    using LoggerType = NGIN::Log::Logger<NGIN::Log::LogLevel::Trace>;

    CaptureState        state {};
    LoggerType::SinkSet sinks {};
    sinks.push_back(NGIN::Log::MakeSink<CaptureSink>(&state));

    LoggerType logger("Runtime", NGIN::Log::LogLevel::Error, std::move(sinks));

    std::atomic<int> sideEffects {0};
    logger.Info([&](NGIN::Log::RecordBuilder&) {
        sideEffects.fetch_add(1, std::memory_order_relaxed);
    });

    logger.Error([&](NGIN::Log::RecordBuilder& rec) {
        sideEffects.fetch_add(1, std::memory_order_relaxed);
        rec.Message("error");
    });

    REQUIRE(sideEffects.load(std::memory_order_relaxed) == 1);
    REQUIRE(state.count.load(std::memory_order_relaxed) == 1);
}

TEST_CASE("Lazy API captures call-site source location", "[log][source]")
{
    using LoggerType = NGIN::Log::Logger<NGIN::Log::LogLevel::Trace>;

    CaptureState        state {};
    LoggerType::SinkSet sinks {};
    sinks.push_back(NGIN::Log::MakeSink<CaptureSink>(&state));

    LoggerType logger("Source", NGIN::Log::LogLevel::Trace, std::move(sinks));
    logger.Info([](NGIN::Log::RecordBuilder& rec) {
        rec.Message("source-check");
    });

    std::lock_guard lock(state.mutex);
    REQUIRE(state.lastRecord.source.line() > 0);
    REQUIRE(std::string_view(state.lastRecord.source.file_name()).find("AllTests.cpp") != std::string_view::npos);
}

TEST_CASE("Formatting API uses explicit source location", "[log][source][format]")
{
    using LoggerType = NGIN::Log::Logger<NGIN::Log::LogLevel::Trace>;

    CaptureState        state {};
    LoggerType::SinkSet sinks {};
    sinks.push_back(NGIN::Log::MakeSink<CaptureSink>(&state));

    LoggerType logger("Format", NGIN::Log::LogLevel::Trace, std::move(sinks));

    const auto source = std::source_location::current();
    logger.Infof(source, "value={}", 17);

    std::lock_guard lock(state.mutex);
    REQUIRE(state.lastMessage.find("value=17") != std::string::npos);
    REQUIRE(state.lastRecord.source.line() == source.line());
    REQUIRE(std::string_view(state.lastRecord.source.file_name()) == std::string_view(source.file_name()));
}

TEST_CASE("Runtime formatting API accepts dynamic formats", "[log][format][runtime]")
{
    using LoggerType = NGIN::Log::Logger<NGIN::Log::LogLevel::Trace>;

    CaptureState        state {};
    LoggerType::SinkSet sinks {};
    sinks.push_back(NGIN::Log::MakeSink<CaptureSink>(&state));

    LoggerType logger("FormatConvenience", NGIN::Log::LogLevel::Trace, std::move(sinks));

    const auto source = std::source_location::current();
    int        value = 9;
    bool       flag = true;
    logger.Debugfv(source, "value={} flag={}", std::make_format_args(value, flag));

    std::lock_guard lock(state.mutex);
    REQUIRE(state.lastMessage.find("value=9") != std::string::npos);
    REQUIRE(state.lastMessage.find("flag=true") != std::string::npos);
    REQUIRE(state.lastRecord.source.line() == source.line());
    REQUIRE(std::string_view(state.lastRecord.source.file_name()) == std::string_view(source.file_name()));
}

TEST_CASE("Runtime formatting API handles invalid format strings", "[log][format][runtime]")
{
    using LoggerType = NGIN::Log::Logger<NGIN::Log::LogLevel::Trace>;

    CaptureState        state {};
    LoggerType::SinkSet sinks {};
    sinks.push_back(NGIN::Log::MakeSink<CaptureSink>(&state));

    LoggerType logger("FormatRuntimeError", NGIN::Log::LogLevel::Trace, std::move(sinks));
    int        value = 17;
    logger.Infofv(std::source_location::current(), "{", std::make_format_args(value));

    std::lock_guard lock(state.mutex);
    REQUIRE(state.lastMessage == "[format-error]");
}

TEST_CASE("RecordBuilder truncates bounded attributes", "[log][builder]")
{
    NGIN::Log::RecordBuilder builder;
    for (int i = 0; i < 16; ++i)
    {
        builder.Attr("k", static_cast<NGIN::Int64>(i));
    }

    REQUIRE(builder.GetAttributes().size() == NGIN::Log::Config::MaxAttributes);
    REQUIRE(builder.GetTruncatedAttributeCount() == 8);
}

TEST_CASE("Sink fan-out dispatches to all sinks", "[log][sink]")
{
    using LoggerType = NGIN::Log::Logger<NGIN::Log::LogLevel::Trace>;

    CaptureState stateA {};
    CaptureState stateB {};

    LoggerType::SinkSet sinks {};
    sinks.push_back(NGIN::Log::MakeSink<CaptureSink>(&stateA));
    sinks.push_back(NGIN::Log::MakeSink<CaptureSink>(&stateB));

    LoggerType logger("Fanout", NGIN::Log::LogLevel::Trace, std::move(sinks));
    logger.Info([](NGIN::Log::RecordBuilder& rec) { rec.Message("fanout"); });

    REQUIRE(stateA.count.load(std::memory_order_relaxed) == 1);
    REQUIRE(stateB.count.load(std::memory_order_relaxed) == 1);
}

TEST_CASE("File sink writes and reopens", "[log][file]")
{
    const auto filePath = std::filesystem::temp_directory_path() / "ngin_log_tests.log";

    {
        NGIN::Log::FileSink sink(filePath.string(), NGIN::Log::FileSinkOptions {.append = false, .autoFlush = true});
        NGIN::Log::LogRecordView record;
        record.timestampEpochNanoseconds = 1;
        record.level = NGIN::Log::LogLevel::Info;
        record.loggerName = "File";
        record.message = "line";
        record.source = std::source_location::current();

        sink.Write(record);
        REQUIRE(sink.Reopen());
        sink.Write(record);
        sink.Flush();
    }

    std::ifstream stream(filePath);
    std::string   content((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());

    std::error_code ec;
    std::filesystem::remove(filePath, ec);

    REQUIRE(content.find("line") != std::string::npos);
}

TEST_CASE("Async sink drops when queue is full", "[log][async]")
{
    SlowState state {};

    auto slowSink = NGIN::Memory::MakeScoped<SlowSink>(&state);
    NGIN::Log::AsyncSink<SlowSink, 8, 4> asyncSink(std::move(slowSink));

    NGIN::Log::LogRecordView record;
    record.timestampEpochNanoseconds = 1;
    record.level = NGIN::Log::LogLevel::Info;
    record.loggerName = "Async";
    record.message = "msg";
    record.source = std::source_location::current();

    constexpr std::size_t totalWrites = 4096;
    for (std::size_t i = 0; i < totalWrites; ++i)
    {
        asyncSink.Write(record);
    }

    asyncSink.Flush();

    REQUIRE(asyncSink.GetDroppedCount() > 0);
    REQUIRE(state.count.load(std::memory_order_relaxed) < totalWrites);
}

TEST_CASE("Async sink producer path is allocation-free after warmup", "[log][async][alloc]")
{
    SlowState state {};

    auto slowSink = NGIN::Memory::MakeScoped<SlowSink>(&state);
    NGIN::Log::AsyncSink<SlowSink, 1024, 64> asyncSink(std::move(slowSink));

    NGIN::Log::LogRecordView record;
    record.timestampEpochNanoseconds = 1;
    record.level = NGIN::Log::LogLevel::Info;
    record.loggerName = "AsyncProducer";
    record.message = "msg";
    record.source = std::source_location::current();

    NGIN::Log::LogAttribute attrs[2] {
        {.key = "id", .value = static_cast<NGIN::Int64>(42)},
        {.key = "tag", .value = std::string_view("hot")},
    };
    record.attributes = attrs;

    for (int i = 0; i < 512; ++i)
    {
        asyncSink.Write(record);
    }
    asyncSink.Flush();

    const auto before = g_allocationCount.load(std::memory_order_relaxed);
    for (int i = 0; i < 4096; ++i)
    {
        asyncSink.Write(record);
    }
    asyncSink.Flush();
    const auto after = g_allocationCount.load(std::memory_order_relaxed);

    REQUIRE(after == before);
}

TEST_CASE("LoggerRegistry manages named logger instances and sink replacement", "[log][registry]")
{
    NGIN::Log::LoggerRegistry registry;

    CaptureState primaryState {};
    CaptureState replacementState {};

    NGIN::Log::LoggerRegistry::SinkSet defaults {};
    defaults.push_back(NGIN::Log::MakeSink<CaptureSink>(&primaryState));
    registry.SetDefaultSinks(defaults);

    auto loggerA = registry.GetOrCreate("A", NGIN::Log::LogLevel::Warn);
    auto loggerB = registry.GetOrCreate("A", NGIN::Log::LogLevel::Info);
    REQUIRE(loggerA.Get() == loggerB.Get());

    loggerA->Error([](NGIN::Log::RecordBuilder& rec) { rec.Message("before"); });
    REQUIRE(primaryState.count.load(std::memory_order_relaxed) == 1);

    NGIN::Log::LoggerRegistry::SinkSet replacement {};
    replacement.push_back(NGIN::Log::MakeSink<CaptureSink>(&replacementState));
    registry.ReplaceLoggerSinks("A", std::move(replacement));

    loggerA->Error([](NGIN::Log::RecordBuilder& rec) { rec.Message("after"); });
    REQUIRE(replacementState.count.load(std::memory_order_relaxed) == 1);

    registry.SetLoggerRuntimeMin("A", NGIN::Log::LogLevel::Fatal);
    REQUIRE(loggerA->GetRuntimeMin() == NGIN::Log::LogLevel::Fatal);
}

TEST_CASE("Concurrent producers and sink reconfiguration remain safe", "[log][concurrency]")
{
    using LoggerType = NGIN::Log::Logger<NGIN::Log::LogLevel::Trace>;

    CaptureState stateA {};
    CaptureState stateB {};

    LoggerType::SinkSet sinksA {};
    sinksA.push_back(NGIN::Log::MakeSink<CaptureSink>(&stateA));
    LoggerType logger("Concurrent", NGIN::Log::LogLevel::Trace, std::move(sinksA));

    std::atomic<bool> running {true};
    std::thread producer([&] {
        while (running.load(std::memory_order_acquire))
        {
            logger.Info([](NGIN::Log::RecordBuilder& rec) { rec.Message("load"); });
        }
    });

    for (int i = 0; i < 128; ++i)
    {
        LoggerType::SinkSet sinks {};
        if ((i & 1) == 0)
        {
            sinks.push_back(NGIN::Log::MakeSink<CaptureSink>(&stateA));
        }
        else
        {
            sinks.push_back(NGIN::Log::MakeSink<CaptureSink>(&stateB));
        }
        logger.SetSinks(std::move(sinks));
    }

    running.store(false, std::memory_order_release);
    producer.join();
    logger.Info([](NGIN::Log::RecordBuilder& rec) { rec.Message("post-reconfig"); });
    logger.Flush();

    const auto total = stateA.count.load(std::memory_order_relaxed) + stateB.count.load(std::memory_order_relaxed);
    REQUIRE(total > 0);
}
