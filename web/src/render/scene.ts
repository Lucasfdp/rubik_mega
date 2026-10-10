import * as THREE from "three";
import { RoundedBoxGeometry } from "three/examples/jsm/geometries/RoundedBoxGeometry.js";
import {
  AXIS_UNIT,
  Axis,
  CORE_FACE_COUNT,
  CORE_FACE_NORMAL,
  axisComponent,
  createCubicGeometry,
  generateCubicLattice,
  type CubicGeometry,
  type LatticeCell,
  type Vec3,
} from "./lattice/cubic.js";
import type { PuzzleHandle } from "../engine/puzzle-handle.js";
import type { ActiveTurn, TurnScene } from "./turn.js";
import { LOOK, createBodyMaterial, createStickerMaterial, outlineOptionsFor } from "./look.js";
import { applyStickerUv, classicLook, createGlyphTexture, type LookOptions } from "./glyphs.js";
import { TIER_SETTINGS, type TierSettings } from "./quality.js";
import { stickerOutline, type Point3 } from "./sticker-outline.js";
import { PuzzleType } from "../engine/types.js";

/// A layer currently mid-turn, for the transient rotation applied on top
/// of every cubie's normal fixed position -- ported from rubik's
/// t_active_turn (render/geometry.h): "spins the matching cubies by
/// angle_deg around the world axis before translating them, draws every
/// other cubie at its normal fixed position." The cubic scene only
/// understands the axis-and-layer form of render/turn.ts's ActiveTurn.
export interface CubicTurn {
  axis: Axis;
  layer: number;
  angleDeg: number;
}

export interface CubicScene extends TurnScene {
  group: THREE.Group;
  /// This scene's world-space layout -- the drag code picks against the
  /// same geometry it was drawn with.
  geometry: CubicGeometry;
  /// Radius of the sphere around the origin that holds the whole cube
  /// at any turn angle -- what the camera frames (interaction/framing.ts).
  boundingRadius: number;
  /// Full resync from the puzzle's latest facelet colours
  /// (PuzzleHandle.faceletView()), then lays everything out at rest
  /// (docs/en/03-architecture.md S6: "idle otherwise"). Call after every
  /// committed move.
  sync(): void;
  /// Per-frame pose update: repositions every cubie, applying `turn`'s
  /// rotation to whichever ones are in its layer (`turn.layer` is a
  /// lattice index, 0..size-1). Pass `null` for the rest pose. Call
  /// every animation frame while a turn is in progress, same as
  /// rubik's draw_scene(). Cheap even at n = 8: 296 body instances plus
  /// 384 sticker instances, plain matrix writes.
  setActiveTurn(turn: ActiveTurn | null): void;
  /// Frees GPU resources -- call before dropping the scene (a size
  /// change builds a fresh one).
  dispose(): void;
}

/// Builds the cubic-lattice scene for any size (docs/en/03-architecture.md
/// S6, docs/en/07-nxn-web-view.md): one InstancedMesh for cubie bodies,
/// one per sticker colour. Sticker colours come straight from core's
/// facelet buffer -- this file knows where facelets ARE
/// (puzzle.layout.facelets), never which colour belongs where.
export function buildCubicScene(
  puzzle: PuzzleHandle,
  tier: TierSettings = TIER_SETTINGS.standard,
  look: LookOptions = classicLook(PuzzleType.Cubic),
): CubicScene {
  const { size, facelets } = puzzle.layout;
  const geometry = createCubicGeometry(size);
  const cells = generateCubicLattice(size);
  const group = new THREE.Group();

  const bodyGeometry = tier.roundedBodies
    ? new RoundedBoxGeometry(geometry.bodySize, geometry.bodySize, geometry.bodySize, tier.bodySegments, geometry.bodySize * LOOK.bodyBevelRatio)
    : new THREE.BoxGeometry(geometry.bodySize, geometry.bodySize, geometry.bodySize);
  const bodyMaterial = createBodyMaterial();
  const bodyMesh = new THREE.InstancedMesh(bodyGeometry, bodyMaterial, cells.length);
  group.add(bodyMesh);

  const stickerThickness = geometry.bodySize * LOOK.stickerThicknessRatio;
  const stickerInset = geometry.bodySize / 2 + stickerThickness / 2 + geometry.bodySize * LOOK.stickerGapRatio;
  const stickerGeometry = roundedStickerGeometry(geometry.bodySize, stickerThickness, outlineOptionsFor(tier));
  if (look.glyphs) applyStickerUv(stickerGeometry, [0, 0, 1]);
  // Every colour shows on exactly size^2 facelets in every state (a
  // turn only relabels which facelet shows which colour), so each
  // colour's mesh is sized once and never overflows.
  const { palette } = look;
  const glyphTextures: THREE.Texture[] = [];
  const capacityPerColour = size * size;
  const stickerMeshes: THREE.InstancedMesh[] = [];
  // Parallel to stickerMeshes: which facelet index each instance
  // currently draws, rebuilt by sync() and read every frame by
  // setActiveTurn() -- an InstancedMesh has no way to ask "what did I
  // put at index i", so this is that memory.
  const assignments: number[][] = [];
  for (let colour = 0; colour < CORE_FACE_COUNT; colour++) {
    const glyph = look.glyphs ? createGlyphTexture(palette[colour]!, look.glyphs[colour]!) : undefined;
    if (glyph) glyphTextures.push(glyph);
    const mesh = new THREE.InstancedMesh(
      stickerGeometry,
      createStickerMaterial(palette[colour]!, THREE.FrontSide, glyph),
      capacityPerColour,
    );
    mesh.count = 0;
    stickerMeshes.push(mesh);
    assignments.push([]);
    group.add(mesh);
  }

  // Scratch objects, reused every call instead of allocated per
  // instance/frame -- this runs every animation frame while a turn is
  // live, same reasoning as rubik's own per-frame draw_scene().
  const basePosition = new THREE.Vector3();
  const baseQuaternion = new THREE.Quaternion();
  const stickerDefaultNormal = new THREE.Vector3(0, 0, 1);
  const normalVector = new THREE.Vector3();
  const axisVector = new THREE.Vector3();
  const unitScale = new THREE.Vector3(1, 1, 1);
  const turnQuaternion = new THREE.Quaternion();
  const matrix = new THREE.Matrix4();

  function poseInstance(
    mesh: THREE.InstancedMesh,
    index: number,
    cell: LatticeCell,
    normal: Vec3 | null,
    turn: CubicTurn | null,
  ): void {
    basePosition.set(geometry.cellToWorld(cell.x), geometry.cellToWorld(cell.y), geometry.cellToWorld(cell.z));
    if (normal) {
      normalVector.set(normal.x, normal.y, normal.z);
      baseQuaternion.setFromUnitVectors(stickerDefaultNormal, normalVector);
      basePosition.addScaledVector(normalVector, stickerInset);
    } else {
      baseQuaternion.identity();
    }
    if (turn && axisComponent(cell, turn.axis) === turn.layer) {
      const unit = AXIS_UNIT[turn.axis];
      const angleRad = (turn.angleDeg * Math.PI) / 180;
      axisVector.set(unit.x, unit.y, unit.z);
      basePosition.applyAxisAngle(axisVector, angleRad);
      turnQuaternion.setFromAxisAngle(axisVector, angleRad);
      baseQuaternion.premultiply(turnQuaternion);
    }
    matrix.compose(basePosition, baseQuaternion, unitScale);
    mesh.setMatrixAt(index, matrix);
  }

  function setActiveTurn(active: ActiveTurn | null): void {
    // A generator-form turn belongs to a cut-plane scene; a cubic scene
    // only ever gets axis-and-layer turns, and treats anything else as rest.
    const turn = active && "axis" in active ? active : null;
    cells.forEach((cell, i) => poseInstance(bodyMesh, i, cell, null, turn));
    bodyMesh.instanceMatrix.needsUpdate = true;
    stickerMeshes.forEach((mesh, colour) => {
      assignments[colour]!.forEach((faceletIndex, i) => {
        const facelet = facelets[faceletIndex]!;
        poseInstance(mesh, i, facelet, CORE_FACE_NORMAL[facelet.face]!, turn);
      });
      mesh.instanceMatrix.needsUpdate = true;
    });
  }

  function sync(): void {
    const colours = puzzle.faceletView();
    if (colours.length !== facelets.length) {
      throw new Error(`scene.sync: ${colours.length} facelet colours for ${facelets.length} facelets`);
    }
    for (const list of assignments) list.length = 0;
    colours.forEach((colour, faceletIndex) => {
      const list = assignments[colour];
      if (!list) throw new Error(`scene.sync: facelet ${faceletIndex} has colour ${colour}, expected 0-5`);
      list.push(faceletIndex);
    });
    stickerMeshes.forEach((mesh, colour) => {
      mesh.count = Math.min(assignments[colour]!.length, capacityPerColour);
    });
    setActiveTurn(null);
  }

  function dispose(): void {
    bodyGeometry.dispose();
    bodyMaterial.dispose();
    stickerGeometry.dispose();
    for (const mesh of stickerMeshes) (mesh.material as THREE.Material).dispose();
    for (const texture of glyphTextures) texture.dispose();
    bodyMesh.dispose();
    for (const mesh of stickerMeshes) mesh.dispose();
  }

  sync();
  return { group, geometry, boundingRadius: Math.sqrt(3) * geometry.halfExtent, sync, setActiveTurn, dispose };
}

/// A sticker as a thin rounded slab, centred on the origin and facing
/// +z, the same outline every other family's stickers get
/// (render/sticker-outline.ts). `faceSize` is the body face it sits on.
function roundedStickerGeometry(faceSize: number, thickness: number, options: Parameters<typeof stickerOutline>[2]): THREE.BufferGeometry {
  const half = faceSize / 2;
  const square: Point3[] = [[-half, -half, 0], [half, -half, 0], [half, half, 0], [-half, half, 0]];
  const outline = stickerOutline(square, [0, 0, 1], options);
  const shape = new THREE.Shape(outline.map(([x, y]) => new THREE.Vector2(x, y)));
  const slab = new THREE.ExtrudeGeometry(shape, { depth: thickness, bevelEnabled: false, steps: 1 });
  // Extrusion runs from z = 0 to z = thickness; poseInstance() expects
  // the slab centred, as the box it replaces was.
  slab.translate(0, 0, -thickness / 2);
  return slab;
}
