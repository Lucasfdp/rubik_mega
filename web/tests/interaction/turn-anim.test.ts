import { describe, expect, it, vi } from "vitest";
import { ANIM_RELEASE_MIN_SEC, TurnAnimation } from "../../src/interaction/turn-anim.js";
import { MoveMap } from "../../src/interaction/move-axis.js";

const MOVES_3 = new MoveMap(3, [0, 1, 2, 3, 4, 5].map((face) => ({ face, depth: 0 })));
import type { PuzzleHandle } from "../../src/engine/puzzle-handle.js";
import type { CubicScene } from "../../src/render/scene.js";

function fakePuzzle() {
  // applyMove() is async on PuzzleHandle (RemotePuzzle round-trips
  // through a Worker) -- mockResolvedValue so TurnAnimation's real
  // `.then(() => scene.sync())` hand-off has a real Promise to chain
  // onto, same as either real implementation.
  return { applyMove: vi.fn().mockResolvedValue(undefined) } as unknown as PuzzleHandle;
}
function fakeScene() {
  return { sync: vi.fn(), setActiveTurn: vi.fn() } as unknown as CubicScene;
}

describe("TurnAnimation", () => {
  it("beginFrom(): commits the move and resyncs once the eased angle reaches the target", async () => {
    const puzzle = fakePuzzle();
    const scene = fakeScene();
    const anim = new TurnAnimation(puzzle, scene, MOVES_3);

    anim.beginFrom({ generator: 1, amount: 1 }, 0, -90); // R, axis X layer 2
    expect(anim.isIdle()).toBe(false);

    // Mid-flight: scene gets a live pose, puzzle untouched.
    anim.update(0.05);
    expect(scene.setActiveTurn).toHaveBeenCalled();
    expect(puzzle.applyMove).not.toHaveBeenCalled();

    // Finish it off with a large dt -- anim_update()'s own "frac >= 1" commit path.
    anim.update(10);
    expect(anim.isIdle()).toBe(true); // isIdle() flips synchronously, before the commit resolves
    expect(puzzle.applyMove).toHaveBeenCalledWith(1, 1);
    expect(scene.sync).not.toHaveBeenCalled(); // not yet -- sync() waits on applyMove()'s Promise

    // Let applyMove()'s resolved Promise's .then() callback run.
    await Promise.resolve();
    expect(scene.sync).toHaveBeenCalledTimes(1);
  });

  it("beginSettle(): resyncs the scene but commits no move", () => {
    const puzzle = fakePuzzle();
    const scene = fakeScene();
    const anim = new TurnAnimation(puzzle, scene, MOVES_3);

    anim.beginSettle(0 /* Axis.X */, 1, 15);
    anim.update(10);
    expect(anim.isIdle()).toBe(true);
    expect(puzzle.applyMove).not.toHaveBeenCalled();
    expect(scene.sync).toHaveBeenCalledTimes(1);
  });

  it("a from===to release (e.g. a settle from 0) still takes the minimum duration, not an instant pop", () => {
    const puzzle = fakePuzzle();
    const scene = fakeScene();
    const anim = new TurnAnimation(puzzle, scene, MOVES_3);

    anim.beginSettle(0, 1, 0); // from 0 to 0 -- would be a zero-length anim without the floor
    anim.update(0.001);
    expect(anim.isIdle()).toBe(false); // still running, not popped instantly
    anim.update(1);
    expect(anim.isIdle()).toBe(true);
  });

  it("ignores a second begin while one is already in flight (isIdle() gates it, same as rubik's own contract)", () => {
    const puzzle = fakePuzzle();
    const scene = fakeScene();
    const anim = new TurnAnimation(puzzle, scene, MOVES_3);
    anim.beginFrom({ generator: 0, amount: 1 }, 0, -90);
    expect(anim.isIdle()).toBe(false);
    // Calling beginFrom again here would be a caller bug (rubik's own
    // anim_begin_from() doc: "Only valid to call while anim_is_idle()")
    // -- pointer-controller.ts only ever calls these from release/
    // settle handlers that first check isIdle() via #anim.isIdle() at
    // pick-start time, so this is a documented precondition, not
    // runtime-enforced here.
    expect(anim.isIdle()).toBe(false);
  });

  it("onSettled() still fires even when applyMove() rejects -- a queued caller must not hang forever", async () => {
    const scene = fakeScene();
    const puzzle = { applyMove: vi.fn().mockRejectedValue(new Error("worker boom")) } as unknown as PuzzleHandle;
    const anim = new TurnAnimation(puzzle, scene, MOVES_3);
    const onSettled = vi.fn();
    const consoleError = vi.spyOn(console, "error").mockImplementation(() => {});

    anim.beginFrom({ generator: 1, amount: 1 }, 0, -90, { onSettled });
    anim.update(10); // commits -- applyMove() rejects asynchronously

    await Promise.resolve();
    await Promise.resolve(); // let the rejection handler's microtask run

    expect(onSettled).toHaveBeenCalledTimes(1); // SolvePlayback's #busy must still clear
    expect(scene.sync).not.toHaveBeenCalled(); // the failed commit never reached sync()
    expect(consoleError).toHaveBeenCalled(); // the failure is visible, not silent

    consoleError.mockRestore();
  });

  it("jump(): applies moves in one batch and resyncs, no animation; refuses while a turn is in flight", async () => {
    const puzzle = { applyMove: vi.fn().mockResolvedValue(undefined), applyMoves: vi.fn().mockResolvedValue(undefined) } as unknown as PuzzleHandle;
    const scene = fakeScene();
    const anim = new TurnAnimation(puzzle, scene, MOVES_3);
    const moves = [{ generator: 1, amount: 1 }, { generator: 0, amount: 3 }];
    await anim.jump(moves);
    expect(puzzle.applyMoves).toHaveBeenCalledWith(moves);
    expect(scene.sync).toHaveBeenCalledTimes(1);
    expect(scene.setActiveTurn).not.toHaveBeenCalled();
    anim.beginFrom({ generator: 1, amount: 1 }, 0, -90);
    await expect(anim.jump(moves)).rejects.toThrow(/in flight/);
  });
});

describe("TurnAnimation wake hook (render on demand)", () => {
  it("wakes the frame loop when a turn, a settle or a jump starts, and not otherwise", async () => {
    const puzzle = { applyMove: vi.fn().mockResolvedValue(undefined), applyMoves: vi.fn().mockResolvedValue(undefined) } as unknown as PuzzleHandle;
    const scene = fakeScene();
    const wake = vi.fn();
    const anim = new TurnAnimation(puzzle, scene, MOVES_3, wake);
    expect(wake).not.toHaveBeenCalled();

    anim.beginFrom({ generator: 1, amount: 1 }, 0, -90);
    expect(wake).toHaveBeenCalledTimes(1);
    anim.update(10);
    await Promise.resolve();

    anim.beginSettle(0 /* Axis.X */, 1, 15);
    expect(wake).toHaveBeenCalledTimes(2);
    anim.update(10);

    await anim.jump([]);
    expect(wake).toHaveBeenCalledTimes(3);
  });

  it("under reduced motion every turn runs at the fastest speed, whatever speed it asked for", () => {
    const scene = fakeScene();
    const slow = new TurnAnimation(fakePuzzle(), scene, MOVES_3, undefined, () => false);
    const reduced = new TurnAnimation(fakePuzzle(), scene, MOVES_3, undefined, () => true);

    // A quarter turn at 90 deg/s takes a second; at the fastest speed it is over in the floor time.
    slow.beginFrom({ generator: 1, amount: 1 }, 0, 90, { speedDegPerSec: 90 });
    slow.update(0.5);
    expect(slow.isIdle()).toBe(false);

    reduced.beginFrom({ generator: 1, amount: 1 }, 0, 90, { speedDegPerSec: 90 });
    reduced.update(ANIM_RELEASE_MIN_SEC + 0.001);
    expect(reduced.isIdle()).toBe(true);
  });
});
