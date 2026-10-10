/// The cubic lattice family's render-side geometry, for any size n
/// (docs/en/07-nxn-web-view.md). Deliberately holds NO colour or
/// orientation rules: which colour sits where comes from core's
/// facelet buffer (PuzzleHandle.faceletView(), D1), so this file only
/// answers "where in 3D space is lattice cell (x, y, z)".
///
/// Axis convention, ported from rubik: +X = R, +Y = U, +Z = F
/// (right-handed). Lattice cells are integer coordinates 0..n-1 on each
/// axis -- the same cells core/'s move engine and facelet geometry use,
/// so a layer index means the same thing on both sides of the WASM
/// boundary.

export interface Vec3 {
  x: number;
  y: number;
  z: number;
}

/// One of the 3 world rotation axes a face turn happens around --
/// ported from rubik's t_axis (include/render/geometry.h). Distinct
/// from a face (6 directions): a layer turn rotates around exactly
/// one of these 3 axes, picking up both faces on it (e.g. AXIS_Y covers
/// both Up and Down layers).
export enum Axis {
  X,
  Y,
  Z,
}

export const AXIS_UNIT: Record<Axis, Vec3> = {
  [Axis.X]: { x: 1, y: 0, z: 0 },
  [Axis.Y]: { x: 0, y: 1, z: 0 },
  [Axis.Z]: { x: 0, y: 0, z: 1 },
};

/// Which world axis a (near-)axis-aligned vector points along -- ported
/// from rubik's vector_to_axis() (src/render/input.c). Used both on a
/// raw raycast hit point (which of the puzzle's 6 outer faces it landed
/// on) and on a rotation-axis cross product (web/src/interaction/
/// drag-state.ts's lockAxis(), rubik's own §1.1/B1).
export function vectorToAxis(v: Vec3): Axis {
  const ax = Math.abs(v.x);
  const ay = Math.abs(v.y);
  const az = Math.abs(v.z);
  if (ax >= ay && ax >= az) return Axis.X;
  if (ay >= az) return Axis.Y;
  return Axis.Z;
}

/// `v`'s component along `axis`.
export function axisComponent(v: Vec3, axis: Axis): number {
  if (axis === Axis.X) return v.x;
  if (axis === Axis.Y) return v.y;
  return v.z;
}

/// World-unit distance between neighbouring cubie centres on a 3x3x3
/// -- ported from rubik/include/render/geometry.h's CUBIE_SPACING
/// (slightly over 1 so a visible seam shows with no extra draw call).
/// Bigger cubes scale this down; see createCubicGeometry().
export const CUBIE_SPACING = 1.05;

/// World-unit size of a 3x3x3 cubie's solid body -- ported from
/// geometry.h's CUBIE_BODY_SIZE. Scaled with CUBIE_SPACING.
export const CUBIE_BODY_SIZE = 0.94;

/// The size whose world dimensions every other size is scaled to match
/// (docs/en/07-nxn-web-view.md D2): a 7x7x7 occupies the same screen
/// space as a 3x3x3, so camera, zoom limits and every pixel-based drag
/// threshold stay valid unchanged.
const REFERENCE_SIZE = 3;

/// The 6 faces, in core's own order (core/include/puzzles/cubic.h's
/// CUBIC_GEN_U..CUBIC_GEN_B) -- the numbers faceletView() and
/// PuzzleLayout.facelets[].face carry.
export const CoreFace = { U: 0, R: 1, F: 2, D: 3, L: 4, B: 5 } as const;
export const CORE_FACE_COUNT = 6;

/// Outward normal per core face, indexed by CoreFace value.
export const CORE_FACE_NORMAL: readonly Vec3[] = [
  { x: 0, y: 1, z: 0 }, // U
  { x: 1, y: 0, z: 0 }, // R
  { x: 0, y: 0, z: 1 }, // F
  { x: 0, y: -1, z: 0 }, // D
  { x: -1, y: 0, z: 0 }, // L
  { x: 0, y: 0, z: -1 }, // B
];

/// WCA colour scheme -- ported from rubik's PALETTE_CLASSIC (U=white,
/// D=yellow, F=green, B=blue, R=red, L=orange), indexed by CoreFace
/// value: a facelet whose colour byte is f shows CORE_FACE_COLOUR[f].
export const CORE_FACE_COLOUR: readonly string[] = [
  "#ffffff", // U
  "#c41e3a", // R
  "#009e60", // F
  "#ffd500", // D
  "#ff5800", // L
  "#0051ba", // B
];

/// One integer lattice cell, each coordinate 0..size-1.
export interface LatticeCell {
  x: number;
  y: number;
  z: number;
}

/// World-space layout of one size-n cube: how far apart cubies sit, how
/// big they are, and the conversions between lattice cells and world
/// coordinates. Built once per puzzle by createCubicGeometry().
export interface CubicGeometry {
  size: number;
  /// World units between neighbouring cubie centres.
  spacing: number;
  /// World-unit edge length of one cubie body.
  bodySize: number;
  /// Half-extent of the single bounding box raycasting picks against
  /// (rubik's geometry_half_extent(), generalised).
  halfExtent: number;
  /// Lattice index (0..size-1) -> world coordinate on any one axis.
  cellToWorld(index: number): number;
  /// World coordinate -> nearest lattice index, clamped to 0..size-1.
  worldToCell(coord: number): number;
}

/// Half-extent of a size-n cube drawn at the 3x3x3's own cubie size --
/// what createCubicGeometry() scales every size down from.
function unscaledHalfExtent(size: number): number {
  return ((size - 1) / 2) * CUBIE_SPACING + CUBIE_BODY_SIZE / 2;
}

/// Builds the world-space layout for a size-n cube, scaled so its
/// overall size matches a 3x3x3's (D2). n == 3 reproduces rubik's own
/// numbers exactly (scale 1).
export function createCubicGeometry(size: number): CubicGeometry {
  const scale = unscaledHalfExtent(REFERENCE_SIZE) / unscaledHalfExtent(size);
  const spacing = CUBIE_SPACING * scale;
  const bodySize = CUBIE_BODY_SIZE * scale;
  const middle = (size - 1) / 2;
  return {
    size,
    spacing,
    bodySize,
    halfExtent: middle * spacing + bodySize / 2,
    cellToWorld: (index) => (index - middle) * spacing,
    worldToCell: (coord) => Math.min(Math.max(Math.round(coord / spacing + middle), 0), size - 1),
  };
}

/// Every cell on the surface of a size-n cube (at least one coordinate
/// at 0 or size-1) -- the cubies there are the only ones ever visible,
/// so the interior gets no instance at all. docs/en/03-architecture.md
/// S6's position-generator contract, for the cubic family.
export function generateCubicLattice(size: number): LatticeCell[] {
  if (!Number.isInteger(size) || size < 2) {
    throw new Error(`generateCubicLattice: size must be an integer >= 2 (got ${size})`);
  }
  const last = size - 1;
  const cells: LatticeCell[] = [];
  for (let x = 0; x < size; x++) {
    for (let y = 0; y < size; y++) {
      for (let z = 0; z < size; z++) {
        if (x === 0 || x === last || y === 0 || y === last || z === 0 || z === last) {
          cells.push({ x, y, z });
        }
      }
    }
  }
  return cells;
}
