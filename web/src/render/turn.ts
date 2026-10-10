import type { Axis } from "./lattice/cubic.js";

/// What one turn acts on, in terms the scene that draws it understands.
/// TurnAnimation, the drag code and SolvePlayback pass these around
/// without looking inside: a cubic scene turns a lattice layer (an axis
/// and a layer index), a cut-plane scene (Skewb, Pyraminx -- 
/// docs/en/09-phase3-skewb-pyraminx.md) turns one of core's generators.
export type TurnTarget = { axis: Axis; layer: number } | { generator: number };

/// A turn caught mid-way: `angleDeg` is a right-handed rotation about
/// the target's axis, the same convention for every family (a clockwise
/// turn seen from the axis tip is negative).
export type ActiveTurn = TurnTarget & { angleDeg: number };

/// The part of a scene TurnAnimation and the solve panel need: redraw
/// from the puzzle's latest colours at rest, or pose a turn in progress.
/// Implemented by CubicScene and CutScene.
export interface TurnScene {
  sync(): void;
  setActiveTurn(turn: ActiveTurn | null): void;
}

/// What TurnAnimation and SolvePlayback need to know about a puzzle's
/// moves, whichever family it is (MoveMap for cubes, CutMoveModel for
/// the cut-plane puzzles).
export interface MoveModel {
  /// What generator `generator` turns, in the form the scene wants.
  targetForGenerator(generator: number): TurnTarget;
  /// The right-handed rotation, in degrees, for `amount` applications
  /// of `generator` -- the shortest way round.
  quarterDegForAmount(generator: number, amount: number): number;
  /// The amount that undoes `amount` of `generator`. Most families have
  /// one answer for every generator and ignore `generator`; Square-1's
  /// layers (11 amounts) and slice (1, its own inverse) differ.
  inverseAmount(amount: number, generator?: number): number;
}
