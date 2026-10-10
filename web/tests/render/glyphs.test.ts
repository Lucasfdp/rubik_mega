import * as THREE from "three";
import { describe, expect, it } from "vitest";
import { PuzzleType } from "../../src/engine/types.js";
import { applyStickerUv, classicLook, lookFor } from "../../src/render/glyphs.js";
import { PALETTES } from "../../src/render/look.js";
import { readableInk } from "../../src/render/palettes.js";

function squareGeometry(): THREE.BufferGeometry {
  // A 2 x 2 square in the XY plane, facing +z, off-centre.
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute("position", new THREE.BufferAttribute(new Float32Array([4, 4, 0, 6, 4, 0, 6, 6, 0, 4, 6, 0]), 3));
  return geometry;
}

describe("applyStickerUv", () => {
  it("maps a square's outline onto the unit square, centred on (0.5, 0.5)", () => {
    const geometry = squareGeometry();
    applyStickerUv(geometry, [0, 0, 1]);
    const uv = Array.from(geometry.getAttribute("uv").array);
    expect(uv).toEqual([0, 0, 1, 0, 1, 1, 0, 1]);
  });

  it("keeps a long thin sticker's centre at the middle and its long side inside the square", () => {
    const geometry = new THREE.BufferGeometry();
    geometry.setAttribute("position", new THREE.BufferAttribute(new Float32Array([0, 0, 0, 4, 0, 0, 4, 1, 0, 0, 1, 0]), 3));
    applyStickerUv(geometry, [0, 0, 1]);
    const uv = geometry.getAttribute("uv");
    const us = Array.from({ length: 4 }, (_, i) => uv.getX(i));
    const vs = Array.from({ length: 4 }, (_, i) => uv.getY(i));
    expect((Math.min(...us) + Math.max(...us)) / 2).toBeCloseTo(0.5);
    expect((Math.min(...vs) + Math.max(...vs)) / 2).toBeCloseTo(0.5);
    expect(Math.max(...us) - Math.min(...us)).toBeCloseTo(1);
    expect(Math.max(...vs) - Math.min(...vs)).toBeCloseTo(0.25);
  });

  it("puts world up at the top of the texture on a side face", () => {
    // A vertical square on the +x face: y runs up.
    const geometry = new THREE.BufferGeometry();
    geometry.setAttribute("position", new THREE.BufferAttribute(new Float32Array([1, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 0]), 3));
    applyStickerUv(geometry, [1, 0, 0]);
    const uv = geometry.getAttribute("uv");
    // Vertices 2 and 3 are the high-y ones: their v is the larger.
    expect(uv.getY(2)).toBeGreaterThan(uv.getY(0));
    expect(uv.getY(3)).toBeGreaterThan(uv.getY(1));
  });

  it("copes with a face looking straight up or down", () => {
    const geometry = new THREE.BufferGeometry();
    geometry.setAttribute("position", new THREE.BufferAttribute(new Float32Array([0, 1, 0, 1, 1, 0, 1, 1, 1, 0, 1, 1]), 3));
    applyStickerUv(geometry, [0, 1, 0]);
    expect(Array.from(geometry.getAttribute("uv").array).every(Number.isFinite)).toBe(true);
  });
});

describe("readableInk", () => {
  it("is black on light colours and white on dark ones", () => {
    expect(readableInk("#ffffff")).toBe("#000000");
    expect(readableInk("#ffd500")).toBe("#000000");
    expect(readableInk("#0051ba")).toBe("#ffffff");
    expect(readableInk("#c41e3a")).toBe("#ffffff");
  });
});

describe("looks", () => {
  it("classicLook is the shipped palette with no marks", () => {
    expect(classicLook(PuzzleType.Cubic)).toEqual({ palette: PALETTES[PuzzleType.Cubic], glyphs: null });
  });

  it("lookFor pairs a palette with its marks, one per colour", () => {
    for (const type of [PuzzleType.Cubic, PuzzleType.Tetrahedral, PuzzleType.Dodecahedral]) {
      const look = lookFor(type, "high-contrast", true);
      expect(look.glyphs).toHaveLength(look.palette.length);
    }
    expect(lookFor(PuzzleType.Cubic, "classic", false).glyphs).toBeNull();
  });
});
