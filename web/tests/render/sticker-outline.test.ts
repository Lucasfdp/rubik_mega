import { describe, expect, it } from "vitest";
import { stickerOutline, type OutlineOptions, type Point3 } from "../../src/render/sticker-outline.js";

const Z: Point3 = [0, 0, 1];
const EPS = 1e-9;

/// A regular polygon in the z = 0 plane, counter-clockwise seen from +z.
function regular(sides: number, radius: number): Point3[] {
  return Array.from({ length: sides }, (_, i): Point3 => {
    const a = (2 * Math.PI * i) / sides;
    return [radius * Math.cos(a), radius * Math.sin(a), 0];
  });
}

/// Signed area about +z (positive: counter-clockwise).
function area(points: readonly Point3[]): number {
  let sum = 0;
  points.forEach((p, i) => {
    const q = points[(i + 1) % points.length]!;
    sum += p[0] * q[1] - q[0] * p[1];
  });
  return sum / 2;
}

/// Every turn has the same sign: the polygon is convex.
function isConvex(points: readonly Point3[]): boolean {
  let sign = 0;
  for (let i = 0; i < points.length; i++) {
    const a = points[i]!;
    const b = points[(i + 1) % points.length]!;
    const c = points[(i + 2) % points.length]!;
    const turn = (b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0]);
    if (Math.abs(turn) < EPS) continue;
    if (sign === 0) sign = Math.sign(turn);
    else if (Math.sign(turn) !== sign) return false;
  }
  return true;
}

/// Whether `p` is inside (or on) the convex counter-clockwise polygon.
function inside(polygon: readonly Point3[], p: Point3): boolean {
  return polygon.every((a, i) => {
    const b = polygon[(i + 1) % polygon.length]!;
    return (b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0]) >= -1e-9;
  });
}

const SHAPES: Record<string, Point3[]> = {
  square: regular(4, Math.SQRT2),
  triangle: regular(3, 1),
  pentagon: regular(5, 1),
  // A Kilominx-style kite: not regular, two different edge lengths.
  kite: [[0, -1, 0], [0.6, 0, 0], [0, 1.4, 0], [-0.6, 0, 0]],
};

describe("stickerOutline", () => {
  const rounded: OutlineOptions = { insetRatio: 0.14, cornerRatio: 0.35, cornerSegments: 3 };

  for (const [name, shape] of Object.entries(SHAPES)) {
    describe(name, () => {
      it("shrinks the area, and rounding shrinks it a little more", () => {
        const sharp = stickerOutline(shape, Z, { ...rounded, cornerRatio: 0 });
        const smooth = stickerOutline(shape, Z, rounded);
        expect(area(sharp)).toBeLessThan(area(shape));
        expect(area(smooth)).toBeLessThan(area(sharp));
        expect(area(smooth)).toBeGreaterThan(0);
      });

      it("stays convex", () => {
        expect(isConvex(stickerOutline(shape, Z, rounded))).toBe(true);
      });

      it("stays entirely inside the original polygon", () => {
        for (const p of stickerOutline(shape, Z, rounded)) expect(inside(shape, p)).toBe(true);
      });

      it("has sides * (segments + 1) vertices with rounding, and one per side without", () => {
        expect(stickerOutline(shape, Z, rounded)).toHaveLength(shape.length * 4);
        expect(stickerOutline(shape, Z, { ...rounded, cornerRatio: 0 })).toHaveLength(shape.length);
        expect(stickerOutline(shape, Z, { ...rounded, cornerSegments: 1 })).toHaveLength(shape.length * 2);
      });

      it("keeps the input winding", () => {
        expect(area(stickerOutline(shape, Z, rounded))).toBeGreaterThan(0);
        const reversed = [...shape].reverse();
        expect(area(stickerOutline(reversed, Z, rounded))).toBeLessThan(0);
      });
    });
  }

  it("an inset of 0.14 turns a square of half-side 1 into one of half-side 0.86", () => {
    const square: Point3[] = [[-1, -1, 0], [1, -1, 0], [1, 1, 0], [-1, 1, 0]];
    const out = stickerOutline(square, Z, { insetRatio: 0.14, cornerRatio: 0, cornerSegments: 1 });
    for (const p of out) {
      expect(Math.abs(p[0])).toBeCloseTo(0.86, 9);
      expect(Math.abs(p[1])).toBeCloseTo(0.86, 9);
    }
  });

  it("zero inset and zero rounding return the polygon unchanged", () => {
    const out = stickerOutline(SHAPES.pentagon!, Z, { insetRatio: 0, cornerRatio: 0, cornerSegments: 1 });
    out.forEach((p, i) => p.forEach((c, k) => expect(c).toBeCloseTo(SHAPES.pentagon![i]![k]!, 9)));
  });

  it("works on a polygon in any plane, and keeps it flat", () => {
    // A square on the x = 2 face of a cube, facing +x.
    const face: Point3[] = [[2, -1, -1], [2, 1, -1], [2, 1, 1], [2, -1, 1]];
    const out = stickerOutline(face, [1, 0, 0], rounded);
    for (const p of out) expect(p[0]).toBeCloseTo(2, 9);
    expect(out.length).toBe(16);
  });

  it("is independent of the polygon's size", () => {
    const big = stickerOutline(SHAPES.pentagon!.map((p): Point3 => [p[0] * 10, p[1] * 10, 0]), Z, rounded);
    const small = stickerOutline(SHAPES.pentagon!, Z, rounded);
    expect(area(big) / area(small)).toBeCloseTo(100, 6);
  });

  it("clamps an absurd inset instead of inverting the polygon", () => {
    const out = stickerOutline(SHAPES.square!, Z, { insetRatio: 5, cornerRatio: 0, cornerSegments: 1 });
    expect(area(out)).toBeGreaterThan(0);
  });

  it("rejects fewer than three points", () => {
    expect(() => stickerOutline([[0, 0, 0], [1, 0, 0]], Z, rounded)).toThrow();
  });
});
