import type { Move, SolveResult } from "../engine/types.js";
import { TurnAnimation } from "./turn-anim.js";

/// Playback speed presets, in degrees per second -- the speed slider
/// (docs/en/06-phase2-solvers.md's web UI, Lucas's "auto-play + step/
/// pause controls" decision) picks one of these rather than an
/// arbitrary continuous value, so the UI can show a small fixed set of
/// named positions rather than a raw number with no units a user can
/// reason about. ANIM_SPEED_DEG_PER_SEC-equivalent (360) is "normal".
/// The top two exist for big-cube reductions (docs/en/08-nxn-reduction.md
/// D5): an 8x8x8 solve is ~1,700 moves, about 20 minutes at "normal".
export const PLAYBACK_SPEEDS_DEG_PER_SEC = [90, 180, 360, 720, 1440, 2880, 5760] as const;
export const DEFAULT_PLAYBACK_SPEED_DEG_PER_SEC = 360;

/// The preset after `current` in direction `delta` (+1 faster, -1 slower),
/// staying at the end of the list rather than wrapping. A value that is not
/// a preset steps to its nearest neighbour in that direction, so a stored
/// speed from an older list still moves sensibly.
export function steppedSpeed(current: number, delta: 1 | -1): number {
  const presets = PLAYBACK_SPEEDS_DEG_PER_SEC;
  if (delta > 0) return presets.find((speed) => speed > current) ?? presets[presets.length - 1]!;
  const slower = [...presets].reverse().find((speed) => speed < current);
  return slower ?? presets[0]!;
}

/// What SolvePlayback reports back after every state change (loaded,
/// played, paused, stepped, cleared) -- one snapshot shape so the UI
/// layer (ui/solve-panel.ts) has a single place to read "what do I show
/// right now" rather than piecing it together from several getters.
export interface PlaybackSnapshot {
  /// The full result currently loaded, or null if nothing has been
  /// solved yet (or playback was cleared).
  result: SolveResult | null;
  /// Index of the NEXT move to play (0..result.moves.length) -- also
  /// how many moves have been committed so far. Equal to
  /// result.moves.length means playback has reached the end.
  cursor: number;
  playing: boolean;
  speedDegPerSec: number;
  /// True while a step is actually animating (between beginFrom() and
  /// its onSettled) -- exposed so the UI (ui/solve-panel.ts) can
  /// visibly disable step/play controls mid-animation, rather than
  /// them silently no-op'ing on a click SolvePlayback's own #busy guard
  /// already ignores.
  busy: boolean;
}

/// Drives TurnAnimation through a pre-computed SolveResult's moves one
/// at a time, forward or backward, either under its own play() loop or
/// one stepForward()/stepBack() at a time -- the move-queue/autoplay
/// layer docs/en/06-phase2-solvers.md's web UI calls for (Lucas's
/// "auto-play + step/pause controls" decision), sitting beside
/// TurnAnimation rather than inside it (see turn-anim.ts's own updated
/// class doc comment for why).
///
/// Deliberately does NOT touch TurnAnimation.isIdle(): that flips
/// synchronously on the eased angle reaching its target, before the
/// move's applyMove() commit resolves (locked in by
/// web/tests/interaction/turn-anim.test.ts), which is exactly wrong for
/// "is it safe to start the next queued move" -- starting too early
/// would animate move N+1 against the pre-move-N puzzle state. Instead
/// this tracks its own #busy flag, set on beginFrom()/beginSettle() and
/// cleared only by the onSettled callback TurnAnimation now invokes
/// once the commit has actually landed.
export class SolvePlayback {
  #anim: TurnAnimation;
  #result: SolveResult | null = null;
  #cursor = 0;
  #playing = false;
  #speedDegPerSec: number = DEFAULT_PLAYBACK_SPEED_DEG_PER_SEC;
  #busy = false;
  /// Bumped by load()/clear() so an onSettled callback from a move
  /// started under a PREVIOUS load() can tell it's stale once it
  /// finally fires (TurnAnimation's single slot can't be cancelled
  /// mid-animation -- see #advanceOne()/stepBack()'s own comments).
  /// Without this, load() called while a move was still animating
  /// (e.g. a second Solve click landing before the first one's button
  /// got disabled) would let that stale callback increment #cursor
  /// against the NEW result, corrupting which move playback thought it
  /// was on -- this is the actual bug class ui/solve-panel.ts's own
  /// click handlers now also guard against at the button level, but
  /// this class shouldn't depend on every caller getting that right.
  #generation = 0;
  /// Set by skipPhase() when a move is mid-animation: the skip runs as
  /// soon as that move settles, instead of being dropped. During
  /// auto-play a move is almost always in flight, so without this the
  /// skip button would be all but unclickable exactly when it matters.
  #skipPending = false;
  /// Set by seek() when a move is mid-animation: the latest requested
  /// cursor, applied as soon as that move settles. Only the latest
  /// matters -- dragging the timeline asks for many positions a second,
  /// and the ones in between are never worth showing.
  #seekPending: number | null = null;
  #onChange: (snapshot: PlaybackSnapshot) => void;

  constructor(anim: TurnAnimation, onChange: (snapshot: PlaybackSnapshot) => void) {
    this.#anim = anim;
    this.#onChange = onChange;
  }

  #emit(): void {
    this.#onChange({
      result: this.#result,
      cursor: this.#cursor,
      playing: this.#playing,
      speedDegPerSec: this.#speedDegPerSec,
      busy: this.#busy,
    });
  }

  /// Loads a freshly-solved result at cursor 0, paused -- the caller
  /// (ui/solve-panel.ts, after awaiting puzzle.solve()) decides whether
  /// to immediately call play() or leave it for the user to press play/
  /// step themselves (Lucas's "auto-play + step/pause controls": both
  /// are available, neither is forced).
  load(result: SolveResult): void {
    this.#generation += 1;
    this.#skipPending = false;
    this.#seekPending = null;
    this.#result = result;
    this.#cursor = 0;
    this.#playing = false;
    this.#emit();
  }

  /// Drops the loaded result entirely (e.g. the user picks a different
  /// algorithm or re-scrambles) -- distinct from pause(): clear() means
  /// "there is nothing to play", pause() means "there is, just not
  /// right now".
  clear(): void {
    this.#generation += 1;
    this.#skipPending = false;
    this.#seekPending = null;
    this.#result = null;
    this.#cursor = 0;
    this.#playing = false;
    this.#emit();
  }

  setSpeed(speedDegPerSec: number): void {
    this.#speedDegPerSec = speedDegPerSec;
    this.#emit();
  }

  /// Starts auto-play from the current cursor -- update() does the
  /// actual stepping, once per settled move, as long as #playing stays
  /// true. A no-op if nothing is loaded or already at the end.
  play(): void {
    if (!this.#result || this.#cursor >= this.#result.moves.length) return;
    this.#playing = true;
    this.#emit();
    this.#tryAdvance();
  }

  pause(): void {
    if (!this.#playing) return;
    this.#playing = false;
    this.#emit();
  }

  /// Plays exactly one move forward and stops (pauses if it was
  /// playing) -- Lucas's step-forward control. A no-op at the end or
  /// while an animation is already in flight (the user mashing the
  /// button faster than moves settle must not desync the cursor from
  /// what TurnAnimation is actually animating).
  stepForward(): void {
    if (this.#busy || !this.#result || this.#cursor >= this.#result.moves.length) return;
    this.#playing = false;
    this.#advanceOne();
  }

  /// Plays the inverse of the PREVIOUS move and moves the cursor back
  /// one -- Lucas's step-back control. Undoing move N-1 is exactly
  /// applying its inverse (see MoveModel.inverseAmount()), so this is a
  /// forward turn of that inverse: 0 -> its quarter angle, then commit.
  /// (An earlier version animated from that angle back to 0, which
  /// showed the layer already turned with the old stickers, then
  /// snapped: the stickers flashed and did not travel with the piece.)
  stepBack(): void {
    if (this.#busy || !this.#result || this.#cursor === 0) return;
    this.#playing = false;
    const index = this.#cursor - 1;
    const move = this.#result.moves[index]!;
    const inverse = { generator: move.generator, amount: this.#anim.moves.inverseAmount(move.amount, move.generator) };
    const toDeg = this.#anim.moves.quarterDegForAmount(inverse.generator, inverse.amount);
    const gen = this.#generation;
    this.#busy = true;
    this.#emit();
    this.#anim.beginFrom(inverse, 0, toDeg, {
      speedDegPerSec: this.#speedDegPerSec,
      onSettled: () => {
        this.#busy = false;
        // A stale callback (load()/clear() ran while this was still
        // animating) still frees the shared TurnAnimation slot above,
        // but must not touch a cursor that now belongs to a different
        // loaded result.
        if (gen === this.#generation) this.#cursor = index;
        this.#emit();
        this.#continue(); // resumes auto-play if the user re-pressed play mid-rewind
      },
    });
  }

  #advanceOne(): void {
    if (!this.#result || this.#cursor >= this.#result.moves.length) return;
    const move = this.#result.moves[this.#cursor]!;
    const toDeg = this.#anim.moves.quarterDegForAmount(move.generator, move.amount);
    const gen = this.#generation;
    this.#busy = true;
    this.#emit();
    this.#anim.beginFrom(move, 0, toDeg, {
      speedDegPerSec: this.#speedDegPerSec,
      onSettled: () => {
        this.#busy = false;
        // Same staleness guard as stepBack() above.
        if (gen === this.#generation) this.#cursor += 1;
        this.#emit();
        this.#continue();
      },
    });
  }

  /// Applies every remaining move of the current phase at once, without
  /// animating, then carries on from the first move of the next phase
  /// (still playing if it was) -- docs/en/08-nxn-reduction.md D5's
  /// "skip to next phase". Same staleness guard as the animated paths:
  /// a load()/clear() while the jump is in flight leaves the new result
  /// untouched. Asked for mid-move, it runs once that move settles.
  skipPhase(): void {
    if (!this.#result || this.#cursor >= this.#result.moves.length) return;
    if (this.#busy) {
      this.#skipPending = true;
      return;
    }
    const { moves, phaseTags } = this.#result;
    const phase = phaseTags[this.#cursor];
    let end = this.#cursor;
    while (end < moves.length && phaseTags[end] === phase) end++;
    const gen = this.#generation;
    this.#busy = true;
    this.#emit();
    this.#anim.jump(moves.slice(this.#cursor, end)).then(
      () => {
        this.#busy = false;
        if (gen === this.#generation) this.#cursor = end;
        this.#emit();
        this.#tryAdvance();
      },
      (error: unknown) => {
        console.error("SolvePlayback: skipping a phase failed -- the cube may be out of sync with the move list", error);
        this.#busy = false;
        this.#playing = false;
        this.#emit();
      },
    );
  }

  /// Jumps the cursor to `index` (0 = nothing played, moves.length = all
  /// played) without animating, whichever direction that is: forwards
  /// applies the moves in between, backwards applies their inverses in
  /// reverse order -- the timeline's tap and drag (docs/en/13-phase6-polish.md
  /// D8). Keeps playing if it was playing. Asked for mid-move it runs once
  /// that move settles; the same staleness guard as every animated path
  /// keeps a load()/clear() in the meantime from being corrupted by it.
  seek(index: number): void {
    if (!this.#result) return;
    const target = Math.min(Math.max(Math.round(index), 0), this.#result.moves.length);
    if (this.#busy) {
      this.#seekPending = target;
      return;
    }
    // A hand-turn in flight owns the animation slot; jumping now would throw.
    if (target === this.#cursor || !this.#anim.isIdle()) return;
    const { moves } = this.#result;
    const batch: Move[] =
      target > this.#cursor
        ? moves.slice(this.#cursor, target)
        : moves
            .slice(target, this.#cursor)
            .reverse()
            .map((move) => ({ generator: move.generator, amount: this.#anim.moves.inverseAmount(move.amount, move.generator) }));
    const gen = this.#generation;
    this.#busy = true;
    this.#emit();
    this.#anim.jump(batch).then(
      () => {
        this.#busy = false;
        if (gen === this.#generation) this.#cursor = target;
        this.#emit();
        this.#continue();
      },
      (error: unknown) => {
        console.error("SolvePlayback: seeking failed -- the cube may be out of sync with the move list", error);
        this.#busy = false;
        this.#playing = false;
        this.#emit();
      },
    );
  }

  /// What to do once a move settles: a seek requested mid-move wins, then
  /// a skip, otherwise carry on auto-playing.
  #continue(): void {
    if (this.#seekPending !== null) {
      const target = this.#seekPending;
      this.#seekPending = null;
      this.seek(target);
      if (this.#busy) return;
    }
    if (this.#skipPending) {
      this.#skipPending = false;
      this.skipPhase();
      return;
    }
    this.#tryAdvance();
  }

  /// Continues auto-play if #playing is still true, there's a next
  /// move, and TurnAnimation is free -- called both right after play()
  /// and after every settled move, so play() naturally chains through
  /// the whole result without a separate setInterval/loop. Pausing
  /// mid-flight (pause() during an in-progress move) lets that one move
  /// finish settling -- stopping mid-rotation would leave the puzzle
  /// state and the rendered pose disagreeing -- it just isn't followed
  /// by another.
  #tryAdvance(): void {
    if (!this.#playing || this.#busy || !this.#result) return;
    if (this.#cursor >= this.#result.moves.length) {
      this.#playing = false;
      this.#emit();
      return;
    }
    this.#advanceOne();
  }
}
