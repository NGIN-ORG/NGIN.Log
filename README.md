# NGIN.Log

NGIN.Log is a standalone, performance-focused C++23 logging component in the NGIN platform family.

It is designed for:
- macro-free call sites,
- explicit and predictable source metadata,
- compile-time and runtime filtering,
- bounded record construction,
- multi-sink fan-out with async sink wrapping.

## Key Properties

- Macro-free API (`std::source_location`, templates, `if constexpr`)
- Compile-time level stripping (`Logger<CompileTimeMin>`)
- Runtime per-logger level control
- Structured key-value attributes
- Lock-free sink read path during dispatch
- Drop-on-full async sink policy (producer latency protection)
- Pluggable formatting policy (`StdFormatter` by default)

## Build Targets

- `NGIN::Log::Static` (`-DNGIN_LOG_BUILD_STATIC=ON`)
- `NGIN::Log::Shared` (`-DNGIN_LOG_BUILD_SHARED=ON`)
- `NGIN::Log` alias resolves to shared when present, else static

## Build Options

Main CMake options:
- `NGIN_LOG_BUILD_STATIC` (default `ON`)
- `NGIN_LOG_BUILD_SHARED` (default `OFF`)
- `NGIN_LOG_BUILD_TESTS` (default `ON`)
- `NGIN_LOG_BUILD_EXAMPLES` (default `ON`)
- `NGIN_LOG_BUILD_BENCHMARKS` (default `ON`)
- `NGIN_LOG_ENABLE_ASAN` (default `OFF`)
- `NGIN_LOG_ENABLE_TSAN` (default `OFF`)
- `NGIN_LOG_ENABLE_LTO` (default `OFF`)
- `NGIN_LOG_STRICT_WARNINGS` (default `ON`)

Typical local configure:

```bash
cmake -S NGIN.Log -B NGIN.Log/build \
  -DNGIN_LOG_BUILD_TESTS=ON \
  -DNGIN_LOG_BUILD_EXAMPLES=ON \
  -DNGIN_LOG_BUILD_BENCHMARKS=ON
cmake --build NGIN.Log/build -j
ctest --test-dir NGIN.Log/build --output-on-failure
```

## Quick Start

```cpp
#include <NGIN/Log/Log.hpp>

#include <source_location>

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

    logger.Debugf(std::source_location::current(), "value={} count={}", 7, 2);
}
```

## API Modes

### 1) Lazy Builder API (hot path)

Preferred for performance-sensitive code:

```cpp
logger.Debug([&](NGIN::Log::RecordBuilder& rec) {
    rec.Message("cache miss");
    rec.Attr("key", std::string_view("user:42"));
    rec.Attr("retry", false);
});
```

Properties:
- disabled compile-time levels are stripped,
- runtime-disabled levels return early,
- no format-argument evaluation when disabled (caller controls work in lambda),
- bounded message/attribute storage.

### 2) Compile-time format-checked API (`*f`)

```cpp
logger.Infof(std::source_location::current(), "value={} ok={}", value, ok);
```

Properties:
- compile-time format checking via `std::format_string<Args...>`,
- explicit source required for accurate caller metadata,
- arguments are evaluated before runtime filter short-circuit.

### 3) Runtime format API (`*fv`)

```cpp
int value = 17;
bool ok = true;
logger.Debugfv(
    std::source_location::current(),
    "value={} ok={}",
    std::make_format_args(value, ok));
```

Properties:
- supports dynamic/runtime format strings,
- explicit source required,
- may allocate internally (`std::vformat` path),
- intended for non-hot-path/config-driven formatting.

## Source Location Semantics

- Lazy APIs (`Trace/Debug/...`) default source to `std::source_location::current()` at the call site.
- `*f` and `*fv` APIs require explicit source argument.
- No convenience `*f(fmt, ...)` overload is provided, because macro-free wrappers cannot reliably preserve true caller source.

## Filtering Semantics

Rule of execution:
- If call-site level `< CompileTimeMin`, the call is compiled out.
- If call-site level `>= CompileTimeMin`, it is still gated by runtime level (`SetRuntimeMin` / constructor runtime level).

Example:
- `Logger<LogLevel::Warn>` will strip `Trace/Debug/Info` call sites at compile time.
- `Warn/Error/Fatal` remain compiled and are runtime-filtered.

## Structured Attributes And Bounded Storage

Attribute value types:
- `NGIN::Int64`
- `NGIN::UInt64`
- `double`
- `bool`
- `std::string_view`

Current defaults in `NGIN::Log::Config`:
- `MaxMessageBytes = 512`
- `MaxAttributes = 8`
- `MaxAttrTextBytes = 256`
- `AsyncQueueCapacity = 8192`

Overflow behavior:
- Excess attributes are dropped and counted in `truncatedAttributeCount`.
- Message/attribute text truncation is counted in `truncatedBytes`.

## Sink Model

- Sinks implement `ILogSink::Write(const LogRecordView&)` + `Flush()`.
- Logger fan-out iterates active sink set.
- Read/dispatch path is lock-free (atomic active generation pointer).
- `SetSinks` is synchronized on write path.
- Old sink generations remain owned until logger destruction (important if sink sets are reconfigured frequently).

## Built-in Sinks

- `NullSink`: drops all records.
- `ConsoleSink`: stdout/stderr output with optional source emission.
- `FileSink`: thread-safe file output with reopen support.
- `AsyncSink<TSink>`:
  - bounded MPSC queue,
  - drop-on-full policy,
  - producer-side fixed-size copy path,
  - worker flush/drain support,
  - dropped-count reporting.

## Async Sink Ownership / Materialization

- `AsyncSink::Write` copies record data into owned inline payload before enqueue.
- Queue payload owns message/logger/attribute text required by the consumer thread.
- Producer path is allocation-free for record materialization/copy.

## Logger Registry

`LoggerRegistry` provides:
- named logger get-or-create,
- default sink set for newly created loggers,
- per-logger runtime level updates,
- per-logger sink replacement.

## Performance Guidance

For lowest overhead:
- Prefer lazy builder APIs in hot paths.
- Keep expensive computation inside lambda body.
- Use `*f` / `*fv` for convenience and non-critical paths.
- Avoid frequent sink reconfiguration in tight loops.
- Use `AsyncSink` when producer tail latency matters more than guaranteed durability under saturation.

## Examples And Benchmarks

- Example app: `NGIN.Log/examples/BasicLogging/main.cpp`
- Benchmarks: `NGIN.Log/benchmarks/LoggingBenchmarks.cpp`

## More Documentation

- [Documentation Index](./docs/README.md)
- [API Reference Guide](./docs/API.md)
- [Architecture Notes](./docs/Architecture.md)
- [Sinks Guide](./docs/Sinks.md)
