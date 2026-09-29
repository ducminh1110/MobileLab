#!/usr/bin/env bash
# Builds self-contained backend and CLI packages under <out>/ (default: ./dist). Works on any OS with
# Node 20+. Produces:
#   <out>/backend-runtime/   dist/, public/ (the web dashboard), production node_modules, start-backend.sh
#   <out>/cli-runtime/       dist/, production node_modules, ioslab launcher
set -euo pipefail

log() { printf '[package] %s\n' "$1"; }

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
OUT_DIR="${1:-$ROOT_DIR/dist}"
BACKEND_OUT="$OUT_DIR/backend-runtime"
CLI_OUT="$OUT_DIR/cli-runtime"

command -v node >/dev/null 2>&1 || { echo "node is required" >&2; exit 1; }
mkdir -p "$OUT_DIR"

log "Building backend"
(cd "$ROOT_DIR/backend" && npm ci && npm run build)

log "Staging backend runtime in $BACKEND_OUT"
rm -rf "$BACKEND_OUT"
mkdir -p "$BACKEND_OUT"
cp -R "$ROOT_DIR/backend/dist" "$BACKEND_OUT/dist"
cp -R "$ROOT_DIR/backend/public" "$BACKEND_OUT/public"
cp "$ROOT_DIR/backend/package.json" "$ROOT_DIR/backend/package-lock.json" "$BACKEND_OUT/"
(cd "$BACKEND_OUT" && npm ci --omit=dev --ignore-scripts --no-audit --no-fund)
cat > "$BACKEND_OUT/start-backend.sh" <<'LAUNCH'
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
exec /usr/bin/env node dist/index.js
LAUNCH
chmod +x "$BACKEND_OUT/start-backend.sh"

log "Building CLI"
(cd "$ROOT_DIR/cli" && npm ci && npm run build)

log "Staging CLI runtime in $CLI_OUT"
rm -rf "$CLI_OUT"
mkdir -p "$CLI_OUT"
cp -R "$ROOT_DIR/cli/dist" "$CLI_OUT/dist"
cp "$ROOT_DIR/cli/package.json" "$ROOT_DIR/cli/package-lock.json" "$CLI_OUT/"
(cd "$CLI_OUT" && npm ci --omit=dev --ignore-scripts --no-audit --no-fund)
cat > "$CLI_OUT/ioslab" <<'LAUNCH'
#!/usr/bin/env bash
set -euo pipefail
exec /usr/bin/env node "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/dist/index.js" "$@"
LAUNCH
chmod +x "$CLI_OUT/ioslab" "$CLI_OUT/dist/index.js"

log "Done: $BACKEND_OUT and $CLI_OUT"
