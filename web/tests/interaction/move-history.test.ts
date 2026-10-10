import { describe, expect, it } from "vitest";
import { MoveHistory } from "../../src/interaction/move-history.js";

const R = { generator: 1, amount: 1 };
const U = { generator: 0, amount: 3 };

describe("MoveHistory", () => {
  it("starts with nothing to undo or redo", () => {
    const h = new MoveHistory();
    expect(h.canUndo).toBe(false);
    expect(h.canRedo).toBe(false);
    expect(h.undo()).toBeNull();
    expect(h.redo()).toBeNull();
  });

  it("undoes moves latest first and redoes them in the order they were made", () => {
    const h = new MoveHistory();
    h.record(R);
    h.record(U);
    expect(h.undo()).toEqual(U);
    expect(h.undo()).toEqual(R);
    expect(h.canUndo).toBe(false);
    expect(h.redo()).toEqual(R);
    expect(h.redo()).toEqual(U);
    expect(h.canRedo).toBe(false);
  });

  it("drops the redo stack when a new move is made", () => {
    const h = new MoveHistory();
    h.record(R);
    h.undo();
    h.record(U);
    expect(h.canRedo).toBe(false);
    expect(h.undo()).toEqual(U);
  });

  it("counts every turn, undo and redo included", () => {
    const h = new MoveHistory();
    h.record(R);
    h.record(U);
    h.undo();
    h.redo();
    expect(h.turns).toBe(4);
  });

  it("copies what it records, so a caller mutating its move cannot rewrite history", () => {
    const h = new MoveHistory();
    const move = { generator: 1, amount: 1 };
    h.record(move);
    move.amount = 2;
    expect(h.undo()).toEqual({ generator: 1, amount: 1 });
  });

  it("clear() forgets everything", () => {
    const h = new MoveHistory();
    h.record(R);
    h.undo();
    h.clear();
    expect(h.canUndo || h.canRedo).toBe(false);
    expect(h.turns).toBe(0);
  });
});
