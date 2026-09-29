import test from "node:test";
import assert from "node:assert/strict";
import { createStyle } from "../src/utils/style";
import { Column, renderTable } from "../src/utils/table";

interface Row {
  name: string;
  count: number;
  note: string;
}

const columns: Array<Column<Row>> = [
  { header: "NAME", value: (r) => r.name },
  { header: "COUNT", value: (r) => String(r.count), align: "right" },
  { header: "NOTE", value: (r) => r.note, flex: true }
];

test("columns line up, numbers are right-aligned, no trailing spaces", () => {
  const lines = renderTable(columns, [
    { name: "alpha", count: 3, note: "x" },
    { name: "b", count: 1200, note: "" }
  ]);
  assert.deepEqual(lines, ["NAME   COUNT  NOTE", "alpha      3  x", "b       1200"]);
  for (const line of lines) assert.equal(line, line.trimEnd());
});

test("an empty table is just the header", () => {
  assert.deepEqual(renderTable(columns, []), ["NAME  COUNT  NOTE"]);
});

test("indent is applied to every line", () => {
  const lines = renderTable(columns, [{ name: "a", count: 1, note: "n" }], { indent: "  " });
  assert.ok(lines.every((line) => line.startsWith("  ")));
});

test("cells are squashed onto one line and stripped of escape sequences", () => {
  const [, row] = renderTable(columns, [{ name: "two\nlines", count: 1, note: "\u001b[31mred\u001b[0m\tnote" }]);
  assert.equal(row, "two lines      1  red note");
});

test("without maxWidth nothing is ever truncated", () => {
  const long = "x".repeat(300);
  const [, row] = renderTable(columns, [{ name: "a", count: 1, note: long }]);
  assert.ok(row.endsWith(long));
});

test("flex columns are shortened with an ellipsis to fit maxWidth", () => {
  const lines = renderTable(columns, [{ name: "alpha", count: 5, note: "this is a rather long explanation" }], { maxWidth: 30 });
  for (const line of lines) assert.ok([...line].length <= 30, `"${line}" is ${[...line].length} wide`);
  assert.match(lines[1], /…$/);
  assert.match(lines[1], /^alpha {2}\s*5 {2}this is/);
});

test("columns that are not flexible are never shortened, even if the table stays too wide", () => {
  const lines = renderTable([{ header: "ID", value: (r: { id: string }) => r.id }], [{ id: "abcdefghij" }], { maxWidth: 4 });
  assert.equal(lines[1], "abcdefghij");
});

test("a flex column is not squeezed below its minimum width", () => {
  const lines = renderTable(
    [
      { header: "A", value: (r: { a: string; b: string }) => r.a },
      { header: "B", value: (r) => r.b, flex: true, minWidth: 8 }
    ],
    [{ a: "aaaa", b: "bbbbbbbbbbbbbbbbbbbb" }],
    { maxWidth: 5 }
  );
  assert.equal(lines[1], "aaaa  bbbbbbb…");
});

test("styled cells (ANSI) do not disturb alignment", () => {
  const chalk = createStyle(true);
  const styled: Array<Column<Row>> = [
    { header: "NAME", value: (r) => r.name, style: (t) => chalk.green(t) },
    { header: "COUNT", value: (r) => String(r.count), align: "right", style: (t) => chalk.red(t) },
    { header: "NOTE", value: (r) => r.note }
  ];
  const rows = [
    { name: "alpha", count: 3, note: "x" },
    { name: "b", count: 1200, note: "y" }
  ];
  const plainLines = renderTable(columns, rows);
  const colored = renderTable(styled, rows, { headerStyle: (t) => chalk.bold(t) });
  const strip = (s: string) => s.replace(/\u001b\[\d+m/g, "");
  assert.ok(colored.some((line) => line.includes("\u001b[32m")), "the style was applied");
  assert.deepEqual(colored.map(strip), plainLines);
});

test("characters are counted as code points, so astral characters do not break alignment", () => {
  const lines = renderTable([{ header: "S", value: (r: { s: string }) => r.s }, { header: "T", value: () => "t" }], [{ s: "✔✔✔" }, { s: "😀" }]);
  assert.deepEqual(lines, ["S    T", "✔✔✔  t", "😀    t"]);
});
