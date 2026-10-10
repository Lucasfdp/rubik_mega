import { describe, expect, it } from "vitest";
import {
  DOUBLE_TAP_GAP_MS,
  DOUBLE_TAP_SLOP_PX,
  DoubleTapDetector,
  TAP_MAX_MOVE_PX,
  TAP_MAX_MS,
  isTap,
} from "../../src/interaction/double-tap.js";

describe("isTap", () => {
  const down = { point: { x: 100, y: 100 }, timeMs: 1000 };
  it("accepts a short, still press", () => {
    expect(isTap(down, { point: { x: 103, y: 102 }, timeMs: 1000 + TAP_MAX_MS })).toBe(true);
  });
  it("rejects a long press", () => {
    expect(isTap(down, { point: down.point, timeMs: 1000 + TAP_MAX_MS + 1 })).toBe(false);
  });
  it("rejects a press that moved", () => {
    expect(isTap(down, { point: { x: 100 + TAP_MAX_MOVE_PX + 1, y: 100 }, timeMs: 1100 })).toBe(false);
  });
});

describe("DoubleTapDetector", () => {
  it("a single tap is not a double tap", () => {
    expect(new DoubleTapDetector().tap({ x: 10, y: 10 }, 0)).toBe(false);
  });

  it("two quick taps in the same place are", () => {
    const detector = new DoubleTapDetector();
    detector.tap({ x: 10, y: 10 }, 0);
    expect(detector.tap({ x: 14, y: 12 }, DOUBLE_TAP_GAP_MS)).toBe(true);
  });

  it("two slow taps are not", () => {
    const detector = new DoubleTapDetector();
    detector.tap({ x: 10, y: 10 }, 0);
    expect(detector.tap({ x: 10, y: 10 }, DOUBLE_TAP_GAP_MS + 1)).toBe(false);
  });

  it("two taps far apart are not", () => {
    const detector = new DoubleTapDetector();
    detector.tap({ x: 10, y: 10 }, 0);
    expect(detector.tap({ x: 10 + DOUBLE_TAP_SLOP_PX + 1, y: 10 }, 100)).toBe(false);
  });

  it("a third tap starts a new pair", () => {
    const detector = new DoubleTapDetector();
    detector.tap({ x: 0, y: 0 }, 0);
    expect(detector.tap({ x: 0, y: 0 }, 100)).toBe(true);
    expect(detector.tap({ x: 0, y: 0 }, 200)).toBe(false);
    expect(detector.tap({ x: 0, y: 0 }, 300)).toBe(true);
  });

  it("reset forgets a pending first tap", () => {
    const detector = new DoubleTapDetector();
    detector.tap({ x: 0, y: 0 }, 0);
    detector.reset();
    expect(detector.tap({ x: 0, y: 0 }, 100)).toBe(false);
  });
});
