import { SQ1_GEN_BOTTOM, SQ1_GEN_SLICE, SQ1_GEN_TOP, SQ1_LAYER_UNITS, SQ1_LAYER_STEPS } from "../engine/types.js";
import type { MoveModel, TurnTarget } from "../render/turn.js";

/// Degrees of one unit: a layer turns in 30-degree steps.
export const SQ1_STEP_DEG = 360 / SQ1_LAYER_UNITS;

/// The slice is half a turn of the right half.
export const SQ1_SLICE_DEG = 180;

/// The Square-1's moves (docs/en/12-phase5-square1.md D2): generator 0
/// turns the top layer clockwise seen from above by `amount` units,
/// generator 1 the bottom layer clockwise seen from BELOW (so
/// anticlockwise from above), generator 2 is the slice, amount 1 only.
/// Angles are right-handed rotations about +Y for a layer, like every
/// family here (clockwise seen from above is negative), and about the
/// cut's normal for the slice.
export class Square1MoveModel implements MoveModel {
  #check(generator: number): void {
    if (generator !== SQ1_GEN_TOP && generator !== SQ1_GEN_BOTTOM && generator !== SQ1_GEN_SLICE) {
      throw new Error(`Square1MoveModel: generator ${generator} is out of range (0-2)`);
    }
  }

  targetForGenerator(generator: number): TurnTarget {
    this.#check(generator);
    return { generator };
  }

  /// A layer amount is drawn the short way round: 7 units clockwise is 5
  /// back. 6 is a half turn either way and is drawn as +6.
  quarterDegForAmount(generator: number, amount: number): number {
    this.#check(generator);
    if (generator === SQ1_GEN_SLICE) {
      if (amount !== 1) throw new Error(`Square1MoveModel: the slice has only amount 1, got ${amount}`);
      return SQ1_SLICE_DEG;
    }
    if (!Number.isInteger(amount) || amount < 1 || amount > SQ1_LAYER_STEPS) {
      throw new Error(`Square1MoveModel: amount ${amount} is out of range (1-${SQ1_LAYER_STEPS})`);
    }
    const units = amount <= SQ1_LAYER_UNITS / 2 ? amount : amount - SQ1_LAYER_UNITS;
    // Top: clockwise from above is negative. Bottom: clockwise from below
    // is anticlockwise from above, so positive.
    return (generator === SQ1_GEN_TOP ? -units : units) * SQ1_STEP_DEG;
  }

  inverseAmount(amount: number, generator?: number): number {
    if (generator === SQ1_GEN_SLICE) return amount;
    if (generator === undefined) throw new Error("Square1MoveModel.inverseAmount needs the generator");
    return SQ1_LAYER_UNITS - amount;
  }

  /// The move for `units` clockwise units of a layer (negative =
  /// anticlockwise), or null for a whole number of turns.
  moveForUnits(generator: number, units: number): { generator: number; amount: number } | null {
    this.#check(generator);
    if (generator === SQ1_GEN_SLICE) throw new Error("Square1MoveModel.moveForUnits: the slice has no units");
    const amount = ((units % SQ1_LAYER_UNITS) + SQ1_LAYER_UNITS) % SQ1_LAYER_UNITS;
    return amount === 0 ? null : { generator, amount };
  }
}
