import * as THREE from "three";
import type { DragAdapter, TurnDragSession } from "./drag-adapter.js";
import type { TurnAnimation, TurnOptions } from "./turn-anim.js";
import type { Move } from "../engine/types.js";
import {
  DEFAULT_DISTANCE_LIMITS,
  applyOrbitToCamera,
  createOrbitCamera,
  orbitDrag,
  orbitNudge,
  orbitZoom,
  type DistanceLimits,
  type OrbitCamera,
} from "./orbit-camera.js";
import { DoubleTapDetector, isTap } from "./double-tap.js";
import { rescaleDistance, zoomLimits } from "./framing.js";
import { OrbitInertia } from "./inertia.js";
import { PINCH_REFERENCE_FIT, pinchZoomDelta, wheelZoomDelta } from "./zoom-input.js";
import type { ScreenPoint } from "./screen.js";
import type { TurnScene } from "../render/turn.js";

/// docs/en/03-architecture.md S7's decided convention (DECISIONS.md,
/// 2026-10-01, "Touch camera gestures revised" supersedes the original
/// "two-finger-drag orbits"): a one-finger drag that starts ON the
/// cube turns a layer (rubik's own left-drag); a one-finger drag that
/// starts OUTSIDE the cube orbits the camera instead -- no second
/// finger needed. Two fingers are reserved for pinch-zoom only: zoom
/// and orbit used to be computed off the same two-finger gesture
/// (centroid delta -> orbit, spread delta -> zoom) and incidental
/// centroid drift during a pinch fought with the zoom itself, feeling
/// janky -- see #onPointerMove's mode==="orbit" branch, which now
/// drives orbitDrag() only from a single pointer and orbitZoom() only
/// from two or more, never both from the same move event. Pointer
/// Events give mouse and touch the same event names (§7: "a pointer's
/// screen-space delta is a screen-space delta whether it came from a
/// mouse or a finger"), so a plain mouse drag off the cube IS the
/// orbit case -- no separate mouse code path.
///
/// How finger-count changes mid-gesture are handled is this project's
/// own call (rubik has no multitouch at all to port): adding a second
/// finger while a turn-drag is in progress cancels that drag (eased
/// back to rest as a "settle") and switches to orbiting; once in orbit
/// mode, this gesture stays in orbit mode until every pointer lifts,
/// rather than re-deciding every frame.
type Mode = "none" | "turn" | "orbit";

/// What the host can tune without PointerController knowing about the
/// page around it.
export interface PointerControllerOptions {
  /// Called whenever input changes what should be on screen, so the
  /// render-on-demand loop (render/frame-scheduler.ts) wakes up.
  onActivity?: () => void;
  /// Orbit inertia (a flick keeps turning). Off for reduced motion.
  inertia?: boolean;
  /// The distance at which the puzzle fits the free screen area
  /// (interaction/framing.ts). Zoom limits and pinch/wheel sensitivity
  /// follow it. Omitted: the pre-Phase-6 fixed camera distance.
  fitDistance?: number;
  /// When `orbit` is handed over from a previous controller, the fit its
  /// distance was relative to, so the user's zoom ratio carries across a
  /// puzzle change. Defaults to `fitDistance` (no rescale).
  orbitFitDistance?: number;
  /// Asked when a drag commits a move, for the options its animation
  /// should carry (ManualPlay records the move and hooks the landing).
  /// Omitted: the move is animated with no extras.
  commitOptions?: (move: Move) => TurnOptions | undefined;
}

export class PointerController {
  #canvas: HTMLCanvasElement;
  #camera: THREE.PerspectiveCamera;
  #scene: TurnScene;
  #adapter: DragAdapter;
  #anim: TurnAnimation;
  #orbit: OrbitCamera;
  #pointers = new Map<number, ScreenPoint>();
  #mode: Mode = "none";
  #drag: TurnDragSession | null = null;
  #turnPointerId: number | null = null;
  #orbitPrevCentroid: ScreenPoint | null = null;
  #orbitPrevSpread = 0;
  #lastMoveTimeStamp: number | undefined;
  #lastOrbitTimeStamp: number | undefined;
  #onActivity: () => void;
  #commitOptions: ((move: Move) => TurnOptions | undefined) | undefined;
  #inertia: OrbitInertia;
  #doubleTap = new DoubleTapDetector();
  /// The fitted distance and the zoom range that follows from it.
  #fit: number;
  #limits: DistanceLimits;
  /// Where and when the current single-pointer orbit gesture began, if
  /// it began on empty space -- the first half of a tap.
  #orbitDown: { point: ScreenPoint; timeMs: number } | null = null;
  /// True while the orbit gesture has only ever had one pointer, so a
  /// release may glide and may count as a tap.
  #orbitSingle = false;

  /// Takes an already-built TurnAnimation rather than constructing its
  /// own (as this did before docs/en/06-phase2-solvers.md's web UI):
  /// TurnAnimation is a single animation slot, and SolvePlayback
  /// (interaction/solve-playback.ts) needs to drive that SAME slot for
  /// queued solve moves -- two independent TurnAnimation instances over
  /// the same puzzle/scene would each think they're idle and could
  /// start conflicting animations at once. main.ts now owns the one
  /// TurnAnimation and hands it to both this controller and
  /// SolvePlayback.
  ///
  /// `orbit` lets a size change keep the user's current camera angle:
  /// pass the previous controller's `.orbit` instead of starting fresh.
  constructor(
    canvas: HTMLCanvasElement,
    camera: THREE.PerspectiveCamera,
    scene: TurnScene,
    adapter: DragAdapter,
    anim: TurnAnimation,
    orbit?: OrbitCamera,
    options: PointerControllerOptions = {},
  ) {
    this.#fit = options.fitDistance ?? PINCH_REFERENCE_FIT;
    this.#limits = options.fitDistance === undefined ? DEFAULT_DISTANCE_LIMITS : zoomLimits(options.fitDistance);
    this.#onActivity = options.onActivity ?? ((): void => {});
    this.#commitOptions = options.commitOptions;
    this.#inertia = new OrbitInertia(options.inertia ?? true);
    if (orbit) {
      // A handed-over orbit keeps its angles and its zoom *ratio*.
      orbit.distance = rescaleDistance(orbit.distance, options.orbitFitDistance ?? this.#fit, this.#fit);
      this.#orbit = orbit;
    } else {
      this.#orbit = createOrbitCamera();
      this.#orbit.distance = this.#fit;
    }
    this.#canvas = canvas;
    this.#camera = camera;
    this.#scene = scene;
    this.#adapter = adapter;
    this.#anim = anim;
    applyOrbitToCamera(this.#orbit, this.#camera);

    canvas.style.touchAction = "none"; // this project reads every gesture itself
    canvas.addEventListener("pointerdown", this.#onPointerDown);
    canvas.addEventListener("pointermove", this.#onPointerMove);
    canvas.addEventListener("pointerup", this.#onPointerUp);
    canvas.addEventListener("pointercancel", this.#onPointerUp);
    // Not passive: the wheel must not also scroll or zoom the page.
    canvas.addEventListener("wheel", this.#onWheel, { passive: false });
  }

  /// The camera orbit state, for handing to a successor controller.
  get orbit(): OrbitCamera {
    return this.#orbit;
  }

  /// The fitted distance this controller zooms relative to.
  get fitDistance(): number {
    return this.#fit;
  }

  /// Whether anything still moves, so the frame loop must keep running:
  /// a finger down, a turn animating, or a flick gliding.
  isBusy(): boolean {
    return this.#mode !== "none" || !this.#anim.isIdle() || this.#inertia.active;
  }

  /// The puzzle's fitted distance changed (a resize, a panel opening, a
  /// different puzzle). Keeps the user's zoom ratio and moves the zoom
  /// range with it.
  setFit(fitDistance: number): void {
    if (fitDistance === this.#fit) return;
    this.#orbit.distance = rescaleDistance(this.#orbit.distance, this.#fit, fitDistance);
    this.#fit = fitDistance;
    this.#limits = zoomLimits(fitDistance);
    this.#onActivity();
  }

  /// Turns the view by a fixed step (the arrow keys). Stops any glide so
  /// the step lands where it says.
  nudgeOrbit(yawDeg: number, pitchDeg: number): void {
    orbitNudge(this.#orbit, yawDeg, pitchDeg);
    this.#inertia.stop();
    this.#onActivity();
  }

  /// Back to the starting view: default angles, fitted distance.
  resetView(): void {
    const start = createOrbitCamera();
    this.#orbit.yawDeg = start.yawDeg;
    this.#orbit.pitchDeg = start.pitchDeg;
    this.#orbit.distance = this.#fit;
    this.#inertia.stop();
    this.#onActivity();
  }

  /// Detaches every listener this controller added -- a size change
  /// (docs/en/07-nxn-web-view.md D5) builds a fresh controller on the
  /// same canvas, and the old one must stop reacting to it.
  dispose(): void {
    this.#canvas.removeEventListener("pointerdown", this.#onPointerDown);
    this.#canvas.removeEventListener("pointermove", this.#onPointerMove);
    this.#canvas.removeEventListener("pointerup", this.#onPointerUp);
    this.#canvas.removeEventListener("pointercancel", this.#onPointerUp);
    this.#canvas.removeEventListener("wheel", this.#onWheel);
    this.#inertia.stop();
    this.#drag = null;
    this.#pointers.clear();
    this.#mode = "none";
  }

  /// Called once per animation frame from main.ts's render loop:
  /// advances any in-flight turn-release/settle animation, keeps a
  /// still-live drag's pose current, and writes the orbit camera's
  /// position -- the TS equivalent of rubik's render_run() frame order
  /// (orbit update, anim update, then draw reads whichever is active).
  update(dtSec: number): void {
    this.#anim.update(dtSec);
    if (this.#mode === "turn" && this.#drag) {
      this.#scene.setActiveTurn(this.#drag.activeTurn());
    }
    this.#inertia.step(dtSec, this.#orbit);
    applyOrbitToCamera(this.#orbit, this.#camera);
  }

  #screenPoint(event: PointerEvent): ScreenPoint {
    const rect = this.#canvas.getBoundingClientRect();
    return { x: event.clientX - rect.left, y: event.clientY - rect.top };
  }

  #rayFrom(point: ScreenPoint): THREE.Ray {
    const rect = this.#canvas.getBoundingClientRect();
    const ndc = new THREE.Vector2((point.x / rect.width) * 2 - 1, -(point.y / rect.height) * 2 + 1);
    const raycaster = new THREE.Raycaster();
    raycaster.setFromCamera(ndc, this.#camera);
    return raycaster.ray;
  }

  #centroidAndSpread(): { centroid: ScreenPoint; spread: number } {
    const points = [...this.#pointers.values()];
    const centroid = {
      x: points.reduce((sum, p) => sum + p.x, 0) / points.length,
      y: points.reduce((sum, p) => sum + p.y, 0) / points.length,
    };
    const spread = points.reduce((sum, p) => sum + Math.hypot(p.x - centroid.x, p.y - centroid.y), 0) / points.length;
    return { centroid, spread };
  }

  #onWheel = (event: WheelEvent): void => {
    event.preventDefault();
    orbitZoom(this.#orbit, wheelZoomDelta(event.deltaY, event.deltaMode, this.#fit), this.#limits);
    this.#onActivity();
  };

  #onPointerDown = (event: PointerEvent): void => {
    this.#canvas.setPointerCapture(event.pointerId);
    this.#pointers.set(event.pointerId, this.#screenPoint(event));
    // A touch stops a glide dead, and every input wakes the frame loop.
    this.#inertia.stop();
    this.#onActivity();

    if (this.#pointers.size === 1 && this.#mode === "none") {
      const rect = this.#canvas.getBoundingClientRect();
      const point = this.#screenPoint(event);
      // Geometric only -- whether anim is mid-flight doesn't change
      // where the ray hits, only whether a hit is allowed to start a
      // turn (below).
      const drag = this.#adapter.start(this.#rayFrom(point), this.#camera, rect.width, rect.height, point);
      if (drag && this.#anim.isIdle()) {
        this.#drag = drag;
        this.#mode = "turn";
        this.#turnPointerId = event.pointerId;
      } else if (!drag) {
        // Missed the cube entirely -- orbit with just this one pointer,
        // regardless of anim state (orbiting the camera doesn't
        // conflict with an in-flight turn, same as two-finger orbit
        // never checked anim state either).
        this.#mode = "orbit";
        this.#orbitSingle = true;
        this.#orbitDown = { point, timeMs: event.timeStamp };
        this.#lastOrbitTimeStamp = event.timeStamp;
        const { centroid, spread } = this.#centroidAndSpread();
        this.#orbitPrevCentroid = centroid;
        this.#orbitPrevSpread = spread;
      }
      // else: hit the cube, but a turn can't start right now (anim
      // mid-flight) -- stays inert until released, same as before this
      // gesture change.
      return;
    }
    if (this.#pointers.size === 2) {
      if (this.#mode === "turn") {
        this.#settleDrag();
      }
      this.#mode = "orbit";
      this.#orbitSingle = false;
      this.#orbitDown = null;
      const { centroid, spread } = this.#centroidAndSpread();
      this.#orbitPrevCentroid = centroid;
      this.#orbitPrevSpread = spread;
    }
  };

  #onPointerMove = (event: PointerEvent): void => {
    if (!this.#pointers.has(event.pointerId)) return;
    this.#pointers.set(event.pointerId, this.#screenPoint(event));

    if (this.#mode === "turn" && this.#drag && event.pointerId === this.#turnPointerId) {
      const dtSec = Math.max(event.timeStamp - (this.#lastMoveTimeStamp ?? event.timeStamp), 0) / 1000;
      this.#lastMoveTimeStamp = event.timeStamp;
      this.#drag.drag(this.#screenPoint(event), dtSec);
      this.#onActivity();
      if (this.#drag.wantsOrbit) {
        this.#drag = null;
        this.#turnPointerId = null;
        this.#mode = "orbit";
        this.#orbitSingle = this.#pointers.size === 1;
        this.#orbitDown = null;
        this.#lastOrbitTimeStamp = event.timeStamp;
        const { centroid, spread } = this.#centroidAndSpread();
        this.#orbitPrevCentroid = centroid;
        this.#orbitPrevSpread = spread;
      }
      return;
    }
    if (this.#mode === "orbit") {
      const { centroid, spread } = this.#centroidAndSpread();
      // One pointer drags (yaw/pitch); two or more pinch-zoom -- never
      // both from the same move event (see the class doc comment on
      // why mixing them felt janky).
      if (this.#pointers.size === 1) {
        if (this.#orbitPrevCentroid) {
          const dx = centroid.x - this.#orbitPrevCentroid.x;
          const dy = centroid.y - this.#orbitPrevCentroid.y;
          orbitDrag(this.#orbit, dx, dy);
          const dtSec = Math.max(event.timeStamp - (this.#lastOrbitTimeStamp ?? event.timeStamp), 0) / 1000;
          this.#inertia.track(dx, dy, dtSec, event.timeStamp);
        }
      } else {
        orbitZoom(this.#orbit, pinchZoomDelta(spread - this.#orbitPrevSpread, this.#fit), this.#limits);
      }
      this.#lastOrbitTimeStamp = event.timeStamp;
      this.#orbitPrevCentroid = centroid;
      this.#orbitPrevSpread = spread;
      this.#onActivity();
    }
  };

  #onPointerUp = (event: PointerEvent): void => {
    const point = this.#pointers.get(event.pointerId) ?? this.#screenPoint(event);
    this.#pointers.delete(event.pointerId);
    this.#onActivity();

    if (this.#mode === "turn" && event.pointerId === this.#turnPointerId) {
      this.#releaseDrag();
    }
    if (this.#pointers.size === 0) {
      if (this.#mode === "orbit" && this.#orbitSingle) {
        this.#finishSingleOrbit(point, event.timeStamp, event.type === "pointerup");
      }
      this.#mode = "none";
      this.#orbitPrevCentroid = null;
      this.#orbitDown = null;
      this.#orbitSingle = false;
      this.#lastMoveTimeStamp = undefined;
      this.#lastOrbitTimeStamp = undefined;
    } else if (this.#mode === "orbit") {
      // A finger lifted mid-orbit/pinch (e.g. 2 -> 1) -- re-baseline
      // against whichever pointer(s) remain, so the next move doesn't
      // jump from the old centroid/spread to the new one.
      const { centroid, spread } = this.#centroidAndSpread();
      this.#orbitPrevCentroid = centroid;
      this.#orbitPrevSpread = spread;
    }
  };

  /// A single-pointer orbit ended: let a flick glide, and treat a short
  /// still press on empty space as a tap -- two of them reset the view.
  /// A cancelled pointer (the system took the touch) is neither.
  #finishSingleOrbit(point: ScreenPoint, timeMs: number, completed: boolean): void {
    if (!completed) {
      this.#inertia.stop();
      return;
    }
    this.#inertia.release(timeMs);
    const down = this.#orbitDown;
    if (down && isTap(down, { point, timeMs }) && this.#doubleTap.tap(point, timeMs)) {
      this.resetView();
    }
  }

  /// Resolves the in-progress drag into a committed move (or a settle
  /// if it snapped to 0 / never locked) and hands off to TurnAnimation
  /// -- ported from rubik's input_pick_release() + the anim_begin_from/
  /// anim_begin_settle hand-off (§5.3/B5).
  #releaseDrag(): void {
    const drag = this.#drag;
    this.#drag = null;
    this.#turnPointerId = null;
    if (!drag) return;
    const { fromDeg, toDeg, move, settle } = drag.release();
    if (move) {
      this.#anim.beginFrom(move, fromDeg, toDeg, this.#commitOptions?.(move));
    } else if (settle) {
      this.#anim.beginSettleTarget(settle, fromDeg);
    }
  }

  /// Cancels the in-progress drag with no move, easing back to rest if
  /// it had locked onto a turn -- used when a second finger joins
  /// mid-turn-drag (this project's own hand-off rule, see the class
  /// doc comment).
  #settleDrag(): void {
    const drag = this.#drag;
    this.#drag = null;
    this.#turnPointerId = null;
    if (!drag) return;
    const { fromDeg, settle } = drag.cancel();
    if (settle) {
      this.#anim.beginSettleTarget(settle, fromDeg);
    }
  }
}
