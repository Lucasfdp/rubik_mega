import type { Move } from "../engine/types.js";

/// The moves the user has made by hand since the puzzle was last
/// scrambled, for undo and redo (docs/en/13-phase6-polish.md D9). It
/// records and hands moves back; it never touches the puzzle, so it is
/// plain data and tests without a DOM or a worker (manual-play.ts plays
/// what it returns).
///
/// Two stacks, the usual shape: `done` is what undo takes from, `undone`
/// what redo takes from, and a new move empties `undone` -- once you turn
/// something else, the moves you took back are no longer reachable.
export class MoveHistory {
  #done: Move[] = [];
  #undone: Move[] = [];
  #turns = 0;

  /// A hand-made move that has just been committed.
  record(move: Move): void {
    this.#done.push({ generator: move.generator, amount: move.amount });
    this.#undone.length = 0;
    this.#turns += 1;
  }

  get canUndo(): boolean {
    return this.#done.length > 0;
  }

  get canRedo(): boolean {
    return this.#undone.length > 0;
  }

  /// How many turns have been made since the last clear(), undo and redo
  /// included: what "Solved in N moves" counts. A turn you took back was
  /// still a turn you made.
  get turns(): number {
    return this.#turns;
  }

  /// Takes the latest move off the history and returns it, for the caller
  /// to play the inverse of. Null if there is nothing to undo.
  undo(): Move | null {
    const move = this.#done.pop();
    if (!move) return null;
    this.#undone.push(move);
    this.#turns += 1;
    return move;
  }

  /// Puts back the move undo() took and returns it, for the caller to play
  /// again. Null if there is nothing to redo.
  redo(): Move | null {
    const move = this.#undone.pop();
    if (!move) return null;
    this.#done.push(move);
    this.#turns += 1;
    return move;
  }

  /// Forgets everything: the puzzle changed under the history (scramble,
  /// a solution, a different puzzle), so none of it applies any more.
  clear(): void {
    this.#done.length = 0;
    this.#undone.length = 0;
    this.#turns = 0;
  }
}
