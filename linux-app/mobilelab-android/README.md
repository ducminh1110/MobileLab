# MobileLab Android — Linux Hybrid x86_64 + ARM64

The Linux execution side of MobileLab for Android device orchestration, hybrid ABI scheduling, Android emulator management, container-backed Android experiments, and parallel app development.

This is a **Linux application/framework for Android testing**, not an Android APK. The current Android direction is intentionally hybrid:

- **x86_64 is the preferred near-term path** because Android SDK images, emulator acceleration, and package availability are currently better.
- **ARM64 remains enabled but fundamental/experimental** because some packages, images, and runtime compatibility paths are still stricter.
- Both ABIs can be discovered, displayed, scheduled, and exposed through the local API at the same time.

## What is implemented

- Native Qt 6 desktop application for Linux
- Hybrid Android target discovery across `x86_64` and `arm64-v8a`
- ABI stability annotations (`preferred`, `fundamental`, `limited`) in runtime data and UI
- Android emulator integration when the host provides the emulator binary
- ARM64-native userspace execution mode for hosts without nested virtualization
- QEMU AArch64/x86_64 capability probing
- Automatic KVM detection with degraded userspace fallback messaging
- Resource-aware job scheduler with weighted execution costs
- Local REST API on `127.0.0.1:4000`
- Device, matrix, runtime, scheduler and console views
- Runtime capability probing (host arch, KVM, QEMU, Android emulator, installed ABIs)
- Device start/stop/restart and shell surfaces
- Device tagging, ABI/API/backend/state search, and health scoring
- Process-backed scheduler with priority queueing, bounded retries, cancellation, and dry-run capacity estimates
- VS Code launch action for coding apps side-by-side with the device lab
- Backend status reporting suitable for CI and future MCP integration

## Execution model

```text
                    MobileLab Android Hybrid
                              │
                  ┌───────────┴───────────┐
                  │    Orchestration API   │
                  └───────────┬───────────┘
                              │
             ┌────────────────┼────────────────┐
             ▼                ▼                ▼
        Scheduler       Google Emulator   Container Runtime
             │          ┌──────┴──────┐         │
             ▼          ▼             ▼         ▼
       Resource Pool  x86_64       arm64-v8a  Waydroid/QEMU
                    preferred     fundamental experimental
```

The runtime probes the host before selecting an execution mode. A machine without `/dev/kvm` is not treated as accelerated: virtualization-dependent workloads are downgraded, reduced in parallelism, or left unavailable.

## Developer notes

Android support was initially aimed at **ARM64-only**, but the ARM64 package/image ecosystem is still too constrained for a stable first release. The project is therefore moving to a **hybrid x86_64 + ARM64** plan:

1. Keep ARM64 in the model so native ARM hosts and future ARM image availability are not blocked.
2. Prioritize x86_64 for day-to-day emulator workflows while compatibility gaps are burned down.
3. Surface ABI stability in the UI/API instead of hiding it behind a generic target list.
4. Keep container Android (Waydroid/QEMU) experimental and license-clean; MobileLab does not bundle proprietary Android system images.

Relevant configuration files:

- `config/matrix/hybrid-x86_64-arm64.yaml`
- `config/backends/hybrid-android.yaml`
- `config/backends/waydroid-arm64.yaml`
- `config/features-priority.yaml`

## REST API

The local backend currently exposes:

```text
GET  /status
GET  /devices
POST /runs
POST /devices/:id/start
POST /devices/:id/stop
GET  /scheduler/dry-run
```

Device responses include `arch`, `backend`, `stability`, `tags`, and `health_score` so callers can distinguish x86_64-preferred targets from ARM64-fundamental targets and quickly filter weak or incompatible devices. The scheduler status and dry-run API expose priority-aware queue capacity before a run is submitted. Submitted commands execute through the local scheduler; the API only binds to loopback, and callers should treat command submission as trusted local automation.

Example:

```bash
curl http://127.0.0.1:4000/status
curl http://127.0.0.1:4000/devices
curl -X POST http://127.0.0.1:4000/runs \
  -H 'Content-Type: application/json' \
  -d '{"abi":"x86_64","command":"./gradlew connectedCheck","priority":80,"retries":1}'
```

`POST /runs` accepts `target` or `abi` (the runtime chooses the healthiest matching target), `command`, `cost` (weighted slot cost), `priority` (0–100), and `retries` (0–10). The request body is validated, including a 4 KiB command limit; an invalid or unavailable target returns `400` rather than queuing a job that cannot run.

## Build

Requires Qt 6 Widgets + Network and CMake 3.20+.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/mobilelab-android
```

For ARM64 Linux hosts, build natively or cross-compile with a standard AArch64 C++ toolchain. For x86_64 Linux hosts, install x86_64 Android system images first and add ARM64 images only where compatibility testing is needed.

## VS Code workflow

The top toolbar includes a **VS Code** action. It launches the `code` CLI in the current MobileLab working directory so app code can be edited while Android devices are running. If `code` is not available on `PATH`, the UI logs the limitation and shows a non-fatal message.

## Design relationship to the macOS application

The macOS application remains the primary iOS-facing product. Its SwiftUI dashboard already provides the device grid, test runs, logs, timeline and inspector concepts used by MobileLab. The Linux application keeps those product-level abstractions while implementing the runtime layer with portable Linux components.

This allows the Android work to be developed and tested independently without pretending that iOS/macOS components can execute on Linux.
