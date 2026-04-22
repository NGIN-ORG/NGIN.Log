#include <NGIN/Log/Log.hpp>

#include <chrono>
#include <format>

int main()
{
    using LoggerType = NGIN::Log::Logger<NGIN::Log::LogLevel::Trace>;

    LoggerType::SinkSet sinks {};
    sinks.push_back(NGIN::Log::MakeSink<NGIN::Log::ConsoleSink>(NGIN::Log::ConsoleSinkOptions {
        .useStderrForErrors = true,
        .includeSource = true,
        .autoFlush = true,
    }));

    auto rotatingJsonSink = NGIN::Memory::MakeScoped<NGIN::Log::RotatingFileSink>(NGIN::Log::RotatingFileSinkOptions {
        .path = "ngin.log",
        .append = true,
        .autoFlush = false,
        .maxFileBytes = 1024 * 1024,
        .maxFiles = 3,
        .formatter = NGIN::Log::MakeRecordFormatter<NGIN::Log::JsonRecordFormatter>(),
    });

    sinks.push_back(NGIN::Log::MakeSink<NGIN::Log::AsyncSink<NGIN::Log::RotatingFileSink>>(
        std::move(rotatingJsonSink),
        NGIN::Log::AsyncSinkOptions {.overflowPolicy = NGIN::Log::AsyncOverflowPolicy::BlockForTimeout,
                                     .blockTimeout = std::chrono::milliseconds(5)}));

    LoggerType logger("Example", NGIN::Log::LogLevel::Debug, std::move(sinks));

    auto requestScope = NGIN::Log::PushLogContext({
        {"request_id", "req-42"},
        {"tenant", "demo"},
    });

    logger.Info("NGIN.Log example started");
    logger.Info(std::format("preformatted value={} tag={}", 42, "sample"));

    logger.Debug([](NGIN::Log::RecordBuilder& rec) {
        rec.Message(std::format("deferred message value={}", 7));
        rec.Attr("status", 200);
        rec.Attr("latency_ns", std::chrono::microseconds(275));
    });

    logger.Flush();
    (void)requestScope;
    return 0;
}
