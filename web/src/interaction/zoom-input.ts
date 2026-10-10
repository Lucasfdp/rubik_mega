/// Turning wheel and pinch input into a distance change for
/// orbit-camera.ts's orbitZoom() (docs/en/13-phase6-polish.md D6). Both
/// scale with the fitted distance, so a notch of the wheel is the same
/// *proportion* of the way in whatever the puzzle or screen size.

/// Share of the fitted distance one pixel of wheel travel moves the
/// camera: a typical 100 px notch is 15 %.
export const WHEEL_ZOOM_PER_PX = 0.0015;

/// The distance the pinch gesture was tuned at, before framing varied it
/// (11 world units, the old fixed camera distance). Pinch moved the
/// camera one world unit per pixel of finger spread there; scaling by
/// fit / PINCH_REFERENCE_FIT keeps the feel the same proportion now.
export const PINCH_REFERENCE_FIT = 11;

/// Pixels per wheel "line" and per "page" when the browser reports
/// those units instead of pixels (Firefox does for a mouse wheel).
const PIXELS_PER_LINE = 16;
const PIXELS_PER_PAGE = 400;

/// Wheel units, as a WheelEvent reports them.
export const WHEEL_DELTA_PIXEL = 0;
export const WHEEL_DELTA_LINE = 1;

/// The orbitZoom() argument for one wheel event: positive zooms in, and
/// scrolling up (negative deltaY) zooms in, as on every map.
export function wheelZoomDelta(deltaY: number, deltaMode: number, fittedDistance: number): number {
  const pixels = deltaMode === WHEEL_DELTA_PIXEL ? deltaY : deltaMode === WHEEL_DELTA_LINE ? deltaY * PIXELS_PER_LINE : deltaY * PIXELS_PER_PAGE;
  return -pixels * WHEEL_ZOOM_PER_PX * fittedDistance;
}

/// The orbitZoom() argument for a pinch whose finger spread changed by
/// `spreadDeltaPx` (positive: fingers moved apart, zoom in).
export function pinchZoomDelta(spreadDeltaPx: number, fittedDistance: number): number {
  return spreadDeltaPx * (fittedDistance / PINCH_REFERENCE_FIT);
}
