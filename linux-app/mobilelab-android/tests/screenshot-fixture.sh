#!/usr/bin/env bash
# Runs the real application against the fake Android SDK fixture in screenshot mode and checks the result.
#   screenshot-fixture.sh APP IMAGECHECK GLASSPROOF
# Environment: MOBILELAB_SHOTS_OUT keeps the PNGs there (default: a temporary directory),
#              MOBILELAB_SHOTS_FULL=1 also renders 2000x1111.
set -euo pipefail
APP="${1:?app}"; CHECK="${2:?imagecheck}"; PROOF="${3:?glass proof}"
export QT_QPA_PLATFORM=offscreen
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK="$(mktemp -d)"
OUT="${MOBILELAB_SHOTS_OUT:-$WORK/shots}"
trap 'rm -rf "$WORK"' EXIT
fail() { echo "[FAIL] $*" >&2; exit 1; }

combos=("1477x959 light" "1477x959 dark")
[ "${MOBILELAB_SHOTS_FULL:-0}" = 1 ] && combos+=("2000x1111 light" "2000x1111 dark")

for combo in "${combos[@]}"; do
  size="${combo% *}"; theme="${combo#* }"
  dir="$OUT/$size-$theme"
  mkdir -p "$dir"
  bash "$HERE/fixtures/make-fixture.sh" "$WORK/fx" >/dev/null
  echo "[INFO] screenshots $size $theme"
  env -i PATH="$PATH" HOME="$WORK/home" \
      XDG_CACHE_HOME="$WORK/cache" XDG_DATA_HOME="$WORK/data" XDG_RUNTIME_DIR="$WORK/run" \
      ANDROID_HOME="$WORK/fx/sdk" ANDROID_AVD_HOME="$WORK/fx/avd" FAKE_ANDROID_STATE="$WORK/fx/state" \
      MOBILELAB_ANDROID_ARTIFACTS="$WORK/artifacts-$size-$theme" MOBILELAB_SETTINGS_DIR="$WORK/settings-$size-$theme" \
      MOBILELAB_ANDROID_API_PORT=0 FAKE_BOOT_DELAY_MS=2200 MOBILELAB_POLL_MS=150 MOBILELAB_THEME="$theme" \
      MOBILELAB_SCREENSHOT_DIR="$dir" MOBILELAB_SCREENSHOT_SIZE="$size" \
      QT_QPA_PLATFORM=offscreen QT_QPA_FONTDIR="" \
      timeout 600 "$APP" > "$dir/app.log" 2>&1 || { tail -30 "$dir/app.log" >&2; cat "$dir/manifest.txt" >&2 2>/dev/null || true; fail "application exited with an error ($size $theme)"; }
  # leave no fake emulator behind
  for pidf in "$WORK"/fx/state/running/*.pid; do [ -e "$pidf" ] && kill "$(cat "$pidf")" 2>/dev/null || true; done
  count=$(ls "$dir"/*.png 2>/dev/null | wc -l)
  [ "$count" -ge 20 ] || fail "only $count screenshots in $dir"
  "$CHECK" "$dir"/*.png > "$dir/imagecheck.txt" || { cat "$dir/imagecheck.txt" >&2; fail "blank or missing screenshot ($size $theme)"; }
  grep -q "^ui-font Inter" "$dir/manifest.txt" || fail "Inter is not the UI font: $(grep ui-font "$dir/manifest.txt")"
  grep -q "mono-font JetBrains Mono" "$dir/manifest.txt" || fail "JetBrains Mono is not the mono font"
  grep -q "^targets 6" "$dir/manifest.txt" || fail "fixture targets were not discovered: $(grep '^targets' "$dir/manifest.txt")"
  ! grep -q "^FAILED" "$dir/manifest.txt" || fail "driver reported: $(grep '^FAILED' "$dir/manifest.txt")"
  echo "[PASS] $count screenshots $size $theme"
done

# The matrix run left real artifacts: run.json, run.log, per target logcat and screenshot, and a failing abi test.
art="$WORK/artifacts-${combos[0]%% *}-light"
run=$(ls -d "$art"/matrix-* | head -1)
[ -s "$run/run.json" ] && [ -s "$run/run.log" ] || fail "run artifacts missing in $run"
[ -s "$run/Pixel_8_API_35/logcat.txt" ] && [ -s "$run/Pixel_8_API_35/screenshot.png" ] || fail "logcat or screenshot artifact missing"
grep -q "error: -\[Pixel_Fold_API_34_arm64 abi\]" "$run/run.log" || fail "the deliberate ABI mismatch was not reported"
grep -q "\*\* TEST FAILED \*\*" "$run/run.log" || fail "run.log has no verdict"

# Glass proof: refraction at the edges, blur without refraction, flat fallback.
mkdir -p "$OUT/glass"
"$PROOF" "$OUT/glass/proof-light.png" | tee "$OUT/glass/proof.txt" | grep -q "glass proof OK" || fail "glass proof failed"
MOBILELAB_THEME=dark "$PROOF" "$OUT/glass/proof-dark.png" | grep -q "glass proof OK" || fail "dark glass proof failed"
echo "[PASS] screenshot fixture test"
