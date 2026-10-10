import { describe, expect, it } from "vitest";
import { Axis } from "../../src/render/lattice/cubic.js";
import { MoveMap } from "../../src/interaction/move-axis.js";
import type { GeneratorLayer } from "../../src/engine/types.js";

/// core/'s generator layout, written out by hand for the two shapes it
/// takes: n == 3 (one generator per face) and n >= 4 (face-major,
/// depth-minor -- core/src/puzzles/cubic.c's cubic_generator_index()).
/// In the app these come from the WASM module itself
/// (PuzzleLayout.generators); puzzle-worker-logic.test.ts checks that.
function layersFor(n: number): GeneratorLayer[] {
  const perFace = n === 3 ? 1 : Math.floor((n - 1) / 2) + 1;
  const layers: GeneratorLayer[] = [];
  for (let face = 0; face < 6; face++) {
    for (let depth = 0; depth < perFace; depth++) layers.push({ face, depth });
  }
  return layers;
}

// Every face's world-axis rotation, hand-transcribed from
// rubik/src/render/anim.c's MOVE_AXIS table (the "*1" entry per face,
// doubled/negated for amount 2/3). Layers are lattice indices (0..2).
const EXPECTED_3: { generator: number; name: string; axis: Axis; layer: number; deg: [number, number, number] }[] = [
  { generator: 0, name: "U", axis: Axis.Y, layer: 2, deg: [-90, -180, 90] },
  { generator: 1, name: "R", axis: Axis.X, layer: 2, deg: [-90, -180, 90] },
  { generator: 2, name: "F", axis: Axis.Z, layer: 2, deg: [-90, -180, 90] },
  { generator: 3, name: "D", axis: Axis.Y, layer: 0, deg: [90, 180, -90] },
  { generator: 4, name: "L", axis: Axis.X, layer: 0, deg: [90, 180, -90] },
  { generator: 5, name: "B", axis: Axis.Z, layer: 0, deg: [90, 180, -90] },
];

describe("MoveMap, 3x3x3", () => {
  const moves = new MoveMap(3, layersFor(3));

  for (const { generator, name, axis, layer, deg } of EXPECTED_3) {
    it(`generator ${generator} (${name}): axis/layer and all 3 amounts match MOVE_AXIS, and round-trip`, () => {
      expect(moves.axisForGenerator(generator)).toEqual({ axis, layer });
      expect(moves.quarterDegForAmount(generator, 1)).toBe(deg[0]);
      expect(moves.quarterDegForAmount(generator, 2)).toBe(deg[1]);
      expect(moves.quarterDegForAmount(generator, 3)).toBe(deg[2]);
      expect(moves.moveForTurn(axis, layer, deg[0])).toEqual({ generator, amount: 1 });
      expect(moves.moveForTurn(axis, layer, deg[1])).toEqual({ generator, amount: 2 });
      expect(moves.moveForTurn(axis, layer, deg[2])).toEqual({ generator, amount: 3 });
    });
  }

  it("a half turn matches regardless of sign (rubik's own B6 fix)", () => {
    expect(moves.moveForTurn(Axis.Y, 2, 180)).toEqual({ generator: 0, amount: 2 });
    expect(moves.moveForTurn(Axis.Y, 2, -180)).toEqual({ generator: 0, amount: 2 });
  });

  it("the middle slice has no generator -- moveForTurn null, layerHasGenerator false", () => {
    expect(moves.moveForTurn(Axis.Y, 1, 90)).toBeNull();
    expect(moves.layerHasGenerator(Axis.Y, 1)).toBe(false);
    expect(moves.layerHasGenerator(Axis.Y, 2)).toBe(true);
  });

  it("returns null for an angle that isn't a quarter or half turn", () => {
    expect(moves.moveForTurn(Axis.Y, 2, 45)).toBeNull();
  });

  it("rejects an out-of-range generator", () => {
    expect(() => moves.quarterDegForAmount(6, 1)).toThrow(/out of range/);
    expect(() => moves.axisForGenerator(-1)).toThrow(/out of range/);
  });
});

describe("MoveMap, bigger cubes", () => {
  it("every layer of every axis has a generator, for n = 4..8", () => {
    for (const n of [4, 5, 6, 7, 8]) {
      const moves = new MoveMap(n, layersFor(n));
      for (const axis of [Axis.X, Axis.Y, Axis.Z]) {
        for (let layer = 0; layer < n; layer++) expect(moves.layerHasGenerator(axis, layer)).toBe(true);
      }
    }
  });

  it("an inner D-side layer turns like D (same clockwise sign), at its own lattice index", () => {
    const moves = new MoveMap(4, layersFor(4));
    const dDepth1 = 3 * 2 + 1; // face D (3), depth 1, 2 generators per face at n == 4
    expect(moves.axisForGenerator(dDepth1)).toEqual({ axis: Axis.Y, layer: 1 });
    expect(moves.quarterDegForAmount(dDepth1, 1)).toBe(90);
    expect(moves.moveForTurn(Axis.Y, 1, 90)).toEqual({ generator: dDepth1, amount: 1 });
  });

  it("odd n: the true middle layer resolves to the U/R/F-side generator (D3), as the same physical turn", () => {
    const moves = new MoveMap(5, layersFor(5));
    const uMiddle = 0 * 3 + 2; // face U, depth 2
    const dMiddle = 3 * 3 + 2; // face D, depth 2 -- same cells
    expect(moves.axisForGenerator(uMiddle)).toEqual(moves.axisForGenerator(dMiddle));
    expect(moves.moveForTurn(Axis.Y, 2, -90)).toEqual({ generator: uMiddle, amount: 1 });
    // D-middle clockwise (+90 world) is the same physical turn as U-middle amount 3.
    expect(moves.moveForTurn(Axis.Y, 2, moves.quarterDegForAmount(dMiddle, 1))).toEqual({ generator: uMiddle, amount: 3 });
  });
});
