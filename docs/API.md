# NGIN.Log API Guide

This guide describes the current public API surface in `include/NGIN/Log`.

## Header Entry Points

- `#include <NGIN/Log/Log.hpp>` for the main API
- `#include <NGIN/Log/Macros.hpp>` for optional source-capturing format macros

## Core Types

### `LogLevel`

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

### `LogAttribute` and `LogRecordView`

Attribute values on the transport path remain:
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
- `attributes`
- `truncatedAttributeCount`
- `truncatedBytes`

## `RecordBuilder`

Stack-only bounded builder:
- `Message(std::string_view)`
- `Format(std::format_string<Args...>, Args&&...)`
- `VFormat(std::string_view, std::format_args)`
- `Attr(std::string_view key, TValue&& value)`

Normalized attribute inputs now include:
- signed and unsigned integrals
- floating-point values
- `bool`
- enums
- `std::string_view`, `const char*`, `std::string`
- `std::error_code`
- `std::chrono::duration`
- pointers

Bounded behavior:
- message text capped by `Config::MaxMessageBytes`
- attributes capped by `Config::MaxAttributes`
- attribute string text pooled in bounded inline storage

## Scoped Context

- `using ContextValue = std::variant<...>`
- `struct ContextAttribute`
- `ScopedLogContext PushLogContext(std::initializer_list<ContextAttribute>)`

Context is thread-local. Merge order is:
1. outer scopes
2. inner scopes
3. per-record attributes

Later duplicate keys win.

## Formatter API

### `IRecordFormatter`

Virtual formatter contract:
- `Format(const LogRecordView&, std::string& output) noexcept`

Helpers:
- `MakeRecordFormatter<TFormatter>(...) -> RecordFormatterPtr`

Built-in formatters:
- `TextRecordFormatter`
- `JsonRecordFormatter`
- `LogFmtRecordFormatter`

Timestamp rendering styles:
- `EpochNanoseconds`
- `EpochMilliseconds`
- `Iso8601Utc`
- `Iso8601Local`

## `Logger<CompileTimeMin, FormatterPolicy>`

Main logging type.

Key API:
- `SetRuntimeMin(LogLevel)`
- `GetRuntimeMin()`
- `SetSinks(SinkSet)`
- `GetSinksSnapshot()`
- `Flush()`
- `GetSinkErrorCount()`

Logging styles:
- `Log<Level>(fn, source = current())`
- direct message overloads: `Trace/Debug/Info/Warn/Error/Fatal(std::string_view, source = current())`
- builder overloads: `Trace/Debug/Info/Warn/Error/Fatal(fn, source = current())`
- format helpers: `Tracef/Debugf/...`
- runtime format helpers: `Tracefv/Debugfv/...`

Important source rule:
- class-based `*f` and `*fv` APIs require explicit `std::source_location`
- optional macros in `NGIN/Log/Macros.hpp` forward the call-site source automatically

## Registry API

### `BasicLoggerRegistry<CompileTimeMin, FormatterPolicy>`

Main registry type. Backward-compatible alias:
- `using LoggerRegistry = BasicLoggerRegistry<LogLevel::Trace, StdFormatter>;`

Supporting types:
- `LoggerConfig`
- `LoggerRule`

Key API:
- `GetOrCreate(name, runtimeMin)`
- `Get(name)`
- `SetDefaultRuntimeMin(level)`
- `SetDefaultSinks(sinks)`
- `GetDefaultRuntimeMin()`
- `GetDefaultSinks()`
- `UpsertRule(rule)`
- `RemoveRule(prefix)`
- `GetEffectiveConfig(name)`
- `SetLoggerRuntimeMin(name, level)`
- `ReplaceLoggerSinks(name, sinks)`
- `GetLoggerNames()`

Rule matching uses the longest dot-delimited prefix.

## Sinks

Core sink contract:
- `Write(const LogRecordView&) noexcept`
- `Flush() noexcept`

Built-in sinks:
- `NullSink`
- `ConsoleSink`
- `FileSink`
- `RotatingFileSink`
- `AsyncSink<TSink, QueueCapacity, BatchSize>`

### `AsyncSink`

Options:
- `overflowPolicy`
- `blockTimeout`
- `emitDropReports`
- `notifyOnError`

Overflow policies:
- `DropNewest`
- `Block`
- `BlockForTimeout`
- `SyncFallback`

Stats:
- `GetStats()`
- `GetDroppedCount()`
- `GetErrorCount()`
- `GetEnqueuedCount()`

## API Contracts And Caveats

- Public logging APIs are `noexcept`
- sink exceptions are swallowed and counted
- re-entrant dispatch is guarded by a thread-local recursion check
- dynamic format (`*fv`) may allocate due to `std::vformat`
- context propagation is thread-local only
