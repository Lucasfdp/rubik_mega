/// Whether the user asked the system for less motion
/// (docs/en/13-phase6-polish.md D11). Read where motion starts -- camera
/// inertia, sheet transitions -- rather than cached, so a change in the
/// system setting applies on the next gesture.
export function prefersReducedMotion(): boolean {
  try {
    return window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  } catch {
    return false;
  }
}
