import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { instantiatePuzzleModule } from "../../../src/engine/wasm-module.js";
import { Puzzle } from "../../../src/engine/puzzle.js";
import { PuzzleType } from "../../../src/engine/types.js";
import { basePlanesOf, faceKite } from "../../../src/render/cut/cut-scene.js";
import { polytopeFromPlanes, type Vec } from "../../../src/render/cut/polytope.js";
import { armsOf, onTurningSide, recessWalls } from "../../../src/render/cut/shell-cut.js";

const WASM_PATH = fileURLToPath(new URL("../../../../core/build/rubik_mega.wasm", import.meta.url));

/// Kite points are sampled this far in from the outline towards the
/// kite's centre, so a sample is never on the band itself, where either
/// answer is right. A still kite touches the band at a single vertex
/// (the lateral midpoint), where the approach is steep in neither height
/// nor radius, so the inset has to be generous: 10 per cent of a kite
/// keeps every sample a few hundredths of a unit clear. Every sticker is
/// drawn shrunk to 86 per cent anyway, so nothing drawn is closer.
const INSET = 0.9;
/// The answer must hold even with the band moved this far either way --
/// well above the layout's 1/4096 rounding, well below INSET's margin.
const MARGIN = 1e-3;

/// Points spread over a kite: a barycentric grid on each fan triangle.
function samples(points: readonly Vec[]): Vec[] {
  const middle = points.reduce<Vec>((m, p) => [m[0] + p[0] / points.length, m[1] + p[1] / points.length, m[2] + p[2] / points.length], [0, 0, 0]);
  const inset = points.map((p): Vec => [middle[0] + (p[0] - middle[0]) * INSET, middle[1] + (p[1] - middle[1]) * INSET, middle[2] + (p[2] - middle[2]) * INSET]);
  const out: Vec[] = [];
  for (let t = 1; t + 1 < inset.length; t++) {
    const [a, b, c] = [inset[0]!, inset[t]!, inset[t + 1]!];
    for (let i = 0; i <= 8; i++) {
      for (let j = 0; i + j <= 8; j++) {
        const [u, v, w] = [i / 8, j / 8, 1 - i / 8 - j / 8];
        out.push([0, 1, 2].map((k) => u * a[k]! + v * b[k]! + w * c[k]!) as Vec);
      }
    }
  }
  return out;
}

async function kilominx() {
  const puzzle = Puzzle.create(await instantiatePuzzleModule(readFileSync(WASM_PATH)), PuzzleType.Dodecahedral, 2);
  const cut = puzzle.layout.cut!;
  const base = basePlanesOf(cut);
  const solid = polytopeFromPlanes(base, cut.tolerance)!;
  const kites = cut.facelets.map((f) => ({ points: faceKite(solid, f.normal, f.pos, cut.tolerance)!, normal: f.normal as Vec }));
  const axes = cut.generators.map((g) => {
    const length = Math.hypot(...g.axis);
    return g.axis.map((x) => x / length) as Vec;
  });
  return { puzzle, cut, base, kites, axes };
}

describe("Kilominx cut band", () => {
  it("finds two arms on each of the five faces beside the turning one", async () => {
    const { puzzle, cut, kites, axes } = await kilominx();
    cut.generators.forEach((g, index) => {
      expect(armsOf(axes[index]!, kites.filter((_, i) => g.moving.includes(i)), cut.tolerance)).toHaveLength(10);
    });
    puzzle.destroy();
  });

  // The claim the whole design rests on: the band separates every
  // turning kite from every still one, for every face. A layer turning
  // about its axis maps the band onto itself, so this one check at rest
  // means no turning kite ever meets a still one, at any angle.
  it("puts every turning kite on the turning side and every still kite off it", async () => {
    const { puzzle, cut, kites, axes } = await kilominx();
    let checked = 0;
    cut.generators.forEach((g, index) => {
      const axis = axes[index]!;
      const arms = armsOf(axis, kites.filter((_, i) => g.moving.includes(i)), cut.tolerance);
      kites.forEach(({ points }, i) => {
        const turning = g.moving.includes(i);
        for (const p of samples(points)) {
          if (turning) expect(onTurningSide(axis, arms, p, -MARGIN)).toBe(true);
          else expect(onTurningSide(axis, arms, p, MARGIN)).toBe(false);
          checked++;
        }
      });
    });
    expect(checked).toBeGreaterThan(12 * 60);
    puzzle.destroy();
  });

  it("builds walls that lie on the band and inside the solid", async () => {
    const { puzzle, cut, base, kites, axes } = await kilominx();
    const depth = (p: Vec): number => Math.max(...base.map((pl) => (pl.n[0] * p[0] + pl.n[1] * p[1] + pl.n[2] * p[2] - pl.d) / Math.hypot(...pl.n)));
    cut.generators.forEach((g, index) => {
      const axis = axes[index]!;
      const arms = armsOf(axis, kites.filter((_, i) => g.moving.includes(i)), cut.tolerance);
      const walls = recessWalls(axis, arms, cut.tolerance);
      expect(walls.length).toBeGreaterThan(0);
      for (const triangle of walls) {
        for (const p of triangle) {
          expect(depth(p)).toBeLessThan(MARGIN);
          // On the band: on the turning side only just, and off it only just.
          expect(onTurningSide(axis, arms, p, MARGIN)).toBe(true);
          expect(onTurningSide(axis, arms, p, -MARGIN)).toBe(false);
        }
      }
    });
    puzzle.destroy();
  });
});
