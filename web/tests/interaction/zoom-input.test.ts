import { describe, expect, it } from "vitest";
import { WHEEL_DELTA_LINE, WHEEL_DELTA_PIXEL, pinchZoomDelta, wheelZoomDelta } from "../../src/interaction/zoom-input.js";
import { createOrbitCamera, orbitZoom } from "../../src/interaction/orbit-camera.js";
import { zoomLimits } from "../../src/interaction/framing.js";

describe("wheelZoomDelta", () => {
  it("scrolling up (negative deltaY) zooms in; scrolling down zooms out", () => {
    expect(wheelZoomDelta(-100, WHEEL_DELTA_PIXEL, 10)).toBeGreaterThan(0);
    expect(wheelZoomDelta(100, WHEEL_DELTA_PIXEL, 10)).toBeLessThan(0);
  });

  it("a 100 px notch moves the camera 15 % of the fitted distance", () => {
    expect(wheelZoomDelta(-100, WHEEL_DELTA_PIXEL, 10)).toBeCloseTo(1.5, 9);
  });

  it("scales with the fit, so every puzzle zooms by the same proportion", () => {
    expect(wheelZoomDelta(-100, WHEEL_DELTA_PIXEL, 20)).toBeCloseTo(2 * wheelZoomDelta(-100, WHEEL_DELTA_PIXEL, 10), 9);
  });

  it("treats a line-mode wheel (Firefox) like the equivalent pixels", () => {
    expect(wheelZoomDelta(-3, WHEEL_DELTA_LINE, 10)).toBeCloseTo(wheelZoomDelta(-48, WHEEL_DELTA_PIXEL, 10), 9);
  });
});

describe("pinchZoomDelta", () => {
  it("fingers apart zoom in, and at the reference fit it is the old one-unit-per-pixel", () => {
    expect(pinchZoomDelta(4, 11)).toBe(4);
    expect(pinchZoomDelta(-4, 11)).toBe(-4);
  });
  it("scales with the fit", () => {
    expect(pinchZoomDelta(4, 22)).toBe(8);
  });
});

describe("orbitZoom with fitted limits", () => {
  it("clamps to the range given, not the old fixed one", () => {
    const orbit = { ...createOrbitCamera(), distance: 30 };
    const limits = zoomLimits(30);
    orbitZoom(orbit, 1000, limits);
    expect(orbit.distance).toBeCloseTo(18, 9);
    orbitZoom(orbit, -1000, limits);
    expect(orbit.distance).toBeCloseTo(75, 9);
  });
});
