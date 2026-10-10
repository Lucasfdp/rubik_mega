/// Inset and rounded sticker outlines (docs/en/13-phase6-polish.md D4).
///
/// Every sticker in every family is a flat convex polygon -- a square on
/// a cube, a triangle on a Pyraminx, a pentagon or a kite on a Megaminx
/// or Kilominx, a wedge on a Square-1. This one pure function turns such
/// a polygon into the outline that is actually drawn: pulled in from the
/// edges so the dark body shows between stickers, with its corners
/// rounded. It is the single place that decides what a sticker looks
/// like, so all six puzzles share one look (look.ts holds the ratios).

export type Point3 = [number, number, number];

/// How a polygon is shaped. Both ratios are fractions of the polygon's
/// own inradius, so a small sticker and a large one get the same
/// proportions rather than the same absolute size.
export interface OutlineOptions {
  /// How far each edge moves inward (0 leaves the polygon as it is).
  insetRatio: number;
  /// How far from each corner the rounding starts (0 keeps sharp corners).
  cornerRatio: number;
  /// Straight pieces a rounded corner is drawn as (1 is a plain chamfer).
  cornerSegments: number;
}

/// Insets beyond this would collapse a polygon whose corners are acute.
const MAX_INSET_RATIO = 0.9;
/// A corner's rounding never takes more than this share of either
/// neighbouring edge, so two adjacent roundings cannot meet and overlap.
const MAX_EDGE_SHARE = 0.49;
/// Below this the two edges at a corner are treated as one straight line.
const STRAIGHT_EPSILON = 1e-9;

type Point2 = [number, number];

function sub(a: Point3, b: Point3): Point3 {
  return [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
}
function dot(a: Point3, b: Point3): number {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
function cross(a: Point3, b: Point3): Point3 {
  return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
}
function unit(a: Point3): Point3 {
  const length = Math.hypot(a[0], a[1], a[2]);
  return [a[0] / length, a[1] / length, a[2] / length];
}

/// The outline to draw for the planar convex polygon `points`, which
/// faces along `normal`. The result keeps the input's winding (so a
/// polygon wound counter-clockwise seen from outside stays that way),
/// lies in the same plane, and sits entirely inside the input.
///
/// The vertex count is `points.length * (cornerSegments + 1)` with
/// rounding on, and `points.length` with `cornerRatio` 0 -- except that
/// a corner so shallow it is already straight keeps one vertex.
export function stickerOutline(points: readonly Point3[], normal: Point3, options: OutlineOptions): Point3[] {
  if (points.length < 3) throw new Error(`stickerOutline: a polygon needs at least 3 points (got ${points.length})`);
  const n = unit(normal);
  const centre: Point3 = [0, 0, 0];
  for (const p of points) for (let k = 0; k < 3; k++) centre[k]! += p[k]! / points.length;

  // A basis of the polygon's plane. If the winding is clockwise about
  // `n`, flip the second axis so the 2D polygon is counter-clockwise --
  // the maths below assumes it, and mapping back with the same basis
  // returns the original winding.
  const u = unit(sub(points[1]!, points[0]!));
  let v = cross(n, u);
  const flat: Point2[] = points.map((p) => {
    const d = sub(p, centre);
    return [dot(d, u), dot(d, v)];
  });
  if (signedArea(flat) < 0) {
    v = [-v[0], -v[1], -v[2]];
    for (const q of flat) q[1] = -q[1];
  }

  const inradius = inradiusOf(flat);
  const inset = insetPolygon(flat, Math.min(Math.max(options.insetRatio, 0), MAX_INSET_RATIO) * inradius);
  const rounded = roundCorners(inset, Math.max(options.cornerRatio, 0) * inradius, Math.max(Math.floor(options.cornerSegments), 1));

  return rounded.map(([x, y]): Point3 => [
    centre[0] + u[0] * x + v[0] * y,
    centre[1] + u[1] * x + v[1] * y,
    centre[2] + u[2] * x + v[2] * y,
  ]);
}

/// Signed area of a 2D polygon: positive when counter-clockwise.
export function signedArea(polygon: readonly Point2[]): number {
  let sum = 0;
  polygon.forEach(([x, y], i) => {
    const [nx, ny] = polygon[(i + 1) % polygon.length]!;
    sum += x * ny - nx * y;
  });
  return sum / 2;
}

/// Left-hand unit normal of the edge from `a` to `b` -- inward, for a
/// counter-clockwise polygon.
function inwardNormal(a: Point2, b: Point2): Point2 {
  const dx = b[0] - a[0];
  const dy = b[1] - a[1];
  const length = Math.hypot(dx, dy);
  return [-dy / length, dx / length];
}

/// Radius of the largest circle about the origin (the polygon's vertex
/// mean) that stays inside it: the shortest distance to any edge.
function inradiusOf(polygon: readonly Point2[]): number {
  let shortest = Infinity;
  polygon.forEach((a, i) => {
    const b = polygon[(i + 1) % polygon.length]!;
    const [nx, ny] = inwardNormal(a, b);
    shortest = Math.min(shortest, -(nx * a[0] + ny * a[1]));
  });
  return shortest;
}

/// Moves every edge of a counter-clockwise convex polygon inward by
/// `distance`. Each new vertex is where its two shifted edges meet, so
/// the vertex count is unchanged.
function insetPolygon(polygon: readonly Point2[], distance: number): Point2[] {
  if (distance === 0) return polygon.map((p): Point2 => [p[0], p[1]]);
  const count = polygon.length;
  const lines = polygon.map((a, i) => {
    const b = polygon[(i + 1) % count]!;
    const normal = inwardNormal(a, b);
    // The edge's line is dot(normal, p) = offset, shifted inward.
    return { normal, offset: normal[0] * a[0] + normal[1] * a[1] + distance };
  });
  return polygon.map((vertex, i) => {
    const previous = lines[(i + count - 1) % count]!;
    const next = lines[i]!;
    const det = previous.normal[0] * next.normal[1] - previous.normal[1] * next.normal[0];
    if (Math.abs(det) < STRAIGHT_EPSILON) {
      // Collinear edges: just slide the vertex along their shared normal.
      return [vertex[0] + next.normal[0] * distance, vertex[1] + next.normal[1] * distance];
    }
    return [
      (previous.offset * next.normal[1] - next.offset * previous.normal[1]) / det,
      (previous.normal[0] * next.offset - next.normal[0] * previous.offset) / det,
    ];
  });
}

/// Replaces every corner with a quadratic Bezier arc that starts and ends
/// `radius`-ish along the two edges. A quadratic curve with its control
/// point at the corner stays inside the corner's triangle, so a convex
/// polygon stays convex.
function roundCorners(polygon: readonly Point2[], radius: number, segments: number): Point2[] {
  if (radius === 0) return polygon.map((p): Point2 => [p[0], p[1]]);
  const count = polygon.length;
  const out: Point2[] = [];
  polygon.forEach((corner, i) => {
    const before = polygon[(i + count - 1) % count]!;
    const after = polygon[(i + 1) % count]!;
    const toBefore: Point2 = [before[0] - corner[0], before[1] - corner[1]];
    const toAfter: Point2 = [after[0] - corner[0], after[1] - corner[1]];
    const lengthBefore = Math.hypot(...toBefore);
    const lengthAfter = Math.hypot(...toAfter);
    const e1: Point2 = [toBefore[0] / lengthBefore, toBefore[1] / lengthBefore];
    const e2: Point2 = [toAfter[0] / lengthAfter, toAfter[1] / lengthAfter];
    const cosine = Math.min(Math.max(e1[0] * e2[0] + e1[1] * e2[1], -1), 1);
    const halfAngle = Math.acos(cosine) / 2;
    // A straight "corner" (half-angle of 90 degrees) has nothing to round.
    if (Math.PI / 2 - halfAngle < STRAIGHT_EPSILON) {
      out.push([corner[0], corner[1]]);
      return;
    }
    // The tangent length that makes an arc of the wanted radius, kept
    // short enough that neighbouring corners never overlap.
    const tangent = Math.min(radius / Math.tan(halfAngle), MAX_EDGE_SHARE * Math.min(lengthBefore, lengthAfter));
    const start: Point2 = [corner[0] + e1[0] * tangent, corner[1] + e1[1] * tangent];
    const end: Point2 = [corner[0] + e2[0] * tangent, corner[1] + e2[1] * tangent];
    for (let k = 0; k <= segments; k++) {
      const s = k / segments;
      const a = (1 - s) * (1 - s);
      const b = 2 * s * (1 - s);
      const c = s * s;
      out.push([a * start[0] + b * corner[0] + c * end[0], a * start[1] + b * corner[1] + c * end[1]]);
    }
  });
  return out;
}
