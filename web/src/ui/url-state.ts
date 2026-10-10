import type { PuzzleType } from "../engine/types.js";
import { puzzleFromKey, puzzleKey, scrambleMovesForPuzzle } from "./puzzle-sizes.js";

/// The address bar as a shareable link (docs/en/13-phase6-polish.md D10):
/// `?p=megaminx`, `?p=cube&n=5`, and `&s=<seed>&m=<moves>` for a shared
/// scramble. Pure string in, string out, so it is tested without a window.
///
/// The URL wins over stored settings. Parameters this module does not own
/// (`debug`) are carried through untouched.

/// What a link asks for. Each part is null when the link does not say, or
/// says something that makes no sense (it is ignored, never an error: a
/// link someone mistyped should still open the app).
export interface LinkRequest {
  puzzle: { type: PuzzleType; size: number } | null;
  /// A scramble to replay on opening, only ever with a puzzle.
  scramble: { seed: number; moves: number } | null;
}

const MAX_SEED = 0xffffffff;
const MAX_MOVES = 500;

function wholeNumber(text: string | null, max: number): number | null {
  if (text === null || !/^\d{1,10}$/.test(text)) return null;
  const value = Number(text);
  return value <= max ? value : null;
}

/// Reads a link's query string (with or without the leading "?").
export function parseLink(search: string): LinkRequest {
  const params = new URLSearchParams(search);
  const slug = params.get("p");
  if (slug === null) return { puzzle: null, scramble: null };
  const size = params.get("n");
  const puzzle = puzzleFromKey(size === null ? slug : `${slug}:${size}`) ?? puzzleFromKey(slug);
  if (!puzzle) return { puzzle: null, scramble: null };
  const seed = wholeNumber(params.get("s"), MAX_SEED);
  if (seed === null) return { puzzle, scramble: null };
  const moves = wholeNumber(params.get("m"), MAX_MOVES) || scrambleMovesForPuzzle(puzzle.type, puzzle.size);
  return { puzzle, scramble: { seed, moves } };
}

/// The query string (with its "?") that names `puzzle`, and a scramble if
/// given, keeping every parameter of `search` that is not ours.
export function buildLink(
  search: string,
  puzzle: { type: PuzzleType; size: number },
  scramble: { seed: number; moves: number } | null,
): string {
  const params = new URLSearchParams(search);
  for (const own of ["p", "n", "s", "m"]) params.delete(own);
  const [slug, size] = puzzleKey(puzzle.type, puzzle.size).split(":") as [string, string | undefined];
  params.set("p", slug);
  if (size !== undefined) params.set("n", size);
  if (scramble) {
    params.set("s", String(scramble.seed));
    params.set("m", String(scramble.moves));
  }
  const text = params.toString();
  return text ? `?${text}` : "";
}
