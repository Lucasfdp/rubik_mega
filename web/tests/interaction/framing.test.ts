import { PerspectiveCamera, Vector3 } from "three";
import { describe, expect, it } from "vitest";
import {
  CAMERA_FOV_DEG,
  FRAME_FILL_FRACTION,
  MIN_FREE_PX,
  ZOOM_DISTANCE_RANGE,
  approach,
  fitDistance,
  framingFor,
  freeRectFor,
  rescaleDistance,
  zoomLimits,
} from "../../src/interaction/framing.js";

/// What a real PerspectiveCamera does with the maths: put a camera at
/// `distance` looking at the origin, apply the view offset, and report
/// where the origin and the bounding sphere's edge land on screen.
function project(distance: number, width: number, height: number, offset: { x: number; y: number }, radius: number) {
  const camera = new PerspectiveCamera(CAMERA_FOV_DEG, width / height, 0.1, 200);
  camera.position.set(0, 0, distance);
  camera.lookAt(0, 0, 0);
  camera.setViewOffset(width, height, offset.x, offset.y, width, height);
  camera.updateProjectionMatrix();
  camera.updateMatrixWorld(true);
  const toPx = (v: Vector3) => {
    const ndc = v.clone().project(camera);
    return { x: ((ndc.x + 1) / 2) * width, y: ((1 - ndc.y) / 2) * height };
  };
  const centre = toPx(new Vector3(0, 0, 0));
  // The sphere's silhouette edge: the point where the line from the
  // camera touches the sphere. At distance d it is sqrt(d^2 - R^2) along
  // the tangent, which leaves the axis at asin(R / d).
  const along = Math.sqrt(distance * distance - radius * radius);
  const edge = toPx(new Vector3(0, (along * radius) / distance, (radius * radius) / distance));
  return { centre, radiusPx: Math.abs(centre.y - edge.y) };
}

describe("fitDistance", () => {
  it("puts the sphere's outline at the requested pixel radius on a real camera", () => {
    for (const [height, radius, target] of [[844, 2.6, 175], [390, 1.9, 150], [1080, 2.6, 400]] as const) {
      const distance = fitDistance(radius, CAMERA_FOV_DEG, height, target);
      const shown = project(distance, 800, height, { x: 0, y: 0 }, radius);
      expect(shown.radiusPx).toBeCloseTo(target, 3);
    }
  });

  it("a bigger target puts the camera closer; a bigger sphere puts it farther", () => {
    expect(fitDistance(2, 50, 800, 300)).toBeLessThan(fitDistance(2, 50, 800, 200));
    expect(fitDistance(3, 50, 800, 200)).toBeGreaterThan(fitDistance(2, 50, 800, 200));
  });

  it("keeps the camera outside the sphere", () => {
    expect(fitDistance(2, 50, 800, 399)).toBeGreaterThan(2);
  });

  it("rejects non-positive inputs", () => {
    expect(() => fitDistance(0, 50, 800, 100)).toThrow();
    expect(() => fitDistance(2, 50, 0, 100)).toThrow();
    expect(() => fitDistance(2, 50, 800, 0)).toThrow();
  });
});

describe("freeRectFor", () => {
  it("is the viewport minus the insets", () => {
    expect(freeRectFor({ width: 1000, height: 800 }, { top: 60, right: 300, bottom: 20, left: 10 })).toEqual({ x: 10, y: 60, width: 690, height: 720 });
  });

  it("treats a negative inset as zero", () => {
    expect(freeRectFor({ width: 500, height: 500 }, { top: -5, right: 0, bottom: 0, left: 0 })).toEqual({ x: 0, y: 0, width: 500, height: 500 });
  });

  it("never reports less than MIN_FREE_PX, however large the insets", () => {
    const rect = freeRectFor({ width: 400, height: 300 }, { top: 250, right: 380, bottom: 200, left: 0 });
    expect(rect.width).toBe(MIN_FREE_PX);
    expect(rect.height).toBe(MIN_FREE_PX);
  });

  it("a tiny viewport is reported whole rather than padded past its size", () => {
    const rect = freeRectFor({ width: 80, height: 80 }, { top: 0, right: 0, bottom: 0, left: 0 });
    expect(rect.width).toBe(80);
  });
});

describe("framingFor", () => {
  const RADIUS = 2.6;

  it("centres the puzzle in the free area on a real camera, for any layout", () => {
    const layouts = [
      { viewport: { width: 390, height: 844 }, insets: { top: 56, right: 0, bottom: 120, left: 0 } }, // phone, sheet peeking
      { viewport: { width: 844, height: 390 }, insets: { top: 48, right: 260, bottom: 0, left: 0 } }, // landscape phone, side panel
      { viewport: { width: 1440, height: 900 }, insets: { top: 56, right: 360, bottom: 0, left: 0 } }, // desktop
      { viewport: { width: 820, height: 1180 }, insets: { top: 56, right: 0, bottom: 480, left: 0 } }, // tablet, sheet half open
      { viewport: { width: 600, height: 600 }, insets: { top: 0, right: 0, bottom: 0, left: 0 } },
    ];
    for (const { viewport, insets } of layouts) {
      const free = freeRectFor(viewport, insets);
      const framing = framingFor(RADIUS, viewport, free);
      const shown = project(framing.distance, viewport.width, viewport.height, { x: framing.offsetX, y: framing.offsetY }, RADIUS);
      expect(shown.centre.x).toBeCloseTo(free.x + free.width / 2, 3);
      expect(shown.centre.y).toBeCloseTo(free.y + free.height / 2, 3);
      expect(shown.radiusPx * 2).toBeCloseTo(FRAME_FILL_FRACTION * Math.min(free.width, free.height), 3);
    }
  });

  it("with nothing in the way there is no offset", () => {
    const viewport = { width: 800, height: 600 };
    const framing = framingFor(RADIUS, viewport, freeRectFor(viewport, { top: 0, right: 0, bottom: 0, left: 0 }));
    expect(framing.offsetX).toBe(0);
    expect(framing.offsetY).toBe(0);
  });

  it("fits the shorter side: a narrow phone frames by its width, not its height", () => {
    const tall = { width: 390, height: 844 };
    const free = (v: typeof tall) => freeRectFor(v, { top: 0, right: 0, bottom: 0, left: 0 });
    const tallPx = project(framingFor(RADIUS, tall, free(tall)).distance, tall.width, tall.height, { x: 0, y: 0 }, RADIUS).radiusPx;
    expect(tallPx * 2).toBeLessThanOrEqual(tall.width);
  });

  it("a different radius changes the distance but not the on-screen size", () => {
    const viewport = { width: 600, height: 800 };
    const free = freeRectFor(viewport, { top: 0, right: 0, bottom: 0, left: 0 });
    const small = framingFor(1.9, viewport, free);
    const large = framingFor(2.6, viewport, free);
    expect(large.distance).toBeGreaterThan(small.distance);
    expect(project(small.distance, 600, 800, { x: 0, y: 0 }, 1.9).radiusPx).toBeCloseTo(project(large.distance, 600, 800, { x: 0, y: 0 }, 2.6).radiusPx, 3);
  });
});

describe("zoom helpers", () => {
  it("zoomLimits are the fixed multiples of the fit", () => {
    expect(zoomLimits(10)).toEqual({ min: 10 * ZOOM_DISTANCE_RANGE.min, max: 10 * ZOOM_DISTANCE_RANGE.max });
  });

  it("rescaleDistance keeps the zoom ratio across a refit", () => {
    expect(rescaleDistance(8, 10, 20)).toBe(16);
    expect(rescaleDistance(10, 10, 7)).toBe(7);
  });
});

describe("approach", () => {
  it("moves towards the target without overshooting, frame-rate independent", () => {
    let slow = 0;
    for (let i = 0; i < 10; i++) slow = approach(slow, 100, 0.05, 12);
    let fast = 0;
    for (let i = 0; i < 50; i++) fast = approach(fast, 100, 0.01, 12);
    expect(slow).toBeLessThan(100);
    expect(slow).toBeCloseTo(fast, 4);
  });

  it("snaps once within epsilon, so a tween ends", () => {
    let value = 0;
    for (let i = 0; i < 400 && value !== 100; i++) value = approach(value, 100, 1 / 60, 12);
    expect(value).toBe(100);
  });
});
