import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { instantiatePuzzleModule } from "../../src/engine/wasm-module.js";
import { Puzzle } from "../../src/engine/puzzle.js";
import { PuzzleType } from "../../src/engine/types.js";
import * as THREE from "three";
import { buildCubicScene } from "../../src/render/scene.js";

const WASM_PATH = fileURLToPath(new URL("../../../core/build/rubik_mega.wasm", import.meta.url));

async function loadModule() {
  return instantiatePuzzleModule(readFileSync(WASM_PATH));
}

// Structural only, no pixels -- CLAUDE.md: "Rendering/visual code is
// reviewed by eye". This just proves the scene graph docs/en/
// 03-architecture.md S6 calls for actually gets built (1 body
// InstancedMesh + 1 per sticker colour), and that sync() after a move
// doesn't throw and keeps every colour's instance count within its
// fixed capacity.
describe("buildCubicScene", () => {
  it("builds one body mesh plus 6 sticker-colour meshes", async () => {
    const module = await loadModule();
    const puzzle = Puzzle.create(module, PuzzleType.Cubic, 3);
    const { group } = buildCubicScene(puzzle);
    expect(group.children).toHaveLength(7);
    puzzle.destroy();
  });

  it("sync() after a move does not throw, and each sticker mesh stays at count 9", async () => {
    const module = await loadModule();
    const puzzle = Puzzle.create(module, PuzzleType.Cubic, 3);
    const { group, sync } = buildCubicScene(puzzle);
    puzzle.applyMove(1, 2); // R2 -- arbitrary, just needs to change state
    expect(() => sync()).not.toThrow();
    for (const child of group.children.slice(1)) {
      // children[0] is the body mesh; the rest are the 6 sticker meshes.
      expect((child as THREE.InstancedMesh).count).toBe(9);
    }
    puzzle.destroy();
  });

  it("any supported size builds: surface cubies only, n*n stickers per colour", async () => {
    const module = await loadModule();
    for (const n of [3, 4, 5, 6, 7, 8]) {
      const puzzle = Puzzle.create(module, PuzzleType.Cubic, n);
      const { group, sync } = buildCubicScene(puzzle);
      puzzle.scramble(30, n);
      sync();
      const [body, ...stickers] = group.children as THREE.InstancedMesh[];
      expect(body!.count).toBe(n ** 3 - (n - 2) ** 3);
      for (const mesh of stickers) expect(mesh.count).toBe(n * n);
      puzzle.destroy();
    }
  });
});
