import * as THREE from "three";

/// Spherical-coordinates orbit camera -- ported from rubik's
/// t_orbit_camera (include/render/input.h), trimmed to the state this
/// project actually drives (drag + pinch, §7's decided convention, and
/// since Phase 6 a fixed-size keyboard step, orbitNudge). No camera
/// presets or idle auto-orbit lerp: those are rubik's own desktop-only
/// extras and nothing in rubik_mega's docs calls for them.
export interface OrbitCamera {
  yawDeg: number;
  pitchDeg: number;
  distance: number;
}

/// The zoom range used until a caller supplies one. Since Phase 6 the
/// real range comes from the fitted distance (interaction/framing.ts's
/// zoomLimits), so it follows the puzzle and the screen.
const ORBIT_MIN_DISTANCE = 4.0;
const ORBIT_MAX_DISTANCE = 20.0;

/// A zoom range: the nearest and farthest the camera may sit.
export interface DistanceLimits {
  min: number;
  max: number;
}
export const DEFAULT_DISTANCE_LIMITS: DistanceLimits = { min: ORBIT_MIN_DISTANCE, max: ORBIT_MAX_DISTANCE };
/// Degrees of orbit per pixel of two-finger drag -- ported from rubik's
/// ORBIT_SENSITIVITY (its right-mouse-drag equivalent).
const ORBIT_SENSITIVITY = 0.3;
/// Ported from rubik's ORBIT_PITCH_LIMIT_DEG: raylib's (and Three.js's)
/// LookAt uses a fixed world-up, so a pitch of +/-90 degrees is a
/// gimbal-lock singularity where the camera's right vector degenerates
/// and the orbit visually reverses instead of continuing over the pole.
/// Clamping just short of the poles avoids it.
const ORBIT_PITCH_LIMIT_DEG = 89.0;

function clamp(value: number, lo: number, hi: number): number {
  return Math.min(Math.max(value, lo), hi);
}

/// Starting view -- matches rubik's own default preset (CAMERA_PRESETS[0]
/// in src/render/input.c), the app's startup default there too.
export function createOrbitCamera(): OrbitCamera {
  return { yawDeg: 40, pitchDeg: 25, distance: 11 };
}

/// Applies a drag delta (in screen pixels) to yaw/pitch. Yaw keeps
/// rubik's own orbit_camera_update() sign (same sensitivity, same
/// direction); pitch is DELIBERATELY the opposite sign from rubik's --
/// Lucas: rubik's original (drag up -> camera moves up, i.e. pitch
/// increases with -deltaY) felt unnatural here, so this flips it to a
/// drag-the-view convention instead (drag up -> pitch decreases,
/// camera dips down, as if you'd grabbed the cube and pulled it
/// upward) -- DECISIONS.md, "Touch camera gestures revised" addendum.
export function orbitDrag(orbit: OrbitCamera, deltaX: number, deltaY: number): void {
  orbit.yawDeg -= deltaX * ORBIT_SENSITIVITY;
  orbit.pitchDeg = clamp(orbit.pitchDeg + deltaY * ORBIT_SENSITIVITY, -ORBIT_PITCH_LIMIT_DEG, ORBIT_PITCH_LIMIT_DEG);
}

/// Turns the view by `yawDeg` and `pitchDeg` degrees (a keyboard step), with
/// the same pole clamp as a drag. Positive yaw turns the view to the right.
export function orbitNudge(orbit: OrbitCamera, yawDeg: number, pitchDeg: number): void {
  orbit.yawDeg += yawDeg;
  orbit.pitchDeg = clamp(orbit.pitchDeg + pitchDeg, -ORBIT_PITCH_LIMIT_DEG, ORBIT_PITCH_LIMIT_DEG);
}

/// Applies a zoom delta (pinch or wheel): positive zooms in (shrinks
/// distance), same sign convention as rubik's mouse-wheel handling.
export function orbitZoom(orbit: OrbitCamera, distanceDelta: number, limits: DistanceLimits = DEFAULT_DISTANCE_LIMITS): void {
  orbit.distance = clamp(orbit.distance - distanceDelta, limits.min, limits.max);
}

/// Writes `orbit`'s spherical position into `camera` and points it at
/// the origin -- ported from orbit_camera_update()'s position math
/// (right-handed, Y up, matching this project's own +X=R/+Y=U/+Z=F
/// convention).
export function applyOrbitToCamera(orbit: OrbitCamera, camera: THREE.PerspectiveCamera): void {
  const yawRad = (orbit.yawDeg * Math.PI) / 180;
  const pitchRad = (orbit.pitchDeg * Math.PI) / 180;
  camera.position.set(
    orbit.distance * Math.cos(pitchRad) * Math.sin(yawRad),
    orbit.distance * Math.sin(pitchRad),
    orbit.distance * Math.cos(pitchRad) * Math.cos(yawRad),
  );
  camera.lookAt(0, 0, 0);
}
