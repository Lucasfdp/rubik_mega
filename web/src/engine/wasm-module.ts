// Thin typed wrapper over core/'s compiled WASM module
// (core/build/rubik_mega.wasm). docs/en/03-architecture.md S3 is the
// full contract this mirrors; core/include/puzzle.h is the C side.
//
// Scope, deliberately: this file only instantiates the module and
// exposes its exports with real types. It runs on the main thread for
// now -- moving it into a Web Worker is docs/en/04-roadmap.md Phase 0
// step 4, once something actually needs the worker (a real solver;
// puzzle_solve is still a stub as of this writing).

/// The WASM module's full export surface, typed. Every entry here must
/// match a `WASM_EXPORT(...)` in core/include/puzzle.h exactly, by name
/// and signature -- pointers are plain `number` (this is a 32-bit
/// address space), not a distinct type, same as the C side treats them
/// as opaque handles.
export interface PuzzleModuleExports {
  readonly memory: WebAssembly.Memory;
  puzzle_create(puzzleType: number, size: number): number;
  puzzle_destroy(puzzlePtr: number): void;
  puzzle_apply_move(puzzlePtr: number, generator: number, amount: number): void;
  puzzle_scramble(puzzlePtr: number, moveCount: number, seed: number): void;
  puzzle_state_ptr(puzzlePtr: number): number;
  puzzle_state_size(puzzlePtr: number): number;
  puzzle_algorithm_count(puzzlePtr: number): number;
  puzzle_algorithm_name(puzzlePtr: number, algoId: number): number;
  puzzle_solve(
    puzzlePtr: number,
    algoId: number,
    outPtr: number,
    maxMoves: number,
    outPhaseTagsPtr: number,
  ): number;
  // docs/en/07-nxn-web-view.md D1: core/ owns every colour rule; web/
  // only reads these back.
  puzzle_facelet_count(puzzlePtr: number): number;
  puzzle_facelet_geometry(puzzlePtr: number): number;
  puzzle_facelets(puzzlePtr: number): number;
  puzzle_generator_count(puzzlePtr: number): number;
  puzzle_generator_face(puzzlePtr: number, generator: number): number;
  puzzle_generator_depth(puzzlePtr: number, generator: number): number;
  puzzle_solve_max_moves(puzzlePtr: number): number;
  // docs/en/09-phase3-skewb-pyraminx.md D4, D5: per-family geometry for
  // the cut-plane puzzles (Skewb, Pyraminx).
  puzzle_facelet_geometry_stride(puzzlePtr: number): number;
  puzzle_generator_axis(puzzlePtr: number, generator: number, outPtr: number): number;
  puzzle_generator_facelets(puzzlePtr: number, generator: number, outPtr: number, max: number): number;
  // docs/en/12-phase5-square1.md D2, D4: per-generator step counts, move
  // legality in the current state, and Square-1's piece-kind view.
  puzzle_generator_steps(puzzlePtr: number, generator: number): number;
  puzzle_move_is_legal(puzzlePtr: number, generator: number, amount: number): number;
  puzzle_square1_units(puzzlePtr: number): number;
  // docs/en/13-phase6-polish.md D9: "does it look solved" (core owns the
  // rule: whole-puzzle rotations count) and the moves a scramble applies,
  // so web/ can animate it.
  puzzle_is_solved(puzzlePtr: number): number;
  puzzle_scramble_moves(puzzlePtr: number, outPtr: number, max: number, moveCount: number, seed: number): number;
  puzzle_scramble_max_moves(puzzlePtr: number, moveCount: number): number;
}

/// Checked at instantiation time so a drift between this file and
/// core/include/puzzle.h fails loudly, at load, with a name -- not as a
/// bare "undefined is not a function" wherever the missing export first
/// gets called.
const REQUIRED_EXPORTS = [
  "memory",
  "puzzle_create",
  "puzzle_destroy",
  "puzzle_apply_move",
  "puzzle_scramble",
  "puzzle_state_ptr",
  "puzzle_state_size",
  "puzzle_algorithm_count",
  "puzzle_algorithm_name",
  "puzzle_solve",
  "puzzle_facelet_count",
  "puzzle_facelet_geometry",
  "puzzle_facelets",
  "puzzle_generator_count",
  "puzzle_generator_face",
  "puzzle_generator_depth",
  "puzzle_solve_max_moves",
  "puzzle_facelet_geometry_stride",
  "puzzle_generator_axis",
  "puzzle_generator_facelets",
  "puzzle_generator_steps",
  "puzzle_move_is_legal",
  "puzzle_square1_units",
  "puzzle_is_solved",
  "puzzle_scramble_moves",
  "puzzle_scramble_max_moves",
] as const;

/// Instantiates the module from raw WASM bytes. No import object is
/// needed: core/'s WASM build is `wasm32-freestanding -nostdlib` with
/// its own bump allocator (core/src/platform/wasm_alloc.c), so it calls
/// nothing on the host -- confirmed against the built module, not just
/// assumed from the build flags (zero imports, verified by instantiating
/// it with `{}` and checking `instance.exports` directly).
///
/// Kept separate from `loadPuzzleModule` below so it's a pure function
/// of its input, with no `fetch` -- trivial to unit-test with bytes read
/// straight off disk (see web/tests/engine/wasm-module.test.ts).
export async function instantiatePuzzleModule(
  bytes: BufferSource,
): Promise<PuzzleModuleExports> {
  const { instance } = await WebAssembly.instantiate(bytes, {});
  const exports = instance.exports as unknown as Record<string, unknown>;
  for (const name of REQUIRED_EXPORTS) {
    if (!(name in exports)) {
      throw new Error(
        `rubik_mega.wasm is missing expected export "${name}" -- ` +
          "core/include/puzzle.h and web/src/engine/wasm-module.ts have drifted apart",
      );
    }
  }
  return exports as unknown as PuzzleModuleExports;
}

/// Fetches and instantiates the module from a URL. The only piece of
/// this file that needs a browser-like `fetch` -- kept out of
/// `instantiatePuzzleModule` on purpose (see above).
export async function loadPuzzleModule(
  url: string | URL,
): Promise<PuzzleModuleExports> {
  const response = await fetch(url);
  if (!response.ok) {
    throw new Error(`failed to fetch ${url}: ${response.status} ${response.statusText}`);
  }
  return instantiatePuzzleModule(await response.arrayBuffer());
}

/// Default location of the built module, relative to this file. Not
/// used yet -- web/src/main.ts still renders the placeholder cube
/// (docs/en/04-roadmap.md Phase 0 step 2 wires this in for real) -- but
/// exported now so that step is a one-line call, not a rediscovery of
/// the relative path from web/src/engine/ to core/build/.
export const DEFAULT_WASM_URL = new URL(
  "../../../core/build/rubik_mega.wasm",
  import.meta.url,
);

/// Reads a NUL-terminated C string out of WASM memory starting at
/// `ptr`. The only exported function returning `const char *` is
/// `puzzle_algorithm_name` (core/include/puzzle.h) -- nothing else in
/// this module's surface needs this, so it stays a free function
/// rather than hidden inside Puzzle.
export function readCString(memory: WebAssembly.Memory, ptr: number): string {
  const bytes = new Uint8Array(memory.buffer, ptr);
  let length = 0;
  while (length < bytes.length && bytes[length] !== 0) {
    length++;
  }
  return new TextDecoder().decode(bytes.subarray(0, length));
}
