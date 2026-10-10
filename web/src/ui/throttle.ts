/// Limits how often a changing message is spoken or shown
/// (docs/en/13-phase6-polish.md D11): a playback at 16x changes the
/// current move many times a second, and a screen reader told about every
/// one falls behind and never catches up. Pure: the caller supplies the
/// clock and does the timer, so the rule is tested without waiting.
///
/// The first message passes at once; later ones inside `minGapMs` are held
/// and only the LATEST is kept, to be released when the gap is over, so
/// the listener always ends on what is true now.
export class Throttle {
  readonly #minGapMs: number;
  #lastMs = -Infinity;
  #pending: string | null = null;

  constructor(minGapMs: number) {
    this.#minGapMs = minGapMs;
  }

  /// A new message at `nowMs`. Returns it if it may be shown now, null if
  /// it was held back.
  offer(text: string, nowMs: number): string | null {
    if (nowMs - this.#lastMs >= this.#minGapMs) {
      this.#lastMs = nowMs;
      this.#pending = null;
      return text;
    }
    this.#pending = text;
    return null;
  }

  /// Milliseconds until a held message may be shown, or null if none is.
  waitMs(nowMs: number): number | null {
    return this.#pending === null ? null : Math.max(this.#minGapMs - (nowMs - this.#lastMs), 0);
  }

  /// The held message, now that its gap is over (null if none, or if it is
  /// not over yet).
  release(nowMs: number): string | null {
    if (this.#pending === null || nowMs - this.#lastMs < this.#minGapMs) return null;
    const text = this.#pending;
    this.#pending = null;
    this.#lastMs = nowMs;
    return text;
  }

  /// Forgets a held message (the thing it described is gone).
  cancel(): void {
    this.#pending = null;
  }
}
