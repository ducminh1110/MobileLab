import test from "node:test";
import assert from "node:assert/strict";
import { matchReference } from "../src/utils/resolve";

interface Item {
  id: string;
  name: string;
}

const items: Item[] = [
  { id: "abc11111-0000-0000-0000-000000000000", name: "iPhone 15 (iOS 18.0)" },
  { id: "abc22222-0000-0000-0000-000000000000", name: "iPhone 15 (iOS 18.0)" },
  { id: "def33333-0000-0000-0000-000000000000", name: "Solo" },
  { id: "ab000000-0000-0000-0000-000000000000", name: "ab" }
];
const id = (i: Item) => i.id;
const name = (i: Item) => i.name;

test("an exact id wins", () => {
  const result = matchReference(items, "def33333-0000-0000-0000-000000000000", id, name);
  assert.equal(result.kind === "match" && result.item.name, "Solo");
});

test("a unique id prefix matches, case-insensitively", () => {
  assert.equal((matchReference(items, "abc1", id, name) as { item: Item }).item.name, "iPhone 15 (iOS 18.0)");
  assert.equal((matchReference(items, "DEF", id, name) as { item: Item }).item.name, "Solo");
});

test("an id prefix shared by several items is ambiguous and lists them", () => {
  const result = matchReference(items, "abc", id, name);
  assert.equal(result.kind, "ambiguous");
  assert.equal(result.kind === "ambiguous" && result.candidates.length, 2);
});

test("an exact name matches when it is unique", () => {
  const result = matchReference(items, "Solo", id, name);
  assert.equal(result.kind === "match" && result.item.id.startsWith("def3"), true);
  assert.equal(matchReference(items, "solo", id, name).kind, "match", "case-insensitive as a fallback");
});

test("a name shared by several items is ambiguous", () => {
  const result = matchReference(items, "iPhone 15 (iOS 18.0)", id, name);
  assert.equal(result.kind, "ambiguous");
});

test("names are not prefix-matched", () => {
  assert.equal(matchReference(items, "iPhone", id, name).kind, "none");
  assert.equal(matchReference(items, "Sol", id, name).kind, "none");
});

test("an exact name beats an id prefix", () => {
  // "ab" is the name of one item and the start of several ids.
  const result = matchReference(items, "ab", id, name);
  assert.equal(result.kind === "match" && result.item.id.startsWith("ab000000"), true);
});

test("unknown and empty references match nothing", () => {
  assert.equal(matchReference(items, "zzz", id, name).kind, "none");
  assert.equal(matchReference(items, "  ", id, name).kind, "none");
});

test("references without names (jobs) only match ids", () => {
  assert.equal(matchReference(items, "Solo", id).kind, "none");
  assert.equal(matchReference(items, "def", id).kind, "match");
});
