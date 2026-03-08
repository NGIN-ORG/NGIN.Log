# NGIN.Log Documentation

This folder contains implementation-aligned documentation for `NGIN.Log`.

## Documents

- [API Guide](./API.md)
  - Public types and logger APIs
  - `*f` and `*fv` usage patterns
  - Registry usage

- [Architecture](./Architecture.md)
  - Dispatch flow
  - Compile-time/runtime filtering model
  - Sink generation publication model
  - Async materialization and queue behavior

- [Sinks](./Sinks.md)
  - `ILogSink` contract
  - Built-in sinks (`NullSink`, `ConsoleSink`, `FileSink`, `AsyncSink`)
  - Operational behavior and failure model

## Recommended Read Order

1. [API Guide](./API.md)
2. [Architecture](./Architecture.md)
3. [Sinks](./Sinks.md)
