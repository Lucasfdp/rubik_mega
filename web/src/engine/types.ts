/// Mirrors core/include/puzzle.h's `t_puzzle_type` enum exactly -- the
/// numeric values cross the WASM boundary as plain integers, so this
/// must stay in lockstep with the C side by hand (see
/// docs/en/03-architecture.md S3). Does not include PUZZLE_TYPE_COUNT:
/// that's a C-side array-sizing constant, not a puzzle you can create.
export enum PuzzleType {
  Cubic = 0,
  Skewb = 1,
  Dodecahedral = 2,
  Tetrahedral = 3,
  Square1 = 4,
}

/// One applied move -- mirrors core/include/move.h's `t_move`.
/// `generator` indexes the puzzle's own generator table; `amount` is a
/// signed step count in that puzzle's own unit (move.h has the detail).
/// Not yet consumed anywhere -- recorded here because `puzzle_solve`'s
/// `out` buffer (core/include/puzzle.h) is an array of these.
export interface Move {
  generator: number;
  amount: number;
}


/// Byte size of one core/include/move.h `t_move` as laid out in WASM
/// memory: `{int16_t generator; int8_t amount;}` naturally aligns to 4
/// bytes (2-byte field, then a 1-byte field, then 1 byte of padding so
/// the next element's int16_t stays 2-byte aligned) -- guarded on the C
/// side by move.h's `_Static_assert(sizeof(t_move) == 4, ...)`, so a
/// struct change there fails that build loudly instead of silently
/// desyncing with this constant.
export const MOVE_STRUCT_BYTES = 4;

/// One completed solve, decoded from `puzzle_solve`'s raw WASM output
/// buffers -- core/include/puzzle.h's `out`/`out_phase_tags` arrays,
/// turned into plain arrays Puzzle.solve()'s caller can use without
/// touching WASM memory directly.
export interface SolveResult {
  /// moves[i] is the move applied at step i; phaseTags[i] is that same
  /// step's 0-indexed phase/stage number (core/src/solve/registry.c's
  /// per-algorithm adapters: 0 for every move on a single-phase
  /// algorithm like 2x2x2, 0..3 for Thistlethwaite/layer-by-layer).
  /// Always the same length -- one tag per move, never a separate count.
  moves: Move[];
  phaseTags: number[];
}

/// Byte stride of one facelet record in `puzzle_facelet_geometry`'s
/// buffer -- core/include/puzzles/cubic_facelets.h's t_cubic_facelet
/// ({x, y, z, face}, one byte each), guarded on the C side by a
/// `_Static_assert(sizeof(t_cubic_facelet) == 4, ...)`.
export const FACELET_GEOMETRY_BYTES = 4;

/// One visible sticker position: the lattice cell it sits on (each
/// coordinate 0..size-1; x = R, y = U, z = F at the high end) and the
/// face it points out of, in core's own face order (0..5 = U R F D L B
/// -- core/include/puzzles/cubic.h's CUBIC_GEN_*).
export interface FaceletGeometry {
  x: number;
  y: number;
  z: number;
  face: number;
}

/// Which (face, depth) one generator turns -- core's
/// `puzzle_generator_face`/`puzzle_generator_depth`, read once per
/// puzzle so web/ never re-derives core's generator numbering.
export interface GeneratorLayer {
  face: number;
  depth: number;
}

/// Bytes per facelet record for the cut-plane families (Skewb,
/// Pyraminx) -- core/include/cut_geometry.h's t_cut_facelet: signed
/// pos[3], signed normal[3], then piece type and slot, guarded on the C
/// side by a `_Static_assert(sizeof(t_cut_facelet) == 8, ...)`. Core
/// reports the stride per puzzle (puzzle_facelet_geometry_stride); this
/// is the value web/ expects it to be.
export const CUT_FACELET_BYTES = 8;

/// Square-1 (docs/en/12-phase5-square1.md): generator numbers, amounts
/// and unit kinds, mirroring core/include/puzzles/square1.h.
export const SQ1_GEN_TOP = 0;
export const SQ1_GEN_BOTTOM = 1;
export const SQ1_GEN_SLICE = 2;
export const SQ1_LAYER_STEPS = 11;
export const SQ1_LAYER_UNITS = 12;
export const SQ1_UNIT_COUNT = 24;
export const SQ1_UNIT_EDGE = 0;
export const SQ1_UNIT_CORNER_FIRST = 1;
export const SQ1_UNIT_CORNER_SECOND = 2;
/// Index of the middle-layer byte in the 25-byte state (0 as built, 1
/// flipped by an odd number of slices).
export const SQ1_MIDDLE_INDEX = 24;
export const SQ1_CAP_FACELETS = 0;
export const SQ1_SIDE_FACELETS = 24;
export const SQ1_MIDDLE_FACELETS = 48;
export const SQ1_FACELET_COUNT = 54;
/// Random slices per Square-1 Scramble (core's SQ1_SCRAMBLE_SLICES).
export const SQ1_SCRAMBLE_SLICES = 25;

/// Quarter-step counts per generator (core's t_generator_table.steps):
/// 3 for a cubic face (90/180/270 degrees), 2 for a 120-degree cut-plane
/// turn. A move's amount is in [1, steps]; the inverse of amount `a` is
/// `steps + 1 - a`.
export const CUBIC_STEPS = 3;
export const CUT_STEPS = 2;
/// A Megaminx or Kilominx face turn is 72 degrees: four amounts, five
/// to a full turn (core's DODECA_STEPS).
export const DODECA_STEPS = 4;

/// Bytes per facelet record for the dodecahedral families
/// (t_cut_facelet_wide: the same fields as the 8-byte record with signed
/// 16-bit coordinates at scale DODECA_COORD_SCALE).
export const CUT_FACELET_WIDE_BYTES = 14;

/// Fixed-point scale of a wide record's positions and normals, and of a
/// dodecahedral generator's axis (core's DODECA_COORD_SCALE). A
/// threshold is a dot product of two such values, so it is at the
/// scale's square.
export const DODECA_COORD_SCALE = 4096;

/// How many amounts a cut-plane generator of `type` has: 4 for the
/// dodecahedral puzzles (72-degree steps), 2 for the 120-degree ones.
export function cutStepsFor(type: PuzzleType): number {
  return type === PuzzleType.Dodecahedral ? DODECA_STEPS : CUT_STEPS;
}

/// One sticker of a cut-plane puzzle, in the puzzle's own integer
/// coordinates (Skewb: scale 3; Pyraminx: barycentric ninths in cube
/// coordinates) -- the one geometry core owns and web/ only draws.
export interface CutFacelet {
  pos: [number, number, number];
  /// Outward direction of the face it lies on (unit for Skewb, squared
  /// length 3 for Pyraminx).
  normal: [number, number, number];
  pieceType: number;
  slot: number;
}

/// One cut-plane generator: it turns every facelet with
/// dot(axis, pos) > threshold by 120 degrees times the move's amount,
/// clockwise seen from the axis tip (core's puzzle_generator_axis and
/// puzzle_generator_facelets).
export interface CutGenerator {
  axis: [number, number, number];
  threshold: number;
  /// Indices of the facelets this generator moves, ascending.
  moving: number[];
}

export interface CutLayout {
  facelets: CutFacelet[];
  generators: CutGenerator[];
  /// The most any coordinate here can differ from its exact value, in
  /// the layout's own units: 0 for Skewb and Pyraminx (integers, exact),
  /// one fixed-point step (1 / DODECA_COORD_SCALE) for the dodecahedral
  /// families, whose coordinates are irrational and were rounded. Every
  /// geometric comparison in render/ and interaction/ widens by it
  /// instead of assuming exactness.
  tolerance: number;
}

/// Everything about a puzzle that is fixed for its lifetime, read once
/// at creation (docs/en/07-nxn-web-view.md): its type and size, where
/// every facelet sits (parallel to PuzzleHandle.faceletView()'s
/// colours), and what every generator turns (index = generator number).
/// Cubic puzzles fill `facelets` and `generators` (face/depth) and leave
/// `cut` null; Skewb and Pyraminx do the reverse, with empty
/// `facelets` and `generators` entries of face/depth -1
/// (docs/en/09-phase3-skewb-pyraminx.md D4, D5).
export interface PuzzleLayout {
  type: PuzzleType;
  size: number;
  /// How many amounts a generator has (CUBIC_STEPS or CUT_STEPS); for
  /// Square-1, whose generators differ, the largest of them.
  steps: number;
  /// Amounts per generator (core's puzzle_generator_steps), index =
  /// generator number. Every entry equals `steps` except for Square-1
  /// (11, 11 and 1: a layer has 11 amounts, the slice one).
  generatorSteps: readonly number[];
  /// Length of faceletView(), for every family.
  faceletCount: number;
  facelets: FaceletGeometry[];
  generators: GeneratorLayer[];
  cut: CutLayout | null;
}
