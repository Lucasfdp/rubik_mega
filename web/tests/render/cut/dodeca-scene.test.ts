import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import * as THREE from "three";
import { describe, expect, it } from "vitest";
import { instantiatePuzzleModule } from "../../../src/engine/wasm-module.js";
import { Puzzle } from "../../../src/engine/puzzle.js";
import { PuzzleType } from "../../../src/engine/types.js";
import { basePlanesOf, buildCutScene, faceKite, fillerCellsOf, hollowCellOf, pieceCell } from "../../../src/render/cut/cut-scene.js";
import { polytopeFromPlanes, polytopeVolume } from "../../../src/render/cut/polytope.js";
import { arcAngleDeg, candidateGenerators, centreGenerator, enterConvex, faceletAt, planePoint } from "../../../src/interaction/cut-drag.js";
import { CutMoveModel, cutStepDeg } from "../../../src/interaction/cut-moves.js";
import { formatMoveFor } from "../../../src/ui/move-notation.js";


const WASM_PATH = fileURLToPath(new URL("../../../../core/build/rubik_mega.wasm", import.meta.url));
const load = async () => instantiatePuzzleModule(readFileSync(WASM_PATH));

/// Stickers a face turn carries, centre included: core's
/// DODECA_*_MOVED_STICKERS count the pieces it moves (25 on a Megaminx),
/// but the centre sticker turns with the layer on screen, so the
/// generator's moving list holds 26.
const CASES = [
  { name: "Megaminx", size: 3, facelets: 132, pieces: 62, moved: 26, fillers: 0 },
  { name: "Kilominx", size: 2, facelets: 60, pieces: 20, moved: 15, fillers: 42 },
];

/// How far a drawn volume may differ from the solid's, relative: the
/// coordinates are rounded to 1/4096, which moves a plane by up to that.
const VOLUME_RELATIVE_TOLERANCE = 5e-3;

describe.each(CASES)("dodecahedral geometry: $name", ({ size, facelets, pieces, moved, fillers }) => {
  it("exposes a 72-degree, 12-generator layout in real units", async () => {
    const puzzle = Puzzle.create(await load(), PuzzleType.Dodecahedral, size);
    const { layout } = puzzle;
    expect(layout.steps).toBe(4);
    expect(layout.faceletCount).toBe(facelets);
    const cut = layout.cut!;
    expect(cut.generators).toHaveLength(12);
    expect(cut.tolerance).toBeGreaterThan(0);
    for (const g of cut.generators) {
      expect(Math.hypot(...g.axis)).toBeCloseTo(1, 3);
      expect(g.moving).toHaveLength(moved);
    }
    for (const f of cut.facelets) expect(Math.hypot(...f.normal)).toBeCloseTo(1, 3);
    puzzle.destroy();
  });

  it("piece, filler and core cells tile the solid", async () => {
    const puzzle = Puzzle.create(await load(), PuzzleType.Dodecahedral, size);
    const cut = puzzle.layout.cut!;
    const base = basePlanesOf(cut);
    expect(base).toHaveLength(12);
    const solid = polytopeVolume(polytopeFromPlanes(base, cut.tolerance)!);
    const keys = new Map<string, number>();
    cut.facelets.forEach((f, i) => {
      const key = `${f.pieceType}:${f.slot}`;
      if (!keys.has(key)) keys.set(key, i);
    });
    expect(keys.size).toBe(pieces);
    let total = 0;
    for (const rep of keys.values()) total += polytopeVolume(pieceCell(cut, base, rep));
    const sides = [...keys.values()].map((rep) => cut.generators.map((g) => g.moving.includes(rep)));
    const extra = fillerCellsOf(cut, base, sides);
    expect(extra).toHaveLength(fillers);
    for (const { cell } of extra) total += polytopeVolume(cell);
    const hollow = hollowCellOf(cut, base);
    expect(hollow).not.toBeNull();
    total += polytopeVolume(hollow!);
    expect(Math.abs(total - solid) / solid).toBeLessThan(VOLUME_RELATIVE_TOLERANCE);
    puzzle.destroy();
  });

  it("builds a scene and moves exactly the turning layer", async () => {
    const puzzle = Puzzle.create(await load(), PuzzleType.Dodecahedral, size);
    const scene = buildCutScene(puzzle);
    const resting = scene.group.children.map((c) => (c as THREE.Mesh).geometry.uuid);
    scene.setActiveTurn({ generator: 0, angleDeg: 36 });
    // A Megaminx piece turns by rotating; a Kilominx kite is redrawn for its pose.
    const turning = scene.group.children.filter((c, i) => c.quaternion.w < 1 - 1e-9 || (c as THREE.Mesh).geometry.uuid !== resting[i]).length;
    expect(turning).toBeGreaterThanOrEqual(moved);
    scene.setActiveTurn(null);
    expect(scene.group.children.every((c) => Math.abs(c.quaternion.w - 1) < 1e-9)).toBe(true);
    scene.dispose();
    puzzle.destroy();
  });

  it("picks the facelet under a ray aimed at it, offering only the smallest cuts", async () => {
    const puzzle = Puzzle.create(await load(), PuzzleType.Dodecahedral, size);
    const cut = puzzle.layout.cut!;
    const base = basePlanesOf(cut);
    cut.facelets.forEach((f, i) => {
      const n = f.normal;
      const origin: [number, number, number] = [f.pos[0] + n[0] * 20, f.pos[1] + n[1] * 20, f.pos[2] + n[2] * 20];
      const hit = enterConvex(base, origin, [-n[0], -n[1], -n[2]]);
      expect(hit).not.toBeNull();
      expect(faceletAt(cut, hit!.point, hit!.plane.n)).toBe(i);
      const sizes = new Set(candidateGenerators(cut, i).map((g) => cut.generators[g]!.moving.length));
      expect(sizes.size).toBe(1);
    });
    puzzle.destroy();
  });

  it("names every face turn: face letters with '', 2, 2', ' suffixes", async () => {
    const puzzle = Puzzle.create(await load(), PuzzleType.Dodecahedral, size);
    expect(formatMoveFor({ generator: 0, amount: 1 }, puzzle.layout)).toBe("U");
    expect(formatMoveFor({ generator: 3, amount: 2 }, puzzle.layout)).toBe("BR2");
    expect(formatMoveFor({ generator: 7, amount: 3 }, puzzle.layout)).toBe("DBR2'");
    expect(formatMoveFor({ generator: 11, amount: 4 }, puzzle.layout)).toBe("D'");
    expect(() => formatMoveFor({ generator: 0, amount: 5 }, puzzle.layout)).toThrow();
    puzzle.destroy();
  });
});

describe("Kilominx kites", () => {
  it("five kites tile every face's pentagon", async () => {
    const puzzle = Puzzle.create(await load(), PuzzleType.Dodecahedral, 2);
    const cut = puzzle.layout.cut!;
    const solid = polytopeFromPlanes(basePlanesOf(cut), cut.tolerance)!;
    const area = (points: [number, number, number][]): number => {
      let sum: [number, number, number] = [0, 0, 0];
      for (let i = 1; i + 1 < points.length; i++) {
        const a = points[0]!, b = points[i]!, c = points[i + 1]!;
        const u = [b[0] - a[0], b[1] - a[1], b[2] - a[2]], v = [c[0] - a[0], c[1] - a[1], c[2] - a[2]];
        sum = [sum[0] + u[1]! * v[2]! - u[2]! * v[1]!, sum[1] + u[2]! * v[0]! - u[0]! * v[2]!, sum[2] + u[0]! * v[1]! - u[1]! * v[0]!];
      }
      return Math.hypot(...sum) / 2;
    };
    const perFace = new Map<string, number>();
    for (const f of cut.facelets) {
      const kite = faceKite(solid, f.normal, f.pos, cut.tolerance);
      expect(kite).not.toBeNull();
      const key = f.normal.map((x) => x.toFixed(2)).join(",");
      perFace.set(key, (perFace.get(key) ?? 0) + area(kite!));
    }
    expect(perFace.size).toBe(12);
    const pentagon = area(solid.faces[0]!.loop.map((i) => solid.vertices[i]!));
    for (const total of perFace.values()) expect(Math.abs(total - pentagon) / pentagon).toBeLessThan(VOLUME_RELATIVE_TOLERANCE);
    puzzle.destroy();
  });

  it("turns whole: a turn rotates its 15 kites and the cap's body, nothing else", async () => {
    const puzzle = Puzzle.create(await load(), PuzzleType.Dodecahedral, 2);
    const scene = buildCutScene(puzzle);
    const visible = (): THREE.Object3D[] => scene.group.children.filter((c) => c.visible);
    const resting = visible().length;
    for (let g = 0; g < 12; g++) {
      scene.setActiveTurn({ generator: g, angleDeg: 36 });
      // Whole meshes rotate; no geometry is rebuilt per pose.
      expect(visible().filter((c) => c.quaternion.w < 1 - 1e-9)).toHaveLength(15 + 1);
      // The resting body gives way to that turn's still part and cap.
      expect(visible()).toHaveLength(resting + 1);
    }
    scene.setActiveTurn(null);
    expect(visible()).toHaveLength(resting);
    expect(scene.group.children.every((c) => Math.abs(c.quaternion.w - 1) < 1e-9)).toBe(true);
    scene.dispose();
    puzzle.destroy();
  });
});

describe("Kilominx turning", () => {
  it("never grows the shape, and is back at rest afterwards", async () => {
    const puzzle = Puzzle.create(await load(), PuzzleType.Dodecahedral, 2);
    const scene = buildCutScene(puzzle);
    const farthest = (): number => {
      let far = 0;
      scene.group.updateMatrixWorld(true);
      for (const child of scene.group.children as THREE.Mesh[]) {
        if (!child.visible) continue;
        const at = child.geometry.getAttribute("position")!;
        const p = new THREE.Vector3();
        for (let i = 0; i < at.count; i++) far = Math.max(far, p.fromBufferAttribute(at, i).applyMatrix4(child.matrixWorld).length());
      }
      return far;
    };
    const rest = farthest();
    for (let angle = -144; angle <= 144; angle += 6) {
      scene.setActiveTurn({ generator: 0, angleDeg: angle });
      expect(farthest()).toBeLessThan(rest * 1.0001);
    }
    scene.setActiveTurn(null);
    expect(farthest()).toBeCloseTo(rest, 9);
    scene.dispose();
    puzzle.destroy();
  });
});

describe("arc geometry for dragging a face round", () => {
  const axis: [number, number, number] = [0, 1, 0];
  it("measures the right-handed angle about the axis", () => {
    expect(arcAngleDeg(axis, [0, 0.5, 0], [0.5, 0.5, 0], [0, 0.5, -0.5])).toBeCloseTo(90, 6);
    expect(arcAngleDeg(axis, [0, 0.5, 0], [0.5, 0.5, 0], [0, 0.5, 0.5])).toBeCloseTo(-90, 6);
  });
  it("refuses an angle too close to the axis to trust", () => {
    expect(arcAngleDeg(axis, [0, 0, 0], [0.01, 0, 0], [0, 0, 0.5])).toBeNull();
  });
  it("finds where a ray meets the plane through a point, or null when parallel", () => {
    expect(planePoint(axis, [3, 0.7, 3], [1, 5, 2], [0, -1, 0])![1]).toBeCloseTo(0.7, 9);
    expect(planePoint(axis, [0, 0.7, 0], [0, 5, 0], [1, 0, 0])).toBeNull();
  });
});

describe("fillers: Skewb and Pyraminx have none", () => {
  it.each([PuzzleType.Skewb, PuzzleType.Tetrahedral])("type %i", async (type) => {
    const puzzle = Puzzle.create(await load(), type, 3);
    const cut = puzzle.layout.cut!;
    const reps = new Map<string, number>();
    cut.facelets.forEach((f, i) => reps.set(`${f.pieceType}:${f.slot}`, reps.get(`${f.pieceType}:${f.slot}`) ?? i));
    const sides = [...reps.values()].map((rep) => cut.generators.map((g) => g.moving.includes(rep)));
    expect(fillerCellsOf(cut, basePlanesOf(cut), sides)).toHaveLength(0);
    puzzle.destroy();
  });
});

describe("CutMoveModel with four amounts", () => {
  const model = new CutMoveModel(12, 4);
  it("steps 72 degrees and draws more than half a turn the short way", () => {
    expect(cutStepDeg(4)).toBe(72);
    expect([1, 2, 3, 4].map((a) => model.quarterDegForAmount(0, a))).toEqual([-72, -144, 144, 72]);
  });
  it("inverts amounts and converts drag steps back to moves", () => {
    expect([1, 2, 3, 4].map((a) => model.inverseAmount(a))).toEqual([4, 3, 2, 1]);
    expect(model.moveForSteps(0, -1)).toEqual({ generator: 0, amount: 1 });
    expect(model.moveForSteps(0, 1)).toEqual({ generator: 0, amount: 4 });
    expect(model.moveForSteps(0, -2)).toEqual({ generator: 0, amount: 2 });
    expect(model.moveForSteps(0, 2)).toEqual({ generator: 0, amount: 3 });
    expect(model.moveForSteps(0, 0)).toBeNull();
    expect(model.moveForSteps(0, 5)).toBeNull();
  });
  it("rejects amount 5", () => {
    expect(() => model.quarterDegForAmount(0, 5)).toThrow();
  });
});
