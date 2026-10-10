/// Where a Kilominx layer really separates from the rest of the puzzle,
/// and the dark walls that separation leaves showing while it turns.
/// Pure geometry, no Three.js.
///
/// Why the Megaminx's flat cut is wrong here: a Kilominx sticker is a
/// whole kite reaching down to its face's centre, so on each face beside
/// the turning one the boundary between moving and still stickers is a
/// V -- face centre up to the two lateral-edge midpoints -- not a
/// straight line. A plane cannot follow a V. But a turning layer only
/// needs its boundary to be a surface of revolution about the turn's
/// axis (anything on it stays on it as the layer turns), and the V is
/// exactly what you get by spinning one arm of it about the axis: a
/// hyperboloid band from the face centres' height up to the lateral-edge
/// midpoints' height. Above that band everything turns; below it
/// nothing does. That is the separation a physical Kilominx's plastic
/// must have, and with it whole kites rotate rigidly -- no clipping, no
/// lifting, no splitting -- and still never meet a still kite, because
/// the moving body never leaves its side of a surface it maps onto
/// itself.
///
/// Mid-turn the band shows: the moving cap's underside and the still
/// part's recess. `recessWalls` builds those faces. (The flat disc that
/// closes the band at the face centres' height is never seen: both
/// bodies cover it at every angle, so it is not drawn.)

import type { Vec } from "./polytope.js";

/// One arm of the V on a face beside the turning one: from that face's
/// centre up to the midpoint of one of its lateral edges (the edges not
/// shared with the turning face). Every arm of a layer spins to the same
/// band -- the faces are copies of each other under the layer's 72-degree
/// symmetry.
export interface Arm {
  centre: Vec;
  midpoint: Vec;
}

/// Wall resolution. Rows run up an arm, columns round the band; the
/// widest wall spans 72 degrees (one face's centre to the next), so 12
/// columns keep each facet to 6 degrees -- smooth at screen size without
/// being thousands of triangles for 24 rarely-seen meshes.
const WALL_ROWS = 8;
const WALL_COLUMNS = 12;

function dot(a: Vec, b: Vec): number {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
function lerp(a: Vec, b: Vec, t: number): Vec {
  return [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t];
}
/// Height along the axis and distance from it.
function heightAndRadius(axis: Vec, p: Vec): [number, number] {
  const h = dot(axis, p);
  return [h, Math.hypot(p[0] - h * axis[0], p[1] - h * axis[1], p[2] - h * axis[2])];
}
/// `p` turned by `angle` radians about the unit `axis` (Rodrigues).
function rotate(axis: Vec, p: Vec, angle: number): Vec {
  const c = Math.cos(angle);
  const s = Math.sin(angle);
  const k = dot(axis, p) * (1 - c);
  return [
    p[0] * c + (axis[1] * p[2] - axis[2] * p[1]) * s + axis[0] * k,
    p[1] * c + (axis[2] * p[0] - axis[0] * p[2]) * s + axis[1] * k,
    p[2] * c + (axis[0] * p[1] - axis[1] * p[0]) * s + axis[2] * k,
  ];
}
/// Signed angle from `a` to `b` about the unit `axis`, in (-pi, pi].
function angleAbout(axis: Vec, a: Vec, b: Vec): number {
  const flat = (p: Vec): Vec => {
    const h = dot(axis, p);
    return [p[0] - h * axis[0], p[1] - h * axis[1], p[2] - h * axis[2]];
  };
  const u = flat(a);
  const v = flat(b);
  const cross: Vec = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]];
  return Math.atan2(dot(axis, cross), dot(u, v));
}

/// The arms of a turning layer, read off its kites as `faceKite` returns
/// them (midpoint, corner, midpoint, face centre -- the centre always
/// last). A kite on the turning face itself has none; a kite on a face
/// beside it has one, ending at whichever of its two midpoints is lower
/// along the axis (the other is on the edge shared with the turning
/// face). `axis` must be unit length; `noise` is the layout's rounding,
/// widening "parallel" the way `faceKite` does.
export function armsOf(axis: Vec, kites: readonly { points: readonly Vec[]; normal: Vec }[], noise: number): Arm[] {
  const arms: Arm[] = [];
  for (const { points, normal } of kites) {
    if (Math.abs(dot(axis, normal)) / Math.hypot(...normal) > 1 - (1e-9 + 100 * noise * noise)) continue;
    const lower = dot(axis, points[0]!) < dot(axis, points[2]!) ? points[0]! : points[2]!;
    arms.push({ centre: points[3]!, midpoint: lower });
  }
  return arms;
}

/// Whether `p` is on the turning side of the band: above the lateral
/// midpoints' height, or between the two heights and no farther from the
/// axis than the band. `slack` widens the test (positive) or narrows it
/// (negative) for points on the band itself. Only meaningful inside the
/// solid. `axis` must be unit length; `arms` non-empty.
export function onTurningSide(axis: Vec, arms: readonly Arm[], p: Vec, slack: number): boolean {
  const arm = arms[0]!;
  const [low] = heightAndRadius(axis, arm.centre);
  const [high] = heightAndRadius(axis, arm.midpoint);
  const [h, r] = heightAndRadius(axis, p);
  if (h >= high - slack) return true;
  if (h < low - slack) return false;
  const t = Math.min(1, Math.max(0, (h - low) / (high - low)));
  const [, band] = heightAndRadius(axis, lerp(arm.centre, arm.midpoint, t));
  return r <= band + slack;
}

/// The band's part inside the solid, as triangles (three points each, in
/// the arms' coordinates). One wall per lateral edge: the band between
/// the two arms that meet at its midpoint, swept the short way round --
/// the way that passes under the lateral edge, inside the solid. The
/// band's other stretches lie outside the solid, past the side faces'
/// middles, and are not drawn. `noise` is how far two copies of one
/// midpoint may differ (the layout's rounding). `axis` must be unit
/// length.
export function recessWalls(axis: Vec, arms: readonly Arm[], noise: number): Vec[][] {
  const same = 1e-6 + 20 * noise;
  const triangles: Vec[][] = [];
  const used = new Set<number>();
  arms.forEach((first, i) => {
    if (used.has(i)) return;
    const j = arms.findIndex((other, k) => k !== i && !used.has(k) && Math.hypot(other.midpoint[0] - first.midpoint[0], other.midpoint[1] - first.midpoint[1], other.midpoint[2] - first.midpoint[2]) < same);
    if (j < 0) throw new Error("recessWalls: an arm has no partner meeting it at its midpoint");
    used.add(i).add(j);
    const second = arms[j]!;
    const grid: Vec[][] = [];
    for (let row = 0; row <= WALL_ROWS; row++) {
      const t = row / WALL_ROWS;
      const from = lerp(first.centre, first.midpoint, t);
      const sweep = angleAbout(axis, from, lerp(second.centre, second.midpoint, t));
      const line: Vec[] = [];
      for (let column = 0; column <= WALL_COLUMNS; column++) line.push(rotate(axis, from, (sweep * column) / WALL_COLUMNS));
      grid.push(line);
    }
    for (let row = 0; row < WALL_ROWS; row++) {
      for (let column = 0; column < WALL_COLUMNS; column++) {
        const a = grid[row]![column]!;
        const b = grid[row]![column + 1]!;
        const c = grid[row + 1]![column + 1]!;
        const d = grid[row + 1]![column]!;
        triangles.push([a, b, c], [a, c, d]);
      }
    }
  });
  return triangles;
}
