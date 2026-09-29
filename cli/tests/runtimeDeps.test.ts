import test from "node:test";
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";

const root = path.resolve(__dirname, "..");
const pkg = JSON.parse(fs.readFileSync(path.join(root, "package.json"), "utf-8")) as { dependencies: Record<string, string> };

// The CLI is CommonJS. An ESM-only dependency (chalk 5, ora 9, ...) makes it crash with ERR_REQUIRE_ESM on
// Node releases without require(esm) (below 20.19 and 22.12). `--no-experimental-require-module` reproduces
// that behaviour on a newer Node, so this catches the problem without needing an old Node in CI.
for (const dependency of Object.keys(pkg.dependencies)) {
  test(`runtime dependency "${dependency}" loads with plain require() on older Node`, () => {
    const result = spawnSync(process.execPath, ["--no-experimental-require-module", "-e", `require(${JSON.stringify(dependency)})`], {
      cwd: root,
      encoding: "utf-8"
    });
    assert.equal(result.status, 0, `${dependency} is not loadable with require():\n${result.stderr}`);
  });
}
