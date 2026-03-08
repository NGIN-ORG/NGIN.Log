# NGIN.Log Architecture

This document describes runtime behavior and performance-relevant design decisions.

## High-Level Flow

Application call site:
1. Choose static level at call site (`Log<Level>` or `Trace/Debug/...` helpers).
2. Apply compile-time filtering (`if constexpr` against `CompileTimeMin`).
3. Apply runtime level filtering (`m_runtimeMin` atomic).
4. Materialize a bounded `RecordBuilder` on the stack.
5. Build message/attributes (lazy lambda or formatter policy path).
6. Dispatch immutable `LogRecordView` to active sinks.

## Compile-Time + Runtime Filtering Model

For a logger `Logger<CompileTimeMin>`:
- If `call_level < CompileTimeMin`: call site is compiled out.
- Else: call site remains and is checked against runtime minimum.

This yields:
- predictable dead-stripping for disabled low levels,
- dynamic control for enabled levels.

## Sink Publication Model

`Logger` stores sink sets as immutable generations:
- active generation pointer is stored atomically,
- dispatch path loads active generation and iterates without locking,
- `SetSinks` acquires write mutex and publishes a new generation pointer.

Generation lifetime:
- previous generations remain owned by logger until logger destruction.
- this avoids use-after-free for concurrent readers.

Operational implication:
- extremely frequent `SetSinks` calls can grow retained generation memory.
- configuration churn should be infrequent relative to log dispatch.

## Dispatch Error Model

Public APIs are `noexcept`.

If sink operations throw:
- logger catches exceptions,
- increments sink error counter,
- continues dispatching remaining sinks.

Recursion guard:
- thread-local dispatch flag prevents recursive log re-entry loops.

## Record Materialization

`RecordBuilder` is bounded and stack-based:
- message buffer size: `Config::MaxMessageBytes`
- max attributes: `Config::MaxAttributes`
- inline attribute text pool: `Config::MaxAttrTextBytes`

Overflow is not fatal:
- excess attributes dropped, counted,
- excess text bytes truncated, counted.

## Async Sink Architecture

`AsyncSink<TSink>`:
- wraps a concrete sink,
- uses bounded MPSC ring queue,
- single consumer worker thread,
- drains in batches (`BatchSize` template parameter),
- drop-on-full policy for producer protection.

Producer path characteristics:
- converts `LogRecordView` into owned inline payload,
- fixed-size copies for message/logger/attribute text,
- queue CAS operations,
- no producer-side heap allocation for record materialization.

Consumer path:
- converts owned payload back to `LogRecordView`,
- calls wrapped sink `Write`,
- periodically emits dropped-record synthetic warning lines.

Flush semantics:
- waits until queue is empty and pending count is zero,
- then flushes wrapped sink.

## Formatting Paths

1. Lazy builder path:
- user constructs message in lambda.
- best control for hot-path behavior.

2. `*f` path (`std::format_string`):
- compile-time format checking.

3. `*fv` path (`std::string_view` + `std::format_args`):
- runtime format support,
- implemented via `std::vformat` in `RecordBuilder::VFormat`,
- may allocate internally.

## Threading Summary

- Logging API is safe for concurrent producer threads.
- Sink thread-safety is sink-specific.
- `ConsoleSink` and `FileSink` serialize internal writes with mutexes.
- `AsyncSink` decouples producers from sink I/O by queueing.

## Why Explicit Source For `*f` / `*fv`

Without macros, wrapper-based convenience overloads cannot guarantee true call-site source metadata in a portable way.
NGIN.Log therefore requires explicit source for formatting APIs to preserve correctness.

Lazy builder helpers (`Trace/Debug/...`) still default source at the direct call site.
