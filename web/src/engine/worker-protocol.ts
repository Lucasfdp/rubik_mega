import type { Move, PuzzleLayout, SolveResult } from "./types.js";

/// Message protocol between RemotePuzzle (remote-puzzle.ts, main
/// thread) and the Worker (worker/puzzle-worker.ts) -- Lucas's
/// snapshot-transfer design (docs/en/04-roadmap.md Phase 0 step 4): no
/// SharedArrayBuffer, so no COOP/COEP hosting requirement, just a
/// Uint8Array copy per response.
///
/// Split into a body union (`PuzzleWorkerRequestBody`) plus `id`,
/// rather than putting `id` directly in each union member and writing
/// `Omit<PuzzleWorkerRequest, "id">` wherever the id doesn't matter yet
/// (remote-puzzle.ts's send path) -- `Omit` over a discriminated union
/// doesn't distribute the way you'd want (it collapses back to a
/// single object type and loses the per-member narrowing), so the
/// intersection shape here keeps both the union's discrimination and a
/// "body without an id yet" type available for free.
export type PuzzleWorkerRequestBody =
  | { kind: "create"; puzzleType: number; size: number }
  | { kind: "applyMove"; generator: number; amount: number }
  | { kind: "applyMoves"; moves: Move[] }
  | { kind: "scramble"; moveCount: number; seed: number }
  | { kind: "scrambleMoves"; moveCount: number; seed: number }
  | { kind: "destroy" }
  | { kind: "algorithmCount" }
  | { kind: "algorithmName"; algoId: number }
  | { kind: "solve"; algoId: number };

/// Everything a mutation response carries about the puzzle's current
/// state. `units` is empty except for Square-1; `legal` is the
/// legality (0/1) of every move in layout order -- generator 0's amounts
/// 1..steps, then generator 1's, and so on -- and is empty (meaning "all
/// legal") except for Square-1. `solved` is core's puzzle_is_solved():
/// the puzzle looks solved (docs/en/13-phase6-polish.md D9).
export interface PuzzleSnapshot {
  state: Uint8Array;
  facelets: Uint8Array;
  units: Uint8Array;
  legal: Uint8Array;
  solved: boolean;
}

export type PuzzleWorkerRequest = PuzzleWorkerRequestBody & { id: number };

/// One response variant per request shape that doesn't fit "state":
/// algorithmCount/algorithmName/solve don't change the puzzle, so
/// echoing a state snapshot back would be either wasted (count/name)
/// or actively wrong (solve() must NOT imply the moves were applied --
/// PuzzleHandle.solve() hands the result to the caller to apply or not,
/// see puzzle-handle.ts). "state" keeps its existing meaning for the
/// four mutation/creation requests; "error" stays the one shared
/// failure shape across every request kind.
export type PuzzleWorkerResponse =
  | ({ id: number; kind: "created"; layout: PuzzleLayout } & PuzzleSnapshot)
  | ({ id: number; kind: "state" } & PuzzleSnapshot)
  | { id: number; kind: "algorithmCount"; count: number }
  | { id: number; kind: "algorithmName"; name: string }
  | { id: number; kind: "solve"; result: SolveResult | null }
  | { id: number; kind: "scrambleMoves"; moves: Move[] }
  | { id: number; kind: "error"; message: string };
