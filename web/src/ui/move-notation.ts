import { PuzzleType, type GeneratorLayer, type Move, type PuzzleLayout } from "../engine/types.js";
import { CUBIC_GEN_U, CUBIC_GEN_R, CUBIC_GEN_F, CUBIC_GEN_D, CUBIC_GEN_L, CUBIC_GEN_B } from "../interaction/move-axis.js";

/// Face letter for each of core's 6 cubic faces, U R F D L B order --
/// mirrors interaction/move-axis.ts's CUBIC_GEN_* constants (which
/// mirror core/include/puzzles/cubic.h). Built by index rather than
/// written as a literal so a future reordering fails loudly here
/// instead of silently mislabelling a move.
const FACE_LETTERS: readonly string[] = (() => {
  const letters: string[] = [];
  letters[CUBIC_GEN_U] = "U";
  letters[CUBIC_GEN_R] = "R";
  letters[CUBIC_GEN_F] = "F";
  letters[CUBIC_GEN_D] = "D";
  letters[CUBIC_GEN_L] = "L";
  letters[CUBIC_GEN_B] = "B";
  return letters;
})();

/// WCA notation suffix for each turn amount -- amount 1 (90 degrees
/// clockwise, core/include/puzzle.h's own convention) is the bare
/// letter, amount 2 is a double turn, amount 3 is the prime
/// (counter-clockwise) form. No other amount is valid for a cubic
/// generator (steps=3).
function suffixForAmount(amount: number): string {
  if (amount === 1) return "";
  if (amount === 2) return "2";
  if (amount === 3) return "'";
  throw new Error(`formatMove: amount ${amount} is out of range (1-3) for a cubic generator`);
}

/// Renders one Move in WCA-style notation, given the puzzle's own
/// generator table (PuzzleLayout.generators, read from core): an outer
/// layer is the bare face letter ("R'"), an inner single layer gets its
/// layer number in front, counting the face itself as 1 ("2R", "3R2"
/// -- docs/en/08-nxn-reduction.md D6). One rule for every size: an
/// odd-n middle layer is "3R" on a 5x5x5, not "M".
export function formatMove(move: Move, generators: readonly GeneratorLayer[]): string {
  const layer = generators[move.generator];
  const letter = layer ? FACE_LETTERS[layer.face] : undefined;
  if (!layer || !letter) {
    throw new Error(`formatMove: generator ${move.generator} is out of range (0-${generators.length - 1})`);
  }
  const prefix = layer.depth === 0 ? "" : String(layer.depth + 1);
  return prefix + letter + suffixForAmount(move.amount);
}

/// Renders a full move sequence as a space-separated string, e.g.
/// "R U R' U'" -- could double as a copy-to-clipboard string, though
/// nothing wires that up yet.
export function formatMoveSequence(moves: readonly Move[], generators: readonly GeneratorLayer[]): string {
  return moves.map((move) => formatMove(move, generators)).join(" ");
}

/// Pyraminx vertex letters, by vertex number (generator / 2): U is the
/// vertex held upward, then L, R, B -- WCA's names for the four
/// vertex layers. Which physical vertex is "L" is a labelling choice
/// (nothing on screen shows the letters), but it is fixed.
const PYRAMINX_VERTEX_LETTERS = ["U", "L", "R", "B"] as const;

/// Skewb corner names by core's corner number: bit 0 set = left (else
/// right), bit 1 set = down (else up), bit 2 set = back (else front)
/// -- core/include/puzzles/skewb.h's SKEWB_CORNER_*_BIT. Three letters,
/// up/down first, because a Skewb has 8 turnable corners and the WCA's
/// four letters only name half of them.
const SKEWB_CORNER_X_BIT = 1;
const SKEWB_CORNER_Y_BIT = 2;
const SKEWB_CORNER_Z_BIT = 4;

function skewbCornerName(corner: number): string {
  return (
    (corner & SKEWB_CORNER_Y_BIT ? "D" : "U") +
    (corner & SKEWB_CORNER_X_BIT ? "L" : "R") +
    (corner & SKEWB_CORNER_Z_BIT ? "B" : "F")
  );
}

/// A 120-degree turn has two amounts: 1 is clockwise (seen from the
/// vertex), 2 is two of those, which is one counter-clockwise -- the
/// prime.
function cutSuffix(amount: number): string {
  if (amount === 1) return "";
  if (amount === 2) return "'";
  throw new Error(`formatMove: amount ${amount} is out of range (1-2) for a cut-plane generator`);
}

/// Megaminx and Kilominx face names by core's face number
/// (core/src/puzzles/dodecahedral.c's FACE_NAMES, which
/// core/include/puzzles/dodecahedral.h documents): U on top, then the
/// upper ring front to back-left (F, R, BR, BL, L), the lower ring
/// (DR, DBR, DB, DBL, DL) and D. Core numbers the generators by face, so
/// a generator's number is its face's.
const DODECA_FACE_NAMES: readonly string[] = ["U", "F", "R", "BR", "BL", "L", "DR", "DBR", "DB", "DBL", "DL", "D"];

/// A 72-degree face turn has four amounts: 1 is one step clockwise seen
/// from outside, 2 two (the double), 3 is two steps back (the double's
/// prime), 4 one step back (the prime).
const DODECA_SUFFIXES: readonly string[] = ["", "2", "2'", "'"];

function dodecaSuffix(amount: number): string {
  const suffix = DODECA_SUFFIXES[amount - 1];
  if (!Number.isInteger(amount) || suffix === undefined) {
    throw new Error(`formatMove: amount ${amount} is out of range (1-${DODECA_SUFFIXES.length}) for a dodecahedral generator`);
  }
  return suffix;
}

/// Square-1 (docs/en/12-phase5-square1.md D7). WCA regulation 12c2 writes
/// a turn as "(X, Y)": the upper layer X times 30 degrees clockwise, the
/// bottom layer Y times 30 degrees clockwise, then "/" for the slice, X
/// and Y in -5..6. The regulation does not say which side the bottom
/// layer is viewed from. UNVERIFIED: this takes it as core's own
/// generator does, clockwise seen from below. If a reference scrambler's
/// picture of a known scramble shows the opposite, flip this one switch.
export const SQ1_BOTTOM_CLOCKWISE_FROM_BELOW = true;

/// A layer amount (1..11 units clockwise) as WCA writes it: -5..6.
function squareOneUnits(amount: number): number {
  if (!Number.isInteger(amount) || amount < 1 || amount > 11) {
    throw new Error(`formatMove: amount ${amount} is out of range (1-11) for a Square-1 layer`);
  }
  return amount <= 6 ? amount : amount - 12;
}

/// One Square-1 move on its own: "(3,0)" for the top, "(0,-2)" for the
/// bottom, "/" for the slice.
function formatSquareOneMove(move: Move): string {
  if (move.generator === 2) return "/";
  const units = squareOneUnits(move.amount);
  if (move.generator === 0) return `(${units},0)`;
  if (move.generator === 1) return `(0,${SQ1_BOTTOM_CLOCKWISE_FROM_BELOW ? units : -units})`;
  throw new Error(`formatMove: generator ${move.generator} is out of range (0-2)`);
}

/// A whole Square-1 sequence the way it is written down: consecutive
/// top and bottom turns fold into one "(X,Y)" (the turns commute), then
/// each slice is "/". A group whose turns cancel to (0,0) is dropped.
export function formatSquareOneSequence(moves: readonly Move[]): string {
  const parts: string[] = [];
  let top = 0;
  let bottom = 0;
  const flush = (): void => {
    top = ((top % 12) + 12) % 12;
    bottom = ((bottom % 12) + 12) % 12;
    if (top !== 0 || bottom !== 0) {
      const x = top <= 6 ? top : top - 12;
      const y = bottom <= 6 ? bottom : bottom - 12;
      parts.push(`(${x},${SQ1_BOTTOM_CLOCKWISE_FROM_BELOW ? y : -y})`);
    }
    top = 0;
    bottom = 0;
  };
  for (const move of moves) {
    if (move.generator === 2) {
      flush();
      parts.push("/");
    } else if (move.generator === 0) {
      top += move.amount;
    } else if (move.generator === 1) {
      bottom += move.amount;
    } else {
      throw new Error(`formatMove: generator ${move.generator} is out of range (0-2)`);
    }
  }
  flush();
  return parts.join(" ");
}

/// One move in any family's notation: WCA for a cube, WCA-style vertex
/// layers for a Pyraminx (`U`/`L`/`R`/`B`, lower case for the tip
/// alone -- core's depth 0), corner names for a Skewb, face names for a
/// Megaminx or Kilominx.
export function formatMoveFor(move: Move, layout: PuzzleLayout): string {
  if (layout.type === PuzzleType.Cubic) return formatMove(move, layout.generators);
  if (move.generator < 0 || move.generator >= layout.generators.length) {
    throw new Error(`formatMove: generator ${move.generator} is out of range (0-${layout.generators.length - 1})`);
  }
  if (layout.type === PuzzleType.Skewb) return skewbCornerName(move.generator) + cutSuffix(move.amount);
  if (layout.type === PuzzleType.Tetrahedral) {
    const letter = PYRAMINX_VERTEX_LETTERS[move.generator >> 1]!;
    const tipOnly = (move.generator & 1) === 0; // core: depth 0 = the tip alone
    return (tipOnly ? letter.toLowerCase() : letter) + cutSuffix(move.amount);
  }
  if (layout.type === PuzzleType.Square1) return formatSquareOneMove(move);
  if (layout.type === PuzzleType.Dodecahedral) return DODECA_FACE_NAMES[move.generator]! + dodecaSuffix(move.amount);
  throw new Error(`formatMove: no notation for puzzle type ${layout.type}`);
}

/// A whole solution as one line of notation, the text the Copy button
/// puts on the clipboard. Square-1 is written the way its community
/// writes it, with turns folded into (top,bottom) pairs between slashes;
/// every other family is its moves separated by spaces.
export function solutionText(moves: readonly Move[], layout: PuzzleLayout): string {
  if (layout.type === PuzzleType.Square1) return formatSquareOneSequence(moves);
  return moves.map((move) => formatMoveFor(move, layout)).join(" ");
}
