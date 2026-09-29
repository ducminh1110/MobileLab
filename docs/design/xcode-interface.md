# MobileLab interface: the Xcode layout

MobileLab is a tool for people who live in Xcode, so every MobileLab interface (the web dashboard served by
the backend, the macOS app, and the Linux app) uses the same window anatomy, vocabulary and keyboard model as
Xcode. This document is the single source of truth for that design. If an implementation and this document
disagree, fix one of them in the same change.

## 0. Ground rules

* **Look like Xcode, do not pretend to be Xcode.** Copy the layout, proportions, density, iconography style
  and interaction model. Do not use Apple's logos, the Xcode app icon, or the words "Xcode" as a product
  name in the UI chrome (mentioning it in text, for example "xcodebuild", is fine). The MobileLab mark is a
  plain rounded square in the accent colour with a phone glyph.
* **Font: Inter for all UI text, JetBrains Mono for code, logs, identifiers and numbers in tables.**
  San Francisco and SF Mono are not used anywhere (they cannot be used off Apple platforms, and the owner
  asked for Inter). Files: `shared/fonts/*.ttf` (native apps) and `backend/public/assets/fonts/*.woff2` (web).
  SF Symbols *are* allowed in the macOS app (they are icons, not the font) because they are what makes it
  look native; web and Linux use the custom SVG icon set described in section 4.
* **Real data only.** Every number, name, status and list in the interface comes from the backend (or, for
  the Linux app, from its own Android core). No decorative fake metrics, no hard-coded "PASSED" rows. When
  there is nothing to show, show an honest empty state (section 10).
* **Demo mode is loud.** When the backend reports `mode: "demo"` the toolbar capsule shows a "Demo" tag and
  the Welcome editor explains that nothing real runs on this host.

Reference screenshots (Xcode 26, light appearance) that the implementations are compared against are kept
outside the repository (they are Apple's images). Fidelity reviews use the same two regions everywhere:
*navigator + editor + canvas* and *navigator (debug) + editor + debug area with variables and console*.

## 1. Window anatomy

```
+--------------------------------------------------------------------------------------------------+
| toolbar                                                                                     52px |
| [sidebar] [stop][run]  MobileLab   ( scheme > destination      status | detail )   [debug][insp.] |
+----------------------+---------------------------------------------------+-----------------------+
| navigator            | jump bar                                     28px | inspector             |
| 300px (220 to 460)   +---------------------------------------------------+ 300px (240 to 460)    |
|                      | editor area                                       |                       |
| tab bar 8 icons 44px |                                                   | tab bar 32px          |
| tree / list          |                                                   | sections              |
|                      +---------------------------------------------------+                       |
|                      | debug bar                                    28px |                       |
| filter bar 34px      | variables view (50%) | console (50%)   240 (120+) |                       |
|                      | filter bar 32px      | filter bar 32px            |                       |
+----------------------+---------------------------------------------------+-----------------------+
```

* Three vertical regions; the navigator and inspector collapse independently, the debug area collapses
  under the editor. Dividers are 1px and draggable (hit area 7px), sizes persist per user.
* Density matches Xcode at 1x: navigator rows **22px**, editor gutter rows **17px** (12px mono), inspector rows
  **22px**, toolbar controls **28px** high, capsule **34px** high, jump bar **28px**.
* Minimum window: 1000 x 640. Below 900px wide the web dashboard switches to a single pane: the navigator
  becomes a full-width screen with the tab bar at the bottom, the editor opens on selection with a back
  button, and the inspector and debug area open as sheets.

## 2. Type scale (Inter 4, `font-feature-settings: "cv11", "ss03"` off; tabular numbers on)

| Use | Size / line | Weight |
| --- | --- | --- |
| Navigator rows, inspector values, buttons, menu items | 13 / 22 | 400 (selected 400) |
| Toolbar title | 13 / 16 | 700 |
| Capsule scheme + destination | 12 / 16 | 500 |
| Capsule status (`Tests Passed`) | 12 / 16 | 600, then ` \| detail` at 400 |
| Jump bar breadcrumbs | 11.5 / 16 | 400, colour secondary |
| Inspector section headers | 11 / 16 | 700 |
| Table headers, small labels | 11 / 14 | 600 |
| Code, logs, console, variables view values | JetBrains Mono 12 / 17 | 400 (function names 500) |

## 3. Colour tokens

Values were sampled from the reference screenshots and Xcode's default themes; keep them as CSS variables /
`Color` assets / QSS constants named exactly like this.

| Token | Light | Dark |
| --- | --- | --- |
| `window` (behind everything, toolbar) | `#f5f5f7` | `#2a2a2d` |
| `sidebar` (navigator, inspector) | `#f3f5f7` | `#262629` |
| `editor` | `#ffffff` | `#1f1f24` |
| `gutter` | `#fbfbfc` | `#1f1f24` |
| `gutter-text` | `#b4b6ba` | `#5f6068` |
| `divider` | `#e4e4e8` | `#3a3a3f` |
| `text` | `#1d1d1f` | `#ececee` |
| `text-secondary` | `#6c6c72` | `#a0a0a8` |
| `text-tertiary` (placeholders, disabled) | `#a1a1a8` | `#6f6f78` |
| `accent` | `#0a7aff` | `#0a84ff` |
| `accent-text` (on accent) | `#ffffff` | `#ffffff` |
| `selection` (navigator row, unfocused) | `#dcdde1` | `#3c3c42` |
| `selection-focused` | `accent` | `accent` |
| `tab-selected` (navigator tab pill) | `accent` filled circle, white glyph | same |
| `capsule` | `#ffffff`, shadow `0 0.5px 3px rgb(0 0 0 / 16%)` | `#3b3b3f`, shadow `0 0.5px 3px rgb(0 0 0 / 50%)` |
| `folder` (runtime / group folders) | `#5cb0f5` | `#5cb0f5` |
| `field` (filter fields, inputs) | `#efeff1` | `#333338` |
| `debug-bar` | `#f2f2f4` | `#2c2c30` |
| `console` | `#eaf1e8` | `#1d2a22` |
| `console-footer` | `#dbe7d9` | `#17211b` |
| `line-current` (running / execution line) | `#e5f7e8` | `#26372c` |
| `line-error` | `#fdeceb` | `#3a2426` |
| `pass` | `#30b356` | `#32d15b` |
| `fail` | `#ff3b30` | `#ff453a` |
| `warn` | `#ff9f0a` | `#ffb340` |
| `running` | `accent` | `accent` |
| `breakpoint` (gutter marker) | `#097dfe` | `#0a84ff` |

Log / editor syntax (the "editor" shows xcodebuild output and JSON, so these map to log tokens):
`comment` `#5d6c79` / `#6c7986`, `keyword` (`Test Suite`, `Test Case`, `Executed`) `#ad3da4` / `#fc5fa3`,
`string` (quoted names) `#d12f1b` / `#fc6a5d`, `number` (durations, counts) `#272ad8` / `#d0bf69`,
`type` (class names such as `LoginTests`) `#703daa` / `#d0a8ff`, `identifier` (methods, paths) `#326d74` / `#67b7a4`,
`error` `#ff3b30` / `#ff453a`, `success` (`** TEST SUCCEEDED **`, `passed`) `pass`.

Both appearances ship; follow the system setting (`prefers-color-scheme`, `NSApp.effectiveAppearance`,
`QPalette` luminance) and offer an override in Settings (System / Light / Dark).

## 4. Iconography

Xcode uses SF Symbols: 1.5px optical strokes, rounded caps and joins, 16px grid, monochrome (secondary
colour) except where colour carries meaning. macOS uses the real SF Symbols. Web and Linux draw an
equivalent set as inline SVG (24px viewBox, stroke 1.6, `currentColor`) with these names:

`sidebar.left`, `sidebar.right`, `sidebar.bottom` (panel toggles), `play.fill`, `stop.fill`, `plus`, `xmark`,
`chevron.right`, `chevron.down` (disclosure and breadcrumb separators), `chevron.up.chevron.down` (popup
arrows), `folder.fill`, `iphone`, `ipad`, `cpu`, `memorychip`, `internaldrive`, `network`,
`magnifyingglass`, `exclamationmark.triangle`, `diamond` (test), `checkmark.diamond.fill` /
`xmark.diamond.fill` (pass / fail), `doc.text` (report), `gearshape`, `trash`, `camera`, `arrow.clockwise`,
`line.3.horizontal.decrease.circle` (filter), `ellipsis.circle`, `bolt.horizontal` (backend), `info.circle`.

Navigator tab icons are 16px glyphs centred in 28px hit targets; the selected tab is an accent-filled circle
with a white glyph (see reference 2). Status is always glyph *and* colour, never colour alone.

## 5. Toolbar

Left to right: **navigator toggle** (`sidebar.left`), **Stop** (`stop.fill`, disabled unless something is
running or queued), **Run** (`play.fill`, runs the selected scheme on the selected destination(s)), the
**title** "MobileLab" with the mark, then the centred **capsule**, then **debug-area toggle**
(`sidebar.bottom`) and **inspector toggle** (`sidebar.right`) at the far right.

The capsule is one 34px pill, white on light, with three regions separated by chevrons and a divider:

1. **Scheme** (mark + name): a popup of recent schemes (from job history) plus *New Scheme…* and
   *Edit Scheme…*. Editing a scheme sets: project or workspace path, working directory, configuration,
   only-testing filters, retries, max parallel, auto-provision. Schemes are stored client-side.
2. **Destination** (device glyph + name): a popup listing booted simulators, then "Create on demand" entries
   built from `/catalog` (runtime x device type), then *Manage Devices…* (opens the Devices navigator). Several
   destinations can be ticked; more than one runtime or device type becomes a matrix run (`POST /runs`).
3. **Status** (right aligned inside the capsule): `**State** | detail`, always describing the most recent job
   of the selected scheme (or the running one):

| Situation | Text |
| --- | --- |
| No job yet | `Ready` `\|` `N simulators booted` (or `No simulators`) |
| queued | `Queued` `\|` `waitingReason` |
| running | spinner + `Running` `\|` `<scheme> on <device>, 00:07` (elapsed ticks every second) |
| retrying | `Retrying` `\|` `attempt 2 of 3` |
| completed | `Tests Passed` `\|` `Today at 9:41 AM` |
| failed, tests failed | `Tests Failed` `\|` `2 of 8 tests failed` |
| failed, `summary.buildFailed` | `Build Failed` `\|` first error |
| cancelled | `Cancelled` `\|` time |
| demo mode | a small `Demo` tag before the state |
| backend unreachable | `Disconnected` `\|` `Retrying…` (red glyph) |

Clicking the status area opens the corresponding report in the editor (Xcode behaviour). Toolbar buttons have
tooltips with their shortcut.

## 6. Navigator (left)

A tab bar of icons (44px high, 8px gap) selects one of six navigators. Each has a filter bar at the bottom
(`line.3.horizontal.decrease.circle` + text field + recent/failed toggle), like Xcode's. Rows are 22px,
16px icon, 6px gap, disclosure triangles for groups, indentation 14px per level, selected row is a rounded
(6px) rectangle in `selection` (or `selection-focused` with `accent-text` while the navigator has focus).
Trees are real trees: arrow keys move / expand / collapse, Return opens, Space previews.

| # | Tab (icon) | Content and data source |
| --- | --- | --- |
| 1 | **Devices** (`iphone`) | Tree like Xcode's project navigator. `Simulators` > one blue **folder per runtime** ("iOS 18.0") > devices (glyph + name + a coloured status dot: ready green, booting/shutting down amber pulsing, busy accent pulsing, stopped grey, error red). Auto-created devices show a small "auto" tag. If the VM backend is enabled a second root `Experimental VMs (simulated)` appears. Source: `GET /devices`. Row context menu: Boot, Shut Down, Screenshot, Run Tests Here, Delete. Footer `+` button creates a simulator (sheet: runtime, device type, name). |
| 2 | **Tests** (`diamond`) | Xcode's Test navigator. `Runs` > run (matrix) > job > suite (class) > test case; every row has the diamond glyph: grey outline (not run), `checkmark.diamond.fill` green, `xmark.diamond.fill` red, spinning ring while running. A hover play button on a job re-runs it. Source: `GET /runs`, `GET /tests`, `GET /tests/:id/results` (lazy, when a job row is expanded). |
| 3 | **Issues** (`exclamationmark.triangle`) | Failed tests (red) and build errors (`summary.errors`) and job errors, grouped by job, each row `message` with file:line when present. Source: same job data. Selecting an issue opens the job's log editor scrolled to the line. |
| 4 | **Find** (`magnifyingglass`) | Search field plus scope popup (Devices / Tests / Events / Log of the open job). Results grouped like Xcode's Find navigator with match highlighting. Log scope uses `GET /tests/:id/output`. |
| 5 | **Debug** (`cpu`) | Xcode's Debug navigator. Header row `MobileLab Backend  PID 4242` with `info` and `pause` glyphs; gauge rows CPU, Memory, Disk (artifact store), Capacity (load of max units) each with a thin usage bar under the value, exactly like reference 2; then `Running jobs` as "threads": `Thread 1  Queue: DemoApp`, its frames underneath (job, device, attempt) and queued jobs greyed. Source: `GET /metrics/summary` (poll 2s while visible) and `GET /devices`. |
| 6 | **Reports** (`doc.text`) | Xcode's Report navigator: finished jobs newest first grouped `Today` / `Yesterday` / date, each row status glyph, `<scheme> on <device>`, time and duration right aligned. Source: `GET /tests`. Selecting opens the report editor. |

## 7. Editor area

**Jump bar** (28px): `sidebar-grid` icon, back / forward chevrons (history of what was opened), then a
breadcrumb with the path to the thing shown, 11.5px, secondary colour, `chevron.right` separators, blue folder
and glyph icons for each crumb: `MobileLab > Simulators > iOS 18.0 > iPhone 15`, or `MobileLab > Reports >
DemoApp > Tests`. Each crumb is a popup that lists siblings. Far right: related-items and editor-options
icons (`arrow.left.arrow.right`, `list.bullet.indent`) and `+` (split editor is out of scope; the icons open
Settings and the inspector).

The editor shows one of:

1. **Welcome** (nothing selected). Centred column: MobileLab mark, `Welcome to MobileLab`, version, and three
   large rows with icon, title and one line each: *Run Tests…*, *Create Simulator…*, *Run Diagnostics…*;
   beside them a list of recent runs (real). In demo mode a notice explains what is simulated.
2. **Simulator preview** (a device selected), Xcode canvas style: the live screenshot inside an iPhone-shaped
   frame (rounded 44px corners, 6px bezel, dynamic island pill) on a slightly darker canvas, refreshed every
   3s while the device is running (`GET /devices/:id/screenshot`); below it the canvas toolbar (`play` boot /
   run tests here, `camera` screenshot, `arrow.clockwise`, zoom -/+ with fit). Stopped device: the frame is
   dimmed with a *Boot* button.
3. **Report** (a job selected), with a segmented control at the top: **Summary | Tests | Logs**.
   * *Summary*: title `Tests Passed` / `Tests Failed` with the diamond glyph, subtitle scheme, device,
     started time, duration, attempts; a row of counters (passed / failed / skipped) with a thin
     segmented bar; the failure list (red left border, name, message); build errors; artifacts list with
     download links and *Reveal xcresult* (copy path); actions *Run Again*, *Cancel*, *Export JUnit*.
   * *Tests*: table like Xcode's report: columns Name (class > method tree), Status diamond, Duration.
   * *Logs*: **the source-editor look**: gutter with right-aligned line numbers (12px mono, `gutter-text`),
     content in mono with log syntax colours, the current/last line highlighted with `line-current`, failure
     lines with `line-error` and a red **inline pill at the right edge** carrying the assertion message (the
     same shape as the green "Thread 1: SwiftUI Body" pill in reference 2), a `breakpoint`-blue marker in the
     gutter on lines the user clicked, and pass/fail diamonds in the gutter on `Test Case` lines. Live jobs
     stream lines (WebSocket, `output=1`) and auto-follow unless the user scrolls up (a `Jump to end` pill
     appears). Large logs render windowed (only visible lines in the DOM / model).
4. **Run report** (a matrix run selected): summary header plus a table device x status x tests x duration; a
   row opens its job report.
5. **Settings** (gear, or the menu): a sheet with panes *General* (appearance, API URL and token),
   *Environment* (the doctor checks with remedies, re-run), *Storage* (artifact size, clean up older than N
   days). Source: `GET /doctor`, `GET /capabilities`, `POST /maintenance/cleanup`.

## 8. Inspector (right)

A 32px tab bar with `doc.text` (Attributes), `clock` (History), `questionmark.circle` (Quick Help). Sections
have bold 11px headers with a disclosure triangle and 22px rows in two columns (label 84px right aligned in
secondary colour, value left aligned), like Xcode's file inspector.

* **Attributes** for a device: *Identity and Type* (Name, Runtime, Device type, Backend `simctl`/`simulated`),
  *Location* (Simulator UDID, with a Copy button), *Resources* (cost units, current job link), *Status*
  (state, last error). For a job: *Identity* (id, scheme, run), *Destination* (device, runtime), *Execution*
  (attempts, retries, exit code, duration, started, finished), *Result* (counts). For nothing selected: the
  backend (version, mode, host, capacity).
* **History**: the events for the selection (`GET /events?jobId=` or `deviceId=`), newest first, time + text.
* **Quick Help**: a plain-language explanation of the selected status and the next possible actions.

## 9. Debug area (bottom of the editor column)

* **Debug bar** (28px): hide button, `play` (re-run), `stop` (cancel), `arrow.clockwise` (retry failed),
  `camera` (screenshot), then the breadcrumb `MobileLab > <job or device> > <running test or state>` and, at
  the far right, `Attempt 1 of 2` and a panel toggle.
* **Variables view** (left half): a tree with 11.5px rows and mono values: the selected job's test cases
  grouped by class, each with `Test`/`Suite` badge squares (green `P`, red `F`, grey `S`) like the `A` / `L`
  badges in reference 2, and `= status: duration` values. Bottom bar: an `Auto` style popup (`All`,
  `Failures`, `Running`) and a filter field.
* **Console** (right half, `console` tint): the live event and output stream for the selected job, or the
  global activity log when nothing is selected, mono 12px, no line numbers, timestamps optional. Bottom bar:
  a popup (`All Output`, `Errors`, `Build`, `Tests`), filter field, `trash` (clear view), and the two
  layout toggles (variables only / console only). Errors are red, passes green, everything else `text`.
* Default height 240px (min 120), remembered, toggled with the toolbar button.

## 10. States

* **Loading**: skeleton rows in the navigator (three grey bars at 22px pitch), never a spinner-only page.
* **Empty**: every navigator and editor has a one-sentence explanation and one primary action, for example
  Devices: "No simulators yet" + *Create Simulator*; Tests: "No test runs yet" + *Run Tests*; Issues: "No
  issues" (positive, with a green diamond).
* **Error**: inline, next to what failed, in `fail` colour, with the server's message. A failed action also
  shows a transient toast; nothing fails silently.
* **Offline**: the capsule turns to `Disconnected`, navigators keep their last data greyed, and a reconnect
  loop with backoff runs; the moment the backend returns everything refreshes.
* **Auth**: on 401 a sheet asks for the API token (stored per user, never in the URL bar), then retries.

## 11. Interaction and keyboard

Xcode shortcuts are used wherever the platform allows. On the web the Command key becomes Ctrl on non-Mac
and a few shortcuts that browsers reserve are remapped; the in-app *Keyboard Shortcuts* sheet (`?`) is the
authoritative list per platform.

| Action | macOS / Linux app | Web |
| --- | --- | --- |
| Run / Stop | `Cmd R` / `Cmd .` (Linux `Ctrl R` / `Ctrl .`) | `Ctrl/Cmd Enter` / `Ctrl/Cmd .` |
| Toggle navigator / inspector / debug area | `Cmd 0` / `Cmd Opt 0` / `Cmd Shift Y` | `Ctrl/Cmd Shift 0` / `Ctrl/Cmd Alt 0` / `Ctrl/Cmd Shift Y` |
| Navigator tabs 1 to 6 | `Cmd 1` to `Cmd 6` | `Ctrl/Cmd Shift 1` to `6` |
| Open Quickly (fuzzy jump to any device, run, job or test) | `Cmd Shift O` | `Ctrl/Cmd Shift O` |
| New simulator | `Cmd N` | `Ctrl/Cmd Alt N` |
| Clear console | `Cmd K` | `Ctrl/Cmd K` |
| Settings | `Cmd ,` | `Ctrl/Cmd ,` |
| Find in navigator | `Cmd Shift F` | `Ctrl/Cmd Shift F` |

Every action is also reachable with the mouse and from a menu (menu bar on macOS and Linux, an
application-style menu button on the web). Focus rings are visible, tab order follows the layout, trees
implement the WAI-ARIA tree pattern, contrast meets WCAG AA in both appearances, and `prefers-reduced-motion`
disables the only animations used (pulse on running glyphs, sheet slide).

## 12. Data contract (what feeds each pane)

All UIs talk to the same REST/WebSocket API (`docs/api.md`): `/health`, `/capabilities`, `/catalog`,
`/doctor`, `/devices`, `/devices/:id/screenshot`, `/tests`, `/tests/:id/{results,output,junit,artifacts}`,
`/runs`, `/events`, `/ws/events`, `/metrics/summary`, `/maintenance/cleanup`. Refresh model: initial fetch,
then a WebSocket subscription (`/ws/events?replay=100`) whose non-output events trigger a debounced refetch of
devices, jobs and runs; 5s polling while the socket is down; the open job additionally subscribes with
`jobId` and `output=1`. Never poll faster than 1s.

The Linux app is different in one respect: it manages **Android** targets through its own core (AVDs,
emulator, Waydroid) and does not talk to the Node backend (iOS Simulators only exist on macOS). It maps the
same anatomy onto that data: Devices = targets grouped by API level and ABI, Tests = matrix runs, Issues =
failed targets and probe errors, Debug = scheduler and host resources, Reports = run artifacts. Its toolbar
capsule reads `Android Matrix > <target>`.

## 13. Fidelity checklist (used by reviewers)

1. Side by side with the references at the same size: toolbar height, capsule shape and shadow, navigator tab
   bar with the blue selected circle, 22px rows with blue folders, gray rounded selection, jump bar with
   chevron breadcrumbs, gutter numbers, green execution line with pill, debug bar, split variables /
   console with green console tint, filter bars.
2. Light and dark both correct (no black backgrounds, no pure white text).
3. Inter is what renders (verify in the running app: computed font, not a fallback) and no SF font names
   appear in the code.
4. Every pane shows real data and honest empty / error states; nothing is hard-coded.
5. Keyboard: full tree navigation, shortcuts from section 11, visible focus, no keyboard traps.
6. Narrow window (web: 390px and 900px) remains usable.
