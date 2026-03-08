# NGIN.Log Sinks Guide

This guide covers sink contracts, built-in sink behavior, and operational considerations.

## Sink Contract

All sinks implement:

```cpp
class ILogSink {
public:
    virtual ~ILogSink() = default;
    virtual void Write(const LogRecordView& record) noexcept = 0;
    virtual void Flush() noexcept = 0;
};
```

Expected behavior:
- `Write` must consume record data synchronously with respect to record view lifetime.
- `Flush` should push buffered data to underlying output.
- methods are `noexcept`; sink internals should catch and absorb failures where needed.

## Sink Construction

Use `MakeSink<TSink>(...)`:

```cpp
using LoggerType = NGIN::Log::Logger<NGIN::Log::LogLevel::Trace>;

LoggerType::SinkSet sinks {};
sinks.push_back(NGIN::Log::MakeSink<NGIN::Log::ConsoleSink>());
```

`MakeSink` returns:
- `SinkPtr` (`NGIN::Memory::Shared<ILogSink>`)
- empty pointer if allocation/construction fails

## `NullSink`

Purpose:
- benchmark/control path,
- disable output without changing call sites.

Behavior:
- `Write` no-op,
- `Flush` no-op.

## `ConsoleSink`

Options (`ConsoleSinkOptions`):
- `useStderrForErrors`
- `includeSource`
- `autoFlush`

Behavior:
- formats human-readable line output,
- writes to stdout/stderr based on level/options,
- internal mutex for thread-safe writes.

Use cases:
- local development,
- diagnostics in short-lived tools.

## `FileSink`

Options (`FileSinkOptions`):
- `append`
- `autoFlush`

Behavior:
- writes formatted lines to file,
- thread-safe via internal mutex,
- `Reopen()` allows manual reopen/rotation coordination.

Use cases:
- persistent service logging,
- integration with external log collectors tailing files.

## `AsyncSink<TSink, QueueCapacity, BatchSize>`

Template parameters:
- `TSink`: wrapped concrete sink type
- `QueueCapacity`: bounded ring size, power-of-two
- `BatchSize`: max dequeue batch per worker iteration

Construction:

```cpp
auto fileSink = NGIN::Memory::MakeScoped<NGIN::Log::FileSink>("app.log");
auto async = NGIN::Log::MakeSink<NGIN::Log::AsyncSink<NGIN::Log::FileSink>>(std::move(fileSink));
```

Behavior:
- producer `Write` enqueues owned payload,
- on full queue: record dropped (producer never blocks),
- worker thread drains queue and writes to wrapped sink,
- periodic synthetic warning record reports dropped count.

Counters:
- `GetDroppedCount()`
- `GetErrorCount()`
- `GetEnqueuedCount()`

Flush + shutdown:
- `Flush` waits until queue drains, then flushes wrapped sink.
- destruction stops worker, drains queue, and flushes.

## Materialization And Lifetime

Why materialization matters:
- `LogRecordView` carries `string_view` and spans; source memory may be stack-local.

`AsyncSink` resolves this by owning queued payload:
- message/logger/attribute text copied into inline fixed-capacity buffers,
- numeric/bool values copied by value,
- safe cross-thread handoff with no dangling references.

## Failure Policy

Framework-level behavior on sink errors:
- logger dispatch catches exceptions from sink `Write`/`Flush`,
- increments sink error counters,
- continues processing.

Sink implementation guidance:
- avoid recursive logging inside sink failure path,
- keep `Write` as short and deterministic as practical.

## Choosing Sink Topologies

Typical patterns:
- Development: `ConsoleSink`
- Production simple: `FileSink`
- Production latency-sensitive: `AsyncSink<FileSink>`
- Multi-destination: combine sink pointers in one `SinkSet`

Fan-out example:

```cpp
LoggerType::SinkSet sinks {};
sinks.push_back(NGIN::Log::MakeSink<NGIN::Log::ConsoleSink>());
sinks.push_back(NGIN::Log::MakeSink<NGIN::Log::FileSink>("app.log"));
```

## Reconfiguration Guidance

`Logger::SetSinks` is safe during concurrent logging, but:
- reconfiguration is write-synchronized,
- old sink generations stay alive until logger destruction.

Recommendation:
- treat sink reconfiguration as operational control path, not hot path.
