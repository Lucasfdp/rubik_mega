import type { PuzzleHandle } from "../../engine/puzzle-handle.js";
import type { ManualPlay } from "../../interaction/manual-play.js";
import { ScrambleRunner } from "../../interaction/scramble-runner.js";
import type { TurnAnimation } from "../../interaction/turn-anim.js";
import type { TurnScene } from "../../render/turn.js";
import {
  DEFAULT_PLAYBACK_SPEED_DEG_PER_SEC,
  PLAYBACK_SPEEDS_DEG_PER_SEC,
  SolvePlayback,
  steppedSpeed,
  type PlaybackSnapshot,
} from "../../interaction/solve-playback.js";
import type { Announcer } from "../announcer.js";
import { formatMoveFor, solutionText } from "../move-notation.js";
import { phaseName } from "../phase-names.js";
import { scrambleMovesForPuzzle } from "../puzzle-sizes.js";
import { icon } from "./icons.js";
import { MoveList } from "./move-list.js";
import { createTimeline, type Timeline } from "./timeline.js";
import { toasts } from "./toast.js";

/// The controls that act on the loaded puzzle: Scramble, Solve, undo and
/// redo, the algorithm picker, playback transport with its timeline,
/// speed, status and the move list. The shell owns the DOM, this owns what
/// the buttons do (docs/en/13-phase6-polish.md D7, D8, D9). One instance
/// per loaded puzzle; `dispose()` detaches every listener, so a puzzle
/// change leaves nothing pointing at the old one.

/// The elements the shell builds and hands over.
export interface SolveControlsElements {
  status: HTMLElement;
  /// The large current move, its phase and its count, shown with a solution.
  now: HTMLElement;
  nowMove: HTMLElement;
  nowPhase: HTMLElement;
  nowCount: HTMLElement;
  /// Where the phase timeline goes.
  timelineHost: HTMLElement;
  /// Undo, redo, Scramble and Solve, shown while no solution is loaded...
  idleGroup: HTMLElement;
  undoButton: HTMLButtonElement;
  redoButton: HTMLButtonElement;
  scrambleButton: HTMLButtonElement;
  solveButton: HTMLButtonElement;
  /// ...and the transport, shown while one is.
  transportGroup: HTMLElement;
  stepBackButton: HTMLButtonElement;
  playPauseButton: HTMLButtonElement;
  stepForwardButton: HTMLButtonElement;
  skipPhaseButton: HTMLButtonElement;
  copyButton: HTMLButtonElement;
  dismissButton: HTMLButtonElement;
  algoField: HTMLElement;
  algoSelect: HTMLSelectElement;
  speedField: HTMLElement;
  speedSlowerButton: HTMLButtonElement;
  speedFasterButton: HTMLButtonElement;
  speedLabel: HTMLElement;
  movesField: HTMLElement;
  movesToggle: HTMLButtonElement;
  moveList: HTMLElement;
}

/// A scramble that has just finished, for the address bar's share link.
export interface ScrambleInfo {
  seed: number;
  moves: number;
}

export interface SolveControlsDeps {
  puzzle: PuzzleHandle;
  /// The one TurnAnimation shared with the pointer controller (see
  /// interaction/pointer-controller.ts for why it must be the same).
  anim: TurnAnimation;
  /// Needed to resync after a scramble that snaps.
  scene: TurnScene;
  /// Hand-made turns, undo and redo (the same instance the drag uses).
  manual: ManualPlay;
  announcer: Announcer;
  /// Whether the move list starts open.
  movesShown: boolean;
  /// What was chosen last time on this puzzle (settings), if anything.
  preferredAlgorithm: string | null;
  preferredSpeed: number | null;
  /// Read each time Scramble is pressed, so the setting applies at once.
  animateScramble(): boolean;
  onMovesShownChange(shown: boolean): void;
  onAlgorithmChange(name: string): void;
  onSpeedChange(degPerSec: number): void;
  /// A scramble finished: the app puts its seed in the address bar.
  onScrambled(info: ScrambleInfo): void;
}

/// Speed as a multiple of the default: 90 deg/s is 0.25x, 360 is 1x...
export function speedLabelFor(degPerSec: number): string {
  const ratio = degPerSec / DEFAULT_PLAYBACK_SPEED_DEG_PER_SEC;
  return `${Number.isInteger(ratio) ? ratio : ratio.toFixed(2).replace(/0$/, "")}×`;
}

/// How long a solve may take before the status says tables are being
/// built: a first solve of a family computes them, and a status that said
/// only "Solving" for a couple of seconds would look hung.
const SLOW_SOLVE_NOTICE_MS = 300;

export class SolveControls {
  #els: SolveControlsElements;
  #deps: SolveControlsDeps;
  #playback: SolvePlayback;
  #scrambler: ScrambleRunner;
  #moveList: MoveList;
  #timeline: Timeline;
  #abort = new AbortController();
  #selectedAlgoId = 0;
  /// False until the algorithm list is known to have at least one entry;
  /// with none (a family without solvers) the panel is Scramble-only.
  #hasAlgorithms = false;
  #movesShown: boolean;
  #solving = false;
  #scrambling = false;
  /// The playback speed, kept here so it holds with no solution loaded.
  #speed: number;
  /// The scramble being run, reported when it lands.
  #pendingScramble: ScrambleInfo | null = null;
  /// Ends the tap-anywhere-to-skip listener when a scramble finishes.
  #scrambleSkipAbort: AbortController | null = null;
  /// The snapshot render() last painted from, kept so the click handlers
  /// can re-derive the right disabled state afterwards without guessing.
  #lastSnapshot: PlaybackSnapshot = {
    result: null,
    cursor: 0,
    playing: false,
    speedDegPerSec: DEFAULT_PLAYBACK_SPEED_DEG_PER_SEC,
    busy: false,
  };

  constructor(els: SolveControlsElements, deps: SolveControlsDeps) {
    this.#els = els;
    this.#deps = deps;
    this.#movesShown = deps.movesShown;
    this.#speed = deps.preferredSpeed ?? DEFAULT_PLAYBACK_SPEED_DEG_PER_SEC;
    this.#playback = new SolvePlayback(deps.anim, (snapshot) => this.#render(snapshot));
    this.#scrambler = new ScrambleRunner({
      puzzle: deps.puzzle,
      anim: deps.anim,
      scene: deps.scene,
      onDone: (ok) => this.#scrambleDone(ok),
      onRunningChange: (running) => this.#scrambleRunning(running),
    });
    this.#moveList = new MoveList(els.moveList, (move) => formatMoveFor(move, deps.puzzle.layout));
    this.#timeline = createTimeline({
      onSeek: (cursor) => this.#playback.seek(cursor),
      phaseLabel: (phase) => this.#phaseLabel(phase),
    });
    els.timelineHost.replaceChildren(this.#timeline.element);
    // Only now: setSpeed() reports, and reporting draws through the two above.
    this.#playback.setSpeed(this.#speed);

    // Hidden until the algorithm count is known, so a Scramble-only
    // puzzle never flashes an empty picker.
    els.algoField.hidden = true;
    els.solveButton.hidden = true;
    els.movesField.hidden = true;
    this.#wire();
    this.#applyMovesVisibility();
    this.#render(this.#lastSnapshot);
    void this.#populateAlgorithms();
  }

  /// Detaches every listener and drops the loaded solution. The elements
  /// stay in the page for the next puzzle's controls to take over.
  dispose(): void {
    this.#abort.abort();
    this.#playback.clear();
    this.#scrambler.dispose();
    this.#deps.announcer.stopPlayback();
  }

  /// Locks the controls while a different puzzle loads.
  setLoading(loading: boolean): void {
    if (loading) {
      this.#els.scrambleButton.disabled = true;
      this.#els.solveButton.disabled = true;
      this.#els.undoButton.disabled = true;
      this.#els.redoButton.disabled = true;
    } else {
      this.#render(this.#lastSnapshot);
    }
  }

  /// Whether a solution is loaded (the keyboard's arrows and Space then
  /// drive it).
  get hasSolution(): boolean {
    return this.#lastSnapshot.result !== null;
  }

  /// Whether something is running that a hand-made turn must not start on
  /// top of: a scramble, a solve search, a seek.
  get busy(): boolean {
    return this.#scrambling || this.#solving || this.#lastSnapshot.busy;
  }

  playPause(): void {
    if (!this.hasSolution) return;
    if (this.#lastSnapshot.playing) this.#playback.pause();
    else this.#playback.play();
  }

  stepForward(): void {
    this.#playback.stepForward();
  }

  stepBack(): void {
    this.#playback.stepBack();
  }

  /// The history or the settling state of hand-made turns changed.
  manualChanged(): void {
    this.#renderManualButtons();
  }

  /// The user turned the puzzle by hand: a loaded solution no longer
  /// describes it, so it is closed (DECISIONS.md, 2026-10-09).
  manualTurned(): void {
    if (this.#lastSnapshot.result) {
      this.#playback.clear();
      toasts.show("Solution closed: you turned the puzzle", "info");
    }
  }

  /// Scrambles to exactly the scramble `seed` and `moves` name (a shared
  /// link). Waits for nothing: the buttons lock like any other scramble.
  scrambleFrom(seed: number, moves: number): void {
    this.#startScramble(seed, moves);
  }

  #wire(): void {
    const { signal } = this.#abort;
    const on = (target: HTMLElement, type: string, handler: () => void): void => target.addEventListener(type, handler, { signal });
    const els = this.#els;
    const playback = this.#playback;
    const manual = this.#deps.manual;

    on(els.algoSelect, "change", () => {
      this.#selectedAlgoId = Number(els.algoSelect.value);
      const name = els.algoSelect.options[els.algoSelect.selectedIndex]?.textContent;
      if (name) this.#deps.onAlgorithmChange(name);
      playback.clear();
    });
    on(els.undoButton, "click", () => void manual.undo());
    on(els.redoButton, "click", () => void manual.redo());
    on(els.scrambleButton, "click", () => this.#startScramble(Math.floor(Math.random() * 0x100000000), this.#scrambleLength()));
    on(els.solveButton, "click", () => this.#solve());
    on(els.playPauseButton, "click", () => this.playPause());
    on(els.stepBackButton, "click", () => playback.stepBack());
    on(els.stepForwardButton, "click", () => playback.stepForward());
    on(els.skipPhaseButton, "click", () => playback.skipPhase());
    on(els.copyButton, "click", () => void this.#copySolution());
    on(els.dismissButton, "click", () => playback.clear());
    on(els.speedSlowerButton, "click", () => this.#changeSpeed(-1));
    on(els.speedFasterButton, "click", () => this.#changeSpeed(1));
    on(els.movesToggle, "click", () => {
      this.#movesShown = !this.#movesShown;
      this.#deps.onMovesShownChange(this.#movesShown);
      this.#applyMovesVisibility();
      if (this.#movesShown) this.#moveList.revealCurrent();
    });
  }

  #changeSpeed(delta: 1 | -1): void {
    this.#speed = steppedSpeed(this.#speed, delta);
    this.#playback.setSpeed(this.#speed);
    this.#deps.onSpeedChange(this.#speed);
  }

  #scrambleLength(): number {
    const { type, size } = this.#deps.puzzle.layout;
    return scrambleMovesForPuzzle(type, size);
  }

  #applyMovesVisibility(): void {
    const { movesField, movesToggle, moveList } = this.#els;
    movesField.hidden = !this.#hasAlgorithms;
    moveList.hidden = !this.#movesShown;
    movesToggle.textContent = this.#movesShown ? "Hide moves" : "Show moves";
    movesToggle.setAttribute("aria-expanded", String(this.#movesShown));
  }

  async #populateAlgorithms(): Promise<void> {
    const { puzzle, preferredAlgorithm } = this.#deps;
    const count = await puzzle.algorithmCount();
    const options: HTMLOptionElement[] = [];
    for (let i = 0; i < count; i++) {
      const option = document.createElement("option");
      option.value = String(i);
      option.textContent = await puzzle.algorithmName(i);
      options.push(option);
    }
    if (this.#abort.signal.aborted) return;
    this.#els.algoSelect.replaceChildren(...options);
    // The last choice on this puzzle, if that solver still exists.
    const remembered = options.findIndex((option) => option.textContent === preferredAlgorithm);
    if (remembered >= 0) {
      this.#els.algoSelect.selectedIndex = remembered;
      this.#selectedAlgoId = remembered;
    }
    this.#hasAlgorithms = count > 0;
    this.#els.algoField.hidden = !this.#hasAlgorithms;
    this.#els.solveButton.hidden = !this.#hasAlgorithms;
    this.#applyMovesVisibility();
    this.#render(this.#lastSnapshot);
  }

  // ---- Scramble ----
  //
  // Starting one locks every control synchronously, before the first
  // `await`, not just reactively via render(): render() only locks them
  // once a snapshot says playback is busy, which is a Worker round-trip
  // away, and a second click inside that window used to start a second
  // play() against the same SolvePlayback and corrupt its cursor
  // (DECISIONS.md, 2026-10-04).

  #startScramble(seed: number, moves: number): void {
    if (this.#scrambling) return;
    this.#scrambling = true;
    this.#playback.clear();
    this.#deps.manual.reset();
    this.#renderLocks();
    const info: ScrambleInfo = { seed, moves };
    this.#pendingScramble = info;
    void this.#scrambler.run(moves, seed, this.#deps.animateScramble());
  }

  #scrambleRunning(running: boolean): void {
    this.#scrambleSkipAbort?.abort();
    this.#scrambleSkipAbort = null;
    if (running) {
      // A tap anywhere skips the rest of an animated scramble.
      const skip = new AbortController();
      this.#scrambleSkipAbort = skip;
      window.addEventListener("pointerdown", () => this.#scrambler.skip(), { capture: true, signal: skip.signal });
      this.#els.status.textContent = "Scrambling… tap to skip";
    }
  }

  #scrambleDone(ok: boolean): void {
    this.#scrambling = false;
    const info = this.#pendingScramble;
    this.#pendingScramble = null;
    if (ok && info) {
      this.#deps.manual.arm();
      this.#deps.onScrambled(info);
      this.#deps.announcer.say("Scrambled");
    } else if (!ok) {
      toasts.show("The scramble failed. Reloading the page usually fixes it.", "error");
    }
    this.#render(this.#lastSnapshot);
  }

  // ---- Solve ----

  #solve(): void {
    if (this.#solving) return;
    this.#solving = true;
    this.#renderLocks();
    this.#els.status.textContent = "Solving…";
    const slow = window.setTimeout(() => {
      this.#els.status.textContent = "Solving… building tables on the first solve";
    }, SLOW_SOLVE_NOTICE_MS);
    void (async () => {
      try {
        const result = await this.#deps.puzzle.solve(this.#selectedAlgoId);
        if (!result) {
          toasts.show("No solution found. Scramble again and retry.", "error");
          return;
        }
        this.#deps.manual.reset();
        this.#playback.load(result);
        this.#playback.play();
      } catch (error) {
        console.error("Solve failed:", error);
        toasts.show("The solve failed. See the console for details.", "error");
      } finally {
        window.clearTimeout(slow);
        this.#solving = false;
        this.#render(this.#lastSnapshot);
      }
    })();
  }

  async #copySolution(): Promise<void> {
    const result = this.#lastSnapshot.result;
    if (!result) return;
    try {
      await navigator.clipboard.writeText(solutionText(result.moves, this.#deps.puzzle.layout));
      toasts.show("Solution copied", "success");
    } catch {
      toasts.show("Could not copy. Select the moves in the list and copy them instead.", "error");
    }
  }

  // ---- Drawing ----

  #phaseLabel(phase: number): string {
    const algoName = this.#els.algoSelect.options[this.#els.algoSelect.selectedIndex]?.textContent ?? "";
    return phaseName(algoName, phase);
  }

  /// Everything that depends on "is something running" and not on the
  /// playback snapshot.
  #renderLocks(): void {
    const els = this.#els;
    const snapshot = this.#lastSnapshot;
    const locked = snapshot.playing || snapshot.busy || this.#solving || this.#scrambling;
    els.scrambleButton.disabled = locked;
    els.solveButton.disabled = locked;
    els.solveButton.toggleAttribute("data-busy", this.#solving);
    els.solveButton.replaceChildren(
      ...(this.#solving ? [Object.assign(document.createElement("span"), { className: "spinner" })] : [icon("solve")]),
      Object.assign(document.createElement("span"), { textContent: this.#solving ? "Solving" : "Solve" }),
    );
    this.#renderManualButtons();
  }

  #renderManualButtons(): void {
    const { manual } = this.#deps;
    const idle = !this.#solving && !this.#scrambling && !this.#lastSnapshot.playing;
    this.#els.undoButton.disabled = !(idle && manual.canUndo);
    this.#els.redoButton.disabled = !(idle && manual.canRedo);
  }

  #render(snapshot: PlaybackSnapshot): void {
    const previous = this.#lastSnapshot;
    this.#lastSnapshot = snapshot;
    const els = this.#els;
    const hasResult = snapshot.result !== null;
    const atEnd = hasResult && snapshot.cursor >= snapshot.result!.moves.length;

    // The bar is either undo/redo/Scramble/Solve, or the transport.
    els.idleGroup.hidden = hasResult;
    els.transportGroup.hidden = !hasResult;
    els.speedField.hidden = !hasResult;
    els.now.hidden = !hasResult;
    els.timelineHost.hidden = !hasResult;
    els.status.hidden = hasResult;

    els.playPauseButton.replaceChildren(icon(snapshot.playing ? "pause" : "play"));
    els.playPauseButton.setAttribute("aria-label", snapshot.playing ? "Pause" : "Play");
    els.playPauseButton.disabled = !hasResult || (atEnd && !snapshot.playing);
    els.stepBackButton.disabled = !hasResult || snapshot.busy || snapshot.cursor === 0;
    els.stepForwardButton.disabled = !hasResult || snapshot.busy || atEnd;
    // Mid-move clicks queue (SolvePlayback.skipPhase), so only the end disables it.
    els.skipPhaseButton.disabled = !hasResult || atEnd;
    els.speedLabel.textContent = speedLabelFor(snapshot.speedDegPerSec);
    els.speedSlowerButton.disabled = snapshot.speedDegPerSec <= PLAYBACK_SPEEDS_DEG_PER_SEC[0]!;
    els.speedFasterButton.disabled = snapshot.speedDegPerSec >= PLAYBACK_SPEEDS_DEG_PER_SEC[PLAYBACK_SPEEDS_DEG_PER_SEC.length - 1]!;
    this.#moveList.render(snapshot);
    this.#timeline.render(snapshot.result, snapshot.cursor);
    this.#renderLocks();

    if (!hasResult) {
      if (previous.result) this.#deps.announcer.stopPlayback();
      if (!this.#solving && !this.#scrambling) {
        els.status.textContent = this.#hasAlgorithms
          ? "Scramble, then Solve."
          : "No solver for this puzzle yet. Scramble it and turn it by hand.";
      }
      return;
    }
    this.#renderNow(snapshot, previous);
  }

  /// The large current move, its phase and count, and what a screen reader
  /// hears (throttled, so a fast playback is not read out move by move).
  #renderNow(snapshot: PlaybackSnapshot, previous: PlaybackSnapshot): void {
    const els = this.#els;
    const { moves, phaseTags } = snapshot.result!;
    const layout = this.#deps.puzzle.layout;
    const finished = moves.length === 0 || snapshot.cursor >= moves.length;
    const index = Math.min(snapshot.cursor, Math.max(moves.length - 1, 0));
    const phase = moves.length === 0 ? 0 : phaseTags[index]!;
    const algoName = els.algoSelect.options[els.algoSelect.selectedIndex]?.textContent ?? "";

    els.nowMove.textContent = moves.length === 0 ? "Solved" : finished ? "Done" : formatMoveFor(moves[index]!, layout);
    els.nowPhase.textContent = moves.length === 0 ? `${algoName}: already solved` : finished ? "Solved" : phaseName(algoName, phase);
    els.nowCount.textContent = moves.length === 0 ? "" : `${finished ? moves.length : snapshot.cursor + 1} / ${moves.length}`;

    if (snapshot.result !== previous.result || snapshot.cursor !== previous.cursor) {
      const spoken = moves.length === 0
        ? "Already solved"
        : finished
          ? "Solved"
          : `${els.nowMove.textContent}. ${els.nowPhase.textContent}. Move ${snapshot.cursor + 1} of ${moves.length}`;
      this.#deps.announcer.playback(spoken);
    }
  }
}
