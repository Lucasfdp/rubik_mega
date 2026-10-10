import { describe, expect, it } from "vitest";
import { RemotePuzzle, type WorkerLike } from "../../src/engine/remote-puzzle.js";
import type { PuzzleWorkerRequest, PuzzleWorkerResponse } from "../../src/engine/worker-protocol.js";
import type { PuzzleLayout } from "../../src/engine/types.js";

/// Placeholder create-time payload -- RemotePuzzle only stores and
/// hands these back, it never interprets them, so their contents don't
/// matter here (puzzle-worker-logic.test.ts checks real ones).
const FACELETS = new Uint8Array(54);
const LAYOUT: PuzzleLayout = { size: 3, facelets: [], generators: [] };

/// A same-thread stand-in for a real Worker: a real `Worker` isn't
/// available in vitest's Node environment, and RemotePuzzle only ever
/// touches the WorkerLike surface (postMessage/addEventListener/
/// removeEventListener/terminate), so this is enough to exercise its
/// id-correlation and error-handling logic without any WASM, any real
/// threading, or puzzle-worker-logic.ts at all -- `respond` lets each
/// test script exactly what the "worker side" sends back, and when.
class FakeWorker implements WorkerLike {
  #listeners = new Set<(event: MessageEvent<PuzzleWorkerResponse>) => void>();
  readonly sent: PuzzleWorkerRequest[] = [];
  terminated = false;

  postMessage(message: unknown): void {
    this.sent.push(message as PuzzleWorkerRequest);
  }

  addEventListener(_type: "message", listener: (event: MessageEvent<PuzzleWorkerResponse>) => void): void {
    this.#listeners.add(listener);
  }

  removeEventListener(_type: "message", listener: (event: MessageEvent<PuzzleWorkerResponse>) => void): void {
    this.#listeners.delete(listener);
  }

  terminate(): void {
    this.terminated = true;
  }

  /// Delivers `response` to every current listener, as if it came from
  /// the real Worker's message port.
  respond(response: PuzzleWorkerResponse): void {
    for (const listener of this.#listeners) {
      listener({ data: response } as MessageEvent<PuzzleWorkerResponse>);
    }
  }
}

describe("RemotePuzzle", () => {
  it("create() sends a \"create\" request and resolves once the matching response arrives", async () => {
    const worker = new FakeWorker();
    const createPromise = RemotePuzzle.create(worker, 0, 3);

    expect(worker.sent).toEqual([{ id: 0, kind: "create", puzzleType: 0, size: 3 }]);
    const initialState = new Uint8Array(40);
    worker.respond({ id: 0, kind: "created", state: initialState, facelets: FACELETS, layout: LAYOUT });

    const remote = await createPromise;
    expect(remote.stateView()).toBe(initialState);
    expect(remote.faceletView()).toBe(FACELETS);
    expect(remote.layout).toBe(LAYOUT);
  });

  it("applyMove() correlates by id even if an unrelated response arrives first", async () => {
    const worker = new FakeWorker();
    const createPromise = RemotePuzzle.create(worker, 0, 3);
    worker.respond({ id: 0, kind: "created", state: new Uint8Array(40), facelets: FACELETS, layout: LAYOUT });
    const remote = await createPromise;

    const movedState = new Uint8Array(40).fill(7);
    const movedFacelets = new Uint8Array(54).fill(2);
    const applyPromise = remote.applyMove(1, 1);
    expect(worker.sent[1]).toEqual({ id: 1, kind: "applyMove", generator: 1, amount: 1 });

    // A response for an id nobody's waiting on (stale/duplicate) is
    // ignored rather than crashing or resolving the wrong request.
    worker.respond({ id: 99, kind: "state", state: new Uint8Array(1), facelets: FACELETS });
    worker.respond({ id: 1, kind: "state", state: movedState, facelets: movedFacelets });

    await applyPromise;
    expect(remote.faceletView()).toBe(movedFacelets);
    expect(remote.stateView()).toBe(movedState);
  });

  it("an \"error\" response rejects the matching call", async () => {
    const worker = new FakeWorker();
    const createPromise = RemotePuzzle.create(worker, 0, 3);
    worker.respond({ id: 0, kind: "created", state: new Uint8Array(40), facelets: FACELETS, layout: LAYOUT });
    const remote = await createPromise;

    const scramblePromise = remote.scramble(20, 42);
    worker.respond({ id: 1, kind: "error", message: "boom" });
    await expect(scramblePromise).rejects.toThrow("boom");
  });

  it("destroy() posts a best-effort \"destroy\" message, terminates the worker, and rejects anything still in flight", async () => {
    const worker = new FakeWorker();
    const createPromise = RemotePuzzle.create(worker, 0, 3);
    worker.respond({ id: 0, kind: "created", state: new Uint8Array(40), facelets: FACELETS, layout: LAYOUT });
    const remote = await createPromise;

    const stuckPromise = remote.applyMove(0, 1);
    stuckPromise.catch(() => {}); // the assertion below is what actually observes the rejection

    remote.destroy();

    expect(worker.sent.at(-1)).toEqual({ id: 2, kind: "destroy" });
    expect(worker.terminated).toBe(true);
    await expect(stuckPromise).rejects.toThrow(/in flight/);
  });

  it("stateView() stays synchronous, always returning the last-received snapshot", async () => {
    const worker = new FakeWorker();
    const createPromise = RemotePuzzle.create(worker, 0, 3);
    const firstState = new Uint8Array(40);
    worker.respond({ id: 0, kind: "created", state: firstState, facelets: FACELETS, layout: LAYOUT });
    const remote = await createPromise;
    expect(remote.stateView()).toBe(firstState); // no round-trip needed to read it back
  });

  it("algorithmCount()/algorithmName() round-trip to their own response kinds", async () => {
    const worker = new FakeWorker();
    const createPromise = RemotePuzzle.create(worker, 0, 3);
    worker.respond({ id: 0, kind: "created", state: new Uint8Array(40), facelets: FACELETS, layout: LAYOUT });
    const remote = await createPromise;

    const countPromise = remote.algorithmCount();
    expect(worker.sent[1]).toEqual({ id: 1, kind: "algorithmCount" });
    worker.respond({ id: 1, kind: "algorithmCount", count: 4 });
    await expect(countPromise).resolves.toBe(4);

    const namePromise = remote.algorithmName(2);
    expect(worker.sent[2]).toEqual({ id: 2, kind: "algorithmName", algoId: 2 });
    worker.respond({ id: 2, kind: "algorithmName", name: "Layer-by-layer (beginner)" });
    await expect(namePromise).resolves.toBe("Layer-by-layer (beginner)");
  });

  it("solve() round-trips the full SolveResult and never touches stateView()", async () => {
    const worker = new FakeWorker();
    const createPromise = RemotePuzzle.create(worker, 0, 3);
    const initialState = new Uint8Array(40);
    worker.respond({ id: 0, kind: "created", state: initialState, facelets: FACELETS, layout: LAYOUT });
    const remote = await createPromise;

    const solvePromise = remote.solve(0);
    expect(worker.sent[1]).toEqual({ id: 1, kind: "solve", algoId: 0 });
    const result = { moves: [{ generator: 1, amount: 1 }], phaseTags: [0] };
    worker.respond({ id: 1, kind: "solve", result });
    await expect(solvePromise).resolves.toEqual(result);
    // Computing a solution must not be mistaken for applying it.
    expect(remote.stateView()).toBe(initialState);
  });

  it("solve() resolves null when the worker reports search failure", async () => {
    const worker = new FakeWorker();
    const createPromise = RemotePuzzle.create(worker, 0, 3);
    worker.respond({ id: 0, kind: "created", state: new Uint8Array(40), facelets: FACELETS, layout: LAYOUT });
    const remote = await createPromise;

    const solvePromise = remote.solve(0);
    worker.respond({ id: 1, kind: "solve", result: null });
    await expect(solvePromise).resolves.toBeNull();
  });
});
