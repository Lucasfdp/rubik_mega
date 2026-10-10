import { el } from "./shell/dom.js";
import { Throttle } from "./throttle.js";

/// The page's polite live region (docs/en/13-phase6-polish.md D11): what a
/// screen reader hears as the app changes, with no visual of its own.
/// Playback moves go through `playback()`, throttled to one a second so a
/// fast solve does not flood the reader; one-off news (a scramble finished,
/// the puzzle was solved) goes through `say()` and is never held back.
export class Announcer {
  readonly element: HTMLElement;
  readonly #playback = new Throttle(1000);
  #timer: number | undefined;

  constructor() {
    this.element = el("div", { class: "visually-hidden", attrs: { role: "status", "aria-live": "polite", "aria-atomic": "true" } });
  }

  /// Speaks `text` now.
  say(text: string): void {
    this.#write(text);
  }

  /// Offers the current playback message; spoken at most once a second,
  /// with the latest one spoken when the gap ends.
  playback(text: string): void {
    const now = performance.now();
    const spoken = this.#playback.offer(text, now);
    if (spoken !== null) {
      this.#write(spoken);
      return;
    }
    if (this.#timer !== undefined) return;
    this.#timer = window.setTimeout(() => {
      this.#timer = undefined;
      const held = this.#playback.release(performance.now());
      if (held !== null) this.#write(held);
    }, this.#playback.waitMs(now) ?? 0);
  }

  /// Forgets a held playback message (the solution was closed).
  stopPlayback(): void {
    this.#playback.cancel();
    if (this.#timer !== undefined) window.clearTimeout(this.#timer);
    this.#timer = undefined;
  }

  #write(text: string): void {
    // Clearing first makes a repeated identical message be announced again.
    this.element.textContent = "";
    this.element.textContent = text;
  }
}
