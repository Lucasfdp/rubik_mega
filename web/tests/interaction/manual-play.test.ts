import { describe, expect, it, vi } from "vitest";
import { ManualPlay } from "../../src/interaction/manual-play.js";
import { MoveMap } from "../../src/interaction/move-axis.js";
import type { PuzzleHandle } from "../../src/engine/puzzle-handle.js";
import type { TurnAnimation, TurnOptions } from "../../src/interaction/turn-anim.js";
import type { Move } from "../../src/engine/types.js";

const MOVES_3 = new MoveMap(3, [0, 1, 2, 3, 4, 5].map((face) => ({ face, depth: 0 })));
const R: Move = { generator: 1, amount: 1 };

function setup(options: { solvedAfter?: () => boolean } = {}) {
  let idle = true;
  let settle: (() => void) | null = null;
  const played: Move[] = [];
  const anim = {
    moves: MOVES_3,
    isIdle: () => idle,
    beginFrom: vi.fn((move: Move, _from: number, _to: number, opts?: TurnOptions) => {
      played.push(move);
      idle = false;
      settle = () => {
        idle = true;
        opts?.onSettled?.();
      };
    }),
  } as unknown as TurnAnimation;
  let solved = true;
  const puzzle = {
    isSolved: () => options.solvedAfter?.() ?? solved,
    isMoveLegal: () => true,
  } as unknown as PuzzleHandle;
  const events = { onChange: vi.fn(), onManualTurn: vi.fn(), onSolved: vi.fn() };
  const play = new ManualPlay(anim, puzzle, events);
  return {
    play,
    events,
    played,
    land: () => settle?.(),
    setSolved: (v: boolean) => (solved = v),
  };
}

describe("ManualPlay", () => {
  it("turn() plays the move, records it, and reports a manual turn", () => {
    const { play, events, played } = setup();
    expect(play.turn(R)).toBe(true);
    expect(played).toEqual([R]);
    expect(events.onManualTurn).toHaveBeenCalledTimes(1);
    expect(play.turns).toBe(1);
  });

  it("turnBy() turns one step clockwise, or the inverse step anticlockwise", () => {
    const { play, land, played } = setup();
    play.turnBy(2, 1);
    land();
    play.turnBy(2, -1);
    expect(played).toEqual([
      { generator: 2, amount: 1 },
      { generator: 2, amount: 3 },
    ]);
  });

  it("refuses a second turn until the first has landed", () => {
    const { play, land, played } = setup();
    play.turn(R);
    expect(play.turn(R)).toBe(false);
    land();
    expect(play.turn(R)).toBe(true);
    expect(played).toHaveLength(2);
  });

  it("undo() plays the inverse once the move has landed, redo() plays it again", () => {
    const { play, land, played } = setup();
    play.turn(R);
    expect(play.canUndo).toBe(false); // still settling
    land();
    expect(play.canUndo).toBe(true);

    play.undo();
    expect(played.at(-1)).toEqual({ generator: 1, amount: 3 });
    land();
    expect(play.canRedo).toBe(true);

    play.redo();
    expect(played.at(-1)).toEqual(R);
  });

  it("commitOptions() records a dragged move and holds undo back until it settles", () => {
    const { play, events } = setup();
    const options = play.commitOptions(R);
    expect(events.onManualTurn).toHaveBeenCalled();
    expect(play.canUndo).toBe(false);
    options.onSettled?.();
    expect(play.canUndo).toBe(true);
  });

  it("celebrates a scrambled puzzle turned to solved, once, with the turn count", () => {
    const { play, land, events, setSolved } = setup();
    setSolved(false);
    play.arm();
    expect(play.armed).toBe(true);

    play.turn(R);
    setSolved(true);
    land();
    expect(events.onSolved).toHaveBeenCalledWith(1);

    play.turn(R);
    land();
    expect(events.onSolved).toHaveBeenCalledTimes(1);
  });

  it("does not celebrate a puzzle that was never armed (a played solution, a fresh puzzle)", () => {
    const { play, land, events } = setup();
    play.turn(R);
    land();
    expect(events.onSolved).not.toHaveBeenCalled();
  });

  it("does not arm on a puzzle that is already solved", () => {
    const { play } = setup();
    play.arm();
    expect(play.armed).toBe(false);
  });

  it("reset() clears the history and disarms", () => {
    const { play, land, setSolved } = setup();
    setSolved(false);
    play.arm();
    play.turn(R);
    land();
    play.reset();
    expect(play.canUndo).toBe(false);
    expect(play.armed).toBe(false);
    expect(play.turns).toBe(0);
  });
});
