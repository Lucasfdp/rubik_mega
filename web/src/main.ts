import type { Group } from "three";
import { PuzzleType } from "./engine/index.js";
import { RemotePuzzle } from "./engine/remote-puzzle.js";
import { buildCubicScene } from "./render/scene.js";
import { cubicDragAdapter } from "./interaction/cubic-drag.js";
import { MoveMap } from "./interaction/move-axis.js";
import type { MoveModel, TurnScene } from "./render/turn.js";
import type { DragAdapter } from "./interaction/drag-adapter.js";
import { PointerController } from "./interaction/pointer-controller.js";
import { ManualPlay } from "./interaction/manual-play.js";
import { TurnAnimation } from "./interaction/turn-anim.js";
import { approach, framingFor, freeRectFor, type Insets, type Viewport } from "./interaction/framing.js";
import { FrameStats } from "./render/frame-stats.js";
import { FrameScheduler, browserClock } from "./render/frame-scheduler.js";
import { AdaptivePixelRatio, TIER_SETTINGS, pixelRatioFor, resolveTier, type DeviceHints, type QualityTier } from "./render/quality.js";
import { createStage } from "./render/stage.js";
import { Shell } from "./ui/shell/shell.js";
import { Celebration } from "./ui/celebration.js";
import { showFatal, removeSplash } from "./ui/fatal.js";
import { KeyboardControls } from "./ui/keyboard-controls.js";
import { prefersReducedMotion } from "./ui/motion.js";
import { DEFAULT_SIZE, puzzleFromKey, puzzleKey, puzzleTitle } from "./ui/puzzle-sizes.js";
import { shareLink } from "./ui/share.js";
import { toasts } from "./ui/shell/toast.js";
import { buildLink, parseLink } from "./ui/url-state.js";
import { lookFor, type LookOptions } from "./render/glyphs.js";
import { applyAppearance, loadSettings, saveSettings, type Settings } from "./ui/settings.js";
import { debugRequested } from "./ui/debug-flag.js";
import "./ui/shell/shell.css";

// The app's wiring: the WebGL stage, the render-on-demand loop, the
// current puzzle's session, and the shell around it.
//
// docs/en/04-roadmap.md Phase 0: the WASM wrapper (engine/), the real
// lattice (render/), pointer-drag turning + orbit/pinch (interaction/,
// docs/en/03-architecture.md S7's decided convention), and the Worker
// move -- the WASM module runs in worker/puzzle-worker.ts, reached
// through RemotePuzzle, so a real solve never blocks this thread's
// render loop.
//
// docs/en/06-phase2-solvers.md's web UI: a single TurnAnimation is built
// per puzzle and shared between PointerController (pointer-drag turns)
// and the solve controls' SolvePlayback (queued solve moves) -- see
// pointer-controller.ts's constructor doc comment for why it must be the
// same instance, not one each.
//
// docs/en/13-phase6-polish.md: frames are drawn only when something
// changed (D1), the three.js set-up lives in render/stage.ts, and the
// interface is ui/shell (D7).

/// The scene a family builds, as far as this file needs to know.
interface FamilyScene extends TurnScene {
  group: Group;
  boundingRadius: number;
  dispose(): void;
}

/// Everything that belongs to one puzzle instance. A puzzle or size
/// change (docs/en/07-nxn-web-view.md D5) tears all of it down and
/// builds a fresh one -- the Worker included, since RemotePuzzle.destroy()
/// terminates it -- rather than trying to resize anything in place.
interface Session {
  puzzle: RemotePuzzle;
  scene: FamilyScene;
  controller: PointerController;
  /// Hand-made turns, undo and redo for this puzzle.
  manual: ManualPlay;
  celebration: Celebration;
  /// The distance the controller's zoom is relative to, for handing its
  /// camera to a successor.
  fit: number;
}

/// How quickly the puzzle glides to a new framing when the interface
/// opens or closes (share of the remaining gap per second).
const FRAMING_RATE_PER_SEC = 14;
/// A framing gap under this many pixels is treated as arrived.
const FRAMING_EPSILON_PX = 0.5;

let settings: Settings = loadSettings();
applyAppearance(settings);

function deviceHints(): DeviceHints {
  return {
    hardwareConcurrency: typeof navigator.hardwareConcurrency === "number" ? navigator.hardwareConcurrency : undefined,
    shortSidePx: Math.min(window.screen.width, window.screen.height),
  };
}

let tier: QualityTier = resolveTier(settings.quality, deviceHints());
let adaptive = new AdaptivePixelRatio(TIER_SETTINGS[tier].maxPixelRatio);
const stats = new FrameStats();

function currentPixelRatio(): number {
  return pixelRatioFor(window.devicePixelRatio, tier, adaptive.cap);
}

const stage = createStage(document.body, TIER_SETTINGS[tier], currentPixelRatio());

let session: Session | null = null;
// Bumped by every load: a slow load that finishes after a newer one
// started is discarded instead of replacing it.
let loadGeneration = 0;
let firstFrameDrawn = false;

// ---- The interface ----

/// The scramble on screen, for the address bar and the Share button.
let currentScramble: { seed: number; moves: number } | null = null;

const shell = new Shell({
  settings,
  callbacks: {
    onPuzzleChange: (type, size) => {
      loadPuzzle(type, size).catch(reportLoadFailure);
    },
    onSettingsChange: (next) => applySettings(next),
    onScrambled: (info) => {
      currentScramble = info;
      syncAddressBar();
    },
    onShare: () => void share(),
  },
  describeAutoQuality: () => `Auto uses ${resolveTier("auto", deviceHints())} on this device.`,
});
document.body.appendChild(shell.element);

// ---- Framing: where the puzzle sits in the space the interface leaves ----

let targetInsets: Insets = { top: 0, right: 0, bottom: 0, left: 0 };
let shownInsets: Insets = targetInsets;
let lastViewport: Viewport = { width: 0, height: 0 };

function viewport(): Viewport {
  return { width: window.innerWidth, height: window.innerHeight };
}

function insetsEqual(a: Insets, b: Insets): boolean {
  return a.top === b.top && a.right === b.right && a.bottom === b.bottom && a.left === b.left;
}

/// Reads the interface's insets afresh. Snaps to them when the screen
/// itself changed (a rotation, a resize) and glides when only the
/// interface moved (the sheet opening), unless the user prefers no motion.
function layoutChanged(): void {
  const now = viewport();
  const screenChanged = now.width !== lastViewport.width || now.height !== lastViewport.height;
  lastViewport = now;
  targetInsets = shell.insets();
  if (screenChanged || prefersReducedMotion()) shownInsets = targetInsets;
  stage.resize(now.width, now.height);
  scheduler.request();
}
shell.onLayoutChange(layoutChanged);
window.addEventListener("resize", layoutChanged);

/// Moves the shown insets towards the target. Returns whether they have
/// not arrived yet.
function stepInsets(dtSec: number): boolean {
  if (insetsEqual(shownInsets, targetInsets)) return false;
  const step = (current: number, target: number): number => approach(current, target, dtSec, FRAMING_RATE_PER_SEC, FRAMING_EPSILON_PX);
  shownInsets = {
    top: step(shownInsets.top, targetInsets.top),
    right: step(shownInsets.right, targetInsets.right),
    bottom: step(shownInsets.bottom, targetInsets.bottom),
    left: step(shownInsets.left, targetInsets.left),
  };
  return !insetsEqual(shownInsets, targetInsets);
}

/// Points the camera's projection at the puzzle's place in the free area
/// and returns the distance at which it fits there.
function applyFraming(boundingRadius: number): number {
  const screen = viewport();
  const framing = framingFor(boundingRadius, screen, freeRectFor(screen, shownInsets));
  const { camera } = stage;
  camera.aspect = screen.width / screen.height;
  camera.setViewOffset(screen.width, screen.height, framing.offsetX, framing.offsetY, screen.width, screen.height);
  camera.updateProjectionMatrix();
  return framing.distance;
}

// ---- The frame loop ----

/// One frame. Returns whether anything is still moving; when nothing is,
/// the scheduler stops asking for frames and the GPU is left alone.
function frame(dtSec: number, nowMs: number): boolean {
  const gliding = stepInsets(dtSec);
  // The celebration's turn of the view comes before the camera is applied.
  const celebrating = session?.celebration.update(dtSec) ?? false;
  if (session) {
    session.fit = applyFraming(session.scene.boundingRadius);
    session.controller.setFit(session.fit);
    session.controller.update(dtSec);
  }
  stage.render();

  if (!firstFrameDrawn) {
    firstFrameDrawn = true;
    removeSplash();
  }
  // Only the interval between two frames that followed each other says
  // anything about speed; the first after a rest has dt 0.
  if (dtSec > 0) {
    const frameMs = dtSec * 1000;
    stats.record(frameMs);
    const lowered = adaptive.observe(frameMs, nowMs);
    if (lowered !== null) stage.setPixelRatio(currentPixelRatio());
  }
  return gliding || celebrating || (session?.controller.isBusy() ?? false);
}

const scheduler = new FrameScheduler(browserClock, frame);

// ---- Puzzle sessions ----

/// What each puzzle family needs to be shown and turned: its scene, how
/// its moves map to turns, and how a drag picks one
/// (docs/en/09-phase3-skewb-pyraminx.md). The cubic family is in the main
/// chunk because it opens first; the others load on demand
/// (docs/en/13-phase6-polish.md D3).
async function buildFamily(
  puzzle: RemotePuzzle,
  tierSettings: (typeof TIER_SETTINGS)[QualityTier],
  look: LookOptions,
): Promise<{ scene: FamilyScene; moves: MoveModel; drag: DragAdapter }> {
  const { type, size, generators, cut } = puzzle.layout;
  if (type === PuzzleType.Cubic) {
    const cubic = buildCubicScene(puzzle, tierSettings, look);
    const moves = new MoveMap(size, generators);
    return { scene: cubic, moves, drag: cubicDragAdapter(cubic.geometry, moves) };
  }
  if (type === PuzzleType.Square1) {
    const [{ buildSquare1Scene }, { square1DragAdapter }, { Square1MoveModel }] = await Promise.all([
      import("./render/square1/square1-scene.js"),
      import("./interaction/square1-drag.js"),
      import("./interaction/square1-moves.js"),
    ]);
    const square1 = buildSquare1Scene(puzzle, tierSettings, look);
    const moves = new Square1MoveModel();
    return { scene: square1, moves, drag: square1DragAdapter(square1, puzzle, moves) };
  }
  if (!cut) throw new Error(`puzzle type ${PuzzleType[type] ?? type} has no cut-plane layout`);
  const [{ buildCutScene }, { cutDragAdapter }, { CutMoveModel }] = await Promise.all([
    import("./render/cut/cut-scene.js"),
    import("./interaction/cut-drag.js"),
    import("./interaction/cut-moves.js"),
  ]);
  const cutScene = buildCutScene(puzzle, tierSettings, look);
  const moves = new CutMoveModel(cut.generators.length, puzzle.layout.steps);
  return { scene: cutScene, moves, drag: cutDragAdapter(cutScene, cut, moves) };
}

/// Builds a scene, controller and controls for `puzzle` and swaps them in
/// for `previous`. The previous session's puzzle is destroyed only when
/// `replacePuzzle` says it is being replaced by a different one.
async function installSession(puzzle: RemotePuzzle, previous: Session | null, replacePuzzle: boolean): Promise<Session> {
  const family = await buildFamily(puzzle, TIER_SETTINGS[tier], currentLook(puzzle.layout.type));
  if (previous) {
    previous.celebration.dispose();
    previous.controller.dispose();
    stage.scene.remove(previous.scene.group);
    previous.scene.dispose();
    if (replacePuzzle) previous.puzzle.destroy();
  }
  // Every change the scene or the animation makes wakes the frame loop.
  const notifying: TurnScene = {
    sync: () => {
      family.scene.sync();
      scheduler.request();
    },
    setActiveTurn: (turn) => {
      family.scene.setActiveTurn(turn);
      scheduler.request();
    },
  };
  stage.scene.add(family.scene.group);
  stage.setSubject(family.scene.boundingRadius);

  const anim = new TurnAnimation(puzzle, notifying, family.moves, () => scheduler.request(), prefersReducedMotion);
  const fit = applyFraming(family.scene.boundingRadius);
  const manual = new ManualPlay(anim, puzzle, {
    onChange: () => shell.controls?.manualChanged(),
    onManualTurn: () => shell.controls?.manualTurned(),
    onSolved: (turns) => celebrateSolve(turns),
  });
  const controller = new PointerController(stage.renderer.domElement, stage.camera, notifying, family.drag, anim, previous?.controller.orbit, {
    onActivity: () => scheduler.request(),
    inertia: !prefersReducedMotion(),
    fitDistance: fit,
    orbitFitDistance: previous?.fit,
    commitOptions: (move) => manual.commitOptions(move),
  });
  const celebration = new Celebration({
    nudgeYaw: (yawDeg) => controller.nudgeOrbit(yawDeg, 0),
    requestFrame: () => scheduler.request(),
    colours: () => currentLook(puzzle.layout.type).palette,
  });
  if (replacePuzzle) keyboard.clearSelection();
  shell.bind({ puzzle, anim, scene: notifying, manual });
  const next: Session = { puzzle, scene: family.scene, controller, manual, celebration, fit };
  session = next;
  layoutChanged();
  scheduler.request();
  return next;
}

/// The sticker colours and marks the settings ask for.
function currentLook(type: PuzzleType): LookOptions {
  return lookFor(type, settings.palette, settings.glyphs);
}

/// A hand-turned puzzle just became solved.
function celebrateSolve(turns: number): void {
  const message = `Solved in ${turns} ${turns === 1 ? "move" : "moves"}`;
  toasts.show(message, "success");
  shell.announcer.say(message);
  session?.celebration.play();
}

// ---- The address bar and sharing (docs/en/13-phase6-polish.md D10) ----

/// Writes the puzzle (and scramble) on screen into the address bar, so the
/// URL is always a link to what is shown. Where the page may not touch its
/// own history (a sandboxed frame), the link is simply not kept up to date.
function syncAddressBar(): void {
  if (!session) return;
  const { type, size } = session.puzzle.layout;
  try {
    window.history.replaceState(null, "", `${window.location.pathname}${buildLink(window.location.search, { type, size }, currentScramble)}${window.location.hash}`);
  } catch {
    // not kept up to date
  }
}

async function share(): Promise<void> {
  if (!session) return;
  const { type, size } = session.puzzle.layout;
  const outcome = await shareLink(window.location.href, `rubik_mega: ${puzzleTitle(type, size)}`);
  if (outcome === "copied") toasts.show("Link copied", "success");
  else if (outcome === "failed") toasts.show("Could not share the link. Copy it from the address bar.", "error");
}

/// A puzzle that would not open. With one already showing, that stays
/// usable and the user is told; with none there is nothing to fall back on.
function reportLoadFailure(error: unknown): void {
  if (!session) {
    showFatal(error);
    return;
  }
  console.error("failed to open a puzzle:", error);
  shell.setLoading(false);
  toasts.show("That puzzle could not be opened. The previous one is still here.", "error");
}

async function loadPuzzle(type: PuzzleType, size: number): Promise<void> {
  const generation = ++loadGeneration;
  currentScramble = null;
  shell.setLoading(true);
  const worker = new Worker(new URL("../worker/puzzle-worker.ts", import.meta.url), { type: "module" });
  const puzzle = await RemotePuzzle.create(worker, type, size);
  if (generation !== loadGeneration) {
    puzzle.destroy();
    return;
  }
  try {
    await installSession(puzzle, session, true);
  } catch (error) {
    puzzle.destroy();
    throw error;
  }
  if (generation === loadGeneration) {
    shell.setLoading(false);
    syncAddressBar();
    rememberSetting({ lastPuzzle: puzzleKey(type, size) });
  }
}

/// Saves a setting the app itself changed (not the settings dialog), and
/// tells the shell so its copy does not overwrite it later.
function rememberSetting(patch: Partial<Settings>): void {
  const next = { ...settings, ...patch };
  shell.updateSettings(next);
  applySettings(next);
}

// ---- Settings ----

function applySettings(next: Settings): void {
  const previous = settings;
  settings = next;
  saveSettings(next);
  applyAppearance(next);
  // The interface size changes every rem, which moves the bars.
  if (next.uiScale !== previous.uiScale) requestAnimationFrame(layoutChanged);
  const lookChanged = next.palette !== previous.palette || next.glyphs !== previous.glyphs;
  if (next.quality === previous.quality) {
    // Colours and marks are baked into the scene's materials.
    if (lookChanged && session) installSession(session.puzzle, session, false).catch(showFatal);
    return;
  }
  const resolved = resolveTier(next.quality, deviceHints());
  if (resolved === tier) {
    if (lookChanged && session) installSession(session.puzzle, session, false).catch(showFatal);
    return;
  }
  tier = resolved;
  adaptive = new AdaptivePixelRatio(TIER_SETTINGS[tier].maxPixelRatio);
  stage.applyTier(TIER_SETTINGS[tier]);
  stage.setPixelRatio(currentPixelRatio());
  // Geometry depends on the tier (rounded or sharp bodies), so the scene
  // is rebuilt over the same puzzle: the scramble in progress survives.
  if (session) installSession(session.puzzle, session, false).catch(showFatal);
}

async function main(): Promise<void> {
  if (debugRequested(window.location.search)) {
    const { mountDebugOverlay } = await import("./ui/debug-overlay.js");
    mountDebugOverlay({
      renderer: stage.renderer,
      stats,
      scheduler,
      tier: () => tier,
      pixelRatio: currentPixelRatio,
      environmentMs: stage.environmentBuildMs,
    });
  }
  // The address bar wins over what was open last time, which wins over the cube.
  const link = parseLink(window.location.search);
  const remembered = settings.lastPuzzle ? puzzleFromKey(settings.lastPuzzle) : null;
  const first = link.puzzle ?? remembered ?? { type: PuzzleType.Cubic, size: DEFAULT_SIZE };
  await loadPuzzle(first.type, first.size);
  // A shared scramble replays on opening.
  if (link.scramble && link.puzzle) shell.controls?.scrambleFrom(link.scramble.seed, link.scramble.moves);
}

const keyboard = new KeyboardControls({
  layout: () => session?.puzzle.layout ?? null,
  manual: () => session?.manual ?? null,
  busy: () => shell.controls?.busy ?? true,
  hasSolution: () => shell.controls?.hasSolution ?? false,
  playPause: () => shell.controls?.playPause(),
  stepForward: () => shell.controls?.stepForward(),
  stepBack: () => shell.controls?.stepBack(),
  nudgeOrbit: (yawDeg, pitchDeg) => session?.controller.nudgeOrbit(yawDeg, pitchDeg),
  resetView: () => session?.controller.resetView(),
  showHelp: () => shell.showShortcuts(),
  say: (text) => {
    shell.announcer.say(text);
    toasts.show(text, "info");
  },
});

// A promise nobody caught is a bug, but not worth a dead page: say so.
window.addEventListener("unhandledrejection", (event) => {
  console.error("unhandled rejection:", event.reason);
  if (session) toasts.show("Something went wrong. If the puzzle stops responding, reload the page.", "error");
});

main().catch(showFatal);
