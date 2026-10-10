import type { CubicGeometry } from "../render/lattice/cubic.js";
import type { DragAdapter, TurnDragSession } from "./drag-adapter.js";
import { dragActiveTurn, pickCancel, pickDrag, pickRelease, pickStart, type DragState } from "./drag-state.js";
import type { MoveMap } from "./move-axis.js";
import type { ScreenPoint } from "./screen.js";

/// The cubic family's gestures, as TurnDragSessions: a thin wrapper
/// over drag-state.ts's ported rubik state machine, whose behaviour is
/// unchanged -- this only gives it the shape PointerController now
/// drives every family through.
class CubicDragSession implements TurnDragSession {
  readonly #state: DragState;

  constructor(state: DragState) {
    this.#state = state;
  }

  get locked(): boolean {
    return this.#state.locked;
  }

  drag(pointer: ScreenPoint, dtSec: number): void {
    pickDrag(this.#state, pointer, dtSec);
  }

  activeTurn(): ReturnType<TurnDragSession["activeTurn"]> {
    return dragActiveTurn(this.#state);
  }

  release(): ReturnType<TurnDragSession["release"]> {
    const { fromDeg, toDeg, move } = pickRelease(this.#state);
    // Locked but snapped back (or blocked): ease the layer to rest.
    const settle = !move && this.#state.locked ? { axis: this.#state.axis, layer: this.#state.layer } : null;
    return { fromDeg, toDeg, move, settle };
  }

  cancel(): ReturnType<TurnDragSession["cancel"]> {
    const fromDeg = this.#state.accumDeg;
    const settle = this.#state.locked ? { axis: this.#state.axis, layer: this.#state.layer } : null;
    pickCancel(this.#state);
    return { fromDeg, settle };
  }
}

export function cubicDragAdapter(geometry: CubicGeometry, moves: MoveMap): DragAdapter {
  return {
    start(ray, camera, width, height, pointer) {
      const state = pickStart(ray, camera, width, height, pointer, geometry, moves);
      return state ? new CubicDragSession(state) : null;
    },
  };
}
