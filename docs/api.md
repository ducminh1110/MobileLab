# MobileLab backend API

The backend is a Fastify server (`backend/`) that manages a pool of iOS simulators, queues and runs
`xcodebuild test` jobs on them, and streams what happens. Every interface (web dashboard, macOS app, CLI,
MCP clients) uses this API.

* Base URL: `http://127.0.0.1:4000` (`HOST`, `PORT`). It binds to loopback by default.
* Content type: JSON. Errors are `{ "error": "<code>", "message": "<human readable>" }` with a 4xx status for
  bad input (never 500), `404 not_found`, `409` for conflicts (busy device, finished job), `429` for capacity.
* Auth: optional. Set `IOSLAB_API_TOKEN` and send `Authorization: Bearer <token>` (or `?token=` for
  WebSocket, Server-Sent Events and downloads, which cannot set headers). `/health`, `/` and `/assets/*` stay
  open so the dashboard can load and ask for the token.
* Demo mode: off macOS the backend simulates `simctl`/`xcodebuild` (`mode: "demo"` in `/health` and
  `/capabilities`). Nothing real runs and results are not real. Force it with `IOSLAB_SIMULATOR_MOCK=true`.

## Concepts

| Thing | Meaning |
| --- | --- |
| Device | A simulator in the pool. `status`: `booting`, `ready`, `busy`, `shutting_down`, `stopped`, `error`. `backend` is `simctl` (real) or `simulated` (experimental VM, never runs tests). `ephemeral: true` means it was created automatically for a run and is removed when nothing needs it. |
| Job | One `xcodebuild test` execution of a scheme on one device. `status`: `queued`, `running`, `retrying`, `completed` (tests passed), `failed`, `cancelled`. |
| Run | A group of jobs created from a matrix of runtimes x device types, with an optional `maxParallel`. |
| Capacity | Every booted device costs units (simulator 1, VM 4). The limit defaults to `min(cores, RAM/2 GB)`; override with `IOSLAB_MAX_LOAD`. |

A queued job is matched to a ready device with the requested runtime/device type. If none exists and
`autoProvision` is true (default) the backend creates a simulator for it, and removes it afterwards.

Runtimes and device types accept identifiers (`com.apple.CoreSimulator.SimRuntime.iOS-18-0`) or shorthand
(`18.0`, `iOS 17.5`, `iPhone 15`); see `GET /catalog` for what the host offers. A device type the runtime cannot
host is a 400 that lists what it can; an omitted runtime is chosen to fit the device type, and an omitted
device type is the newest plain iPhone the runtime supports.

## System

| Method and path | Description |
| --- | --- |
| `GET /health` | `{ status, timestamp, version, mode, uptimeSeconds }` |
| `GET /capabilities` | host (`platform`, `architecture`, `kernel`, `node`, `supportedTargets`), `mode`, `vm`, `auth`, `workspaceRoot`, `dataDir`, `capacity` |
| `GET /catalog` | `{ runtimes: [{identifier, name, version, supportedDeviceTypes?}], deviceTypes: [{identifier, name, family}], source }`. `supportedDeviceTypes` lists the device type identifiers a runtime can host (a new iPhone does not exist on an old iOS); build destination pickers from it. |
| `GET /doctor` | Real environment checks: `{ status: healthy\|degraded\|unhealthy, mode, checks: [{id, name, status: ok\|warn\|fail\|skip, message, remedy?}] }` |
| `GET /metrics` | Prometheus text (`ioslab_*`) |
| `GET /metrics/summary` | `{ devices, jobs, queueDepth, running, jobsByStatus, capacity, artifactBytes, hostMemoryFreeGb, process: {pid, uptimeSeconds, rssBytes, cpuPercent} }` |
| `POST /maintenance/cleanup` | `{ days? }` deletes finished jobs older than `days` (default `IOSLAB_RETENTION_DAYS`, 14). Returns what was removed. |

## Devices

| Method and path | Description |
| --- | --- |
| `GET /devices` | `{ items: Device[], capacity }` |
| `GET /devices/:id` | One device |
| `POST /devices/spawn` | `{ name?, runtime?, modelId?, type?: simulator\|vm, cpu?, memory?, disk?, wait? }`. Waits until the device is ready (200) unless `wait: false` (202, poll or watch events). 429 when capacity is exhausted. |
| `POST /devices/boot`, `POST /devices/shutdown` | `{ id }`. Idempotent. 409 if the device is busy. |
| `POST /devices/:id/boot`, `POST /devices/:id/shutdown` | Same, REST style. |
| `DELETE /devices/:id` | Shuts down and deletes the simulator from the host. 204. |
| `POST /devices/sync` | Reconcile the pool with `simctl list` (simulators removed or stopped outside MobileLab). |
| `GET /devices/:id/screenshot` | `image/png` of the running simulator |

## Tests (jobs)

| Method and path | Description |
| --- | --- |
| `POST /tests/run` | `{ testTarget (or scheme), projectPath?, workspacePath?, workingDirectory?, configuration?, onlyTesting?, maxRetries?, requiredRuntime?, requiredModelId?, autoProvision?, wait?, timeoutSeconds? }`. Returns `202 { job, scheduled }` immediately, or with `wait: true` holds the request until the job finishes. |
| `GET /tests` | `{ items }` newest first. Filters `status`, `runId`, `limit`. |
| `GET /tests/:id` | Job: `status`, `attempts`, `retries`, `assignedDeviceId/Name`, `waitingReason`, `exitCode`, `error`, `summary {total, passed, failed, skipped, durationSeconds, buildFailed, errors}`, timestamps |
| `POST /tests/:id/cancel` | Cancels a queued or running job (kills xcodebuild). 409 if already finished. |
| `POST /tests/:id/rerun` | New job with the same parameters. |
| `GET /tests/:id/results` | Parsed test cases `{ attempt, cases: [{className, name, status, durationSeconds, message?}], summary }` |
| `GET /tests/:id/output?tail=N` | Tail of the raw xcodebuild output `{ text, truncated, sizeBytes, attempt }` (works while running) |
| `GET /tests/:id/junit` | JUnit XML |
| `GET /tests/:id/artifacts` | Logs, results JSON and the `.xcresult` bundle (a folder; open it on the host) |
| `GET /artifacts/:id/download` | Download a file artifact |
| `GET /tests/:id/logs` | Lifecycle events of the job (legacy; prefer `/events?jobId=`) |

Scheme names steer demo mode: containing `fail` gives failing tests, `flaky` fails the first attempt, `missing`
is a build error, `slow` runs long enough to cancel, anything else passes 8 of 8.

Failing tests fail the job (`failed`, exit code 65). Retries (`maxRetries`, default 0) re-run failed jobs
with exponential backoff, except build errors, which are deterministic.

## Runs (matrix)

| Method and path | Description |
| --- | --- |
| `POST /runs` | `{ scheme (or testTarget), runtimes?: string[], models?: string[], maxParallel?, maxRetries?, autoProvision?, name?, projectPath?, ... }` creates one job per runtime x model. Combinations that cannot exist are skipped and listed: `202 { run, jobs, skipped: [{runtime, model, reason}] }` (400 if none exist). |
| `GET /runs` | `{ items }` with `status` (`queued`, `running`, `passed`, `failed`, `cancelled`) and per-status `counts` |
| `GET /runs/:id` | `{ run, jobs }` |
| `POST /runs/:id/cancel` | Cancels every unfinished job of the run |

## Events

Lifecycle events look like `{ id, timestamp, source, type, action, message, jobId?, deviceId?, runId? }`.
Raw xcodebuild output lines are `action: "output"` events that are streamed but not stored.

| Method and path | Description |
| --- | --- |
| `GET /events` | History `{ items, lastId }`. Filters `jobId`, `deviceId`, `runId`, `sinceId`, `limit`. |
| `GET /events/stream` | Server-Sent Events (`event: engine`), resumes from `Last-Event-ID`. |
| `WS /ws/events` (alias `/ws/logs`) | Live events. Query: `jobId`, `deviceId`, `runId`, `output=1` (include raw build output), `replay=N` (send the last N matching events first), `token`. |

## Experimental VM backend (simulated)

`/vms`, `/vms/spawn`, `/vms/:id/{screenshot,input,backup,restore,switch,chaos,aging}`. Enabled in demo mode or
with `IOSLAB_EXPERIMENTAL_VM=1`; otherwise `501`. It models a VM lifecycle only: no VM is started and it
cannot run tests (`canRunTests: false`).

## MCP

`POST /mcp` speaks JSON-RPC 2.0 (MCP `2024-11-05`): `initialize`, `ping`, `tools/list`, `tools/call`; batches
and notifications are supported. Tools: `list_devices`, `spawn_device`, `boot_device`, `shutdown_device`,
`delete_device`, `run_test` (waits for the result by default), `get_job`, `list_jobs`, `get_job_output`,
`cancel_job`, `get_screenshot` (returns an image block), and, when the VM backend is on, `inject_input`,
`inject_chaos`, `simulate_device_aging`. Tool failures are returned as `isError` results so the model can
read the message.

## Configuration

| Variable | Default | Meaning |
| --- | --- | --- |
| `HOST`, `PORT` | `127.0.0.1`, `4000` | Listen address |
| `IOSLAB_API_TOKEN` | none | Require this bearer token |
| `IOSLAB_DATA_DIR` | `~/.mobilelab` | State (`state.json`) and artifacts |
| `IOSLAB_SIMULATOR_MOCK` | auto (`true` off macOS) | Simulate simctl/xcodebuild |
| `IOSLAB_EXPERIMENTAL_VM` | follows demo mode | Enable the simulated VM API |
| `IOSLAB_MAX_LOAD` | derived | Capacity units |
| `IOSLAB_TEST_TIMEOUT_MS` | `1800000` | Per-attempt xcodebuild timeout |
| `IOSLAB_RETENTION_DAYS` | `14` | Auto-clean finished jobs older than this at start |
| `IOSLAB_WORKSPACE_ROOT` | current directory | Where xcodebuild runs when a job names no project |
| `IOSLAB_WEBHOOK_URL` | none | POST a JSON summary (with a Slack-style `text`) when a job finishes |
| `IOSLAB_MOCK_LATENCY_MS` | `350` | Speed of demo-mode operations |
| `LOG_LEVEL` | `info` | pino level |

Invalid values stop the server at start with a message naming the variable.
