import { describe, expect, it } from "vitest";
import {
  CUBIE_BODY_SIZE,
  CUBIE_SPACING,
  createCubicGeometry,
  generateCubicLattice,
} from "../../../src/render/lattice/cubic.js";

// Colour/orientation rules moved to core/ (docs/en/07-nxn-web-view.md
// D1) -- core/tests/test_facelets.c checks them against an independent
// geometric model. What's left here is pure geometry.

describe("generateCubicLattice", () => {
  it("returns exactly the surface cells, all distinct, for every supported size", () => {
    for (const n of [3, 4, 5, 6, 7, 8]) {
      const cells = generateCubicLattice(n);
      expect(cells).toHaveLength(n ** 3 - (n - 2) ** 3);
      expect(new Set(cells.map((c) => `${c.x},${c.y},${c.z}`)).size).toBe(cells.length);
      for (const { x, y, z } of cells) {
        expect([x, y, z].some((v) => v === 0 || v === n - 1)).toBe(true);
      }
    }
  });

  it("rejects a size with no meaningful lattice", () => {
    expect(() => generateCubicLattice(1)).toThrow(/size/);
    expect(() => generateCubicLattice(3.5)).toThrow(/size/);
  });
});

describe("createCubicGeometry", () => {
  it("n == 3 reproduces rubik's own 3x3x3 numbers exactly", () => {
    const g = createCubicGeometry(3);
    expect(g.spacing).toBe(CUBIE_SPACING);
    expect(g.bodySize).toBe(CUBIE_BODY_SIZE);
    expect(g.halfExtent).toBeCloseTo(CUBIE_SPACING + CUBIE_BODY_SIZE / 2);
    expect([0, 1, 2].map(g.cellToWorld)).toEqual([-CUBIE_SPACING, 0, CUBIE_SPACING]);
  });

  it("every size occupies the same overall extent (D2: scale, don't move the camera)", () => {
    const reference = createCubicGeometry(3).halfExtent;
    for (const n of [4, 5, 6, 7, 8]) {
      expect(createCubicGeometry(n).halfExtent).toBeCloseTo(reference);
    }
  });

  it("worldToCell inverts cellToWorld, and clamps outside the cube", () => {
    for (const n of [3, 4, 7]) {
      const g = createCubicGeometry(n);
      for (let i = 0; i < n; i++) expect(g.worldToCell(g.cellToWorld(i))).toBe(i);
      expect(g.worldToCell(100)).toBe(n - 1);
      expect(g.worldToCell(-100)).toBe(0);
    }
  });
});
