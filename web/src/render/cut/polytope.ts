/// Exact-enough convex geometry for the cut-plane puzzles
/// (docs/en/09-phase3-skewb-pyraminx.md D6): every piece is a convex
/// cell -- the base solid clipped by the cut planes -- so one small
/// half-space intersection routine gives bodies, sticker outlines and
/// (later) Phase 4's dodecahedral pieces. Pure functions, no Three.js.

export type Vec = [number, number, number];

/// The half-space `dot(n, p) <= d`. `n` need not be unit length.
export interface Plane {
  n: Vec;
  d: number;
}

/// A convex polytope as its vertices plus, per bounding plane, the
/// polygon where it touches the polytope (vertex indices, ordered
/// counter-clockwise seen from outside along that plane's normal).
export interface Polytope {
  vertices: Vec[];
  faces: { plane: Plane; loop: number[] }[];
}

const EPS = 1e-7;

/// How much slack the half-space routines allow, derived once from the
/// `noise` of the input -- the most any plane coordinate can differ from
/// its exact value. Skewb and Pyraminx are exact (noise 0), so they keep
/// the tight tolerances; Megaminx and Kilominx coordinates were rounded
/// to 1/4096 by core, so "the same plane", "inside" and "on this plane"
/// all have to be wider than that rounding or a cell silently loses a
/// face. Every width stays far below the smallest real feature (a
/// piece's edge is of the order of 0.05 in the solid's unit-inradius
/// coordinates), so widening cannot merge distinct things.
interface Tolerance {
  /// Slack for "dot(n, p) <= d" and for planes being the same plane.
  inside: number;
  /// Two points closer than this are the same vertex.
  merge: number;
  /// A vertex this close to a plane lies on it.
  onPlane: number;
  /// How far from 1 the cosine of two planes' normals may be and still
  /// call them parallel (a cosine's error is quadratic in the angle's).
  parallel: number;
}

function toleranceFor(noise: number): Tolerance {
  return {
    inside: EPS + 8 * noise,
    merge: 1e-6 + 20 * noise,
    onPlane: EPS * 10 + 16 * noise,
    parallel: EPS + 100 * noise * noise,
  };
}

function dot(a: Vec, b: Vec): number {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
function cross(a: Vec, b: Vec): Vec {
  return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
}
function sub(a: Vec, b: Vec): Vec {
  return [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
}
function norm(a: Vec): number {
  return Math.sqrt(dot(a, a));
}

/// Two planes bound the same half-space (duplicates arise because
/// opposite Skewb corners cut along one plane, and a plane can be both
/// a base face and a cut).
function samePlane(a: Plane, b: Plane, tol: Tolerance): boolean {
  const la = norm(a.n);
  const lb = norm(b.n);
  const c = dot(a.n, b.n) / (la * lb);
  return Math.abs(c - 1) < tol.parallel && Math.abs(a.d / la - b.d / lb) < tol.inside;
}

export function dedupePlanes(planes: readonly Plane[], noise = 0): Plane[] {
  const tol = toleranceFor(noise);
  const out: Plane[] = [];
  for (const plane of planes) if (!out.some((q) => samePlane(q, plane, tol))) out.push(plane);
  return out;
}

/// Solves the 3x3 system n_i . p = d_i by Cramer's rule, or null if the
/// planes don't meet in a single point.
function intersect3(a: Plane, b: Plane, c: Plane): Vec | null {
  const det = dot(a.n, cross(b.n, c.n));
  if (Math.abs(det) < EPS) return null;
  const p0 = cross(b.n, c.n);
  const p1 = cross(c.n, a.n);
  const p2 = cross(a.n, b.n);
  return [
    (a.d * p0[0] + b.d * p1[0] + c.d * p2[0]) / det,
    (a.d * p0[1] + b.d * p1[1] + c.d * p2[1]) / det,
    (a.d * p0[2] + b.d * p1[2] + c.d * p2[2]) / det,
  ];
}

/// Orders coplanar `points` (indices into `vertices`) counter-clockwise
/// seen from outside along `normal`.
function orderLoop(vertices: readonly Vec[], indices: number[], normal: Vec): number[] {
  const centre: Vec = [0, 0, 0];
  for (const i of indices) for (let k = 0; k < 3; k++) centre[k]! += vertices[i]![k]! / indices.length;
  const u = sub(vertices[indices[0]!]!, centre);
  const w = cross(normal, u);
  const angle = (i: number): number => {
    const r = sub(vertices[i]!, centre);
    return Math.atan2(dot(r, w), dot(r, u));
  };
  return [...indices].sort((i, j) => angle(i) - angle(j));
}

/// The convex polytope `{p : dot(n, p) <= d for every plane}`, or null
/// if it is empty or flat. Vertices are every feasible triple
/// intersection (deduplicated); each plane that touches at least three
/// of them becomes a face.
export function polytopeFromPlanes(input: readonly Plane[], noise = 0): Polytope | null {
  const tol = toleranceFor(noise);
  const planes = dedupePlanes(input, noise);
  const vertices: Vec[] = [];
  for (let i = 0; i < planes.length; i++) {
    for (let j = i + 1; j < planes.length; j++) {
      for (let k = j + 1; k < planes.length; k++) {
        const p = intersect3(planes[i]!, planes[j]!, planes[k]!);
        if (!p) continue;
        if (planes.some((q) => dot(q.n, p) > q.d + tol.inside * Math.max(1, norm(q.n)))) continue;
        if (!vertices.some((v) => norm(sub(v, p)) < tol.merge)) vertices.push(p);
      }
    }
  }
  if (vertices.length < 4) return null;
  const faces: Polytope["faces"] = [];
  for (const plane of planes) {
    const onPlane: number[] = [];
    vertices.forEach((v, i) => {
      if (Math.abs(dot(plane.n, v) - plane.d) < tol.onPlane * Math.max(1, norm(plane.n))) onPlane.push(i);
    });
    if (onPlane.length >= 3) faces.push({ plane, loop: orderLoop(vertices, onPlane, plane.n) });
  }
  return faces.length >= 4 ? { vertices, faces } : null;
}

export function centroid(points: readonly Vec[]): Vec {
  const c: Vec = [0, 0, 0];
  for (const p of points) for (let k = 0; k < 3; k++) c[k]! += p[k]! / points.length;
  return c;
}

/// Volume by the divergence theorem over the face loops (each fan-
/// triangulated, signed against the face's outward normal).
export function polytopeVolume(poly: Polytope): number {
  let volume = 0;
  for (const { loop } of poly.faces) {
    const a = poly.vertices[loop[0]!]!;
    for (let i = 1; i + 1 < loop.length; i++) {
      const b = poly.vertices[loop[i]!]!;
      const c = poly.vertices[loop[i + 1]!]!;
      volume += dot(a, cross(b, c)) / 6;
    }
  }
  return Math.abs(volume);
}
