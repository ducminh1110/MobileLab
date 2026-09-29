# Architecture

MobileLab orchestrates iOS Simulator test runs: it keeps a pool of simulators, matches queued
`xcodebuild test` jobs to them, streams what happens, and stores the results. This page describes what the
code does today. The API is in [`../api.md`](../api.md) and the interface design in
[`../design/xcode-interface.md`](../design/xcode-interface.md).

```
  web dashboard   macOS app    CLI (ioslab)    MCP clients    CI / scripts
         \            |            |               |              /
          +-----------+------------+---------------+-------------+
                                   |  REST, WebSocket / SSE, JSON-RPC
                        +----------v-----------+
                        |  Fastify API layer   |  auth, validation, errors
                        +----------+-----------+
                                   |
                        +----------v-----------+      +------------------+
                        |    Orchestrator      +------+  EventHub        |  lifecycle events + live output
                        |  devices, jobs, runs |      +------------------+
                        |  dispatcher (pump)   |      +------------------+
                        +---+--------------+---+------+  StateStore      |  state.json, atomic writes
                            |              |          +------------------+
              +-------------v--+     +-----v-----------+
              | SimulatorEngine|     |   VMEngine      |  experimental, simulated:
              | simctl, xcode- |     | (never runs     |  models a VM lifecycle only
              | build, catalog |     |  tests)         |
              +-------+--------+     +-----------------+
                      |
              +-------v--------+
              | CommandRunner  |  RealCommandRunner (spawn) or MockCommandRunner (demo mode)
              +----------------+
```

## The orchestrator

* **Devices** live in a `PoolManager`. A simulator is created with `simctl create`, booted with `simctl boot`
  plus `bootstatus`, and can be shut down or deleted. Every operation is idempotent and tracked, so two
  calls for the same device cannot interleave.
* **Jobs** are queued in memory and persisted. `pump()` is a synchronous, idempotent function called whenever
  something might unblock work (job queued, device ready, job finished, capacity freed). For each queued job
  in FIFO order it: honours the run's `maxParallel`; reuses a ready device that matches the requested
  runtime and device type; otherwise waits for a booting one; otherwise, if `autoProvision` is on and
  capacity allows, creates an *ephemeral* simulator for it. Ephemeral simulators are removed as soon as no
  queued job can use them.
* **Execution** runs `xcodebuild test` with a per-attempt result bundle and log file, feeds every output line
  to the `TestResultParser` (XCTest and Swift Testing) and to the live event stream, and then settles the job:
  `completed`, `failed`, `cancelled`, or `retrying` with exponential backoff (build errors are never retried).
  Timeouts and cancellation kill the whole process group.
* **Capacity** counts every booted device (simulator 1, VM 4) against `min(cores, RAM / 2 GB)`, or
  `IOSLAB_MAX_LOAD`. Jobs that cannot get capacity wait with a visible `waitingReason` rather than failing.
* **Persistence**: devices, jobs, runs and the artifact index are written to `state.json` (debounced,
  atomic). On start the pool is reconciled with `simctl list`, jobs that were running are marked failed
  ("backend restarted"), and leftover ephemeral simulators are removed.

## Demo mode and the experimental VM

Simulators only exist on macOS, so elsewhere the backend swaps the command runner for a stateful mock of
`simctl` and `xcodebuild` (unknown runtime, booting twice, testing on a stopped device and so on all fail
the way the real tools do). Everything is labelled demo mode. The VM engine models the lifecycle of a
virtualised iOS guest (firmware stages, backups, input, fault-injection flags) so the API and interfaces can be
developed against it; it starts nothing and its devices are `canRunTests: false`, so the scheduler never
assigns work to them.

## Interfaces

* `backend/public/`: the web dashboard (static ES modules, no build step) served at `/`.
* `macos-app/`: the native macOS app (SwiftUI); logic lives in the SwiftPM `IOSLabDashboardCore` target.
* `cli/`: the `ioslab` command.
* `linux-app/mobilelab-android/`: the Linux Android device lab (Qt 6) with its own core; it does not use the
  Node backend.

All of them follow the same Xcode-style layout and vocabulary, defined once in
[`../design/xcode-interface.md`](../design/xcode-interface.md).
