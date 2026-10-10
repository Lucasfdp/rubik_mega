import type { PuzzleHandle } from "./puzzle-handle.js";
import type { PuzzleSnapshot, PuzzleWorkerRequest, PuzzleWorkerRequestBody, PuzzleWorkerResponse } from "./worker-protocol.js";
import type { Move, PuzzleLayout, SolveResult } from "./types.js";

/// The subset of the real Worker API RemotePuzzle depends on, so tests
/// can inject an in-process fake instead of a real Worker -- Workers
/// aren't available in vitest's Node environment (see
/// web/tests/engine/remote-puzzle.test.ts). A real `Worker` satisfies
/// this structurally with no adapter needed.
export interface WorkerLike {
  postMessage(message: unknown, transfer?: Transferable[]): void;
  addEventListener(type: "message", listener: (event: MessageEvent<PuzzleWorkerResponse>) => void): void;
  removeEventListener(type: "message", listener: (event: MessageEvent<PuzzleWorkerResponse>) => void): void;
  terminate(): void;
}

interface PendingRequest {
  /// Resolves with the FULL response object, not just its payload --
  /// each calling method below (applyMove/scramble/algorithmCount/...)
  /// picks the one field it needs and narrows on `kind` itself, since
  /// different request kinds succeed with different response shapes
  /// (worker-protocol.ts: "state" vs "algorithmCount" vs "algorithmName"
  /// vs "solve") and #request() has no way to know which one a given
  /// caller is expecting.
  resolve: (response: Exclude<PuzzleWorkerResponse, { kind: "error" }>) => void;
  reject: (error: Error) => void;
}

/// Worker-backed PuzzleHandle (docs/en/04-roadmap.md Phase 0 step 4):
/// every mutation round-trips through `worker`, correlated by an
/// auto-incrementing id against a single onmessage listener so several
/// requests could in principle be in flight at once -- though in
/// practice nothing in this codebase issues a second one before the
/// first resolves (TurnAnimation only ever has one turn in flight;
/// puzzle-worker.ts's own onmessage is written assuming the same).
///
/// stateView() always reads the last snapshot a response delivered,
/// never live Worker-side memory -- there's no shared memory to read
/// (Lucas's snapshot-transfer choice over SharedArrayBuffer).
export class RemotePuzzle implements PuzzleHandle {
  readonly #worker: WorkerLike;
  #nextId = 0;
  readonly #pending = new Map<number, PendingRequest>();
  #lastState: Uint8Array = new Uint8Array(0);
  #lastFacelets: Uint8Array = new Uint8Array(0);
  #lastUnits: Uint8Array = new Uint8Array(0);
  #lastLegal: Uint8Array = new Uint8Array(0);
  #lastSolved = true;
  #layout: PuzzleLayout | null = null;

  private constructor(worker: WorkerLike) {
    this.#worker = worker;
    this.#worker.addEventListener("message", this.#onMessage);
  }

  get layout(): PuzzleLayout {
    if (!this.#layout) throw new Error("RemotePuzzle.layout read before create() resolved");
    return this.#layout;
  }

  /// Creates the Worker-side puzzle and waits for its initial state --
  /// by the time this resolves, stateView() already has real data, so
  /// RemotePuzzle never needs an "is it ready yet" check of its own.
  static async create(worker: WorkerLike, puzzleType: number, size: number): Promise<RemotePuzzle> {
    const remote = new RemotePuzzle(worker);
    const response = await remote.#request({ kind: "create", puzzleType, size });
    if (response.kind !== "created") throw new Error(`expected a "created" response to "create", got "${response.kind}"`);
    remote.#take(response);
    remote.#layout = response.layout;
    return remote;
  }

  /// Keeps the snapshot a response delivered.
  #take(snapshot: PuzzleSnapshot): void {
    this.#lastState = snapshot.state;
    this.#lastFacelets = snapshot.facelets;
    this.#lastUnits = snapshot.units ?? new Uint8Array(0);
    this.#lastLegal = snapshot.legal ?? new Uint8Array(0);
    this.#lastSolved = snapshot.solved ?? false;
  }

  #onMessage = (event: MessageEvent<PuzzleWorkerResponse>): void => {
    const response = event.data;
    const pending = this.#pending.get(response.id);
    if (!pending) return; // stale or unrecognised id -- nothing waiting on it
    this.#pending.delete(response.id);
    if (response.kind === "error") {
      pending.reject(new Error(response.message));
    } else {
      pending.resolve(response);
    }
  };

  #request(body: PuzzleWorkerRequestBody): Promise<Exclude<PuzzleWorkerResponse, { kind: "error" }>> {
    const id = this.#nextId++;
    return new Promise((resolve, reject) => {
      this.#pending.set(id, { resolve, reject });
      this.#worker.postMessage({ ...body, id } satisfies PuzzleWorkerRequest);
    });
  }

  async applyMove(generator: number, amount: number): Promise<void> {
    const response = await this.#request({ kind: "applyMove", generator, amount });
    if (response.kind !== "state") throw new Error(`expected a "state" response to "applyMove", got "${response.kind}"`);
    this.#take(response);
  }

  async applyMoves(moves: readonly Move[]): Promise<void> {
    const response = await this.#request({ kind: "applyMoves", moves: [...moves] });
    if (response.kind !== "state") throw new Error(`expected a "state" response to "applyMoves", got "${response.kind}"`);
    this.#take(response);
  }

  async scramble(moveCount: number, seed: number): Promise<void> {
    const response = await this.#request({ kind: "scramble", moveCount, seed });
    if (response.kind !== "state") throw new Error(`expected a "state" response to "scramble", got "${response.kind}"`);
    this.#take(response);
  }

  async scrambleMoves(moveCount: number, seed: number): Promise<Move[]> {
    const response = await this.#request({ kind: "scrambleMoves", moveCount, seed });
    if (response.kind !== "scrambleMoves") throw new Error(`expected a "scrambleMoves" response, got "${response.kind}"`);
    return response.moves;
  }

  /// Answers from the last snapshot, like stateView().
  isSolved(): boolean {
    return this.#lastSolved;
  }

  stateView(): Uint8Array {
    return this.#lastState;
  }

  faceletView(): Uint8Array {
    return this.#lastFacelets;
  }

  unitsView(): Uint8Array {
    return this.#lastUnits;
  }

  /// Answers from the last snapshot's legality list (worker-protocol.ts);
  /// an empty list means every move is legal.
  isMoveLegal(generator: number, amount: number): boolean {
    if (this.#lastLegal.length === 0) return true;
    const steps = this.layout.generatorSteps;
    let offset = 0;
    for (let g = 0; g < generator; g++) offset += steps[g] ?? 0;
    return this.#lastLegal[offset + amount - 1] === 1;
  }

  async algorithmCount(): Promise<number> {
    const response = await this.#request({ kind: "algorithmCount" });
    if (response.kind !== "algorithmCount") {
      throw new Error(`expected an "algorithmCount" response, got "${response.kind}"`);
    }
    return response.count;
  }

  async algorithmName(algoId: number): Promise<string> {
    const response = await this.#request({ kind: "algorithmName", algoId });
    if (response.kind !== "algorithmName") {
      throw new Error(`expected an "algorithmName" response, got "${response.kind}"`);
    }
    return response.name;
  }

  /// Does not update #lastState -- same as Puzzle.solve(), computing a
  /// solution never implies applying it (see PuzzleHandle.solve()'s doc
  /// comment).
  async solve(algoId: number): Promise<SolveResult | null> {
    const response = await this.#request({ kind: "solve", algoId });
    if (response.kind !== "solve") {
      throw new Error(`expected a "solve" response, got "${response.kind}"`);
    }
    return response.result;
  }

  /// PuzzleHandle.destroy() is synchronous (matching Puzzle.destroy()),
  /// so this can't await the Worker's own cleanup -- it fires a
  /// best-effort "destroy" message (freeing the Worker-side WASM
  /// puzzle) and terminates immediately after. terminate() reclaims
  /// everything regardless, even if the message never gets processed,
  /// so nothing here depends on that message actually landing.
  destroy(): void {
    try {
      this.#worker.postMessage({ id: this.#nextId++, kind: "destroy" } satisfies PuzzleWorkerRequest);
    } catch {
      // Worker may already be unusable -- destroy() must stay safe to call.
    }
    this.#worker.removeEventListener("message", this.#onMessage);
    this.#worker.terminate();
    for (const pending of this.#pending.values()) {
      pending.reject(new Error("RemotePuzzle destroyed with a request still in flight"));
    }
    this.#pending.clear();
  }
}
