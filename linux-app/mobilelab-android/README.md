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
- Local REST API on `127.0.0.1:4100` (override with `MOBILELAB_ANDROID_API_PORT`)
- Device, matrix, runtime, scheduler and console views
- Runtime capability probing (host arch, KVM, QEMU, Android emulator, installed ABIs)
- Device start/stop/restart and shell surfaces
- Device tagging, ABI/API/backend/state search, and health scoring
- Scheduler priority queue metadata, retry policy metadata, and dry-run capacity estimates
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

Device responses include `arch`, `backend`, `stability`, `tags`, and `health_score` so callers can distinguish x86_64-preferred targets from ARM64-fundamental targets and quickly filter weak or incompatible devices. The scheduler status and dry-run API expose priority-aware queue capacity before a run is submitted.
The port defaults to 4100 and is set with `MOBILELAB_ANDROID_API_PORT`.

Example:

```bash
curl http://127.0.0.1:4100/status
curl http://127.0.0.1:4100/devices
curl -X POST http://127.0.0.1:4100/runs
```

## Install

Download from the [releases page](https://github.com/ducminh1110/MobileLab/releases):

```sh
# Debian / Ubuntu (Qt 6 and the other libraries come in as dependencies)
sudo apt install ./MobileLab-linux-amd64.deb
mobilelab-android            # or start it from the application menu

# any other x86_64 distribution with Qt 6 installed
tar xzf MobileLab-linux-x86_64.tar.gz && ./mobilelab-android/bin/mobilelab-android
```

Build the package yourself with `cmake -S . -B build -G Ninja && cmake --build build && (cd build && cpack -G DEB)`.

## Build

Requires Qt 6 Widgets, Network, Svg and Test (developed against Qt 6.4.2; newer Qt 6 uses `QStyleHints::colorScheme` behind a version check) and CMake 3.20+. Build out of tree:

```bash
sudo apt install qt6-base-dev qt6-svg-dev cmake g++
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/mobilelab-android
ctest --test-dir build --output-on-failure
cmake --install build --prefix /usr/local
```

For ARM64 Linux hosts build natively or with a standard AArch64 toolchain.

## Look and feel

The UI follows Xcode 26: navigator (six tabs), editor with jump bar, inspector, debug area, capsule status in the toolbar, and an own implementation of Liquid Glass (`src/ui/glass`, spec in `docs/design/liquid-glass.md`).

- Fonts: Inter (UI) and JetBrains Mono (code), embedded via qrc from `shared/fonts`. No system or SF font is used as primary.
- Theme follows the desktop colour scheme; override in Settings or with `MOBILELAB_THEME=light|dark`.
- Glass levels: `MOBILELAB_GLASS=off|blur|full` (also in Settings). `off` is an opaque fill, `blur` is frosted tint without displacement, `full` adds refraction, chromatic offset and rim. Without a backdrop it falls back to tint plus rim. `MOBILELAB_REDUCE_TRANSPARENCY=1` forces off; `MOBILELAB_REDUCED_MOTION=1` disables animations.

### Shortcuts

| Keys | Action |
|---|---|
| Ctrl+0 | Toggle navigator |
| Ctrl+Alt+0 | Toggle inspector |
| Ctrl+Shift+Y | Toggle debug area |
| Ctrl+1 .. Ctrl+6 | Devices, Tests, Issues, Find, Debug, Reports |
| Ctrl+R / Ctrl+. | Run / Stop |
| Ctrl+Shift+O | Open Quickly |
| Ctrl+, | Settings |

Dragging a pane handle past its minimum collapses it; double-click on a handle toggles it. Sizes, collapsed state and theme persist via QSettings.

### Environment variables

`MOBILELAB_ANDROID_API_PORT` (default 4100), `MOBILELAB_ANDROID_ARTIFACTS`, `MOBILELAB_ANDROID_CONFIG`, `MOBILELAB_SETTINGS_DIR`, `MOBILELAB_POLL_MS`, `MOBILELAB_BOOT_TIMEOUT_S`, `MOBILELAB_SCREENSHOT_DIR`, `MOBILELAB_SCREENSHOT_SIZE`.

### Screenshot mode

`MOBILELAB_SCREENSHOT_DIR=/some/dir MOBILELAB_SCREENSHOT_SIZE=1477x959 QT_QPA_PLATFORM=offscreen ./build/mobilelab-android` drives every navigator, editor and dialog, writes PNGs plus `manifest.txt` and `glass-stats.txt` (glass repaint times) and exits.

## Tests and what is verified

- Unit tests: glass maths (SDF, refraction table), log classifier, theme tokens (spec values, contrast, fonts), core, window behaviour (shortcuts, collapse, drag snap, persistence, Open Quickly), and a full matrix run against a fake SDK.
- `tests/fixtures` holds a fake SDK (emulator, avdmanager, adb scripts and AVD files); the `mobilelab-screenshots` ctest runs the app against it offscreen, checks PNGs exist and are not blank, and runs the glass proof (`mobilelab-glass-proof`, capsule over a striped backdrop).
- Not verified: a real Android SDK/emulator or Waydroid, real Wayland/X11 compositing of translucent popups, Qt newer than 6.4.2.

## VS Code workflow

The top toolbar includes a **VS Code** action. It launches the `code` CLI in the current MobileLab working directory so app code can be edited while Android devices are running. If `code` is not available on `PATH`, the UI logs the limitation and shows a non-fatal message.

## Design relationship to the macOS application

The macOS application remains the primary iOS-facing product. Its SwiftUI dashboard already provides the device grid, test runs, logs, timeline and inspector concepts used by MobileLab. The Linux application keeps those product-level abstractions while implementing the runtime layer with portable Linux components.

This allows the Android work to be developed and tested independently without pretending that iOS/macOS components can execute on Linux.
