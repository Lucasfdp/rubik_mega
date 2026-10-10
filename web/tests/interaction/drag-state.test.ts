import * as THREE from "three";
import { describe, expect, it } from "vitest";
import { dragActiveTurn, pickDrag, pickRelease, pickStart } from "../../src/interaction/drag-state.js";
import { Axis, createCubicGeometry } from "../../src/render/lattice/cubic.js";
import { MoveMap } from "../../src/interaction/move-axis.js";

/// A 3x3x3's geometry and generator table -- the size every case below
/// was originally written for (rubik's own drag tests). Layers and
/// cubie positions are lattice indices, 0..2.
const GEOMETRY_3 = createCubicGeometry(3);
const MOVES_3 = new MoveMap(3, [0, 1, 2, 3, 4, 5].map((face) => ({ face, depth: 0 })));

const WIDTH = 800;
const HEIGHT = 800;

function cameraAt(x: number, y: number, z: number): THREE.PerspectiveCamera {
  const camera = new THREE.PerspectiveCamera(50, WIDTH / HEIGHT, 0.1, 100);
  camera.position.set(x, y, z);
  camera.lookAt(0, 0, 0);
  camera.updateMatrixWorld();
  return camera;
}

function centerScreenRay(camera: THREE.PerspectiveCamera): THREE.Ray {
  const raycaster = new THREE.Raycaster();
  raycaster.setFromCamera(new THREE.Vector2(0, 0), camera);
  return raycaster.ray;
}

const CENTER: { x: number; y: number } = { x: WIDTH / 2, y: HEIGHT / 2 };

describe("pickStart", () => {
  it("a symmetric corner view (camera at (10,10,10)) picks the URF corner on the Right face", () => {
    // By construction the ray from (10,10,10) through the origin hits
    // the box exactly at its (h, h, h) corner -- x/y/z arrive at the
    // half-extent simultaneously -- so this is a fully predictable,
    // hand-checkable pick: vectorToAxis's X/Y/Z tie-break picks X
    // first, giving normal (1,0,0) and cubiePos [2,2,2] deterministically.
    const camera = cameraAt(10, 10, 10);
    const drag = pickStart(centerScreenRay(camera), camera, WIDTH, HEIGHT, CENTER, GEOMETRY_3, MOVES_3);
    expect(drag).not.toBeNull();
    expect(drag!.normal).toEqual({ x: 1, y: 0, z: 0 });
    expect(drag!.cubiePos).toEqual([2, 2, 2]);
    expect(drag!.planeDir).toEqual([
      { x: 0, y: 1, z: 0 },
      { x: 0, y: 0, z: 1 },
    ]);
  });

  it("a head-on view (camera at (0,0,10)) picks the Front face's own centre", () => {
    const camera = cameraAt(0, 0, 10);
    const drag = pickStart(centerScreenRay(camera), camera, WIDTH, HEIGHT, CENTER, GEOMETRY_3, MOVES_3);
    expect(drag).not.toBeNull();
    expect(drag!.normal).toEqual({ x: 0, y: 0, z: 1 });
    expect(drag!.cubiePos).toEqual([1, 1, 2]); // the Front face's centre cubie
  });

  it("returns null when the ray misses the puzzle entirely", () => {
    const camera = cameraAt(10, 10, 10);
    const raycaster = new THREE.Raycaster();
    raycaster.setFromCamera(new THREE.Vector2(0.9, 0.9), camera); // off into empty space
    const drag = pickStart(raycaster.ray, camera, WIDTH, HEIGHT, CENTER, GEOMETRY_3, MOVES_3);
    expect(drag).toBeNull();
  });
});

describe("corner-pick drag -> lockAxis -> release (the cross-product sign, rubik's own B1 fix)", () => {
  it("dragging along planeDir[0] locks axis=Z, layer=2, and resolves to generator 2 (F)", () => {
    const camera = cameraAt(10, 10, 10);
    const drag = pickStart(centerScreenRay(camera), camera, WIDTH, HEIGHT, CENTER, GEOMETRY_3, MOVES_3)!;
    const dir = drag.screenDir[0];

    // Past the dead zone: locks axis/layer/sign/chosen for the rest of
    // the drag (rubik's own B2 -- never re-decided after this frame).
    pickDrag(drag, { x: CENTER.x + dir.x * 0.3, y: CENTER.y + dir.y * 0.3 }, 1 / 60);
    expect(drag.locked).toBe(true);
    expect(drag.axis).toBe(Axis.Z);
    expect(drag.layer).toBe(2);
    expect(drag.sign).toBe(1);
    expect(drag.chosen).toBe(0);

    // Drag far enough to approach a quarter turn.
    pickDrag(drag, { x: CENTER.x + dir.x * 1.0, y: CENTER.y + dir.y * 1.0 }, 1 / 60);
    expect(drag.accumDeg).toBeGreaterThan(30);
    expect(dragActiveTurn(drag)).toEqual({ axis: Axis.Z, layer: 2, angleDeg: drag.accumDeg });

    const release = pickRelease(drag);
    expect(release.toDeg).toBe(90);
    expect(release.move).toEqual({ generator: 2, amount: 3 }); // F', axis Z layer 2, +90
    expect(drag.active).toBe(false);
  });

  it("a drag that never leaves the dead zone releases with no move", () => {
    const camera = cameraAt(10, 10, 10);
    const drag = pickStart(centerScreenRay(camera), camera, WIDTH, HEIGHT, CENTER, GEOMETRY_3, MOVES_3)!;
    pickDrag(drag, { x: CENTER.x + 1, y: CENTER.y }, 1 / 60); // 1px, well under DRAG_DEADZONE_PX
    expect(drag.locked).toBe(false);
    const release = pickRelease(drag);
    expect(release.move).toBeNull();
    expect(release.toDeg).toBe(0);
  });
});

describe("centre-pick circular drag -> release", () => {
  it("a quarter-circle sweep accumulates ~90 degrees and resolves to a real move", () => {
    const camera = cameraAt(0, 0, 10);
    const drag = pickStart(centerScreenRay(camera), camera, WIDTH, HEIGHT, CENTER, GEOMETRY_3, MOVES_3)!;

    // First move past the dead zone locks the centre-turn mode.
    pickDrag(drag, { x: CENTER.x + 30, y: CENTER.y }, 1 / 60);
    expect(drag.centerTurn).toBe(true);
    expect(drag.axis).toBe(Axis.Z);
    expect(drag.layer).toBe(2);

    // Sweep a quarter circle around the centre.
    for (let i = 1; i <= 10; i++) {
      const angle = (i / 10) * (Math.PI / 2);
      pickDrag(drag, { x: CENTER.x + 30 * Math.cos(angle), y: CENTER.y + 30 * Math.sin(angle) }, 1 / 60);
    }
    expect(Math.abs(drag.accumDeg)).toBeCloseTo(90, 0);

    const release = pickRelease(drag);
    expect(Math.abs(release.toDeg)).toBe(90);
    expect(release.move).not.toBeNull();
    expect(release.move!.generator).toBe(2); // still the Front face
  });
});

describe("a middle-slice pick is blocked, not turnable", () => {
  it("the Right-Front edge, dragged vertically (toward the E-slice axis), locks the middle layer and rubber-bands", () => {
    // Camera at (10, 0, 10): the ray through screen centre hits the box
    // at exactly (h, 0, h) -- the Right-Front edge cubie, cubiePos
    // [2, 1, 2]. Dragging along its Z-ish plane direction (chosen=1)
    // cross-products to axis=Y -- and this edge's own Y coordinate is
    // 1 (it's not in the U or D layer), so lockAxis resolves layer 1:
    // no single face-turn move spins just this edge that way (it's the
    // equatorial E-slice, which core/ doesn't expose as a generator).
    const camera = cameraAt(10, 0, 10);
    const drag = pickStart(centerScreenRay(camera), camera, WIDTH, HEIGHT, CENTER, GEOMETRY_3, MOVES_3)!;
    expect(drag.cubiePos).toEqual([2, 1, 2]);

    const dir = drag.screenDir[1];
    pickDrag(drag, { x: CENTER.x + dir.x * 0.3, y: CENTER.y + dir.y * 0.3 }, 1 / 60);
    expect(drag.locked).toBe(true);
    expect(drag.axis).toBe(Axis.Y);
    expect(drag.layer).toBe(1);
    expect(drag.blocked).toBe(true);

    // Even a big drag stays rubber-banded within DRAG_BLOCK_DEG (12deg).
    pickDrag(drag, { x: CENTER.x + dir.x * 2, y: CENTER.y + dir.y * 2 }, 1 / 60);
    expect(Math.abs(drag.accumDeg)).toBeLessThan(12);

    const release = pickRelease(drag);
    expect(release.move).toBeNull();
    expect(release.toDeg).toBe(0);
  });
});

describe("bigger cubes: inner layers turn, nothing is blocked", () => {
  const GEOMETRY_5 = createCubicGeometry(5);
  const MOVES_5 = new MoveMap(
    5,
    [0, 1, 2, 3, 4, 5].flatMap((face) => [0, 1, 2].map((depth) => ({ face, depth }))),
  );

  it("5x5x5: a head-on pick on the Front centre drags straight (no centre-twist), turning the middle slice", () => {
    const camera = cameraAt(0, 0, 10);
    const drag = pickStart(centerScreenRay(camera), camera, WIDTH, HEIGHT, CENTER, GEOMETRY_5, MOVES_5)!;
    expect(drag.cubiePos).toEqual([2, 2, 4]);
    const dir = drag.screenDir[0];
    pickDrag(drag, { x: CENTER.x + dir.x * 1.0, y: CENTER.y + dir.y * 1.0 }, 1 / 60);
    expect(drag.centerTurn).toBe(false);
    expect(drag.blocked).toBe(false);
    expect(drag.layer).toBe(2);
    const release = pickRelease(drag);
    expect(release.move).not.toBeNull();
    // D3: the odd-n middle layer resolves to the U/R/F-side generator.
    expect(MOVES_5.axisForGenerator(release.move!.generator)).toEqual({ axis: drag.axis, layer: 2 });
  });

  it("4x4x4: the Right-Front edge's inner wing, dragged vertically, turns inner layer 2 (not blocked)", () => {
    const geometry = createCubicGeometry(4);
    const moves = new MoveMap(4, [0, 1, 2, 3, 4, 5].flatMap((face) => [0, 1].map((depth) => ({ face, depth }))));
    const camera = cameraAt(10, 0.2, 10); // nudged up so the hit lands on the y = 2 wing, not the seam
    const drag = pickStart(centerScreenRay(camera), camera, WIDTH, HEIGHT, CENTER, geometry, moves)!;
    expect(drag.cubiePos).toEqual([3, 2, 3]);
    const dir = drag.screenDir[1];
    pickDrag(drag, { x: CENTER.x + dir.x * 1.0, y: CENTER.y + dir.y * 1.0 }, 1 / 60);
    expect(drag.axis).toBe(Axis.Y);
    expect(drag.layer).toBe(2);
    expect(drag.blocked).toBe(false);
    expect(pickRelease(drag).move).toEqual(expect.objectContaining({ generator: expect.any(Number) }));
  });
});
