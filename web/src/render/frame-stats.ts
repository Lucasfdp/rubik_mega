/// Rolling frame-time statistics for the debug overlay and the adaptive
/// pixel ratio (docs/en/13-phase6-polish.md D2). Pure: no clock, no DOM,
/// so the percentile maths is unit tested.

/// Frames the window remembers -- two seconds at 60 fps, long enough to
/// smooth a stutter and short enough to notice a real slowdown.
export const FRAME_STATS_WINDOW = 120;

export class FrameStats {
  #capacity: number;
  #samples: number[] = [];
  #next = 0;

  constructor(capacity: number = FRAME_STATS_WINDOW) {
    if (!Number.isInteger(capacity) || capacity < 1) throw new Error(`FrameStats: capacity must be a positive integer (got ${capacity})`);
    this.#capacity = capacity;
  }

  /// Adds one frame time in milliseconds, dropping the oldest once full.
  record(ms: number): void {
    if (this.#samples.length < this.#capacity) {
      this.#samples.push(ms);
    } else {
      this.#samples[this.#next] = ms;
    }
    this.#next = (this.#next + 1) % this.#capacity;
  }

  get count(): number {
    return this.#samples.length;
  }

  clear(): void {
    this.#samples = [];
    this.#next = 0;
  }

  /// The `p`th percentile (0-100, nearest-rank) of the window, or null
  /// with no samples. Nearest-rank, not interpolated: "95% of frames
  /// took at most this long" is the claim the budget makes.
  percentile(p: number): number | null {
    if (this.#samples.length === 0) return null;
    const sorted = [...this.#samples].sort((a, b) => a - b);
    const rank = Math.ceil((Math.min(Math.max(p, 0), 100) / 100) * sorted.length);
    return sorted[Math.max(rank - 1, 0)]!;
  }

  /// Mean of the window, or null with no samples.
  mean(): number | null {
    if (this.#samples.length === 0) return null;
    return this.#samples.reduce((sum, ms) => sum + ms, 0) / this.#samples.length;
  }
}
