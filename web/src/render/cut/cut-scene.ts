import * as THREE from "three";
import { PuzzleType, type CutLayout } from "../../engine/types.js";
import type { PuzzleHandle } from "../../engine/puzzle-handle.js";
import type { ActiveTurn, TurnScene } from "../turn.js";
import { centroid, dedupePlanes, polytopeFromPlanes, polytopeVolume, type Plane, type Polytope, type Vec } from "./polytope.js";
import { armsOf, recessWalls } from "./shell-cut.js";
import { applyStickerUv, classicLook, createGlyphTexture, type LookOptions } from "../glyphs.js";
import { LOOK, createBodyMaterial, createStickerMaterial, outlineOptionsFor, DoubleSide } from "../look.js";
import { TIER_SETTINGS, type TierSettings } from "../quality.js";
import { stickerOutline } from "../sticker-outline.js";

/// The scene for the cut-plane puzzles, Skewb and Pyraminx
/// (docs/en/09-phase3-skewb-pyraminx.md D4-D6). Like the cubic scene it
/// knows where things ARE, never which colour belongs where: stickers
/// are coloured from core's facelet buffer (PuzzleHandle.faceletView()),
/// and which of them a turn moves comes from core's generator export.
///
/// Geometry is derived, not typed: every piece is the convex cell you
/// get by clipping the base solid with the cut planes, and the base
/// solid itself is read off the sticker normals. A piece's body is that
/// cell; its stickers are the cell's faces on the outside of the solid,
/// shrunk a little so the dark body shows between them.

/// Circumradius of the drawn puzzle, in world units -- chosen so both
/// families fill about the screen a 3x3x3 does (cubic.ts: half-extent
/// ~1.5, corner radius ~2.6).
const TARGET_RADIUS = 1.9;

export interface CutScene extends TurnScene {
  group: THREE.Group;
  /// Puzzle coordinates (core's integers) times this is local world
  /// space -- group.localToWorld() then adds the display orientation.
  scale: number;
  /// Radius of the sphere around the origin that holds the whole puzzle
  /// at any turn angle -- what the camera frames (interaction/framing.ts).
  boundingRadius: number;
  /// The solid's bounding planes in puzzle coordinates, read off the
  /// sticker normals; the drag code picks against them.
  basePlanes: readonly Plane[];
  dispose(): void;
}

function vec(a: readonly [number, number, number]): Vec {
  return [a[0], a[1], a[2]];
}
function dot(a: Vec, b: Vec): number {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/// The base solid's planes: one per distinct (normal, offset) among the
/// stickers. A face's stickers all share both.
export function basePlanesOf(cut: CutLayout): Plane[] {
  return dedupePlanes(
    cut.facelets.map((f) => ({ n: vec(f.normal), d: dot(vec(f.normal), vec(f.pos)) })),
    cut.tolerance,
  );
}

/// The cell of piece `facelet`'s slot: the base solid, cut by every
/// generator's plane on the side this piece is on. Exported so the
/// tests can check the geometry that is actually drawn.
export function pieceCell(cut: CutLayout, base: readonly Plane[], representative: number): Polytope {
  const planes: Plane[] = [...base];
  for (const g of cut.generators) {
    const axis = vec(g.axis);
    planes.push(
      g.moving.includes(representative) ? { n: [-axis[0], -axis[1], -axis[2]], d: -g.threshold } : { n: axis, d: g.threshold },
    );
  }
  const cell = polytopeFromPlanes(planes, cut.tolerance);
  if (!cell) throw new Error(`cut scene: piece of facelet ${representative} has an empty cell`);
  return cell;
}

/// Below this (in puzzle-coordinate units cubed) a "hollow" cell is a
/// flat sliver from coincident cut planes, not a real region. Skewb's
/// opposite corners cut along one plane, which is exactly that case; the
/// smallest real piece is of the order of 1.
const HOLLOW_MIN_VOLUME = 1e-6;

/// The part of the solid no piece lives in: the region on the
/// stationary side of every cut (dot(axis, p) <= threshold for all
/// generators), or null if there is none. A Skewb has none -- every cell
/// the cuts make is a piece. A Pyraminx has one: the sticker-less
/// octahedron in the middle, bounded by the four layer planes.
///
/// It is drawn as the real thing, never as something bigger. An earlier
/// version drew a dark tetrahedron at 0.7 of the whole puzzle instead,
/// which is about nine times the octahedron's volume, so it sat inside
/// the pieces too -- and a turning layer swept through it, which showed
/// as dark spikes poking out of the moving stickers. The true cell
/// never overlaps a moving piece at any angle (a test sweeps them).
export function hollowCellOf(cut: CutLayout, base: readonly Plane[]): Polytope | null {
  const planes: Plane[] = [...base];
  for (const g of cut.generators) planes.push({ n: vec(g.axis), d: g.threshold });
  const cell = polytopeFromPlanes(planes, cut.tolerance);
  return cell && polytopeVolume(cell) > HOLLOW_MIN_VOLUME ? cell : null;
}

/// A region of the solid that is not a piece of the puzzle: `sides[g]`
/// says whether generator g carries it.
export interface FillerCell {
  cell: Polytope;
  sides: boolean[];
}

/// Regions that lie beyond one or two cuts but belong to no piece. A
/// Kilominx has them -- it is a Megaminx without edges and centres, but
/// its cut planes are the Megaminx's, so those regions exist and a turn
/// carries them. They are drawn as plain dark body, with no stickers, so
/// the puzzle is solid while a layer turns; between turns they are
/// invisible bookkeeping, because geometry never moves -- only colours
/// do. Skewb and Pyraminx have none (every region is a piece; the tiling
/// test checks it), so for them this returns [].
///
/// Only regions on one or two cuts are tried: that is every region the
/// non-piece part of a Megaminx's shell is made of, and it keeps the
/// search to a few dozen small half-space intersections.
export function fillerCellsOf(cut: CutLayout, base: readonly Plane[], pieceSides: readonly (readonly boolean[])[]): FillerCell[] {
  const count = cut.generators.length;
  const known = new Set(pieceSides.map((sides) => sides.map((s) => (s ? "1" : "0")).join("")));
  const minVolume = HOLLOW_MIN_VOLUME + (cut.tolerance > 0 ? 1e-4 : 0);
  const subsets: number[][] = [];
  for (let a = 0; a < count; a++) {
    subsets.push([a]);
    for (let b = a + 1; b < count; b++) subsets.push([a, b]);
  }
  const fillers: FillerCell[] = [];
  for (const subset of subsets) {
    const sides = cut.generators.map((_, g) => subset.includes(g));
    if (known.has(sides.map((s) => (s ? "1" : "0")).join(""))) continue;
    const planes: Plane[] = [...base];
    cut.generators.forEach((g, index) => {
      const axis = vec(g.axis);
      planes.push(sides[index] ? { n: [-axis[0], -axis[1], -axis[2]], d: -g.threshold } : { n: axis, d: g.threshold });
    });
    const cell = polytopeFromPlanes(planes, cut.tolerance);
    if (cell && polytopeVolume(cell) > minVolume) fillers.push({ cell, sides });
  }
  return fillers;
}

/// A Kilominx corner's sticker as a real Kilominx draws it: the kite of
/// its face's pentagon -- the corner, the midpoints of the two edges at
/// it, and the face centre -- so a face's five kites tile the whole
/// pentagon. (The sticker position core reports is only where the kite's
/// centre sits.) Counter-clockwise seen from outside, like every face
/// loop here. Null if no face of `solid` has this normal.
export function faceKite(solid: Polytope, normal: Vec, pos: Vec, noise: number): Vec[] | null {
  const length = Math.hypot(...normal);
  const face = solid.faces.find((f) => dot(f.plane.n, normal) / (Math.hypot(...f.plane.n) * length) > 1 - (1e-9 + 100 * noise * noise));
  if (!face) return null;
  const corners = face.loop.map((i) => solid.vertices[i]!);
  let at = 0;
  corners.forEach((c, i) => {
    if (Math.hypot(c[0] - pos[0], c[1] - pos[1], c[2] - pos[2]) < Math.hypot(corners[at]![0] - pos[0], corners[at]![1] - pos[1], corners[at]![2] - pos[2])) at = i;
  });
  const corner = corners[at]!;
  const previous = corners[(at + corners.length - 1) % corners.length]!;
  const next = corners[(at + 1) % corners.length]!;
  const midway = (a: Vec, b: Vec): Vec => [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2];
  return [midway(previous, corner), corner, midway(corner, next), centroid(corners)];
}

/// Fan-triangulates one polygon into `positions`, scaled and with every
/// vertex pulled `lift` along `normal`.
function pushPolygon(positions: number[], points: readonly Vec[], normal: Vec, lift: number): void {
  const length = Math.hypot(...normal);
  for (let i = 1; i + 1 < points.length; i++) {
    for (const p of [points[0]!, points[i]!, points[i + 1]!]) {
      positions.push(p[0] + (normal[0] / length) * lift, p[1] + (normal[1] / length) * lift, p[2] + (normal[2] / length) * lift);
    }
  }
}

function geometryFrom(positions: number[]): THREE.BufferGeometry {
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute("position", new THREE.Float32BufferAttribute(positions, 3));
  geometry.computeVertexNormals();
  return geometry;
}

/// A Kilominx kite as drawn: its outline in puzzle coordinates (whole,
/// not shrunk) and its face's outward normal.
interface ShellKite {
  points: Vec[];
  normal: Vec;
}

/// A shell's dark plastic for one generator's turn: the still part and
/// the turning cap, each its kites' backing plus its side of the recess
/// walls (shell-cut.ts), so both are closed and nothing is seen through.
interface ShellBodies {
  still: THREE.Mesh;
  moving: THREE.Mesh;
}

export function buildCutScene(puzzle: PuzzleHandle, tier: TierSettings = TIER_SETTINGS.standard, look?: LookOptions): CutScene {
  const { type, cut, faceletCount } = puzzle.layout;
  if (!cut) throw new Error(`buildCutScene: puzzle type ${PuzzleType[type] ?? type} is not a cut-plane puzzle`);
  // `cut` is narrowed above but TypeScript drops that inside closures.
  const cutLayout: CutLayout = cut;
  const base = basePlanesOf(cut);

  // Pieces: facelets sharing (pieceType, slot).
  const pieces = new Map<string, number[]>();
  cut.facelets.forEach((f, i) => {
    const key = `${f.pieceType}:${f.slot}`;
    pieces.set(key, [...(pieces.get(key) ?? []), i]);
  });
  const sidesOf = (representative: number): boolean[] => cut.generators.map((g) => g.moving.includes(representative));
  const cells = [...pieces.values()].map((facelets) => ({
    facelets,
    cell: pieceCell(cut, base, facelets[0]!),
    sides: sidesOf(facelets[0]!),
  }));
  const fillers = fillerCellsOf(cut, base, cells.map(({ sides }) => sides));
  // Pieces that leave regions of the solid empty (a Kilominx's edge and
  // centre positions) cannot be drawn as their cells: no tiling by cuts
  // can own a face's centre. They are drawn as a shell instead -- each
  // piece is just its stickers, whole kites turning rigidly with it --
  // over dark bodies cut the way a real Kilominx is (shell-cut.ts), not
  // by the planes.
  const shell = fillers.length > 0;
  const solid = shell ? polytopeFromPlanes(base, cut.tolerance) : null;

  const radius = Math.max(...cells.flatMap(({ cell }) => cell.vertices.map((v) => Math.hypot(...v))));
  const scale = TARGET_RADIUS / radius;
  const scaled = (v: Vec): Vec => [v[0] * scale, v[1] * scale, v[2] * scale];
  const group = new THREE.Group();
  const bodyMaterial = createBodyMaterial(DoubleSide);
  const { palette, glyphs } = look ?? classicLook(type);
  const glyphTextures = glyphs ? palette.map((colour, i) => createGlyphTexture(colour, glyphs[i]!)) : [];
  const materials = palette.map((colour, i) => createStickerMaterial(colour, THREE.FrontSide, glyphTextures[i]));
  const outline = outlineOptionsFor(tier);
  const geometries: THREE.BufferGeometry[] = [];

  /// One sticker: the face polygon pulled in from its edges and rounded
  /// (the gaps between stickers, render/sticker-outline.ts), and lifted
  /// off the body so the two never z-fight.
  function stickerMesh(points: readonly Vec[], normal: Vec, material: THREE.Material): THREE.Mesh {
    const positions: number[] = [];
    pushPolygon(positions, stickerOutline(points, normal, outline).map(scaled), normal, LOOK.stickerLift);
    const mesh = new THREE.Mesh(geometryFrom(positions), material);
    if (glyphs) applyStickerUv(mesh.geometry, normal);
    geometries.push(mesh.geometry);
    group.add(mesh);
    return mesh;
  }

  const bodyMeshes: { mesh: THREE.Mesh; sides: boolean[] }[] = [];
  const stickerMeshes: { mesh: THREE.Mesh; sides: boolean[] }[] = new Array(faceletCount);
  const kites: ShellKite[] = [];
  for (const { facelets, cell, sides } of cells) {
    if (!shell) {
      const positions: number[] = [];
      for (const face of cell.faces) {
        pushPolygon(positions, face.loop.map((i) => scaled(cell.vertices[i]!)), face.plane.n, 0);
      }
      const body = new THREE.Mesh(geometryFrom(positions), bodyMaterial);
      geometries.push(body.geometry);
      group.add(body);
      bodyMeshes.push({ mesh: body, sides });
    }

    for (const i of facelets) {
      const normal = vec(cut.facelets[i]!.normal);
      const face = cell.faces.find((f) => {
        const c = dot(f.plane.n, normal) / (Math.hypot(...f.plane.n) * Math.hypot(...normal));
        // Widened by the layout's rounding (0 for the exact families).
        const parallel = 1e-9 + 100 * cut.tolerance * cut.tolerance;
        const sameOffset = 1e-6 + 8 * cut.tolerance;
        return c > 1 - parallel && Math.abs(f.plane.d / Math.hypot(...f.plane.n) - dot(normal, vec(cut.facelets[i]!.pos)) / Math.hypot(...normal)) < sameOffset;
      });
      if (!face) throw new Error(`cut scene: facelet ${i} has no outer face on its piece`);
      const kite = solid ? faceKite(solid, normal, vec(cut.facelets[i]!.pos), cut.tolerance) : null;
      const mesh = stickerMesh(kite ?? face.loop.map((v) => cell.vertices[v]!), normal, materials[0]!);
      stickerMeshes[i] = { mesh, sides };
      if (kite) kites[i] = { points: kite, normal };
    }
  }

  /// A dark mesh from polygons in puzzle coordinates, added to the group.
  function darkMesh(polygons: readonly (readonly Vec[])[]): THREE.Mesh {
    const positions: number[] = [];
    for (const polygon of polygons) pushPolygon(positions, polygon.map(scaled), [0, 0, 1], 0);
    const mesh = new THREE.Mesh(geometryFrom(positions), bodyMaterial);
    geometries.push(mesh.geometry);
    group.add(mesh);
    return mesh;
  }

  // A shell's dark plastic. At rest: every kite's full outline, backing
  // the gaps between stickers. While generator g turns: that split into
  // the still part and the cap, each closed by the recess walls -- built
  // up front for every generator so a turn never allocates.
  const restBody = shell ? darkMesh(kites.map(({ points }) => points)) : null;
  const shellBodies: ShellBodies[] = !shell
    ? []
    : cut.generators.map((g) => {
        const axis = vec(g.axis);
        const length = Math.hypot(...axis);
        const unit: Vec = [axis[0] / length, axis[1] / length, axis[2] / length];
        const turning = kites.filter((_, i) => g.moving.includes(i));
        const walls = recessWalls(unit, armsOf(unit, turning, cut.tolerance), cut.tolerance);
        const still = darkMesh([...kites.filter((_, i) => !g.moving.includes(i)).map(({ points }) => points), ...walls]);
        const moving = darkMesh([...turning.map(({ points }) => points), ...walls]);
        still.visible = false;
        moving.visible = false;
        return { still, moving };
      });

  // Any region no piece fills (a Pyraminx's middle) is drawn as a dark
  // solid, so you never see through the puzzle while a layer turns. It
  // stands still: not in bodyMeshes, so no turn ever moves it.
  const hollow = shell ? null : hollowCellOf(cut, base);
  if (hollow) {
    const positions: number[] = [];
    for (const face of hollow.faces) pushPolygon(positions, face.loop.map((i) => scaled(hollow.vertices[i]!)), face.plane.n, 0);
    const mesh = new THREE.Mesh(geometryFrom(positions), bodyMaterial);
    geometries.push(mesh.geometry);
    group.add(mesh);
  }
  if (type === PuzzleType.Tetrahedral) {
    // Vertex 0 points up, as a Pyraminx is held.
    const up = vec(cut.generators[0]!.axis);
    group.quaternion.setFromUnitVectors(new THREE.Vector3(...up).normalize(), new THREE.Vector3(0, 1, 0));
  }

  // Which meshes each generator moves: stickers by facelet index,
  // bodies by their piece's representative facelet.
  const movingSets = cut.generators.map((g) => new Set(g.moving));

  function setActiveTurn(turn: ActiveTurn | null): void {
    const generator = turn && "generator" in turn ? turn.generator : null;
    const moving = generator === null ? null : movingSets[generator];
    const quaternion = new THREE.Quaternion();
    if (generator !== null && turn && moving) {
      const axis = new THREE.Vector3(...cutLayout.generators[generator]!.axis).normalize();
      quaternion.setFromAxisAngle(axis, (turn.angleDeg * Math.PI) / 180);
    }
    bodyMeshes.forEach(({ mesh, sides }) => {
      if (generator !== null && sides[generator]) mesh.quaternion.copy(quaternion);
      else mesh.quaternion.identity();
    });
    stickerMeshes.forEach(({ mesh, sides }) => {
      if (generator !== null && sides[generator]) mesh.quaternion.copy(quaternion);
      else mesh.quaternion.identity();
    });
    if (restBody) restBody.visible = generator === null;
    shellBodies.forEach(({ still, moving }, g) => {
      still.visible = moving.visible = g === generator;
      if (g === generator) moving.quaternion.copy(quaternion);
      else moving.quaternion.identity();
    });
  }

  function sync(): void {
    const colours = puzzle.faceletView();
    if (colours.length !== faceletCount) {
      throw new Error(`cut scene sync: ${colours.length} facelet colours for ${faceletCount} facelets`);
    }
    colours.forEach((colour, i) => {
      const material = materials[colour];
      if (!material) throw new Error(`cut scene sync: facelet ${i} has colour ${colour}, expected 0-${materials.length - 1}`);
      stickerMeshes[i]!.mesh.material = material;
    });
    setActiveTurn(null);
  }

  function dispose(): void {
    for (const geometry of geometries) geometry.dispose();
    bodyMaterial.dispose();
    for (const material of materials) material.dispose();
    for (const texture of glyphTextures) texture.dispose();
  }

  sync();
  return { group, scale, boundingRadius: TARGET_RADIUS, basePlanes: base, sync, setActiveTurn, dispose };
}
