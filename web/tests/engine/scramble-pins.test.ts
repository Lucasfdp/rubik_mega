import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { instantiatePuzzleModule } from "../../src/engine/wasm-module.js";
import { Puzzle } from "../../src/engine/puzzle.js";
import { PuzzleType } from "../../src/engine/types.js";

/// A seed in a shared link has to mean ONE scramble on every build
/// (docs/en/13-phase6-polish.md D10). This is the same table as
/// core/tests/test_scramble_pins.c, checked against the compiled WASM, so a
/// compiler or target difference in how a scramble draws its numbers fails
/// here. Update both together, and say so in DECISIONS.md.
const PIN_SEED = 12345;

const PINS = [
  { name: "cube 3", type: PuzzleType.Cubic, size: 3, moves: 25, hash: 0xc69836dc },
  { name: "cube 4", type: PuzzleType.Cubic, size: 4, moves: 40, hash: 0x04490edb },
  { name: "cube 8", type: PuzzleType.Cubic, size: 8, moves: 120, hash: 0x76b93417 },
  { name: "skewb", type: PuzzleType.Skewb, size: 0, moves: 25, hash: 0xf6e19702 },
  { name: "pyraminx", type: PuzzleType.Tetrahedral, size: 0, moves: 25, hash: 0x38ed35c6 },
  { name: "kilominx", type: PuzzleType.Dodecahedral, size: 2, moves: 40, hash: 0x6219ca27 },
  { name: "megaminx", type: PuzzleType.Dodecahedral, size: 3, moves: 70, hash: 0x40151d5c },
  { name: "square-1", type: PuzzleType.Square1, size: 0, moves: 25, hash: 0x94fa06ca },
];

/// FNV-1a, the same as the C test.
function fnv1a(bytes: Uint8Array): number {
  let hash = 0x811c9dc5;
  for (const byte of bytes) hash = Math.imul(hash ^ byte, 16777619) >>> 0;
  return hash;
}

async function loadModule() {
  return instantiatePuzzleModule(readFileSync(fileURLToPath(new URL("../../../core/build/rubik_mega.wasm", import.meta.url))));
}

describe("scramble pins (WASM)", () => {
  it.each(PINS)("$name: seed 12345 leaves the pinned state", async ({ type, size, moves, hash }) => {
    const puzzle = Puzzle.create(await loadModule(), type, size);
    await puzzle.scramble(moves, PIN_SEED);
    expect(fnv1a(puzzle.stateView())).toBe(hash);
    puzzle.destroy();
  });

  it.each(PINS)("$name: scrambleMoves() replayed gives the same state as scramble()", async ({ type, size, moves }) => {
    const module = await loadModule();
    const direct = Puzzle.create(module, type, size);
    await direct.scramble(moves, PIN_SEED);
    const replayed = Puzzle.create(module, type, size);
    const list = await replayed.scrambleMoves(moves, PIN_SEED);
    for (const move of list) await replayed.applyMove(move.generator, move.amount);
    expect(Array.from(replayed.stateView())).toEqual(Array.from(direct.stateView()));
    direct.destroy();
    replayed.destroy();
  });
});

describe("isSolved (WASM)", () => {
  it.each(PINS)("$name: solved fresh, not solved after a scramble", async ({ type, size, moves }) => {
    const puzzle = Puzzle.create(await loadModule(), type, size);
    expect(puzzle.isSolved()).toBe(true);
    const list = await puzzle.scrambleMoves(moves, PIN_SEED);
    for (const move of list) await puzzle.applyMove(move.generator, move.amount);
    expect(puzzle.isSolved()).toBe(false);
    puzzle.destroy();
  });
});
