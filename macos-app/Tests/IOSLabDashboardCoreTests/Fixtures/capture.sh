#!/usr/bin/env bash
# Re-captures the JSON fixtures in this folder from a real MobileLab backend running in demo mode.
# The Core tests decode these files, so when the backend API changes, run this script and fix what breaks.
#
#   ./capture.sh [backend-dir] [port]        (defaults: ../../../../backend and 4360; needs node, npx, curl, python3)
#
# It starts two throwaway backends (one without and one with an API token), drives them through a fixed
# scenario (devices, passing / failing / build-error / flaky / slow jobs, a matrix run, cleanup) and writes
# what the API answered. Nothing here is hand written.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
BACKEND_DIR="$(cd "${1:-$HERE/../../../../backend}" && pwd)"
PORT="${2:-4360}"
PORT_TOKEN=$((PORT + 1))
TOKEN="secret-token"
DATA="$(mktemp -d)"
JSON='content-type: application/json'
B="http://127.0.0.1:$PORT"
T="http://127.0.0.1:$PORT_TOKEN"
PIDS=()

cleanup() {
  for pid in "${PIDS[@]:-}"; do [ -n "$pid" ] && kill -- "-$pid" 2>/dev/null || true; done
  rm -rf "$DATA"
}
trap cleanup EXIT

start() { # port, data dir, extra env...
  local port="$1" dir="$2"; shift 2
  # setsid makes the backend the leader of its own process group, so cleanup can kill npx, tsx and node together.
  env "$@" IOSLAB_DATA_DIR="$dir" PORT="$port" LOG_LEVEL=silent IOSLAB_SIMULATOR_MOCK=true \
    setsid bash -c 'cd "$0" && exec npx tsx src/index.ts' "$BACKEND_DIR" >/dev/null 2>&1 &
  PIDS+=("$!")
  for _ in $(seq 1 60); do curl -fs "http://127.0.0.1:$port/health" >/dev/null 2>&1 && return 0; sleep 0.5; done
  echo "backend on port $port did not start" >&2; exit 1
}

field() { python3 -c "import json,sys; d=json.load(open('$1')); print(eval(sys.argv[1]))" "$2"; }
wait_status() { # base url, job id, wanted status
  for _ in $(seq 1 200); do
    s=$(curl -s "$1/tests/$2" | python3 -c "import json,sys; print(json.load(sys.stdin).get('status'))")
    [ "$s" = "$3" ] && return 0; sleep 0.05
  done
  echo "job $2 never reached $3" >&2; return 1
}

mkdir -p "$DATA/a" "$DATA/b"
start "$PORT" "$DATA/a"
start "$PORT_TOKEN" "$DATA/b" IOSLAB_API_TOKEN="$TOKEN"

cd "$HERE"
curl -s "$B/health" >health.json
curl -s "$B/capabilities" >capabilities.json
curl -s "$B/catalog" >catalog.json
curl -s "$B/doctor" >doctor.json
curl -s "$B/devices" >devices-empty.json
curl -s "$B/tests" >tests-empty.json
curl -s "$B/runs" >runs-empty.json
curl -s "$B/metrics/summary" >metrics-idle.json

curl -s -X POST "$B/devices/spawn" -H "$JSON" -d '{"name":"iPhone 15 Test","runtime":"18.0","modelId":"iPhone 15"}' >device-spawn.json
curl -s "$B/devices" >devices.json

curl -s -X POST "$B/tests/run" -H "$JSON" -d '{"scheme":"DemoApp","wait":true}' >run-pass.json
curl -s -X POST "$B/tests/run" -H "$JSON" -d '{"scheme":"LoginFailTests","wait":true}' >run-fail.json
curl -s -X POST "$B/tests/run" -H "$JSON" -d '{"scheme":"MissingScheme","wait":true}' >run-missing.json
PASS=$(field run-pass.json "d['job']['id']"); FAIL=$(field run-fail.json "d['job']['id']"); MISS=$(field run-missing.json "d['job']['id']")
curl -s "$B/tests/$PASS/results" >results-pass.json
curl -s "$B/tests/$FAIL/results" >results-fail.json
curl -s "$B/tests/$MISS/results" >results-missing.json
curl -s "$B/tests/$PASS/output" >output-pass.json
curl -s "$B/tests/$FAIL/output" >output-fail.json
curl -s "$B/tests/$MISS/output" >output-missing.json
curl -s "$B/tests/$PASS/artifacts" >artifacts-pass.json
curl -s "$B/tests/$FAIL/junit" >junit-fail.xml
curl -s "$B/events?jobId=$FAIL" >events-job.json

# flaky job with retries: catch it in the "retrying" state
curl -s -X POST "$B/tests/run" -H "$JSON" -d '{"scheme":"FlakyTests","maxRetries":2}' >run-flaky-queued.json
FLAKY=$(field run-flaky-queued.json "d['job']['id']")
wait_status "$B" "$FLAKY" retrying && curl -s "$B/tests/$FLAKY" >job-retrying.json
wait_status "$B" "$FLAKY" completed && curl -s "$B/tests/$FLAKY" >job-flaky-done.json

# a slow job: capture it while running, then cancel it (a second cancel is a 409)
SLOWRUN=$(curl -s -X POST "$B/tests/run" -H "$JSON" -d '{"scheme":"SlowSuite"}')
echo "$SLOWRUN" >run-slow-queued.json
SLOW=$(field run-slow-queued.json "d['job']['id']")
wait_status "$B" "$SLOW" running
curl -s "$B/tests/$SLOW" >job-running.json
curl -s "$B/metrics/summary" >metrics-busy.json
curl -s "$B/devices" >devices-busy.json
sleep 1
curl -s "$B/tests/$SLOW/output" >output-running.json
curl -s -X POST "$B/tests/$SLOW/cancel" >job-cancelled.json
curl -s -X POST "$B/tests/$SLOW/cancel" >error-409.json

# a job that needs another runtime: the backend creates an ephemeral (auto) simulator for it
curl -s -X POST "$B/tests/run" -H "$JSON" -d '{"scheme":"SlowOne","requiredRuntime":"17.5"}' >run-slow-ephemeral.json
SLOW2=$(field run-slow-ephemeral.json "d['job']['id']")
wait_status "$B" "$SLOW2" running
curl -s "$B/devices" >devices-ephemeral.json
curl -s -X POST "$B/tests/$SLOW2/cancel" >/dev/null

# matrix run: 2 runtimes x 2 device types; the backend lists combinations the host cannot create in `skipped`
curl -s -X POST "$B/runs" -H "$JSON" -d '{"scheme":"DemoApp","name":"Matrix","runtimes":["18.0","17.5"],"models":["iPhone 15","iPad (10th generation)"],"maxParallel":2}' >run-create.json
RUN=$(field run-create.json "d['run']['id']")
for _ in $(seq 1 200); do
  s=$(curl -s "$B/runs/$RUN" | python3 -c "import json,sys; print(json.load(sys.stdin)['run']['status'])")
  [ "$s" = "passed" ] && break; sleep 0.1
done
curl -s "$B/runs/$RUN" >run-detail.json
# iPhone 16 Pro does not exist on iOS 17.5 in the demo catalog: that combination comes back in `skipped`
curl -s -X POST "$B/runs" -H "$JSON" -d '{"scheme":"DemoApp","runtimes":["17.5"],"models":["iPhone 16 Pro","iPhone 15"]}' >run-skipped.json
SKIPRUN=$(field run-skipped.json "d['run']['id']")
for _ in $(seq 1 200); do
  s=$(curl -s "$B/runs/$SKIPRUN" | python3 -c "import json,sys; print(json.load(sys.stdin)['run']['status'])")
  [ "$s" = "passed" ] && break; sleep 0.1
done
curl -s "$B/runs" >runs.json
curl -s "$B/tests" >tests.json
curl -s "$B/events?limit=25" >events.json
curl -s "$B/metrics/summary" >metrics-summary.json
curl -s "$B/devices" >devices-after.json
curl -s -X POST "$B/devices/sync" >devices-sync.json
curl -s -X POST "$B/maintenance/cleanup" -H "$JSON" -d '{"days":3650}' >cleanup.json

# errors
curl -s "$B/tests/does-not-exist" >error-404.json
curl -s -X POST "$B/tests/run" -H "$JSON" -d '{}' >error-400.json
curl -s "$T/devices" >error-401.json
curl -s -H "authorization: Bearer $TOKEN" -X POST "$T/vms/spawn" -H "$JSON" -d '{"name":"Lab VM"}' >vm-spawn.json
sleep 2
curl -s -H "authorization: Bearer $TOKEN" "$T/devices" >devices-vm.json
curl -s -H "authorization: Bearer $TOKEN" -X POST "$T/devices/spawn" -H "$JSON" -d '{"name":"one too many"}' >error-429.json

# live websocket capture: replay of history plus raw output of a new job
cat >"$DATA/wscap.cjs" <<EOF
const WebSocket = require('$BACKEND_DIR/node_modules/ws');
const out = [];
const ws = new WebSocket(process.argv[2]);
ws.on('message', (m) => out.push(m.toString()));
setTimeout(() => { ws.close(); process.stdout.write(out.join('\n') + '\n'); process.exit(0); }, 4000);
EOF
node "$DATA/wscap.cjs" "ws://127.0.0.1:$PORT/ws/events?replay=6&output=1" >events-ws.ndjson &
WS=$!
sleep 1
curl -s -X POST "$B/tests/run" -H "$JSON" -d '{"scheme":"LoginFailTests"}' >/dev/null
wait "$WS"

# a PNG for the screenshot decoder test
DEV=$(field device-spawn.json "d['id']")
curl -s "$B/devices/$DEV/screenshot" -o screenshot.png
echo "captured $(ls | wc -l) files into $HERE"
