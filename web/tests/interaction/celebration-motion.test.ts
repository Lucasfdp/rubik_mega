import { describe, expect, it } from "vitest";
import { makeConfetti, spinProgress, stepConfetti } from "../../src/interaction/celebration-motion.js";

describe("spinProgress", () => {
  it("starts at rest, ends at rest, and passes halfway at the middle", () => {
    expect(spinProgress(0, 2)).toBe(0);
    expect(spinProgress(1, 2)).toBeCloseTo(0.5);
    expect(spinProgress(2, 2)).toBe(1);
  });

  it("never goes backwards, and clamps outside the duration", () => {
    let last = -1;
    for (let t = -0.5; t <= 2.5; t += 0.05) {
      const p = spinProgress(t, 2);
      expect(p).toBeGreaterThanOrEqual(last);
      last = p;
    }
    expect(spinProgress(-3, 2)).toBe(0);
    expect(spinProgress(9, 2)).toBe(1);
  });

  it("is slow at the ends and fast in the middle", () => {
    expect(spinProgress(0.1, 2)).toBeLessThan(0.01);
    expect(spinProgress(1.1, 2) - spinProgress(0.9, 2)).toBeGreaterThan(0.2);
  });
});

describe("confetti", () => {
  const seeded = (): (() => number) => {
    let s = 7;
    return () => ((s = (s * 1664525 + 1013904223) >>> 0) / 0x100000000);
  };

  it("makes the requested number, in the given colours, inside the area", () => {
    const parts = makeConfetti(40, ["#111111", "#222222"], 800, 600, seeded());
    expect(parts).toHaveLength(40);
    for (const p of parts) {
      expect(["#111111", "#222222"]).toContain(p.colour);
      expect(p.x).toBe(400);
      expect(p.y).toBeGreaterThan(0);
      expect(p.y).toBeLessThan(600);
      expect(p.vy).toBeLessThan(0); // thrown upwards
    }
  });

  it("falls under gravity and all of it is gone after the longest life", () => {
    let parts = makeConfetti(30, ["#fff"], 800, 600, seeded());
    const startVy = parts.map((p) => p.vy);
    parts = stepConfetti(parts, 0.1);
    parts.forEach((p, i) => expect(p.vy).toBeGreaterThan(startVy[i]! * 0.84 - 1)); // slowed upward, pulled down
    for (let t = 0; t < 4; t += 0.1) parts = stepConfetti(parts, 0.1);
    expect(parts).toHaveLength(0);
  });

  it("does not mutate the particles it was given", () => {
    const parts = makeConfetti(3, ["#fff"], 100, 100, seeded());
    const copy = JSON.parse(JSON.stringify(parts));
    stepConfetti(parts, 0.5);
    expect(parts).toEqual(copy);
  });
});
