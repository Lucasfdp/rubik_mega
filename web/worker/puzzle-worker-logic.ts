import { Puzzle } from "../src/engine/puzzle.js";
import type { PuzzleModuleExports } from "../src/engine/wasm-module.js";
import type { PuzzleType } from "../src/engine/types.js";
import type { PuzzleSnapshot, PuzzleWorkerRequest, PuzzleWorkerResponse } from "../src/engine/worker-protocol.js";

/// Mutable slot for the Worker's one live Puzzle -- a plain object
/// (not a closure variable captured by self.onmessage) so
/// handlePuzzleWorkerRequest stays a pure function of its arguments and
/// is directly unit-testable (web/tests/worker/puzzle-worker-logic.test.ts)
/// against the real compiled WASM module, with no real Worker involved.
export interface PuzzleRef {
  current: Puzzle | null;
}

/// Pure request -> response reducer: the whole of puzzle-worker.ts's
/// logic, minus the postMessage/onmessage glue (docs/en/04-roadmap.md
/// Phase 0 step 4). Kept in its own file, separate from
/// worker/puzzle-worker.ts, specifically so it can run under vitest's
/// Node environment, where `self`/`Worker` don't exist.
///
/// Requests are assumed to arrive one at a time per puzzle-worker.ts's
/// own onmessage (see its doc comment) -- this function doesn't queue
/// or reject an overlapping request of its own accord.
export async function handlePuzzleWorkerRequest(
  module: PuzzleModuleExports,
  ref: PuzzleRef,
  request: PuzzleWorkerRequest,
): Promise<PuzzleWorkerResponse> {
  try {
    switch (request.kind) {
      case "create": {
        // A stray prior puzzle (a second "create" with no "destroy" in
        // between) would otherwise leak its WASM-side allocation.
        ref.current?.destroy();
        ref.current = Puzzle.create(module, request.puzzleType as PuzzleType, request.size);
        return {
          id: request.id,
          kind: "created",
          ...snapshotOf(ref.current),
          layout: ref.current.layout,
        };
      }
      case "applyMove": {
        const puzzle = requireLive(ref);
        await puzzle.applyMove(request.generator, request.amount);
        return { id: request.id, kind: "state", ...snapshotOf(puzzle) };
      }
      case "applyMoves": {
        const puzzle = requireLive(ref);
        await puzzle.applyMoves(request.moves);
        return { id: request.id, kind: "state", ...snapshotOf(puzzle) };
      }
      case "scramble": {
        const puzzle = requireLive(ref);
        await puzzle.scramble(request.moveCount, request.seed);
        return { id: request.id, kind: "state", ...snapshotOf(puzzle) };
      }
      case "scrambleMoves": {
        const puzzle = requireLive(ref);
        return { id: request.id, kind: "scrambleMoves", moves: await puzzle.scrambleMoves(request.moveCount, request.seed) };
      }
      case "destroy": {
        ref.current?.destroy();
        ref.current = null;
        return {
          id: request.id,
          kind: "state",
          state: new Uint8Array(0),
          facelets: new Uint8Array(0),
          units: new Uint8Array(0),
          legal: new Uint8Array(0),
          solved: false,
        };
      }
      case "algorithmCount": {
        const puzzle = requireLive(ref);
        return { id: request.id, kind: "algorithmCount", count: await puzzle.algorithmCount() };
      }
      case "algorithmName": {
        const puzzle = requireLive(ref);
        return { id: request.id, kind: "algorithmName", name: await puzzle.algorithmName(request.algoId) };
      }
      case "solve": {
        const puzzle = requireLive(ref);
        return { id: request.id, kind: "solve", result: await puzzle.solve(request.algoId) };
      }
    }
  } catch (error) {
    return { id: request.id, kind: "error", message: error instanceof Error ? error.message : String(error) };
  }
}

/// Copies out everything a response carries about the puzzle's state;
/// live views into WASM memory cannot cross the Worker boundary.
function snapshotOf(puzzle: Puzzle): PuzzleSnapshot {
  return {
    state: puzzle.stateView().slice(),
    facelets: puzzle.faceletView().slice(),
    units: puzzle.unitsView().slice(),
    legal: puzzle.legalView(),
    solved: puzzle.isSolved(),
  };
}

function requireLive(ref: PuzzleRef): Puzzle {
  if (!ref.current) {
    throw new Error('no puzzle created yet -- send a "create" request first');
  }
  return ref.current;
}
