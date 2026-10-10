/// Camera framing maths (docs/en/13-phase6-polish.md D6): where the
/// camera sits so a puzzle fills the part of the screen the interface
/// leaves free, and how the projection shifts to centre it there. Pure
/// numbers in and out, so it is unit tested; the camera and the DOM are
/// only touched by whoever calls it.

/// The camera's vertical field of view, in degrees. A property of the
/// projection, so main.ts and the maths below read the same constant.
export const CAMERA_FOV_DEG = 50;

/// Share of the free area's shorter side the puzzle's bounding sphere
/// fills. Under 1 so a corner turning towards the camera never touches
/// the edge or the interface.
export const FRAME_FILL_FRACTION = 0.9;

/// How far the user may zoom, as multiples of the fitted distance:
/// 0.6 is closer (bigger), 2.5 is farther (smaller). Relative, so every
/// puzzle and every screen size has the same range.
export const ZOOM_DISTANCE_RANGE = { min: 0.6, max: 2.5 } as const;

/// The size of the screen in CSS pixels.
export interface Viewport {
  width: number;
  height: number;
}

/// How much of each screen edge the interface covers, in CSS pixels.
export interface Insets {
  top: number;
  right: number;
  bottom: number;
  left: number;
}

/// A rectangle in CSS pixels, origin top-left.
export interface Rect {
  x: number;
  y: number;
  width: number;
  height: number;
}

/// The smallest free area ever reported. Insets that would leave less
/// (a keyboard open on a landscape phone, a very short window) are
/// squeezed instead, so the maths never divides by zero or flips.
export const MIN_FREE_PX = 120;

/// The part of the screen left once the interface takes its insets.
export function freeRectFor(viewport: Viewport, insets: Insets): Rect {
  const clamp = (value: number): number => Math.max(value, 0);
  const left = clamp(insets.left);
  const top = clamp(insets.top);
  const width = Math.max(viewport.width - left - clamp(insets.right), Math.min(MIN_FREE_PX, viewport.width));
  const height = Math.max(viewport.height - top - clamp(insets.bottom), Math.min(MIN_FREE_PX, viewport.height));
  return { x: left, y: top, width, height };
}

/// How far from the camera a sphere of `boundingRadius` must sit for its
/// outline to be `targetRadiusPx` on screen, for a perspective camera
/// with vertical field of view `fovDeg` over a viewport `viewportHeightPx`
/// tall. Square pixels make this independent of the width, which is what
/// lets one rule cover a tall phone and a wide desktop alike.
///
/// A sphere of radius R at distance d subtends a half-angle asin(R / d),
/// and the half-height of the screen is tan(fov / 2) at unit depth, so
/// (targetRadiusPx / (viewportHeightPx / 2)) = tan(a) / tan(fov / 2).
export function fitDistance(boundingRadius: number, fovDeg: number, viewportHeightPx: number, targetRadiusPx: number): number {
  if (boundingRadius <= 0 || viewportHeightPx <= 0 || targetRadiusPx <= 0) {
    throw new Error(`fitDistance: radius, viewport height and target must be positive (${boundingRadius}, ${viewportHeightPx}, ${targetRadiusPx})`);
  }
  const halfFov = (fovDeg * Math.PI) / 360;
  const tanAngle = ((2 * targetRadiusPx) / viewportHeightPx) * Math.tan(halfFov);
  return boundingRadius / Math.sin(Math.atan(tanAngle));
}

/// Where the camera goes and how the projection shifts, for one puzzle
/// in one free area.
export interface Framing {
  /// Distance from the origin at which the puzzle fills FRAME_FILL_FRACTION
  /// of the free area's shorter side.
  distance: number;
  /// The two arguments of PerspectiveCamera.setViewOffset(.., x, y, ..)
  /// that slide the picture so the origin lands at the free area's
  /// centre. setViewOffset moves the window over the picture, so the
  /// picture moves the opposite way.
  offsetX: number;
  offsetY: number;
}

export function framingFor(boundingRadius: number, viewport: Viewport, free: Rect, fill: number = FRAME_FILL_FRACTION): Framing {
  const targetRadiusPx = (fill * Math.min(free.width, free.height)) / 2;
  return {
    distance: fitDistance(boundingRadius, CAMERA_FOV_DEG, viewport.height, targetRadiusPx),
    offsetX: viewport.width / 2 - (free.x + free.width / 2),
    offsetY: viewport.height / 2 - (free.y + free.height / 2),
  };
}

/// The distance range the user may zoom within, given the fitted distance.
export function zoomLimits(fittedDistance: number): { min: number; max: number } {
  return { min: fittedDistance * ZOOM_DISTANCE_RANGE.min, max: fittedDistance * ZOOM_DISTANCE_RANGE.max };
}

/// Re-expresses a distance in a new fit, keeping the user's zoom
/// *ratio*: someone zoomed in to 0.8x who rotates the phone or switches
/// puzzle stays at 0.8x of the new fitted distance.
export function rescaleDistance(distance: number, oldFit: number, newFit: number): number {
  return (distance / oldFit) * newFit;
}

/// Moves `current` towards `target` by exponential smoothing: a fixed
/// share of the remaining gap per unit time, so the speed does not
/// depend on the frame rate. Snaps once within `epsilon`, so a tween
/// finishes instead of creeping for ever.
export function approach(current: number, target: number, dtSec: number, ratePerSec: number, epsilon = 0.25): number {
  const gap = target - current;
  if (Math.abs(gap) <= epsilon) return target;
  return current + gap * (1 - Math.exp(-ratePerSec * dtSec));
}
