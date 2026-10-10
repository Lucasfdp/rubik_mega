import { FrameStats } from "./frame-stats.js";

/// Quality tiers and the adaptive pixel ratio
/// (docs/en/13-phase6-polish.md D2, D5). Pure: the tier table and the
/// two decisions (which tier to start on, when to drop resolution) are
/// numbers in, numbers out, so they are unit tested and live apart from
/// the WebGL code that applies them.

export type QualityTier = "low" | "standard" | "high";

/// What a tier switches on. Every visual switch lives here, never in a
/// scene file (D4: "the quality tier's switches" belong to the look kit).
export interface TierSettings {
  /// Ceiling on renderer.setPixelRatio.
  maxPixelRatio: number;
  /// Image-based lighting from a pre-filtered room environment.
  environment: boolean;
  /// Contact shadow under the puzzle.
  contactShadow: boolean;
  /// Cubic bodies as rounded boxes rather than sharp ones.
  roundedBodies: boolean;
  /// Corner subdivisions of a rounded cubic body (more is rounder and
  /// costs triangles; ignored unless roundedBodies).
  bodySegments: number;
  /// Corner subdivisions of a rounded sticker outline.
  stickerCornerSegments: number;
}

export const TIER_SETTINGS: Readonly<Record<QualityTier, TierSettings>> = {
  low: { maxPixelRatio: 1, environment: false, contactShadow: false, roundedBodies: false, bodySegments: 1, stickerCornerSegments: 1 },
  standard: { maxPixelRatio: 2, environment: true, contactShadow: true, roundedBodies: true, bodySegments: 2, stickerCornerSegments: 3 },
  // Reserved for whatever passes the budget on a measured device
  // (spec D5: shadow maps or post-processing only if increment 1's
  // baseline shows headroom). Until then it is `standard` with rounder
  // corners, so choosing it is safe and visibly different.
  high: { maxPixelRatio: 2, environment: true, contactShadow: true, roundedBodies: true, bodySegments: 4, stickerCornerSegments: 5 },
};

/// What the user can pick in settings: a tier, or "auto" (the default
/// pickDefaultTier chooses).
export type QualityChoice = QualityTier | "auto";
export const QUALITY_CHOICES: readonly QualityChoice[] = ["auto", "low", "standard", "high"];

/// What pickDefaultTier looks at.
export interface DeviceHints {
  /// navigator.hardwareConcurrency, undefined where the browser hides it.
  hardwareConcurrency: number | undefined;
  /// The shorter side of the screen, in CSS pixels.
  shortSidePx: number;
}

/// At or below this many logical cores a device starts on `low`.
const LOW_CORE_LIMIT = 2;
/// At or above this many cores, on a big screen, `high` is the start.
const HIGH_CORE_MINIMUM = 8;
/// "A big screen" for the `high` start: a laptop or desktop, not a phone.
const HIGH_SCREEN_MINIMUM_PX = 700;

/// The tier a device starts on (spec D5: "from hardwareConcurrency and
/// screen size"). Deliberately cautious: a browser that hides its core
/// count gets `standard`, not `low`, because the adaptive pixel ratio
/// catches a slow device anyway, whereas starting low would never be
/// corrected upwards.
export function pickDefaultTier(hints: DeviceHints): QualityTier {
  const cores = hints.hardwareConcurrency;
  if (cores !== undefined && cores <= LOW_CORE_LIMIT) return "low";
  if (cores !== undefined && cores >= HIGH_CORE_MINIMUM && hints.shortSidePx >= HIGH_SCREEN_MINIMUM_PX) return "high";
  return "standard";
}

/// The tier a choice resolves to on this device.
export function resolveTier(choice: QualityChoice, hints: DeviceHints): QualityTier {
  return choice === "auto" ? pickDefaultTier(hints) : choice;
}

/// The pixel ratio to render at: the device's own, capped by the tier,
/// and never below 1.
export function pixelRatioFor(devicePixelRatio: number, tier: QualityTier, adaptiveCap: number): number {
  return Math.max(1, Math.min(devicePixelRatio, TIER_SETTINGS[tier].maxPixelRatio, adaptiveCap));
}

/// Frame-time budget in milliseconds: 60 fps (spec D2).
export const FRAME_BUDGET_MS = 1000 / 60;
/// The adaptive rule fires when the 95th percentile exceeds the budget
/// by this factor, not the budget itself. A 60 Hz screen delivers frames
/// 16.7 ms apart even when the work takes 2 ms, and the interval jitters
/// by a fraction of a millisecond, so comparing against the bare budget
/// would punish a healthy device.
export const ADAPTIVE_SLACK = 1.25;
/// How long the 95th percentile must stay over before resolution drops
/// (spec D2: "stays over budget for 2 seconds").
export const ADAPTIVE_SUSTAIN_MS = 2000;
/// Fewest samples before the percentile means anything.
const ADAPTIVE_MIN_SAMPLES = 30;
/// The resolution ladder (spec D2: 2, then 1.5, then 1).
export const PIXEL_RATIO_STEPS: readonly number[] = [2, 1.5, 1];

/// Drops render resolution one step when frames stay too slow, and
/// never raises it again in the same session (spec D2: "avoids
/// oscillating"). Feed it the interval of every frame that follows
/// another; it answers with the new cap when a step is due.
export class AdaptivePixelRatio {
  #cap: number;
  #stats = new FrameStats();
  #overSinceMs: number | null = null;
  #thresholdMs: number;
  #sustainMs: number;

  constructor(startCap: number, thresholdMs: number = FRAME_BUDGET_MS * ADAPTIVE_SLACK, sustainMs: number = ADAPTIVE_SUSTAIN_MS) {
    this.#cap = startCap;
    this.#thresholdMs = thresholdMs;
    this.#sustainMs = sustainMs;
  }

  /// The current ceiling on the pixel ratio.
  get cap(): number {
    return this.#cap;
  }

  /// Records one frame interval at time `nowMs`. Returns the new cap
  /// when this observation completes a sustained slowdown, else null.
  observe(frameMs: number, nowMs: number): number | null {
    this.#stats.record(frameMs);
    const p95 = this.#stats.count >= ADAPTIVE_MIN_SAMPLES ? this.#stats.percentile(95) : null;
    if (p95 === null || p95 <= this.#thresholdMs) {
      this.#overSinceMs = null;
      return null;
    }
    this.#overSinceMs ??= nowMs;
    if (nowMs - this.#overSinceMs < this.#sustainMs) return null;
    const lower = PIXEL_RATIO_STEPS.find((step) => step < this.#cap);
    // Start afresh at the new resolution either way: stale samples from
    // the old one must not trigger a second drop immediately.
    this.#stats.clear();
    this.#overSinceMs = null;
    if (lower === undefined) return null;
    this.#cap = lower;
    return lower;
  }
}
