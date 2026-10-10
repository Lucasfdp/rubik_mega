/// Render-on-demand frame loop (docs/en/13-phase6-polish.md D1).
///
/// Before Phase 6 main.ts called requestAnimationFrame forever, so a
/// puzzle nobody was touching still cost the GPU 60 frames a second.
/// This class owns the one rule that replaces that: a frame is scheduled
/// only when something asked for one (`request()`), and the loop keeps
/// going only while the per-frame callback says there is still motion.
/// Everything that changes what is on screen -- a pointer move, a turn
/// in flight, a resize, a scene resync -- calls `request()`; nothing
/// else ever does, which is what makes "zero frames at rest" provable
/// by a unit test rather than by eye.

/// A frame source, injected so a test can drive time by hand. The
/// browser's own is `browserClock`.
export interface FrameClock {
  request(callback: (nowMs: number) => void): number;
  cancel(handle: number): void;
}

export const browserClock: FrameClock = {
  request: (callback) => requestAnimationFrame(callback),
  cancel: (handle) => cancelAnimationFrame(handle),
};

/// Longest step handed to the per-frame callback. A tab that was hidden
/// mid-turn comes back with a gap of many seconds; passing that through
/// would finish the whole animation in one jump instead of resuming it.
export const MAX_FRAME_DT_SEC = 0.1;

/// What one frame does. Returns true while something is still animating
/// (the scheduler then asks for another frame), false once everything is
/// at rest. `dtSec` is 0 on the first frame after an idle spell, since
/// there is no previous frame to measure against.
export type FrameCallback = (dtSec: number, nowMs: number) => boolean;

export class FrameScheduler {
  #clock: FrameClock;
  #callback: FrameCallback;
  #handle: number | null = null;
  #lastMs: number | null = null;
  #framesRendered = 0;

  constructor(clock: FrameClock, callback: FrameCallback) {
    this.#clock = clock;
    this.#callback = callback;
  }

  /// Asks for a frame. Idempotent while one is already pending, so any
  /// number of callers in the same tick cost a single render.
  request(): void {
    if (this.#handle !== null) return;
    this.#handle = this.#clock.request(this.#onFrame);
  }

  /// Whether a frame is waiting to run.
  get isScheduled(): boolean {
    return this.#handle !== null;
  }

  /// Frames run since construction -- the debug overlay's "frames
  /// rendered" counter, and what the "zero frames at rest" gate reads.
  get framesRendered(): number {
    return this.#framesRendered;
  }

  /// Cancels a pending frame (page teardown). Safe when none is pending.
  dispose(): void {
    if (this.#handle !== null) this.#clock.cancel(this.#handle);
    this.#handle = null;
  }

  #onFrame = (nowMs: number): void => {
    this.#handle = null;
    const dtSec = this.#lastMs === null ? 0 : Math.min(Math.max((nowMs - this.#lastMs) / 1000, 0), MAX_FRAME_DT_SEC);
    this.#lastMs = nowMs;
    this.#framesRendered++;
    const busy = this.#callback(dtSec, nowMs);
    if (busy) this.request();
    else this.#lastMs = null;
  };
}
