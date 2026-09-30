#!/usr/bin/env bash
# Fake `adb` for the running fake emulators (see fake-emulator.sh).
STATE="${FAKE_ANDROID_STATE:?FAKE_ANDROID_STATE is not set}"
AVD_HOME="${ANDROID_AVD_HOME:?ANDROID_AVD_HOME is not set}"
BOOT_DELAY_MS="${FAKE_BOOT_DELAY_MS:-800}"
serial=""
if [ "${1:-}" = "-s" ]; then serial="$2"; shift 2; fi
cmd="${1:-}"; [ $# -gt 0 ] && shift
avd_of() { cat "$STATE/running/$1" 2>/dev/null; }
booted() {
  local s now; s=$(cat "$STATE/running/$1.started" 2>/dev/null) || return 1
  now=$(date +%s%N)
  [ $(( (now - s) / 1000000 )) -ge "$BOOT_DELAY_MS" ]
}
ini_value() { grep -m1 "^$2=" "$AVD_HOME/$1.avd/config.ini" 2>/dev/null | cut -d= -f2-; }
case "$cmd" in
  devices)
    echo "List of devices attached"
    for f in "$STATE"/running/emulator-*; do
      case "$f" in *.pid|*.started) continue ;; esac
      [ -e "$f" ] && printf '%s\tdevice\n' "$(basename "$f")"
    done
    ;;
  emu)
    [ -e "$STATE/running/$serial" ] || { echo "error: device '$serial' not found" >&2; exit 1; }
    case "${1:-}" in
      avd) [ "${2:-}" = name ] && { echo "$(avd_of "$serial")"; echo OK; } ;;
      kill)
        pid=$(cat "$STATE/running/$serial.pid" 2>/dev/null)
        rm -f "$STATE/running/$serial" "$STATE/running/$serial.pid" "$STATE/running/$serial.started"
        [ -n "$pid" ] && kill "$pid" 2>/dev/null
        echo "OK: killing emulator, bye bye"
        ;;
    esac
    ;;
  shell)
    [ -e "$STATE/running/$serial" ] || { echo "error: device '$serial' not found" >&2; exit 1; }
    avd=$(avd_of "$serial")
    if [ "${1:-}" = getprop ]; then
      case "${2:-}" in
        sys.boot_completed) booted "$serial" && echo 1 ;;
        ro.product.cpu.abi)
          if [ -e "$STATE/abi-override/$avd" ]; then cat "$STATE/abi-override/$avd"; else ini_value "$avd" abi.type; fi ;;
        ro.build.version.sdk)
          ini_value "$avd" image.sysdir.1 | sed -n 's|.*android-\([0-9][0-9]*\).*|\1|p' ;;
        *) echo ;;
      esac
    else
      echo "fake shell: $*"
    fi
    ;;
  logcat)
    [ -e "$STATE/running/$serial" ] || { echo "error: device '$serial' not found" >&2; exit 1; }
    avd=$(avd_of "$serial")
    echo "--------- beginning of main"
    echo "09-30 09:41:00.101  1180  1180 I SystemServer: Entered the Android system server for $avd"
    echo "09-30 09:41:01.240  1180  1204 I ActivityManager: Start proc com.android.launcher3"
    echo "09-30 09:41:02.517  1318  1318 W Launcher: Skipping widget binding, provider not ready"
    echo "09-30 09:41:03.882  1180  1240 E WifiService: Failed to load wifi config, using defaults"
    echo "09-30 09:41:04.100  1180  1180 D BootAnimation: sys.boot_completed=1"
    ;;
  exec-out)
    [ -e "$STATE/running/$serial" ] || { echo "error: device '$serial' not found" >&2; exit 1; }
    if [ "${1:-}" = screencap ]; then cat "$(dirname "$STATE")/screen.png"; fi
    ;;
  version) echo "Android Debug Bridge version 1.0.41 (fake)" ;;
  *) echo "fake adb: unsupported command $cmd" >&2; exit 1 ;;
esac
