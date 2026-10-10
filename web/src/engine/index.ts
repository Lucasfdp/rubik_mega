// Barrel for web/src/engine/ -- the thin typed wrapper over core/'s
// WASM module (docs/en/03-architecture.md S3, S9).

export { PuzzleType, MOVE_STRUCT_BYTES } from "./types.js";
export type { Move, SolveResult } from "./types.js";

export {
  instantiatePuzzleModule,
  loadPuzzleModule,
  DEFAULT_WASM_URL,
  readCString,
} from "./wasm-module.js";
export type { PuzzleModuleExports } from "./wasm-module.js";

export { Puzzle } from "./puzzle.js";
export type { PuzzleHandle } from "./puzzle-handle.js";
export { RemotePuzzle } from "./remote-puzzle.js";
export type { WorkerLike } from "./remote-puzzle.js";
export type { PuzzleWorkerRequest, PuzzleWorkerRequestBody, PuzzleWorkerResponse } from "./worker-protocol.js";
