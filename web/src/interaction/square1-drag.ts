import * as THREE from "three";
import type { PuzzleHandle } from "../engine/puzzle-handle.js";
import { SQ1_GEN_BOTTOM, SQ1_GEN_SLICE, SQ1_GEN_TOP } from "../engine/types.js";
import type { ActiveTurn, TurnTarget } from "../render/turn.js";
import { BLOCKED_SLICE_WOBBLE_DEG, type Square1Hit, type Square1Scene } from "../render/square1/square1-scene.js";
import type { DragAdapter, TurnDragSession } from "./drag-adapter.js";
import { BOTTOM_Y0, TOP_Y1, cutNormal } from "../render/square1/square1-geometry.js";
import { SQ1_SLICE_DEG, SQ1_STEP_DEG, type Square1MoveModel } from "./square1-moves.js";
import type { ScreenPoint } from "./screen.js";

/// Pixels a drag must travel before it is decided what it is.
export const SQ1_DEAD_ZONE_PX = 10;
/// Degrees of slice per pixel dragged along the piece's own path.
export const SQ1_SLICE_DEG_PER_PX = 1;
/// A legal slice commits once it is past halfway (90 of its 180
/// degrees), the same rule a layer turn has.
export const SQ1_SLICE_COMMIT_DEG = SQ1_SLICE_DEG / 2;

/// How near a hit's height must be to a layer's outer plane to count as
/// on its cap rather than its side.
const CAP_TOLERANCE = 0.02;

type Phase = "undecided" | "layer" | "slice" | "orbit";

function wrapDeg(deg: number): number {
  return ((((deg + 180) % 360) + 360) % 360) - 180;
}

/// Which way the world vector `delta` at world point `at` runs on the
/// screen, as a unit vector (or null if it is end-on).
function screenDirection(at: THREE.Vector3, delta: THREE.Vector3, camera: THREE.Camera, width: number, height: number): ScreenPoint | null {
  const project = (p: THREE.Vector3): ScreenPoint => {
    const ndc = p.clone().project(camera);
    return { x: ((ndc.x + 1) / 2) * width, y: ((1 - ndc.y) / 2) * height };
  };
  const a = project(at);
  const b = project(at.clone().add(delta));
  const length = Math.hypot(b.x - a.x, b.y - a.y);
  return length < 1e-6 ? null : { x: (b.x - a.x) / length, y: (b.y - a.y) / length };
}

/// One Square-1 gesture (docs/en/12-phase5-square1.md D6). Dragged on a
/// top or bottom piece, along the layer, it turns that layer live (the
/// angle follows the finger around the vertical axis) and snaps to 30
/// degrees on release. Dragged up or down on a right-half piece, or at all
/// on the right half of the middle layer, it poses the slice; a blocked
/// slice stops at a small wobble. The left half never slices, and the
/// middle layer never turns sideways: those drags orbit the camera.
class Square1DragSession implements TurnDragSession {
  readonly #puzzle: PuzzleHandle;
  readonly #moves: Square1MoveModel;
  readonly #hit: Square1Hit;
  readonly #camera: THREE.Camera;
  readonly #width: number;
  readonly #height: number;
  readonly #start: ScreenPoint;
  readonly #layerGenerator: number | null;
  #phase: Phase = "undecided";
  #accumDeg = 0;
  #lastLayerDeg = 0;

  constructor(
    puzzle: PuzzleHandle,
    moves: Square1MoveModel,
    hit: Square1Hit,
    camera: THREE.Camera,
    width: number,
    height: number,
    start: ScreenPoint,
  ) {
    this.#puzzle = puzzle;
    this.#moves = moves;
    this.#hit = hit;
    this.#camera = camera;
    this.#width = width;
    this.#height = height;
    this.#start = start;
    this.#layerGenerator = hit.layer === "top" ? SQ1_GEN_TOP : hit.layer === "bottom" ? SQ1_GEN_BOTTOM : null;
  }

  get locked(): boolean {
    return this.#phase === "layer" || this.#phase === "slice";
  }

  get wantsOrbit(): boolean {
    return this.#phase === "orbit";
  }

  /// The generator being posed, or null while undecided / orbiting.
  get #generator(): number | null {
    if (this.#phase === "layer") return this.#layerGenerator;
    if (this.#phase === "slice") return SQ1_GEN_SLICE;
    return null;
  }

  #rayAt(pointer: ScreenPoint): THREE.Ray {
    const ndc = new THREE.Vector2((pointer.x / this.#width) * 2 - 1, -(pointer.y / this.#height) * 2 + 1);
    const raycaster = new THREE.Raycaster();
    raycaster.setFromCamera(ndc, this.#camera);
    return raycaster.ray;
  }

  /// The angle (degrees, right-handed about +Y) the hit point has been
  /// swung through, read where the pointer's ray meets the horizontal
  /// plane through the hit point.
  #layerAngle(pointer: ScreenPoint): number | null {
    const ray = this.#rayAt(pointer);
    const dy = ray.direction.y;
    if (Math.abs(dy) < 1e-4) return null;
    const t = (this.#hit.point.y - ray.origin.y) / dy;
    if (t <= 0) return null;
    const x2 = ray.origin.x + ray.direction.x * t;
    const z2 = ray.origin.z + ray.direction.z * t;
    const { x: x1, z: z1 } = this.#hit.point;
    return (Math.atan2(-(x1 * z2 - z1 * x2), x1 * x2 + z1 * z2) * 180) / Math.PI;
  }

  #decide(dx: number, dy: number): void {
    const at = this.#hit.point;
    const along = (v: ScreenPoint | null): number => (v ? Math.abs(dx * v.x + dy * v.y) : 0);
    if (this.#layerGenerator === null) {
      // The middle layer: only its right half moves (the slice), and
      // only when dragged up or down.
      const vertical = screenDirection(at, new THREE.Vector3(0, 0.1, 0), this.#camera, this.#width, this.#height);
      const sideways = screenDirection(at, new THREE.Vector3(at.z, 0, -at.x).setLength(0.1), this.#camera, this.#width, this.#height);
      this.#phase = this.#hit.half === "right" && along(vertical) >= along(sideways) ? "slice" : "orbit";
      return;
    }
    // On a cap, "up the screen" is mostly "away from you" along the
    // layer, not up the puzzle: the two read the same on screen and
    // would be guessed at. A cap is for turning its layer; the slice is
    // taken by the sides and the middle layer.
    const onCap = this.#hit.layer === "top" ? at.y > TOP_Y1 - CAP_TOLERANCE : at.y < BOTTOM_Y0 + CAP_TOLERANCE;
    if (onCap) {
      this.#phase = "layer";
      this.#lastLayerDeg = 0;
      return;
    }
    const tangent = screenDirection(at, new THREE.Vector3(at.z, 0, -at.x).setLength(0.1), this.#camera, this.#width, this.#height);
    const vertical = screenDirection(at, new THREE.Vector3(0, 0.1, 0), this.#camera, this.#width, this.#height);
    if (along(tangent) >= along(vertical)) {
      this.#phase = "layer";
      this.#lastLayerDeg = 0;
    } else {
      this.#phase = this.#hit.half === "right" ? "slice" : "orbit";
    }
  }

  /// How far a slice is posed by a drag of (dx, dy) pixels: along the
  /// direction the touched point itself would travel.
  #sliceAngle(dx: number, dy: number): number {
    const at = this.#hit.point;
    const [nx, nz] = cutNormal();
    const axis = new THREE.Vector3(nx, 0, nz);
    const velocity = new THREE.Vector3().crossVectors(axis, at).setLength(0.1);
    const path =
      screenDirection(at, velocity, this.#camera, this.#width, this.#height) ??
      screenDirection(at, new THREE.Vector3(0, 0.1, 0), this.#camera, this.#width, this.#height) ?? { x: 0, y: -1 };
    const raw = (dx * path.x + dy * path.y) * SQ1_SLICE_DEG_PER_PX;
    const clamped = Math.max(-SQ1_SLICE_DEG, Math.min(SQ1_SLICE_DEG, raw));
    if (this.#puzzle.isMoveLegal(SQ1_GEN_SLICE, 1)) return clamped;
    return Math.max(-BLOCKED_SLICE_WOBBLE_DEG, Math.min(BLOCKED_SLICE_WOBBLE_DEG, clamped));
  }

  drag(pointer: ScreenPoint, _dtSec: number): void {
    const dx = pointer.x - this.#start.x;
    const dy = pointer.y - this.#start.y;
    if (this.#phase === "undecided") {
      if (Math.hypot(dx, dy) < SQ1_DEAD_ZONE_PX) return;
      this.#decide(dx, dy);
    }
    if (this.#phase === "layer") {
      const angle = this.#layerAngle(pointer);
      if (angle !== null) {
        this.#accumDeg += wrapDeg(angle - this.#lastLayerDeg);
        this.#lastLayerDeg = angle;
      }
    } else if (this.#phase === "slice") {
      this.#accumDeg = this.#sliceAngle(dx, dy);
    }
  }

  activeTurn(): ActiveTurn | null {
    const generator = this.#generator;
    return generator === null ? null : { generator, angleDeg: this.#accumDeg };
  }

  #settleTarget(): TurnTarget | null {
    const generator = this.#generator;
    return generator === null ? null : { generator };
  }

  release(): { fromDeg: number; toDeg: number; move: { generator: number; amount: number } | null; settle: TurnTarget | null } {
    const generator = this.#generator;
    const fromDeg = this.#accumDeg;
    if (generator === null) return { fromDeg: 0, toDeg: 0, move: null, settle: null };
    if (generator === SQ1_GEN_SLICE) {
      const commit = Math.abs(fromDeg) > SQ1_SLICE_COMMIT_DEG && this.#puzzle.isMoveLegal(SQ1_GEN_SLICE, 1);
      return commit
        ? { fromDeg, toDeg: Math.sign(fromDeg) * SQ1_SLICE_DEG, move: { generator, amount: 1 }, settle: null }
        : { fromDeg, toDeg: 0, move: null, settle: { generator } };
    }
    const steps = Math.round(fromDeg / SQ1_STEP_DEG); // right-handed units
    // Top: clockwise from above (negative) counts up; bottom: clockwise
    // from below is anticlockwise from above (positive).
    const move = this.#moves.moveForUnits(generator, generator === SQ1_GEN_TOP ? -steps : steps);
    return move
      ? { fromDeg, toDeg: steps * SQ1_STEP_DEG, move, settle: null }
      : { fromDeg, toDeg: 0, move: null, settle: { generator } };
  }

  cancel(): { fromDeg: number; settle: TurnTarget | null } {
    return { fromDeg: this.#accumDeg, settle: this.#settleTarget() };
  }
}

export function square1DragAdapter(scene: Square1Scene, puzzle: PuzzleHandle, moves: Square1MoveModel): DragAdapter {
  return {
    start(ray, camera, width, height, pointer) {
      const hit = scene.pick(ray);
      // The frame (the left half of the middle layer) is never turned or
      // sliced: a drag on it orbits the camera.
      if (!hit || (hit.layer === "middle" && hit.half === "left")) return null;
      return new Square1DragSession(puzzle, moves, hit, camera, width, height, pointer);
    },
  };
}
