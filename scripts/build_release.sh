#!/usr/bin/env bash
set -euo pipefail

log() {
  printf '[ioslab-build] %s\n' "$1"
}

fail() {
  printf '[ioslab-build] ERROR: %s\n' "$1" >&2
  exit 1
}

REPO_URL="${1:-}"
WORKDIR="${2:-}"
IOSLAB_SIMULATOR_MOCK="${IOSLAB_SIMULATOR_MOCK:-false}"

if [[ -n "$REPO_URL" ]]; then
  TARGET_DIR="${WORKDIR:-$(mktemp -d)}"
  log "Cloning repository: $REPO_URL -> $TARGET_DIR"
  git clone "$REPO_URL" "$TARGET_DIR"
  ROOT_DIR="$TARGET_DIR"
else
  ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
fi

DIST_DIR="$ROOT_DIR/dist"
PROJECT_PATH="$ROOT_DIR/macos-app/IOSLabDashboard.xcodeproj"
SCHEME_NAME="IOSLabDashboard"
mkdir -p "$DIST_DIR"

log "Using root directory: $ROOT_DIR"
log "IOSLAB_SIMULATOR_MOCK=$IOSLAB_SIMULATOR_MOCK"

log "Packaging backend (with the web dashboard) and CLI"
"$ROOT_DIR/scripts/package_runtime.sh" "$DIST_DIR"
RUNTIME_DIR="$DIST_DIR/backend-runtime"

log "Validating macOS/Xcode environment"
if [[ "$(uname -s)" != "Darwin" ]]; then
  fail "This dashboard build requires macOS (uname -s returned $(uname -s))"
fi

if ! command -v xcodebuild >/dev/null 2>&1; then
  fail "xcodebuild not found on this machine"
fi

if [[ ! -d "$PROJECT_PATH" ]]; then
  fail "Expected project file not found at macos-app/IOSLabDashboard.xcodeproj"
fi

if [[ ! -f "$PROJECT_PATH/project.pbxproj" ]]; then
  fail "Project exists but project.pbxproj is missing at macos-app/IOSLabDashboard.xcodeproj/project.pbxproj"
fi

if ! xcodebuild -list -project "$PROJECT_PATH" >/tmp/ioslab-xcodeproj-check.log 2>&1; then
  cat /tmp/ioslab-xcodeproj-check.log >&2 || true
  fail "Unable to read project at macos-app/IOSLabDashboard.xcodeproj"
fi

log "Building dashboard with xcodebuild (scheme: $SCHEME_NAME)"
(
  cd "$ROOT_DIR/macos-app"
  xcodebuild \
    -project IOSLabDashboard.xcodeproj \
    -scheme "$SCHEME_NAME" \
    -configuration Release \
    -derivedDataPath .build/release \
    build
)

APP_PATH="$ROOT_DIR/macos-app/.build/release/Build/Products/Release/IOSLabDashboard.app"
if [[ ! -d "$APP_PATH" ]]; then
  fail "xcodebuild completed but expected app bundle is missing at $APP_PATH"
fi

log "Embedding backend runtime into app bundle"
rm -rf "$APP_PATH/Contents/Resources/backend-runtime"
mkdir -p "$APP_PATH/Contents/Resources/backend-runtime"
cp -R "$RUNTIME_DIR"/* "$APP_PATH/Contents/Resources/backend-runtime/"

log "Packaging dashboard app bundle"
ditto -c -k --sequesterRsrc --keepParent "$APP_PATH" "$DIST_DIR/IOSLabDashboard.zip"

if command -v productbuild >/dev/null 2>&1; then
  log "Packaging signed-style installer pkg"
  rm -f "$DIST_DIR/IOSLabDashboard.pkg"
  productbuild --component "$APP_PATH" /Applications "$DIST_DIR/IOSLabDashboard.pkg"
else
  log "productbuild not found; skipping pkg installer generation"
fi

log "Build artifacts generated in: $DIST_DIR"
ls -lah "$DIST_DIR"
