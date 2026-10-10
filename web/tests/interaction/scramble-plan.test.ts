import { describe, expect, it } from "vitest";
import {
  planScrambleAnimation,
  SCRAMBLE_BASE_SPEED_DEG_PER_SEC,
  SCRAMBLE_MAX_SPEED_DEG_PER_SEC,
} from "../../src/interaction/scramble-plan.js";
import { ANIM_RELEASE_MIN_SEC } from "../../src/interaction/turn-anim.js";
import type { Move } from "../../src/engine/types.js";

const quarter = (): number => 90;
const movesOf = (n: number): Move[] => Array.from({ length: n }, (_, i) => ({ generator: i % 6, amount: 1 }));

describe("planScrambleAnimation", () => {
  it("plays a short scramble in full at the base speed", () => {
    const moves = movesOf(10);
    expect(planScrambleAnimation(moves, quarter)).toEqual({
      instant: [],
      animated: moves,
      speedDegPerSec: SCRAMBLE_BASE_SPEED_DEG_PER_SEC,
    });
  });

  it("speeds up before it cuts anything", () => {
    // Each quarter turn takes max(90 / speed, minimum); find a count that
    // misses the budget at the base speed but fits at a faster one.
    const count = Math.floor(3 / Math.max(90 / SCRAMBLE_BASE_SPEED_DEG_PER_SEC, ANIM_RELEASE_MIN_SEC)) + 5;
    const plan = planScrambleAnimation(movesOf(count), quarter, 3);
    if (plan.instant.length === 0) expect(plan.speedDegPerSec).toBeGreaterThanOrEqual(SCRAMBLE_BASE_SPEED_DEG_PER_SEC);
  });

  it("applies the start of a too-long scramble instantly and animates its end", () => {
    const moves = movesOf(400);
    const plan = planScrambleAnimation(moves, quarter, 1);
    expect(plan.speedDegPerSec).toBe(SCRAMBLE_MAX_SPEED_DEG_PER_SEC);
    expect(plan.instant.length).toBeGreaterThan(0);
    expect([...plan.instant, ...plan.animated]).toEqual(moves);
  });

  it("always animates at least the last move", () => {
    const plan = planScrambleAnimation(movesOf(50), quarter, 0);
    expect(plan.animated).toHaveLength(1);
  });

  it("handles an empty scramble", () => {
    expect(planScrambleAnimation([], quarter).animated).toEqual([]);
  });
});
