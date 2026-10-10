import * as THREE from "three";

export interface ScreenPoint {
  x: number;
  y: number;
}

/// Projects a world point to pixel coordinates in the canvas -- the
/// Three.js equivalent of raylib's GetWorldToScreen(), which rubik's
/// input_pick_start() uses to build screen_dir[] (render/input.c).
export function worldToScreen(
  point: THREE.Vector3,
  camera: THREE.Camera,
  width: number,
  height: number,
): ScreenPoint {
  const ndc = point.clone().project(camera);
  return { x: ((ndc.x + 1) / 2) * width, y: ((1 - ndc.y) / 2) * height };
}
