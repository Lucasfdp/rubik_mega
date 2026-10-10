import { NeutralToneMapping, DoubleSide, FrontSide, MeshStandardMaterial, type Side, type Texture } from "three";
import { PuzzleType } from "../engine/types.js";
import { CORE_FACE_COLOUR } from "./lattice/cubic.js";
import type { TierSettings } from "./quality.js";
import type { OutlineOptions } from "./sticker-outline.js";

/// The one place every visual setting that is not geometry lives
/// (docs/en/13-phase6-polish.md D4): body colour, surface finish, the
/// sticker and bevel proportions, the palettes, lighting and tone
/// mapping. Before Phase 6 `BODY_COLOUR = 0x111111` was copied into three
/// scene files; now a scene imports it from here, and changing the look
/// of every puzzle is an edit to this file alone.
///
/// Proportions are ratios of something already in the scene (the body,
/// a sticker's inradius), never absolute world units, so they hold at
/// every cube size and for every family.

export const LOOK = {
  /// Plastic body between and under the stickers.
  bodyColour: 0x111111,
  bodyRoughness: 0.6,
  /// A touch glossier than the body so stickers catch a highlight.
  stickerRoughness: 0.35,

  /// How far each sticker edge moves in from its piece's edge, and how
  /// far from each corner the rounding starts, as fractions of the
  /// sticker's inradius (render/sticker-outline.ts). 0.14 reproduces the
  /// 0.86 shrink the cut-plane and Square-1 scenes always used.
  stickerInsetRatio: 0.14,
  stickerCornerRatio: 0.3,

  /// Corner radius of a rounded cubic body, as a fraction of its edge.
  bodyBevelRatio: 0.12,
  /// A cubic sticker's thickness and its gap above the body surface (so
  /// the two never z-fight), as fractions of the body's edge. The old
  /// 3x3x3 values (0.02 and 0.001 world units on a 0.94 body).
  stickerThicknessRatio: 0.02 / 0.94,
  stickerGapRatio: 0.001 / 0.94,
  /// How far a cut-plane or Square-1 sticker sits off its body face, in
  /// world units (those scenes are built at a fixed world size).
  stickerLift: 0.006,

  /// Tone mapping stops bright stickers clipping to flat white. AgX was
  /// tried first and measurably greyed the sticker colours on the
  /// screenshot sheet (spec D5); the Khronos PBR Neutral curve keeps
  /// hues true, which matters for a colour-matching puzzle.
  toneMapping: NeutralToneMapping,
  toneMappingExposure: 0.9,

  /// Strength of the room environment's light, and of the one key light
  /// that gives the puzzle its shape. Without an environment (the `low`
  /// tier) an ambient light stands in for it.
  environmentIntensity: 0.75,
  keyLightIntensity: 1.6,
  ambientIntensity: 0.6,
  keyLightPosition: [4, 6, 5] as const,

  /// Contact shadow: how dark its centre is, how far below the puzzle's
  /// bounding sphere it sits and how wide it is, as multiples of that
  /// sphere's radius.
  shadowOpacity: 0.35,
  shadowDrop: 1.12,
  shadowSpread: 2.6,
} as const;

/// What the tier asks of a sticker outline.
export function outlineOptionsFor(settings: TierSettings): OutlineOptions {
  return {
    insetRatio: LOOK.stickerInsetRatio,
    cornerRatio: LOOK.stickerCornerRatio,
    cornerSegments: settings.stickerCornerSegments,
  };
}

/// The plastic between stickers. DoubleSide for the scenes whose bodies
/// are open meshes seen from inside while a layer turns.
export function createBodyMaterial(side: Side = FrontSide): MeshStandardMaterial {
  return new MeshStandardMaterial({ color: LOOK.bodyColour, roughness: LOOK.bodyRoughness, metalness: 0, side });
}

/// One sticker colour. With a `glyph` texture (render/glyphs.ts) the
/// texture carries the colour and the mark, so the material's own colour is
/// white and leaves the texture untinted.
export function createStickerMaterial(colour: string | number, side: Side = FrontSide, glyph?: Texture): MeshStandardMaterial {
  return new MeshStandardMaterial({ color: glyph ? 0xffffff : colour, map: glyph ?? null, roughness: LOOK.stickerRoughness, metalness: 0, side });
}

export { DoubleSide, FrontSide };

/// One colour per colour index core can paint, as CSS strings. Skewb and
/// Square-1 use the cube's own palette (core numbers their colours in
/// the same order); Pyraminx's four faces get the classic green, red,
/// blue, yellow. Megaminx and Kilominx: core's colour index is the face
/// index (U, F, R, BR, BL, L, DR, DBR, DB, DBL, DL, D --
/// core/include/puzzles/dodecahedral.h). Twelve colours that stay apart
/// on a phone: the classic white / green / red / yellow / purple / blue
/// on the upper half, and their opposites' partners below.
export const CUBE_PALETTE: readonly string[] = CORE_FACE_COLOUR;

export const PALETTES: Readonly<Partial<Record<PuzzleType, readonly string[]>>> = {
  [PuzzleType.Cubic]: CUBE_PALETTE,
  [PuzzleType.Skewb]: CUBE_PALETTE,
  [PuzzleType.Square1]: CUBE_PALETTE,
  [PuzzleType.Tetrahedral]: ["#009e60", "#c41e3a", "#0051ba", "#ffd500"],
  [PuzzleType.Dodecahedral]: [
    "#f5f5f5", "#009e60", "#c41e3a", "#ffd500", "#6a3d9a", "#0051ba",
    "#ff8fb1", "#8bc34a", "#ff7f00", "#7fd1ee", "#e8d8a0", "#8a8a8a",
  ],
};
