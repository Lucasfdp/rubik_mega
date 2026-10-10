import { SPIN_SEC, makeConfetti, spinProgress, stepConfetti, type Particle } from "../interaction/celebration-motion.js";
import { prefersReducedMotion } from "./motion.js";
import { el } from "./shell/dom.js";

/// The moment after you solve it by hand (docs/en/13-phase6-polish.md D9):
/// the view makes one slow turn about the vertical axis and a light burst
/// of confetti in the puzzle's own colours falls. Both are skipped under
/// prefers-reduced-motion (the toast the caller shows still says so).
///
/// The turn moves the CAMERA, not the puzzle: what you see is the same, and
/// a drag that starts meanwhile still picks the sticker under the finger.
export interface CelebrationDeps {
  /// Turns the view by `yawDeg` (PointerController.nudgeOrbit).
  nudgeYaw(yawDeg: number): void;
  /// Wakes the render-on-demand loop.
  requestFrame(): void;
  /// Sticker colours to throw.
  colours(): readonly string[];
}

const CONFETTI_COUNT = 70;

export class Celebration {
  readonly #deps: CelebrationDeps;
  #elapsedSec = 0;
  #spinning = false;
  #lastAngle = 0;
  #canvas: HTMLCanvasElement | null = null;
  #particles: Particle[] = [];
  #raf = 0;
  #lastFrameMs = 0;

  constructor(deps: CelebrationDeps) {
    this.#deps = deps;
  }

  play(): void {
    if (prefersReducedMotion()) return;
    this.#elapsedSec = 0;
    this.#lastAngle = 0;
    this.#spinning = true;
    this.#deps.requestFrame();
    this.#startConfetti();
  }

  /// Advances the turn; called from the frame loop. Returns whether the
  /// view is still turning (so the loop keeps asking for frames).
  update(dtSec: number): boolean {
    if (!this.#spinning) return false;
    this.#elapsedSec += dtSec;
    const angle = 360 * spinProgress(this.#elapsedSec);
    this.#deps.nudgeYaw(angle - this.#lastAngle);
    this.#lastAngle = angle;
    if (this.#elapsedSec >= SPIN_SEC) this.#spinning = false;
    return this.#spinning;
  }

  /// Stops everything now (the puzzle is being replaced).
  dispose(): void {
    this.#spinning = false;
    this.#stopConfetti();
  }

  #startConfetti(): void {
    this.#stopConfetti();
    const canvas = el("canvas", { class: "confetti", attrs: { "aria-hidden": "true" } });
    const ratio = Math.min(window.devicePixelRatio || 1, 2);
    canvas.width = Math.round(window.innerWidth * ratio);
    canvas.height = Math.round(window.innerHeight * ratio);
    document.body.append(canvas);
    this.#canvas = canvas;
    this.#particles = makeConfetti(CONFETTI_COUNT, this.#deps.colours(), canvas.width, canvas.height, Math.random);
    this.#lastFrameMs = performance.now();
    this.#raf = requestAnimationFrame(this.#tick);
  }

  readonly #tick = (nowMs: number): void => {
    const canvas = this.#canvas;
    const context = canvas?.getContext("2d");
    if (!canvas || !context) return;
    const dtSec = Math.min((nowMs - this.#lastFrameMs) / 1000, 0.05);
    this.#lastFrameMs = nowMs;
    this.#particles = stepConfetti(this.#particles, dtSec);
    context.clearRect(0, 0, canvas.width, canvas.height);
    for (const p of this.#particles) {
      context.globalAlpha = Math.min(1, (p.life - p.age) * 2);
      context.fillStyle = p.colour;
      context.fillRect(p.x, p.y, p.size, p.size * 0.6);
    }
    if (this.#particles.length === 0) this.#stopConfetti();
    else this.#raf = requestAnimationFrame(this.#tick);
  };

  #stopConfetti(): void {
    cancelAnimationFrame(this.#raf);
    this.#canvas?.remove();
    this.#canvas = null;
    this.#particles = [];
  }
}
