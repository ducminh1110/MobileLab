#!/usr/bin/env bash
# Fake `emulator`: -list-avds prints the AVD names, -avd NAME registers a running "device" until it is killed.
STATE="${FAKE_ANDROID_STATE:?FAKE_ANDROID_STATE is not set}"
AVD_HOME="${ANDROID_AVD_HOME:?ANDROID_AVD_HOME is not set}"
if [ "${1:-}" = "-list-avds" ]; then
  for f in "$AVD_HOME"/*.ini; do [ -e "$f" ] && basename "$f" .ini; done
  exit 0
fi
name=""
while [ $# -gt 0 ]; do
  case "$1" in -avd) name="$2"; shift ;; esac
  shift
done
[ -n "$name" ] || { echo "usage: emulator -avd NAME" >&2; exit 1; }
[ -e "$AVD_HOME/$name.ini" ] || { echo "PANIC: unknown AVD $name" >&2; exit 1; }
mkdir -p "$STATE/running"
port=5554
while [ -e "$STATE/running/emulator-$port" ]; do port=$((port + 2)); done
serial="emulator-$port"
printf '%s' "$name" > "$STATE/running/$serial"
echo $$ > "$STATE/running/$serial.pid"
date +%s%N > "$STATE/running/$serial.started"
echo "INFO: fake emulator started $name as $serial"
trap 'rm -f "$STATE/running/$serial" "$STATE/running/$serial.pid" "$STATE/running/$serial.started"; exit 0' TERM INT
while [ -e "$STATE/running/$serial" ]; do sleep 0.1; done
exit 0
