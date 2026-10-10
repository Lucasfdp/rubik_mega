import * as THREE from "three";
import {
  PuzzleType,
  SQ1_CAP_FACELETS,
  SQ1_FACELET_COUNT,
  SQ1_GEN_BOTTOM,
  SQ1_GEN_SLICE,
  SQ1_GEN_TOP,
  SQ1_LAYER_UNITS,
  SQ1_MIDDLE_FACELETS,
  SQ1_MIDDLE_INDEX,
  SQ1_SIDE_FACELETS,
  SQ1_UNIT_COUNT,
} from "../../engine/types.js";
import type { PuzzleHandle } from "../../engine/puzzle-handle.js";
import type { ActiveTurn, TurnScene } from "../turn.js";
import { applyStickerUv, classicLook, createGlyphTexture, type LookOptions } from "../glyphs.js";
import { LOOK, createBodyMaterial, createStickerMaterial, outlineOptionsFor, DoubleSide, FrontSide } from "../look.js";
import { TIER_SETTINGS, type TierSettings } from "../quality.js";
import { stickerOutline } from "../sticker-outline.js";
import {
  BOTTOM_Y0,
  BOTTOM_Y1,
  BOUNDING_RADIUS,
  MIDDLE_HALF,
  MIDDLE_HALVES,
  TOP_Y0,
  TOP_Y1,
  cutNormal,
  isRightHalf,
  outlineOf,
  placementsFor,
  type Layer,
  type Placement,
} from "./square1-geometry.js";

/// The scene for the Square-1 (docs/en/12-phase5-square1.md D5). Unlike
/// every other family its pieces change SHAPE from place to place -- a
/// 30-degree edge or a 60-degree corner -- so unlike the cut scene it
/// cannot build geometry once and only recolour it: sync() lays the 16
/// pieces and the two middle halves out again from core's units view and
/// facelet colours. Twenty-odd small meshes per move is nothing, and
/// "always a full resync, never incremental" is the rule every scene
/// here follows (docs/en/03-architecture.md S6).

/// How far a drag may pose a slice that is not legal: a small wobble, so
/// the puzzle visibly resists rather than ignoring the finger.
export const BLOCKED_SLICE_WOBBLE_DEG = 8;

/// What a ray hit: which layer and which half of the puzzle, and where.
export interface Square1Hit {
  layer: "top" | "middle" | "bottom";
  half: "left" | "right";
  point: THREE.Vector3;
}

export interface Square1Scene extends TurnScene {
  group: THREE.Group;
  /// Radius of the sphere around the origin that holds the whole puzzle
  /// at any turn angle -- what the camera frames (interaction/framing.ts).
  boundingRadius: number;
  /// The first piece `ray` meets, or null if it misses the puzzle.
  pick(ray: THREE.Ray): Square1Hit | null;
  dispose(): void;
}

type Vec3 = [number, number, number];

/// A flat polygon as triangles, wound so it faces along `normal`.
function polygonGeometry(points: readonly Vec3[], normal: Vec3): THREE.BufferGeometry {
  const positions: number[] = [];
  const a = new THREE.Vector3(...points[0]!);
  const b = new THREE.Vector3(...points[1]!);
  const c = new THREE.Vector3(...points[2]!);
  const facing = new THREE.Vector3().subVectors(b, a).cross(new THREE.Vector3().subVectors(c, a)).dot(new THREE.Vector3(...normal));
  const ordered = facing >= 0 ? points : [...points].reverse();
  for (let i = 1; i + 1 < ordered.length; i++) {
    for (const p of [ordered[0]!, ordered[i]!, ordered[i + 1]!]) positions.push(p[0], p[1], p[2]);
  }
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute("position", new THREE.Float32BufferAttribute(positions, 3));
  geometry.computeVertexNormals();
  return geometry;
}

/// The dark body of a prism: `outline` (x, z) extruded from y0 to y1.
function prismGeometry(outline: readonly [number, number][], y0: number, y1: number): THREE.BufferGeometry {
  const positions: number[] = [];
  const top: Vec3[] = outline.map(([x, z]) => [x, y1, z]);
  const bottom: Vec3[] = outline.map(([x, z]) => [x, y0, z]);
  const add = (points: Vec3[]): void => {
    for (let i = 1; i + 1 < points.length; i++) {
      for (const p of [points[0]!, points[i]!, points[i + 1]!]) positions.push(p[0], p[1], p[2]);
    }
  };
  add(top);
  add([...bottom].reverse());
  for (let i = 0; i < outline.length; i++) {
    const j = (i + 1) % outline.length;
    add([top[i]!, bottom[i]!, bottom[j]!, top[j]!]);
  }
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute("position", new THREE.Float32BufferAttribute(positions, 3));
  geometry.computeVertexNormals();
  return geometry;
}

/// The horizontal outward normal of the side face from plan point p to q.
function faceNormal(p: [number, number], q: [number, number]): Vec3 {
  const dx = q[0] - p[0];
  const dz = q[1] - p[1];
  const length = Math.hypot(dx, dz);
  let nx = dz / length;
  let nz = -dx / length;
  if (nx * (p[0] + q[0]) + nz * (p[1] + q[1]) < 0) {
    nx = -nx;
    nz = -nz;
  }
  return [nx, 0, nz];
}

export function buildSquare1Scene(
  puzzle: PuzzleHandle,
  tier: TierSettings = TIER_SETTINGS.standard,
  look: LookOptions = classicLook(PuzzleType.Square1),
): Square1Scene {
  if (puzzle.layout.type !== PuzzleType.Square1) throw new Error("buildSquare1Scene: not a Square-1");
  const group = new THREE.Group();
  const bodyMaterial = createBodyMaterial(DoubleSide);
  const { palette, glyphs } = look;
  const glyphTextures = glyphs ? palette.map((colour, i) => createGlyphTexture(colour, glyphs[i]!)) : [];
  const materials = palette.map((colour, i) => createStickerMaterial(colour, FrontSide, glyphTextures[i]));
  const outline = outlineOptionsFor(tier);

  /// Everything sync() built, so the next sync can throw it away.
  let geometries: THREE.BufferGeometry[] = [];
  let built: THREE.Object3D[] = [];
  let bodies: THREE.Mesh[] = [];
  /// Per piece group: what a turn must know to decide whether it moves.
  interface Mover {
    object: THREE.Group;
    layer: "top" | "middle" | "bottom";
    half: "left" | "right";
    /// Orientation it rests in (the flipped right middle half).
    rest: THREE.Quaternion;
  }
  let movers: Mover[] = [];

  function addBody(piece: THREE.Group, outline: readonly [number, number][], y0: number, y1: number, layer: Mover["layer"], half: Mover["half"]): void {
    const geometry = prismGeometry(outline, y0, y1);
    geometries.push(geometry);
    const mesh = new THREE.Mesh(geometry, bodyMaterial);
    mesh.userData = { layer, half };
    piece.add(mesh);
    bodies.push(mesh);
  }

  function addSticker(piece: THREE.Group, points: Vec3[], normal: Vec3, colour: number): void {
    const material = materials[colour];
    if (!material) throw new Error(`square1 scene: colour ${colour} is not 0-${materials.length - 1}`);
    const geometry = polygonGeometry(stickerOutline(points, normal, outline), normal);
    if (glyphs) applyStickerUv(geometry, normal);
    geometries.push(geometry);
    piece.add(new THREE.Mesh(geometry, material));
  }

  /// A side sticker on the face from plan point p to q, between y0 and y1.
  function addSide(piece: THREE.Group, p: [number, number], q: [number, number], y0: number, y1: number, colour: number): void {
    const n = faceNormal(p, q);
    const lift = (pt: [number, number], y: number): Vec3 => [pt[0] + n[0] * LOOK.stickerLift, y, pt[1] + n[2] * LOOK.stickerLift];
    addSticker(piece, [lift(p, y0), lift(q, y0), lift(q, y1), lift(p, y1)], n, colour);
  }

  function addLayerPiece(placement: Placement, colours: Uint8Array): void {
    const layer: Layer = placement.layer;
    const base = layer === "top" ? 0 : SQ1_LAYER_UNITS;
    const [y0, y1] = layer === "top" ? [TOP_Y0, TOP_Y1] : [BOTTOM_Y0, BOTTOM_Y1];
    const outline = outlineOf(placement);
    const piece = new THREE.Group();
    addBody(piece, outline, y0, y1, layer, isRightHalf(placement) ? "right" : "left");
    // Cap: the piece's own plan, on the outward face of its layer.
    const capY = layer === "top" ? y1 + LOOK.stickerLift : y0 - LOOK.stickerLift;
    const unit = base + placement.firstUnit;
    addSticker(
      piece,
      outline.map(([x, z]): Vec3 => [x, capY, z]),
      [0, layer === "top" ? 1 : -1, 0],
      colours[SQ1_CAP_FACELETS + unit]!,
    );
    // Sides: one face per unit, clockwise from above.
    const corners = outline.slice(1);
    const second = base + ((placement.firstUnit + 1) % SQ1_LAYER_UNITS);
    const sideUnits = placement.kind === "edge" ? [unit] : [unit, second];
    sideUnits.forEach((u, face) => {
      addSide(piece, corners[face]!, corners[face + 1]!, y0, y1, colours[SQ1_SIDE_FACELETS + u]!);
    });
    group.add(piece);
    built.push(piece);
    movers.push({ object: piece, layer, half: isRightHalf(placement) ? "right" : "left", rest: new THREE.Quaternion() });
  }

  const normal = cutNormal();
  const sliceAxis = new THREE.Vector3(normal[0], 0, normal[1]).normalize();

  function addMiddle(colours: Uint8Array, flipped: boolean): void {
    MIDDLE_HALVES.forEach((half, index) => {
      const piece = new THREE.Group();
      addBody(piece, half.outline, -MIDDLE_HALF, MIDDLE_HALF, "middle", half.half);
      // Core numbers the middle's six stickers front, side, back of the
      // left half and then of the right. A flipped right half is the same
      // plastic turned over, so its front sticker is the old back one.
      const stickerAt = (face: number): number => {
        const swapped = flipped && half.half === "right" ? [2, 1, 0][face]! : face;
        return colours[SQ1_MIDDLE_FACELETS + index * 3 + swapped]!;
      };
      half.faces.forEach(([a, b], face) => {
        addSide(piece, half.outline[a]!, half.outline[b]!, -MIDDLE_HALF, MIDDLE_HALF, stickerAt(face));
      });
      const rest = new THREE.Quaternion();
      if (flipped && half.half === "right") rest.setFromAxisAngle(sliceAxis, Math.PI);
      piece.quaternion.copy(rest);
      group.add(piece);
      built.push(piece);
      movers.push({ object: piece, layer: "middle", half: half.half, rest });
    });
  }

  function clear(): void {
    for (const object of built) group.remove(object);
    for (const geometry of geometries) geometry.dispose();
    built = [];
    geometries = [];
    bodies = [];
    movers = [];
  }

  function sync(): void {
    const colours = puzzle.faceletView();
    const units = puzzle.unitsView();
    if (colours.length !== SQ1_FACELET_COUNT) throw new Error(`square1 sync: ${colours.length} facelets, expected ${SQ1_FACELET_COUNT}`);
    if (units.length !== SQ1_UNIT_COUNT) throw new Error(`square1 sync: ${units.length} units, expected ${SQ1_UNIT_COUNT}`);
    clear();
    for (const layer of ["top", "bottom"] as const) {
      for (const placement of placementsFor(units, layer)) addLayerPiece(placement, colours);
    }
    addMiddle(colours, puzzle.stateView()[SQ1_MIDDLE_INDEX] === 1);
    group.updateMatrixWorld(true);
  }

  const turn = new THREE.Quaternion();

  function setActiveTurn(active: ActiveTurn | null): void {
    const generator = active && "generator" in active ? active.generator : null;
    turn.identity();
    if (generator !== null && active) {
      const radians = (active.angleDeg * Math.PI) / 180;
      turn.setFromAxisAngle(generator === SQ1_GEN_SLICE ? sliceAxis : new THREE.Vector3(0, 1, 0), radians);
    }
    for (const mover of movers) {
      const moves =
        generator === SQ1_GEN_TOP ? mover.layer === "top"
        : generator === SQ1_GEN_BOTTOM ? mover.layer === "bottom"
        : generator === SQ1_GEN_SLICE ? mover.half === "right"
        : false;
      if (moves) mover.object.quaternion.multiplyQuaternions(turn, mover.rest);
      else mover.object.quaternion.copy(mover.rest);
    }
  }

  function pick(ray: THREE.Ray): Square1Hit | null {
    group.updateMatrixWorld(true);
    const hit = new THREE.Raycaster(ray.origin, ray.direction).intersectObjects(bodies, false)[0];
    if (!hit) return null;
    const { layer, half } = hit.object.userData as Pick<Square1Hit, "layer" | "half">;
    return { layer, half, point: hit.point.clone() };
  }

  function dispose(): void {
    clear();
    bodyMaterial.dispose();
    for (const material of materials) material.dispose();
    for (const texture of glyphTextures) texture.dispose();
  }

  sync();
  return { group, boundingRadius: BOUNDING_RADIUS, sync, setActiveTurn, pick, dispose };
}
