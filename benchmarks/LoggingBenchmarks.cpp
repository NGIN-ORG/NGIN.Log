#include <NGIN/Log/Log.hpp>

#include <chrono>
#include <iostream>
#include <source_location>
#include <thread>

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
    auto fileSink = NGIN::Memory::MakeScoped<NGIN::Log::FileSink>(
        "ngin.log.bench",
        NGIN::Log::FileSinkOptions {.append = false, .autoFlush = false});
    asyncSinks.push_back(NGIN::Log::MakeSink<NGIN::Log::AsyncSink<NGIN::Log::FileSink>>(std::move(fileSink)));
    FullLogger asyncLogger("Bench.Async", NGIN::Log::LogLevel::Trace, std::move(asyncSinks));

    constexpr int iterations = 200000;

    Benchmark("disabled-debug-compile-filter", iterations, [&](int i) {
        compileFiltered.Debug([&](NGIN::Log::RecordBuilder& rec) {
            rec.Message("this should never run");
            rec.Attr("i", static_cast<NGIN::Int64>(i));
        });
    });

    Benchmark("enabled-info-null-sink", iterations, [&](int i) {
        enabled.Info([&](NGIN::Log::RecordBuilder& rec) {
            rec.Message("hot-path");
            rec.Attr("i", static_cast<NGIN::Int64>(i));
        });
    });

    Benchmark("enabled-formatted-null-sink", iterations, [&](int i) {
        enabled.Infof(std::source_location::current(), "value={} status={}", i, "ok");
    });

    Benchmark("enabled-async-file", 100000, [&](int i) {
        asyncLogger.Infof(std::source_location::current(), "async value={}", i);
    });

    asyncLogger.Flush();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    return 0;
}
