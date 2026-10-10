import {
  SQ1_LAYER_UNITS,
  SQ1_UNIT_CORNER_FIRST,
  SQ1_UNIT_CORNER_SECOND,
  SQ1_UNIT_COUNT,
} from "../../engine/types.js";

/// The Square-1's geometry, as plain numbers (docs/en/12-phase5-square1.md
/// D5): where each 30-degree unit sits, the rigid outline of an edge and
/// of a corner, and the two halves of the middle layer. No Three.js here,
/// so the tests can check the tiling and the slice without a renderer.
///
/// Angles are measured CLOCKWISE seen from above, from the front normal
/// (front 0, left 90, back 180, right 270). A direction at angle a is
/// (x, z) = (-sin a, cos a): +Z is front, +X is right.

/// Where the cut leaves the puzzle at the front, clockwise from the front
/// normal. The cut is the vertical plane through the centre; it runs from
/// this angle to the opposite one (+180). It sits 15 degrees off the
/// front normal so that, in a cube-shaped puzzle, it lands exactly
/// between an edge and a corner -- which is also why the left half of the
/// middle layer is the smaller piece at the front.
export const CUT_FRONT_DEG = 15;

/// The angle at which unit 0 starts. The right half of the puzzle
/// (units 0..5) is the one the cut's back end (195) opens onto, going
/// clockwise through the right face.
export const UNIT_ZERO_START_DEG = CUT_FRONT_DEG + 180;

export const UNIT_DEG = 30;

/// Half the side of the cube-shaped puzzle, seen from above.
export const HALF_SIDE = 1.1;

/// Each layer is a third of the height (the middle one included, as the
/// real puzzle's cube shape has it), so the cube shape is a true cube.
export const LAYER_THICKNESS = (2 * HALF_SIDE) / 3;
/// Heights of the layers' planes: the middle layer spans +-MIDDLE_HALF.
export const MIDDLE_HALF = LAYER_THICKNESS / 2;
export const TOP_Y0 = MIDDLE_HALF;
export const TOP_Y1 = MIDDLE_HALF + LAYER_THICKNESS;
export const BOTTOM_Y1 = -MIDDLE_HALF;
export const BOTTOM_Y0 = -MIDDLE_HALF - LAYER_THICKNESS;

const RAD = Math.PI / 180;

export type Layer = "top" | "bottom";

/// (x, z) of the point at `radius` and clockwise angle `deg`.
export function polar(radius: number, deg: number): [number, number] {
  return [-radius * Math.sin(deg * RAD), radius * Math.cos(deg * RAD)];
}

/// Angle (degrees, clockwise from front, in [0, 360)) of the middle of
/// unit `unit`, 0..11 -- the same in the top and the bottom layer.
export function unitCentreDeg(unit: number): number {
  return (UNIT_ZERO_START_DEG + UNIT_DEG * unit + UNIT_DEG / 2) % 360;
}

/// One piece as it sits in a layer: where its units are, how wide it is,
/// and the angle of the middle of it.
export interface Placement {
  layer: Layer;
  kind: "edge" | "corner";
  /// Index (0..11) of its first unit going clockwise.
  firstUnit: number;
  centreDeg: number;
  spanDeg: number;
}

/// Reads a layer's pieces off core's units view (24 bytes, top then
/// bottom). A corner is announced by its first unit and its second is
/// skipped; a second unit with no first before it (it is the layer's
/// unit 0, its first at unit 11) belongs to the corner already found.
export function placementsFor(units: Uint8Array, layer: Layer): Placement[] {
  if (units.length !== SQ1_UNIT_COUNT) throw new Error(`placementsFor: ${units.length} units, expected ${SQ1_UNIT_COUNT}`);
  const base = layer === "top" ? 0 : SQ1_LAYER_UNITS;
  const found: Placement[] = [];
  for (let i = 0; i < SQ1_LAYER_UNITS; i++) {
    const unit = units[base + i]!;
    if (unit === SQ1_UNIT_CORNER_SECOND) continue;
    if (unit === SQ1_UNIT_CORNER_FIRST) {
      if (units[base + ((i + 1) % SQ1_LAYER_UNITS)] !== SQ1_UNIT_CORNER_SECOND) {
        throw new Error(`placementsFor: unit ${i} starts a corner with no second half`);
      }
      found.push({ layer, kind: "corner", firstUnit: i, centreDeg: (UNIT_ZERO_START_DEG + UNIT_DEG * (i + 1)) % 360, spanDeg: 2 * UNIT_DEG });
    } else {
      found.push({ layer, kind: "edge", firstUnit: i, centreDeg: unitCentreDeg(i), spanDeg: UNIT_DEG });
    }
  }
  return found;
}

/// A piece's corners on the plan, going clockwise from the centre, as
/// (radius, angle relative to its own middle). An edge is the triangle
/// centre / the two points of the square's side 15 degrees either way; a
/// corner is the kite centre / side point / square corner / side point.
const SIDE_RADIUS = HALF_SIDE / Math.cos(15 * RAD);
const CORNER_RADIUS = HALF_SIDE * Math.SQRT2;

export interface PolarPoint {
  radius: number;
  deg: number;
}

/// The outer corners of a piece (the centre, which every piece shares, is
/// left out): two for an edge, three for a corner. Consecutive pairs are
/// its outer faces, clockwise -- so a corner's first face is its first
/// unit's side sticker.
export function outerPoints(kind: "edge" | "corner"): PolarPoint[] {
  return kind === "edge"
    ? [
        { radius: SIDE_RADIUS, deg: -15 },
        { radius: SIDE_RADIUS, deg: 15 },
      ]
    : [
        { radius: SIDE_RADIUS, deg: -30 },
        { radius: CORNER_RADIUS, deg: 0 },
        { radius: SIDE_RADIUS, deg: 30 },
      ];
}

/// The outline of `placement` on the plan, centre first, as (x, z).
export function outlineOf(placement: Placement): [number, number][] {
  return [
    [0, 0],
    ...outerPoints(placement.kind).map((p) => polar(p.radius, p.deg + placement.centreDeg)),
  ];
}

/// The two halves of the middle layer on the plan, as (x, z) polygons
/// going clockwise from above. The cut's two ends lie on the square's
/// front and back sides, `HALF_SIDE * tan(15)` off the middle of each.
const CUT_OFFSET = HALF_SIDE * Math.tan(CUT_FRONT_DEG * RAD);

export interface MiddleHalf {
  half: "left" | "right";
  /// Outline, clockwise from above.
  outline: [number, number][];
  /// Its three outer faces as index pairs into `outline`: the front, the
  /// side and the back one (core's facelet order for the middle layer).
  faces: [number, number][];
}

export const MIDDLE_HALVES: readonly MiddleHalf[] = [
  {
    half: "left",
    outline: [
      [-CUT_OFFSET, HALF_SIDE],
      [-HALF_SIDE, HALF_SIDE],
      [-HALF_SIDE, -HALF_SIDE],
      [CUT_OFFSET, -HALF_SIDE],
    ],
    faces: [[0, 1], [1, 2], [2, 3]],
  },
  {
    half: "right",
    outline: [
      [CUT_OFFSET, -HALF_SIDE],
      [HALF_SIDE, -HALF_SIDE],
      [HALF_SIDE, HALF_SIDE],
      [-CUT_OFFSET, HALF_SIDE],
    ],
    // front (long), right side, back (short) -- listed front first to
    // match core's facelet order; the outline above runs back to front.
    faces: [[3, 2], [2, 1], [1, 0]],
  },
];

/// The horizontal direction perpendicular to the cut, pointing into the
/// right half: the axis the slice turns about (the right half is turned
/// half way round it, which swaps its top and bottom and reverses it
/// front to back, staying on its own side of the cut).
export function cutNormal(): [number, number] {
  const deg = UNIT_ZERO_START_DEG + 90; // the middle of the right half
  return polar(1, deg);
}

/// Signed distance of the plan point (x, z) from the cut plane, positive
/// on the right half's side.
export function sideOfCut(x: number, z: number): number {
  const [nx, nz] = cutNormal();
  return x * nx + z * nz;
}

/// Whether `placement` lies in the right half (units 0..5). Only
/// meaningful for pieces that do not straddle the cut, which is every
/// piece when the slice is legal.
export function isRightHalf(placement: Placement): boolean {
  return placement.firstUnit < 6;
}

/// Radius of the sphere around the origin that holds the whole puzzle at
/// any turn: its farthest points are a corner's outer vertex, out at
/// CORNER_RADIUS in plan, on the top or bottom face. Layer turns and the
/// slice only rotate pieces about axes through the origin, so the sphere
/// holds at every angle.
export const BOUNDING_RADIUS = Math.hypot(CORNER_RADIUS, TOP_Y1);
