# NGIN.Log

NGIN.Log is a standalone C++23 logging library in the NGIN family.

It focuses on explicit, structured, performance-conscious logging without relying on macro-heavy APIs.

The short version is:

- macro-free logging API
- explicit source metadata
- compile-time and runtime filtering
- structured attributes
- sync and async sink composition

It is designed to be useful as a normal C++ logging library, not only inside the full NGIN platform.

## What NGIN.Log Is For

NGIN.Log is for applications that want:

- clear logging call sites
- predictable filtering behavior
- structured records
- multiple sinks
- async logging without turning the producer path into a black box

It aims to keep the logging model explicit: what gets logged, where it goes, and when it is filtered should be understandable from the code.

## What It Provides

NGIN.Log provides:

- macro-free logger APIs
- compile-time minimum log levels
- runtime per-logger level control
- structured key-value attributes
- sink fan-out
- async sink wrapping with bounded queues
- built-in sinks such as console and file sinks

## Quick Example

```cpp
#include <NGIN/Log/Log.hpp>

int main()
{
    using AppLogger = NGIN::Log::Logger<NGIN::Log::LogLevel::Trace>;

    AppLogger::SinkSet sinks {};
    sinks.push_back(NGIN::Log::MakeSink<NGIN::Log::ConsoleSink>());

    AppLogger logger("App", NGIN::Log::LogLevel::Info, std::move(sinks));

    logger.Info([](NGIN::Log::RecordBuilder& rec) {
        rec.Message("started");
        rec.Attr("pid", static_cast<NGIN::UInt64>(1234));
    });
}
```

## The Main API Styles

### Builder API

Best when you want tight control over work done on the logging path.

```cpp
logger.Debug([&](NGIN::Log::RecordBuilder& rec) {
    rec.Message("cache miss");
    rec.Attr("key", std::string_view("user:42"));
});
```

### Format API

Useful when convenience matters more than minimizing every instruction on the hot path.

```cpp
logger.Infof(std::source_location::current(), "value={} ok={}", value, ok);
```

## Build Targets

- `NGIN::Log::Static`
- `NGIN::Log::Shared`
- `NGIN::Log` alias resolves to shared when present, else static

## Build Options

Main CMake options:

- `NGIN_LOG_BUILD_STATIC` default `ON`
- `NGIN_LOG_BUILD_SHARED` default `OFF`
- `NGIN_LOG_BUILD_TESTS` default `ON`
- `NGIN_LOG_BUILD_EXAMPLES` default `ON`
- `NGIN_LOG_BUILD_BENCHMARKS` default `ON`
- `NGIN_LOG_ENABLE_ASAN` default `OFF`
- `NGIN_LOG_ENABLE_TSAN` default `OFF`
- `NGIN_LOG_ENABLE_LTO` default `OFF`
- `NGIN_LOG_STRICT_WARNINGS` default `ON`

## Typical Local Build

```bash
cmake -S . -B build \
  -DNGIN_LOG_BUILD_TESTS=ON \
  -DNGIN_LOG_BUILD_EXAMPLES=ON \
  -DNGIN_LOG_BUILD_BENCHMARKS=ON

cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Where It Fits

Within the NGIN ecosystem, NGIN.Log is the logging layer other libraries can build on.

It is still a normal standalone library, but in the platform stack it commonly sits above `NGIN.Base` and below higher-level runtime or tooling layers.

## Read Next

- [Documentation Index](docs/README.md)
- [API Reference Guide](docs/API.md)
- [Architecture Notes](docs/Architecture.md)
- [Sinks Guide](docs/Sinks.md)
