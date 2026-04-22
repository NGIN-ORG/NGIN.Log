# NGIN.Log Documentation

This folder contains implementation-aligned documentation for `NGIN.Log`.

## Documents

- [API Guide](./API.md)
  - Public types and logger APIs
  - direct-message and builder call styles
  - context, registry, formatter, and async options

- [Architecture](./Architecture.md)
  - Dispatch flow
  - compile-time/runtime filtering model
  - sink snapshot publication model
  - context merge and async delivery behavior

- [Sinks](./Sinks.md)
  - `ILogSink` contract
  - built-in sinks and formatter split
  - rotating-file and async operational behavior

## Recommended Read Order

1. [API Guide](./API.md)
2. [Architecture](./Architecture.md)
3. [Sinks](./Sinks.md)
