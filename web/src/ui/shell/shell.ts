import type { PuzzleType } from "../../engine/types.js";
import type { PuzzleHandle } from "../../engine/puzzle-handle.js";
import type { Insets, Viewport } from "../../interaction/framing.js";
import type { ManualPlay } from "../../interaction/manual-play.js";
import type { TurnAnimation } from "../../interaction/turn-anim.js";
import type { TurnScene } from "../../render/turn.js";
import { Announcer } from "../announcer.js";
import { prefersReducedMotion } from "../motion.js";
import { puzzleKey, puzzleTitle } from "../puzzle-sizes.js";
import type { Settings } from "../settings.js";
import { BottomSheet } from "./bottom-sheet.js";
import { el } from "./dom.js";
import { icon } from "./icons.js";
import { layoutModeFor, type LayoutMode } from "./layout.js";
import { createPuzzlePicker, type PuzzlePicker } from "./puzzle-picker.js";
import { createSettingsDialog, type SettingsDialog } from "./settings-dialog.js";
import { createShortcutsDialog, type ShortcutsDialog } from "./shortcuts-dialog.js";
import { SolveControls, type ScrambleInfo } from "./solve-controls.js";
import { toasts } from "./toast.js";

/// The user interface around the 3D view (docs/en/13-phase6-polish.md D7).
///
/// One DOM, two arrangements, chosen by layout.ts and written to a
/// `data-layout` attribute the CSS keys on:
///  - compact (phones, portrait tablets): a slim top bar, and one panel
///    that is a bottom sheet. Its always-visible "peek" is the status
///    line and the action bar, so Scramble and Solve are always one thumb
///    away; dragging it up reveals the algorithm picker, speed and moves.
///  - regular (desktop, landscape): the same panel as a column on the
///    right, which can be hidden.
/// Nothing is sized in pixels: spacing, type and controls are rem, so the
/// interface-size setting scales all of it, and the safe-area insets keep
/// it clear of notches and home indicators.

export interface ShellCallbacks {
  /// The user picked a different puzzle (the shell rebuilds nothing; the
  /// app owns that, and calls bind() when the new puzzle is ready).
  onPuzzleChange(type: PuzzleType, size: number): void;
  /// Any setting changed or was first chosen; the app applies and saves it.
  onSettingsChange(settings: Settings): void;
  /// A scramble finished (its seed is the shareable part).
  onScrambled(info: ScrambleInfo): void;
  /// The Share button was pressed.
  onShare(): void;
}

export interface ShellOptions {
  settings: Settings;
  callbacks: ShellCallbacks;
  /// One line saying which tier Auto picks on this device.
  describeAutoQuality(): string;
}

/// What the shell needs from a loaded puzzle.
export interface BoundPuzzle {
  puzzle: PuzzleHandle;
  anim: TurnAnimation;
  scene: TurnScene;
  /// Hand-made turns, undo and redo, built by the app because the drag
  /// needs the same instance.
  manual: ManualPlay;
}

export class Shell {
  readonly element: HTMLElement;
  #options: ShellOptions;
  #settings: Settings;
  #layout: LayoutMode;
  #panelOpen = true;
  #listeners: Array<() => void> = [];
  #controls: SolveControls | null = null;
  #current: { type: PuzzleType; size: number } | null = null;

  #topbar: HTMLElement;
  #panel: HTMLElement;
  #puzzleTitle: HTMLElement;
  #panelToggle: HTMLButtonElement;
  #sheet: BottomSheet;
  #picker: PuzzlePicker;
  #settingsDialog: SettingsDialog;
  #shortcuts: ShortcutsDialog;
  /// Spoken updates (playback, scramble); also the keyboard's pick notes.
  readonly announcer = new Announcer();
  #observer: ResizeObserver;

  constructor(options: ShellOptions) {
    this.#options = options;
    this.#settings = options.settings;
    this.#layout = layoutModeFor(this.#viewport());

    // Top bar.
    this.#puzzleTitle = el("span", { class: "pill__title", text: "Cube" });
    const puzzleButton = el(
      "button",
      { class: "pill", props: { type: "button" }, attrs: { "aria-haspopup": "dialog" } },
      this.#puzzleTitle,
      icon("chevronDown", "icon icon--small"),
    );
    puzzleButton.addEventListener("click", () => {
      if (this.#current) this.#picker.open(this.#current);
    });
    this.#panelToggle = el("button", { class: "icon-button panel-toggle", props: { type: "button" }, attrs: { "aria-label": "Hide controls", "aria-expanded": "true" } }, icon("panel"));
    this.#panelToggle.addEventListener("click", () => this.#setPanelOpen(!this.#panelOpen));
    const settingsButton = el("button", { class: "icon-button", props: { type: "button" }, attrs: { "aria-label": "Settings", "aria-haspopup": "dialog" } }, icon("settings"));
    settingsButton.addEventListener("click", () => this.#settingsDialog.open(this.#settings, this.#options.describeAutoQuality()));
    const shareButton = el("button", { class: "icon-button", props: { type: "button" }, attrs: { "aria-label": "Share a link to this puzzle" } }, icon("share"));
    shareButton.addEventListener("click", () => this.#options.callbacks.onShare());
    this.#topbar = el("header", { class: "topbar" }, puzzleButton, el("span", { class: "topbar__spacer" }), shareButton, this.#panelToggle, settingsButton);

    // The panel: header (always visible) and body.
    const handle = el("button", { class: "panel__handle", props: { type: "button" }, attrs: { "aria-label": "Expand controls", "aria-expanded": "false" } }, el("span", { class: "panel__grabber" }));
    const status = el("div", { class: "status" });

    const scrambleButton = el("button", { class: "btn", props: { type: "button" } }, icon("scramble"), el("span", { text: "Scramble" }));
    const solveButton = el("button", { class: "btn btn--primary", props: { type: "button" } }, icon("solve"), el("span", { text: "Solve" }));
    const historyButton = (name: "undo" | "redo", label: string): HTMLButtonElement =>
      el("button", { class: "btn btn--icon", props: { type: "button", disabled: true }, attrs: { "aria-label": label } }, icon(name));
    const undoButton = historyButton("undo", "Undo your last turn");
    const redoButton = historyButton("redo", "Redo");
    const idleGroup = el("div", { class: "actions" }, undoButton, redoButton, scrambleButton, solveButton);

    const transportButton = (name: Parameters<typeof icon>[0], label: string, extra = ""): HTMLButtonElement =>
      el("button", { class: `btn btn--icon ${extra}`.trim(), props: { type: "button" }, attrs: { "aria-label": label } }, icon(name));
    const stepBackButton = transportButton("stepBack", "Step back");
    const playPauseButton = transportButton("play", "Play", "btn--primary");
    const stepForwardButton = transportButton("stepForward", "Step forward");
    const skipPhaseButton = transportButton("skipPhase", "Skip to next phase");
    const copyButton = transportButton("copy", "Copy the solution");
    const dismissButton = transportButton("close", "Close solution");
    const transportGroup = el("div", { class: "actions actions--transport" }, stepBackButton, playPauseButton, stepForwardButton, skipPhaseButton, copyButton, dismissButton);

    const nowMove = el("div", { class: "now__move", attrs: { "aria-hidden": "true" } });
    const nowPhase = el("span", { class: "now__phase" });
    const nowCount = el("span", { class: "now__count" });
    const now = el("div", { class: "now" }, nowMove, el("div", { class: "now__meta", attrs: { "aria-hidden": "true" } }, nowPhase, nowCount));
    const timelineHost = el("div", { class: "timeline-host" });

    const header = el("div", { class: "panel__header" }, handle, status, now, timelineHost, el("div", { class: "actionbar" }, idleGroup, transportGroup));

    const algoSelect = el("select", { class: "select", attrs: { id: "algo-select" } });
    const algoField = el("div", { class: "field" }, el("label", { text: "Method", attrs: { for: "algo-select" } }), algoSelect);
    const stepperButton = (symbol: string, label: string): HTMLButtonElement =>
      el("button", { class: "btn btn--icon btn--small", text: symbol, props: { type: "button" }, attrs: { "aria-label": label } });
    const speedSlowerButton = stepperButton("−", "Slower");
    const speedFasterButton = stepperButton("+", "Faster");
    const speedLabel = el("span", { class: "field__value", attrs: { "aria-live": "off" } });
    const speedField = el("div", { class: "field" }, el("span", { class: "field__name", text: "Speed" }), el("div", { class: "stepper", attrs: { role: "group", "aria-label": "Playback speed" } }, speedSlowerButton, speedLabel, speedFasterButton));
    const movesToggle = el("button", { class: "link-button", props: { type: "button" } });
    const moveList = el("div", { class: "move-list" });
    const movesField = el("div", { class: "field field--stack" }, movesToggle, moveList);
    const body = el("div", { class: "panel__body" }, algoField, speedField, movesField);

    this.#panel = el("aside", { class: "panel", attrs: { "aria-label": "Controls" } }, header, body);

    this.#picker = createPuzzlePicker((type, size) => this.#options.callbacks.onPuzzleChange(type, size));
    this.#shortcuts = createShortcutsDialog();
    this.#settingsDialog = createSettingsDialog(
      (settings) => {
        this.#settings = settings;
        this.#options.callbacks.onSettingsChange(settings);
      },
      () => this.showShortcuts(),
    );

    this.element = el(
      "div",
      { class: "shell" },
      this.#topbar,
      this.#panel,
      this.#picker.element,
      this.#settingsDialog.element,
      this.#shortcuts.element,
      toasts.element,
      this.announcer.element,
    );
    this.element.dataset.layout = this.#layout;
    this.element.dataset.panel = "open";

    this.#sheet = new BottomSheet({
      panel: this.#panel,
      header,
      handle,
      body,
      viewport: () => this.#viewport(),
      topReserve: () => this.#topbar.getBoundingClientRect().bottom,
      onChange: () => this.#notify(),
    });
    this.#sheet.setEnabled(this.#layout === "compact");

    // The status line can wrap and the top bar can change height, which
    // moves the peek height and the free area.
    this.#observer = new ResizeObserver(() => this.#relayout());
    this.#observer.observe(header);
    this.#observer.observe(this.#topbar);
    this.#observer.observe(this.#panel);
    window.addEventListener("resize", this.#relayout);
    window.visualViewport?.addEventListener("resize", this.#relayout);

    this.#elements = {
      status, now, nowMove, nowPhase, nowCount, timelineHost, idleGroup, undoButton, redoButton, scrambleButton, solveButton,
      transportGroup, stepBackButton, playPauseButton, stepForwardButton, skipPhaseButton, copyButton, dismissButton,
      algoField, algoSelect, speedField, speedSlowerButton, speedFasterButton, speedLabel, movesField, movesToggle, moveList,
    };
  }

  #elements: ConstructorParameters<typeof SolveControls>[0];

  get layout(): LayoutMode {
    return this.#layout;
  }

  /// How much of each screen edge the interface covers right now, which
  /// is what the camera frames the puzzle around.
  insets(): Insets {
    const top = this.#topbar.getBoundingClientRect().bottom;
    if (this.#layout === "compact") {
      return { top, right: 0, bottom: this.#sheet.coverInset(), left: 0 };
    }
    const right = this.#panelOpen ? this.#panel.offsetWidth + (parseFloat(getComputedStyle(this.#panel).right) || 0) : 0;
    return { top, right, bottom: 0, left: 0 };
  }

  /// Calls `listener` whenever the layout or the insets may have changed.
  onLayoutChange(listener: () => void): void {
    this.#listeners.push(listener);
  }

  /// Takes over for a freshly loaded puzzle: the title, and the controls.
  bind(bound: BoundPuzzle): void {
    this.#controls?.dispose();
    const { type, size } = bound.puzzle.layout;
    this.#current = { type, size };
    this.#puzzleTitle.textContent = puzzleTitle(type, size);
    this.element.removeAttribute("data-loading");
    const key = puzzleKey(type, size);
    this.#controls = new SolveControls(this.#elements, {
      puzzle: bound.puzzle,
      anim: bound.anim,
      scene: bound.scene,
      manual: bound.manual,
      announcer: this.announcer,
      movesShown: this.#settings.movesShown ?? this.#layout === "regular",
      preferredAlgorithm: this.#settings.algorithms[key] ?? null,
      preferredSpeed: this.#settings.speed,
      animateScramble: () => this.#settings.animateScramble && !prefersReducedMotion(),
      onMovesShownChange: (shown) => {
        this.#remember({ movesShown: shown });
        this.#relayout();
      },
      onAlgorithmChange: (name) => this.#remember({ algorithms: { ...this.#settings.algorithms, [key]: name } }),
      onSpeedChange: (speed) => this.#remember({ speed }),
      onScrambled: (info) => this.#options.callbacks.onScrambled(info),
    });
  }

  /// The solve controls of the open puzzle, or null while none is.
  get controls(): SolveControls | null {
    return this.#controls;
  }

  /// Opens the keyboard shortcut list for the open puzzle.
  showShortcuts(): void {
    if (this.#current) this.#shortcuts.open(this.#current.type);
  }

  #remember(patch: Partial<Settings>): void {
    this.#settings = { ...this.#settings, ...patch };
    this.#options.callbacks.onSettingsChange(this.#settings);
  }

  /// Locks the controls while another puzzle loads.
  setLoading(loading: boolean): void {
    this.element.toggleAttribute("data-loading", loading);
    this.#controls?.setLoading(loading);
  }

  /// The settings changed from outside (the app applied a correction).
  updateSettings(settings: Settings): void {
    this.#settings = settings;
  }

  dispose(): void {
    this.#observer.disconnect();
    window.removeEventListener("resize", this.#relayout);
    window.visualViewport?.removeEventListener("resize", this.#relayout);
    this.#sheet.dispose();
    this.#controls?.dispose();
    this.element.remove();
  }

  #viewport(): Viewport {
    return { width: window.innerWidth, height: window.innerHeight };
  }

  #relayout = (): void => {
    const layout = layoutModeFor(this.#viewport());
    if (layout !== this.#layout) {
      this.#layout = layout;
      this.element.dataset.layout = layout;
      this.#sheet.setEnabled(layout === "compact");
      // The hide button exists only in the regular layout; leaving it
      // must not strand a hidden panel.
      if (layout === "compact" && !this.#panelOpen) this.#setPanelOpen(true);
    } else {
      this.#sheet.refresh();
    }
    this.#notify();
  };

  #setPanelOpen(open: boolean): void {
    this.#panelOpen = open;
    this.element.dataset.panel = open ? "open" : "closed";
    this.#panelToggle.setAttribute("aria-expanded", String(open));
    this.#panelToggle.setAttribute("aria-label", open ? "Hide controls" : "Show controls");
    // A hidden panel must not hold keyboard focus.
    this.#panel.inert = !open;
    this.#notify();
  }

  #notify(): void {
    for (const listener of this.#listeners) listener();
  }
}
