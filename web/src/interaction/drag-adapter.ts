import type * as THREE from "three";
import type { Move } from "../engine/types.js";
import type { ActiveTurn, TurnTarget } from "../render/turn.js";
import type { ScreenPoint } from "./screen.js";

/// One drag-to-turn gesture, from pointer-down to release, for any
/// puzzle family. PointerController drives these without knowing how a
/// family decides what a drag turns: the cubic family snaps to one of 3
/// world axes (cubic-drag.ts), the cut-plane families pick among the
/// handful of cuts through the touched sticker (cut-drag.ts).
export interface TurnDragSession {
  /// Past the dead zone: what is being turned is decided.
  readonly locked: boolean;
  /// Set by a session that has decided the gesture is not a turn after
  /// all (Square-1: a vertical drag on the frame half, a horizontal one
  /// on the middle layer): the controller then drops the session and
  /// orbits the camera with the same pointer from that move on.
  readonly wantsOrbit?: boolean;
  /// Feeds the pointer's current position; `dtSec` since the last call.
  drag(pointer: ScreenPoint, dtSec: number): void;
  /// The turn being posed right now, or null while still undecided.
  activeTurn(): ActiveTurn | null;
  /// Ends the gesture. `move` is the committed move (null if it snapped
  /// back to rest); `settle` names what to ease back to rest in that
  /// case, null if nothing had been decided.
  release(): { fromDeg: number; toDeg: number; move: Move | null; settle: TurnTarget | null };
  /// Abandons the gesture (a second finger joined). `settle` as above.
  cancel(): { fromDeg: number; settle: TurnTarget | null };
}

/// Starts gestures for one puzzle: casts the pointer's ray at it.
export interface DragAdapter {
  /// @return null if the ray misses the puzzle (the pointer then orbits
  ///         the camera instead).
  start(
    ray: THREE.Ray,
    camera: THREE.Camera,
    width: number,
    height: number,
    pointer: ScreenPoint,
  ): TurnDragSession | null;
}
