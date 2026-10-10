import { describe, expect, it } from "vitest";
import { PuzzleType } from "../../src/engine/types.js";
import { buildLink, parseLink } from "../../src/ui/url-state.js";
import { puzzleFromKey, puzzleKey, PUZZLE_CHOICES } from "../../src/ui/puzzle-sizes.js";

describe("puzzleKey / puzzleFromKey", () => {
  it("round-trips every puzzle the picker offers, and every cube size", () => {
    for (const choice of PUZZLE_CHOICES) {
      expect(puzzleFromKey(puzzleKey(choice.type, choice.size))).toEqual({ type: choice.type, size: choice.size });
    }
    for (const n of [3, 4, 5, 6, 7, 8]) expect(puzzleFromKey(puzzleKey(PuzzleType.Cubic, n))).toEqual({ type: PuzzleType.Cubic, size: n });
  });

  it("tells Megaminx from Kilominx, which share a puzzle type", () => {
    expect(puzzleFromKey("megaminx")).toEqual({ type: PuzzleType.Dodecahedral, size: 3 });
    expect(puzzleFromKey("kilominx")).toEqual({ type: PuzzleType.Dodecahedral, size: 2 });
  });

  it("rejects names and sizes that mean nothing", () => {
    for (const bad of ["", "dodecahedron", "cube:2", "cube:9", "cube:x", "skewb:3", "cube:3:1"]) expect(puzzleFromKey(bad), bad).toBeNull();
  });
});

describe("parseLink", () => {
  it("reads a puzzle name and a cube size", () => {
    expect(parseLink("?p=megaminx").puzzle).toEqual({ type: PuzzleType.Dodecahedral, size: 3 });
    expect(parseLink("?p=cube&n=5").puzzle).toEqual({ type: PuzzleType.Cubic, size: 5 });
    expect(parseLink("p=cube").puzzle).toEqual({ type: PuzzleType.Cubic, size: 3 });
  });

  it("reads a shared scramble, with the puzzle's own length if none is given", () => {
    expect(parseLink("?p=cube&n=4&s=12345&m=40").scramble).toEqual({ seed: 12345, moves: 40 });
    expect(parseLink("?p=cube&n=4&s=12345").scramble).toEqual({ seed: 12345, moves: 40 });
    expect(parseLink("?p=skewb&s=0").scramble).toEqual({ seed: 0, moves: 25 });
  });

  it("ignores a link it cannot make sense of, rather than failing", () => {
    expect(parseLink("")).toEqual({ puzzle: null, scramble: null });
    expect(parseLink("?p=nonsense&s=5")).toEqual({ puzzle: null, scramble: null });
    expect(parseLink("?p=cube&n=99").puzzle).toEqual({ type: PuzzleType.Cubic, size: 3 });
    expect(parseLink("?p=cube&s=-1").scramble).toBeNull();
    expect(parseLink("?p=cube&s=4294967296").scramble).toBeNull();
    expect(parseLink("?p=cube&s=abc").scramble).toBeNull();
  });

  it("does not replay a scramble for a link with no puzzle", () => {
    expect(parseLink("?s=5").scramble).toBeNull();
  });
});

describe("buildLink", () => {
  const cube5 = { type: PuzzleType.Cubic, size: 5 };

  it("names the puzzle", () => {
    expect(buildLink("", cube5, null)).toBe("?p=cube&n=5");
    expect(buildLink("", { type: PuzzleType.Square1, size: 0 }, null)).toBe("?p=square1");
  });

  it("adds a scramble, and replaces an earlier one", () => {
    expect(buildLink("?p=cube&n=3&s=1&m=25", cube5, { seed: 99, moves: 60 })).toBe("?p=cube&n=5&s=99&m=60");
    expect(buildLink("?p=cube&n=3&s=1&m=25", cube5, null)).toBe("?p=cube&n=5");
  });

  it("keeps parameters it does not own", () => {
    expect(buildLink("?debug", cube5, null)).toContain("debug");
  });

  it("is read back by parseLink to the same request", () => {
    const link = buildLink("", { type: PuzzleType.Dodecahedral, size: 2 }, { seed: 4000000000, moves: 40 });
    expect(parseLink(link)).toEqual({ puzzle: { type: PuzzleType.Dodecahedral, size: 2 }, scramble: { seed: 4000000000, moves: 40 } });
  });
});
