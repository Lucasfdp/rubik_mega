import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { instantiatePuzzleModule, readCString } from "../../src/engine/wasm-module.js";

const WASM_PATH = fileURLToPath(
  new URL("../../../core/build/rubik_mega.wasm", import.meta.url),
);

function loadBytes(): Buffer {
  return readFileSync(WASM_PATH);
}

describe("instantiatePuzzleModule", () => {
  it("instantiates with no imports and exposes the documented export surface", async () => {
    const module = await instantiatePuzzleModule(loadBytes());
    expect(module.memory).toBeInstanceOf(WebAssembly.Memory);
    expect(typeof module.puzzle_create).toBe("function");
    expect(typeof module.puzzle_solve).toBe("function");
  });

  it("throws a clear error if a required export is missing", async () => {
    // A minimal, validly-headered WASM module with none of puzzle.h's
    // exports -- proves the drift check actually fires on a real
    // mismatch, not just passes by construction against the real file.
    const EMPTY_WASM = new Uint8Array([0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00]);
    await expect(instantiatePuzzleModule(EMPTY_WASM)).rejects.toThrow(
      /missing expected export "memory"/,
    );
  });
});

describe("readCString", () => {
  it("reads up to the first NUL byte", () => {
    const memory = new WebAssembly.Memory({ initial: 1 });
    const bytes = new Uint8Array(memory.buffer);
    const encoded = new TextEncoder().encode("Kociemba (two-phase)\0trailing garbage");
    bytes.set(encoded, 100);
    expect(readCString(memory, 100)).toBe("Kociemba (two-phase)");
  });

  it("returns an empty string for an immediate NUL", () => {
    const memory = new WebAssembly.Memory({ initial: 1 });
    expect(readCString(memory, 0)).toBe("");
  });
});
