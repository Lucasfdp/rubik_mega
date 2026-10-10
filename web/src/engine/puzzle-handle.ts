import type { Move, PuzzleLayout, SolveResult } from "./types.js";

/// Shared async contract between the main-thread-direct `Puzzle`
/// (puzzle.ts) and the Worker-backed `RemotePuzzle` (remote-puzzle.ts)
/// -- docs/en/04-roadmap.md Phase 0 step 4 moves the WASM module into a
/// Web Worker so a long solve (Phase 2+, puzzle_solve is still a stub)
/// doesn't freeze the render thread, but render/scene.ts and
/// interaction/{turn-anim,pointer-controller}.ts (Phase 0 steps 2-3)
/// were all written against a *synchronous* Puzzle. Rather than rewrite
/// that layer around postMessage plumbing, both implementations satisfy
/// this one interface instead: mutation (applyMove/scramble) only ever
/// needs to answer "when is it safely committed", which a Promise
/// expresses whether the mutation happened synchronously (Puzzle) or
/// after a Worker round-trip (RemotePuzzle) -- and reads (stateView/
/// destroy) only ever need the latest known snapshot, which both
/// implementations can return synchronously (Puzzle reads live WASM
/// memory directly; RemotePuzzle reads its last-received snapshot).
export interface PuzzleHandle {
  /// Fixed for the puzzle's lifetime: size, facelet geometry, generator
  /// layers (docs/en/07-nxn-web-view.md). Available synchronously from
  /// the moment the handle exists.
  readonly layout: PuzzleLayout;
  /// The latest known colour of every facelet, parallel to
  /// layout.facelets -- one byte each, the face (0..5 = U R F D L B)
  /// whose solved colour it shows. Same freshness guarantee as
  /// stateView(): current once a mutation's Promise has resolved.
  faceletView(): Uint8Array;
  /// Square-1 only: one byte per unit (SQ1_UNIT_EDGE, SQ1_UNIT_CORNER_FIRST
  /// or SQ1_UNIT_CORNER_SECOND), top then bottom, from core's
  /// puzzle_square1_units; empty for every other family. Same freshness
  /// guarantee as stateView().
  unitsView(): Uint8Array;
  /// Whether `generator`/`amount` is legal in the latest known state --
  /// core's puzzle_move_is_legal. Only a Square-1 slice with a corner
  /// across the cut is ever illegal; everything else is always true.
  /// Synchronous like stateView(): RemotePuzzle answers from the snapshot
  /// its last response delivered.
  isMoveLegal(generator: number, amount: number): boolean;
  /// Resolves once the move has been applied and stateView() reflects it.
  applyMove(generator: number, amount: number): Promise<void>;
  /// Applies `moves` in order, in one go, and resolves once stateView()
  /// and faceletView() reflect all of them -- one Worker round-trip
  /// instead of one per move, for skipping through a long solve
  /// (docs/en/08-nxn-reduction.md D5).
  applyMoves(moves: readonly Move[]): Promise<void>;
  /// Resolves once the scramble has been applied and stateView() reflects it.
  scramble(moveCount: number, seed: number): Promise<void>;
  /// The moves `scramble(moveCount, seed)` would apply to the puzzle as it
  /// is NOW, without applying them -- so a scramble can be played as an
  /// animation and still end where the instant one does
  /// (docs/en/13-phase6-polish.md D9). Square-1's moves depend on the state
  /// they start from, so the list is only good for the current state.
  scrambleMoves(moveCount: number, seed: number): Promise<Move[]>;
  /// Whether the puzzle looks solved, by core's rule (puzzle_is_solved):
  /// every face one colour, so a solved cube turned as a whole counts.
  /// Synchronous like stateView(): current once a mutation's Promise has
  /// resolved.
  isSolved(): boolean;
  /// The latest known state snapshot. Synchronous even on RemotePuzzle:
  /// it returns the snapshot delivered by the last applyMove/scramble/
  /// create response, not a fresh round-trip -- a caller that just
  /// awaited a mutation already has the guarantee it's current.
  stateView(): Uint8Array;
  /// Releases the underlying puzzle. Idempotent.
  destroy(): void;
  /// How many solving algorithms this puzzle's (type, size) offers --
  /// core/include/puzzle.h's `puzzle_algorithm_count` (0 before Phase 2;
  /// 4 for the cubic 3x3x3 as of docs/en/06-phase2-solvers.md's
  /// increment 5). Promise-wrapped on Puzzle for the same
  /// sync-under-an-async-contract reason as applyMove/scramble -- see
  /// puzzle.ts's own doc comment.
  algorithmCount(): Promise<number>;
  /// Human-readable name of algorithm `algoId` (0-indexed, < algorithmCount()) --
  /// e.g. "Kociemba (two-phase)". Throws/rejects for an out-of-range id.
  algorithmName(algoId: number): Promise<string>;
  /// Runs algorithm `algoId` against the CURRENT state and returns its
  /// solution, or null if the algorithm's search failed (the `UINT32_MAX`
  /// sentinel puzzle_solve returns on an arena/table build failure --
  /// not expected in practice, but a real possible return, not an
  /// exception, since the WASM side never throws). Does not itself apply
  /// the moves -- that's left to the caller (SolvePlayback), so a solve
  /// can be computed once and then played back, stepped, or rewound
  /// without re-solving.
  solve(algoId: number): Promise<SolveResult | null>;
}
