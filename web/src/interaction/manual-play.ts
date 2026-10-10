import type { PuzzleHandle } from "../engine/puzzle-handle.js";
import type { Move } from "../engine/types.js";
import { MoveHistory } from "./move-history.js";
import type { TurnAnimation, TurnOptions } from "./turn-anim.js";

/// Everything the user does to the puzzle with their own hands, as one
/// object: a drag that commits a turn, a keyboard turn, undo and redo
/// (docs/en/13-phase6-polish.md D9). It sits beside SolvePlayback on the
/// one shared TurnAnimation, and owns three things neither of them should:
///
///  - the MoveHistory, so undo and redo work whichever way a move was made;
///  - whether a hand-made move is still settling, so undo cannot start
///    while the move it would take back has not landed in the puzzle;
///  - the "you solved it" moment: a puzzle that was scrambled, then turned
///    by hand until core says it looks solved. A solution an algorithm
///    played never reaches this class, so it is never celebrated.
export interface ManualPlayEvents {
  /// The history or the settling state changed (button states).
  onChange(): void;
  /// The user turned the puzzle by hand: a solution that was loaded no
  /// longer describes it.
  onManualTurn(): void;
  /// A scrambled puzzle was just turned to solved; `turns` is how many
  /// turns it took since the scramble.
  onSolved(turns: number): void;
}

export class ManualPlay {
  readonly #anim: TurnAnimation;
  readonly #puzzle: PuzzleHandle;
  readonly #events: ManualPlayEvents;
  readonly #history = new MoveHistory();
  #settling = false;
  #armed = false;

  constructor(anim: TurnAnimation, puzzle: PuzzleHandle, events: ManualPlayEvents) {
    this.#anim = anim;
    this.#puzzle = puzzle;
    this.#events = events;
  }

  get canUndo(): boolean {
    return this.#history.canUndo && !this.#busy();
  }

  get canRedo(): boolean {
    return this.#history.canRedo && !this.#busy();
  }

  /// Turns made since the last reset(), undo and redo included.
  get turns(): number {
    return this.#history.turns;
  }

  /// Whether a solved puzzle would be celebrated right now.
  get armed(): boolean {
    return this.#armed;
  }

  /// A hand-made move can start only on an idle animation slot with
  /// nothing from before still landing.
  #busy(): boolean {
    return this.#settling || !this.#anim.isIdle();
  }

  /// Forgets the history and stops watching for a solve: the puzzle was
  /// changed some other way (scramble, solution, new puzzle).
  reset(): void {
    this.#history.clear();
    this.#armed = false;
    this.#settling = false;
    this.#events.onChange();
  }

  /// Starts watching for the user to solve the puzzle. Call right after a
  /// scramble. A puzzle that is already solved (which a scramble should
  /// never leave) arms nothing: there would be nothing to solve.
  arm(): void {
    this.#armed = !this.#puzzle.isSolved();
  }

  /// For a drag that has just released onto a move: records it and returns
  /// the options the animation needs, so the move is marked as settling
  /// until it has landed. PointerController passes the result straight to
  /// TurnAnimation.beginFrom().
  commitOptions(move: Move): TurnOptions {
    this.#settling = true;
    this.#history.record(move);
    this.#events.onManualTurn();
    this.#events.onChange();
    return { onSettled: () => this.#settled() };
  }

  /// Plays `move` as a turn the user made (a keypress). False if it cannot
  /// start now: something is animating or landing, or the puzzle refuses
  /// it (a Square-1 slice with a corner across the cut).
  turn(move: Move): boolean {
    if (this.#busy() || !this.#puzzle.isMoveLegal(move.generator, move.amount)) return false;
    this.#events.onManualTurn();
    this.#play(move, () => this.#history.record(move));
    return true;
  }

  /// Turns generator `generator` one step clockwise (direction 1) or
  /// anticlockwise (-1): the arrow keys on a picked face or corner.
  turnBy(generator: number, direction: 1 | -1): boolean {
    const amount = direction > 0 ? 1 : this.#anim.moves.inverseAmount(1, generator);
    return this.turn({ generator, amount });
  }

  /// Takes back the latest hand-made move by playing its inverse.
  undo(): boolean {
    if (!this.canUndo) return false;
    const move = this.#history.undo()!;
    return this.#playInverse(move);
  }

  /// Plays the move undo() took back, again.
  redo(): boolean {
    if (!this.canRedo) return false;
    const move = this.#history.redo()!;
    this.#play(move, () => {});
    return true;
  }

  #playInverse(move: Move): boolean {
    const inverse: Move = { generator: move.generator, amount: this.#anim.moves.inverseAmount(move.amount, move.generator) };
    this.#events.onManualTurn();
    this.#play(inverse, () => {});
    return true;
  }

  /// Animates one move forwards (0 to its angle) and commits it, the same
  /// way a solve's step does, so the stickers travel with the pieces.
  #play(move: Move, record: () => void): void {
    this.#settling = true;
    record();
    this.#events.onChange();
    const toDeg = this.#anim.moves.quarterDegForAmount(move.generator, move.amount);
    this.#anim.beginFrom(move, 0, toDeg, { onSettled: () => this.#settled() });
  }

  #settled(): void {
    this.#settling = false;
    this.#events.onChange();
    if (this.#armed && this.#puzzle.isSolved()) {
      this.#armed = false;
      this.#events.onSolved(this.#history.turns);
    }
  }
}
