import type { Move } from "../engine/types.js";
import { ANIM_RELEASE_MIN_SEC } from "./turn-anim.js";

/// How an animated scramble is paced (docs/en/13-phase6-polish.md D9): it
/// plays fast enough to read as a scramble rather than a solve, and never
/// keeps the user waiting.

/// The speed a scramble plays at when it is short enough to afford it
/// (degrees per second): quick, but each turn still reads as a turn.
export const SCRAMBLE_BASE_SPEED_DEG_PER_SEC = 1440;
/// The fastest it will go to fit a long scramble in the budget.
export const SCRAMBLE_MAX_SPEED_DEG_PER_SEC = 5760;
/// The longest an animated scramble may take, in seconds.
export const SCRAMBLE_BUDGET_SEC = 3;

export interface ScrambleAnimationPlan {
  /// Moves applied at once, before the animation starts: the scramble's
  /// beginning, for a scramble too long to show in full.
  instant: Move[];
  /// Moves played as turns, in order.
  animated: Move[];
  speedDegPerSec: number;
}

/// How long `moves` take at `speed`. A turn never plays faster than the
/// animation's own minimum, which is what makes a long scramble impossible
/// to squeeze into the budget by speed alone.
function durationSec(moves: readonly Move[], degreesOf: (move: Move) => number, speed: number): number {
  return moves.reduce((total, move) => total + Math.max(Math.abs(degreesOf(move)) / speed, ANIM_RELEASE_MIN_SEC), 0);
}

/// Splits a scramble into what is applied instantly and what is played,
/// and picks the speed. First tries the base speed, then doubles it up to
/// the maximum. If even that is over budget (an 8x8x8's 120 moves are
/// about 7 s at the animation's minimum turn time), it shows only the LAST
/// moves that fit and applies the rest first: the scramble ends exactly
/// where it would have, and what you watch is its final stretch.
export function planScrambleAnimation(
  moves: readonly Move[],
  degreesOf: (move: Move) => number,
  budgetSec: number = SCRAMBLE_BUDGET_SEC,
): ScrambleAnimationPlan {
  for (let speed = SCRAMBLE_BASE_SPEED_DEG_PER_SEC; speed <= SCRAMBLE_MAX_SPEED_DEG_PER_SEC; speed *= 2) {
    if (durationSec(moves, degreesOf, speed) <= budgetSec) return { instant: [], animated: [...moves], speedDegPerSec: speed };
  }
  const speed = SCRAMBLE_MAX_SPEED_DEG_PER_SEC;
  let start = 0;
  while (start < moves.length - 1 && durationSec(moves.slice(start), degreesOf, speed) > budgetSec) start += 1;
  return { instant: moves.slice(0, start), animated: moves.slice(start), speedDegPerSec: speed };
}
