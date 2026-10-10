import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { describe, expect, it, vi } from "vitest";
import { phaseName, phaseNamesForAlgorithm } from "../../src/ui/phase-names.js";
import { instantiatePuzzleModule } from "../../src/engine/wasm-module.js";
import { Puzzle } from "../../src/engine/puzzle.js";
import { PuzzleType } from "../../src/engine/types.js";

const WASM_PATH = fileURLToPath(new URL("../../../core/build/rubik_mega.wasm", import.meta.url));

describe("phaseNamesForAlgorithm / phaseName", () => {
  // registry.c's kociemba_algorithm_solve genuinely emits two phase
  // tag values -- a table with only one entry here is exactly the bug
  // that froze real Kociemba playback partway through every solve.
  it("Kociemba has 2 phases; 2x2x2 has exactly one", () => {
    expect(phaseNamesForAlgorithm("Kociemba (two-phase)")).toHaveLength(2);
    expect(phaseNamesForAlgorithm("2x2x2 (corners only)")).toHaveLength(1);
  });

  it("Thistlethwaite and layer-by-layer have 4 phases; reduction has 5", () => {
    expect(phaseNamesForAlgorithm("Thistlethwaite (four-phase)")).toHaveLength(4);
    expect(phaseNamesForAlgorithm("Layer-by-layer (beginner)")).toHaveLength(4);
    expect(phaseNamesForAlgorithm("Reduction (corners, edges, centres)")).toHaveLength(5);
  });

  it("Skewb has 1 phase; Pyraminx has 2 (layers, then tips)", () => {
    expect(phaseNamesForAlgorithm("Optimal (full depth table)")).toHaveLength(1);
    expect(phaseNamesForAlgorithm("Optimal, then tips (full depth table)")).toEqual(["Edges and axial pieces", "Tips"]);
  });

  it("the staged solvers name every stage: Skewb 3, Pyraminx 4 (tips last)", () => {
    expect(phaseNamesForAlgorithm("Face-first (staged)")).toEqual(["First-face corners", "First-face centre", "Finish"]);
    const pyraminx = phaseNamesForAlgorithm("Layer-first (staged)");
    expect(pyraminx).toHaveLength(4);
    expect(pyraminx[3]).toBe("Tips");
  });

  it("phaseName() returns the matching entry", () => {
    expect(phaseName("Kociemba (two-phase)", 1)).toMatch(/G1/);
    expect(phaseName("Thistlethwaite (four-phase)", 0)).toMatch(/orient edges/);
    expect(phaseName("Layer-by-layer (beginner)", 3)).toMatch(/last layer/i);
    expect(phaseName("Reduction (corners, edges, centres)", 4)).toBe("Centres");
  });

  it("every algorithm core registers, at every size, has a phase table here (names must match core exactly)", async () => {
    const module = await instantiatePuzzleModule(readFileSync(WASM_PATH));
    for (const n of [3, 4, 5, 6, 7, 8]) {
      const puzzle = Puzzle.create(module, PuzzleType.Cubic, n);
      const count = await puzzle.algorithmCount();
      expect(count).toBeGreaterThan(0);
      for (let i = 0; i < count; i++) {
        const name = await puzzle.algorithmName(i);
        expect(phaseNamesForAlgorithm(name).length).toBeGreaterThan(0);
      }
      puzzle.destroy();
    }
  });

  it("Skewb and Pyraminx: every algorithm core registers has a phase table here", async () => {
    const module = await instantiatePuzzleModule(readFileSync(WASM_PATH));
    for (const type of [PuzzleType.Skewb, PuzzleType.Tetrahedral]) {
      const puzzle = Puzzle.create(module, type, 0);
      const count = await puzzle.algorithmCount();
      expect(count).toBe(2);
      for (let i = 0; i < count; i++) {
        expect(phaseNamesForAlgorithm(await puzzle.algorithmName(i)).length).toBeGreaterThan(0);
      }
      puzzle.destroy();
    }
  });

  it("Kilominx and Megaminx: every algorithm core registers has a phase table of the right length", async () => {
    const module = await instantiatePuzzleModule(readFileSync(WASM_PATH));
    for (const [size, phases] of [[2, 1], [3, 2]] as const) {
      const puzzle = Puzzle.create(module, PuzzleType.Dodecahedral, size);
      expect(await puzzle.algorithmCount()).toBe(1);
      expect(phaseNamesForAlgorithm(await puzzle.algorithmName(0))).toHaveLength(phases);
      puzzle.destroy();
    }
  });

  it("Square-1: core's one algorithm has its two phases named here", async () => {
    const module = await instantiatePuzzleModule(readFileSync(WASM_PATH));
    const puzzle = Puzzle.create(module, PuzzleType.Square1, 0);
    expect(await puzzle.algorithmCount()).toBe(1);
    expect(phaseNamesForAlgorithm(await puzzle.algorithmName(0))).toEqual(["Cube shape", "Finish"]);
    puzzle.destroy();
  });

  it("throws for an unknown algorithm name", () => {
    expect(() => phaseNamesForAlgorithm("Nope")).toThrow(/no phase names recorded/);
  });

  // phaseName() must never throw: it runs inside SolvePlayback's
  // onSettled, before #tryAdvance(), so a throw freezes autoplay.
  it("falls back to a generic label (and logs) for an unknown phase or algorithm, instead of throwing", () => {
    const errorSpy = vi.spyOn(console, "error").mockImplementation(() => {});
    expect(phaseName("Kociemba (two-phase)", 99)).toBe("Solving");
    expect(phaseName("Nope", 0)).toBe("Solving");
    expect(errorSpy).toHaveBeenCalledTimes(2);
    errorSpy.mockRestore();
  });
});
