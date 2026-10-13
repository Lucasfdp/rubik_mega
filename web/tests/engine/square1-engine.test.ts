import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { instantiatePuzzleModule } from "../../src/engine/wasm-module.js";
import { Puzzle } from "../../src/engine/puzzle.js";
import { RemotePuzzle, type WorkerLike } from "../../src/engine/remote-puzzle.js";
import { PuzzleType, SQ1_GEN_BOTTOM, SQ1_GEN_SLICE, SQ1_GEN_TOP, type Move } from "../../src/engine/types.js";
import type { PuzzleWorkerRequest, PuzzleWorkerResponse } from "../../src/engine/worker-protocol.js";
import { handlePuzzleWorkerRequest, type PuzzleRef } from "../../worker/puzzle-worker-logic.js";
import { formatMoveFor, formatSquareOneSequence } from "../../src/ui/move-notation.js";

const WASM_PATH = fileURLToPath(new URL("../../../core/build/rubik_mega.wasm", import.meta.url));
const load = async () => instantiatePuzzleModule(readFileSync(WASM_PATH));

/// A WorkerLike that runs the real worker logic in-process.
function loopback(module: Awaited<ReturnType<typeof load>>): WorkerLike {
  const ref: PuzzleRef = { current: null };
  const listeners = new Set<(event: MessageEvent<PuzzleWorkerResponse>) => void>();
  return {
    postMessage(message) {
      void handlePuzzleWorkerRequest(module, ref, message as PuzzleWorkerRequest).then((data) => {
        for (const listener of listeners) listener({ data } as MessageEvent<PuzzleWorkerResponse>);
      });
    },
    addEventListener(_type, listener) {
      listeners.add(listener);
    },
    removeEventListener(_type, listener) {
      listeners.delete(listener);
    },
    terminate() {},
  };
}

describe("Square-1 through the engine", () => {
  it("reads its layout: three generators with 11, 11 and 1 amounts, 54 facelets, no cut planes", async () => {
    const puzzle = Puzzle.create(await load(), PuzzleType.Square1, 0);
    expect(puzzle.layout.generatorSteps).toEqual([11, 11, 1]);
    expect(puzzle.layout.steps).toBe(11);
    expect(puzzle.layout.faceletCount).toBe(54);
    expect(puzzle.layout.generators).toHaveLength(3);
    expect(puzzle.layout.cut).toBeNull();
    expect(puzzle.stateView()).toHaveLength(25);
    expect(puzzle.unitsView()).toHaveLength(24);
    puzzle.destroy();
  });

  it("every other family still has one steps value for all its generators, and no units", async () => {
    const module = await load();
    for (const [type, size] of [[PuzzleType.Cubic, 3], [PuzzleType.Cubic, 5], [PuzzleType.Skewb, 0], [PuzzleType.Tetrahedral, 0], [PuzzleType.Dodecahedral, 3]] as const) {
      const puzzle = Puzzle.create(module, type, size);
      expect(puzzle.layout.generatorSteps).toHaveLength(puzzle.layout.generators.length);
      expect(new Set(puzzle.layout.generatorSteps)).toEqual(new Set([puzzle.layout.steps]));
      expect(puzzle.unitsView()).toHaveLength(0);
      expect(puzzle.isMoveLegal(0, 1)).toBe(true);
      puzzle.destroy();
    }
  });

  it("a slice is legal on the solved puzzle and blocked after the top turns by 2", async () => {
    const puzzle = Puzzle.create(await load(), PuzzleType.Square1, 0);
    expect(puzzle.isMoveLegal(SQ1_GEN_SLICE, 1)).toBe(true);
    await puzzle.applyMove(SQ1_GEN_TOP, 2);
    expect(puzzle.isMoveLegal(SQ1_GEN_SLICE, 1)).toBe(false);
    expect(puzzle.isMoveLegal(SQ1_GEN_TOP, 5)).toBe(true);
    puzzle.destroy();
  });

  it("the worker snapshot carries units and legality, and RemotePuzzle answers isMoveLegal from it", async () => {
    const remote = await RemotePuzzle.create(loopback(await load()), PuzzleType.Square1, 0);
    expect(remote.unitsView()).toHaveLength(24);
    expect(remote.isMoveLegal(SQ1_GEN_SLICE, 1)).toBe(true);
    await remote.applyMove(SQ1_GEN_TOP, 2);
    expect(remote.isMoveLegal(SQ1_GEN_SLICE, 1)).toBe(false);
    expect(remote.isMoveLegal(SQ1_GEN_BOTTOM, 3)).toBe(true);
    expect(remote.isMoveLegal(SQ1_GEN_TOP, 11)).toBe(true);
    await remote.applyMove(SQ1_GEN_TOP, 10); // back to the cube shape
    expect(remote.isMoveLegal(SQ1_GEN_SLICE, 1)).toBe(true);
    remote.destroy();
  });

  it("RemotePuzzle's legality and units agree with the in-process Puzzle after random moves", async () => {
    const module = await load();
    const remote = await RemotePuzzle.create(loopback(module), PuzzleType.Square1, 0);
    const local = Puzzle.create(module, PuzzleType.Square1, 0);
    let seed = 12345;
    const next = (n: number): number => {
      seed = (Math.imul(seed, 1103515245) + 12345) >>> 0;
      return (seed >>> 8) % n;
    };
    for (let i = 0; i < 60; i++) {
      const generator = next(3);
      const amount = 1 + next(generator === SQ1_GEN_SLICE ? 1 : 11);
      await remote.applyMove(generator, amount);
      await local.applyMove(generator, amount);
      expect(remote.stateView()).toEqual(local.stateView());
      expect(remote.unitsView()).toEqual(local.unitsView());
      expect(remote.isMoveLegal(SQ1_GEN_SLICE, 1)).toBe(local.isMoveLegal(SQ1_GEN_SLICE, 1));
    }
    remote.destroy();
    local.destroy();
  });

  it("solves 20 scrambles through the compiled module: legal moves, tags 0 then 1, solved at the end", async () => {
    const module = await load();
    for (let seed = 1; seed <= 20; seed++) {
      const puzzle = Puzzle.create(module, PuzzleType.Square1, 0);
      await puzzle.scramble(25, seed * 7919);
      expect(await puzzle.algorithmName(0)).toBe("Two-phase (near-optimal)");
      const result = await puzzle.solve(0);
      expect(result).not.toBeNull();
      const { moves, phaseTags } = result!;
      expect(phaseTags.every((tag, i) => tag <= 1 && (i === 0 || tag >= phaseTags[i - 1]!))).toBe(true);
      for (const move of moves) {
        expect(puzzle.isMoveLegal(move.generator, move.amount)).toBe(true);
        await puzzle.applyMove(move.generator, move.amount);
      }
      const solved = Puzzle.create(module, PuzzleType.Square1, 0);
      expect(puzzle.stateView()).toEqual(solved.stateView());
      expect(puzzle.faceletView()).toEqual(solved.faceletView());
      solved.destroy();
      puzzle.destroy();
    }
  }, 30_000); // 20 two-phase solves take ~6 s; vitest's default is 5 s.
});

describe("Square-1 notation", () => {
  const layout = { type: PuzzleType.Square1, generators: [{}, {}, {}] } as never;

  it("writes each move on its own as WCA does, amounts -5..6", () => {
    expect(formatMoveFor({ generator: SQ1_GEN_TOP, amount: 3 }, layout)).toBe("(3,0)");
    expect(formatMoveFor({ generator: SQ1_GEN_TOP, amount: 6 }, layout)).toBe("(6,0)");
    expect(formatMoveFor({ generator: SQ1_GEN_TOP, amount: 7 }, layout)).toBe("(-5,0)");
    expect(formatMoveFor({ generator: SQ1_GEN_BOTTOM, amount: 11 }, layout)).toBe("(0,-1)");
    expect(formatMoveFor({ generator: SQ1_GEN_SLICE, amount: 1 }, layout)).toBe("/");
    expect(() => formatMoveFor({ generator: SQ1_GEN_TOP, amount: 12 }, layout)).toThrow(/out of range/);
  });

  it("folds consecutive turns into (x,y) before each slice", () => {
    const moves: Move[] = [
      { generator: SQ1_GEN_TOP, amount: 3 },
      { generator: SQ1_GEN_BOTTOM, amount: 11 },
      { generator: SQ1_GEN_SLICE, amount: 1 },
      { generator: SQ1_GEN_BOTTOM, amount: 2 },
      { generator: SQ1_GEN_TOP, amount: 9 },
      { generator: SQ1_GEN_TOP, amount: 5 }, // 9 + 5 = 14 = 2 mod 12
      { generator: SQ1_GEN_SLICE, amount: 1 },
      { generator: SQ1_GEN_TOP, amount: 4 },
      { generator: SQ1_GEN_TOP, amount: 8 }, // cancels
      { generator: SQ1_GEN_SLICE, amount: 1 },
    ];
    expect(formatSquareOneSequence(moves)).toBe("(3,-1) / (2,2) / /");
  });

  it("a sequence ending in turns keeps them", () => {
    expect(formatSquareOneSequence([{ generator: SQ1_GEN_SLICE, amount: 1 }, { generator: SQ1_GEN_BOTTOM, amount: 6 }])).toBe("/ (0,6)");
  });
});
