import * as THREE from "three";
import type { CutLayout } from "../engine/types.js";
import type { Move } from "../engine/types.js";
import type { CutScene } from "../render/cut/cut-scene.js";
import type { Plane, Vec } from "../render/cut/polytope.js";
import type { ActiveTurn, TurnTarget } from "../render/turn.js";
import type { DragAdapter, TurnDragSession } from "./drag-adapter.js";
import { DRAG_DEADZONE_PX, DRAG_FLICK_LOOKAHEAD, DRAG_MIN_PX_PER_UNIT, DRAG_RADIUS } from "./drag-state.js";
import { cutStepDeg, type CutMoveModel } from "./cut-moves.js";
import { worldToScreen, type ScreenPoint } from "./screen.js";

/// Drag-to-turn for the cut-plane puzzles (Skewb, Pyraminx --
/// docs/en/09-phase3-skewb-pyraminx.md D5). The same feel as the cubic
/// gestures (same dead zone, same arc radius, same flick look-ahead),
/// but the thing being turned is decided differently: the touched
/// sticker lies on a few cuts, and the drag direction picks one.

function dot(a: Vec, b: Vec): number {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
function cross(a: Vec, b: Vec): Vec {
  return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
}

/// Where a ray, in puzzle coordinates, enters the solid bounded by
/// `planes` -- a convex solid, so the entry is the latest of the
/// per-plane entry times and the ray hits if that is before the
/// earliest exit. Null on a miss.
export function enterConvex(planes: readonly Plane[], origin: Vec, dir: Vec): { point: Vec; plane: Plane } | null {
  let tEnter = -Infinity;
  let tExit = Infinity;
  let entering: Plane | null = null;
  for (const plane of planes) {
    const along = dot(plane.n, dir);
    const slack = plane.d - dot(plane.n, origin);
    if (Math.abs(along) < 1e-12) {
      if (slack < 0) return null; // parallel and outside
      continue;
    }
    const t = slack / along;
    if (along < 0) {
      if (t > tEnter) {
        tEnter = t;
        entering = plane;
      }
    } else if (t < tExit) {
      tExit = t;
    }
  }
  if (!entering || tEnter > tExit || tExit < 0) return null;
  const t = Math.max(tEnter, 0);
  return { point: [origin[0] + dir[0] * t, origin[1] + dir[1] * t, origin[2] + dir[2] * t], plane: entering };
}

/// The facelet a point on the outside of the solid lies on: among the
/// facelets facing the same way, the one on the same side of every cut
/// as the point. (Nearest-centre would be wrong: a Skewb's centre
/// diamond is bigger than its corner triangles are far from it.)
export function faceletAt(cut: CutLayout, point: Vec, normal: Vec): number | null {
  const length = Math.hypot(...normal);
  let nearest: { index: number; dist: number } | null = null;
  const sides = cut.generators.map((g) => dot(g.axis, point) > g.threshold);
  for (let i = 0; i < cut.facelets.length; i++) {
    const f = cut.facelets[i]!;
    const facing = dot(f.normal, normal) / (Math.hypot(...f.normal) * length);
    if (facing < 1 - (1e-9 + 100 * cut.tolerance * cut.tolerance)) continue;
    if (cut.generators.every((g, k) => g.moving.includes(i) === sides[k])) return i;
    const dist = Math.hypot(f.pos[0] - point[0], f.pos[1] - point[1], f.pos[2] - point[2]);
    if (!nearest || dist < nearest.dist) nearest = { index: i, dist };
  }
  return nearest?.index ?? null;
}

/// The generator that turns the face a touch landed on the centre of, if
/// it did: the point lies beyond that one face's cut and no other. A
/// Kilominx has no centre piece to touch -- its centre cap is a region,
/// not a facelet -- so without this a touch there would turn whichever
/// corner's face happened to be nearest.
export function centreGenerator(cut: CutLayout, point: Vec, normal: Vec): number | null {
  const length = Math.hypot(...normal);
  const beyond = cut.generators.flatMap((g, index) => (dot(g.axis, point) > g.threshold ? [index] : []));
  if (beyond.length !== 1) return null;
  const axis = cut.generators[beyond[0]!]!.axis;
  const facing = dot(axis, normal) / (Math.hypot(...axis) * length);
  return facing > 1 - (1e-9 + 100 * cut.tolerance * cut.tolerance) ? beyond[0]! : null;
}

/// The cuts a drag on `facelet` may turn: every generator that moves
/// it, but only those moving the fewest stickers -- touching a tip
/// turns the tip, not the whole layer it belongs to; touching a Skewb
/// sticker offers all four equal cuts.
export function candidateGenerators(cut: CutLayout, facelet: number): number[] {
  const containing = cut.generators.flatMap((g, index) => (g.moving.includes(facelet) ? [{ index, size: g.moving.length }] : []));
  const smallest = Math.min(...containing.map((c) => c.size));
  return containing.filter((c) => c.size === smallest).map((c) => c.index);
}

/// Facing below which the touched plane is seen too edge-on for the
/// pointer's position on it to mean anything (1 = square on): the drag
/// then falls back to measuring straight-line motion.
const ARC_MIN_FACING = 0.2;

/// Distance from the axis, in puzzle units (a Megaminx's inradius is 1),
/// below which an angle about the axis is too sensitive to a pixel of
/// motion to use.
const ARC_MIN_RADIUS = 0.15;

/// Where a ray, in puzzle coordinates, meets the plane through `through`
/// with normal `axis`, or null if it runs (nearly) parallel to it.
export function planePoint(axis: Vec, through: Vec, origin: Vec, dir: Vec): Vec | null {
  const along = dot(axis, dir);
  if (Math.abs(along) < 1e-9) return null;
  const t = (dot(axis, through) - dot(axis, origin)) / along;
  return [origin[0] + dir[0] * t, origin[1] + dir[1] * t, origin[2] + dir[2] * t];
}

/// The right-handed angle, in degrees within (-180, 180], that carries
/// `from` to `to` about the unit `axis` through `centre` -- or null when
/// either lies too close to the axis to give an angle worth trusting.
export function arcAngleDeg(axis: Vec, centre: Vec, from: Vec, to: Vec): number | null {
  const u: Vec = [from[0] - centre[0], from[1] - centre[1], from[2] - centre[2]];
  const v: Vec = [to[0] - centre[0], to[1] - centre[1], to[2] - centre[2]];
  if (Math.hypot(...u) < ARC_MIN_RADIUS || Math.hypot(...v) < ARC_MIN_RADIUS) return null;
  return (Math.atan2(dot(axis, cross(u, v)), dot(u, v)) * 180) / Math.PI;
}

/// What a session needs to follow the pointer round a face's axis
/// instead of along a straight line: the camera and canvas to cast the
/// pointer's ray, and the puzzle group's frame to bring it into puzzle
/// coordinates. Only the 72-degree puzzles use it -- up to four steps
/// of turning is a circle, which a straight-line projection cannot
/// follow past about a quarter of it.
interface ArcContext {
  camera: THREE.Camera;
  width: number;
  height: number;
  /// World to puzzle coordinates (the inverse of the group's matrix,
  /// with the display scale divided out).
  toPuzzle: THREE.Matrix4;
  scale: number;
  /// The first touched point, on the face, in puzzle coordinates.
  hit: Vec;
}

interface Candidate {
  generator: number;
  /// Screen displacement, in px, of one world unit of motion along this
  /// cut's positive-rotation direction at the touched point.
  screenDir: ScreenPoint;
  /// The cut's rotation axis (unit, puzzle coordinates).
  axis: Vec;
}

class CutDragSession implements TurnDragSession {
  locked = false;
  readonly #candidates: Candidate[];
  readonly #moves: CutMoveModel;
  /// Degrees of one step and how many steps a drag may reach: past the
  /// last amount it has gone all the way round (+-2 steps of 120 for a
  /// Skewb, +-4 steps of 72 for a Megaminx).
  readonly #stepDeg: number;
  readonly #maxSteps: number;
  readonly #start: ScreenPoint;
  #chosen: Candidate | null = null;
  readonly #arc: ArcContext | null;
  #accumDeg = 0;
  #velDegPerSec = 0;
  #lastArcPoint: Vec | null = null;
  #lastPointer: ScreenPoint;

  constructor(candidates: Candidate[], moves: CutMoveModel, start: ScreenPoint, arc: ArcContext | null = null) {
    this.#arc = arc;
    this.#lastPointer = start;
    this.#candidates = candidates;
    this.#moves = moves;
    this.#stepDeg = cutStepDeg(moves.steps);
    this.#maxSteps = moves.steps;
    this.#start = start;
  }

  drag(pointer: ScreenPoint, dtSec: number): void {
    const d = { x: pointer.x - this.#start.x, y: pointer.y - this.#start.y };
    if (!this.locked) {
      if (Math.hypot(d.x, d.y) < DRAG_DEADZONE_PX) return;
      // Whichever cut's motion the drag lines up with best.
      let best = -1;
      for (const c of this.#candidates) {
        const len = Math.hypot(c.screenDir.x, c.screenDir.y);
        const score = len > 1e-3 ? Math.abs(d.x * c.screenDir.x + d.y * c.screenDir.y) / len : 0;
        if (score > best) {
          best = score;
          this.#chosen = c;
        }
      }
      this.locked = true;
    }
    const sd = this.#chosen!.screenDir;
    const len2 = Math.max(sd.x * sd.x + sd.y * sd.y, DRAG_MIN_PX_PER_UNIT * DRAG_MIN_PX_PER_UNIT);
    const prev = this.#accumDeg;
    let raw: number;
    if (this.#arc) {
      raw = prev + this.#arcStep(pointer, sd, len2);
    } else {
      raw = ((d.x * sd.x + d.y * sd.y) / len2 / DRAG_RADIUS) * (180 / Math.PI);
    }
    this.#lastPointer = pointer;
    this.#accumDeg = Math.min(Math.max(raw, -this.#maxSteps * this.#stepDeg), this.#maxSteps * this.#stepDeg);
    if (dtSec > 0) this.#velDegPerSec = 0.7 * this.#velDegPerSec + 0.3 * ((this.#accumDeg - prev) / dtSec);
  }

  /// How far the pointer has gone round the chosen axis since the last
  /// call, in degrees: the angle between where it was and where it is,
  /// both on the plane through the touched point square to the axis. When
  /// that plane is seen too edge-on, or the pointer is on the axis, the
  /// straight-line motion since last time stands in for it.
  #arcStep(pointer: ScreenPoint, sd: ScreenPoint, len2: number): number {
    const arc = this.#arc!;
    const axis = this.#chosen!.axis;
    const here = this.#planeUnder(pointer, arc, axis);
    const before = this.#lastArcPoint ?? arc.hit;
    this.#lastArcPoint = here;
    const centre: Vec = (() => {
      const h = dot(axis, arc.hit);
      return [axis[0] * h, axis[1] * h, axis[2] * h];
    })();
    const angle = here ? arcAngleDeg(axis, centre, before, here) : null;
    if (angle !== null) return angle;
    const dx = pointer.x - this.#lastPointer.x;
    const dy = pointer.y - this.#lastPointer.y;
    return ((dx * sd.x + dy * sd.y) / len2 / DRAG_RADIUS) * (180 / Math.PI);
  }

  /// The pointer's ray met with the plane through the touched point
  /// square to `axis`, in puzzle coordinates; null when it is too
  /// edge-on to use.
  #planeUnder(pointer: ScreenPoint, arc: ArcContext, axis: Vec): Vec | null {
    const raycaster = new THREE.Raycaster();
    raycaster.setFromCamera(new THREE.Vector2((pointer.x / arc.width) * 2 - 1, 1 - (pointer.y / arc.height) * 2), arc.camera);
    const ray = raycaster.ray.clone().applyMatrix4(arc.toPuzzle);
    const dir = new THREE.Vector3().copy(ray.direction).normalize();
    if (Math.abs(axis[0] * dir.x + axis[1] * dir.y + axis[2] * dir.z) < ARC_MIN_FACING) return null;
    return planePoint(
      axis,
      arc.hit,
      [ray.origin.x / arc.scale, ray.origin.y / arc.scale, ray.origin.z / arc.scale],
      [dir.x, dir.y, dir.z],
    );
  }

  activeTurn(): ActiveTurn | null {
    return this.locked ? { generator: this.#chosen!.generator, angleDeg: this.#accumDeg } : null;
  }

  release(): { fromDeg: number; toDeg: number; move: Move | null; settle: TurnTarget | null } {
    const fromDeg = this.#accumDeg;
    if (!this.locked) return { fromDeg, toDeg: 0, move: null, settle: null };
    const generator = this.#chosen!.generator;
    const steps = Math.min(
      Math.max(Math.round((this.#accumDeg + this.#velDegPerSec * DRAG_FLICK_LOOKAHEAD) / this.#stepDeg), -this.#maxSteps),
      this.#maxSteps,
    );
    const move = this.#moves.moveForSteps(generator, steps);
    return { fromDeg, toDeg: steps * this.#stepDeg, move, settle: move ? null : { generator } };
  }

  cancel(): { fromDeg: number; settle: TurnTarget | null } {
    const settle = this.locked ? { generator: this.#chosen!.generator } : null;
    const fromDeg = this.#accumDeg;
    this.locked = false;
    this.#accumDeg = 0;
    return { fromDeg, settle };
  }
}

export function cutDragAdapter(scene: CutScene, cut: CutLayout, moves: CutMoveModel): DragAdapter {
  return {
    start(ray, camera, width, height, pointer) {
      scene.group.updateMatrixWorld(true);
      const local = ray.clone().applyMatrix4(scene.group.matrixWorld.clone().invert());
      const origin: Vec = [local.origin.x / scene.scale, local.origin.y / scene.scale, local.origin.z / scene.scale];
      const hit = enterConvex(scene.basePlanes, origin, [local.direction.x, local.direction.y, local.direction.z]);
      if (!hit) return null;
      const centre = centreGenerator(cut, hit.point, hit.plane.n);
      const facelet = faceletAt(cut, hit.point, hit.plane.n);
      if (facelet === null && centre === null) return null;

      const world = scene.group.localToWorld(new THREE.Vector3(...hit.point).multiplyScalar(scene.scale));
      const origin2d = worldToScreen(world, camera, width, height);
      const quaternion = scene.group.getWorldQuaternion(new THREE.Quaternion());
      const candidates = (centre !== null ? [centre] : candidateGenerators(cut, facelet!)).map((generator): Candidate => {
        // A point turning positively (right-handed) about `axis` moves
        // along axis x point.
        const tangent = cross(cut.generators[generator]!.axis, hit.point);
        const direction = new THREE.Vector3(...tangent).normalize().applyQuaternion(quaternion);
        const next = worldToScreen(world.clone().add(direction), camera, width, height);
        const unit = Math.hypot(...cut.generators[generator]!.axis);
        const axis = cut.generators[generator]!.axis;
        return {
          generator,
          screenDir: { x: next.x - origin2d.x, y: next.y - origin2d.y },
          axis: [axis[0] / unit, axis[1] / unit, axis[2] / unit] as Vec,
        };
      });
      const arc: ArcContext | null =
        moves.steps > 2
          ? { camera, width, height, toPuzzle: scene.group.matrixWorld.clone().invert(), scale: scene.scale, hit: hit.point }
          : null;
      return new CutDragSession(candidates, moves, pointer, arc);
    },
  };
}
