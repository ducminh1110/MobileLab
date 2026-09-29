/**
 * Terminal colours. A few ANSI codes instead of a dependency: chalk 5 is ESM-only and this CLI is CommonJS,
 * so it crashed on Node releases without require(esm) (20.0 to 20.18).
 */
export interface Style {
  bold(text: string): string;
  dim(text: string): string;
  red(text: string): string;
  green(text: string): string;
  yellow(text: string): string;
  cyan(text: string): string;
}

const wrap = (open: number, close: number) => (text: string) => `\u001b[${open}m${text}\u001b[${close}m`;
const plain = (text: string) => text;

export function createStyle(enabled: boolean): Style {
  if (!enabled) return { bold: plain, dim: plain, red: plain, green: plain, yellow: plain, cyan: plain };
  return {
    bold: wrap(1, 22),
    dim: wrap(2, 22),
    red: wrap(31, 39),
    green: wrap(32, 39),
    yellow: wrap(33, 39),
    cyan: wrap(36, 39)
  };
}
