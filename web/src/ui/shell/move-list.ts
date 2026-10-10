import type { PlaybackSnapshot } from "../../interaction/solve-playback.js";
import type { Move } from "../../engine/types.js";

/// The solution as a row of move chips with the current one highlighted.
///
/// The chips are built once per loaded result and then only re-classed as
/// the cursor moves: a big-cube reduction is up to ~1,800 moves, and
/// rebuilding that many nodes on every settled move stutters on a phone
/// (docs/en/08-nxn-reduction.md D5).
export class MoveList {
  #container: HTMLElement;
  #format: (move: Move) => string;
  #result: PlaybackSnapshot["result"] = null;
  #spans: HTMLSpanElement[] = [];
  #cursor = 0;

  constructor(container: HTMLElement, format: (move: Move) => string) {
    this.#container = container;
    this.#format = format;
  }

  render(snapshot: PlaybackSnapshot): void {
    const result = snapshot.result;
    if (result !== this.#result) {
      this.#result = result;
      this.#spans = [];
      const fragment = document.createDocumentFragment();
      (result?.moves ?? []).forEach((move, index) => {
        const span = document.createElement("span");
        span.textContent = this.#format(move);
        span.className = chipClass(index, snapshot.cursor);
        this.#spans.push(span);
        fragment.appendChild(span);
        fragment.appendChild(document.createTextNode(" "));
      });
      this.#container.replaceChildren(fragment);
    } else {
      const from = Math.min(this.#cursor, snapshot.cursor);
      const to = Math.min(Math.max(this.#cursor, snapshot.cursor), this.#spans.length - 1);
      for (let index = from; index <= to; index++) this.#spans[index]!.className = chipClass(index, snapshot.cursor);
    }
    this.#cursor = snapshot.cursor;
    // `block: "nearest"` only scrolls when the chip is out of view, so it
    // does not fight a user who scrolled up to look at an earlier move.
    this.#spans[snapshot.cursor]?.scrollIntoView({ block: "nearest" });
  }

  /// Scrolls the current chip into view -- for when the list is revealed.
  revealCurrent(): void {
    this.#spans[this.#cursor]?.scrollIntoView({ block: "nearest" });
  }
}

function chipClass(index: number, cursor: number): string {
  return "move" + (index < cursor ? " move--done" : "") + (index === cursor ? " move--current" : "");
}
