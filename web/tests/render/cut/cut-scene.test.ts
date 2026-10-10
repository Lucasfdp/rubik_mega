import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { instantiatePuzzleModule } from "../../../src/engine/wasm-module.js";
import { Puzzle } from "../../../src/engine/puzzle.js";
import { PuzzleType, type CutLayout } from "../../../src/engine/types.js";
import { basePlanesOf, buildCutScene, hollowCellOf, pieceCell } from "../../../src/render/cut/cut-scene.js";
import { polytopeFromPlanes, polytopeVolume } from "../../../src/render/cut/polytope.js";
import type { Plane, Vec } from "../../../src/render/cut/polytope.js";
import {
  candidateGenerators,
  enterConvex,
  faceletAt,
} from "../../../src/interaction/cut-drag.js";
import { CutMoveModel, CUT_STEP_DEG } from "../../../src/interaction/cut-moves.js";
import { formatMoveFor } from "../../../src/ui/move-notation.js";

const WASM_PATH = fileURLToPath(new URL("../../../../core/build/rubik_mega.wasm", import.meta.url));
const load = async () => instantiatePuzzleModule(readFileSync(WASM_PATH));

const CASES = [
  { type: PuzzleType.Skewb, name: "Skewb", facelets: 30, generators: 8 },
  { type: PuzzleType.Tetrahedral, name: "Pyraminx", facelets: 36, generators: 8 },
];

describe.each(CASES)("cut-plane geometry: $name", ({ type, facelets, generators }) => {
  it("exposes the layout the web draws from", async () => {
    const puzzle = Puzzle.create(await load(), type, 3);
    expect(puzzle.layout.faceletCount).toBe(facelets);
    expect(puzzle.layout.cut?.generators).toHaveLength(generators);
    puzzle.destroy();
  });

  it("piece cells tile the base solid exactly", async () => {
    const puzzle = Puzzle.create(await load(), type, 3);
    const cut = puzzle.layout.cut!;
    const base = basePlanesOf(cut);
    const solid = polytopeVolume(polytopeFromPlanes(base)!);
    const pieces = new Set(cut.facelets.map((f) => `${f.pieceType}:${f.slot}`));
    let total = 0;
    for (const key of pieces) {
      const rep = cut.facelets.findIndex((f) => `${f.pieceType}:${f.slot}` === key);
      const planes: Plane[] = [...base];
      for (const g of cut.generators) {
        const n = g.axis.slice(0, 3) as [number, number, number];
        planes.push(g.moving.includes(rep) ? { n: [-n[0], -n[1], -n[2]], d: -g.threshold } : { n, d: g.threshold });
      }
      const cell = polytopeFromPlanes(planes);
      if (cell) total += polytopeVolume(cell);
    }
    // A sticker-less core (the Pyraminx's octahedron) is not a piece, but
    // it fills the rest of the solid -- and it is exactly what the scene
    // draws as its dark core (hollowCellOf), so the drawn core can never
    // be bigger or smaller than the real gap.
    const hollow = hollowCellOf(cut, base);
    if (hollow) total += polytopeVolume(hollow);
    expect(total).toBeCloseTo(solid, 6);
    puzzle.destroy();
  });

  it("builds a scene whose turn moves exactly the generator's stickers", async () => {
    const puzzle = Puzzle.create(await load(), type, 3);
    const scene = buildCutScene(puzzle);
    expect(scene.group.children.length).toBeGreaterThan(facelets);
    scene.setActiveTurn({ generator: 0, angleDeg: 60 });
    const moved = scene.group.children.filter((c) => c.quaternion.w < 0.999 - 1e-9).length;
    const g0 = puzzle.layout.cut!.generators[0]!;
    expect(moved).toBeGreaterThanOrEqual(g0.moving.length);
    scene.setActiveTurn(null);
    expect(scene.group.children.every((c) => Math.abs(c.quaternion.w - 1) < 1e-9)).toBe(true);
    scene.dispose();
    puzzle.destroy();
  });

  it("picks the facelet under a ray aimed at it, and only the smallest cuts", async () => {
    const puzzle = Puzzle.create(await load(), type, 3);
    const cut = puzzle.layout.cut!;
    const base = basePlanesOf(cut);
    cut.facelets.forEach((f, i) => {
      const n = f.normal;
      const origin: [number, number, number] = [f.pos[0] + n[0] * 20, f.pos[1] + n[1] * 20, f.pos[2] + n[2] * 20];
      const hit = enterConvex(base, origin, [-n[0], -n[1], -n[2]]);
      expect(hit).not.toBeNull();
      expect(faceletAt(cut, hit!.point, n)).toBe(i);
      const candidates = candidateGenerators(cut, i);
      expect(candidates.length).toBeGreaterThan(0);
      const sizes = new Set(candidates.map((g) => cut.generators[g]!.moving.length));
      expect(sizes.size).toBe(1);
    });
    puzzle.destroy();
  });
});

describe("CutMoveModel", () => {
  const model = new CutMoveModel(8);
  it("draws amount 1 clockwise and amount 2 the short way round", () => {
    expect(model.quarterDegForAmount(0, 1)).toBe(CUT_STEP_DEG);
    expect(model.quarterDegForAmount(0, 2)).toBe(-CUT_STEP_DEG);
    expect(model.inverseAmount(1)).toBe(2);
    expect(model.inverseAmount(2)).toBe(1);
  });
  it("rejects out-of-range input", () => {
    expect(() => model.targetForGenerator(8)).toThrow();
    expect(() => model.quarterDegForAmount(0, 3)).toThrow();
  });
});

describe("formatMoveFor", () => {
  it("names Pyraminx tips in lower case and primes amount 2", async () => {
    const puzzle = Puzzle.create(await load(), PuzzleType.Tetrahedral, 3);
    expect(formatMoveFor({ generator: 1, amount: 1 }, puzzle.layout)).toBe("U");
    expect(formatMoveFor({ generator: 0, amount: 2 }, puzzle.layout)).toBe("u'");
    puzzle.destroy();
  });
  it("names Skewb corners with three letters", async () => {
    const puzzle = Puzzle.create(await load(), PuzzleType.Skewb, 3);
    expect(formatMoveFor({ generator: 0, amount: 1 }, puzzle.layout)).toMatch(/^[UDLRFB]{3}$/);
    puzzle.destroy();
  });
});

/// Rotates a plane about `axis` through the origin by `deg` degrees.
function rotatePlane(plane: Plane, axis: Vec, deg: number): Plane {
  const k = Math.hypot(...axis);
  const u: Vec = [axis[0] / k, axis[1] / k, axis[2] / k];
  const t = (deg * Math.PI) / 180;
  const c = Math.cos(t);
  const sn = Math.sin(t);
  const v = plane.n;
  const d = u[0] * v[0] + u[1] * v[1] + u[2] * v[2];
  const cr: Vec = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]];
  return {
    n: [v[0] * c + cr[0] * sn + u[0] * d * (1 - c), v[1] * c + cr[1] * sn + u[1] * d * (1 - c), v[2] * c + cr[2] * sn + u[2] * d * (1 - c)],
    d: plane.d,
  };
}

const SWEEP_ANGLES_DEG = [15, 30, 45, 60, 75, 90, 105];
/// Overlap volumes below this are rounding, not clipping (the smallest
/// real piece is of the order of 1 in puzzle units).
const NO_OVERLAP = 1e-6;

describe.each(CASES)("turning never clips: $name", ({ type }) => {
  /// One cell per piece, plus the dark core if the puzzle has one.
  async function cellsOf(): Promise<{ cut: CutLayout; pieces: { rep: number; planes: Plane[] }[]; core: Plane[] | null }> {
    const puzzle = Puzzle.create(await load(), type, 3);
    const cut = puzzle.layout.cut!;
    const base = basePlanesOf(cut);
    const keys = new Set(cut.facelets.map((f) => `${f.pieceType}:${f.slot}`));
    const pieces = [...keys].map((key) => {
      const rep = cut.facelets.findIndex((f) => `${f.pieceType}:${f.slot}` === key);
      return { rep, planes: pieceCell(cut, base, rep).faces.map((f) => f.plane) };
    });
    const hollow = hollowCellOf(cut, base);
    puzzle.destroy();
    return { cut, pieces, core: hollow ? hollow.faces.map((f) => f.plane) : null };
  }

  // The regression for the Pyraminx's dark spikes: the old core was a
  // tetrahedron at 0.7 of the whole puzzle, which overlapped the turning
  // pieces. Every generator, at angles between the 120-degree rests.
  it("no turning piece ever overlaps a stationary piece or the drawn core", async () => {
    const { cut, pieces, core } = await cellsOf();
    cut.generators.forEach((g) => {
      const axis = g.axis.slice(0, 3) as Vec;
      const stationary = pieces.filter((p) => !g.moving.includes(p.rep)).map((p) => p.planes);
      if (core) stationary.push(core);
      for (const deg of SWEEP_ANGLES_DEG) {
        for (const moving of pieces.filter((p) => g.moving.includes(p.rep))) {
          const turned = moving.planes.map((plane) => rotatePlane(plane, axis, deg));
          for (const other of stationary) {
            const overlap = polytopeFromPlanes([...turned, ...other]);
            expect(overlap ? polytopeVolume(overlap) : 0).toBeLessThan(NO_OVERLAP);
          }
        }
      }
    });
  });

  it("the drawn core lies entirely on the stationary side of every cut", async () => {
    const puzzle = Puzzle.create(await load(), type, 3);
    const cut = puzzle.layout.cut!;
    const hollow = hollowCellOf(cut, basePlanesOf(cut));
    expect(hollow !== null).toBe(type === PuzzleType.Tetrahedral);
    for (const g of cut.generators) {
      for (const v of hollow?.vertices ?? []) {
        expect(g.axis[0] * v[0] + g.axis[1] * v[1] + g.axis[2] * v[2]).toBeLessThanOrEqual(g.threshold + 1e-6);
      }
    }
    puzzle.destroy();
  });
});

describe("the old oversize Pyraminx core would have clipped (the sweep above has teeth)", () => {
  it("a tetrahedron at 0.7 of the puzzle overlaps the turning axial piece", async () => {
    const puzzle = Puzzle.create(await load(), PuzzleType.Tetrahedral, 3);
    const cut = puzzle.layout.cut!;
    const base = basePlanesOf(cut);
    const g = cut.generators[1]!; // layer turn at vertex 0
    const oldCore: Plane[] = base.map((p) => ({ n: p.n, d: p.d * 0.7 }));
    const rep = cut.facelets.findIndex((f) => f.pieceType === 1 && f.slot === 0); // axial piece 0
    const turned = pieceCell(cut, base, rep).faces.map((f) => rotatePlane(f.plane, g.axis.slice(0, 3) as Vec, 60));
    const overlap = polytopeFromPlanes([...turned, ...oldCore]);
    expect(overlap ? polytopeVolume(overlap) : 0).toBeGreaterThan(1);
    puzzle.destroy();
  });
});
