import { describe, expect, it, vi } from "vitest";
import { SolvePlayback, steppedSpeed, type PlaybackSnapshot } from "../../src/interaction/solve-playback.js";
import { MoveMap } from "../../src/interaction/move-axis.js";
import type { TurnAnimation, TurnOptions } from "../../src/interaction/turn-anim.js";
import type { Move, SolveResult } from "../../src/engine/types.js";

const MOVES_3 = new MoveMap(3, [0, 1, 2, 3, 4, 5].map((face) => ({ face, depth: 0 })));

/// A fake slot like solve-playback.test.ts's, with the isIdle() and jump()
/// that seek() uses; each jump() call is recorded and resolved on cue.
function fakeAnim() {
  const jumps: { moves: Move[]; resolve: () => void }[] = [];
  let idle = true;
  const anim = {
    moves: MOVES_3,
    isIdle: () => idle,
    jump: vi.fn(
      (moves: Move[]) =>
        new Promise<void>((resolve) => {
          jumps.push({ moves, resolve });
        }),
    ),
    beginFrom: vi.fn((_m: Move, _f: number, _t: number, options?: TurnOptions) => {
      void options;
    }),
  } as unknown as TurnAnimation;
  return { anim, jumps, setIdle: (v: boolean) => (idle = v) };
}

const result = (): SolveResult => ({
  moves: [
    { generator: 1, amount: 1 }, // R
    { generator: 0, amount: 2 }, // U2
    { generator: 4, amount: 3 }, // L'
    { generator: 2, amount: 1 }, // F
  ],
  phaseTags: [0, 0, 1, 1],
});

const flush = () => new Promise((resolve) => setTimeout(resolve, 0));

describe("SolvePlayback.seek", () => {
  it("jumps forward by applying the moves in between, and moves the cursor when they land", async () => {
    const { anim, jumps } = fakeAnim();
    const snaps: PlaybackSnapshot[] = [];
    const playback = new SolvePlayback(anim, (s) => snaps.push(s));
    playback.load(result());

    playback.seek(3);
    expect(jumps[0]!.moves).toEqual(result().moves.slice(0, 3));
    expect(snaps.at(-1)).toMatchObject({ cursor: 0, busy: true });

    jumps[0]!.resolve();
    await flush();
    expect(snaps.at(-1)).toMatchObject({ cursor: 3, busy: false });
  });

  it("jumps backward by applying the inverses in reverse order", async () => {
    const { anim, jumps } = fakeAnim();
    const snaps: PlaybackSnapshot[] = [];
    const playback = new SolvePlayback(anim, (s) => snaps.push(s));
    playback.load(result());
    playback.seek(4);
    jumps[0]!.resolve();
    await flush();

    playback.seek(1);
    // Moves 1..3 undone latest first: F', L, U2 (its own inverse).
    expect(jumps[1]!.moves).toEqual([
      { generator: 2, amount: 3 },
      { generator: 4, amount: 1 },
      { generator: 0, amount: 2 },
    ]);
    jumps[1]!.resolve();
    await flush();
    expect(snaps.at(-1)!.cursor).toBe(1);
  });

  it("clamps the target to the solution", async () => {
    const { anim, jumps } = fakeAnim();
    const playback = new SolvePlayback(anim, () => {});
    playback.load(result());
    playback.seek(99);
    expect(jumps[0]!.moves).toHaveLength(4);
  });

  it("does nothing to the cursor it is already at, or while a hand-turn owns the slot", () => {
    const { anim, jumps, setIdle } = fakeAnim();
    const playback = new SolvePlayback(anim, () => {});
    playback.load(result());
    playback.seek(0);
    setIdle(false);
    playback.seek(2);
    expect(jumps).toHaveLength(0);
  });

  it("does nothing with no solution loaded", () => {
    const { anim, jumps } = fakeAnim();
    new SolvePlayback(anim, () => {}).seek(2);
    expect(jumps).toHaveLength(0);
  });

  it("a clear() while the jump is in flight leaves the cursor alone", async () => {
    const { anim, jumps } = fakeAnim();
    const snaps: PlaybackSnapshot[] = [];
    const playback = new SolvePlayback(anim, (s) => snaps.push(s));
    playback.load(result());
    playback.seek(2);
    playback.clear();
    jumps[0]!.resolve();
    await flush();
    expect(snaps.at(-1)).toMatchObject({ result: null, cursor: 0 });
  });
});

describe("steppedSpeed", () => {
  it("moves one preset faster or slower", () => {
    expect(steppedSpeed(360, 1)).toBe(720);
    expect(steppedSpeed(360, -1)).toBe(180);
  });

  it("stops at either end instead of wrapping", () => {
    expect(steppedSpeed(5760, 1)).toBe(5760);
    expect(steppedSpeed(90, -1)).toBe(90);
  });

  it("steps from a value that is not a preset to its neighbour in that direction", () => {
    expect(steppedSpeed(400, 1)).toBe(720);
    expect(steppedSpeed(400, -1)).toBe(360);
  });
});
