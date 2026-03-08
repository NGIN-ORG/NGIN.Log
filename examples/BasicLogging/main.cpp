#include <NGIN/Log/Log.hpp>

#include <source_location>

int main()
{
    using LoggerType = NGIN::Log::Logger<NGIN::Log::LogLevel::Trace>;

    LoggerType::SinkSet sinks {};
    sinks.push_back(NGIN::Log::MakeSink<NGIN::Log::ConsoleSink>());

    auto fileSink = NGIN::Memory::MakeScoped<NGIN::Log::FileSink>(
        "ngin.log",
        NGIN::Log::FileSinkOptions {.append = true, .autoFlush = true});

    sinks.push_back(NGIN::Log::MakeSink<NGIN::Log::AsyncSink<NGIN::Log::FileSink>>(std::move(fileSink)));

    LoggerType logger("Example", NGIN::Log::LogLevel::Debug, std::move(sinks));

    logger.Info([](NGIN::Log::RecordBuilder& rec) {
        rec.Message("NGIN.Log example started");
        rec.Attr("mode", std::string_view("demo"));
    });

    const int value = 42;
    logger.Debugf(std::source_location::current(), "formatted value={} tag={}", value, "sample");

    logger.Flush();
    return 0;
}
