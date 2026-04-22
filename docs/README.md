# NGIN.Log Documentation

This folder contains implementation-aligned documentation for `NGIN.Log`.

## Documents

- [Quick Start](./QuickStart.md)
  - first console logger
  - direct-message vs builder usage
  - scoped context and async file setup

- [Production Guide](./Production.md)
  - recommended sink topologies
  - formatter selection
  - async overflow policy guidance

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

- [Performance Guide](./Performance.md)
  - benchmark interpretation
  - what the numbers do and do not mean
  - how to run the benchmark target

## Recommended Read Order

1. [Quick Start](./QuickStart.md)
2. [Production Guide](./Production.md)
3. [API Guide](./API.md)
4. [Sinks](./Sinks.md)
5. [Architecture](./Architecture.md)
6. [Performance Guide](./Performance.md)
