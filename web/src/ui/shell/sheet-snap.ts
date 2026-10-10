/// Snap-point maths for the bottom sheet (docs/en/13-phase6-polish.md D7):
/// where the three resting heights are, and which one a released drag
/// settles on. Pure, so the feel of the sheet is tested without a DOM.

export type SheetState = "peek" | "half" | "full";
export const SHEET_STATES: readonly SheetState[] = ["peek", "half", "full"];

/// The measurements the snap points depend on, all in CSS pixels.
export interface SheetMetrics {
  viewportHeight: number;
  /// Height of the always-visible part: the grab handle, the status line
  /// and the action bar.
  peekHeight: number;
  /// Room kept clear at the top for the top bar, so a full sheet never
  /// covers it.
  topReserve: number;
}

/// The half-open sheet shows this share of the screen height.
export const HALF_FRACTION = 0.45;
/// How far ahead (seconds) the release velocity is projected when
/// choosing a snap: a fast flick up skips to the next one.
export const FLING_PROJECTION_SEC = 0.15;
/// However tall the sheet, the camera treats at most this share of the
/// screen as covered, so an open sheet does not shrink the puzzle to
/// nothing: it just overlaps it.
export const MAX_COVER_FRACTION = 0.5;

/// Visible height of the sheet in each state. Always ordered
/// peek <= half <= full, even on a screen too short for them to differ.
export function snapHeights(metrics: SheetMetrics): Record<SheetState, number> {
  const peek = Math.min(metrics.peekHeight, metrics.viewportHeight);
  const full = Math.max(peek, metrics.viewportHeight - metrics.topReserve);
  const half = Math.min(Math.max(Math.round(metrics.viewportHeight * HALF_FRACTION), peek), full);
  return { peek, half, full };
}

/// The state a drag released at `visibleHeight` settles on, given its
/// velocity in pixels per second (positive: moving up, growing the
/// sheet). Projects where the sheet would have got to and takes the
/// nearest snap point there.
export function nearestSnap(visibleHeight: number, velocityPxPerSec: number, heights: Record<SheetState, number>): SheetState {
  const projected = visibleHeight + velocityPxPerSec * FLING_PROJECTION_SEC;
  let best: SheetState = "peek";
  for (const state of SHEET_STATES) {
    if (Math.abs(heights[state] - projected) < Math.abs(heights[best] - projected)) best = state;
  }
  return best;
}

/// Keeps a dragged height between the lowest and highest snap points.
export function clampVisible(height: number, heights: Record<SheetState, number>): number {
  return Math.min(Math.max(height, heights.peek), heights.full);
}

/// The next state when the handle is tapped: opens a closed sheet, and
/// closes an open one.
export function toggledState(state: SheetState): SheetState {
  return state === "peek" ? "half" : "peek";
}

/// The next state one step up or down, for keyboard control.
export function steppedState(state: SheetState, direction: 1 | -1): SheetState {
  const index = SHEET_STATES.indexOf(state) + direction;
  return SHEET_STATES[Math.min(Math.max(index, 0), SHEET_STATES.length - 1)]!;
}

/// How much of the screen's bottom the camera should treat as covered by
/// the sheet in `state`.
export function coverInset(state: SheetState, metrics: SheetMetrics): number {
  return Math.min(snapHeights(metrics)[state], metrics.viewportHeight * MAX_COVER_FRACTION);
}
