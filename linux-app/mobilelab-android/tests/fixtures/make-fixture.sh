#!/usr/bin/env bash
# Builds a fake Android SDK (emulator, adb, avdmanager as shell scripts) plus AVD definitions under $1.
# The application's real discovery and execution code runs against it; nothing in the product is faked.
set -euo pipefail
ROOT="${1:?usage: make-fixture.sh DIR}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK="$ROOT/sdk"; AVD="$ROOT/avd"; STATE="$ROOT/state"
rm -rf "$ROOT"
mkdir -p "$SDK/emulator" "$SDK/platform-tools" "$SDK/cmdline-tools/latest/bin" "$AVD" "$STATE/running" "$STATE/abi-override"
mkdir -p "$SDK/system-images/android-35/google_apis/x86_64" "$SDK/system-images/android-34/google_apis/x86_64" \
         "$SDK/system-images/android-34/google_apis/arm64-v8a" "$SDK/system-images/android-30/default/arm64-v8a"
for d in "$SDK"/system-images/*/*/*; do echo "Pkg.Revision=1" > "$d/source.properties"; done
cp "$HERE/android-screen.png" "$ROOT/screen.png"
cp "$HERE/fake-emulator.sh" "$SDK/emulator/emulator"
cp "$HERE/fake-adb.sh" "$SDK/platform-tools/adb"
cp "$HERE/fake-avdmanager.sh" "$SDK/cmdline-tools/latest/bin/avdmanager"
chmod +x "$SDK/emulator/emulator" "$SDK/platform-tools/adb" "$SDK/cmdline-tools/latest/bin/avdmanager"

mkavd() { # name api abi tag device [omit-api]
  local name="$1" api="$2" abi="$3" tag="$4" dev="$5" omit="${6:-}"
  mkdir -p "$AVD/$name.avd"
  {
    echo "avd.ini.encoding=UTF-8"
    echo "path=$AVD/$name.avd"
    echo "path.rel=avd/$name.avd"
    [ -z "$omit" ] && echo "target=android-$api"
  } > "$AVD/$name.ini"
  {
    echo "AvdId=$name"
    echo "abi.type=$abi"
    echo "hw.cpu.arch=$([ "$abi" = arm64-v8a ] && echo arm64 || echo x86_64)"
    [ -z "$omit" ] && echo "image.sysdir.1=system-images/android-$api/$tag/$abi/"
    echo "tag.id=$tag"
    echo "hw.device.name=$dev"
    echo "avd.ini.displayname=$name"
  } > "$AVD/$name.avd/config.ini"
}
mkavd Pixel_8_API_35        35 x86_64    google_apis pixel_8
mkavd Pixel_7_API_34        34 x86_64    google_apis pixel_7
mkavd Pixel_Tablet_API_34   34 x86_64    google_apis pixel_tablet
mkavd Pixel_Fold_API_34_arm64 34 arm64-v8a google_apis pixel_fold
mkavd Legacy_ARM_API_30     30 arm64-v8a default     pixel_4
mkavd Unknown_API_Device    0  x86_64    google_apis pixel_6 omit
# The Fold AVD is configured for arm64-v8a but its (fake) system reports x86_64: a real failing `abi` test case.
echo "x86_64" > "$STATE/abi-override/Pixel_Fold_API_34_arm64"
echo "$ROOT"
