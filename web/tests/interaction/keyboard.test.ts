import { describe, expect, it } from "vitest";
import { cubeKeyMove, ignoresKey, interpretKey, KEY_ORBIT_STEP_DEG, SHORTCUT_HELP, type KeyContext, type KeyInput } from "../../src/interaction/keyboard.js";
import { PuzzleType, type PuzzleLayout } from "../../src/engine/types.js";

/// A cube layout with just the fields the keyboard reads: one generator per
/// face per depth, face-major, with core's per-face count (the engine test pins it).
function cube(n: number): PuzzleLayout {
  const perFace = n === 3 ? 1 : Math.floor((n - 1) / 2) + 1;
  const generators = [];
  for (let face = 0; face < 6; face++) {
    for (let depth = 0; depth < perFace; depth++) generators.push({ face, depth });
  }
  return { type: PuzzleType.Cubic, size: n, generators } as unknown as PuzzleLayout;
}

function cut(type: PuzzleType, generatorCount: number): PuzzleLayout {
  return { type, size: 3, generators: Array.from({ length: generatorCount }, (_, i) => ({ face: i, depth: 0 })) } as unknown as PuzzleLayout;
}

const key = (k: string, extra: Partial<KeyInput> = {}): KeyInput => ({ key: k, shiftKey: false, ctrlKey: false, metaKey: false, altKey: false, ...extra });
const ctx = (layout: PuzzleLayout, extra: Partial<KeyContext> = {}): KeyContext => ({
  layout,
  hasSolution: false,
  selectedGenerator: null,
  pendingLayer: null,
  ...extra,
});

describe("cubeKeyMove", () => {
  it("turns the outer layer of a face clockwise, or anticlockwise with prime", () => {
    const layout = cube(3);
    const r = cubeKeyMove(layout, 1, 1, false)!;
    expect(layout.generators[r.generator]).toEqual({ face: 1, depth: 0 });
    expect(r.amount).toBe(1);
    expect(cubeKeyMove(layout, 1, 1, true)!.amount).toBe(3);
  });

  it("reaches a far-half layer from the opposite face, sense reversed", () => {
    const layout = cube(4);
    // Layer 4 from U is the D face's outer layer, turned the other way.
    const move = cubeKeyMove(layout, 0, 4, false)!;
    expect(layout.generators[move.generator]).toEqual({ face: 3, depth: 0 });
    expect(move.amount).toBe(3);
  });

  it("is null for a layer the cube does not have", () => {
    expect(cubeKeyMove(cube(3), 0, 0, false)).toBeNull();
    expect(cubeKeyMove(cube(3), 0, 4, false)).toBeNull();
    // A 3x3x3 has no middle-slice generator.
    expect(cubeKeyMove(cube(3), 0, 2, false)).toBeNull();
  });

  it("finds every layer of a 5x5x5 from both ends", () => {
    const layout = cube(5);
    for (let layer = 1; layer <= 5; layer++) {
      for (let face = 0; face < 6; face++) {
        const move = cubeKeyMove(layout, face, layer, false);
        expect(move, `layer ${layer} face ${face}`).not.toBeNull();
      }
    }
  });
});

describe("interpretKey", () => {
  it("maps cube letters to turns and Shift to the inverse", () => {
    const layout = cube(3);
    expect(interpretKey(key("r"), ctx(layout))).toEqual({ kind: "turn", move: { generator: 1, amount: 1 } });
    expect(interpretKey(key("R", { shiftKey: true }), ctx(layout))).toEqual({ kind: "turn", move: { generator: 1, amount: 3 } });
  });

  it("uses a typed layer number for the next letter", () => {
    const layout = cube(5);
    expect(interpretKey(key("2"), ctx(layout))).toEqual({ kind: "selectLayer", layer: 2 });
    const command = interpretKey(key("u"), ctx(layout, { pendingLayer: 2 }));
    expect(command).toMatchObject({ kind: "turn" });
    if (command?.kind === "turn") expect(layout.generators[command.move.generator]).toEqual({ face: 0, depth: 1 });
  });

  it("ignores a layer number the cube does not have", () => {
    expect(interpretKey(key("4"), ctx(cube(3)))).toBeNull();
  });

  it("picks a generator on a non-cube and turns it with the arrows", () => {
    const layout = cut(PuzzleType.Tetrahedral, 4);
    expect(interpretKey(key("2"), ctx(layout))).toEqual({ kind: "selectGenerator", generator: 1 });
    expect(interpretKey(key("9"), ctx(layout))).toBeNull();
    expect(interpretKey(key("ArrowRight"), ctx(layout, { selectedGenerator: 1 }))).toEqual({ kind: "turnSelected", direction: 1 });
    expect(interpretKey(key("ArrowLeft"), ctx(layout, { selectedGenerator: 1 }))).toEqual({ kind: "turnSelected", direction: -1 });
  });

  it("reaches all twelve Megaminx faces", () => {
    const layout = cut(PuzzleType.Dodecahedral, 12);
    const picked = ["1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "="].map((k) => interpretKey(key(k), ctx(layout)));
    expect(picked.every((c) => c?.kind === "selectGenerator")).toBe(true);
  });

  it("orbits with the arrows when no solution or face is in play", () => {
    expect(interpretKey(key("ArrowLeft"), ctx(cube(3)))).toEqual({ kind: "orbit", yawDeg: KEY_ORBIT_STEP_DEG, pitchDeg: 0 });
    expect(interpretKey(key("ArrowUp"), ctx(cube(3)))).toEqual({ kind: "orbit", yawDeg: 0, pitchDeg: KEY_ORBIT_STEP_DEG });
  });

  it("drives playback with arrows and space only when a solution is loaded", () => {
    const layout = cube(3);
    expect(interpretKey(key(" "), ctx(layout))).toBeNull();
    expect(interpretKey(key(" "), ctx(layout, { hasSolution: true }))).toEqual({ kind: "playPause" });
    expect(interpretKey(key("ArrowRight"), ctx(layout, { hasSolution: true }))).toEqual({ kind: "stepForward" });
    expect(interpretKey(key("ArrowLeft"), ctx(layout, { hasSolution: true }))).toEqual({ kind: "stepBack" });
  });

  it("undoes and redoes with Ctrl or Cmd", () => {
    const layout = cube(3);
    expect(interpretKey(key("z", { ctrlKey: true }), ctx(layout))).toEqual({ kind: "undo" });
    expect(interpretKey(key("z", { metaKey: true }), ctx(layout))).toEqual({ kind: "undo" });
    expect(interpretKey(key("Z", { ctrlKey: true, shiftKey: true }), ctx(layout))).toEqual({ kind: "redo" });
    expect(interpretKey(key("y", { ctrlKey: true }), ctx(layout))).toEqual({ kind: "redo" });
  });

  it("leaves browser shortcuts alone: Ctrl+R, Alt+anything", () => {
    expect(interpretKey(key("r", { ctrlKey: true }), ctx(cube(3)))).toBeNull();
    expect(interpretKey(key("r", { altKey: true }), ctx(cube(3)))).toBeNull();
  });

  it("maps / to the Square-1 slice and nothing else on other puzzles", () => {
    expect(interpretKey(key("/"), ctx(cut(PuzzleType.Square1, 3)))).toEqual({ kind: "turn", move: { generator: 2, amount: 1 } });
    expect(interpretKey(key("/"), ctx(cube(3)))).toBeNull();
  });

  it("handles help, reset view and Escape", () => {
    expect(interpretKey(key("?"), ctx(cube(3)))).toEqual({ kind: "help" });
    expect(interpretKey(key("Home"), ctx(cube(3)))).toEqual({ kind: "resetView" });
    expect(interpretKey(key("Escape"), ctx(cube(3)))).toBeNull();
    expect(interpretKey(key("Escape"), ctx(cube(3), { pendingLayer: 2 }))).toEqual({ kind: "clearSelection" });
  });
});

describe("SHORTCUT_HELP", () => {
  it("has an entry for every scope", () => {
    for (const scope of ["all", "cube", "other"] as const) expect(SHORTCUT_HELP.some((s) => s.scope === scope)).toBe(true);
  });
});

describe("ignoresKey", () => {
  const page = { tag: "BODY", editable: false, role: null };
  it("handles keys on the page itself", () => {
    expect(ignoresKey(page, "r", false)).toBe(false);
  });
  it("leaves typing alone", () => {
    expect(ignoresKey({ tag: "INPUT", editable: false, role: null }, "r", false)).toBe(true);
    expect(ignoresKey({ tag: "DIV", editable: true, role: null }, "r", false)).toBe(true);
    expect(ignoresKey({ tag: "SELECT", editable: false, role: null }, "r", false)).toBe(true);
  });
  it("leaves everything to an open dialog", () => {
    expect(ignoresKey(page, "r", true)).toBe(true);
  });
  it("lets Space and Enter press a focused button, but not other keys through to it", () => {
    const button = { tag: "BUTTON", editable: false, role: null };
    expect(ignoresKey(button, " ", false)).toBe(true);
    expect(ignoresKey(button, "Enter", false)).toBe(true);
    expect(ignoresKey(button, "r", false)).toBe(false);
  });
});
