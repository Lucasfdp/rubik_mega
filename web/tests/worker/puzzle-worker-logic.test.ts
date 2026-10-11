import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { instantiatePuzzleModule } from "../../src/engine/wasm-module.js";
import { PuzzleType } from "../../src/engine/types.js";
import { handlePuzzleWorkerRequest, type PuzzleRef } from "../../worker/puzzle-worker-logic.js";

const WASM_PATH = fileURLToPath(
  new URL("../../../core/build/rubik_mega.wasm", import.meta.url),
);

async function loadModule() {
  return instantiatePuzzleModule(readFileSync(WASM_PATH));
}

describe("handlePuzzleWorkerRequest", () => {
  it("\"create\" makes a live puzzle and returns its initial state", async () => {
    const module = await loadModule();
    const ref: PuzzleRef = { current: null };

    const response = await handlePuzzleWorkerRequest(module, ref, {
      id: 0,
      kind: "create",
      puzzleType: PuzzleType.Cubic,
      size: 3,
    });

    expect(response).toEqual({
      id: 0,
      kind: "created",
      state: expect.any(Uint8Array),
      facelets: expect.any(Uint8Array),
      units: expect.any(Uint8Array),
      legal: expect.any(Uint8Array),
      solved: true,
      layout: expect.any(Object),
    });
    expect(ref.current).not.toBeNull();
    if (response.kind !== "created") throw new Error("expected a created response");
    expect(response.state.length).toBe(40);
    expect(response.facelets.length).toBe(54);
    expect(response.layout.size).toBe(3);
    expect(response.layout.facelets).toHaveLength(54);
    expect(response.layout.generators).toHaveLength(6);
  });

  it("an unsupported (type, size) pair comes back as an \"error\" response, not a thrown exception", async () => {
    const module = await loadModule();
    const ref: PuzzleRef = { current: null };

    // core/src/puzzle.c: a cube below 2x2x2 is the bound that still
    // rejects (sizes 2 and up all build, see the tests right below).
    const response = await handlePuzzleWorkerRequest(module, ref, {
      id: 0,
      kind: "create",
      puzzleType: PuzzleType.Cubic,
      size: 1,
    });

    expect(response.kind).toBe("error");
    expect(ref.current).toBeNull();
  });

  it("\"create\" succeeds for a cubic size beyond 3 too (Phase 1's NxN support)", async () => {
    const module = await loadModule();
    const ref: PuzzleRef = { current: null };

    const response = await handlePuzzleWorkerRequest(module, ref, {
      id: 0,
      kind: "create",
      puzzleType: PuzzleType.Cubic,
      size: 4,
    });

    expect(response.kind).toBe("created");
    expect(ref.current).not.toBeNull();
    if (response.kind !== "created") throw new Error("expected a created response");
    expect(response.facelets.length).toBe(6 * 4 * 4);
    expect(response.layout.generators).toHaveLength(12); // 6 faces x 2 depths
  });

  it("\"applyMove\" mutates the live puzzle and returns the new state", async () => {
    const module = await loadModule();
    const ref: PuzzleRef = { current: null };
    const created = await handlePuzzleWorkerRequest(module, ref, {
      id: 0,
      kind: "create",
      puzzleType: PuzzleType.Cubic,
      size: 3,
    });
    if (created.kind !== "created") throw new Error("expected created response");

    const response = await handlePuzzleWorkerRequest(module, ref, {
      id: 1,
      kind: "applyMove",
      generator: 0,
      amount: 1,
    });

    expect(response.kind).toBe("state");
    if (response.kind === "state") expect(Array.from(response.state)).not.toEqual(Array.from(created.state));
  });

  it("\"applyMove\" before any \"create\" comes back as an \"error\", not a crash", async () => {
    const module = await loadModule();
    const ref: PuzzleRef = { current: null };

    const response = await handlePuzzleWorkerRequest(module, ref, {
      id: 0,
      kind: "applyMove",
      generator: 0,
      amount: 1,
    });

    expect(response.kind).toBe("error");
  });

  it("\"scramble\" with the same seed is reproducible, same as Puzzle.scramble()", async () => {
    const module = await loadModule();
    const refA: PuzzleRef = { current: null };
    await handlePuzzleWorkerRequest(module, refA, { id: 0, kind: "create", puzzleType: PuzzleType.Cubic, size: 3 });
    const responseA = await handlePuzzleWorkerRequest(module, refA, {
      id: 1,
      kind: "scramble",
      moveCount: 20,
      seed: 42,
    });

    const refB: PuzzleRef = { current: null };
    await handlePuzzleWorkerRequest(module, refB, { id: 0, kind: "create", puzzleType: PuzzleType.Cubic, size: 3 });
    const responseB = await handlePuzzleWorkerRequest(module, refB, {
      id: 1,
      kind: "scramble",
      moveCount: 20,
      seed: 42,
    });

    if (responseA.kind !== "state" || responseB.kind !== "state") throw new Error("expected state responses");
    expect(Array.from(responseA.state)).toEqual(Array.from(responseB.state));
  });

  it("\"destroy\" frees the puzzle and is safe to send again with nothing live", async () => {
    const module = await loadModule();
    const ref: PuzzleRef = { current: null };
    await handlePuzzleWorkerRequest(module, ref, { id: 0, kind: "create", puzzleType: PuzzleType.Cubic, size: 3 });

    const response = await handlePuzzleWorkerRequest(module, ref, { id: 1, kind: "destroy" });
    expect(response).toEqual({
      id: 1,
      kind: "state",
      state: new Uint8Array(0),
      facelets: new Uint8Array(0),
      units: new Uint8Array(0),
      legal: new Uint8Array(0),
      solved: false,
    });
    expect(ref.current).toBeNull();

    // A second destroy with nothing live must not throw -- mirrors
    // Puzzle.destroy()'s own idempotence (puzzle.test.ts).
    await expect(handlePuzzleWorkerRequest(module, ref, { id: 2, kind: "destroy" })).resolves.toEqual({
      id: 2,
      kind: "state",
      state: new Uint8Array(0),
      facelets: new Uint8Array(0),
      units: new Uint8Array(0),
      legal: new Uint8Array(0),
      solved: false,
    });
  });

  it("a second \"create\" with no \"destroy\" in between frees the stray prior puzzle first", async () => {
    const module = await loadModule();
    const ref: PuzzleRef = { current: null };
    await handlePuzzleWorkerRequest(module, ref, { id: 0, kind: "create", puzzleType: PuzzleType.Cubic, size: 3 });
    const first = ref.current;

    await handlePuzzleWorkerRequest(module, ref, { id: 1, kind: "create", puzzleType: PuzzleType.Cubic, size: 3 });

    expect(ref.current).not.toBe(first);
    // The first puzzle was destroy()ed -- using it now throws, same
    // contract as Puzzle.destroy()'s own "used after destroy" guard.
    expect(() => first!.stateView()).toThrow(/used after destroy/);
  });

  it("\"algorithmCount\"/\"algorithmName\" pass through to the live puzzle", async () => {
    const module = await loadModule();
    const ref: PuzzleRef = { current: null };
    await handlePuzzleWorkerRequest(module, ref, { id: 0, kind: "create", puzzleType: PuzzleType.Cubic, size: 3 });

    const countResponse = await handlePuzzleWorkerRequest(module, ref, { id: 1, kind: "algorithmCount" });
    expect(countResponse).toEqual({ id: 1, kind: "algorithmCount", count: 3 });

    const nameResponse = await handlePuzzleWorkerRequest(module, ref, { id: 2, kind: "algorithmName", algoId: 0 });
    expect(nameResponse).toEqual({ id: 2, kind: "algorithmName", name: "Kociemba (two-phase)" });
  });

  it("\"solve\" runs the named algorithm against the live puzzle and returns a decoded SolveResult", async () => {
    const module = await loadModule();
    const ref: PuzzleRef = { current: null };
    await handlePuzzleWorkerRequest(module, ref, { id: 0, kind: "create", puzzleType: PuzzleType.Cubic, size: 3 });
    await handlePuzzleWorkerRequest(module, ref, { id: 1, kind: "scramble", moveCount: 20, seed: 3 });

    const response = await handlePuzzleWorkerRequest(module, ref, { id: 2, kind: "solve", algoId: 0 });
    expect(response.kind).toBe("solve");
    if (response.kind !== "solve") throw new Error("expected a solve response");
    expect(response.result).not.toBeNull();
    expect(response.result!.moves.length).toBe(response.result!.phaseTags.length);
  });

  it("\"solve\"/\"algorithmCount\"/\"algorithmName\" before any \"create\" come back as \"error\", not a crash", async () => {
    const module = await loadModule();
    const ref: PuzzleRef = { current: null };

    const countResponse = await handlePuzzleWorkerRequest(module, ref, { id: 0, kind: "algorithmCount" });
    expect(countResponse.kind).toBe("error");

    const solveResponse = await handlePuzzleWorkerRequest(module, ref, { id: 1, kind: "solve", algoId: 0 });
    expect(solveResponse.kind).toBe("error");
  });

  it("\"applyMoves\" applies a whole list in one request, same result as one by one", async () => {
    const module = await loadModule();
    const moves = [
      { generator: 1, amount: 1 },
      { generator: 0, amount: 3 },
      { generator: 5, amount: 2 },
    ];
    const refA: PuzzleRef = { current: null };
    await handlePuzzleWorkerRequest(module, refA, { id: 0, kind: "create", puzzleType: PuzzleType.Cubic, size: 4 });
    const batch = await handlePuzzleWorkerRequest(module, refA, { id: 1, kind: "applyMoves", moves });
    const refB: PuzzleRef = { current: null };
    await handlePuzzleWorkerRequest(module, refB, { id: 0, kind: "create", puzzleType: PuzzleType.Cubic, size: 4 });
    let single = batch;
    for (const [k, m] of moves.entries()) {
      single = await handlePuzzleWorkerRequest(module, refB, { id: k + 1, kind: "applyMove", ...m });
    }
    if (batch.kind !== "state" || single.kind !== "state") throw new Error("expected state responses");
    expect(Array.from(batch.facelets)).toEqual(Array.from(single.facelets));
    expect(Array.from(batch.state)).toEqual(Array.from(single.state));
  });
});
