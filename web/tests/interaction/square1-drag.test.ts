import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import * as THREE from "three";
import { describe, expect, it } from "vitest";
import { instantiatePuzzleModule } from "../../src/engine/wasm-module.js";
import { Puzzle } from "../../src/engine/puzzle.js";
import { PuzzleType, SQ1_GEN_BOTTOM, SQ1_GEN_SLICE, SQ1_GEN_TOP } from "../../src/engine/types.js";
import { BOTTOM_Y0, TOP_Y1 } from "../../src/render/square1/square1-geometry.js";
import { BLOCKED_SLICE_WOBBLE_DEG, buildSquare1Scene } from "../../src/render/square1/square1-scene.js";
import { Square1MoveModel } from "../../src/interaction/square1-moves.js";
import { square1DragAdapter } from "../../src/interaction/square1-drag.js";
import type { TurnDragSession } from "../../src/interaction/drag-adapter.js";

const WASM_PATH = fileURLToPath(new URL("../../../core/build/rubik_mega.wasm", import.meta.url));
const load = async () => instantiatePuzzleModule(readFileSync(WASM_PATH));

const W = 800;
const H = 600;

function cameraAt(y: number): THREE.PerspectiveCamera {
  const camera = new THREE.PerspectiveCamera(50, W / H, 0.1, 100);
  camera.position.set(0.5, y, 7);
  camera.lookAt(0, 0, 0);
  camera.updateMatrixWorld(true);
  camera.updateProjectionMatrix();
  return camera;
}

function toScreen(p: THREE.Vector3, camera: THREE.Camera): { x: number; y: number } {
  const ndc = p.clone().project(camera);
  return { x: ((ndc.x + 1) / 2) * W, y: ((1 - ndc.y) / 2) * H };
}

function rayThrough(pointer: { x: number; y: number }, camera: THREE.Camera): THREE.Ray {
  const raycaster = new THREE.Raycaster();
  raycaster.setFromCamera(new THREE.Vector2((pointer.x / W) * 2 - 1, -(pointer.y / H) * 2 + 1), camera);
  return raycaster.ray;
}

function rotateY(p: THREE.Vector3, deg: number): THREE.Vector3 {
  return p.clone().applyAxisAngle(new THREE.Vector3(0, 1, 0), (deg * Math.PI) / 180);
}

async function setup(prepare?: (puzzle: Puzzle) => Promise<void>) {
  const puzzle = Puzzle.create(await load(), PuzzleType.Square1, 0);
  await prepare?.(puzzle);
  const scene = buildSquare1Scene(puzzle);
  const moves = new Square1MoveModel();
  const adapter = square1DragAdapter(scene, puzzle, moves);
  return { puzzle, scene, adapter };
}

/// Starts a drag at the screen position of `world` and feeds it each of
/// `path` in turn.
function drag(
  adapter: ReturnType<typeof square1DragAdapter>,
  camera: THREE.Camera,
  start: { x: number; y: number },
  path: { x: number; y: number }[],
): TurnDragSession | null {
  const session = adapter.start(rayThrough(start, camera), camera, W, H, start);
  if (!session) return null;
  for (const p of path) session.drag(p, 0.016);
  return session;
}

describe("Square-1 move model", () => {
  const model = new Square1MoveModel();

  it("draws a top amount the short way, clockwise from above as negative", () => {
    expect([1, 2, 6, 7, 11].map((a) => model.quarterDegForAmount(SQ1_GEN_TOP, a))).toEqual([-30, -60, -180, 150, 30]);
  });

  it("draws a bottom amount the opposite way (clockwise from below is anticlockwise from above)", () => {
    expect([1, 2, 6, 7, 11].map((a) => model.quarterDegForAmount(SQ1_GEN_BOTTOM, a))).toEqual([30, 60, 180, -150, -30]);
  });

  it("the slice is 180 degrees and its own inverse; a layer's inverse is 12 - amount", () => {
    expect(model.quarterDegForAmount(SQ1_GEN_SLICE, 1)).toBe(180);
    expect(model.inverseAmount(1, SQ1_GEN_SLICE)).toBe(1);
    expect(model.inverseAmount(3, SQ1_GEN_TOP)).toBe(9);
    expect(model.inverseAmount(11, SQ1_GEN_BOTTOM)).toBe(1);
  });

  it("rejects out-of-range generators and amounts", () => {
    expect(() => model.quarterDegForAmount(3, 1)).toThrow(/out of range/);
    expect(() => model.quarterDegForAmount(SQ1_GEN_TOP, 12)).toThrow(/out of range/);
    expect(() => model.quarterDegForAmount(SQ1_GEN_SLICE, 2)).toThrow(/only amount 1/);
  });

  it("moveForUnits wraps and returns null for whole turns", () => {
    expect(model.moveForUnits(SQ1_GEN_TOP, -1)).toEqual({ generator: SQ1_GEN_TOP, amount: 11 });
    expect(model.moveForUnits(SQ1_GEN_BOTTOM, 13)).toEqual({ generator: SQ1_GEN_BOTTOM, amount: 1 });
    expect(model.moveForUnits(SQ1_GEN_TOP, 12)).toBeNull();
  });
});

describe("Square-1 drag", () => {
  const camera = cameraAt(5);

  it("a swing around the top layer snaps to the nearest 30 degrees and commits that top turn", async () => {
    const { adapter } = await setup();
    const grab = new THREE.Vector3(0.7, TOP_Y1, 0.4);
    // Clockwise seen from above is a negative right-handed angle: -64
    // degrees is 2.13 units, which snaps to 2.
    const target = rotateY(grab, -64);
    const session = drag(adapter, camera, toScreen(grab, camera), [
      toScreen(rotateY(grab, -20), camera),
      toScreen(rotateY(grab, -45), camera),
      toScreen(target, camera),
    ])!;
    expect(session.locked).toBe(true);
    expect(session.activeTurn()).toMatchObject({ generator: SQ1_GEN_TOP });
    expect(session.activeTurn()!.angleDeg).toBeCloseTo(-64, 0);
    const released = session.release();
    expect(released.move).toEqual({ generator: SQ1_GEN_TOP, amount: 2 });
    expect(released.toDeg).toBe(-60);
  });

  it("an anticlockwise swing of the top is the inverse amount", async () => {
    const { adapter } = await setup();
    const grab = new THREE.Vector3(-0.6, TOP_Y1, 0.5);
    const session = drag(adapter, camera, toScreen(grab, camera), [
      toScreen(rotateY(grab, 40), camera),
      toScreen(rotateY(grab, 62), camera),
    ])!;
    expect(session.release().move).toEqual({ generator: SQ1_GEN_TOP, amount: 10 });
  });

  it("the bottom layer, seen from below, turns the other way round for the same swing", async () => {
    const { adapter } = await setup();
    const below = cameraAt(-5);
    const grab = new THREE.Vector3(0.7, BOTTOM_Y0, 0.4);
    // Positive right-handed is anticlockwise from above = clockwise from
    // below = a positive bottom amount.
    const session = drag(adapter, below, toScreen(grab, below), [
      toScreen(rotateY(grab, 35), below),
      toScreen(rotateY(grab, 62), below),
    ])!;
    expect(session.release().move).toEqual({ generator: SQ1_GEN_BOTTOM, amount: 2 });
  });

  it("a vertical drag on the side of a right-half top piece is the slice, not a layer turn", async () => {
    const { adapter } = await setup();
    const front = cameraAt(0.2);
    const grab = new THREE.Vector3(0.9, 0.8, 1.1); // the front face, right of the cut, in the top layer
    const start = toScreen(grab, front);
    const sessions = [60, -60].map((dy) => drag(adapter, front, start, [{ x: start.x, y: start.y + dy }, { x: start.x, y: start.y + 3 * dy }])!);
    const best = sessions.reduce((a, b) => (Math.abs(a.activeTurn()!.angleDeg) > Math.abs(b.activeTurn()!.angleDeg) ? a : b));
    expect(best.activeTurn()).toMatchObject({ generator: SQ1_GEN_SLICE });
    expect(best.release().move).toEqual({ generator: SQ1_GEN_SLICE, amount: 1 });
  });

  it("a swing of less than half a unit settles back with no move", async () => {
    const { adapter } = await setup();
    const grab = new THREE.Vector3(0.7, TOP_Y1, 0.4);
    const session = drag(adapter, camera, toScreen(grab, camera), [toScreen(rotateY(grab, -9), camera), toScreen(rotateY(grab, -12), camera)])!;
    const released = session.release();
    expect(released.move).toBeNull();
    expect(released.settle).toEqual({ generator: SQ1_GEN_TOP });
  });

  it("a drag that misses the puzzle, or lands on the frame half of the middle layer, is not a turn (it orbits)", async () => {
    const { adapter } = await setup();
    expect(adapter.start(rayThrough({ x: 5, y: 5 }, camera), camera, W, H, { x: 5, y: 5 })).toBeNull();
    const frame = cameraAt(0.2);
    const point = toScreen(new THREE.Vector3(-0.9, 0, 1.1), frame);
    expect(adapter.start(rayThrough(point, frame), frame, W, H, point)).toBeNull();
  });

  it("a legal slice follows the finger, commits past 90 degrees and settles back before", async () => {
    const { adapter, puzzle } = await setup();
    expect(puzzle.isMoveLegal(SQ1_GEN_SLICE, 1)).toBe(true);
    const front = cameraAt(0.2);
    const grab = new THREE.Vector3(0.8, 0, 1.1); // right half of the middle layer, front
    const start = toScreen(grab, front);
    const farther = (px: number) => ({ x: start.x, y: start.y + px });
    // Dragging either way along the path poses the slice; find the sign
    // that grows by checking both.
    const down = drag(adapter, front, start, [farther(40), farther(150)])!;
    const up = drag(adapter, front, start, [farther(-40), farther(-150)])!;
    const best = Math.abs(down.activeTurn()?.angleDeg ?? 0) > Math.abs(up.activeTurn()?.angleDeg ?? 0) ? down : up;
    expect(best.locked).toBe(true);
    expect(best.activeTurn()).toMatchObject({ generator: SQ1_GEN_SLICE });
    expect(Math.abs(best.activeTurn()!.angleDeg)).toBeGreaterThan(90);
    const released = best.release();
    expect(released.move).toEqual({ generator: SQ1_GEN_SLICE, amount: 1 });
    expect(Math.abs(released.toDeg)).toBe(180);

    const short = drag(adapter, front, start, [farther(20), farther(60)])!;
    const shortUp = drag(adapter, front, start, [farther(-20), farther(-60)])!;
    for (const session of [short, shortUp]) {
      const r = session.release();
      expect(r.move).toBeNull();
      expect(r.settle).toEqual({ generator: SQ1_GEN_SLICE });
    }
  });

  it("a blocked slice only wobbles, never commits, however far it is dragged", async () => {
    // Top turned by 2 puts a corner across the cut.
    const { adapter, puzzle } = await setup((p) => p.applyMove(SQ1_GEN_TOP, 2));
    expect(puzzle.isMoveLegal(SQ1_GEN_SLICE, 1)).toBe(false);
    const front = cameraAt(0.2);
    const start = toScreen(new THREE.Vector3(0.8, 0, 1.1), front);
    for (const sign of [1, -1]) {
      const session = drag(adapter, front, start, [
        { x: start.x, y: start.y + sign * 40 },
        { x: start.x, y: start.y + sign * 400 },
      ])!;
      expect(session.locked).toBe(true);
      expect(Math.abs(session.activeTurn()!.angleDeg)).toBeLessThanOrEqual(BLOCKED_SLICE_WOBBLE_DEG);
      const released = session.release();
      expect(released.move).toBeNull();
      expect(released.settle).toEqual({ generator: SQ1_GEN_SLICE });
    }
  });

  it("a vertical drag on a left-half top piece, or a sideways one on the middle layer, hands over to the camera", async () => {
    const { adapter } = await setup();
    const front = cameraAt(0.2);
    const leftTop = new THREE.Vector3(-0.9, 0.7, 1.1);
    const a = toScreen(leftTop, front);
    const up = drag(adapter, front, a, [{ x: a.x, y: a.y - 60 }])!;
    expect(up.wantsOrbit).toBe(true);
    expect(up.locked).toBe(false);
    const rightMiddle = toScreen(new THREE.Vector3(0.8, 0, 1.1), front);
    const side = drag(adapter, front, rightMiddle, [{ x: rightMiddle.x + 80, y: rightMiddle.y + 2 }])!;
    expect(side.wantsOrbit).toBe(true);
  });

  it("below the dead zone nothing is decided yet", async () => {
    const { adapter } = await setup();
    const grab = new THREE.Vector3(0.7, TOP_Y1, 0.4);
    const start = toScreen(grab, camera);
    const session = drag(adapter, camera, start, [{ x: start.x + 3, y: start.y + 2 }])!;
    expect(session.locked).toBe(false);
    expect(session.activeTurn()).toBeNull();
    expect(session.release().move).toBeNull();
  });
});
