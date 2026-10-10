import type { ScreenPoint } from "./screen.js";

/// Double-tap / double-click detection (docs/en/13-phase6-polish.md D6:
/// "double-tap empty space resets the view"). Only taps on empty space
/// are ever given to this, so it can never delay a turn: a drag that
/// starts on the puzzle never reaches it.

/// Longest gap between two taps that still counts as a double.
export const DOUBLE_TAP_GAP_MS = 300;
/// How far apart (CSS pixels) the two taps may land.
export const DOUBLE_TAP_SLOP_PX = 32;
/// A press that moves further than this, or lasts longer than
/// TAP_MAX_MS, is a drag or a hold, not a tap.
export const TAP_MAX_MOVE_PX = 10;
export const TAP_MAX_MS = 250;

/// Whether a press from `down` to `up` was short and still enough to be a tap.
export function isTap(down: { point: ScreenPoint; timeMs: number }, up: { point: ScreenPoint; timeMs: number }): boolean {
  return up.timeMs - down.timeMs <= TAP_MAX_MS && Math.hypot(up.point.x - down.point.x, up.point.y - down.point.y) <= TAP_MAX_MOVE_PX;
}

export class DoubleTapDetector {
  #last: { point: ScreenPoint; timeMs: number } | null = null;

  /// Registers one tap. Returns true when it completes a double tap
  /// (and forgets it, so a third tap starts a fresh pair).
  tap(point: ScreenPoint, timeMs: number): boolean {
    const last = this.#last;
    if (last && timeMs - last.timeMs <= DOUBLE_TAP_GAP_MS && Math.hypot(point.x - last.point.x, point.y - last.point.y) <= DOUBLE_TAP_SLOP_PX) {
      this.#last = null;
      return true;
    }
    this.#last = { point, timeMs };
    return false;
  }

  /// Forgets a pending first tap (a drag began, the puzzle changed).
  reset(): void {
    this.#last = null;
  }
}
