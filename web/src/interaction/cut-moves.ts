import { CUT_STEPS } from "../engine/types.js";
import type { MoveModel, TurnTarget } from "../render/turn.js";

/// Degrees of one clockwise (seen from the axis tip) 120-degree turn,
/// as a right-handed rotation about the axis: negative, like a cubic
/// face's clockwise quarter turn (move-axis.ts's FACE_TURN).
export const CUT_STEP_DEG = -120;

/// Size in degrees of one step of a generator with `steps` amounts: a
/// full turn is steps + 1 of them (120 for 2 amounts, 72 for 4).
export function cutStepDeg(steps: number): number {
  return 360 / (steps + 1);
}

/// The move model for the cut-plane puzzles: every generator turns in
/// equal steps -- 120 degrees, amounts 1 and 2, for Skewb and Pyraminx
/// (CUT_STEPS); 72 degrees, amounts 1 to 4, for Megaminx and Kilominx
/// (DODECA_STEPS). `steps` is core's t_generator_table.steps.
export class CutMoveModel implements MoveModel {
  readonly generatorCount: number;
  readonly steps: number;

  constructor(generatorCount: number, steps: number = CUT_STEPS) {
    this.generatorCount = generatorCount;
    this.steps = steps;
  }

  #check(generator: number): void {
    if (!Number.isInteger(generator) || generator < 0 || generator >= this.generatorCount) {
      throw new Error(`CutMoveModel: generator ${generator} is out of range (0-${this.generatorCount - 1})`);
    }
  }

  targetForGenerator(generator: number): TurnTarget {
    this.#check(generator);
    return { generator };
  }

  /// Amount a is `a` clockwise steps (negative degrees). When that is
  /// more than half a turn it is drawn the other way round instead -- the
  /// same turn, the short way, as a cubic amount 3 is drawn as -90: a
  /// Skewb's amount 2 is +120, a Megaminx's amount 3 is +144.
  quarterDegForAmount(generator: number, amount: number): number {
    this.#check(generator);
    if (!Number.isInteger(amount) || amount < 1 || amount > this.steps) {
      throw new Error(`CutMoveModel: amount ${amount} is out of range (1-${this.steps})`);
    }
    const turn = this.steps + 1;
    const shortWay = amount * 2 <= turn ? amount : amount - turn;
    return -shortWay * cutStepDeg(this.steps);
  }

  /// `steps` amounts, so a <-> steps + 1 - a.
  inverseAmount(amount: number): number {
    return this.steps + 1 - amount;
  }

  /// The (generator, amount) that rotates by `steps` right-handed
  /// steps (negative = clockwise): -1 is amount 1, +1 is the last
  /// amount, and anything past a full turn wraps. Null for a whole
  /// number of turns.
  moveForSteps(generator: number, steps: number): { generator: number; amount: number } | null {
    this.#check(generator);
    const turn = this.steps + 1;
    const wrapped = ((-steps % turn) + turn) % turn;
    return wrapped === 0 ? null : { generator, amount: wrapped };
  }
}
