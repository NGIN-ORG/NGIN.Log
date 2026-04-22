# NGIN.Log

NGIN.Log is a standalone C++23 logging library in the NGIN family.

It is built around a bounded record builder, compile-time plus runtime filtering, structured attributes, formatter-driven output, and bounded async delivery. `NGIN.Log` owns record capture and delivery; application code owns message-string construction.

## Who It Is For

`NGIN.Log` is a good fit for:

- applications that want structured logging without committing to a formatting framework
- libraries and runtime code that care about bounded behavior under load
- systems that need compile-time and runtime filtering together
- teams that want clear separation between record capture, output formatting, and transport
- production services that need async delivery, file rotation, and live sink reconfiguration

It is especially useful when you prefer:

- direct control over where formatting cost happens
- structured attributes instead of message-only logs
- explicit operational policies for async overflow and file output
- logging APIs that stay usable without macros

## Who It Is Not For

You probably do not need `NGIN.Log` if:

- a stream-style logger with minimal structure is enough
- you want the logging library itself to own `fmt` or `std::format` style message interpolation
- your application needs automatic async context propagation across executors or fibers
- you want one opinionated application framework to decide logging, metrics, and tracing together

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

## Recommended Defaults

If you are adopting the library for a normal application, start here:

- use direct-message logging for already-available text
- use builder/lambda logging when message construction is expensive or when attributes matter
- prefer structured attributes for IDs, sizes, durations, status codes, and similar operational values
- use `ConsoleSink` during development
- use `RotatingFileSink` with `JsonRecordFormatter` for production ingestion
- wrap file sinks in `AsyncSink` when output latency should not block the caller
- keep thread-local scoped context for request-style metadata, but document that it does not automatically cross thread or task handoff boundaries

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

What to notice:

- message-string construction stays in application code
- the builder path is the deferred-work path
- source metadata is captured automatically by the supported APIs
- sink transport and record formatting are configured independently

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

## Start Here

If you are new to the library, use this path:

1. [Documentation Index](docs/README.md)
2. [Quick Start](docs/QuickStart.md)
3. [Production Guide](docs/Production.md)
4. [API Guide](docs/API.md)
5. [Architecture Notes](docs/Architecture.md)
6. [Performance Guide](docs/Performance.md)

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
- [Quick Start](docs/QuickStart.md)
- [Production Guide](docs/Production.md)
- [API Guide](docs/API.md)
- [Architecture Notes](docs/Architecture.md)
- [Performance Guide](docs/Performance.md)
- [Sinks Guide](docs/Sinks.md)
