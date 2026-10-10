import { describe, expect, it, vi } from "vitest";
import { SolvePlayback, type PlaybackSnapshot } from "../../src/interaction/solve-playback.js";
import { MoveMap } from "../../src/interaction/move-axis.js";

/// The 3x3x3 generator table (one generator per face, depth 0).
const MOVES_3 = new MoveMap(3, [0, 1, 2, 3, 4, 5].map((face) => ({ face, depth: 0 })));
const quarterDegForAmount = (g: number, a: number): number => MOVES_3.quarterDegForAmount(g, a);
import type { TurnAnimation, TurnOptions } from "../../src/interaction/turn-anim.js";
import type { Move, SolveResult } from "../../src/engine/types.js";

/// A fake TurnAnimation: SolvePlayback only ever calls beginFrom() on
/// it, and only ever learns a move has "landed" through the onSettled
/// callback it passes in -- so this fake just records each call and
/// lets a test fire that callback on cue, exactly like TurnAnimation
/// would once a real applyMove()+sync() finished (turn-anim.test.ts
/// uses the same fake-the-collaborator approach for TurnAnimation's own
/// dependencies).
function fakeAnim() {
  let resolveJump: (() => void) | null = null;
  const jump = vi.fn(
    () =>
      new Promise<void>((resolve) => {
        resolveJump = resolve;
      }),
  );
  const calls: { move: Move; fromDeg: number; toDeg: number; options?: TurnOptions }[] = [];
  let pendingOnSettled: (() => void) | null = null;
  const beginFrom = vi.fn((move: Move, fromDeg: number, toDeg: number, options?: TurnOptions) => {
    calls.push({ move, fromDeg, toDeg, options });
    pendingOnSettled = options?.onSettled ?? null;
  });
  return {
    anim: { beginFrom, jump, moves: MOVES_3 } as unknown as TurnAnimation,
    jump,
    finishJump(): void {
      resolveJump?.();
    },
    beginFrom,
    calls,
    settle(): void {
      const cb = pendingOnSettled;
      pendingOnSettled = null;
      cb?.();
    },
  };
}

function fakeResult(): SolveResult {
  return {
    moves: [
      { generator: 1, amount: 1 }, // R
      { generator: 0, amount: 2 }, // U2
      { generator: 4, amount: 3 }, // L'
    ],
    phaseTags: [0, 0, 0],
  };
}

describe("SolvePlayback", () => {
  it("load() sets cursor 0, paused, and reports the loaded result", () => {
    const { anim } = fakeAnim();
    const snapshots: PlaybackSnapshot[] = [];
    const playback = new SolvePlayback(anim, (s) => snapshots.push(s));

    const result = fakeResult();
    playback.load(result);

    expect(snapshots.at(-1)).toEqual({ result, cursor: 0, playing: false, speedDegPerSec: 360, busy: false });
  });

  it("play() animates each move in sequence, advancing the cursor as each settles", () => {
    const { anim, beginFrom, settle } = fakeAnim();
    const snapshots: PlaybackSnapshot[] = [];
    const playback = new SolvePlayback(anim, (s) => snapshots.push(s));
    const result = fakeResult();
    playback.load(result);

    playback.play();
    expect(beginFrom).toHaveBeenCalledTimes(1);
    expect(beginFrom).toHaveBeenCalledWith(
      result.moves[0],
      0,
      quarterDegForAmount(1, 1),
      expect.objectContaining({ speedDegPerSec: 360 }),
    );

    settle(); // move 1 lands
    expect(snapshots.at(-1)?.cursor).toBe(1);
    expect(beginFrom).toHaveBeenCalledTimes(2); // auto-advanced to move 2

    settle(); // move 2 lands
    expect(beginFrom).toHaveBeenCalledTimes(3);
    settle(); // move 3 lands -- end of sequence

    const last = snapshots.at(-1)!;
    expect(last.cursor).toBe(3);
    expect(last.playing).toBe(false); // play() stops itself at the end
  });

  it("pause() lets the in-flight move finish but does not start another", () => {
    const { beginFrom, settle, anim } = fakeAnim();
    const playback = new SolvePlayback(anim, () => {});
    playback.load(fakeResult());

    playback.play();
    playback.pause();
    settle(); // the move that was already animating still lands

    expect(beginFrom).toHaveBeenCalledTimes(1); // no second move started
  });

  it("stepForward() plays exactly one move and ignores a second call while busy", () => {
    const { beginFrom, settle, anim } = fakeAnim();
    const snapshots: PlaybackSnapshot[] = [];
    const playback = new SolvePlayback(anim, (s) => snapshots.push(s));
    playback.load(fakeResult());

    playback.stepForward();
    playback.stepForward(); // ignored -- the first move hasn't settled yet
    expect(beginFrom).toHaveBeenCalledTimes(1);

    settle();
    expect(snapshots.at(-1)?.cursor).toBe(1);

    playback.stepForward(); // now safe to start move 2
    expect(beginFrom).toHaveBeenCalledTimes(2);
  });

  it("stepForward() is a no-op once the cursor reaches the end", () => {
    const { beginFrom, settle, anim } = fakeAnim();
    const playback = new SolvePlayback(anim, () => {});
    const result: SolveResult = { moves: [{ generator: 1, amount: 1 }], phaseTags: [0] };
    playback.load(result);

    playback.stepForward();
    settle();
    playback.stepForward();
    expect(beginFrom).toHaveBeenCalledTimes(1);
  });

  it("stepBack() plays the inverse of the previous move and decrements the cursor", () => {
    const { beginFrom, settle, anim } = fakeAnim();
    const snapshots: PlaybackSnapshot[] = [];
    const playback = new SolvePlayback(anim, (s) => snapshots.push(s));
    const result = fakeResult();
    playback.load(result);

    playback.stepForward();
    settle(); // cursor now 1, move 0 (R) applied

    playback.stepBack();
    // R's inverse is R' (amount 4-1=3), played as an ordinary forward
    // turn: 0 -> its quarter angle, so the stickers travel with the
    // layer and the commit lands exactly where the animation ends.
    expect(beginFrom).toHaveBeenCalledWith(
      { generator: 1, amount: 3 },
      0,
      quarterDegForAmount(1, 3),
      expect.anything(),
    );
    settle();
    expect(snapshots.at(-1)?.cursor).toBe(0);
  });

  it("stepBack() is a no-op at cursor 0", () => {
    const { beginFrom, anim } = fakeAnim();
    const playback = new SolvePlayback(anim, () => {});
    playback.load(fakeResult());
    playback.stepBack();
    expect(beginFrom).not.toHaveBeenCalled();
  });

  it("clear() drops the loaded result and resets the cursor", () => {
    const { anim } = fakeAnim();
    const snapshots: PlaybackSnapshot[] = [];
    const playback = new SolvePlayback(anim, (s) => snapshots.push(s));
    playback.load(fakeResult());
    playback.clear();
    expect(snapshots.at(-1)).toEqual({ result: null, cursor: 0, playing: false, speedDegPerSec: 360, busy: false });
  });

  it("busy flips true the instant a move starts animating, false once it settles", () => {
    const { settle, anim } = fakeAnim();
    const snapshots: PlaybackSnapshot[] = [];
    const playback = new SolvePlayback(anim, (s) => snapshots.push(s));
    playback.load(fakeResult());

    expect(snapshots.at(-1)?.busy).toBe(false);
    playback.stepForward();
    expect(snapshots.at(-1)?.busy).toBe(true);
    settle();
    expect(snapshots.at(-1)?.busy).toBe(false);
  });

  it("a load() while a move is still animating ignores that move's stale onSettled rather than corrupting the new result's cursor", () => {
    const { anim, calls, settle } = fakeAnim();
    const snapshots: PlaybackSnapshot[] = [];
    const playback = new SolvePlayback(anim, (s) => snapshots.push(s));

    const firstResult = fakeResult(); // 3 moves
    playback.load(firstResult);
    playback.play();
    expect(calls).toHaveLength(1); // move 0 of firstResult is now animating

    // A second Solve lands before that first move settles (the exact
    // race ui/solve-panel.ts's click handlers now also guard against at
    // the button level) -- load() a different result on top of it.
    const secondResult: SolveResult = {
      moves: [{ generator: 2, amount: 1 }],
      phaseTags: [0],
    };
    playback.load(secondResult);
    expect(snapshots.at(-1)).toEqual({
      result: secondResult,
      cursor: 0,
      playing: false,
      speedDegPerSec: 360,
      // Still true: the shared TurnAnimation slot really is still
      // animating the stale first move (load() can't cancel it, only
      // stop CARING about its result) -- it flips false once that move
      // actually settles, below.
      busy: true,
    });

    // The FIRST move's animation now finishes and fires its (stale) onSettled.
    settle();
    // Must not have advanced secondResult's cursor, or played a second move.
    expect(snapshots.at(-1)?.cursor).toBe(0);
    expect(snapshots.at(-1)?.result).toBe(secondResult);
    expect(calls).toHaveLength(1); // no new move started off the stale callback

    // Playback is still perfectly usable afterward.
    playback.play();
    expect(calls).toHaveLength(2);
    expect(calls[1]!.move).toEqual(secondResult.moves[0]);
  });

  it("setSpeed() changes the speed future moves animate at", () => {
    const { beginFrom, anim } = fakeAnim();
    const playback = new SolvePlayback(anim, () => {});
    playback.load(fakeResult());
    playback.setSpeed(720);
    playback.stepForward();
    expect(beginFrom).toHaveBeenCalledWith(
      expect.anything(),
      expect.anything(),
      expect.anything(),
      expect.objectContaining({ speedDegPerSec: 720 }),
    );
  });

  it("skipPhase() jumps over the rest of the current phase in one go; asked mid-move, it waits for that move", async () => {
    const { anim, jump, finishJump, settle } = fakeAnim();
    const snapshots: PlaybackSnapshot[] = [];
    const playback = new SolvePlayback(anim, (s) => snapshots.push(s));
    const result: SolveResult = {
      moves: [
        { generator: 1, amount: 1 },
        { generator: 0, amount: 2 },
        { generator: 4, amount: 3 },
        { generator: 2, amount: 1 },
      ],
      phaseTags: [0, 0, 0, 1],
    };
    playback.load(result);
    playback.stepForward(); // move 0 animating
    playback.skipPhase(); // queued, not dropped
    expect(jump).not.toHaveBeenCalled();
    settle(); // move 0 lands, cursor 1 -- the queued skip runs now
    expect(jump).toHaveBeenCalledWith(result.moves.slice(1, 3));
    expect(snapshots.at(-1)?.busy).toBe(true);
    finishJump();
    await Promise.resolve();
    await Promise.resolve();
    expect(snapshots.at(-1)?.cursor).toBe(3);
    expect(snapshots.at(-1)?.busy).toBe(false);
  });
});
