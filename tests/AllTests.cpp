#include <NGIN/Log/Log.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <new>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <variant>
#include <vector>

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
    using CapturedValue = std::variant<NGIN::Int64, NGIN::UInt64, double, bool, std::string>;

    struct CapturedAttribute
    {
        std::string                 key {};
        CapturedValue               value {};
        NGIN::Log::LogAttributeKind kind {NGIN::Log::LogAttributeKind::Default};
    };

    struct CaptureState
    {
        std::atomic<std::size_t>      count {0};
        std::mutex                    mutex {};
        NGIN::Log::LogRecordView      lastRecord {};
        std::string                   lastMessage {};
        std::string                   lastLogger {};
        std::vector<CapturedAttribute> lastAttributes {};
        NGIN::UInt32                  lastTruncatedAttributeCount {0};
        NGIN::UInt32                  lastTruncatedBytes {0};
    };

    [[nodiscard]] auto CopyAttributeValue(const NGIN::Log::AttributeValue& value) -> CapturedValue
    {
        return std::visit(
            [](const auto& attributeValue) -> CapturedValue {
                using AttributeType = std::decay_t<decltype(attributeValue)>;
                if constexpr (std::same_as<AttributeType, std::string_view>)
                {
                    return std::string(attributeValue);
                }
                else
                {
                    return attributeValue;
                }
            },
            value);
    }

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
            m_state->lastAttributes.clear();
            m_state->lastAttributes.reserve(record.attributes.size());
            for (const auto& attribute : record.attributes)
            {
                m_state->lastAttributes.push_back(CapturedAttribute {
                    .key = std::string(attribute.key),
                    .value = CopyAttributeValue(attribute.value),
                    .kind = attribute.kind,
                });
            }
            m_state->lastTruncatedAttributeCount = record.truncatedAttributeCount;
            m_state->lastTruncatedBytes = record.truncatedBytes;
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

    struct DelayedState
    {
        std::atomic<std::size_t> count {0};
        std::chrono::milliseconds delay {2};
    };

    class DelayedSink final : public NGIN::Log::ILogSink
    {
    public:
        explicit DelayedSink(DelayedState* state) noexcept
            : m_state(state)
        {
        }

        void Write(const NGIN::Log::LogRecordView&) noexcept override
        {
            if (!m_state)
            {
                return;
            }

            std::this_thread::sleep_for(m_state->delay);
            m_state->count.fetch_add(1, std::memory_order_relaxed);
        }

        void Flush() noexcept override {}

    private:
        DelayedState* m_state {nullptr};
    };

    template<class TLogger>
    concept HasDebugf = requires(TLogger& logger) {
        logger.Debugf(std::source_location::current(), "value={}", 1);
    };

    [[nodiscard]] auto FindCapturedAttribute(const CaptureState& state, std::string_view key) -> const CapturedAttribute*
    {
        for (const auto& attribute : state.lastAttributes)
        {
            if (attribute.key == key)
            {
                return &attribute;
            }
        }

        return nullptr;
    }

    [[nodiscard]] auto MakeRecord(
        std::string_view loggerName,
        std::string_view message,
        const NGIN::UInt64 timestamp = 1710000000123456789ULL) -> NGIN::Log::LogRecordView
    {
        NGIN::Log::LogRecordView record;
        record.timestampEpochNanoseconds = timestamp;
        record.level = NGIN::Log::LogLevel::Info;
        record.loggerName = loggerName;
        record.message = message;
        record.source = std::source_location::current();
        record.threadIdHash = 42;
        return record;
    }
}

using SignatureLogger = NGIN::Log::Logger<NGIN::Log::LogLevel::Trace>;
static_assert(!HasDebugf<SignatureLogger>, "Formatting overloads must not be callable");

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

TEST_CASE("Direct message API captures call-site source location", "[log][source]")
{
    using LoggerType = NGIN::Log::Logger<NGIN::Log::LogLevel::Trace>;

    CaptureState        state {};
    LoggerType::SinkSet sinks {};
    sinks.push_back(NGIN::Log::MakeSink<CaptureSink>(&state));

    LoggerType logger("DirectSource", NGIN::Log::LogLevel::Trace, std::move(sinks));

    const auto source = std::source_location::current();
    logger.Info("value=17", source);

    std::lock_guard lock(state.mutex);
    REQUIRE(state.lastMessage == "value=17");
    REQUIRE(state.lastRecord.source.line() == source.line());
    REQUIRE(std::string_view(state.lastRecord.source.file_name()) == std::string_view(source.file_name()));
}

TEST_CASE("Direct message and builder APIs preserve ease of use", "[log][api]")
{
    using LoggerType = NGIN::Log::Logger<NGIN::Log::LogLevel::Trace>;

    CaptureState        state {};
    LoggerType::SinkSet sinks {};
    sinks.push_back(NGIN::Log::MakeSink<CaptureSink>(&state));

    LoggerType logger("Direct", NGIN::Log::LogLevel::Trace, std::move(sinks));
    logger.Warn("plain message");
    {
        std::lock_guard lock(state.mutex);
        REQUIRE(state.lastMessage == "plain message");
    }

    logger.Info([](NGIN::Log::RecordBuilder& rec) {
        rec.Message("builder message");
        rec.Attr("value", 9);
    });

    std::lock_guard lock(state.mutex);
    REQUIRE(state.lastMessage == "builder message");
    REQUIRE(std::get<NGIN::Int64>(FindCapturedAttribute(state, "value")->value) == 9);
}

TEST_CASE("RecordBuilder supports richer attribute normalization", "[log][builder][attr]")
{
    using LoggerType = NGIN::Log::Logger<NGIN::Log::LogLevel::Trace>;

    CaptureState        state {};
    LoggerType::SinkSet sinks {};
    sinks.push_back(NGIN::Log::MakeSink<CaptureSink>(&state));

    LoggerType logger("Attrs", NGIN::Log::LogLevel::Trace, std::move(sinks));
    int        value = 7;
    const auto error = std::make_error_code(std::errc::invalid_argument);

    logger.Info([&](NGIN::Log::RecordBuilder& rec) {
        rec.Message("attr-check");
        rec.Attr("count", 12);
        rec.Attr("latency", std::chrono::milliseconds(5));
        rec.Attr("pointer", &value);
        rec.Attr("error", error);
        rec.Attr("text", std::string("owned"));
    });

    std::lock_guard lock(state.mutex);
    REQUIRE(std::get<NGIN::Int64>(FindCapturedAttribute(state, "count")->value) == 12);
    REQUIRE(std::get<NGIN::Int64>(FindCapturedAttribute(state, "latency")->value) == 5000000);
    REQUIRE(std::get<std::string>(FindCapturedAttribute(state, "pointer")->value).starts_with("0x"));
    REQUIRE(std::get<std::string>(FindCapturedAttribute(state, "error")->value).find("generic:") != std::string::npos);
    REQUIRE(std::get<std::string>(FindCapturedAttribute(state, "text")->value) == "owned");
}

TEST_CASE("RecordBuilder attribute kinds default and explicit hints propagate", "[log][builder][kind]")
{
    using LoggerType = NGIN::Log::Logger<NGIN::Log::LogLevel::Trace>;

    CaptureState        state {};
    LoggerType::SinkSet sinks {};
    sinks.push_back(NGIN::Log::MakeSink<CaptureSink>(&state));

    LoggerType logger("AttrKinds", NGIN::Log::LogLevel::Trace, std::move(sinks));

    logger.Info([](NGIN::Log::RecordBuilder& rec) {
        rec.Message("kinds");
        rec.Attr("defaulted", 1);
        rec.Attr("tagged", "yes", NGIN::Log::LogAttributeKind::Tag);
        rec.Attr("contextual", "ctx", NGIN::Log::LogAttributeKind::Context);
        rec.Attr("extra", true, NGIN::Log::LogAttributeKind::Extra);
    });

    std::lock_guard lock(state.mutex);
    REQUIRE(FindCapturedAttribute(state, "defaulted")->kind == NGIN::Log::LogAttributeKind::Default);
    REQUIRE(FindCapturedAttribute(state, "tagged")->kind == NGIN::Log::LogAttributeKind::Tag);
    REQUIRE(FindCapturedAttribute(state, "contextual")->kind == NGIN::Log::LogAttributeKind::Context);
    REQUIRE(FindCapturedAttribute(state, "extra")->kind == NGIN::Log::LogAttributeKind::Extra);
}

TEST_CASE("Scoped context merges outer inner and record attributes with later values winning", "[log][context]")
{
    using LoggerType = NGIN::Log::Logger<NGIN::Log::LogLevel::Trace>;

    CaptureState        state {};
    LoggerType::SinkSet sinks {};
    sinks.push_back(NGIN::Log::MakeSink<CaptureSink>(&state));

    LoggerType logger("Context", NGIN::Log::LogLevel::Trace, std::move(sinks));

    auto outer = NGIN::Log::PushLogContext({
        {"request_id", "outer"},
        {"tenant", "alpha"},
    });
    auto inner = NGIN::Log::PushLogContext({
        {"request_id", "inner"},
        {"span", "s1"},
    });

    logger.Info([](NGIN::Log::RecordBuilder& rec) {
        rec.Message("ctx");
        rec.Attr("span", "override");
        rec.Attr("status", 200);
    });

    std::lock_guard lock(state.mutex);
    REQUIRE(std::get<std::string>(FindCapturedAttribute(state, "request_id")->value) == "inner");
    REQUIRE(std::get<std::string>(FindCapturedAttribute(state, "tenant")->value) == "alpha");
    REQUIRE(std::get<std::string>(FindCapturedAttribute(state, "span")->value) == "override");
    REQUIRE(std::get<NGIN::Int64>(FindCapturedAttribute(state, "status")->value) == 200);
    (void)outer;
    (void)inner;
}

TEST_CASE("Merged attributes preserve per-record kind precedence over context", "[log][context][kind]")
{
    using LoggerType = NGIN::Log::Logger<NGIN::Log::LogLevel::Trace>;

    CaptureState        state {};
    LoggerType::SinkSet sinks {};
    sinks.push_back(NGIN::Log::MakeSink<CaptureSink>(&state));

    LoggerType logger("ContextKinds", NGIN::Log::LogLevel::Trace, std::move(sinks));

    auto scope = NGIN::Log::PushLogContext({
        {"subsystem", "context"},
        {"request_id", "req-42"},
    });

    logger.Info([](NGIN::Log::RecordBuilder& rec) {
        rec.Message("ctx-kind");
        rec.Attr("subsystem", "streaming", NGIN::Log::LogAttributeKind::Tag);
        rec.Attr("request_id", "record", NGIN::Log::LogAttributeKind::Extra);
    });

    std::lock_guard lock(state.mutex);
    REQUIRE(std::get<std::string>(FindCapturedAttribute(state, "subsystem")->value) == "streaming");
    REQUIRE(FindCapturedAttribute(state, "subsystem")->kind == NGIN::Log::LogAttributeKind::Tag);
    REQUIRE(std::get<std::string>(FindCapturedAttribute(state, "request_id")->value) == "record");
    REQUIRE(FindCapturedAttribute(state, "request_id")->kind == NGIN::Log::LogAttributeKind::Extra);
    (void)scope;
}

TEST_CASE("Duplicate per-record attributes preserve later kind and value", "[log][builder][kind]")
{
    using LoggerType = NGIN::Log::Logger<NGIN::Log::LogLevel::Trace>;

    CaptureState        state {};
    LoggerType::SinkSet sinks {};
    sinks.push_back(NGIN::Log::MakeSink<CaptureSink>(&state));

    LoggerType logger("DuplicateKinds", NGIN::Log::LogLevel::Trace, std::move(sinks));

    logger.Info([](NGIN::Log::RecordBuilder& rec) {
        rec.Message("dup");
        rec.Attr("phase", "start", NGIN::Log::LogAttributeKind::Tag);
        rec.Attr("phase", "final", NGIN::Log::LogAttributeKind::Extra);
    });

    std::lock_guard lock(state.mutex);
    REQUIRE(std::get<std::string>(FindCapturedAttribute(state, "phase")->value) == "final");
    REQUIRE(FindCapturedAttribute(state, "phase")->kind == NGIN::Log::LogAttributeKind::Extra);
}

TEST_CASE("Context truncation is counted when merged attributes exceed bounds", "[log][context][truncate]")
{
    using LoggerType = NGIN::Log::Logger<NGIN::Log::LogLevel::Trace>;

    CaptureState        state {};
    LoggerType::SinkSet sinks {};
    sinks.push_back(NGIN::Log::MakeSink<CaptureSink>(&state));

    LoggerType logger("ContextOverflow", NGIN::Log::LogLevel::Trace, std::move(sinks));

    std::vector<NGIN::Log::ContextAttribute> attrs;
    attrs.reserve(16);
    for (int i = 0; i < 16; ++i)
    {
        attrs.emplace_back("ctx" + std::to_string(i), i);
    }

    auto scope = NGIN::Log::PushLogContext({
        attrs[0], attrs[1], attrs[2], attrs[3], attrs[4], attrs[5], attrs[6], attrs[7],
        attrs[8], attrs[9], attrs[10], attrs[11], attrs[12], attrs[13], attrs[14], attrs[15],
    });
    logger.Info("ctx-overflow");

    std::lock_guard lock(state.mutex);
    REQUIRE(state.lastAttributes.size() == NGIN::Log::Config::MaxAttributes);
    REQUIRE(state.lastTruncatedAttributeCount >= 8);
    (void)scope;
}

TEST_CASE("Text formatter escapes control characters and stays single line", "[log][formatter][text]")
{
    NGIN::Log::TextRecordFormatter formatter;
    auto                          record = MakeRecord("Text", "line1\nline2\t\"quote\"");
    NGIN::Log::LogAttribute       attrs[] {
        {.key = "path", .value = std::string_view("a\\b")},
    };
    record.attributes = attrs;

    std::string output;
    formatter.Format(record, output);

    REQUIRE(output.find("line1\\nline2\\t\\\"quote\\\"") != std::string::npos);
    REQUIRE(output.find("a\\\\b") != std::string::npos);
    REQUIRE(output.find('\n') == output.size() - 1);
}

TEST_CASE("Json formatter emits stable fields and escapes content", "[log][formatter][json]")
{
    NGIN::Log::JsonRecordFormatter formatter;
    auto                          record = MakeRecord("Json", "hello\n\"world\"");
    NGIN::Log::LogAttribute       attrs[] {
        {.key = "kind", .value = std::string_view("text")},
        {.key = "ok", .value = true},
    };
    record.attributes = attrs;

    std::string output;
    formatter.Format(record, output);

    REQUIRE(output.find("\"timestamp\":") != std::string::npos);
    REQUIRE(output.find("\"level\":\"Info\"") != std::string::npos);
    REQUIRE(output.find("\"message\":\"hello\\n\\\"world\\\"\"") != std::string::npos);
    REQUIRE(output.find("\"attributes\":{\"kind\":\"text\",\"ok\":true}") != std::string::npos);
}

TEST_CASE("Logfmt formatter stays single line and escapes strings", "[log][formatter][logfmt]")
{
    NGIN::Log::LogFmtRecordFormatter formatter;
    auto                            record = MakeRecord("LogFmt", "hello\nworld");
    NGIN::Log::LogAttribute         attrs[] {
        {.key = "tag", .value = std::string_view("quoted \"value\"")},
    };
    record.attributes = attrs;

    std::string output;
    formatter.Format(record, output);

    REQUIRE(output.find("msg=\"hello\\nworld\"") != std::string::npos);
    REQUIRE(output.find("tag=\"quoted \\\"value\\\"\"") != std::string::npos);
    REQUIRE(output.find('\n') == output.size() - 1);
}

TEST_CASE("Built-in formatters ignore attribute kind hints", "[log][formatter][kind]")
{
    auto recordDefault = MakeRecord("Kinds", "msg");
    auto recordTagged = MakeRecord("Kinds", "msg");
    NGIN::Log::LogAttribute defaultAttrs[] {
        {.key = "code", .value = static_cast<NGIN::Int64>(42)},
    };
    NGIN::Log::LogAttribute taggedAttrs[] {
        {.key = "code", .value = static_cast<NGIN::Int64>(42), .kind = NGIN::Log::LogAttributeKind::Tag},
    };
    recordDefault.attributes = defaultAttrs;
    recordTagged.attributes = taggedAttrs;

    NGIN::Log::TextRecordFormatter textFormatter;
    NGIN::Log::JsonRecordFormatter jsonFormatter;
    NGIN::Log::LogFmtRecordFormatter logFmtFormatter;

    std::string textDefault;
    std::string textTagged;
    std::string jsonDefault;
    std::string jsonTagged;
    std::string logFmtDefault;
    std::string logFmtTagged;

    textFormatter.Format(recordDefault, textDefault);
    textFormatter.Format(recordTagged, textTagged);
    jsonFormatter.Format(recordDefault, jsonDefault);
    jsonFormatter.Format(recordTagged, jsonTagged);
    logFmtFormatter.Format(recordDefault, logFmtDefault);
    logFmtFormatter.Format(recordTagged, logFmtTagged);

    REQUIRE(textDefault == textTagged);
    REQUIRE(jsonDefault == jsonTagged);
    REQUIRE(logFmtDefault == logFmtTagged);
}

TEST_CASE("Text formatter caches and avoids steady-state allocations after warmup", "[log][formatter][alloc]")
{
    NGIN::Log::TextRecordFormatter formatter;
    auto                          record = MakeRecord("Alloc", "warmup");
    std::string                   output;
    output.reserve(512);

    for (int i = 0; i < 256; ++i)
    {
        formatter.Format(record, output);
    }

    const auto before = g_allocationCount.load(std::memory_order_relaxed);
    for (int i = 0; i < 1024; ++i)
    {
        formatter.Format(record, output);
    }
    const auto after = g_allocationCount.load(std::memory_order_relaxed);

    REQUIRE(after == before);
}

TEST_CASE("File sink writes and reopens", "[log][file]")
{
    const auto filePath = std::filesystem::temp_directory_path() / "ngin_log_tests.log";

    {
        NGIN::Log::FileSink sink(filePath.string(), NGIN::Log::FileSinkOptions {.append = false, .autoFlush = true});
        auto                record = MakeRecord("File", "line");

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

TEST_CASE("Rotating file sink rotates by size and day boundary", "[log][file][rotate]")
{
    const auto path = std::filesystem::temp_directory_path() / "ngin_rotating_log_tests.log";
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + ".1");
    std::filesystem::remove(path.string() + ".2");

    NGIN::Log::RotatingFileSink sink(NGIN::Log::RotatingFileSinkOptions {
        .path = path.string(),
        .append = false,
        .autoFlush = true,
        .maxFileBytes = 60,
        .maxFiles = 2,
        .rotateDailyLocal = true,
    });

    auto dayOne = MakeRecord("Rotate", "first-line", 1710000000123456789ULL);
    auto dayTwo = MakeRecord("Rotate", "second-line", 1710086400123456789ULL);

    sink.Write(dayOne);
    sink.Write(dayOne);
    sink.Write(dayTwo);
    sink.Flush();

    REQUIRE(std::filesystem::exists(path));
    REQUIRE(std::filesystem::exists(path.string() + ".1"));

    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path.string() + ".1", ec);
    std::filesystem::remove(path.string() + ".2", ec);
}

TEST_CASE("Async sink default drop policy reports drops and stats", "[log][async]")
{
    SlowState state {};

    auto slowSink = NGIN::Memory::MakeScoped<SlowSink>(&state);
    NGIN::Log::AsyncSink<SlowSink, 8, 4> asyncSink(std::move(slowSink));

    auto record = MakeRecord("Async", "msg");

    constexpr std::size_t totalWrites = 4096;
    for (std::size_t i = 0; i < totalWrites; ++i)
    {
        asyncSink.Write(record);
    }

    asyncSink.Flush();

    const auto stats = asyncSink.GetStats();
    REQUIRE(stats.dropped > 0);
    REQUIRE(stats.enqueued > 0);
    REQUIRE(stats.currentApproxDepth == 0);
    REQUIRE(state.count.load(std::memory_order_relaxed) < totalWrites);
}

TEST_CASE("Async sink block policy delivers all records without drops", "[log][async][block]")
{
    DelayedState state {};

    auto delayedSink = NGIN::Memory::MakeScoped<DelayedSink>(&state);
    NGIN::Log::AsyncSink<DelayedSink, 4, 1> asyncSink(
        std::move(delayedSink),
        NGIN::Log::AsyncSinkOptions {.overflowPolicy = NGIN::Log::AsyncOverflowPolicy::Block});

    auto record = MakeRecord("AsyncBlock", "msg");
    for (int i = 0; i < 16; ++i)
    {
        asyncSink.Write(record);
    }

    asyncSink.Flush();
    const auto stats = asyncSink.GetStats();
    REQUIRE(stats.dropped == 0);
    REQUIRE(stats.delivered == 16);
}

TEST_CASE("Async sink timeout policy counts timeout drops", "[log][async][timeout]")
{
    DelayedState state {.delay = std::chrono::milliseconds(10)};

    auto delayedSink = NGIN::Memory::MakeScoped<DelayedSink>(&state);
    NGIN::Log::AsyncSink<DelayedSink, 2, 1> asyncSink(
        std::move(delayedSink),
        NGIN::Log::AsyncSinkOptions {
            .overflowPolicy = NGIN::Log::AsyncOverflowPolicy::BlockForTimeout,
            .blockTimeout = std::chrono::milliseconds(1),
        });

    auto record = MakeRecord("AsyncTimeout", "msg");
    for (int i = 0; i < 64; ++i)
    {
        asyncSink.Write(record);
    }

    asyncSink.Flush();
    const auto stats = asyncSink.GetStats();
    REQUIRE(stats.timeoutDropped > 0);
    REQUIRE(stats.dropped >= stats.timeoutDropped);
}

TEST_CASE("Async sink sync fallback records fallback writes", "[log][async][fallback]")
{
    DelayedState state {.delay = std::chrono::milliseconds(4)};

    auto delayedSink = NGIN::Memory::MakeScoped<DelayedSink>(&state);
    NGIN::Log::AsyncSink<DelayedSink, 2, 1> asyncSink(
        std::move(delayedSink),
        NGIN::Log::AsyncSinkOptions {.overflowPolicy = NGIN::Log::AsyncOverflowPolicy::SyncFallback});

    auto record = MakeRecord("AsyncFallback", "msg");
    for (int i = 0; i < 32; ++i)
    {
        asyncSink.Write(record);
    }

    asyncSink.Flush();
    const auto stats = asyncSink.GetStats();
    REQUIRE(stats.fallbackWrites > 0);
    REQUIRE(stats.dropped == 0);
}

TEST_CASE("Async sink producer path is allocation-free after warmup", "[log][async][alloc]")
{
    SlowState state {};

    auto slowSink = NGIN::Memory::MakeScoped<SlowSink>(&state);
    NGIN::Log::AsyncSink<SlowSink, 1024, 64> asyncSink(std::move(slowSink));

    auto                  record = MakeRecord("AsyncProducer", "msg");
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

TEST_CASE("Async sink preserves attribute kind hints", "[log][async][kind]")
{
    CaptureState state {};

    auto captureSink = NGIN::Memory::MakeScoped<CaptureSink>(&state);
    NGIN::Log::AsyncSink<CaptureSink, 8, 1> asyncSink(std::move(captureSink));

    auto record = MakeRecord("AsyncKind", "msg");
    NGIN::Log::LogAttribute attrs[2] {
        {.key = "tag", .value = std::string_view("alpha"), .kind = NGIN::Log::LogAttributeKind::Tag},
        {.key = "payload", .value = static_cast<NGIN::Int64>(7), .kind = NGIN::Log::LogAttributeKind::Extra},
    };
    record.attributes = attrs;

    asyncSink.Write(record);
    asyncSink.Flush();

    std::lock_guard lock(state.mutex);
    REQUIRE(FindCapturedAttribute(state, "tag")->kind == NGIN::Log::LogAttributeKind::Tag);
    REQUIRE(FindCapturedAttribute(state, "payload")->kind == NGIN::Log::LogAttributeKind::Extra);
}

TEST_CASE("Async sink withstands repeated producer flush stress", "[log][async][stress]")
{
    SlowState state {};
    auto      slowSink = NGIN::Memory::MakeScoped<SlowSink>(&state);
    auto      sharedAsync = NGIN::Log::MakeSink<NGIN::Log::AsyncSink<SlowSink, 256, 16>>(
        std::move(slowSink),
        NGIN::Log::AsyncSinkOptions {.overflowPolicy = NGIN::Log::AsyncOverflowPolicy::Block});

    using LoggerType = NGIN::Log::Logger<NGIN::Log::LogLevel::Trace>;
    LoggerType::SinkSet sinks {};
    sinks.push_back(sharedAsync);
    LoggerType logger("Stress", NGIN::Log::LogLevel::Trace, std::move(sinks));

    std::atomic<bool> running {true};
    std::thread producerA([&] {
        while (running.load(std::memory_order_acquire))
        {
            logger.Info("load-a");
        }
    });
    std::thread producerB([&] {
        while (running.load(std::memory_order_acquire))
        {
            logger.Info("load-b");
        }
    });

    for (int i = 0; i < 32; ++i)
    {
        logger.Flush();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    running.store(false, std::memory_order_release);
    producerA.join();
    producerB.join();
    logger.Flush();

    REQUIRE(state.count.load(std::memory_order_relaxed) > 0);
}

TEST_CASE("LoggerRegistry applies longest prefix rules and updates existing loggers", "[log][registry]")
{
    NGIN::Log::LoggerRegistry registry;

    CaptureState defaultState {};
    CaptureState specificState {};

    NGIN::Log::LoggerRegistry::SinkSet defaults {};
    defaults.push_back(NGIN::Log::MakeSink<CaptureSink>(&defaultState));
    registry.SetDefaultSinks(defaults);

    NGIN::Log::LoggerRule rule {
        .prefix = "net.http",
        .runtimeMin = NGIN::Log::LogLevel::Debug,
        .sinks = NGIN::Log::LoggerRegistry::SinkSet {NGIN::Log::MakeSink<CaptureSink>(&specificState)},
    };
    registry.UpsertRule(std::move(rule));

    auto logger = registry.GetOrCreate("net.http.client");
    REQUIRE(logger->GetRuntimeMin() == NGIN::Log::LogLevel::Debug);
    logger->Info("rule");
    REQUIRE(specificState.count.load(std::memory_order_relaxed) == 1);

    registry.UpsertRule(NGIN::Log::LoggerRule {
        .prefix = "net.http.client",
        .runtimeMin = NGIN::Log::LogLevel::Error,
        .sinks = defaults,
    });
    REQUIRE(logger->GetRuntimeMin() == NGIN::Log::LogLevel::Error);

    logger->Warn("filtered");
    logger->Error("passes");
    REQUIRE(defaultState.count.load(std::memory_order_relaxed) == 1);
}

TEST_CASE("Templated registry preserves compile-time filtering", "[log][registry][compile-time]")
{
    using RegistryType = NGIN::Log::BasicLoggerRegistry<NGIN::Log::LogLevel::Error>;

    CaptureState          state {};
    RegistryType::SinkSet sinks {};
    sinks.push_back(NGIN::Log::MakeSink<CaptureSink>(&state));

    RegistryType registry;
    registry.SetDefaultSinks(sinks);
    auto logger = registry.GetOrCreate("templated");

    std::atomic<int> sideEffects {0};
    logger->Debug([&](NGIN::Log::RecordBuilder&) {
        sideEffects.fetch_add(1, std::memory_order_relaxed);
    });

    REQUIRE(sideEffects.load(std::memory_order_relaxed) == 0);
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
            logger.Info("load");
        }
    });

    for (int i = 0; i < 128; ++i)
    {
        LoggerType::SinkSet sinks {};
        sinks.push_back(NGIN::Log::MakeSink<CaptureSink>((i & 1) == 0 ? &stateA : &stateB));
        logger.SetSinks(std::move(sinks));
    }

    running.store(false, std::memory_order_release);
    producer.join();
    logger.Info("post-reconfig");
    logger.Flush();

    const auto total = stateA.count.load(std::memory_order_relaxed) + stateB.count.load(std::memory_order_relaxed);
    REQUIRE(total > 0);
}
