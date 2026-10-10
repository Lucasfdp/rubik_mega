import { Axis } from "../render/lattice/cubic.js";
import type { GeneratorLayer } from "../engine/types.js";
import type { MoveModel, TurnTarget } from "../render/turn.js";

/// Core's 6 faces by name -- mirrors core/include/puzzles/cubic.h's
/// CUBIC_GEN_* defines (U R F D L B order). On a 3x3x3 these are also
/// the generator numbers (one generator per face); on bigger cubes a
/// generator number means nothing on its own -- go through MoveMap.
export const CUBIC_GEN_U = 0;
export const CUBIC_GEN_R = 1;
export const CUBIC_GEN_F = 2;
export const CUBIC_GEN_D = 3;
export const CUBIC_GEN_L = 4;
export const CUBIC_GEN_B = 5;

/// Which world axis a face's layers turn about, which end of it the face
/// sits at, and the world-axis rotation (degrees) of ONE clockwise
/// quarter turn of any of its layers -- ported from rubik's MOVE_AXIS
/// table (src/render/anim.c). That table's own comment: the signs come
/// from building cube.h's FACE_TABLES as real 3D rotation matrices and
/// solving, per face, which signed rotation reproduces the exact
/// corner/edge cycle, then replaying 5,000 random moves. Inner layers
/// turn the same way as their own face (core's rotate_quarter(),
/// DECISIONS.md 2026-10-04 "NxN rotation direction"), so one entry per
/// face covers every depth.
interface FaceTurn {
  axis: Axis;
  sign: 1 | -1;
  clockwiseDeg: number;
}

const FACE_TURN: readonly FaceTurn[] = [
  { axis: Axis.Y, sign: 1, clockwiseDeg: -90 }, // U
  { axis: Axis.X, sign: 1, clockwiseDeg: -90 }, // R
  { axis: Axis.Z, sign: 1, clockwiseDeg: -90 }, // F
  { axis: Axis.Y, sign: -1, clockwiseDeg: 90 }, // D
  { axis: Axis.X, sign: -1, clockwiseDeg: 90 }, // L
  { axis: Axis.Z, sign: -1, clockwiseDeg: 90 }, // B
];

/// One generator, resolved to what the renderer needs: the world axis,
/// the lattice layer index (0..size-1) along it, and its clockwise
/// quarter-turn angle.
interface GeneratorTurn {
  axis: Axis;
  layer: number;
  clockwiseDeg: number;
  /// +1 if the generator belongs to a U/R/F face, -1 for D/L/B -- used
  /// only to break the odd-n middle-layer tie (D3).
  sign: 1 | -1;
}

/// The two-way mapping between a puzzle's generators and world-space
/// layer turns, for one puzzle size -- built from core's own
/// generator -> (face, depth) table (PuzzleLayout.generators), so this
/// file never re-derives core's generator numbering
/// (docs/en/07-nxn-web-view.md D1's "one truth" rule, applied to moves).
export class MoveMap implements MoveModel {
  readonly size: number;
  readonly #turns: readonly GeneratorTurn[];

  constructor(size: number, generators: readonly GeneratorLayer[]) {
    this.size = size;
    this.#turns = generators.map(({ face, depth }) => {
      const turn = FACE_TURN[face];
      if (!turn) throw new Error(`MoveMap: face ${face} is out of range (0-5)`);
      return {
        axis: turn.axis,
        layer: turn.sign > 0 ? size - 1 - depth : depth,
        clockwiseDeg: turn.clockwiseDeg,
        sign: turn.sign,
      };
    });
  }

  #turn(generator: number): GeneratorTurn {
    const turn = this.#turns[generator];
    if (!turn) {
      throw new Error(`MoveMap: generator ${generator} is out of range (0-${this.#turns.length - 1})`);
    }
    return turn;
  }

  /// The world axis and lattice layer generator `generator` turns.
  axisForGenerator(generator: number): { axis: Axis; layer: number } {
    const { axis, layer } = this.#turn(generator);
    return { axis, layer };
  }

  /// The same, as the scene-neutral TurnTarget TurnAnimation passes on.
  targetForGenerator(generator: number): TurnTarget {
    return this.axisForGenerator(generator);
  }

  /// A cubic face has 3 amounts (90/180/270), so the inverse of `a` is
  /// `4 - a`: 1 <-> 3, 2 <-> 2 -- core's own move-inversion convention.
  inverseAmount(amount: number): number {
    return 4 - amount;
  }

  /// The world-axis rotation, in degrees, for `amount` applications of
  /// `generator`'s clockwise quarter turn -- amount 2 is double, amount
  /// 3 the negation (one short of a full turn is one turn back),
  /// matching core's puzzle_apply_move() semantics exactly.
  quarterDegForAmount(generator: number, amount: number): number {
    const { clockwiseDeg } = this.#turn(generator);
    if (amount === 2) return clockwiseDeg * 2;
    if (amount === 3) return -clockwiseDeg;
    return clockwiseDeg;
  }

  /// True if some generator turns `layer` on `axis` -- false only for a
  /// 3x3x3's middle slices in this build (core has no slice generator
  /// at n == 3), which the drag code rubber-bands instead of turning.
  layerHasGenerator(axis: Axis, layer: number): boolean {
    return this.#turns.some((turn) => turn.axis === axis && turn.layer === layer);
  }

  /// The (generator, amount) whose single application rotates `layer`
  /// on `axis` by exactly `quarterDeg` -- the inverse of
  /// quarterDegForAmount(). Ported from rubik's anim_move_for_turn(): a
  /// half turn matches either sign, since a drag can reach +180 or -180
  /// (rubik's B6 fix). On odd n the true middle layer belongs to two
  /// generators (a U-side one and a D-side one, same cells); the U/R/F
  /// side wins (docs/en/07-nxn-web-view.md D3) -- deterministic, and the
  /// amount is derived from that generator's own clockwise sense, so
  /// the result is the same physical turn either way.
  ///
  /// @return null if no generator turns that layer, or `quarterDeg` is
  ///         not a quarter or half turn.
  moveForTurn(axis: Axis, layer: number, quarterDeg: number): { generator: number; amount: number } | null {
    let best: number | null = null;
    this.#turns.forEach((turn, generator) => {
      if (turn.axis !== axis || turn.layer !== layer) return;
      if (best === null || turn.sign > this.#turns[best]!.sign) best = generator;
    });
    if (best === null) return null;
    const { clockwiseDeg } = this.#turns[best]!;
    if (quarterDeg === clockwiseDeg) return { generator: best, amount: 1 };
    if (Math.abs(quarterDeg) === 180) return { generator: best, amount: 2 };
    if (quarterDeg === -clockwiseDeg) return { generator: best, amount: 3 };
    return null;
  }
}
