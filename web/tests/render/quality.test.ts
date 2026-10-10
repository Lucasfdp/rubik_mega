import { describe, expect, it } from "vitest";
import {
  ADAPTIVE_SUSTAIN_MS,
  AdaptivePixelRatio,
  FRAME_BUDGET_MS,
  TIER_SETTINGS,
  pickDefaultTier,
  pixelRatioFor,
  resolveTier,
} from "../../src/render/quality.js";

describe("pickDefaultTier", () => {
  it("starts a two-core device on low", () => {
    expect(pickDefaultTier({ hardwareConcurrency: 2, shortSidePx: 390 })).toBe("low");
  });
  it("starts a phone with many cores on standard (small screen)", () => {
    expect(pickDefaultTier({ hardwareConcurrency: 8, shortSidePx: 390 })).toBe("standard");
  });
  it("starts a big, many-core screen on high", () => {
    expect(pickDefaultTier({ hardwareConcurrency: 12, shortSidePx: 900 })).toBe("high");
  });
  it("gives a browser that hides its core count standard, not low", () => {
    expect(pickDefaultTier({ hardwareConcurrency: undefined, shortSidePx: 390 })).toBe("standard");
  });
});

describe("resolveTier", () => {
  it("passes an explicit choice through and resolves auto", () => {
    expect(resolveTier("low", { hardwareConcurrency: 16, shortSidePx: 1000 })).toBe("low");
    expect(resolveTier("auto", { hardwareConcurrency: 16, shortSidePx: 1000 })).toBe("high");
  });
});

describe("pixelRatioFor", () => {
  it("is the device ratio capped by the tier and by the adaptive cap, never below 1", () => {
    expect(pixelRatioFor(3, "standard", 2)).toBe(2);
    expect(pixelRatioFor(3, "low", 2)).toBe(1);
    expect(pixelRatioFor(3, "standard", 1.5)).toBe(1.5);
    expect(pixelRatioFor(0.75, "standard", 2)).toBe(1);
  });
});

describe("tier table", () => {
  it("low turns every costly switch off", () => {
    expect(TIER_SETTINGS.low).toMatchObject({ environment: false, contactShadow: false, roundedBodies: false, maxPixelRatio: 1 });
  });
});

describe("AdaptivePixelRatio", () => {
  /// Feeds `frameMs` once per frame for `seconds`, returning every drop.
  function run(adaptive: AdaptivePixelRatio, frameMs: number, seconds: number, startMs = 0): number[] {
    const drops: number[] = [];
    for (let t = startMs; t < startMs + seconds * 1000; t += frameMs) {
      const next = adaptive.observe(frameMs, t);
      if (next !== null) drops.push(next);
    }
    return drops;
  }

  it("never drops while frames hold the budget", () => {
    const adaptive = new AdaptivePixelRatio(2);
    expect(run(adaptive, FRAME_BUDGET_MS, 30)).toEqual([]);
    expect(adaptive.cap).toBe(2);
  });

  it("does not drop on a brief stutter", () => {
    const adaptive = new AdaptivePixelRatio(2);
    run(adaptive, FRAME_BUDGET_MS, 5);
    expect(run(adaptive, 40, 1, 5000)).toEqual([]);
  });

  it("drops one step after the slowdown is sustained", () => {
    const adaptive = new AdaptivePixelRatio(2);
    const drops = run(adaptive, 40, ADAPTIVE_SUSTAIN_MS / 1000 + 2);
    expect(drops[0]).toBe(1.5);
  });

  it("steps 2, 1.5, 1 and then stops, never going back up", () => {
    const adaptive = new AdaptivePixelRatio(2);
    const drops = run(adaptive, 40, 60);
    expect(drops).toEqual([1.5, 1]);
    expect(adaptive.cap).toBe(1);
    run(adaptive, FRAME_BUDGET_MS, 60, 100_000);
    expect(adaptive.cap).toBe(1);
  });

  it("a cap that is already 1 never drops", () => {
    const adaptive = new AdaptivePixelRatio(1);
    expect(run(adaptive, 60, 30)).toEqual([]);
  });

  it("a device already below a step drops to the next step beneath it", () => {
    const adaptive = new AdaptivePixelRatio(1.75);
    expect(run(adaptive, 40, 5)[0]).toBe(1.5);
  });
});
