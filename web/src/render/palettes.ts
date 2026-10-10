import { PuzzleType } from "../engine/types.js";
import { PALETTES } from "./look.js";

/// Alternative sticker palettes and the maths used to judge them
/// (docs/en/13-phase6-polish.md D11). Pure: no DOM, no three.js, so the
/// review ("which pair of colours collides for someone with deuteranopia")
/// is a unit test and the numbers are reproducible.

export type PaletteChoice = "classic" | "high-contrast";
export const PALETTE_CHOICES: readonly PaletteChoice[] = ["classic", "high-contrast"];

/// The high-contrast palettes. Found by searching a pool of 26 candidate
/// colours (the Okabe-Ito set, which was chosen to stay apart under the
/// common colour-vision deficiencies, plus the classic colours and a few
/// extras) for the set whose CLOSEST PAIR is furthest apart across normal
/// vision, protanopia, deuteranopia and tritanopia together. The cube's
/// closest pair goes from 13.6 (classic, tritanopia) to 29.8 (CIE76 ΔE);
/// twelve colours cannot be separated much further than 13, which is why
/// glyph mode exists. The review is a test (tests/render/palettes.test.ts)
/// and its numbers are in DECISIONS.md.
const CUBE_HIGH_CONTRAST = ["#ffffff", "#d55e00", "#0051ba", "#ffd500", "#8c564b", "#56b4e9"]; // U R F D L B

const HIGH_CONTRAST: Readonly<Partial<Record<PuzzleType, readonly string[]>>> = {
  [PuzzleType.Cubic]: CUBE_HIGH_CONTRAST,
  [PuzzleType.Skewb]: CUBE_HIGH_CONTRAST,
  [PuzzleType.Square1]: CUBE_HIGH_CONTRAST,
  [PuzzleType.Tetrahedral]: ["#2e8b57", "#6a3d9a", "#e69f00", "#ffffff"],
  // U F R BR BL L DR DBR DB DBL DL D
  [PuzzleType.Dodecahedral]: [
    "#ffffff", "#2e8b57", "#d55e00", "#0051ba", "#f0e442", "#cc79a7",
    "#7fd1ee", "#ff8fb1", "#b9f2a5", "#7a7a7a", "#c41e3a", "#8bc34a",
  ],
};

/// The palette for a family: one CSS colour per colour index core paints.
export function paletteFor(type: PuzzleType, choice: PaletteChoice): readonly string[] {
  const classic = PALETTES[type];
  if (!classic) throw new Error(`paletteFor: puzzle type ${PuzzleType[type] ?? type} has no palette`);
  return choice === "high-contrast" ? (HIGH_CONTRAST[type] ?? classic) : classic;
}

// ---- Colour maths ----

export type Rgb = readonly [number, number, number];

export function parseHex(hex: string): Rgb {
  const match = /^#([0-9a-f]{6})$/i.exec(hex);
  if (!match) throw new Error(`parseHex: "${hex}" is not a #rrggbb colour`);
  const value = parseInt(match[1]!, 16);
  return [(value >> 16) & 255, (value >> 8) & 255, value & 255];
}

const toLinear = (channel: number): number => {
  const c = channel / 255;
  return c <= 0.04045 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4;
};
const fromLinear = (linear: number): number => {
  const c = Math.min(Math.max(linear, 0), 1);
  return 255 * (c <= 0.0031308 ? c * 12.92 : 1.055 * c ** (1 / 2.4) - 0.055);
};

export type Deficiency = "normal" | "protanopia" | "deuteranopia" | "tritanopia";
export const DEFICIENCIES: readonly Deficiency[] = ["normal", "protanopia", "deuteranopia", "tritanopia"];

/// Machado, Oliveira and Fernandes (2009), severity 1.0, applied to linear
/// RGB: the standard physiologically based simulation of dichromacy.
const CVD_MATRICES: Readonly<Record<Exclude<Deficiency, "normal">, readonly number[]>> = {
  protanopia: [0.152286, 1.052583, -0.204868, 0.114503, 0.786281, 0.099216, -0.003882, -0.048116, 1.051998],
  deuteranopia: [0.367322, 0.860646, -0.227968, 0.280085, 0.672501, 0.047413, -0.01182, 0.04294, 0.968881],
  tritanopia: [1.255528, -0.076749, -0.178779, -0.078411, 0.930809, 0.147602, 0.004733, 0.691367, 0.3039],
};

/// How a colour looks to someone with the given deficiency.
export function simulate(colour: Rgb, deficiency: Deficiency): Rgb {
  if (deficiency === "normal") return colour;
  const m = CVD_MATRICES[deficiency];
  const [r, g, b] = [toLinear(colour[0]), toLinear(colour[1]), toLinear(colour[2])];
  return [
    fromLinear(m[0]! * r + m[1]! * g + m[2]! * b),
    fromLinear(m[3]! * r + m[4]! * g + m[5]! * b),
    fromLinear(m[6]! * r + m[7]! * g + m[8]! * b),
  ];
}

/// CIE L*a*b* (D65), the space in which a distance roughly matches how
/// different two colours look.
export function toLab(colour: Rgb): readonly [number, number, number] {
  const [r, g, b] = [toLinear(colour[0]), toLinear(colour[1]), toLinear(colour[2])];
  const x = (0.4124564 * r + 0.3575761 * g + 0.1804375 * b) / 0.95047;
  const y = 0.2126729 * r + 0.7151522 * g + 0.072175 * b;
  const z = (0.0193339 * r + 0.119192 * g + 0.9503041 * b) / 1.08883;
  const f = (t: number): number => (t > 216 / 24389 ? Math.cbrt(t) : (24389 / 27 * t + 16) / 116);
  const [fx, fy, fz] = [f(x), f(y), f(z)];
  return [116 * fy - 16, 500 * (fx - fy), 200 * (fy - fz)];
}

/// Distance between two colours (CIE76 ΔE: plain Euclidean in Lab). Rough,
/// but enough to rank palettes; about 2.3 is a just-noticeable difference.
export function deltaE(a: Rgb, b: Rgb): number {
  const [l1, a1, b1] = toLab(a);
  const [l2, a2, b2] = toLab(b);
  return Math.hypot(l1 - l2, a1 - a2, b1 - b2);
}

export interface PaletteReview {
  /// The smallest distance between any two colours, as seen with this
  /// deficiency, and which two they are.
  minDeltaE: number;
  closest: readonly [number, number];
}

/// The closest pair in `palette` as seen with `deficiency`.
export function reviewPalette(palette: readonly string[], deficiency: Deficiency): PaletteReview {
  const seen = palette.map((hex) => simulate(parseHex(hex), deficiency));
  let best: PaletteReview = { minDeltaE: Infinity, closest: [0, 0] };
  for (let i = 0; i < seen.length; i++) {
    for (let j = i + 1; j < seen.length; j++) {
      const d = deltaE(seen[i]!, seen[j]!);
      if (d < best.minDeltaE) best = { minDeltaE: d, closest: [i, j] };
    }
  }
  return best;
}

// ---- Sticker glyphs ----

/// What is drawn on a sticker in glyph mode, per colour index. A cube, a
/// Skewb and a Square-1 name the face the colour belongs to (core's U R F
/// D L B order); a Pyraminx uses shapes, since it has no letters to
/// borrow; a Megaminx or Kilominx uses its face names.
const CUBE_GLYPHS = ["U", "R", "F", "D", "L", "B"];
const PYRAMINX_GLYPHS = ["●", "■", "▲", "◆"];
const DODECA_GLYPHS = ["U", "F", "R", "BR", "BL", "L", "DR", "DBR", "DB", "DBL", "DL", "D"];

export function glyphsFor(type: PuzzleType): readonly string[] {
  switch (type) {
    case PuzzleType.Tetrahedral:
      return PYRAMINX_GLYPHS;
    case PuzzleType.Dodecahedral:
      return DODECA_GLYPHS;
    default:
      return CUBE_GLYPHS;
  }
}

/// Black or white, whichever reads better on `hex`: the colour of a glyph
/// drawn on a sticker of that colour (WCAG relative luminance, so a
/// yellow sticker gets black ink and a blue one white).
export function readableInk(hex: string): "#000000" | "#ffffff" {
  const [r, g, b] = parseHex(hex).map(toLinear) as [number, number, number];
  const luminance = 0.2126 * r + 0.7152 * g + 0.0722 * b;
  // Contrast against white is 1.05 / (L + 0.05), against black (L + 0.05) / 0.05.
  return (luminance + 0.05) / 0.05 > 1.05 / (luminance + 0.05) ? "#000000" : "#ffffff";
}
