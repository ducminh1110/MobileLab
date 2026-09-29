export const meta = {
  name: 'xcode-style-ui',
  description: 'Build the web, macOS and Linux MobileLab interfaces in the Xcode layout with Inter, then review, verify findings adversarially, fix and re-verify each',
  whenToUse: 'Redesign all MobileLab user interfaces to match docs/design/xcode-interface.md',
  phases: [
    { title: 'Build', detail: 'one builder per interface (web, macOS, Linux)' },
    { title: 'Review', detail: 'three lenses per interface: fidelity, correctness, accessibility/ergonomics' },
    { title: 'Verify', detail: 'skeptic tries to refute each finding' },
    { title: 'Fix', detail: 'apply confirmed findings' },
    { title: 'Final', detail: 're-run checks and screenshots' },
  ],
}

const SCRATCH = '/tmp/claude-0/-home-user-MobileLab/58c13101-a621-5f70-9d63-76f94f8ba81b/scratchpad'
const REF1 = SCRATCH + '/reference/xcode-1.png'
const REF2 = SCRATCH + '/reference/xcode-2.png'

const COMMON = `
You are part of a team giving MobileLab three interfaces (web dashboard, macOS app, Linux app) that look and behave like Xcode. Read these first, completely:
- /home/user/MobileLab/docs/design/xcode-interface.md : the authoritative design spec (window anatomy, type scale, colour tokens for light and dark, icon rules, toolbar capsule, six navigators, editor types, inspector, debug area, states, keyboard, data contract, fidelity checklist).
- The two reference screenshots (Apple Xcode 26, light appearance). View them with the Read tool: ${REF1} and ${REF2}. The spec's numbers were sampled from them. Reference 1 = navigator (project tree) + source editor + canvas preview. Reference 2 = Debug navigator with gauges and thread frames + editor with an execution line and inline pill + debug area with variables view and green console.
Context: MobileLab (formerly iOSLab) orchestrates iOS Simulator test runs. The backend (Node, Fastify, TypeScript) was just rebuilt and is committed: routes in /home/user/MobileLab/backend/src/api/routes, types in backend/src/simulator/models/types.ts, events in backend/src/core/eventHub.ts, tests in backend/tests. On Linux the backend runs in demo mode with a stateful mock of simctl/xcodebuild; scheme names steer results (contains "fail" -> failing tests with assertion messages, "flaky" -> fails first attempt then passes, "missing" -> build error, "slow" -> slow and cancellable, anything else passes 8 of 8). Start it with: cd /home/user/MobileLab/backend && IOSLAB_DATA_DIR=<a temp dir> PORT=<your port> LOG_LEVEL=warn npx tsx src/index.ts  (run in the background with &, remember the PID, stop it with kill <pid>. NEVER use pkill -f or killall: they match your own shell and kill the session.)
Rules for everyone:
- The owner explicitly requires: Inter for ALL UI text (San Francisco, SF Mono, and system-ui/-apple-system stacks as the primary face are NOT acceptable; fall back only to generic sans-serif after Inter), JetBrains Mono for code, logs and identifiers, and an interface that looks like Xcode as closely as the platform allows. Fonts are already in the repo: /home/user/MobileLab/shared/fonts/*.ttf and /home/user/MobileLab/backend/public/assets/fonts/*.woff2 (Inter is variable there).
- Real data only. No fabricated numbers, no hard-coded PASSED rows, no decorative fake metrics. Empty states are honest (spec section 10).
- Do NOT run git commit, git push, git checkout, git stash or git reset. Do not edit files outside your own area (given below). Other engineers work in the other areas at the same time. The CLI in /home/user/MobileLab/cli is off limits to everyone.
- Use only your assigned ports. Put scratch files and screenshots under ${SCRATCH}/<your area>/ .
- Be honest in your final report: what you verified and how, what you could not verify, known gaps. Never claim pixel accuracy you did not measure.
`

const AREAS = {
  web: {
    name: 'web dashboard',
    build: `
YOUR AREA: the web dashboard served by the backend: everything under /home/user/MobileLab/backend/public/ (delete the placeholder index.html and the old assets/app.css: they belong to an earlier abandoned generic design; keep assets/fonts/), plus one new test file backend/tests/integration/dashboard.integration.test.ts. Ports 4300-4349. You may make a backend change ONLY if the UI truly cannot work without it; it must be additive, keep "cd backend && npm run check && npm test" green, and be listed in your report.

BUILD the dashboard exactly as the spec describes (sections 1 to 11), as an Xcode-style three-pane app filling the viewport: toolbar with navigator toggle, Stop, Run, title, the capsule (scheme popup, destination popup with multi-select and on-demand entries from /catalog, status area with the exact status strings of spec section 5), debug-area and inspector toggles; six navigators with icon tab bar (blue selected circle), real ARIA trees with full keyboard support, filter bars; jump bar with breadcrumb popups; editors: Welcome, Simulator preview (iPhone-shaped canvas with live screenshot refresh), Report with Summary | Tests | Logs (Logs = source-editor look: gutter line numbers, log syntax colours, current-line highlight, red inline pill for failures, pass/fail diamonds in the gutter, follow-with-jump-to-end, windowed rendering for big logs), Run report, Settings sheet (General, Environment/doctor, Storage/cleanup); inspector with Attributes, History, Quick Help; debug area with debug bar, variables view, green console with filter and popups; draggable dividers with persisted sizes; collapse toggles; Open Quickly (fuzzy) palette; keyboard shortcuts sheet; toasts; auth token sheet on 401; offline handling with reconnect; light and dark following prefers-color-scheme plus an override in Settings; narrow layout (spec section 1, below 900px single pane with bottom tab bar). Add the create-simulator sheet, scheme editor sheet, and context menus (right click and keyboard) for devices and jobs.
Fonts: @font-face for Inter (backend/public/assets/fonts/InterVariable.woff2, weight range 100 900) and JetBrains Mono (Regular/Medium/Bold woff2). Body font-family must start with "Inter". Icons: inline SVG set drawn by you in the SF Symbols style (24px viewBox, stroke about 1.6, round caps), names per spec section 4; no icon fonts, no emoji, no external requests.
Technical constraints: static files only, ES modules, no build step, no CDN, no third-party runtime dependencies. The backend sends a strict CSP (see backend/src/api/server.ts: script-src 'self'; style-src 'self'; default-src 'self'; connect-src 'self' ws: wss:; img-src 'self' data: blob:), so: no inline <style>, no style="" attributes in markup, no inline event handlers, no eval. Set dynamic sizes through CSSOM (element.style.setProperty) or SVG attributes or classes. Use event delegation. Render efficiently: skip DOM updates when the produced HTML is unchanged, preserve focus and scroll across updates (restore focus by a stable key), update relative times and elapsed timers in place, keep frame times low with hundreds of jobs and 50k-line logs (virtualise the log).
Data: REST + WebSocket exactly as in spec section 12 (use /ws/events?replay=100 for lifecycle events, and a second socket with jobId and output=1 for the open job; poll only as a fallback). Handle every API error by showing the server's message near the thing that failed.
VERIFY as you build, with a real browser: Chromium is installed (executable /opt/pw-browsers/chromium-1194/chrome-linux/chrome, or chrome-headless-shell under /opt/pw-browsers/chromium_headless_shell-1194). Install playwright-core in ${SCRATCH}/web/ (npm i playwright-core; do NOT run "playwright install") and drive it with executablePath. Seed realistic data through the API (several simulators across runtimes, passing / failing / flaky runs, a matrix run, a cancelled slow run). Take screenshots at 1477x959 and 2000x1111 (DPR 1) in light and dark, 900x700 and 390x844, of: welcome, devices tree + simulator preview, tests navigator with expanded job, issues, debug navigator, reports, failing report Logs tab with the inline pill, Summary tab, debug area with variables + console, toolbar states (idle, running, passed, failed, disconnected), settings sheet, quick open. Compare each against the references with the Read tool and iterate at least three full rounds until proportions, density, colours, capsule, tab bar, selection, gutter, execution line, debug bar and console tint are convincingly Xcode. Keep a script that regenerates all screenshots. Also test flows end to end: create simulator; run passing scheme; run failing scheme and inspect the Issues + Logs; run flaky with retries; matrix run; stop a slow run; boot/shutdown/delete from context menu; backend stop and restart (Disconnected then recovery); token auth (backend started with IOSLAB_API_TOKEN); keyboard-only operation; no console errors or CSP violations; Inter really renders (document.fonts.check and getComputedStyle, and confirm no fallback font is used). Fix everything you find.
Write backend/tests/integration/dashboard.integration.test.ts: assert that GET / serves the shell with the CSP header, every script/style/font/image URL referenced by index.html and every relative ES module import in assets/js resolves to an existing file served with the right content type, there is no inline style/script and no style= attribute in index.html or in any string template that builds markup (grep-style check), the fonts are served, and Inter is declared first in the font stacks. Run cd backend && npm run check && npm test at the end.
FINAL REPORT (returned as your result): summary, list of files, exact verification performed (with screenshot paths under ${SCRATCH}/web/shots/), deviations from the spec and why, known gaps.`,
  },
  mac: {
    name: 'macOS app',
    build: `
YOUR AREA: /home/user/MobileLab/macos-app/ (SwiftUI app, Xcode project, SwiftPM package with the IOSLabDashboardCore target and tests) and you may add font references for /home/user/MobileLab/shared/fonts (read only; do not modify those files). You can NOT run Xcode or SwiftUI here (Linux). No ports needed except your own test servers (4350-4399).

PROBLEM: the current app is an Xcode-clone made of about 3000 lines of SwiftUI full of fake data (Int.random CPU numbers, hard-coded 98.4 health score, fake storage 124 GB, fake LLDB/SourceKit/Playground views, a Stop button that only logs, "0 Errors" hard-coded, 8pt fonts). Replace it with a real, small, native macOS app that matches the spec: the Xcode layout with the six navigators, editor area (Welcome, Simulator preview, Report with Summary/Tests/Logs), inspector, debug area (variables view + console), toolbar with Stop, Run and the capsule (scheme popup, destination popup, status text per the spec table), jump bar, dividers, light and dark. Use real SF Symbols (Image(systemName:)) for icons: they are allowed on macOS (icons, not the font). TEXT MUST USE INTER (bundled TTFs from /home/user/MobileLab/shared/fonts, registered at launch via CTFontManagerRegisterFontsForURLs from the app bundle, exposed through one AppFont helper; JetBrains Mono for code/log/console). No use of .system fonts or SF Mono anywhere for text.
ARCHITECTURE (this is what makes it testable without a Mac): put every piece of logic that does not need SwiftUI/AppKit/Combine into the SwiftPM target Sources/IOSLabDashboardCore so it compiles and is unit tested on Linux with "swift test": Codable models matching the backend API exactly (read backend/src/simulator/models/types.ts, backend/src/api/routes/*.ts and the responses they return: devices with capacity, jobs with summary, runs, results with cases, capabilities, catalog, doctor, metrics summary incl. process, events), an API client over an injectable transport protocol (real one uses URLSession; on Linux use FoundationNetworking behind #if canImport), token support, error mapping to the server's message, an event-stream client abstraction with reconnect/backoff (real one uses URLSessionWebSocketTask behind a protocol so it can be faked), the reducer that applies events to state, the toolbar status text mapping from the spec table, tree builders for the Devices and Tests navigators, the log line classifier (pass/fail/keyword/error lines, test-case lines -> diamonds, failure lines -> inline pill text), formatting helpers (durations, relative dates, bytes), the shortcut table, and the persisted UI-state model. The SwiftUI target (macos-app/IOSLabDashboard/) contains only views, ObservableObject wrappers around Core (ObservableObject/Combine is fine there), font registration, window/toolbar/menus/commands (Run Cmd+R, Stop Cmd+., toggles, Open Quickly, Settings), and BackendRuntime (launch/stop the bundled backend; keep it working; avoid port clashes; surface failures in the UI). Delete every fake-data view and file; remove them from the Xcode project too.
XCODE PROJECT: macos-app/IOSLabDashboard.xcodeproj/project.pbxproj is hand-maintained. Do NOT hand-edit blindly: use the python package pbxproj (pip install pbxproj) or a careful scripted edit, then validate: every referenced object id exists and is unique, every file path exists on disk, sources build phases contain all Swift files, the font TTFs are in the Resources build phase (or Info.plist ATSApplicationFontsPath if you prefer that mechanism), the Core sources are compiled into the app target as before (see how the current project includes Sources/IOSLabDashboardCore) and the test target still references real files. Keep the deployment target as it is in the project. Keep .github/workflows/release.yml working: it archives scheme IOSLabDashboard unsigned.
VERIFY: try to get a Swift toolchain on this Linux box (the host download.swift.org is reachable; e.g. Swift 6.x for Ubuntu 24.04 x86_64, extract it under ${SCRATCH}/mac/ and put it on PATH; time-box the attempt to about 20 minutes; if it fails say so). With it: swift build and swift test for the Core package must pass (write thorough Core tests: decoding real backend JSON captured from a running demo backend, reducers, status strings, tree builders, log classifier, formatters, API client against a fake transport). For the SwiftUI files (cannot be compiled here) run swiftc -parse on every file to catch syntax errors, and review each file line by line for API availability against the deployment target, main-actor isolation, retain cycles in the WebSocket loop, and SwiftUI misuse; write down in your report exactly which parts are compile-verified and which are only parse-verified. If you cannot get a toolchain, say so plainly and compensate with extra-careful review.
Also update macos-app/README.md (how to build, what the app is, fonts, what is verified). FINAL REPORT (returned as your result): summary, files added/removed, exact verification performed, what is compile-verified vs parse-only vs unverified, known gaps.`,
  },
  linux: {
    name: 'Linux app',
    build: `
YOUR AREA: /home/user/MobileLab/linux-app/mobilelab-android/ (Qt 6 Widgets C++ app, CMake). Fonts are read-only in /home/user/MobileLab/shared/fonts. No backend ports needed except the app's own REST server (see below).

CONTEXT: this Linux app is the Android device lab (AVDs via the Android SDK, emulator, Waydroid, hybrid x86_64 + ARM64 matrix execution). It has its own core (src/core, src/runtime, src/waydroid, src/arm64) and does NOT talk to the Node backend (iOS Simulators exist only on macOS). Read src/MainWindow.cpp/h first: it is a dark generic "cards" dashboard with emoji-like glyph labels, one 89-line file with everything crammed on long lines, and it starts its REST server on port 4000, which collides with the Node backend's default port.
TASK: redesign the UI to look nearly like Xcode following the spec (spec section 12 last paragraph maps the anatomy onto Android data): toolbar with navigator toggle, Stop, Run (runs the matrix), title, capsule (scheme "Android Matrix" popup, destination popup listing real targets, status text using the spec's strings: Ready | N targets, Running..., Tests Passed / Tests Failed with real counts from MatrixExecutor results, KVM/emulator problems as honest status), debug-area and inspector toggles; six navigators (Devices = targets grouped by API level then ABI with status dots, Tests = matrix runs and their per-target results, Issues = failed targets and probe problems (missing emulator, no KVM, no system images) with remedies, Find, Debug = scheduler/host resources with real numbers (CPU/memory/KVM/emulator state, scheduler slots) drawn as Xcode gauges, Reports = run artifacts); jump bar with breadcrumb; editor area with Welcome, Target detail/preview (screenshot if runtime_->screenshot works, honest placeholder otherwise), Run report with Summary/Tests/Logs; inspector with Attributes/History/Quick Help; debug area with variables view and console (the existing runtime/scheduler/matrix log signals feed the console). Use Inter and JetBrains Mono from shared/fonts embedded with a Qt resource file (qt_add_resources; alias names; fall back gracefully if loading fails but log it), QSS light and dark that follow the desktop colour scheme (QStyleHints::colorScheme on Qt >= 6.5, palette luminance otherwise) using the exact tokens in the spec, icons drawn by you as SVG in the SF-Symbols-like style (embedded in the qrc, tinted at runtime), no emoji or Unicode pseudo-icons. Split the code into sensible files (Toolbar/Capsule, Navigator + tabs, tree models, editors, Inspector, DebugArea, Theme, Icons) instead of one file; update CMakeLists.txt and keep all existing functionality (AVD wizard, start/stop, ABI shell probe, VS Code launcher, matrix execution, API server, live console). Fix real issues you meet on the way: make the REST port configurable (env MOBILELAB_ANDROID_API_PORT, default 4100 so it no longer collides with the Node backend), remove fake constants, and make sure timers/connections do not leak.
BUILD AND VERIFY FOR REAL: install what you need with apt-get (qt6-base-dev, qt6-base-dev-tools, cmake, build-essential, libgl1-mesa-dev, pkg-config; Ubuntu 24.04 ships Qt 6.4.2; write code that also builds on newer Qt 6). Build out of tree in ${SCRATCH}/linux/build. Add a small, legitimate screenshot mode to the app (for example env MOBILELAB_SCREENSHOT_DIR: after the window is shown, grab each editor/navigator state to PNG files and exit) and run it with QT_QPA_PLATFORM=offscreen. Because this container has no Android SDK, create a test fixture that makes the real discovery code find targets: fake "emulator", "avdmanager" and "adb" shell scripts plus AVD ini/config files in a temp ANDROID_HOME (put the fixture under linux-app/mobilelab-android/tests/fixtures/ and use it from a ctest test); the product code must not contain fake data. Compare screenshots (1477x959 and 2000x1111, light and dark) with the references using the Read tool and iterate at least three rounds until layout, proportions, density, colours, capsule, tab bar, selection, gutter/log look, debug area and console tint match the spec closely. Also run the existing ctest (tests/arm64-smoke.sh) and your new tests. Update linux-app/mobilelab-android/README.md (build, fonts, the port change, screenshot mode, what is verified).
FINAL REPORT (returned as your result): summary, files, exact verification performed (screenshot paths under ${SCRATCH}/linux/shots/), deviations from the spec and why, known gaps.`,
  },
}

const LENSES = {
  fidelity: (a) => `LENS: visual and structural fidelity to Xcode (spec section 13 checklist). Actually run the ${a.name} where the platform allows and capture screenshots at 1477x959 and 2000x1111 in light and dark; view them next to ${REF1} and ${REF2} with the Read tool. List every visible deviation: proportions, row height, spacing, capsule shape/shadow/content, navigator tab bar and selected state, selection colour, folder/glyph colours, jump bar, gutter, execution line, inline pill, debug bar, variables badges, console tint, filter bars, dark mode, fonts (is Inter really what renders?). For the macOS app you cannot render: review the SwiftUI code against the spec numbers and say what you could not check.`,
  correctness: (a) => `LENS: functional correctness and code quality. Read the ${a.name} code line by line and exercise it. Hunt for real bugs: wrong data mapping against the backend (check against backend/src/api/routes and types.ts and real responses), state that goes stale, race conditions, leaks (timers, sockets, observers), unhandled errors, focus/selection bugs, broken shortcuts, layout that breaks on resize, performance traps with many jobs or huge logs, fake or hard-coded data, dead code, security issues (XSS via innerHTML of server strings, token leakage in URLs or logs, unsafe path handling), and tests that do not test what they claim. Run the test suites and the app. Prove each finding with a concrete reproduction or a precise code citation.`,
  ux: (a) => `LENS: ergonomics, accessibility and states. Evaluate the ${a.name} as a daily-use tool: keyboard-only operation (tab order, tree navigation, shortcuts from spec section 11, no traps, visible focus), screen-reader semantics (roles, labels, live regions for status changes), contrast in both appearances, hit-target sizes, empty/loading/error/offline/auth states from spec section 10 (do they exist, are they honest and actionable), narrow or small windows, reduced motion, first-run experience with an empty backend, the demo-mode disclosure, and whether the most frequent tasks (run tests, see why they failed, re-run, cancel, create/delete a simulator) take the fewest possible steps. Try them for real where the platform allows.`,
}

const FINDINGS = {
  type: 'object',
  properties: {
    summary: { type: 'string' },
    findings: {
      type: 'array',
      items: {
        type: 'object',
        properties: {
          severity: { type: 'string', enum: ['blocker', 'major', 'minor'] },
          area: { type: 'string' },
          file: { type: 'string' },
          problem: { type: 'string' },
          evidence: { type: 'string' },
          fix: { type: 'string' },
        },
        required: ['severity', 'problem', 'evidence', 'fix'],
      },
    },
  },
  required: ['summary', 'findings'],
}

const VERDICT = {
  type: 'object',
  properties: { real: { type: 'boolean' }, reason: { type: 'string' } },
  required: ['real', 'reason'],
}

const REPORT = {
  type: 'object',
  properties: {
    summary: { type: 'string' },
    filesChanged: { type: 'array', items: { type: 'string' } },
    verification: { type: 'array', items: { type: 'string' } },
    screenshots: { type: 'array', items: { type: 'string' } },
    knownGaps: { type: 'array', items: { type: 'string' } },
  },
  required: ['summary', 'verification', 'knownGaps'],
}

const UIS = ['web', 'mac', 'linux']

const results = await pipeline(
  UIS,
  // 1. build
  (key) => agent(COMMON + AREAS[key].build, { label: 'build:' + key, phase: 'Build', schema: REPORT }),
  // 2. review with three lenses, then adversarially verify every finding
  async (build, key) => {
    const a = AREAS[key]
    const reviews = await parallel(
      Object.entries(LENSES).map(([lens, make]) => () =>
        agent(
          COMMON +
            '\nYOUR AREA: read-only review of the ' + a.name + ' (' + key + '). Do NOT modify any product file. You may create scratch files under ' + SCRATCH + '/review-' + key + '-' + lens + '/ and run the app or tests.\n' +
            'The builder reported:\n' + JSON.stringify(build, null, 2) + '\n\n' +
            make(a) +
            '\nReport only problems you have evidence for. Rank by severity (blocker: broken or badly wrong, major: clearly deviates from the spec or loses correctness/usability, minor: polish). Give a concrete fix for each.',
          { label: 'review:' + key + ':' + lens, phase: 'Review', schema: FINDINGS, effort: 'high' }
        )
      )
    )
    const all = reviews.filter(Boolean).flatMap((r) => r.findings.map((f) => ({ ...f })))
    // collapse exact-duplicate problems reported by several lenses
    const seen = new Set()
    const unique = all.filter((f) => {
      const k = (f.file || '') + '|' + f.problem.slice(0, 80).toLowerCase()
      if (seen.has(k)) return false
      seen.add(k)
      return true
    })
    const cap = 24
    const bySeverity = { blocker: 0, major: 1, minor: 2 }
    unique.sort((x, y) => bySeverity[x.severity] - bySeverity[y.severity])
    if (unique.length > cap) log(key + ': ' + (unique.length - cap) + ' lowest-severity findings were not verified (cap ' + cap + ')')
    const toCheck = unique.slice(0, cap)
    const verdicts = await parallel(
      toCheck.map((f) => () =>
        agent(
          'You are a skeptical verifier for the ' + AREAS[key].name + ' in /home/user/MobileLab. A reviewer claims this defect:\n' + JSON.stringify(f, null, 2) +
            '\nTry to REFUTE it: read the cited code, reproduce it (run the app, tests or a small script where possible; never use pkill -f/killall; do not modify product files or run git write commands), and check whether the spec at /home/user/MobileLab/docs/design/xcode-interface.md really requires what the reviewer expects. Default to real=false if you cannot confirm it.',
          { label: 'verify:' + key, phase: 'Verify', schema: VERDICT }
        ).then((v) => ({ finding: f, verdict: v }))
      )
    )
    const confirmed = verdicts.filter((x) => x && x.verdict && x.verdict.real).map((x) => x.finding)
    const rejected = verdicts.filter((x) => x && x.verdict && !x.verdict.real).length
    log(key + ': ' + confirmed.length + ' confirmed, ' + rejected + ' refuted, ' + (unique.length - toCheck.length) + ' unverified')
    return { build, confirmed, rejected }
  },
  // 3. fix confirmed findings
  async (state, key) => {
    if (!state.confirmed.length) return { ...state, fix: { summary: 'nothing confirmed, nothing to fix' } }
    const fix = await agent(
      COMMON + AREAS[key].build.split('BUILD')[0] +
        '\nYou are now the FIXER for the ' + AREAS[key].name + '. The builder finished; independent reviewers found the defects below and skeptical verifiers confirmed them. Fix every one of them properly in your area (root causes, not patches), keep everything else working, and re-run the relevant verification (tests, screenshots, builds) afterwards. If you disagree with a finding after investigating, say why in the report instead of ignoring it.\nCONFIRMED FINDINGS:\n' +
        JSON.stringify(state.confirmed, null, 2) +
        '\nBuilder report for context:\n' + JSON.stringify(state.build, null, 2),
      { label: 'fix:' + key, phase: 'Fix', schema: REPORT }
    )
    return { ...state, fix }
  },
  // 4. final independent check
  async (state, key) => {
    const final = await agent(
      COMMON +
        '\nYOUR AREA: final acceptance check of the ' + AREAS[key].name + ' (read-only except scratch files; do not commit). Re-run every automated check for this area (web: cd backend && npm run check && npm test, plus your own Playwright run and fresh screenshots; macOS: swift build/test of the Core package if a toolchain exists under ' + SCRATCH + '/mac or elsewhere, otherwise swiftc -parse of every Swift file and the pbxproj id/path validation; Linux: cmake build in ' + SCRATCH + '/linux/build, ctest, and the offscreen screenshot mode). Then compare against the references once more and confirm that: Inter is the font actually used; no fake data remains (grep for random, hard-coded percentages, placeholder rows); demo mode is disclosed; the spec fidelity checklist passes. Return an honest verdict.\nPrevious reports:\n' + JSON.stringify({ build: state.build, fix: state.fix }, null, 2),
      {
        label: 'final:' + key,
        phase: 'Final',
        effort: 'high',
        schema: {
          type: 'object',
          properties: {
            verdict: { type: 'string', enum: ['ready', 'ready-with-gaps', 'not-ready'] },
            summary: { type: 'string' },
            checksRun: { type: 'array', items: { type: 'string' } },
            remainingIssues: { type: 'array', items: { type: 'string' } },
            screenshots: { type: 'array', items: { type: 'string' } },
          },
          required: ['verdict', 'summary', 'checksRun', 'remainingIssues'],
        },
      }
    )
    return { ...state, final }
  }
)

return UIS.map((key, i) => ({ ui: key, result: results[i] }))
