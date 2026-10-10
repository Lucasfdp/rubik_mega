import * as THREE from "three";
import {
  AXIS_UNIT,
  Axis,
  CUBIE_SPACING,
  axisComponent,
  vectorToAxis,
  type CubicGeometry,
  type Vec3,
} from "../render/lattice/cubic.js";
import type { MoveMap } from "./move-axis.js";
import { worldToScreen, type ScreenPoint } from "./screen.js";

/// Ported from rubik/src/render/input.c -- see that file's own constant
/// comments for the reasoning behind each number; kept identical rather
/// than re-tuned. All still valid at every size: bigger cubes are drawn
/// at the same overall world size as a 3x3x3 (docs/en/07-nxn-web-view.md
/// D2), so DRAG_RADIUS (the arc radius a screen drag is converted
/// through) stays the 3x3x3 value on purpose -- the cube, not one
/// cubie, is what the finger is rotating.
export const DRAG_DEADZONE_PX = 8.0;
export const DRAG_RADIUS = 1.5 * CUBIE_SPACING;
export const DRAG_MIN_PX_PER_UNIT = 20.0;
export const DRAG_FLICK_LOOKAHEAD = 0.08;
const DRAG_BLOCK_DEG = 12.0;

function vec2Sub(a: ScreenPoint, b: ScreenPoint): ScreenPoint {
  return { x: a.x - b.x, y: a.y - b.y };
}
function vec2Dot(a: ScreenPoint, b: ScreenPoint): number {
  return a.x * b.x + a.y * b.y;
}
function vec2Length(a: ScreenPoint): number {
  return Math.sqrt(vec2Dot(a, a));
}
function clamp(value: number, lo: number, hi: number): number {
  return Math.min(Math.max(value, lo), hi);
}

/// One finger/mouse-button's worth of drag-to-turn state -- ported from
/// rubik's t_drag_state (include/render/input.h); see that file's own
/// long field-by-field comment for why each one exists. `active`: a
/// valid pick is down. `locked`: past the dead zone -- axis/layer/sign/
/// chosen are now fixed for the rest of this drag (never re-decided).
/// `blocked`: locked onto a layer no generator turns (a 3x3x3's middle
/// slices) -- still tracked with a rubber-band feel, always settles to
/// 0. `cubiePos`/`layer` are lattice indices (0..size-1), the same
/// numbers core's generators and the scene use. `geometry`/`moves` are
/// the puzzle this drag started on, kept so later frames need nothing
/// else passed in.
export interface DragState {
  geometry: CubicGeometry;
  moves: MoveMap;
  active: boolean;
  locked: boolean;
  blocked: boolean;
  centerTurn: boolean;
  normal: Vec3;
  planeDir: [Vec3, Vec3];
  screenDir: [ScreenPoint, ScreenPoint];
  startPointer: ScreenPoint;
  cubiePos: [number, number, number];
  chosen: 0 | 1;
  axis: Axis;
  layer: number;
  sign: number;
  accumDeg: number;
  centerPrevAngleDeg: number;
  velDegPerSec: number;
}

function vec(x: number, y: number, z: number): Vec3 {
  return { x, y, z };
}

/// Casts `ray` against the single bounding box covering the whole
/// puzzle (geometry.halfExtent) and, if it hits, builds a fresh
/// DragState's picking fields -- ported from rubik's input_pick_start()
/// (render/input.c): "a seam click can no longer leak through to an
/// interior neighbour's face" (one box, not 26 per-cubie boxes). The
/// normal and cubie slot both come from the hit point itself.
///
/// @param pointerScreen This pointer's CURRENT screen position, in the
///        same pixel space as `width`/`height` -- becomes
///        DragState.startPointer, every later frame's displacement is
///        measured from here.
/// @return null if the ray hit nothing.
export function pickStart(
  ray: THREE.Ray,
  camera: THREE.Camera,
  width: number,
  height: number,
  pointerScreen: ScreenPoint,
  geometry: CubicGeometry,
  moves: MoveMap,
): DragState | null {
  const h = geometry.halfExtent;
  const box = new THREE.Box3(new THREE.Vector3(-h, -h, -h), new THREE.Vector3(h, h, h));
  const hitPoint = new THREE.Vector3();
  if (!ray.intersectBox(box, hitPoint)) {
    return null;
  }
  const hit: Vec3 = { x: hitPoint.x, y: hitPoint.y, z: hitPoint.z };
  const n = vectorToAxis(hit);
  const nComp = axisComponent(hit, n);
  const normalUnit = AXIS_UNIT[n];
  const normal = vec(normalUnit.x, normalUnit.y, normalUnit.z);
  const sign = nComp > 0 ? 1 : -1;
  if (n === Axis.X) normal.x = sign;
  else if (n === Axis.Y) normal.y = sign;
  else normal.z = sign;

  const cubiePos: [number, number, number] = [0, 0, 0];
  const planeDir: [Vec3, Vec3] = [vec(0, 0, 0), vec(0, 0, 0)];
  let k = 0;
  for (let i = 0; i < 3; i++) {
    const axis = [Axis.X, Axis.Y, Axis.Z][i]!;
    const comp = axisComponent(hit, axis);
    if (axis === n) {
      cubiePos[i] = comp > 0 ? geometry.size - 1 : 0;
    } else {
      cubiePos[i] = geometry.worldToCell(comp);
      const u = AXIS_UNIT[axis];
      planeDir[k] = vec(u.x, u.y, u.z);
      k++;
    }
  }

  const center = new THREE.Vector3(
    geometry.cellToWorld(cubiePos[0]),
    geometry.cellToWorld(cubiePos[1]),
    geometry.cellToWorld(cubiePos[2]),
  );
  const origin = worldToScreen(center, camera, width, height);
  const screenDir: [ScreenPoint, ScreenPoint] = [
    vec2Sub(worldToScreen(center.clone().add(new THREE.Vector3(planeDir[0].x, planeDir[0].y, planeDir[0].z)), camera, width, height), origin),
    vec2Sub(worldToScreen(center.clone().add(new THREE.Vector3(planeDir[1].x, planeDir[1].y, planeDir[1].z)), camera, width, height), origin),
  ];

  return {
    geometry,
    moves,
    active: true,
    locked: false,
    blocked: false,
    centerTurn: false,
    normal,
    planeDir,
    screenDir,
    startPointer: pointerScreen,
    cubiePos,
    chosen: 0,
    axis: Axis.X,
    layer: 0,
    sign: 1,
    accumDeg: 0,
    centerPrevAngleDeg: 0,
    velDegPerSec: 0,
  };
}

/// True when BOTH straight-line drag directions off the picked cubie
/// would land on a layer no generator turns -- generalised from rubik's
/// is_center_pick() ("a face's own dead-centre cubie"), which is exactly
/// what this reduces to on a 3x3x3. Such a pick gets the circular
/// "twist the face" treatment instead of only ever being `blocked`. On
/// bigger cubes every layer has a generator, so this is never true and
/// a centre drag turns the slice under the finger, as on a real cube.
function isCenterPick(state: DragState): boolean {
  const normalAxis = vectorToAxis(state.normal);
  for (const axis of [Axis.X, Axis.Y, Axis.Z]) {
    if (axis === normalAxis) continue;
    if (state.moves.layerHasGenerator(axis, state.cubiePos[axis])) return false;
  }
  return true;
}

/// Called once, the first frame a drag leaves the dead zone: picks
/// whichever of planeDir[0]/[1] the total displacement `d` best lines
/// up with, then derives a SIGNED rotation axis as normal x
/// planeDir[chosen] -- ported from rubik's lock_axis() (§1.1/B1: this
/// cross product, not a plane_right/plane_up split, is what keeps the
/// sign right for all 12 face/direction cases, not just 3 of them).
function lockAxis(state: DragState, d: ScreenPoint): void {
  const score: [number, number] = [0, 0];
  for (let k = 0; k < 2; k++) {
    const len = vec2Length(state.screenDir[k as 0 | 1]);
    score[k] = len > 1e-3 ? Math.abs(vec2Dot(d, state.screenDir[k as 0 | 1])) / len : 0;
  }
  state.chosen = score[1]! > score[0]! ? 1 : 0;
  const n = state.normal;
  const p = state.planeDir[state.chosen];
  // rot = normal x planeDir[chosen]
  const rot: Vec3 = {
    x: n.y * p.z - n.z * p.y,
    y: n.z * p.x - n.x * p.z,
    z: n.x * p.y - n.y * p.x,
  };
  state.axis = vectorToAxis(rot);
  state.sign = axisComponent(rot, state.axis) > 0 ? 1 : -1;
  state.layer = state.cubiePos[state.axis];
  state.blocked = !state.moves.layerHasGenerator(state.axis, state.layer);
  state.locked = true;
}

/// The clicked centre's angle around its own face, measured in the
/// (screenDir[0], screenDir[1]) basis, each leg scaled to px-per-world-
/// unit -- ported from rubik's center_angle_deg(). An approximation
/// good enough for an optional gesture, not a precision measurement
/// (that file's own comment).
function centerAngleDeg(state: DragState, d: ScreenPoint): number {
  const len0 = Math.max(vec2Length(state.screenDir[0]), DRAG_MIN_PX_PER_UNIT);
  const len1 = Math.max(vec2Length(state.screenDir[1]), DRAG_MIN_PX_PER_UNIT);
  const u = vec2Dot(d, state.screenDir[0]) / len0;
  const v = vec2Dot(d, state.screenDir[1]) / len1;
  return Math.atan2(v, u) * (180 / Math.PI);
}

/// Called once, the first frame a centre-pick drag leaves the dead
/// zone -- ported from rubik's lock_center(): axis/layer are the
/// face's OWN axis/layer (there is no "other" layer to resolve for a
/// centre pick), accumDeg starts at 0 sweep rather than a linear angle.
function lockCenter(state: DragState, d: ScreenPoint): void {
  state.centerTurn = true;
  state.axis = vectorToAxis(state.normal);
  state.layer = state.cubiePos[state.axis];
  state.blocked = false;
  state.centerPrevAngleDeg = centerAngleDeg(state, d);
  state.locked = true;
}

/// Per-frame update for a centre-pick drag: measures the SIGNED change
/// in angle since last frame (never an absolute angle, which would
/// jump at +/-180) and accumulates it -- ported from rubik's
/// update_center_angle().
function updateCenterAngle(state: DragState, d: ScreenPoint, dt: number): void {
  const angle = centerAngleDeg(state, d);
  let delta = angle - state.centerPrevAngleDeg;
  if (delta > 180) delta -= 360;
  else if (delta < -180) delta += 360;
  state.centerPrevAngleDeg = angle;
  const prev = state.accumDeg;
  state.accumDeg = clamp(state.accumDeg + delta, -180, 180);
  if (dt > 0) {
    state.velDegPerSec = 0.7 * state.velDegPerSec + 0.3 * ((state.accumDeg - prev) / dt);
  }
}

/// Per-frame update for a normal (non-centre) drag: total displacement
/// `d` projected onto the locked planeDir, turned into a world-space
/// arc angle, rubber-banded if `blocked` -- ported from rubik's
/// update_linear_angle().
function updateLinearAngle(state: DragState, d: ScreenPoint, dt: number): void {
  const sd = state.screenDir[state.chosen];
  const len2 = Math.max(vec2Dot(sd, sd), DRAG_MIN_PX_PER_UNIT * DRAG_MIN_PX_PER_UNIT);
  let raw = (state.sign * (vec2Dot(d, sd) / len2)) / DRAG_RADIUS * (180 / Math.PI);
  raw = clamp(raw, -180, 180);
  if (state.blocked) {
    raw = DRAG_BLOCK_DEG * Math.tanh(raw / DRAG_BLOCK_DEG);
  }
  const prev = state.accumDeg;
  state.accumDeg = raw;
  if (dt > 0) {
    state.velDegPerSec = 0.7 * state.velDegPerSec + 0.3 * ((raw - prev) / dt);
  }
}

/// Advances an active drag by one frame -- ported from rubik's
/// input_pick_drag(). No-op if `state.active` is false.
export function pickDrag(state: DragState, pointerScreen: ScreenPoint, dt: number): void {
  if (!state.active) return;
  const d = vec2Sub(pointerScreen, state.startPointer);
  if (!state.locked) {
    if (vec2Length(d) < DRAG_DEADZONE_PX) return;
    if (isCenterPick(state)) lockCenter(state, d);
    else lockAxis(state, d);
  }
  if (state.centerTurn) updateCenterAngle(state, d, dt);
  else updateLinearAngle(state, d, dt);
}

export interface DragRelease {
  fromDeg: number;
  toDeg: number;
  move: { generator: number; amount: number } | null;
}

/// Ends the drag: snaps the accumulated angle (plus a short velocity
/// look-ahead) to the nearest quarter turn and resolves it to a real
/// move -- ported from rubik's input_pick_release(). `move` is null if
/// the drag never left the dead zone, is blocked (no generator), or
/// snapped to 0 -- matching that function's MOVE_COUNT return.
export function pickRelease(state: DragState): DragRelease {
  state.active = false;
  const fromDeg = state.accumDeg;
  if (!state.locked || state.blocked) {
    return { fromDeg, toDeg: 0, move: null };
  }
  const quarters = Math.round((state.accumDeg + state.velDegPerSec * DRAG_FLICK_LOOKAHEAD) / 90);
  const toDeg = clamp(quarters, -2, 2) * 90;
  if (toDeg === 0) {
    return { fromDeg, toDeg: 0, move: null };
  }
  return { fromDeg, toDeg, move: state.moves.moveForTurn(state.axis, state.layer, toDeg) };
}

/// Cancels an in-progress drag with no move -- ported from rubik's
/// input_pick_cancel(). Call on any interruption (pointer lost, a
/// second pointer joins and hands off to orbit, etc.) so a drag can
/// never get stuck following a pointer that's no longer down.
export function pickCancel(state: DragState): void {
  state.active = false;
  state.locked = false;
  state.centerTurn = false;
  state.accumDeg = 0;
}

/// The layer currently mid-turn by this drag, for the scene's
/// per-frame pose update -- ported from rubik's
/// input_drag_get_active_turn(). Inactive until the drag is past the
/// dead zone (`locked`), so a small, still-undecided drag never shows a
/// phantom rotation.
export function dragActiveTurn(state: DragState): { axis: Axis; layer: number; angleDeg: number } | null {
  if (!state.active || !state.locked) return null;
  return { axis: state.axis, layer: state.layer, angleDeg: state.accumDeg };
}
