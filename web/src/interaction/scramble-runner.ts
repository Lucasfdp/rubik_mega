import type { PuzzleHandle } from "../engine/puzzle-handle.js";
import type { Move } from "../engine/types.js";
import { SolvePlayback } from "./solve-playback.js";
import { planScrambleAnimation } from "./scramble-plan.js";
import type { TurnAnimation } from "./turn-anim.js";
import type { TurnScene } from "../render/turn.js";

/// Scrambles the puzzle, either at once or as an animation
/// (docs/en/13-phase6-polish.md D9). The animated form is the same
/// SolvePlayback that plays a solution, fed the scramble's moves instead:
/// one queue-driving implementation, not two, and it already has the
/// staleness guards and the "jump over the rest" a tap-to-skip needs.
///
/// Whichever way it runs, the puzzle ends in the state core's
/// puzzle_scramble(count, seed) leaves it in: the animated moves are
/// core's own list for that seed (PuzzleHandle.scrambleMoves), applied one
/// by one, and the instant form is that same call made directly.
export interface ScrambleRunnerDeps {
  puzzle: PuzzleHandle;
  anim: TurnAnimation;
  scene: TurnScene;
  /// Called when the scramble has fully landed (or failed), with whether
  /// it worked.
  onDone(ok: boolean): void;
  /// Called when the animation starts and when it ends, so the caller can
  /// lock its controls and listen for a tap to skip.
  onRunningChange(running: boolean): void;
}

export class ScrambleRunner {
  readonly #deps: ScrambleRunnerDeps;
  readonly #playback: SolvePlayback;
  #running = false;

  constructor(deps: ScrambleRunnerDeps) {
    this.#deps = deps;
    this.#playback = new SolvePlayback(deps.anim, (snapshot) => {
      if (!this.#running || !snapshot.result) return;
      if (snapshot.cursor >= snapshot.result.moves.length && !snapshot.busy) this.#finish(true);
    });
  }

  get running(): boolean {
    return this.#running;
  }

  /// Scrambles with `moveCount` random moves from `seed`. With `animate`
  /// the moves are played as turns; otherwise the puzzle snaps there.
  /// Never throws: a failure is reported through onDone(false).
  async run(moveCount: number, seed: number, animate: boolean): Promise<void> {
    const { puzzle, anim, scene } = this.#deps;
    this.#running = true;
    this.#deps.onRunningChange(true);
    try {
      if (!animate) {
        await puzzle.scramble(moveCount, seed);
        scene.sync();
        this.#finish(true);
        return;
      }
      const moves = await puzzle.scrambleMoves(moveCount, seed);
      const plan = planScrambleAnimation(moves, (move: Move) => anim.moves.quarterDegForAmount(move.generator, move.amount));
      if (plan.instant.length > 0) await anim.jump(plan.instant);
      if (plan.animated.length === 0) {
        this.#finish(true);
        return;
      }
      this.#playback.load({ moves: plan.animated, phaseTags: plan.animated.map(() => 0) });
      this.#playback.setSpeed(plan.speedDegPerSec);
      this.#playback.play();
    } catch (error) {
      console.error("Scramble failed:", error);
      this.#finish(false);
    }
  }

  /// Applies whatever is left of a running animated scramble at once, so
  /// the user need not wait for it. A no-op when nothing is running.
  skip(): void {
    if (this.#running) this.#playback.skipPhase();
  }

  /// Drops a running scramble without applying the rest (the puzzle is
  /// being replaced).
  dispose(): void {
    this.#running = false;
    this.#playback.clear();
  }

  #finish(ok: boolean): void {
    if (!this.#running) return;
    this.#running = false;
    this.#playback.clear();
    this.#deps.onRunningChange(false);
    this.#deps.onDone(ok);
  }
}
