import type { Viewport } from "../../interaction/framing.js";
import {
  clampVisible,
  coverInset,
  nearestSnap,
  snapHeights,
  steppedState,
  toggledState,
  type SheetMetrics,
  type SheetState,
} from "./sheet-snap.js";

/// The bottom sheet behaviour (docs/en/13-phase6-polish.md D7): it rests
/// at one of three heights, follows a finger while dragged, and settles
/// on the nearest snap point (sheet-snap.ts has the maths). The element
/// is one fixed-height panel slid up and down with a CSS variable, so
/// dragging costs no layout.
///
/// In the regular layout there is no sheet: `setEnabled(false)` clears
/// every effect and the panel is simply a side column.

/// A drag must move this far (CSS pixels) before it counts as a drag
/// rather than a tap on the handle.
const DRAG_SLOP_PX = 6;
/// Only the last stretch of a drag is used to read its release speed.
const VELOCITY_WINDOW_MS = 100;

export interface BottomSheetOptions {
  /// The sliding panel; gets `data-sheet` and the `--sheet-*` variables.
  panel: HTMLElement;
  /// The always-visible part of the panel (handle, status, action bar),
  /// measured to find the peek height.
  header: HTMLElement;
  /// The grab handle; takes the drag and the keyboard control.
  handle: HTMLButtonElement;
  /// The part that is out of view at peek; made inert so keyboard focus
  /// cannot wander into it while hidden.
  body: HTMLElement;
  viewport: () => Viewport;
  /// Height of the top bar (and its safe area): a full sheet stops below it.
  topReserve: () => number;
  /// Called whenever the state or the measurements change.
  onChange: () => void;
}

interface Sample {
  timeMs: number;
  visible: number;
}

export class BottomSheet {
  #o: BottomSheetOptions;
  #state: SheetState = "peek";
  #enabled = true;
  #dragging = false;
  #suppressClick = false;
  #startY = 0;
  #startVisible = 0;
  #samples: Sample[] = [];
  #abort = new AbortController();

  constructor(options: BottomSheetOptions) {
    this.#o = options;
    const { signal } = this.#abort;
    const { handle, panel } = options;
    handle.addEventListener("pointerdown", this.#onPointerDown, { signal });
    handle.addEventListener("pointermove", this.#onPointerMove, { signal });
    handle.addEventListener("pointerup", this.#onPointerUp, { signal });
    handle.addEventListener("pointercancel", this.#onPointerUp, { signal });
    handle.addEventListener("click", this.#onClick, { signal });
    handle.addEventListener("keydown", this.#onHandleKey, { signal });
    panel.addEventListener(
      "keydown",
      (event) => {
        if (event.key === "Escape" && this.#enabled && this.#state !== "peek") {
          event.stopPropagation();
          this.setState("peek");
        }
      },
      { signal },
    );
    this.refresh();
  }

  get state(): SheetState {
    return this.#state;
  }

  metrics(): SheetMetrics {
    const panelStyle = getComputedStyle(this.#o.panel);
    return {
      viewportHeight: this.#o.viewport().height,
      // The panel pads its bottom for the home-indicator safe area, which
      // belongs to the peek height or the action bar would be hidden by it.
      peekHeight: this.#o.header.offsetHeight + (parseFloat(panelStyle.paddingBottom) || 0),
      topReserve: this.#o.topReserve(),
    };
  }

  /// How much of the screen's bottom the camera should treat as covered.
  coverInset(): number {
    return this.#enabled ? coverInset(this.#state, this.metrics()) : 0;
  }

  /// Switches the sheet behaviour on (compact layout) or off (regular).
  setEnabled(enabled: boolean): void {
    this.#enabled = enabled;
    this.refresh();
  }

  setState(state: SheetState): void {
    this.#state = state;
    this.refresh();
    this.#o.onChange();
  }

  /// Re-measures and re-applies the current state. Call after anything
  /// that can change the measurements: a resize, the status line wrapping.
  refresh(): void {
    const { panel, handle, body } = this.#o;
    if (!this.#enabled) {
      panel.removeAttribute("data-sheet");
      panel.style.removeProperty("--sheet-full");
      panel.style.removeProperty("--sheet-offset");
      body.inert = false;
      return;
    }
    const metrics = this.metrics();
    const heights = snapHeights(metrics);
    panel.dataset.sheet = this.#state;
    panel.style.setProperty("--sheet-full", `${heights.full}px`);
    panel.style.setProperty("--sheet-offset", `${heights.full - heights[this.#state]}px`);
    const open = this.#state !== "peek";
    body.inert = !open;
    handle.setAttribute("aria-expanded", String(open));
    handle.setAttribute("aria-label", open ? "Collapse controls" : "Expand controls");
  }

  dispose(): void {
    this.#abort.abort();
  }

  #visibleNow(): number {
    const heights = snapHeights(this.metrics());
    return heights[this.#state];
  }

  #onPointerDown = (event: PointerEvent): void => {
    if (!this.#enabled || !event.isPrimary) return;
    this.#o.handle.setPointerCapture(event.pointerId);
    this.#startY = event.clientY;
    this.#startVisible = this.#visibleNow();
    this.#dragging = false;
    this.#samples = [{ timeMs: event.timeStamp, visible: this.#startVisible }];
  };

  #onPointerMove = (event: PointerEvent): void => {
    if (!this.#enabled || !this.#o.handle.hasPointerCapture(event.pointerId)) return;
    const moved = this.#startY - event.clientY;
    if (!this.#dragging && Math.abs(moved) < DRAG_SLOP_PX) return;
    if (!this.#dragging) {
      this.#dragging = true;
      this.#o.panel.dataset.dragging = "";
    }
    const heights = snapHeights(this.metrics());
    const visible = clampVisible(this.#startVisible + moved, heights);
    this.#o.panel.style.setProperty("--sheet-offset", `${heights.full - visible}px`);
    this.#samples.push({ timeMs: event.timeStamp, visible });
  };

  #onPointerUp = (event: PointerEvent): void => {
    if (!this.#enabled) return;
    if (this.#o.handle.hasPointerCapture(event.pointerId)) this.#o.handle.releasePointerCapture(event.pointerId);
    if (!this.#dragging) return;
    this.#dragging = false;
    // The click that follows a drag must not also toggle the sheet.
    this.#suppressClick = true;
    delete this.#o.panel.dataset.dragging;
    const heights = snapHeights(this.metrics());
    const last = this.#samples[this.#samples.length - 1]!;
    const recent = this.#samples.find((s) => last.timeMs - s.timeMs <= VELOCITY_WINDOW_MS) ?? last;
    const seconds = Math.max(last.timeMs - recent.timeMs, 1) / 1000;
    const velocity = (last.visible - recent.visible) / seconds;
    this.setState(event.type === "pointercancel" ? this.#state : nearestSnap(last.visible, velocity, heights));
  };

  #onClick = (): void => {
    if (this.#suppressClick) {
      this.#suppressClick = false;
      return;
    }
    if (this.#enabled) this.setState(toggledState(this.#state));
  };

  #onHandleKey = (event: KeyboardEvent): void => {
    if (!this.#enabled) return;
    if (event.key === "ArrowUp" || event.key === "ArrowDown") {
      event.preventDefault();
      this.setState(steppedState(this.#state, event.key === "ArrowUp" ? 1 : -1));
    }
  };
}
