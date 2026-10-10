import { describe, expect, it } from "vitest";
import {
  INERTIA_START_SPEED,
  INERTIA_STALE_MS,
  INERTIA_TIME_CONSTANT_SEC,
  OrbitInertia,
} from "../../src/interaction/inertia.js";
import { createOrbitCamera } from "../../src/interaction/orbit-camera.js";

const FRAME = 1 / 60;

/// A flick: `speed` px/s to the right, sampled over a few moves ending at t = 100 ms.
function flick(inertia: OrbitInertia, speed: number): void {
  for (let i = 1; i <= 5; i++) inertia.track(speed * FRAME, 0, FRAME, 20 * i);
}

describe("OrbitInertia", () => {
  it("does not glide after a slow release", () => {
    const inertia = new OrbitInertia();
    flick(inertia, INERTIA_START_SPEED / 4);
    inertia.release(110);
    expect(inertia.active).toBe(false);
  });

  it("does not glide when the pointer rested before release", () => {
    const inertia = new OrbitInertia();
    flick(inertia, 1000);
    inertia.release(100 + INERTIA_STALE_MS + 1);
    expect(inertia.active).toBe(false);
  });

  it("glides after a fast release, in the direction of the flick", () => {
    const inertia = new OrbitInertia();
    const orbit = createOrbitCamera();
    flick(inertia, 1000);
    inertia.release(110);
    expect(inertia.active).toBe(true);
    const before = orbit.yawDeg;
    inertia.step(FRAME, orbit);
    // Dragging right decreases yaw (orbit-camera.ts).
    expect(orbit.yawDeg).toBeLessThan(before);
  });

  it("decays to zero and stops by itself", () => {
    const inertia = new OrbitInertia();
    const orbit = createOrbitCamera();
    flick(inertia, 2000);
    inertia.release(110);
    let frames = 0;
    while (inertia.step(FRAME, orbit) && frames < 600) frames++;
    expect(inertia.active).toBe(false);
    expect(frames).toBeGreaterThan(10);
    expect(frames).toBeLessThan(300);
    const settled = orbit.yawDeg;
    inertia.step(FRAME, orbit);
    expect(orbit.yawDeg).toBe(settled);
  });

  it("covers the same distance however the frames are cut up", () => {
    const run = (dt: number): number => {
      const inertia = new OrbitInertia();
      const orbit = createOrbitCamera();
      flick(inertia, 1500);
      inertia.release(110);
      for (let t = 0; t < 1 && inertia.active; t += dt) inertia.step(dt, orbit);
      return orbit.yawDeg;
    };
    // Within a degree of a ~90 degree glide; the difference is only where each stops under the floor speed.
    expect(Math.abs(run(1 / 30) - run(1 / 240))).toBeLessThan(1);
  });

  it("travels about speed * time constant in total", () => {
    const inertia = new OrbitInertia();
    const orbit = createOrbitCamera();
    flick(inertia, 1000);
    inertia.release(110);
    const start = orbit.yawDeg;
    while (inertia.step(FRAME, orbit));
    // 1000 px/s * 0.3 s = 300 px at 0.3 deg/px = 90 deg (a little less: it stops below the floor speed).
    const travelled = start - orbit.yawDeg;
    expect(travelled).toBeGreaterThan(60);
    expect(travelled).toBeLessThan(0.3 * 1000 * INERTIA_TIME_CONSTANT_SEC + 1);
  });

  it("stop() halts it at once, and a new touch gets a clean start", () => {
    const inertia = new OrbitInertia();
    const orbit = createOrbitCamera();
    flick(inertia, 1000);
    inertia.release(110);
    inertia.stop();
    expect(inertia.active).toBe(false);
    const before = orbit.yawDeg;
    expect(inertia.step(FRAME, orbit)).toBe(false);
    expect(orbit.yawDeg).toBe(before);
    // Releasing again without any new movement does not resurrect the old speed.
    inertia.release(500);
    expect(inertia.active).toBe(false);
  });

  it("disabled (reduced motion) never glides", () => {
    const inertia = new OrbitInertia(false);
    flick(inertia, 5000);
    inertia.release(110);
    expect(inertia.active).toBe(false);
  });
});
