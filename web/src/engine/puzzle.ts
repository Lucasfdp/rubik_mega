import { readCString, type PuzzleModuleExports } from "./wasm-module.js";
import {
  CUBIC_STEPS,
  CUT_FACELET_BYTES,
  CUT_FACELET_WIDE_BYTES,
  DODECA_COORD_SCALE,
  FACELET_GEOMETRY_BYTES,
  PuzzleType,
  MOVE_STRUCT_BYTES,
  SQ1_LAYER_STEPS,
  SQ1_UNIT_COUNT,
  cutStepsFor,
  type CutFacelet,
  type CutGenerator,
  type CutLayout,
  type FaceletGeometry,
  type GeneratorLayer,
  type PuzzleLayout,
  type SolveResult,
  type Move,
} from "./types.js";
import type { PuzzleHandle } from "./puzzle-handle.js";

/// One WASM memory page, the unit memory.grow() counts in.
const WASM_PAGE_BYTES = 65536;

/// One phase tag in puzzle_solve's output: a uint32_t.
const PHASE_TAG_BYTES = 4;

export class Puzzle implements PuzzleHandle {
  readonly #module: PuzzleModuleExports;
  #ptr: number;
  readonly layout: PuzzleLayout;

  private constructor(module: PuzzleModuleExports, ptr: number, type: PuzzleType, size: number) {
    this.#module = module;
    this.#ptr = ptr;
    this.layout = readLayout(module, ptr, type, size);
  }

  static create(module: PuzzleModuleExports, type: PuzzleType, size = 0): Puzzle {
    const ptr = module.puzzle_create(type, size);
    if (ptr === 0) {
      throw new Error(
        `puzzle_create failed for (type=${PuzzleType[type] ?? type}, size=${size}) -- ` +
          "unsupported (type, size) pair, or the WASM arena is out of memory",
      );
    }
    return new Puzzle(module, ptr, type, size);
  }

  #assertAlive(): void {
    if (this.#ptr === 0) {
      throw new Error("Puzzle used after destroy()");
    }
  }

  destroy(): void {
    if (this.#ptr === 0) return;
    this.#module.puzzle_destroy(this.#ptr);
    this.#ptr = 0;
  }

  /// Synchronous under the hood -- the WASM mutation happens on this
  /// same call, before the resolved Promise is returned. The Promise
  /// exists only to satisfy PuzzleHandle (docs/en/04-roadmap.md Phase 0
  /// step 4): it's the same async contract RemotePuzzle's Worker
  /// round-trip has to offer, so callers on either implementation can
  /// `await applyMove(...)` and get one guarantee -- stateView()
  /// reflects it -- regardless of which one they're holding.
  applyMove(generator: number, amount: number): Promise<void> {
    this.#assertAlive();
    this.#module.puzzle_apply_move(this.#ptr, generator, amount);
    return Promise.resolve();
  }

  /// Applies a whole move list in one call -- see PuzzleHandle.applyMoves().
  applyMoves(moves: readonly Move[]): Promise<void> {
    this.#assertAlive();
    for (const { generator, amount } of moves) this.#module.puzzle_apply_move(this.#ptr, generator, amount);
    return Promise.resolve();
  }

  /// See applyMove()'s doc comment -- same synchronous-under-an-async-
  /// contract reasoning.
  scramble(moveCount: number, seed: number): Promise<void> {
    this.#assertAlive();
    this.#module.puzzle_scramble(this.#ptr, moveCount, seed);
    return Promise.resolve();
  }

  /// Core's puzzle_is_solved: every face one colour (see PuzzleHandle).
  isSolved(): boolean {
    this.#assertAlive();
    return this.#module.puzzle_is_solved(this.#ptr) === 1;
  }

  /// Scratch for scrambleMoves(): lazily taken with memory.grow() (this
  /// module exports no malloc, same trick as solve()'s buffers) and kept.
  #scrambleScratchPtr = 0;
  #scrambleScratchMoves = 0;

  scrambleMoves(moveCount: number, seed: number): Promise<Move[]> {
    this.#assertAlive();
    const capacity = this.#module.puzzle_scramble_max_moves(this.#ptr, moveCount);
    if (capacity > this.#scrambleScratchMoves) {
      const bytes = capacity * MOVE_STRUCT_BYTES;
      this.#scrambleScratchPtr = this.#module.memory.buffer.byteLength;
      this.#module.memory.grow(Math.max(1, Math.ceil(bytes / WASM_PAGE_BYTES)));
      this.#scrambleScratchMoves = capacity;
    }
    const count = this.#module.puzzle_scramble_moves(this.#ptr, this.#scrambleScratchPtr, capacity, moveCount, seed);
    if (count > capacity) throw new Error(`puzzle_scramble_moves returned ${count} moves, more than its stated bound ${capacity}`);
    // The views are made after the call: growing memory detaches older ones.
    const view = new DataView(this.#module.memory.buffer);
    const moves: Move[] = [];
    for (let i = 0; i < count; i++) {
      moves.push({
        generator: view.getInt16(this.#scrambleScratchPtr + i * MOVE_STRUCT_BYTES, true),
        amount: view.getInt8(this.#scrambleScratchPtr + i * MOVE_STRUCT_BYTES + 2),
      });
    }
    return Promise.resolve(moves);
  }

  unitsView(): Uint8Array {
    this.#assertAlive();
    if (this.layout.type !== PuzzleType.Square1) return new Uint8Array(0);
    const ptr = this.#module.puzzle_square1_units(this.#ptr);
    return new Uint8Array(this.#module.memory.buffer, ptr, SQ1_UNIT_COUNT);
  }

  isMoveLegal(generator: number, amount: number): boolean {
    this.#assertAlive();
    if (this.layout.type !== PuzzleType.Square1) return true;
    return this.#module.puzzle_move_is_legal(this.#ptr, generator, amount) === 1;
  }

  /// The legality of every move in layout order (see PuzzleSnapshot in
  /// worker-protocol.ts); empty for the families where all are legal.
  legalView(): Uint8Array {
    this.#assertAlive();
    if (this.layout.type !== PuzzleType.Square1) return new Uint8Array(0);
    const legal: number[] = [];
    this.layout.generatorSteps.forEach((steps, generator) => {
      for (let amount = 1; amount <= steps; amount++) legal.push(this.isMoveLegal(generator, amount) ? 1 : 0);
    });
    return Uint8Array.from(legal);
  }

  stateView(): Uint8Array {
    this.#assertAlive();
    const ptr = this.#module.puzzle_state_ptr(this.#ptr);
    const size = this.#module.puzzle_state_size(this.#ptr);
    return new Uint8Array(this.#module.memory.buffer, ptr, size);
  }

  /// Repaints (core's `puzzle_facelets`) and returns a live view of
  /// every facelet's colour -- valid until the next call or the next
  /// memory.grow(); copy it (`.slice()`) to keep it longer.
  faceletView(): Uint8Array {
    this.#assertAlive();
    const ptr = this.#module.puzzle_facelets(this.#ptr);
    return new Uint8Array(this.#module.memory.buffer, ptr, this.layout.faceletCount);
  }

  /// See applyMove()'s doc comment -- same synchronous-under-an-async-
  /// contract reasoning; PuzzleHandle declares this Promise-returning
  /// so RemotePuzzle can satisfy it with a real Worker round-trip.
  algorithmCount(): Promise<number> {
    this.#assertAlive();
    return Promise.resolve(this.#module.puzzle_algorithm_count(this.#ptr));
  }

  /// See applyMove()'s doc comment -- same reasoning.
  algorithmName(algoId: number): Promise<string> {
    this.#assertAlive();
    const ptr = this.#module.puzzle_algorithm_name(this.#ptr, algoId);
    return Promise.resolve(readCString(this.#module.memory, ptr));
  }

  solveRaw(algoId: number, outPtr: number, maxMoves: number, outPhaseTagsPtr: number): number {
    this.#assertAlive();
    return this.#module.puzzle_solve(this.#ptr, algoId, outPtr, maxMoves, outPhaseTagsPtr);
  }

  /// Lazily-allocated, reused scratch region for solve()'s output
  /// buffers -- grown once (via memory.grow(), the only way to get more
  /// WASM memory from the outside: this module exports no `malloc`) and
  /// kept for the lifetime of this Puzzle rather than re-grown per call.
  /// Sized from core's own `puzzle_solve_max_moves`, which depends on
  /// the puzzle's size: a 3x3x3 needs 200, an 8x8x8 reduction thousands
  /// (docs/en/08-nxn-reduction.md D4).
  #scratchMovesPtr: number | null = null;
  #scratchPhaseTagsPtr = 0;
  #maxMoves = 0;

  #ensureScratch(): number {
    if (this.#scratchMovesPtr !== null) return this.#scratchMovesPtr;
    this.#maxMoves = this.#module.puzzle_solve_max_moves(this.#ptr);
    const movesBytes = this.#maxMoves * MOVE_STRUCT_BYTES;
    const phaseTagsBytes = this.#maxMoves * PHASE_TAG_BYTES;
    const totalBytes = movesBytes + phaseTagsBytes;
    const beforePages = this.#module.memory.buffer.byteLength;
    this.#module.memory.grow(Math.max(1, Math.ceil(totalBytes / WASM_PAGE_BYTES)));
    this.#scratchMovesPtr = beforePages;
    this.#scratchPhaseTagsPtr = beforePages + movesBytes;
    return this.#scratchMovesPtr;
  }

  /// Runs `algoId` against the current state, decoding the raw WASM
  /// output into plain arrays -- see PuzzleHandle.solve()'s doc comment
  /// for the null-on-failure contract. The scratch buffers live past
  /// this call (see #ensureScratch), but the DataView/Uint8Array views
  /// onto them are re-created each time: memory.grow() can detach a
  /// previously-constructed buffer view, so holding one across calls
  /// would risk reading a stale, detached ArrayBuffer.
  solve(algoId: number): Promise<SolveResult | null> {
    this.#assertAlive();
    const movesPtr = this.#ensureScratch();
    const phaseTagsPtr = this.#scratchPhaseTagsPtr;
    const count = this.#module.puzzle_solve(this.#ptr, algoId, movesPtr, this.#maxMoves, phaseTagsPtr);
    if (count === 0xffffffff) {
      return Promise.resolve(null);
    }
    const view = new DataView(this.#module.memory.buffer);
    const moves: Move[] = [];
    const phaseTags: number[] = [];
    for (let i = 0; i < count; i++) {
      const generator = view.getInt16(movesPtr + i * MOVE_STRUCT_BYTES, true);
      const amount = view.getInt8(movesPtr + i * MOVE_STRUCT_BYTES + 2);
      moves.push({ generator, amount });
      phaseTags.push(view.getUint32(phaseTagsPtr + i * PHASE_TAG_BYTES, true));
    }
    return Promise.resolve({ moves, phaseTags });
  }
}

/// Reads everything fixed about a freshly created puzzle out of WASM
/// memory once. Plain JS objects afterwards -- they cross the Worker
/// boundary by structured clone and never touch WASM memory again.
function readLayout(module: PuzzleModuleExports, ptr: number, type: PuzzleType, size: number): PuzzleLayout {
  const faceletCount = module.puzzle_facelet_count(ptr);
  const generatorCount = module.puzzle_generator_count(ptr);
  if (type === PuzzleType.Square1) {
    // No facelet geometry and no cut planes: the scene places pieces by
    // angle from unitsView() (docs/en/12-phase5-square1.md D4, D5).
    const generatorSteps: number[] = [];
    for (let g = 0; g < generatorCount; g++) generatorSteps.push(module.puzzle_generator_steps(ptr, g));
    const generators: GeneratorLayer[] = generatorSteps.map(() => ({ face: -1, depth: -1 }));
    return { type, size, steps: SQ1_LAYER_STEPS, generatorSteps, faceletCount, facelets: [], generators, cut: null };
  }
  if (type !== PuzzleType.Cubic) {
    const cut = readCutLayout(module, ptr, type, faceletCount, generatorCount);
    const generators: GeneratorLayer[] = cut.generators.map(() => ({ face: -1, depth: -1 }));
    const steps = cutStepsFor(type);
    return { type, size, steps, generatorSteps: generators.map(() => steps), faceletCount, facelets: [], generators, cut };
  }
  const bytes = new Uint8Array(
    module.memory.buffer,
    module.puzzle_facelet_geometry(ptr),
    faceletCount * FACELET_GEOMETRY_BYTES,
  );
  const facelets: FaceletGeometry[] = [];
  for (let i = 0; i < faceletCount; i++) {
    const at = i * FACELET_GEOMETRY_BYTES;
    facelets.push({ x: bytes[at]!, y: bytes[at + 1]!, z: bytes[at + 2]!, face: bytes[at + 3]! });
  }
  const generators: GeneratorLayer[] = [];
  for (let g = 0; g < generatorCount; g++) {
    generators.push({ face: module.puzzle_generator_face(ptr, g), depth: module.puzzle_generator_depth(ptr, g) });
  }
  return {
    type,
    size,
    steps: CUBIC_STEPS,
    generatorSteps: generators.map(() => CUBIC_STEPS),
    faceletCount,
    facelets,
    generators,
    cut: null,
  };
}

/// Bytes of scratch an output-buffer export needs: 4 int32s for an
/// axis, or one byte per facelet -- whichever is larger, rounded up.
const AXIS_INTS = 4;

/// Reads a cut-plane puzzle's facelet records and every generator's
/// axis, threshold and moving facelets. Core's exports write into
/// caller-provided memory and this module has no malloc, so a small
/// scratch region is taken with memory.grow() (the same trick solve()
/// uses) -- once, here, at creation. Views are re-made after the grow:
/// growing can detach a previously constructed buffer view.
function readCutLayout(
  module: PuzzleModuleExports,
  ptr: number,
  type: PuzzleType,
  faceletCount: number,
  generatorCount: number,
): CutLayout {
  const wide = type === PuzzleType.Dodecahedral;
  const expectedStride = wide ? CUT_FACELET_WIDE_BYTES : CUT_FACELET_BYTES;
  // Wide coordinates are fixed-point: divide by the scale to get real
  // units, and by its square for a threshold (a dot product of two).
  const unit = wide ? DODECA_COORD_SCALE : 1;
  const stride = module.puzzle_facelet_geometry_stride(ptr);
  if (stride !== expectedStride) {
    throw new Error(`readCutLayout: core reports a ${stride}-byte facelet record, web/ expects ${expectedStride}`);
  }
  const scratchBytes = AXIS_INTS * 4 + faceletCount;
  const scratch = module.memory.buffer.byteLength;
  module.memory.grow(Math.max(1, Math.ceil(scratchBytes / WASM_PAGE_BYTES)));

  const records = new DataView(module.memory.buffer, module.puzzle_facelet_geometry(ptr), faceletCount * stride);
  const facelets: CutFacelet[] = [];
  for (let i = 0; i < faceletCount; i++) {
    const at = i * stride;
    const coord = (offset: number): number =>
      wide ? records.getInt16(at + offset * 2, true) / unit : records.getInt8(at + offset);
    const tail = wide ? 12 : 6;
    facelets.push({
      pos: [coord(0), coord(1), coord(2)],
      normal: [coord(3), coord(4), coord(5)],
      pieceType: records.getUint8(at + tail),
      slot: records.getUint8(at + tail + 1),
    });
  }

  const generators: CutGenerator[] = [];
  const movingPtr = scratch + AXIS_INTS * 4;
  for (let g = 0; g < generatorCount; g++) {
    if (module.puzzle_generator_axis(ptr, g, scratch) !== 1) {
      throw new Error(`readCutLayout: core has no axis for generator ${g}`);
    }
    const axisView = new Int32Array(module.memory.buffer, scratch, AXIS_INTS);
    const axis: [number, number, number] = [axisView[0]! / unit, axisView[1]! / unit, axisView[2]! / unit];
    const threshold = axisView[3]! / (unit * unit);
    const movingCount = module.puzzle_generator_facelets(ptr, g, movingPtr, faceletCount);
    if (movingCount > faceletCount) {
      throw new Error(`readCutLayout: generator ${g} moves ${movingCount} of ${faceletCount} facelets`);
    }
    const moving = Array.from(new Uint8Array(module.memory.buffer, movingPtr, movingCount));
    generators.push({ axis, threshold, moving });
  }
  return { facelets, generators, tolerance: wide ? 1 / DODECA_COORD_SCALE : 0 };
}
