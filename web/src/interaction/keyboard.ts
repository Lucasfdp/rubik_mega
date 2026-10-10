import { PuzzleType, type Move, type PuzzleLayout } from "../engine/types.js";
import { CUBIC_GEN_B, CUBIC_GEN_D, CUBIC_GEN_F, CUBIC_GEN_L, CUBIC_GEN_R, CUBIC_GEN_U } from "./move-axis.js";

/// What the keyboard does (docs/en/13-phase6-polish.md D6, D9, D11): one
/// pure function from a key press and a little context to a command, so
/// the whole map is tested without a DOM. ui/keyboard-controls.ts reads
/// the events, calls this, and carries the command out.

/// The slice of a KeyboardEvent this needs.
export interface KeyInput {
  key: string;
  shiftKey: boolean;
  ctrlKey: boolean;
  metaKey: boolean;
  altKey: boolean;
}

export interface KeyContext {
  layout: PuzzleLayout;
  /// A solution is loaded, so the arrow keys and space drive playback.
  hasSolution: boolean;
  /// The generator the number keys last picked on a non-cube, or null.
  selectedGenerator: number | null;
  /// The layer number the last digit typed on a cube picked (1 = the
  /// outer layer), or null for the outer layer.
  pendingLayer: number | null;
}

export type KeyCommand =
  | { kind: "turn"; move: Move }
  | { kind: "selectGenerator"; generator: number }
  | { kind: "selectLayer"; layer: number }
  | { kind: "turnSelected"; direction: 1 | -1 }
  | { kind: "clearSelection" }
  | { kind: "playPause" }
  | { kind: "stepForward" }
  | { kind: "stepBack" }
  | { kind: "undo" }
  | { kind: "redo" }
  | { kind: "help" }
  | { kind: "resetView" }
  | { kind: "orbit"; yawDeg: number; pitchDeg: number };

/// Degrees of camera orbit per arrow press (a held key repeats).
export const KEY_ORBIT_STEP_DEG = 5;

/// The cube's face keys, by letter, to core's face numbers.
const CUBE_FACE_KEYS: Readonly<Record<string, number>> = {
  u: CUBIC_GEN_U,
  r: CUBIC_GEN_R,
  f: CUBIC_GEN_F,
  d: CUBIC_GEN_D,
  l: CUBIC_GEN_L,
  b: CUBIC_GEN_B,
};

/// Keys that pick a generator on a non-cube, in order: 1-9, 0, then the
/// two next to it, so a Megaminx's twelve faces all have one.
export const GENERATOR_KEYS: readonly string[] = ["1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "="];

/// The opposite face of a cube face (U/D, R/L, F/B): the faces come in
/// pairs three apart (core's U R F D L B).
function oppositeFace(face: number): number {
  return (face + 3) % 6;
}

/// The move a cube key means: turn layer `layer` (1 = outermost) counted
/// from `face`, clockwise seen from that face, or counter-clockwise if
/// `prime`. A face's generators reach only the half of the layers nearest
/// it, so a layer in the far half is the same turn from the opposite face
/// with the sense reversed. Null if the cube has no such layer to turn (a
/// 3x3x3 has no middle-slice generator; layer 0 or past the size).
export function cubeKeyMove(layout: PuzzleLayout, face: number, layer: number, prime: boolean): Move | null {
  const n = layout.size;
  if (layer < 1 || layer > n) return null;
  const find = (f: number, depth: number): number => layout.generators.findIndex((g) => g.face === f && g.depth === depth);
  const near = find(face, layer - 1);
  if (near >= 0) return { generator: near, amount: prime ? 3 : 1 };
  const far = find(oppositeFace(face), n - layer);
  if (far >= 0) return { generator: far, amount: prime ? 1 : 3 };
  return null;
}

/// The number a digit key means (1-9), or null.
function digit(key: string): number | null {
  return /^[1-9]$/.test(key) ? Number(key) : null;
}

/// What a key press landed on, as far as deciding whether to ignore it.
export interface KeyTarget {
  tag: string;
  editable: boolean;
  role: string | null;
}

/// Whether the keymap must leave this key alone: the user is typing into a
/// field, a dialog is open (it has its own keys), or the key is the one
/// that activates the button or link focused (Space and Enter must press
/// it, not play a solution).
export function ignoresKey(target: KeyTarget, key: string, dialogOpen: boolean): boolean {
  if (dialogOpen) return true;
  const tag = target.tag.toUpperCase();
  if (target.editable || tag === "INPUT" || tag === "TEXTAREA" || tag === "SELECT") return true;
  if ((tag === "BUTTON" || tag === "A" || target.role === "button") && (key === " " || key === "Enter")) return true;
  return false;
}

/// Resolves a key press, or null if the key means nothing here (the
/// browser then keeps it).
export function interpretKey(input: KeyInput, context: KeyContext): KeyCommand | null {
  const key = input.key;
  const lower = key.toLowerCase();
  const command = input.ctrlKey || input.metaKey;

  if (command) {
    if (lower === "z") return { kind: input.shiftKey ? "redo" : "undo" };
    if (lower === "y") return { kind: "redo" };
    return null;
  }
  if (input.altKey) return null;

  if (key === "?") return { kind: "help" };
  if (key === "Escape") return context.selectedGenerator !== null || context.pendingLayer !== null ? { kind: "clearSelection" } : null;
  if (key === "Home") return { kind: "resetView" };
  if (key === " ") return context.hasSolution ? { kind: "playPause" } : null;

  if (key === "ArrowLeft" || key === "ArrowRight") {
    const direction = key === "ArrowRight" ? 1 : -1;
    if (context.hasSolution) return { kind: direction > 0 ? "stepForward" : "stepBack" };
    if (context.selectedGenerator !== null) return { kind: "turnSelected", direction };
    return { kind: "orbit", yawDeg: -direction * KEY_ORBIT_STEP_DEG, pitchDeg: 0 };
  }
  if (key === "ArrowUp" || key === "ArrowDown") {
    return { kind: "orbit", yawDeg: 0, pitchDeg: (key === "ArrowUp" ? 1 : -1) * KEY_ORBIT_STEP_DEG };
  }

  const { layout } = context;
  if (layout.type === PuzzleType.Cubic) {
    const layer = digit(key);
    if (layer !== null) return layer <= layout.size ? { kind: "selectLayer", layer } : null;
    const face = CUBE_FACE_KEYS[lower];
    if (face === undefined || key.length !== 1) return null;
    const move = cubeKeyMove(layout, face, context.pendingLayer ?? 1, input.shiftKey);
    return move ? { kind: "turn", move } : null;
  }
  if (layout.type === PuzzleType.Square1 && key === "/") {
    return { kind: "turn", move: { generator: SQUARE1_SLICE_GENERATOR, amount: 1 } };
  }
  const index = GENERATOR_KEYS.indexOf(key);
  if (index >= 0 && index < layout.generators.length) return { kind: "selectGenerator", generator: index };
  return null;
}

/// Square-1's slice generator (core's SQ1_GEN_SLICE), the one move it has
/// a key of its own for, as in the notation: "/".
const SQUARE1_SLICE_GENERATOR = 2;

/// The shortcuts the help sheet lists, as data so the sheet cannot drift
/// from the map above. `scope` says which puzzles it applies to.
export interface ShortcutHelp {
  keys: string;
  action: string;
  scope: "all" | "cube" | "other";
}

export const SHORTCUT_HELP: readonly ShortcutHelp[] = [
  { keys: "U R F D L B", action: "Turn that face clockwise (Shift: anticlockwise)", scope: "cube" },
  { keys: "2 – 8, then a face", action: "Turn that layer instead of the outer one", scope: "cube" },
  { keys: "1 – 9, 0, -, =", action: "Pick a face or corner, then ← → to turn it", scope: "other" },
  { keys: "/", action: "Square-1: the slice", scope: "other" },
  { keys: "← → ↑ ↓", action: "Turn the view (or step through a solution, or turn the picked face)", scope: "all" },
  { keys: "Space", action: "Play or pause a solution", scope: "all" },
  { keys: "Ctrl/⌘ Z", action: "Undo your last turn", scope: "all" },
  { keys: "Ctrl/⌘ Shift Z", action: "Redo", scope: "all" },
  { keys: "Home", action: "Reset the view", scope: "all" },
  { keys: "Esc", action: "Clear a picked face or layer", scope: "all" },
  { keys: "?", action: "Show this list", scope: "all" },
];
