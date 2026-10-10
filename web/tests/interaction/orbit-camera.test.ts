import * as THREE from "three";
import { describe, expect, it } from "vitest";
import { applyOrbitToCamera, createOrbitCamera, orbitDrag, orbitNudge, orbitZoom } from "../../src/interaction/orbit-camera.js";

describe("createOrbitCamera", () => {
  it("starts at rubik's own default preset (yaw 40, pitch 25, distance 11)", () => {
    expect(createOrbitCamera()).toEqual({ yawDeg: 40, pitchDeg: 25, distance: 11 });
  });
});

describe("orbitDrag", () => {
  it("dragging right (+deltaX) decreases yaw, dragging up (-deltaY) decreases pitch", () => {
    // Pitch is deliberately inverted from rubik's own convention --
    // Lucas: the original (drag up -> pitch increases) felt unnatural.
    // See orbitDrag()'s own doc comment / DECISIONS.md.
    const orbit = createOrbitCamera();
    orbitDrag(orbit, 10, -10);
    expect(orbit.yawDeg).toBeLessThan(40);
    expect(orbit.pitchDeg).toBeLessThan(25);
  });

  it("clamps pitch just short of the poles (gimbal-lock guard)", () => {
    const orbit = createOrbitCamera();
    orbitDrag(orbit, 0, 100000);
    expect(orbit.pitchDeg).toBeLessThanOrEqual(89);
    orbitDrag(orbit, 0, -200000);
    expect(orbit.pitchDeg).toBeGreaterThanOrEqual(-89);
  });
});

describe("orbitZoom", () => {
  it("a positive delta zooms in (shrinks distance), clamped to [4, 20]", () => {
    const orbit = createOrbitCamera();
    orbitZoom(orbit, 3);
    expect(orbit.distance).toBe(8);
    orbitZoom(orbit, 1000);
    expect(orbit.distance).toBe(4);
    orbitZoom(orbit, -1000);
    expect(orbit.distance).toBe(20);
  });
});

describe("applyOrbitToCamera", () => {
  it("yaw=0, pitch=0 puts the camera on the +Z axis, looking at the origin", () => {
    const camera = new THREE.PerspectiveCamera();
    applyOrbitToCamera({ yawDeg: 0, pitchDeg: 0, distance: 10 }, camera);
    expect(camera.position.x).toBeCloseTo(0);
    expect(camera.position.y).toBeCloseTo(0);
    expect(camera.position.z).toBeCloseTo(10);
  });

  it("yaw=90, pitch=0 puts the camera on the +X axis", () => {
    const camera = new THREE.PerspectiveCamera();
    applyOrbitToCamera({ yawDeg: 90, pitchDeg: 0, distance: 10 }, camera);
    expect(camera.position.x).toBeCloseTo(10);
    expect(camera.position.z).toBeCloseTo(0, 5);
  });

  it("pitch=90 puts the camera straight up on +Y", () => {
    const camera = new THREE.PerspectiveCamera();
    applyOrbitToCamera({ yawDeg: 0, pitchDeg: 90, distance: 10 }, camera);
    expect(camera.position.y).toBeCloseTo(10);
  });
});

describe("orbitNudge", () => {
  it("turns the view by the given degrees", () => {
    const orbit = { yawDeg: 40, pitchDeg: 25, distance: 10 };
    orbitNudge(orbit, 5, -10);
    expect(orbit).toEqual({ yawDeg: 45, pitchDeg: 15, distance: 10 });
  });

  it("stops short of the poles, like a drag", () => {
    const orbit = { yawDeg: 0, pitchDeg: 85, distance: 10 };
    orbitNudge(orbit, 0, 50);
    expect(orbit.pitchDeg).toBe(89);
    orbitNudge(orbit, 0, -500);
    expect(orbit.pitchDeg).toBe(-89);
  });
});
