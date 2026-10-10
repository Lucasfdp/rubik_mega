import { describe, expect, it } from "vitest";
import { formatMove, formatMoveSequence, solutionText } from "../../src/ui/move-notation.js";
import { CUBIC_GEN_U, CUBIC_GEN_R, CUBIC_GEN_F, CUBIC_GEN_D, CUBIC_GEN_L, CUBIC_GEN_B } from "../../src/interaction/move-axis.js";
import { PuzzleType, type GeneratorLayer, type PuzzleLayout } from "../../src/engine/types.js";

/// A 3x3x3's generator table: one generator per face, depth 0.
const LAYERS_3: GeneratorLayer[] = [0, 1, 2, 3, 4, 5].map((face) => ({ face, depth: 0 }));
/// A 5x5x5's: face-major, depths 0..2 (core's cubic_generator_index()).
const LAYERS_5: GeneratorLayer[] = [0, 1, 2, 3, 4, 5].flatMap((face) => [0, 1, 2].map((depth) => ({ face, depth })));

describe("formatMove", () => {
  it("renders amount 1/2/3 as the bare letter / double / prime", () => {
    expect(formatMove({ generator: CUBIC_GEN_R, amount: 1 }, LAYERS_3)).toBe("R");
    expect(formatMove({ generator: CUBIC_GEN_R, amount: 2 }, LAYERS_3)).toBe("R2");
    expect(formatMove({ generator: CUBIC_GEN_R, amount: 3 }, LAYERS_3)).toBe("R'");
  });

  it("covers all 6 faces with the right letter", () => {
    expect(formatMove({ generator: CUBIC_GEN_U, amount: 1 }, LAYERS_3)).toBe("U");
    expect(formatMove({ generator: CUBIC_GEN_F, amount: 1 }, LAYERS_3)).toBe("F");
    expect(formatMove({ generator: CUBIC_GEN_D, amount: 1 }, LAYERS_3)).toBe("D");
    expect(formatMove({ generator: CUBIC_GEN_L, amount: 1 }, LAYERS_3)).toBe("L");
    expect(formatMove({ generator: CUBIC_GEN_B, amount: 1 }, LAYERS_3)).toBe("B");
  });

  it("inner layers get their layer number (face = 1), outer layers stay bare (D6)", () => {
    expect(formatMove({ generator: 1 * 3 + 0, amount: 1 }, LAYERS_5)).toBe("R");
    expect(formatMove({ generator: 1 * 3 + 1, amount: 3 }, LAYERS_5)).toBe("2R'");
    expect(formatMove({ generator: 3 * 3 + 2, amount: 2 }, LAYERS_5)).toBe("3D2");
  });

  it("throws on an out-of-range generator or amount", () => {
    expect(() => formatMove({ generator: 6, amount: 1 }, LAYERS_3)).toThrow(/out of range/);
    expect(() => formatMove({ generator: CUBIC_GEN_U, amount: 0 }, LAYERS_3)).toThrow(/out of range/);
    expect(() => formatMove({ generator: CUBIC_GEN_U, amount: 4 }, LAYERS_3)).toThrow(/out of range/);
  });
});

describe("formatMoveSequence", () => {
  it("joins moves with single spaces", () => {
    const moves = [
      { generator: CUBIC_GEN_R, amount: 1 },
      { generator: CUBIC_GEN_U, amount: 1 },
      { generator: CUBIC_GEN_R, amount: 3 },
      { generator: CUBIC_GEN_U, amount: 3 },
    ];
    expect(formatMoveSequence(moves, LAYERS_3)).toBe("R U R' U'");
  });

  it("renders an empty sequence as an empty string", () => {
    expect(formatMoveSequence([], LAYERS_3)).toBe("");
  });
});

describe("solutionText", () => {
  it("joins a cube solution's moves with spaces", () => {
    const layout = { type: PuzzleType.Cubic, size: 3, generators: LAYERS_3 } as unknown as PuzzleLayout;
    const moves = [
      { generator: CUBIC_GEN_R, amount: 1 },
      { generator: CUBIC_GEN_U, amount: 3 },
      { generator: CUBIC_GEN_F, amount: 2 },
    ];
    expect(solutionText(moves, layout)).toBe("R U' F2");
    expect(solutionText([], layout)).toBe("");
  });

  it("writes a Square-1 solution as folded (top,bottom) pairs and slashes", () => {
    const layout = { type: PuzzleType.Square1, size: 0, generators: [{}, {}, {}] } as unknown as PuzzleLayout;
    const moves = [
      { generator: 0, amount: 3 },
      { generator: 1, amount: 0 + 2 },
      { generator: 2, amount: 1 },
      { generator: 0, amount: 11 },
    ];
    expect(solutionText(moves, layout)).toMatch(/^\(3,-?2\) \/ \(-1,0\)$/);
  });
});
