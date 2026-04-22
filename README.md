# NGIN.Log

NGIN.Log is a standalone C++23 logging library in the NGIN family.

It is built around a bounded record builder, compile-time plus runtime filtering, structured attributes, formatter-driven output, and bounded async delivery. `NGIN.Log` owns record capture and delivery; application code owns message-string construction.

## What It Provides

- macro-free logger APIs
- compile-time minimum log levels plus runtime per-logger filtering
- direct message logging plus builder-style logging
- richer attribute normalization including durations, error codes, enums, pointers, and owned strings
- thread-local scoped logging context
- output formatter/transport split with text, JSON, and logfmt rendering
- sync sinks (`ConsoleSink`, `FileSink`, `RotatingFileSink`)
- async sink wrapping with bounded queues, overflow policies, and live stats
- hierarchical logger registry rules for live runtime configuration

## Quick Example

```cpp
#include <NGIN/Log/Log.hpp>

#include <format>

int main()
{
    using AppLogger = NGIN::Log::Logger<NGIN::Log::LogLevel::Trace>;

    AppLogger::SinkSet sinks {};
    sinks.push_back(NGIN::Log::MakeSink<NGIN::Log::ConsoleSink>());

    auto fileSink = NGIN::Memory::MakeScoped<NGIN::Log::RotatingFileSink>(
        NGIN::Log::RotatingFileSinkOptions {
            .path = "app.log",
            .formatter = NGIN::Log::MakeRecordFormatter<NGIN::Log::JsonRecordFormatter>(),
        });

    sinks.push_back(NGIN::Log::MakeSink<NGIN::Log::AsyncSink<NGIN::Log::RotatingFileSink>>(
        std::move(fileSink),
        NGIN::Log::AsyncSinkOptions {.overflowPolicy = NGIN::Log::AsyncOverflowPolicy::Block}));

    AppLogger logger("App", NGIN::Log::LogLevel::Info, std::move(sinks));

    auto scope = NGIN::Log::PushLogContext({{"request_id", "req-1"}});
    logger.Info("started");
    logger.Info(std::format("value={} ok={}", 42, true));
    logger.Debug([](NGIN::Log::RecordBuilder& rec) {
        rec.Message(std::format("lazy-build value={}", 17));
        rec.Attr("value", 17);
        rec.Attr("ok", true);
    });
    logger.Flush();
    (void)scope;
}
```

## Main API Shapes

### Builder API

Best for hot paths, structured records, and deferred work. The callback only runs after compile-time and runtime filtering pass.

```cpp
logger.Debug([&](NGIN::Log::RecordBuilder& rec) {
    rec.Message("cache miss");
    rec.Attr("key", std::string_view("user:42"));
    rec.Attr("latency", std::chrono::microseconds(17));
});
```

### Direct Message API

Best for the common simple case when message text is already available.

```cpp
logger.Warn("slow request");
```

### Message Construction

Message formatting is application-owned. Build text with `std::format`, `fmt::format`, or any other formatter before calling the direct-message API, or defer that work inside the builder callback.

```cpp
logger.Info(std::format("value={} ok={}", value, ok));
```

Prefer structured attributes over embedding operational fields into the message when those values matter for filtering or ingestion.

## Build Targets

- `NGIN::Log::Static`
- `NGIN::Log::Shared`
- `NGIN::Log` alias resolves to shared when present, else static

## Typical Local Build

```bash
cmake -S . -B build \
  -DNGIN_LOG_BUILD_TESTS=ON \
  -DNGIN_LOG_BUILD_EXAMPLES=ON \
  -DNGIN_LOG_BUILD_BENCHMARKS=ON

cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Read Next

- [Documentation Index](docs/README.md)
- [API Guide](docs/API.md)
- [Architecture Notes](docs/Architecture.md)
- [Sinks Guide](docs/Sinks.md)
