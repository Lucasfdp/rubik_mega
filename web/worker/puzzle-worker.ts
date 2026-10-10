/// <reference lib="webworker" />

/// Worker entry point (docs/en/03-architecture.md S9's repo layout:
/// worker/ is a top-level sibling of src/, its own bundle entry rather
/// than part of the main-thread app). Deliberately thin: loads the WASM
/// module once, then for every message forwards it to
/// handlePuzzleWorkerRequest (puzzle-worker-logic.ts) and posts back
/// whatever it returns -- all the actual request-handling logic lives
/// there, specifically so it's unit-testable outside a real Worker.
///
/// Requests are handled strictly in the order their "message" events
/// fire: onmessage awaits the full handlePuzzleWorkerRequest() call
/// before returning, so a second message already queued behind it
/// won't start processing until the first's response has been posted.
/// That's enough ordering for this codebase's one actual client
/// (RemotePuzzle, via TurnAnimation, which never has two moves in
/// flight at once -- see remote-puzzle.ts's own doc comment); it is
/// not a general-purpose request queue.
import { DEFAULT_WASM_URL, loadPuzzleModule } from "../src/engine/index.js";
import { handlePuzzleWorkerRequest, type PuzzleRef } from "./puzzle-worker-logic.js";
import type { PuzzleWorkerRequest } from "../src/engine/worker-protocol.js";

const ref: PuzzleRef = { current: null };
const modulePromise = loadPuzzleModule(DEFAULT_WASM_URL);

self.onmessage = async (event: MessageEvent<PuzzleWorkerRequest>): Promise<void> => {
  const module = await modulePromise;
  const response = await handlePuzzleWorkerRequest(module, ref, event.data);
  const transfer = response.kind === "state" ? [response.state.buffer] : [];
  self.postMessage(response, transfer);
};
