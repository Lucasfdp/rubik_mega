import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { instantiatePuzzleModule } from "../../src/engine/wasm-module.js";
import { Puzzle } from "../../src/engine/puzzle.js";
import { PuzzleType } from "../../src/engine/types.js";

const WASM_PATH = fileURLToPath(
  new URL("../../../core/build/rubik_mega.wasm", import.meta.url),
);

async function loadModule() {
  return instantiatePuzzleModule(readFileSync(WASM_PATH));
}

/// True if every face of a Skewb or Pyraminx shows one colour -- their
/// notion of solved (D1-A: for a Skewb, uniform faces, not the identity).
function facesUniform(puzzle: Puzzle): boolean {
  const faceColours = new Map<string, Set<number>>();
  const { facelets } = puzzle.layout.cut!;
  puzzle.faceletView().forEach((colour, i) => {
    const key = facelets[i]!.normal.join(",");
    faceColours.set(key, (faceColours.get(key) ?? new Set<number>()).add(colour));
  });
  return [...faceColours.values()].every((colours) => colours.size === 1);
}

describe("Puzzle", () => {
  it("creates a 3x3x3 cubic puzzle with a 40-byte state buffer", async () => {
    const module = await loadModule();
    const puzzle = Puzzle.create(module, PuzzleType.Cubic, 3);
    expect(puzzle.stateView().length).toBe(40);
    puzzle.destroy();
  });

  it("rejects an unsupported (type, size) pair instead of returning a dead handle", async () => {
    const module = await loadModule();
    // core/src/puzzle.c: `size < 3` is still rejected for every puzzle
    // family (the only bound that still holds -- a cubic puzzle now
    // supports any size >= 3, see the NxN test right below, which
    // superseded the old "only size 3 works" assumption this test used
    // to encode; HANDOFF.md, 2026-10-04).
    expect(() => Puzzle.create(module, PuzzleType.Cubic, 2)).toThrow(/puzzle_create failed/);
  });

  it("creates a cubic puzzle for sizes beyond 3 too (Phase 1's NxN support)", async () => {
    const module = await loadModule();
    const puzzle = Puzzle.create(module, PuzzleType.Cubic, 4);
    // A 4x4x4 has no fixed centres/parity-constrained edges the way a
    // 3x3x3's shape does, so its state buffer is a different (larger)
    // size -- this only asserts creation succeeds and produces a live,
    // usable handle, not any particular byte layout.
    expect(puzzle.stateView().length).toBeGreaterThan(40);
    puzzle.destroy();
  });

  it("applyMove mutates the live state buffer in place (zero-copy)", async () => {
    const module = await loadModule();
    const puzzle = Puzzle.create(module, PuzzleType.Cubic, 3);
    const view = puzzle.stateView();
    const before = Array.from(view);
    puzzle.applyMove(0, 1);
    // Same underlying view, contents changed -- not a copy.
    expect(Array.from(view)).not.toEqual(before);
    expect(Array.from(puzzle.stateView())).toEqual(Array.from(view));
    puzzle.destroy();
  });

  it("scramble() changes state; the same seed is reproducible", async () => {
    const module = await loadModule();
    const puzzleA = Puzzle.create(module, PuzzleType.Cubic, 3);
    const solved = Array.from(puzzleA.stateView());
    puzzleA.scramble(20, 42);
    const scrambledA = Array.from(puzzleA.stateView());
    expect(scrambledA).not.toEqual(solved);

    const puzzleB = Puzzle.create(module, PuzzleType.Cubic, 3);
    puzzleB.scramble(20, 42);
    expect(Array.from(puzzleB.stateView())).toEqual(scrambledA);

    puzzleA.destroy();
    puzzleB.destroy();
  });

  it("throws once destroyed, and destroy() itself is idempotent", async () => {
    const module = await loadModule();
    const puzzle = Puzzle.create(module, PuzzleType.Cubic, 3);
    puzzle.destroy();
    expect(() => puzzle.destroy()).not.toThrow();
    expect(() => puzzle.applyMove(0, 1)).toThrow(/used after destroy/);
    expect(() => puzzle.stateView()).toThrow(/used after destroy/);
  });

  it("algorithmCount/algorithmName expose the 4 built cubic 3x3x3 algorithms", async () => {
    const module = await loadModule();
    const puzzle = Puzzle.create(module, PuzzleType.Cubic, 3);
    await expect(puzzle.algorithmCount()).resolves.toBe(4);
    await expect(puzzle.algorithmName(0)).resolves.toBe("Kociemba (two-phase)");
    await expect(puzzle.algorithmName(1)).resolves.toBe("Thistlethwaite (four-phase)");
    await expect(puzzle.algorithmName(2)).resolves.toBe("Layer-by-layer (beginner)");
    await expect(puzzle.algorithmName(3)).resolves.toBe("2x2x2 (corners only)");
    puzzle.destroy();
  });

  it("solveRaw passes straight through to puzzle_solve", async () => {
    const module = await loadModule();
    const puzzle = Puzzle.create(module, PuzzleType.Cubic, 3);
    // Scrambled first -- a solved cube needs 0 moves from every
    // algorithm, which wouldn't exercise the real output-decoding path.
    await puzzle.scramble(20, 1);
    const movesPtr = module.memory.buffer.byteLength;
    module.memory.grow(1);
    const phaseTagsPtr = movesPtr + 64 * 4;
    const count = puzzle.solveRaw(0, movesPtr, 64, phaseTagsPtr);
    expect(count).toBeGreaterThan(0);
    expect(count).not.toBe(0xffffffff);
    puzzle.destroy();
  });

  it("solve() decodes a real solution and applying it returns the cube to solved", async () => {
    const module = await loadModule();
    const puzzle = Puzzle.create(module, PuzzleType.Cubic, 3);
    const solved = Array.from(puzzle.stateView());
    await puzzle.scramble(25, 7);
    const result = await puzzle.solve(0); // Kociemba
    expect(result).not.toBeNull();
    if (!result) throw new Error("unreachable");
    expect(result.moves.length).toBeGreaterThan(0);
    expect(result.moves.length).toBe(result.phaseTags.length);
    for (const move of result.moves) {
      await puzzle.applyMove(move.generator, move.amount);
    }
    expect(Array.from(puzzle.stateView())).toEqual(solved);
    puzzle.destroy();
  });

  it("solve() on an already-solved cube returns zero moves, not null", async () => {
    const module = await loadModule();
    const puzzle = Puzzle.create(module, PuzzleType.Cubic, 3);
    const result = await puzzle.solve(0);
    expect(result).toEqual({ moves: [], phaseTags: [] });
    puzzle.destroy();
  });

  it("layout matches core's own facelet geometry and generator table (D1)", async () => {
    const module = await loadModule();
    for (const n of [3, 4, 5, 8]) {
      const puzzle = Puzzle.create(module, PuzzleType.Cubic, n);
      const { layout } = puzzle;
      expect(layout.size).toBe(n);
      expect(layout.facelets).toHaveLength(6 * n * n);
      // Every facelet sits on the face it says it points out of.
      for (const { x, y, z, face } of layout.facelets) {
        const onFace = [y === n - 1, x === n - 1, z === n - 1, y === 0, x === 0, z === 0][face];
        expect(onFace).toBe(true);
      }
      const perFace = n === 3 ? 1 : Math.floor((n - 1) / 2) + 1;
      expect(layout.generators).toHaveLength(6 * perFace);
      layout.generators.forEach(({ face, depth }, g) => expect(face * perFace + depth).toBe(g));
      puzzle.destroy();
    }
  });

  it("faceletView(): solved shows each face's own colour; every colour stays at n*n after a scramble", async () => {
    const module = await loadModule();
    const puzzle = Puzzle.create(module, PuzzleType.Cubic, 5);
    expect(Array.from(puzzle.faceletView())).toEqual(puzzle.layout.facelets.map((f) => f.face));
    await puzzle.scramble(60, 7);
    const counts = new Array<number>(6).fill(0);
    for (const colour of puzzle.faceletView()) counts[colour]!++;
    expect(counts).toEqual(new Array<number>(6).fill(25));
    puzzle.destroy();
  });

  it("NxN: reduction solves a scrambled 5x5x5 and 8x8x8 through the real module", async () => {
    const module = await loadModule();
    for (const n of [5, 8]) {
      const puzzle = Puzzle.create(module, PuzzleType.Cubic, n);
      expect(await puzzle.algorithmCount()).toBe(1);
      await puzzle.scramble(200, 99);
      const result = await puzzle.solve(0);
      expect(result).not.toBeNull();
      expect(result!.moves.length).toBeGreaterThan(0);
      await puzzle.applyMoves(result!.moves);
      expect(Array.from(puzzle.faceletView())).toEqual(puzzle.layout.facelets.map((f) => f.face));
      puzzle.destroy();
    }
  });

  // Phase 3 increments 4 and 5 (docs/en/09-phase3-skewb-pyraminx.md,
  // docs/en/10-phase3-human-methods.md): the exit gate's "Scramble ->
  // Solve plays to the end", through the real WASM module, for BOTH
  // algorithms of each family. Skewb's "solved" is uniform faces, not
  // the identity state (D1-A), so this checks the facelets, as the UI
  // would show them. `phases` is how many phase tags the algorithm uses
  // and `maxMoves` its proven cap (stage depths summed).
  it("Skewb and Pyraminx: every algorithm solves a scrambled puzzle to uniform faces, phase tags in range and never decreasing", async () => {
    const module = await loadModule();
    const cases = [
      { type: PuzzleType.Skewb, algorithms: [{ phases: 1, maxMoves: 16 }, { phases: 3, maxMoves: 24 }] },
      { type: PuzzleType.Tetrahedral, algorithms: [{ phases: 2, maxMoves: 16 }, { phases: 4, maxMoves: 28 }] },
    ];
    for (const { type, algorithms } of cases) {
      const puzzle = Puzzle.create(module, type, 0);
      expect(await puzzle.algorithmCount()).toBe(algorithms.length);
      for (const [algorithm, { phases, maxMoves }] of algorithms.entries()) {
        for (const seed of [3, 11, 29]) {
          await puzzle.scramble(40, seed);
          const result = await puzzle.solve(algorithm);
          expect(result).not.toBeNull();
          expect(result!.moves.length).toBeGreaterThan(0);
          expect(result!.moves.length).toBeLessThanOrEqual(maxMoves);
          result!.phaseTags.forEach((tag) => expect(tag).toBeLessThan(phases));
          result!.phaseTags.forEach((tag, i) => {
            if (i > 0) expect(tag).toBeGreaterThanOrEqual(result!.phaseTags[i - 1]!);
          });
          await puzzle.applyMoves(result!.moves);
          expect(facesUniform(puzzle)).toBe(true);
        }
      }
      puzzle.destroy();
    }
  });

  // Phase 3 increment 5's exit gate, the compiled-WASM half: 200 random
  // scrambles per algorithm per family, each solved to uniform faces
  // with phase tags in range and non-decreasing. (The native half is
  // core/tests/test_{pyraminx,skewb}_staged.c, which also asserts what
  // each phase promised; from here only the facelets are visible.)
  it("Skewb and Pyraminx: 200 scrambles per algorithm solve through the compiled module", async () => {
    const module = await loadModule();
    const cases = [
      { type: PuzzleType.Skewb, phases: [1, 3] },
      { type: PuzzleType.Tetrahedral, phases: [2, 4] },
    ];
    for (const { type, phases } of cases) {
      const puzzle = Puzzle.create(module, type, 0);
      for (const [algorithm, phaseCount] of phases.entries()) {
        for (let seed = 1; seed <= 200; seed++) {
          await puzzle.scramble(40, seed);
          const result = await puzzle.solve(algorithm);
          expect(result).not.toBeNull();
          result!.phaseTags.forEach((tag, i) => {
            expect(tag).toBeLessThan(phaseCount);
            if (i > 0) expect(tag).toBeGreaterThanOrEqual(result!.phaseTags[i - 1]!);
          });
          await puzzle.applyMoves(result!.moves);
          expect(facesUniform(puzzle)).toBe(true);
        }
      }
      puzzle.destroy();
    }
  });

  // Phase 4 increment 6 (docs/en/11-phase4-dodecahedral.md): the
  // compiled-WASM half of the Megaminx and Kilominx solvers. "Solved"
  // here is the facelet colours of a fresh puzzle (centres never move, so
  // that is the identity state's picture), and the native half --
  // core/tests/test_dodeca_solve.c -- covers 200 scrambles with the
  // measured length caps.
  it("Kilominx and Megaminx: scrambled puzzles solve through the compiled module", async () => {
    const module = await loadModule();
    for (const { size, phases } of [{ size: 2, phases: 1 }, { size: 3, phases: 2 }]) {
      const puzzle = Puzzle.create(module, PuzzleType.Dodecahedral, size);
      const solved = Array.from(puzzle.faceletView());
      expect(await puzzle.algorithmCount()).toBe(1);
      for (let seed = 1; seed <= 25; seed++) {
        await puzzle.scramble(size === 2 ? 40 : 70, seed);
        expect(Array.from(puzzle.faceletView())).not.toEqual(solved);
        const result = await puzzle.solve(0);
        expect(result).not.toBeNull();
        result!.phaseTags.forEach((tag, i) => {
          expect(tag).toBeLessThan(phases);
          if (i > 0) expect(tag).toBeGreaterThanOrEqual(result!.phaseTags[i - 1]!);
        });
        await puzzle.applyMoves(result!.moves);
        expect(Array.from(puzzle.faceletView())).toEqual(solved);
      }
      puzzle.destroy();
    }
  });

  // The arena is a bump allocator that is reset when a puzzle is
  // destroyed; creating and destroying dodecahedral puzzles (the solver
  // tables are static, not arena) must never run it out.
  it("Kilominx and Megaminx: create, solve and destroy repeatedly without exhausting the arena", async () => {
    const module = await loadModule();
    for (let round = 0; round < 12; round++) {
      const size = round % 2 === 0 ? 2 : 3;
      const puzzle = Puzzle.create(module, PuzzleType.Dodecahedral, size);
      await puzzle.scramble(30, round + 1);
      expect(await puzzle.solve(0)).not.toBeNull();
      puzzle.destroy();
    }
  });
});
