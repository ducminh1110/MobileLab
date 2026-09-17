# MobileLab

> **Formerly iOSLab**

**Cross-platform mobile testing and device orchestration for modern development workflows.**

MobileLab is an open-source platform for orchestrating mobile application testing across devices, operating-system versions, and execution environments.

The project originated as **iOSLab**, a macOS-focused tool for running large iOS Simulator test matrices in parallel. Its architecture is now evolving toward a broader mobile orchestration platform, with **iOS as the primary platform** and **Android support under active development and testing**.

> **Project status:** iOS is the primary supported platform. Android support is experimental, under active development and internal testing, and is not yet publicly released.

---

## Table of Contents

* [Formerly iOSLab](#formerly-ioslab)
* [Why MobileLab](#why-mobilelab)
* [Project Status](#project-status)
* [Core Capabilities](#core-capabilities)
* [Platform Support](#platform-support)
* [Architecture](#architecture)
* [Installation](#installation)
* [Usage](#usage)
* [Migration from iOSLab](#migration-from-ioslab)
* [Roadmap](#roadmap)
* [Contributing](#contributing)
* [License](#license)

---

## Formerly iOSLab

MobileLab was originally created as **iOSLab** to solve a focused problem: executing large iOS Simulator test matrices concurrently on a single Mac rather than running each device and operating-system combination sequentially.

As the project evolved, its core orchestration architecture became increasingly independent of iOS-specific execution. Scheduling, resource management, device lifecycle management, APIs, automation, and tooling can be shared across different mobile execution backends.

That evolution is the foundation of the **iOSLab → MobileLab** transition.

### Why the repository still contains `iOSLab`

The migration is ongoing. Existing source code, CLI commands, environment variables, module names, and historical commits may still contain identifiers such as:

```text
iOSLab
ioslab
IOSLAB_*
```

These identifiers are retained where required for compatibility with the existing implementation and tooling.

They do **not** indicate a separate project.

**MobileLab is the current project name and the canonical name going forward.**

---

## Why MobileLab

Modern mobile applications need to be validated across increasingly large combinations of:

* device models
* operating-system versions
* screen configurations
* architectures
* runtime environments
* application states

Executing every combination sequentially quickly becomes inefficient.

MobileLab approaches this problem by treating mobile environments as a **managed device pool** rather than a sequence of independent test runs.

```text
Traditional sequential execution

Build
  │
  ▼
Device A
  │
  ▼
Device B
  │
  ▼
Device C
  │
  ▼
Device D


MobileLab

                         ┌── Device A
                         ├── Device B
Test Matrix ─────────────┼── Device C
                         ├── Device D
                         └── ...
```

The orchestration layer is responsible for coordinating environments, scheduling work, managing resources, and exposing the resulting capabilities to development and automation tooling.

---

## Project Status

| Component                        | Status            | Notes                                             |
| -------------------------------- | ----------------- | ------------------------------------------------- |
| iOS Simulator orchestration      | 🟢 Active         | Primary supported execution path                  |
| Device / OS matrix testing       | 🟢 Active         | Core testing workflow                             |
| Resource-aware scheduling        | 🟢 Active         | Controls execution based on resource requirements |
| REST API                         | 🟢 Active         | Core orchestration interface                      |
| MCP integration                  | 🟢 Active         | Programmatic access for compatible AI tooling     |
| Visual regression infrastructure | 🟢 Active         | Screenshot-based validation                       |
| iOS virtualization               | 🟡 Experimental   | API and implementation may change                 |
| Android execution                | 🟡 In development | Hybrid x86_64 + ARM64 Linux workbench under active development |
| Unified iOS / Android model      | 🟡 In development | Cross-platform orchestration                      |
| Distributed execution            | 🔵 Planned        | Multi-machine / multi-node execution              |

### Android status

Android support is currently **not a public stable feature**.

The Android implementation is being developed as a Linux hybrid workbench that can expose x86_64 and ARM64 targets at the same time. x86_64 is the current near-term focus because emulator images and packages are more broadly available; ARM64 remains fundamental/experimental while package availability and compatibility issues are resolved. Interfaces, execution backends, and configuration formats may change substantially before a public release.

---

# Core Capabilities

## Parallel execution

Run multiple mobile environments concurrently on a single host.

```bash
ioslab test --devices=32
```

The existing `ioslab` CLI namespace remains available during the migration.

---

## Device and OS matrix testing

Define a target testing matrix and allow the orchestration layer to manage execution across available environments.

```yaml
test:
  devices: 24
  ios_versions: [16, 17, 18]
```

The long-term architecture is designed to extend this model across multiple mobile platforms.

---

## Resource-aware scheduling

Different execution environments have different resource requirements.

MobileLab can associate execution targets with resource costs and use host availability when determining concurrency.

For example:

```text
Simulator  → cost weight 1
VM         → cost weight 4
```

The purpose is to prevent the scheduler from treating lightweight simulator workloads and substantially heavier virtualized environments as equivalent workloads.

---

## Visual regression testing

MobileLab can capture application states and compare screenshots against established baselines.

This enables detection of issues such as:

* layout changes
* rendering differences
* OS-specific visual regressions
* unexpected UI changes

---

## REST API

The orchestration layer exposes APIs for external tools and automation workflows.

Representative operations include:

```http
POST /devices/spawn
POST /tests/run
GET  /metrics
```

The API is intended to provide a programmatic interface for device management, test execution, and system observability.

---

## Model Context Protocol

MobileLab includes an MCP interface for compatible AI development tooling.

Representative operations include:

```text
list_devices
spawn_device
run_test
get_screenshot
inject_input
```

This allows supported agents and automation systems to interact with mobile testing environments through structured operations rather than relying exclusively on manual interaction.

---

## Local CI workflows

MobileLab can be integrated into local build, test, validation, and reporting workflows.

For example:

```bash
ioslab run pipeline.yaml
```

Existing `ioslab` naming is retained while the command-line interface transitions toward the MobileLab namespace.

---

# Platform Support

## iOS — Active

iOS is currently the primary supported platform.

Current capabilities include:

* iOS Simulator orchestration
* parallel simulator execution
* device and OS matrix testing
* resource-aware scheduling
* visual regression infrastructure
* REST API integration
* MCP integration
* experimental virtualized iOS environments

### Experimental iOS virtualization

The repository contains experimental infrastructure for virtualized iOS environments on supported Apple Silicon systems.

The VM lifecycle currently includes stages such as:

```text
fw_prepare
    ↓
fw_patch
    ↓
restore
    ↓
cfw_install
    ↓
boot
```

Virtualized environments have substantially different resource requirements from Simulator instances and are therefore handled as separate execution targets.

> **Note:** iOS virtualization is experimental. Interfaces and implementation details may change between releases.

---

## Android — Under Development

Android support is being developed as part of the cross-platform expansion.

Current areas of development include:

* Android emulator orchestration
* ARM-based Android execution research
* device lifecycle management
* resource-aware scheduling
* unified device abstractions
* cross-platform test matrix design

The Android backend is intended to participate in the same high-level orchestration model as iOS rather than becoming an entirely independent testing stack.

> ⚠️ **Android support is currently internal and experimental. It has not been publicly released and should not be treated as a stable API.**

---

# Architecture

MobileLab separates platform-specific execution backends from the shared orchestration layer.

```text
                         ┌───────────────────────┐
                         │       MobileLab       │
                         │  Orchestration Layer  │
                         └───────────┬───────────┘
                                     │
             ┌───────────────────────┼───────────────────────┐
             │                       │                       │
             ▼                       ▼                       ▼
       Device Pool             Job Scheduler           REST / MCP
             │                       │                       │
       ┌─────┴─────┐                 │                AI / CI / Tools
       │           │                 │
       ▼           ▼                 │
      iOS       Android              │
       │           │                 │
   Simulator    Emulator             │
       │           │                 │
   Experimental    ARM                │
       VM       Development           │
       │           │                 │
       └─────┬─────┘                 │
             │                       │
             └───────────┬───────────┘
                         ▼
                Resource Management
                         │
                         ▼
                  Test Execution
```

The architecture is built around a shared orchestration model:

```text
Platform Backend
       │
       ▼
Device Abstraction
       │
       ▼
Scheduler
       │
       ▼
Resource Manager
       │
       ▼
Test Execution
       │
       ├── REST
       ├── MCP
       └── CI / Tooling
```

Platform-specific backends remain responsible for their own execution environments, while scheduling, resource management, APIs, and higher-level orchestration can be shared.

This separation allows the existing iOS infrastructure to evolve toward cross-platform execution without forcing platform-specific implementation details into the orchestration layer.

---

# Installation

## Prerequisites

### iOS development

The current iOS workflow is designed for macOS environments with:

* macOS
* Xcode
* Apple Silicon recommended for virtualization workloads
* sufficient CPU and memory for the desired simulator concurrency

Higher simulator counts and virtualized environments require substantially more system resources.

### Android development

Android support is still under development, so requirements may change as the execution backend matures.

---

## Build from source

Clone the repository:

```bash
git clone https://github.com/ducminh11102012/MobileLab.git
cd MobileLab
```

Initialize the development environment:

```bash
make setup
```

To build without requiring a connected simulator environment:

```bash
IOSLAB_SIMULATOR_MOCK=true ./scripts/build_release.sh
```

The `IOSLAB_*` environment variables remain available for compatibility with the original iOSLab implementation.

---

## Prebuilt releases

Precompiled application bundles and CLI artifacts are distributed through the project's GitHub releases.

Some historical releases may continue to use the `ioslab` naming. This is expected during the ongoing migration.

---

# Usage

## Parallel iOS testing

Run a small parallel test workload:

```bash
ioslab test --devices=12
```

Run a larger workload:

```bash
ioslab test --devices=32
```

The number of environments that can run concurrently depends on the host's available CPU, memory, storage, and platform-specific constraints.

---

## Experimental VM lifecycle

For supported experimental virtualized environments:

```bash
ioslab vm new "iPhone VM" --cpu 4 --memory 6 --disk 64

ioslab vm boot <vmId>

ioslab vm backup <vmId> "Clean-Install"

ioslab vm restore <vmId> "Clean-Install"

ioslab vm list
```

VM functionality is experimental and may change between releases.

---

## Cross-platform matrix

The following represents the direction of the cross-platform configuration model:

```yaml
test:
  devices:
    - type: ios-simulator
      count: 12
      os_versions: [17, 18]

    - type: android-emulator
      count: 12
      os_versions: [14, 15]
```

> This cross-platform configuration is currently a development direction and should not be interpreted as a stable public Android interface.

---

# Migration from iOSLab

The project is transitioning from its original iOS-specific identity to the broader MobileLab architecture:

```text
                       iOSLab
                          │
                          │ evolution
                          ▼
                      MobileLab
                          │
              ┌───────────┴───────────┐
              ▼                       ▼
             iOS                   Android
           Active              Development
              │                       │
        ┌─────┴─────┐           ┌─────┴─────┐
        ▼           ▼           ▼           ▼
    Simulator   Experimental  Emulator     ARM
                   VM        Development   Research
```

During this transition, legacy identifiers may remain in:

* CLI commands
* environment variables
* source files
* module names
* configuration
* scripts
* release artifacts
* historical commits

For example:

```text
iOSLab
ioslab
IOSLAB_*
```

These identifiers are retained where necessary for compatibility.

They will be migrated progressively as the cross-platform architecture stabilizes.

---

# Roadmap

## Available

* [x] Parallel iOS Simulator execution
* [x] Device and OS matrix testing
* [x] Resource-aware scheduling
* [x] REST API
* [x] MCP integration
* [x] Local CI workflows
* [x] Visual regression infrastructure
* [x] Experimental iOS virtualization

## In development

* [ ] Android execution backend
* [ ] Android emulator orchestration
* [ ] ARM-focused Android execution
* [ ] Unified iOS / Android device model
* [ ] Cross-platform matrix configuration
* [ ] Expanded cross-platform scheduling

## Planned

* [ ] Multi-machine execution
* [ ] Distributed device pools
* [ ] Physical device integration
* [ ] Cross-platform visual regression
* [ ] Remote execution nodes
* [ ] AI-generated mobile test flows

> The roadmap describes project direction rather than guaranteed release timelines. Features and priorities may change as the architecture evolves.

---

# Contributing

Contributions are welcome.

Areas where contributions are particularly useful include:

* scheduling and resource management
* simulator and emulator orchestration
* Android support
* ARM execution infrastructure
* virtualization
* distributed execution
* testing infrastructure
* developer tooling
* documentation

For significant architectural changes, opening an issue before submitting a large pull request is recommended so the proposed approach can be discussed first.

---

# Project Status

MobileLab is an actively evolving open-source project.

The **iOS execution path is currently the most mature part of the platform**. Android and cross-platform execution are under active development and may change significantly before becoming stable public features.

As the project transitions from iOSLab to MobileLab, some implementation details and interfaces may continue to reflect the original iOS-focused architecture.

The repository should therefore be considered an evolving engineering project rather than a finalized cross-platform testing platform.

---

# License

MobileLab is released under the **MIT License**.

See [`LICENSE.md`](LICENSE.md) for the full license text.
