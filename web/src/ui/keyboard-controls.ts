import type { PuzzleLayout } from "../engine/types.js";
import { ignoresKey, interpretKey, type KeyCommand } from "../interaction/keyboard.js";
import type { ManualPlay } from "../interaction/manual-play.js";

/// Carries out the keyboard map (docs/en/13-phase6-polish.md D9). What a
/// key MEANS is decided by interaction/keyboard.ts (pure, tested); this
/// owns the events, the two small pieces of state the map needs (which
/// face or layer was picked), and calling the right part of the app.
export interface KeyboardHost {
  layout(): PuzzleLayout | null;
  /// Hand-made turns, undo and redo. Null while no puzzle is loaded.
  manual(): ManualPlay | null;
  /// A scramble is playing, or a puzzle is loading: turns would collide.
  busy(): boolean;
  hasSolution(): boolean;
  playPause(): void;
  stepForward(): void;
  stepBack(): void;
  nudgeOrbit(yawDeg: number, pitchDeg: number): void;
  resetView(): void;
  showHelp(): void;
  /// A short message about the pick ("Layer 2: press a face letter").
  say(text: string): void;
}

export class KeyboardControls {
  readonly #host: KeyboardHost;
  readonly #abort = new AbortController();
  #selectedGenerator: number | null = null;
  #pendingLayer: number | null = null;

  constructor(host: KeyboardHost) {
    this.#host = host;
    window.addEventListener("keydown", (event) => this.#onKeyDown(event), { signal: this.#abort.signal });
  }

  dispose(): void {
    this.#abort.abort();
  }

  /// A different puzzle replaced the one a pick referred to.
  clearSelection(): void {
    this.#selectedGenerator = null;
    this.#pendingLayer = null;
  }

  #onKeyDown(event: KeyboardEvent): void {
    const layout = this.#host.layout();
    if (!layout || event.defaultPrevented) return;
    const target = event.target instanceof HTMLElement ? event.target : null;
    const dialogOpen = document.querySelector("dialog[open]") !== null;
    if (ignoresKey({ tag: target?.tagName ?? "BODY", editable: target?.isContentEditable ?? false, role: target?.getAttribute("role") ?? null }, event.key, dialogOpen)) {
      return;
    }
    const command = interpretKey(event, {
      layout,
      hasSolution: this.#host.hasSolution(),
      selectedGenerator: this.#selectedGenerator,
      pendingLayer: this.#pendingLayer,
    });
    if (!command) return;
    event.preventDefault();
    this.#run(command);
  }

  #run(command: KeyCommand): void {
    const host = this.#host;
    const manual = host.manual();
    switch (command.kind) {
      case "turn":
        if (manual && !host.busy() && manual.turn(command.move)) this.#pendingLayer = null;
        return;
      case "turnSelected":
        if (manual && !host.busy() && this.#selectedGenerator !== null) manual.turnBy(this.#selectedGenerator, command.direction);
        return;
      case "selectLayer":
        this.#pendingLayer = command.layer;
        host.say(`Layer ${command.layer}: press a face letter`);
        return;
      case "selectGenerator":
        this.#selectedGenerator = command.generator;
        host.say(`Face ${command.generator + 1} picked: use the left and right arrows to turn it`);
        return;
      case "clearSelection":
        this.clearSelection();
        return;
      case "playPause":
        host.playPause();
        return;
      case "stepForward":
        host.stepForward();
        return;
      case "stepBack":
        host.stepBack();
        return;
      case "undo":
        if (!host.busy()) manual?.undo();
        return;
      case "redo":
        if (!host.busy()) manual?.redo();
        return;
      case "help":
        host.showHelp();
        return;
      case "resetView":
        host.resetView();
        return;
      case "orbit":
        host.nudgeOrbit(command.yawDeg, command.pitchDeg);
        return;
    }
  }
}
