#include <NGIN/Log/Log.hpp>

#include <chrono>
#include <iostream>
#include <source_location>

namespace
{
    template<class TFn>
    auto Benchmark(const char* name, const int iterations, TFn&& fn) -> void
    {
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < iterations; ++i)
        {
            fn(i);
        }
        const auto end = std::chrono::steady_clock::now();
        const auto totalNs = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        const auto avgNs = static_cast<double>(totalNs) / static_cast<double>(iterations);

        std::cout << name << ": total=" << totalNs << " ns avg=" << avgNs << " ns\n";
    }
}

int main()
{
    using FastLogger = NGIN::Log::Logger<NGIN::Log::LogLevel::Error>;
    using FullLogger = NGIN::Log::Logger<NGIN::Log::LogLevel::Trace>;

    FastLogger::SinkSet fastSinks {};
    fastSinks.push_back(NGIN::Log::MakeSink<NGIN::Log::NullSink>());
    FastLogger compileFiltered("Bench.CompileFiltered", NGIN::Log::LogLevel::Trace, std::move(fastSinks));

    FullLogger::SinkSet fullSinks {};
    fullSinks.push_back(NGIN::Log::MakeSink<NGIN::Log::NullSink>());
    FullLogger enabled("Bench.Enabled", NGIN::Log::LogLevel::Trace, std::move(fullSinks));

    FullLogger::SinkSet asyncSinks {};
    auto rotatingSink = NGIN::Memory::MakeScoped<NGIN::Log::RotatingFileSink>(NGIN::Log::RotatingFileSinkOptions {
        .path = "ngin.log.bench",
        .append = false,
        .autoFlush = false,
        .maxFileBytes = 8 * 1024 * 1024,
        .maxFiles = 2,
        .formatter = NGIN::Log::MakeRecordFormatter<NGIN::Log::JsonRecordFormatter>(),
    });
    asyncSinks.push_back(NGIN::Log::MakeSink<NGIN::Log::AsyncSink<NGIN::Log::RotatingFileSink>>(
        std::move(rotatingSink),
        NGIN::Log::AsyncSinkOptions {.overflowPolicy = NGIN::Log::AsyncOverflowPolicy::Block}));
    FullLogger asyncLogger("Bench.Async", NGIN::Log::LogLevel::Trace, std::move(asyncSinks));

    constexpr int iterations = 200000;

    Benchmark("disabled-debug-compile-filter", iterations, [&](int i) {
        compileFiltered.Debug([&](NGIN::Log::RecordBuilder& rec) {
            rec.Message("this should never run");
            rec.Attr("i", static_cast<NGIN::Int64>(i));
        });
    });

    Benchmark("enabled-info-direct-message-null-sink", iterations, [&](int i) {
        enabled.Info([&](NGIN::Log::RecordBuilder& rec) {
            rec.Message("hot-path");
            rec.Attr("i", static_cast<NGIN::Int64>(i));
        });
    });

    Benchmark("enabled-formatted-null-sink", iterations, [&](int i) {
        enabled.Infof(std::source_location::current(), "value={} status={}", i, "ok");
    });

    Benchmark("enabled-contextual-null-sink", iterations, [&](int i) {
        auto scope = NGIN::Log::PushLogContext({{"bench", "context"}, {"iteration", i}});
        enabled.Warn("contextual");
        (void)scope;
    });

    Benchmark("enabled-async-rotating-json", 100000, [&](int i) {
        asyncLogger.Infof(std::source_location::current(), "async value={}", i);
    });

    asyncLogger.Flush();
    return 0;
}
