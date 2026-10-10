import { describe, expect, it } from "vitest";
import { MAX_COVER_FRACTION, clampVisible, coverInset, nearestSnap, snapHeights, steppedState, toggledState, type SheetMetrics } from "../../../src/ui/shell/sheet-snap.js";

const phone: SheetMetrics = { viewportHeight: 844, peekHeight: 120, topReserve: 56 };

describe("sheet snap", () => {
  it("orders peek <= half <= full, even on a tiny screen", () => {
    for (const m of [phone, { viewportHeight: 300, peekHeight: 120, topReserve: 56 }, { viewportHeight: 100, peekHeight: 120, topReserve: 56 }]) {
      const h = snapHeights(m);
      expect(h.peek).toBeLessThanOrEqual(h.half);
      expect(h.half).toBeLessThanOrEqual(h.full);
    }
  });

  it("settles on the nearest snap when released at rest", () => {
    const h = snapHeights(phone);
    expect(nearestSnap(h.peek + 10, 0, h)).toBe("peek");
    expect(nearestSnap(h.half - 10, 0, h)).toBe("half");
    expect(nearestSnap(h.full - 10, 0, h)).toBe("full");
  });

  it("a fast upward flick skips ahead, a downward one falls back", () => {
    const h = snapHeights(phone);
    expect(nearestSnap(h.peek + 20, 3000, h)).not.toBe("peek");
    expect(nearestSnap(h.full - 20, -3000, h)).not.toBe("full");
  });

  it("clamps dragged heights", () => {
    const h = snapHeights(phone);
    expect(clampVisible(0, h)).toBe(h.peek);
    expect(clampVisible(99999, h)).toBe(h.full);
  });

  it("toggles and steps", () => {
    expect(toggledState("peek")).toBe("half");
    expect(toggledState("full")).toBe("peek");
    expect(steppedState("peek", -1)).toBe("peek");
    expect(steppedState("peek", 1)).toBe("half");
    expect(steppedState("full", 1)).toBe("full");
  });

  it("never lets the camera treat more than the cap as covered", () => {
    expect(coverInset("full", phone)).toBeCloseTo(phone.viewportHeight * MAX_COVER_FRACTION);
    expect(coverInset("peek", phone)).toBe(snapHeights(phone).peek);
  });
});
