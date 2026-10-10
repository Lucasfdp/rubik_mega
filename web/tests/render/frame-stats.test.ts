import { describe, expect, it } from "vitest";
import { FrameStats } from "../../src/render/frame-stats.js";

describe("FrameStats", () => {
  it("has no percentile or mean with no samples", () => {
    const stats = new FrameStats();
    expect(stats.percentile(95)).toBeNull();
    expect(stats.mean()).toBeNull();
  });

  it("uses nearest-rank percentiles", () => {
    const stats = new FrameStats();
    for (let ms = 1; ms <= 100; ms++) stats.record(ms);
    expect(stats.percentile(50)).toBe(50);
    expect(stats.percentile(95)).toBe(95);
    expect(stats.percentile(100)).toBe(100);
    expect(stats.percentile(0)).toBe(1);
  });

  it("p95 shows a slow tail the mean hides", () => {
    const stats = new FrameStats();
    for (let i = 0; i < 94; i++) stats.record(10);
    for (let i = 0; i < 6; i++) stats.record(50);
    expect(stats.mean()).toBeCloseTo(12.4, 6);
    expect(stats.percentile(95)).toBe(50);
  });

  it("forgets the oldest sample once the window is full", () => {
    const stats = new FrameStats(3);
    [100, 1, 2, 3].forEach((ms) => stats.record(ms));
    expect(stats.count).toBe(3);
    expect(stats.percentile(100)).toBe(3);
  });

  it("clear empties the window", () => {
    const stats = new FrameStats();
    stats.record(5);
    stats.clear();
    expect(stats.count).toBe(0);
  });

  it("rejects a non-positive capacity", () => {
    expect(() => new FrameStats(0)).toThrow();
  });
});
