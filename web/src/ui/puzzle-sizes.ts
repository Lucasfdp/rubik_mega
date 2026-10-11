import { PuzzleType, SQ1_SCRAMBLE_SLICES } from "../engine/types.js";
import type { GlyphKind } from "./shell/icons.js";

/// Cube sizes the size picker offers (docs/en/07-nxn-web-view.md D5):
/// 2 up to 8, the largest size core/ can build (a 9x9x9's 294 centres
/// overflow the state buffer's one-byte-per-piece limit --
/// core/include/puzzles/cubic.h's CUBIC_CACHE_MAX comment).
export const SUPPORTED_SIZES: readonly number[] = [2, 3, 4, 5, 6, 7, 8];

/// The size the app opens on -- the one puzzle with solvers today.
export const DEFAULT_SIZE = 3;

/// Random moves per Scramble, by size (D4): the WCA's own random-move
/// scramble lengths for 4x4x4 to 7x7x7, 3x3x3 unchanged from before
/// this table existed, 8x8x8 extrapolated (+20 per size, the same step
/// as the WCA's 5x5x5-to-7x7x7 lengths; there is no WCA 8x8x8 event).
const SCRAMBLE_MOVES_BY_SIZE: Readonly<Record<number, number>> = {
  // 2x2x2: its whole state space is 3.7 million positions, so 15 random
  // moves (the WCA's own scrambles are 9-11 from a random state) mix it fully.
  2: 15,
  3: 25,
  4: 40,
  5: 60,
  6: 80,
  7: 100,
  8: 120,
};

/// How many random moves a Scramble applies on a size-n cube.
export function scrambleMovesFor(size: number): number {
  const moves = SCRAMBLE_MOVES_BY_SIZE[size];
  if (moves === undefined) throw new Error(`scrambleMovesFor: unsupported size ${size}`);
  return moves;
}

/// The puzzles the picker offers, in order. `hasSize` says whether the
/// size picker applies (only the cube has one); otherwise `size` is what
/// core is asked to build -- 0 where core ignores it, and 3 or 2 for
/// Megaminx and Kilominx, which are two sizes of one family
/// (PuzzleType.Dodecahedral), so a type alone does not name the puzzle.
export interface PuzzleChoice {
  type: PuzzleType;
  label: string;
  hasSize: boolean;
  size: number;
  /// Which picture the picker shows for it.
  glyph: GlyphKind;
  /// What the address bar calls it (`?p=megaminx`): stable, so a shared
  /// link keeps working.
  slug: string;
}

export const PUZZLE_CHOICES: readonly PuzzleChoice[] = [
  { type: PuzzleType.Cubic, label: "Cube", hasSize: true, size: DEFAULT_SIZE, glyph: "cube", slug: "cube" },
  { type: PuzzleType.Skewb, label: "Skewb", hasSize: false, size: 0, glyph: "skewb", slug: "skewb" },
  { type: PuzzleType.Tetrahedral, label: "Pyraminx", hasSize: false, size: 0, glyph: "pyraminx", slug: "pyraminx" },
  { type: PuzzleType.Dodecahedral, label: "Megaminx", hasSize: false, size: 3, glyph: "megaminx", slug: "megaminx" },
  { type: PuzzleType.Dodecahedral, label: "Kilominx", hasSize: false, size: 2, glyph: "kilominx", slug: "kilominx" },
  { type: PuzzleType.Square1, label: "Square-1", hasSize: false, size: 0, glyph: "square1", slug: "square1" },
];

/// What the top bar calls the puzzle on screen: a cube says its size
/// ("Cube 4x4x4"), every other puzzle just its name.
export function puzzleTitle(type: PuzzleType, size: number): string {
  const choice = PUZZLE_CHOICES[choiceIndexFor(type, size)]!;
  return choice.hasSize ? `${choice.label} ${size}\u00d7${size}\u00d7${size}` : choice.label;
}

/// Index in PUZZLE_CHOICES of the entry showing a puzzle of this type
/// and size (the cube's sizes all share one entry), or 0 if none does.
export function choiceIndexFor(type: PuzzleType, size: number): number {
  const exact = PUZZLE_CHOICES.findIndex((c) => c.type === type && (c.hasSize || c.size === size));
  if (exact >= 0) return exact;
  return Math.max(0, PUZZLE_CHOICES.findIndex((c) => c.type === type));
}

/// Random moves per Scramble for Skewb and Pyraminx: well past enough to
/// mix them (their solutions are at most 15 moves), and the WCA's own
/// scrambles for both are in this range.
const CUT_PLANE_SCRAMBLE_MOVES = 25;

/// Random moves per Scramble for the dodecahedral puzzles. A Megaminx
/// has 12 faces x 4 amounts to choose from, so it takes far more moves
/// to be thoroughly mixed than a cube: the WCA's own scrambles are 77
/// moves (7 lines of 10 face turns plus U-layer turns); this is a plain
/// random-move scramble of that order. A Kilominx's corners mix in about
/// half that.
const MEGAMINX_SCRAMBLE_MOVES = 70;
const KILOMINX_SCRAMBLE_MOVES = 40;

/// How many random moves a Scramble applies to a puzzle of this type
/// and size. For Square-1 the count is SLICES (core's scramble precedes
/// each by the layer turns that make it legal; D9).
export function scrambleMovesForPuzzle(type: PuzzleType, size: number): number {
  if (type === PuzzleType.Cubic) return scrambleMovesFor(size);
  if (type === PuzzleType.Square1) return SQ1_SCRAMBLE_SLICES;
  if (type === PuzzleType.Dodecahedral) return size === 2 ? KILOMINX_SCRAMBLE_MOVES : MEGAMINX_SCRAMBLE_MOVES;
  return CUT_PLANE_SCRAMBLE_MOVES;
}

/// A puzzle as a short stable string: "megaminx", "cube:4". The address
/// bar and the saved settings both name a puzzle this way.
export function puzzleKey(type: PuzzleType, size: number): string {
  const choice = PUZZLE_CHOICES[choiceIndexFor(type, size)]!;
  return choice.hasSize ? `${choice.slug}:${size}` : choice.slug;
}

/// The puzzle a key names, or null if it names none (a hand-edited or
/// stale value; a cube size core cannot build is also none).
export function puzzleFromKey(key: string): { type: PuzzleType; size: number } | null {
  const [slug, sizeText, ...rest] = key.split(":");
  const choice = PUZZLE_CHOICES.find((c) => c.slug === slug);
  if (!choice || rest.length > 0) return null;
  if (!choice.hasSize) return sizeText === undefined ? { type: choice.type, size: choice.size } : null;
  const size = sizeText === undefined ? DEFAULT_SIZE : Number(sizeText);
  return SUPPORTED_SIZES.includes(size) ? { type: choice.type, size } : null;
}
