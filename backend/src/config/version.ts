import fs from "node:fs";
import path from "node:path";

/** Reads the version from package.json (works from both src/ and dist/). */
export function readVersion(): string {
  try {
    const file = path.join(__dirname, "..", "..", "package.json");
    return (JSON.parse(fs.readFileSync(file, "utf-8")) as { version?: string }).version ?? "0.0.0";
  } catch {
    return "0.0.0";
  }
}
