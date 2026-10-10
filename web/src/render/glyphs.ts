import * as THREE from "three";
import { PuzzleType } from "../engine/types.js";
import { PALETTES } from "./look.js";
import { glyphsFor, paletteFor, readableInk, type PaletteChoice } from "./palettes.js";

/// Sticker glyph mode (docs/en/13-phase6-polish.md D11): a letter or shape
/// on every sticker, so a puzzle can be played without telling colours
/// apart. A glyph is part of the sticker's own material (a texture with the
/// sticker colour behind the mark), not an extra mesh, so turning it on
/// costs no draw calls.

/// How a scene should look: the colour of each colour index, and, in glyph
/// mode, the mark on each.
export interface LookOptions {
  palette: readonly string[];
  /// One mark per colour index, or null for plain stickers.
  glyphs: readonly string[] | null;
}

/// What a scene builder uses when none is given: the classic colours, no marks.
export function classicLook(type: PuzzleType): LookOptions {
  const palette = PALETTES[type];
  if (!palette) throw new Error(`classicLook: puzzle type ${PuzzleType[type] ?? type} has no palette`);
  return { palette, glyphs: null };
}

/// The look the settings ask for.
export function lookFor(type: PuzzleType, palette: PaletteChoice, glyphs: boolean): LookOptions {
  return { palette: paletteFor(type, palette), glyphs: glyphs ? glyphsFor(type) : null };
}

const TEXTURE_SIZE = 128;
/// The marks that are drawn as shapes rather than typeset: a font may draw
/// them as emoji, or not at all.
const SHAPES = new Set(["●", "■", "▲", "◆"]);

function drawShape(context: CanvasRenderingContext2D, shape: string, centre: number, radius: number): void {
  context.beginPath();
  if (shape === "●") context.arc(centre, centre, radius, 0, Math.PI * 2);
  else if (shape === "■") context.rect(centre - radius, centre - radius, radius * 2, radius * 2);
  else if (shape === "▲") {
    context.moveTo(centre, centre - radius);
    context.lineTo(centre + radius * 1.05, centre + radius * 0.8);
    context.lineTo(centre - radius * 1.05, centre + radius * 0.8);
    context.closePath();
  } else {
    context.moveTo(centre, centre - radius * 1.1);
    context.lineTo(centre + radius * 0.85, centre);
    context.lineTo(centre, centre + radius * 1.1);
    context.lineTo(centre - radius * 0.85, centre);
    context.closePath();
  }
  context.fill();
}

/// One sticker's texture: `colour` all over, the mark in readable ink at
/// the centre. Everything outside the mark is the plain colour, so the
/// sides of a sticker (which sample the texture's edge) match its face.
export function createGlyphTexture(colour: string, mark: string): THREE.Texture {
  const canvas = document.createElement("canvas");
  canvas.width = canvas.height = TEXTURE_SIZE;
  const context = canvas.getContext("2d");
  if (!context) throw new Error("createGlyphTexture: no 2d canvas context");
  context.fillStyle = colour;
  context.fillRect(0, 0, TEXTURE_SIZE, TEXTURE_SIZE);
  context.fillStyle = readableInk(colour);
  const centre = TEXTURE_SIZE / 2;
  if (SHAPES.has(mark)) drawShape(context, mark, centre, TEXTURE_SIZE * 0.24);
  else {
    context.font = `700 ${mark.length > 1 ? 44 : 64}px system-ui, -apple-system, "Segoe UI", sans-serif`;
    context.textAlign = "center";
    context.textBaseline = "middle";
    context.fillText(mark, centre, centre + 3);
  }
  const texture = new THREE.CanvasTexture(canvas);
  texture.colorSpace = THREE.SRGBColorSpace;
  texture.anisotropy = 4;
  return texture;
}

/// Gives `geometry` texture coordinates that put the middle of its outline
/// at (0.5, 0.5), by projecting onto the plane with unit `normal`. "Up" in
/// the texture is the world's +y as far as the plane allows, so marks stand
/// upright on the faces of a puzzle held the usual way. The outline fills
/// the unit square, so a mark drawn in the middle of the texture sits in the
/// middle of the sticker whatever its shape.
export function applyStickerUv(geometry: THREE.BufferGeometry, normal: readonly [number, number, number]): void {
  const n = new THREE.Vector3(...normal).normalize();
  const worldUp = Math.abs(n.y) > 0.99 ? new THREE.Vector3(0, 0, -Math.sign(n.y)) : new THREE.Vector3(0, 1, 0);
  const v = worldUp.clone().addScaledVector(n, -worldUp.dot(n)).normalize();
  const u = new THREE.Vector3().crossVectors(v, n);
  const positions = geometry.getAttribute("position");
  const point = new THREE.Vector3();
  const us: number[] = [];
  const vs: number[] = [];
  for (let i = 0; i < positions.count; i++) {
    point.fromBufferAttribute(positions, i);
    us.push(point.dot(u));
    vs.push(point.dot(v));
  }
  const centreU = (Math.min(...us) + Math.max(...us)) / 2;
  const centreV = (Math.min(...vs) + Math.max(...vs)) / 2;
  const extent = Math.max(...us.map((x) => Math.abs(x - centreU)), ...vs.map((y) => Math.abs(y - centreV)), 1e-9);
  const uv = new Float32Array(positions.count * 2);
  for (let i = 0; i < positions.count; i++) {
    uv[i * 2] = 0.5 + (us[i]! - centreU) / (2 * extent);
    uv[i * 2 + 1] = 0.5 + (vs[i]! - centreV) / (2 * extent);
  }
  geometry.setAttribute("uv", new THREE.BufferAttribute(uv, 2));
}
