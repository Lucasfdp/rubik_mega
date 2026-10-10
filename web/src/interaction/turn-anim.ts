import type { Axis } from "../render/lattice/cubic.js";
import type { PuzzleHandle } from "../engine/puzzle-handle.js";
import type { Move } from "../engine/types.js";
import type { ActiveTurn, MoveModel, TurnScene, TurnTarget } from "../render/turn.js";

/// Degrees of rotation per second -- ported from rubik's
/// ANIM_DEFAULT_SPEED (src/render/anim.c).
const ANIM_SPEED_DEG_PER_SEC = 360;

/// Floor on a drag hand-off's animation length -- ported from rubik's
/// ANIM_RELEASE_MIN_SEC: a release right at the target angle (from ===
/// to, e.g. a settle from 0) would otherwise divide out to a
/// zero-length animation and pop instead of ease.
export const ANIM_RELEASE_MIN_SEC = 0.06;

/// What every turn runs at when the user asked for reduced motion
/// (docs/en/13-phase6-polish.md D11): the top of the playback speeds, so a
/// turn is over in the floor time above and reads as a change of state
/// rather than a movement.
export const REDUCED_MOTION_SPEED_DEG_PER_SEC = 5760;

function easeOutCubic(t: number): number {
  const inv = 1 - t;
  return 1 - inv * inv * inv;
}

/// Extra, purely additive hooks for one in-flight animation --
/// everything here defaults to today's behaviour when omitted, so
/// existing pointer-drag callers (pointer-controller.ts) that pass
/// neither argument are completely unaffected.
export interface TurnOptions {
  /// Degrees per second for this one turn, overriding
  /// ANIM_SPEED_DEG_PER_SEC -- added for SolvePlayback's speed slider
  /// (docs/en/06-phase2-solvers.md's web UI), which needs to vary
  /// playback speed without that becoming a case this file has to know
  /// about (a drag hand-off never sets this).
  speedDegPerSec?: number;
  /// Called once this animation has FULLY settled -- after the
  /// committed move's applyMove() Promise resolves and scene.sync()
  /// has run (beginFrom), or immediately after a settle's own sync()
  /// (beginSettle). Deliberately a new, separate hook rather than
  /// widening isIdle()'s own meaning: isIdle() stays exactly what
  /// web/tests/interaction/turn-anim.test.ts already locks in --
  /// true as soon as the eased angle reaches its target, synchronously,
  /// before any async commit lands -- because pointer-controller.ts's
  /// drag handling depends on that early flip (it's what lets a new
  /// drag start the instant the visual animation finishes, not after
  /// a Worker round-trip). SolvePlayback needs the LATER, stronger
  /// guarantee ("state is really settled") to know when it's safe to
  /// queue its next move, so it gets its own callback instead.
  onSettled?: () => void;
}

interface InFlight {
  /// null for a "settle" (a cancelled/blocked drag easing back to 0,
  /// committing nothing) -- otherwise the move to commit on completion.
  move: { generator: number; amount: number } | null;
  target: TurnTarget;
  fromDeg: number;
  toDeg: number;
  elapsedSec: number;
  durationSec: number;
  onSettled?: () => void;
}

/// Single-slot turn-animation state: at its core this only ever has ONE
/// turn in flight at a time (docs/en/04-roadmap.md Phase 0 step 3 is
/// pointer interaction, not autoplay), so this is a deliberately
/// narrower port of rubik's t_anim_state (render/anim.h) -- no ease-IN
/// path, because nothing in this project produces a move any way other
/// than a drag hand-off or a queued solve step (always ease-OUT,
/// rubik's own §5.3/B5 case). TurnOptions' `speedDegPerSec`/`onSettled`
/// (added for interaction/solve-playback.ts, docs/en/06-phase2-solvers.md's
/// web UI) are the one deliberate exception to "narrower": they let an
/// external move QUEUE (SolvePlayback) drive this single slot one move
/// at a time without this class growing a queue of its own -- the slot
/// itself, and isIdle()'s existing synchronous-on-eased-angle contract,
/// are unchanged.
export class TurnAnimation {
  #puzzle: PuzzleHandle;
  #scene: TurnScene;
  #current: InFlight | null = null;
  /// This puzzle's generator <-> layer mapping -- public so the two
  /// other move sources sharing this one animation slot
  /// (PointerController, SolvePlayback) resolve moves against the same
  /// table instead of each being handed their own copy.
  readonly moves: MoveModel;
  /// Called whenever this animation starts something that needs frames
  /// drawn (a turn, a settle, an instant jump). The render-on-demand
  /// loop (render/frame-scheduler.ts) is asleep at rest, and a solve
  /// playback starts a turn with no pointer event to wake it.
  #onWake: (() => void) | undefined;
  /// Asked at the start of each turn, so a change of the system setting
  /// applies to the next turn.
  #reducedMotion: () => boolean;

  constructor(puzzle: PuzzleHandle, scene: TurnScene, moves: MoveModel, onWake?: () => void, reducedMotion: () => boolean = () => false) {
    this.#puzzle = puzzle;
    this.#scene = scene;
    this.moves = moves;
    this.#onWake = onWake;
    this.#reducedMotion = reducedMotion;
  }

  /// The speed a turn runs at: its own if it asked for one, the default
  /// otherwise, and always the fastest under reduced motion.
  #speedFor(options?: TurnOptions): number {
    if (this.#reducedMotion()) return REDUCED_MOTION_SPEED_DEG_PER_SEC;
    return options?.speedDegPerSec ?? ANIM_SPEED_DEG_PER_SEC;
  }

  /// Applies `moves` instantly -- no animation -- and resyncs the scene,
  /// resolving once both are done. For skipping through a long solve
  /// (docs/en/08-nxn-reduction.md D5). Only valid while idle; it goes
  /// through the same puzzle and scene as every animated turn, so the
  /// two can never disagree about what the cube looks like.
  async jump(moves: readonly Move[]): Promise<void> {
    if (!this.isIdle()) throw new Error("TurnAnimation.jump() called while a turn is in flight");
    await this.#puzzle.applyMoves(moves);
    this.#scene.sync();
    this.#onWake?.();
  }

  isIdle(): boolean {
    return this.#current === null;
  }

  /// Starts `move` animating from `fromDeg` to `toDeg`, easing OUT --
  /// ported from rubik's anim_begin_from(): the drag was already moving
  /// when the pointer released, so easing back IN (slow start) would
  /// make it visibly stall before continuing. Only valid while idle.
  beginFrom(
    move: { generator: number; amount: number },
    fromDeg: number,
    toDeg: number,
    options?: TurnOptions,
  ): void {
    const target = this.moves.targetForGenerator(move.generator);
    const speed = this.#speedFor(options);
    this.#current = {
      move,
      target,
      fromDeg,
      toDeg,
      elapsedSec: 0,
      durationSec: Math.max(Math.abs(toDeg - fromDeg) / speed, ANIM_RELEASE_MIN_SEC),
      onSettled: options?.onSettled,
    };
    this.#onWake?.();
  }

  /// Starts a "settle": eases `axis`/`layer` from `fromDeg` back to 0
  /// and commits nothing -- ported from rubik's anim_begin_settle(), for
  /// a cancelled or blocked-slice drag. Only valid while idle.
  beginSettle(axis: Axis, layer: number, fromDeg: number, options?: TurnOptions): void {
    this.beginSettleTarget({ axis, layer }, fromDeg, options);
  }

  /// beginSettle() for any family's turn target.
  beginSettleTarget(target: TurnTarget, fromDeg: number, options?: TurnOptions): void {
    this.#current = {
      move: null,
      target,
      fromDeg,
      toDeg: 0,
      elapsedSec: 0,
      durationSec: Math.max(Math.abs(fromDeg) / this.#speedFor(options), ANIM_RELEASE_MIN_SEC),
      onSettled: options?.onSettled,
    };
    this.#onWake?.();
  }

  /// Advances the in-progress turn by `dtSec`, applying the eased angle
  /// to the scene every frame; on completion, commits a real move
  /// (puzzle.applyMove() + scene.sync(), which also returns the pose to
  /// rest) or just clears a settle -- ported from rubik's anim_update().
  update(dtSec: number): void {
    if (!this.#current) return;
    const anim = this.#current;
    anim.elapsedSec += dtSec;
    const frac = anim.elapsedSec / anim.durationSec;
    if (frac >= 1) {
      this.#current = null;
      if (anim.move) {
        // PuzzleHandle.applyMove() is async (RemotePuzzle round-trips
        // through a Worker, docs/en/04-roadmap.md Phase 0 step 4) -- the
        // committed move only lands in stateView() once this resolves,
        // so sync() has to wait for it rather than running right after
        // the call, or it would re-read the pre-move state. This opens
        // a brief gap (one Worker round-trip -- sub-millisecond in
        // practice) where isIdle() is already true but the scene hasn't
        // resynced yet; nothing reads scene state in that gap except
        // the next frame's pose, which just holds at the just-finished
        // eased angle until sync() lands moments later. `onSettled`
        // (SolvePlayback's hook) deliberately fires only once sync()
        // has actually run, since that's the point at which the next
        // queued move is safe to start.
        //
        // The rejection branch matters just as much as the success one:
        // SolvePlayback's own #busy flag is cleared ONLY by onSettled
        // firing (interaction/solve-playback.ts), so a rejected
        // applyMove() with no handler here would leave a queued solve
        // permanently stuck mid-playback -- no error shown anywhere, no
        // further moves ever played, indistinguishable from "it just
        // stopped". Logging and still calling onSettled on failure means
        // a transient Worker error surfaces (console) and unblocks
        // whatever was waiting, instead of hanging forever silently.
        void this.#puzzle.applyMove(anim.move.generator, anim.move.amount).then(
          () => {
            this.#scene.sync();
            anim.onSettled?.();
          },
          (error: unknown) => {
            console.error(
              "TurnAnimation: applyMove() failed after a committed turn -- the rendered cube may now be out of sync with the puzzle's real state",
              error,
            );
            anim.onSettled?.();
          },
        );
        return;
      }
      // A settle never changed the puzzle's state, but it DID leave the
      // pose mid-rotation, and sync() is what returns everything to
      // rest (docs/en/03-architecture.md S6: always a full resync,
      // never incremental) -- no async hand-off needed since nothing
      // was committed.
      this.#scene.sync();
      anim.onSettled?.();
      return;
    }
    const eased = easeOutCubic(frac);
    const angleDeg = anim.fromDeg + (anim.toDeg - anim.fromDeg) * eased;
    this.#scene.setActiveTurn({ ...anim.target, angleDeg } satisfies ActiveTurn);
  }
}
