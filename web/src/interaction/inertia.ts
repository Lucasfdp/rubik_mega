import { orbitDrag, type OrbitCamera } from "./orbit-camera.js";

/// Orbit inertia (docs/en/13-phase6-polish.md D6): a flick keeps the
/// view turning and slows to a stop, as a native scroll view does. It
/// must stop dead on the next touch, which is `stop()`.

/// Time constant of the slow-down, in seconds: speed falls to 1/e of
/// itself every 0.3 s, so a flick glides a little under a second.
export const INERTIA_TIME_CONSTANT_SEC = 0.3;
/// A release slower than this (pixels per second) does not glide.
export const INERTIA_START_SPEED = 80;
/// Gliding stops once the speed falls below this.
export const INERTIA_STOP_SPEED = 8;
/// A pointer that has been still this long (ms) before release is a
/// deliberate stop, not a flick, whatever its last move said.
export const INERTIA_STALE_MS = 80;
/// Weight of the newest sample in the smoothed velocity.
const NEW_SAMPLE_WEIGHT = 0.6;

export class OrbitInertia {
  #enabled: boolean;
  #vx = 0;
  #vy = 0;
  #lastMoveMs = 0;
  #gliding = false;

  /// `enabled` false (reduced motion) makes every call a no-op.
  constructor(enabled = true) {
    this.#enabled = enabled;
  }

  /// Whether the view is still gliding, so the frame loop must keep running.
  get active(): boolean {
    return this.#gliding;
  }

  /// Records one pointer move of (dxPx, dyPx) over `dtSec` seconds, ending at `nowMs`.
  track(dxPx: number, dyPx: number, dtSec: number, nowMs: number): void {
    if (!this.#enabled || dtSec <= 0) return;
    this.#vx = this.#vx * (1 - NEW_SAMPLE_WEIGHT) + (dxPx / dtSec) * NEW_SAMPLE_WEIGHT;
    this.#vy = this.#vy * (1 - NEW_SAMPLE_WEIGHT) + (dyPx / dtSec) * NEW_SAMPLE_WEIGHT;
    this.#lastMoveMs = nowMs;
  }

  /// The pointer lifted at `nowMs`: start gliding if it was moving fast.
  release(nowMs: number): void {
    if (!this.#enabled) return;
    const stale = nowMs - this.#lastMoveMs > INERTIA_STALE_MS;
    this.#gliding = !stale && Math.hypot(this.#vx, this.#vy) >= INERTIA_START_SPEED;
    if (!this.#gliding) this.#reset();
  }

  /// Stops at once and forgets any speed (a new touch, a view reset).
  stop(): void {
    this.#gliding = false;
    this.#reset();
  }

  /// Advances the glide by `dtSec`, turning `orbit` by the distance
  /// travelled. Returns whether it is still gliding afterwards.
  step(dtSec: number, orbit: OrbitCamera): boolean {
    if (!this.#gliding) return false;
    // The distance covered while slowing from v to v * decay over dt is
    // v * tau * (1 - decay): exact, so it does not depend on frame rate.
    const decay = Math.exp(-dtSec / INERTIA_TIME_CONSTANT_SEC);
    const travel = INERTIA_TIME_CONSTANT_SEC * (1 - decay);
    orbitDrag(orbit, this.#vx * travel, this.#vy * travel);
    this.#vx *= decay;
    this.#vy *= decay;
    if (Math.hypot(this.#vx, this.#vy) < INERTIA_STOP_SPEED) this.stop();
    return this.#gliding;
  }

  #reset(): void {
    this.#vx = 0;
    this.#vy = 0;
  }
}
