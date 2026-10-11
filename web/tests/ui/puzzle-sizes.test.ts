import { describe, expect, it } from "vitest";
import { PuzzleType } from "../../src/engine/types.js";
import { DEFAULT_SIZE, PUZZLE_CHOICES, SUPPORTED_SIZES, choiceIndexFor, puzzleTitle, scrambleMovesFor, scrambleMovesForPuzzle } from "../../src/ui/puzzle-sizes.js";

describe("puzzle sizes", () => {
  it("offers 2..8 and opens on 3", () => {
    expect(SUPPORTED_SIZES).toEqual([2, 3, 4, 5, 6, 7, 8]);
    expect(SUPPORTED_SIZES).toContain(DEFAULT_SIZE);
  });

  it("scramble lengths follow the WCA table (D4), 3x3x3 unchanged", () => {
    expect(SUPPORTED_SIZES.map(scrambleMovesFor)).toEqual([15, 25, 40, 60, 80, 100, 120]);
  });

  it("rejects a size with no entry", () => {
    expect(() => scrambleMovesFor(9)).toThrow(/unsupported size/);
  });

  it("offers Megaminx (size 3) and Kilominx (size 2) as two entries of one family", () => {
    const dodeca = PUZZLE_CHOICES.filter((c) => c.type === PuzzleType.Dodecahedral);
    expect(dodeca.map((c) => [c.label, c.size, c.hasSize])).toEqual([["Megaminx", 3, false], ["Kilominx", 2, false]]);
    expect(PUZZLE_CHOICES[choiceIndexFor(PuzzleType.Dodecahedral, 2)]!.label).toBe("Kilominx");
    expect(PUZZLE_CHOICES[choiceIndexFor(PuzzleType.Dodecahedral, 3)]!.label).toBe("Megaminx");
    expect(PUZZLE_CHOICES[choiceIndexFor(PuzzleType.Cubic, 7)]!.label).toBe("Cube");
    expect(PUZZLE_CHOICES[choiceIndexFor(PuzzleType.Skewb, 0)]!.label).toBe("Skewb");
  });

  it("offers Square-1 as its own entry, scrambled by 25 slices", () => {
    expect(PUZZLE_CHOICES[choiceIndexFor(PuzzleType.Square1, 0)]!.label).toBe("Square-1");
    expect(scrambleMovesForPuzzle(PuzzleType.Square1, 0)).toBe(25);
  });

  it("scrambles a Megaminx longer than a Kilominx, and both longer than a Skewb", () => {
    const mega = scrambleMovesForPuzzle(PuzzleType.Dodecahedral, 3);
    const kilo = scrambleMovesForPuzzle(PuzzleType.Dodecahedral, 2);
    expect(mega).toBeGreaterThan(kilo);
    expect(kilo).toBeGreaterThan(scrambleMovesForPuzzle(PuzzleType.Skewb, 0));
  });
});

describe("puzzleTitle", () => {
  it("names a cube with its size and every other puzzle by name", () => {
    expect(puzzleTitle(PuzzleType.Cubic, 4)).toBe("Cube 4\u00d74\u00d74");
    expect(puzzleTitle(PuzzleType.Dodecahedral, 2)).toBe("Kilominx");
    expect(puzzleTitle(PuzzleType.Square1, 0)).toBe("Square-1");
  });

  it("every choice has a distinct picture", () => {
    const glyphs = PUZZLE_CHOICES.map((c) => c.glyph);
    expect(new Set(glyphs).size).toBe(glyphs.length);
  });
});
