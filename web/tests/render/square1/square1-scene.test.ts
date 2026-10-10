import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import * as THREE from "three";
import { describe, expect, it } from "vitest";
import { instantiatePuzzleModule } from "../../../src/engine/wasm-module.js";
import { Puzzle } from "../../../src/engine/puzzle.js";
import { PuzzleType, SQ1_GEN_SLICE, SQ1_GEN_TOP } from "../../../src/engine/types.js";
import {
  BOTTOM_Y0,
  TOP_Y1,
  MIDDLE_HALVES,
  cutNormal,
  isRightHalf,
  outlineOf,
  placementsFor,
  sideOfCut,
  unitCentreDeg,
} from "../../../src/render/square1/square1-geometry.js";
import { buildSquare1Scene } from "../../../src/render/square1/square1-scene.js";

const WASM_PATH = fileURLToPath(new URL("../../../../core/build/rubik_mega.wasm", import.meta.url));
const load = async () => instantiatePuzzleModule(readFileSync(WASM_PATH));
const EPS = 1e-9;

async function scrambled(seed: number): Promise<Puzzle> {
  const puzzle = Puzzle.create(await load(), PuzzleType.Square1, 0);
  await puzzle.scramble(25, seed);
  return puzzle;
}

describe("Square-1 geometry", () => {
  it("a solved puzzle is the cube shape: corners at the diagonals, edges at the faces", async () => {
    const puzzle = Puzzle.create(await load(), PuzzleType.Square1, 0);
    const top = placementsFor(puzzle.unitsView(), "top");
    expect(top.map((p) => [p.kind, p.firstUnit, p.centreDeg])).toEqual([
      ["corner", 0, 225], // back-right
      ["edge", 2, 270], // right
      ["corner", 3, 315], // right-front
      ["edge", 5, 0], // front
      ["corner", 6, 45], // front-left
      ["edge", 8, 90], // left
      ["corner", 9, 135], // left-back
      ["edge", 11, 180], // back
    ]);
    puzzle.destroy();
  });

  it("every layer's pieces tile its 360 degrees with no gap or overlap, for 200 reachable shapes", async () => {
    const module = await load();
    for (let seed = 1; seed <= 200; seed++) {
      const puzzle = Puzzle.create(module, PuzzleType.Square1, 0);
      await puzzle.scramble(25, seed);
      for (const layer of ["top", "bottom"] as const) {
        const pieces = placementsFor(puzzle.unitsView(), layer);
        expect(pieces.reduce((sum, p) => sum + p.spanDeg, 0)).toBe(360);
        let unit = pieces[0]!.firstUnit;
        for (const piece of pieces) {
          expect(piece.firstUnit).toBe(unit);
          expect(piece.centreDeg).toBeCloseTo((unitCentreDeg(0) + 30 * unit + (piece.spanDeg - 30) / 2) % 360, 9);
          unit = (unit + piece.spanDeg / 30) % 12;
        }
        expect(unit).toBe(pieces[0]!.firstUnit);
      }
      puzzle.destroy();
    }
  });

  it("the cube shape is a square: the outer corners of a solved layer all lie on it", async () => {
    const puzzle = Puzzle.create(await load(), PuzzleType.Square1, 0);
    for (const piece of placementsFor(puzzle.unitsView(), "top")) {
      for (const [x, z] of outlineOf(piece).slice(1)) {
        expect(Math.max(Math.abs(x), Math.abs(z))).toBeCloseTo(1.1, 9);
      }
    }
    puzzle.destroy();
  });

  it("the cut puts the right half's pieces on one side and the left half's on the other, in every legal shape", async () => {
    const module = await load();
    let legalShapes = 0;
    for (let seed = 1; seed <= 200; seed++) {
      const puzzle = Puzzle.create(module, PuzzleType.Square1, 0);
      await puzzle.scramble(25, seed);
      if (puzzle.isMoveLegal(SQ1_GEN_SLICE, 1)) {
        legalShapes++;
        for (const layer of ["top", "bottom"] as const) {
          for (const piece of placementsFor(puzzle.unitsView(), layer)) {
            for (const [x, z] of outlineOf(piece)) {
              const side = sideOfCut(x, z);
              if (isRightHalf(piece)) expect(side).toBeGreaterThan(-EPS);
              else expect(side).toBeLessThan(EPS);
            }
          }
        }
      }
      puzzle.destroy();
    }
    expect(legalShapes).toBeGreaterThan(20);
  });

  it("the middle halves lie on their own sides of the cut", () => {
    for (const half of MIDDLE_HALVES) {
      for (const [x, z] of half.outline) {
        const side = sideOfCut(x, z);
        if (half.half === "right") expect(side).toBeGreaterThan(-EPS);
        else expect(side).toBeLessThan(EPS);
      }
    }
  });

  it("turning the right half about the cut's normal never carries a vertex across the cut, at every 30 degrees", () => {
    const [nx, nz] = cutNormal();
    const axis = new THREE.Vector3(nx, 0, nz);
    const points = MIDDLE_HALVES[1]!.outline.flatMap(([x, z]) => [new THREE.Vector3(x, TOP_Y1, z), new THREE.Vector3(x, BOTTOM_Y0, z)]);
    for (let deg = 0; deg <= 360; deg += 30) {
      const q = new THREE.Quaternion().setFromAxisAngle(axis, (deg * Math.PI) / 180);
      for (const p of points) {
        const moved = p.clone().applyQuaternion(q);
        expect(sideOfCut(moved.x, moved.z)).toBeGreaterThan(-1e-9);
      }
    }
  });

  it("the slice sends the right half's top pieces where the bottom ones were (180 degrees about the normal)", async () => {
    const puzzle = Puzzle.create(await load(), PuzzleType.Square1, 0);
    const [nx, nz] = cutNormal();
    const q = new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(nx, 0, nz), Math.PI);
    // Top unit i goes to bottom unit 5 - i: the centre of top unit i,
    // turned, lands on the centre of bottom unit 5 - i.
    for (let i = 0; i < 6; i++) {
      const deg = unitCentreDeg(i);
      const p = new THREE.Vector3(-Math.sin((deg * Math.PI) / 180), 1, Math.cos((deg * Math.PI) / 180)).applyQuaternion(q);
      const target = unitCentreDeg(5 - i);
      expect(p.y).toBeCloseTo(-1, 9);
      expect(Math.atan2(-p.x, p.z) * (180 / Math.PI) + 360).toBeCloseTo(target + (target < 180 ? 360 : 0), 6);
    }
    puzzle.destroy();
  });
});

describe("Square-1 scene", () => {
  it("lays out 16 pieces and the two middle halves, and rebuilds them on sync", async () => {
    const puzzle = await scrambled(7);
    const scene = buildSquare1Scene(puzzle);
    expect(scene.group.children).toHaveLength(18);
    await puzzle.applyMove(SQ1_GEN_TOP, 1);
    scene.sync();
    expect(scene.group.children).toHaveLength(18);
    scene.dispose();
    puzzle.destroy();
  });

  it("pick: a ray from above meets a top piece, one from the front at the middle height the middle layer", async () => {
    const puzzle = Puzzle.create(await load(), PuzzleType.Square1, 0);
    const scene = buildSquare1Scene(puzzle);
    const down = scene.pick(new THREE.Ray(new THREE.Vector3(0.7, 5, 0.3), new THREE.Vector3(0, -1, 0)));
    expect(down?.layer).toBe("top");
    expect(down?.point.y).toBeCloseTo(TOP_Y1, 6);
    const up = scene.pick(new THREE.Ray(new THREE.Vector3(0.7, -5, 0.3), new THREE.Vector3(0, 1, 0)));
    expect(up?.layer).toBe("bottom");
    const front = scene.pick(new THREE.Ray(new THREE.Vector3(0.7, 0, 5), new THREE.Vector3(0, 0, -1)));
    expect(front?.layer).toBe("middle");
    expect(front?.half).toBe("right");
    const left = scene.pick(new THREE.Ray(new THREE.Vector3(-0.9, 0, 5), new THREE.Vector3(0, 0, -1)));
    expect(left?.layer).toBe("middle");
    expect(left?.half).toBe("left");
    expect(scene.pick(new THREE.Ray(new THREE.Vector3(9, 0, 5), new THREE.Vector3(0, 0, -1)))).toBeNull();
    scene.dispose();
    puzzle.destroy();
  });

  it("a top turn moves only the top pieces; a slice only the right half's, and rests when cleared", async () => {
    const puzzle = Puzzle.create(await load(), PuzzleType.Square1, 0);
    const scene = buildSquare1Scene(puzzle);
    scene.setActiveTurn({ generator: SQ1_GEN_TOP, angleDeg: 30 });
    const turned = scene.group.children.filter((c) => !c.quaternion.equals(new THREE.Quaternion()));
    expect(turned).toHaveLength(8); // the top layer's eight pieces
    scene.setActiveTurn({ generator: SQ1_GEN_SLICE, angleDeg: 90 });
    const sliced = scene.group.children.filter((c) => !c.quaternion.equals(new THREE.Quaternion()));
    // right half: 4 pieces of the top layer, 4 of the bottom, the middle half
    expect(sliced).toHaveLength(9);
    scene.setActiveTurn(null);
    expect(scene.group.children.every((c) => c.quaternion.equals(new THREE.Quaternion()))).toBe(true);
    scene.dispose();
    puzzle.destroy();
  });

  it("after a slice the right middle half is drawn turned over, and sync agrees with the slice's final pose", async () => {
    const puzzle = Puzzle.create(await load(), PuzzleType.Square1, 0);
    const scene = buildSquare1Scene(puzzle);
    expect(puzzle.isMoveLegal(SQ1_GEN_SLICE, 1)).toBe(true);
    await puzzle.applyMove(SQ1_GEN_SLICE, 1);
    scene.sync();
    const right = scene.group.children[scene.group.children.length - 1]!;
    expect(right.quaternion.equals(new THREE.Quaternion())).toBe(false);
    // The pose a finished slice animation leaves is the same rotation.
    const [nx, nz] = cutNormal();
    const expected = new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(nx, 0, nz), Math.PI);
    expect(Math.abs(right.quaternion.dot(expected))).toBeCloseTo(1, 9);
    scene.dispose();
    puzzle.destroy();
  });
});
