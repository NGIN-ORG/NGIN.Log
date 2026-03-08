# NGIN.Log API Guide

This guide describes the public API surface in `include/NGIN/Log`.

## Header Entry Point

Use umbrella include for most applications:

```cpp
#include <NGIN/Log/Log.hpp>
```

## Core Types

## `LogLevel`

`enum class LogLevel : NGIN::UInt8`

Levels:
- `Trace`
- `Debug`
- `Info`
- `Warn`
- `Error`
- `Fatal`
- `Off`

Utility:
- `ToString(LogLevel) -> std::string_view`

## `LogAttribute` and `LogRecordView`

Attribute value supports:
- `NGIN::Int64`
- `NGIN::UInt64`
- `double`
- `bool`
- `std::string_view`

`LogRecordView` fields:
- `timestampEpochNanoseconds`
- `level`
- `loggerName`
- `message`
- `source`
- `threadIdHash`
- `attributes` (`std::span<const LogAttribute>`)
- `truncatedAttributeCount`
- `truncatedBytes`

## `RecordBuilder`

Stack-only bounded builder:
- `Message(std::string_view)`
- `Format(std::format_string<Args...>, Args&&...)`
- `VFormat(std::string_view, std::format_args)` (runtime format, may allocate internally)
- `Attr(std::string_view key, const AttributeValue&)`

Bounded behavior:
- message text capped by `Config::MaxMessageBytes`
- attributes capped by `Config::MaxAttributes`
- attribute string text pooled in bounded inline storage (`Config::MaxAttrTextBytes`)

Truncation counters:
- `GetTruncatedAttributeCount()`
- `GetTruncatedBytes()`

## Sink Interfaces

## `ILogSink`

Virtual sink contract:
- `Write(const LogRecordView&) noexcept`
- `Flush() noexcept`

Helpers:
- `NullSink` (drops records)
- `MakeSink<TSink>(...) -> SinkPtr`

`SinkPtr`:
- `using SinkPtr = NGIN::Memory::Shared<ILogSink>;`

## `Logger<CompileTimeMin, FormatterPolicy>`

Main logging type.

Template params:
- `CompileTimeMin`: call sites below this level compile out via `if constexpr`.
- `FormatterPolicy`: formatting backend policy (`StdFormatter` default).

Key API:
- `SetRuntimeMin(LogLevel)`
- `GetRuntimeMin()`
- `SetSinks(SinkSet)`
- `GetSinksSnapshot()`
- `Flush()`
- `GetSinkErrorCount()`

Lazy logging:
- `Log<Level>(fn, source = current())`
- `Trace/Debug/Info/Warn/Error/Fatal`

Format-checked logging (`std::format_string`):
- `Logf<Level>(std::source_location, std::format_string<...>, ...)`
- `Tracef/Debugf/Infof/Warnf/Errorf/Fatalf`

Runtime format logging (`std::format_args`):
- `Logfv<Level>(std::source_location, std::string_view, std::format_args)`
- `Tracefv/Debugfv/Infofv/Warnfv/Errorfv/Fatalfv`

Important source rule:
- `*f` and `*fv` require explicit `std::source_location` argument.

## `LoggerRegistry`

Named logger registry with thread-safe access:
- `GetOrCreate(name, runtimeMin)`
- `Get(name)`
- `SetDefaultSinks(SinkSet)`
- `GetDefaultSinks()`
- `SetLoggerRuntimeMin(name, level)`
- `ReplaceLoggerSinks(name, sinks)`
- `GetLoggerNames()`

## Built-in Sinks

- `ConsoleSink`
- `FileSink`
- `AsyncSink<TSink, QueueCapacity, BatchSize>`

`AsyncSink` wraps a concrete sink allocated as `NGIN::Memory::Scoped<TSink>`.

## API Usage Patterns

## Preferred hot-path pattern

```cpp
logger.Info([&](NGIN::Log::RecordBuilder& rec) {
    rec.Message("request complete");
    rec.Attr("status", static_cast<NGIN::Int64>(200));
});
```

## Compile-time format-checked path

```cpp
logger.Warnf(
    std::source_location::current(),
    "latency_ms={} endpoint={}",
    latencyMs,
    endpoint);
```

## Dynamic format string path

```cpp
std::string fmt = "payload={} retries={}";
int         retries = 2;
logger.Debugfv(
    std::source_location::current(),
    fmt,
    std::make_format_args(payload, retries));
```

## API Contracts And Caveats

- Public logging APIs are `noexcept`.
- Sink exceptions are swallowed and counted as sink errors.
- Re-entrant dispatch is guarded by thread-local recursion check.
- `std::format_args` references passed lvalues; keep arguments alive through the logging call.
- Runtime format (`*fv`) is convenience-focused and may allocate due to `std::vformat`.
