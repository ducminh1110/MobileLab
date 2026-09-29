# Where things stand and what to do next

Written at the end of a work session so the next one can continue without re-discovering anything.

## State of each part

| Part | State | Verified how |
| --- | --- | --- |
| Backend (`backend/`) | Rebuilt and committed. Async dispatcher, retries, cancel, auto-provisioning, matrix runs, persistence, real `/doctor`, catalog aware of runtime compatibility, optional token, demo mode. | 88 tests (unit + integration), all passing. The real `simctl`/`xcodebuild` path has **never run on a Mac**; everything was exercised against the stateful mock and parser fixtures. |
| CLI (`cli/`) | Done and committed. | 112 tests against the real backend in-process; run by hand against a demo backend; checked on Node 20.11. |
| Docs, Makefile, CI, release packaging | Done: `docs/api.md`, `docs/architecture`, `Makefile`, `.github/workflows/ci.yml`, `scripts/package_runtime.sh`, updated release workflow. | `scripts/package_runtime.sh` was only checked piecewise (backend runtime started standalone); run it end to end once. The release workflow is unchanged in behaviour but untested on a macOS runner. |
| Web dashboard (`backend/public/`) | **Work in progress**, committed as `wip(...)` snapshots. Most screens exist in the Xcode layout with Inter (welcome, devices + simulator preview, tests tree, issues, reports, report with Summary/Tests/Logs, debug navigator, settings, quick open, dialogs, dark mode). | Screenshots taken by the builder in `scratchpad/web/shots` (not in the repo). No independent review, no fix round, and its own `dashboard.integration.test.ts` may not exist yet. |
| macOS app (`macos-app/`) | **Work in progress**: logic moved to the SwiftPM `IOSLabDashboardCore` target, views being rewritten. Snapshots only. | SwiftUI cannot be compiled on Linux. Check whether a Swift toolchain was installed and what `swift test` says before trusting anything. |
| Linux app (`linux-app/mobilelab-android/`) | **Not started** (queued behind the others). | - |

## Known problems seen in the web dashboard screenshots

1. The red inline pill on a failing log line covers the end of the line's text (Xcode's pill sits after it).
2. The destination popup's "Create on Demand" list offers combinations that do not exist (for example
   iPhone 16 Pro on iOS 17.5). Filter by `runtime.supportedDeviceTypes` from `GET /catalog`, and handle
   `skipped` in the `POST /runs` response.
3. The first line of the console pane is cut off at the top; long console lines do not wrap.
4. Not yet checked: keyboard navigation of the trees, the narrow (900px and 390px) layouts, offline and token
   flows, performance with many jobs and a 50k-line log.

## How to resume the interface work

The whole redesign was run as one workflow: build the three interfaces in parallel, review each with three
lenses (fidelity, correctness, ergonomics), have a skeptic try to refute every finding, fix what survives,
then a final check. The script is saved at `.claude/workflows/xcode-style-ui.js`. Before re-running it:

1. Look at what is already in the tree (the snapshots above) and tell the builders to continue from it rather
   than start over.
2. The workflow expects the two Xcode reference screenshots at
   `<scratchpad>/reference/xcode-1.png` and `xcode-2.png` (they are Apple's images and are not in the repo).
   Re-attach them and update the `SCRATCH` path at the top of the script.
3. The design is specified in `docs/design/xcode-interface.md` (anatomy, tokens, mapping, keyboard, data
   contract, fidelity checklist). Inter and JetBrains Mono are in `shared/fonts` and
   `backend/public/assets/fonts`; the UI must not use San Francisco.

## Next feature (asked for, not started)

**Connect a Mac from the web UI**: a button that takes `username@address` plus a password or an uploaded SSH
key, connects over SSH, installs the MobileLab remote server on the Mac automatically, and sets up remote
work. The full concept is in `docs/design/remote-mac.md`.

## Smaller open items

* Rewrite the root `README.md` (still the long original; several usage examples in it describe commands that
  did not exist before this work, such as `ioslab test --devices=N`; the CLI now has `--parallel`).
* Add macOS (Swift Core tests, `xcodebuild build`) and Linux (Qt build with offscreen screenshot test) jobs to
  `.github/workflows/ci.yml` once those apps are done.
* The macOS app starts a bundled backend with `/usr/bin/env node`; a Mac without Node needs a clear message
  or a bundled Node (the Connect Mac concept solves this for remote Macs).
* Run the real thing on a Mac: `IOSLAB_SIMULATOR_MOCK=false`, spawn a simulator, run a real scheme, check
  the parser against real `xcodebuild` output (XCTest and Swift Testing) and the doctor checks.
