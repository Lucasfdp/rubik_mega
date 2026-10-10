import type { WebGLRenderer } from "three";
import type { FrameStats } from "../render/frame-stats.js";
import type { FrameScheduler } from "../render/frame-scheduler.js";

/// The `?debug` overlay (docs/en/13-phase6-polish.md D2): numbers read
/// on the phone itself, no cable. Loaded with a dynamic import only when
/// the flag is present, so it costs nothing otherwise.

/// What the overlay reads. Functions, not values, for the ones that
/// change (the adaptive rule can lower the pixel ratio mid-session).
export interface DebugSources {
  renderer: WebGLRenderer;
  stats: FrameStats;
  scheduler: FrameScheduler;
  tier(): string;
  pixelRatio(): number;
  /// Milliseconds the room environment took to pre-filter, or null.
  environmentMs(): number | null;
}

/// How often the text refreshes. Half a second keeps it readable and the
/// DOM work negligible; it never touches the render loop.
const REFRESH_MS = 500;

function ms(value: number | null): string {
  return value === null ? "-" : value.toFixed(1);
}

/// Adds the overlay to the page and returns a function that removes it.
export function mountDebugOverlay(sources: DebugSources): () => void {
  const element = document.createElement("pre");
  element.className = "debug-overlay";
  element.setAttribute("aria-hidden", "true");
  document.body.appendChild(element);

  let previousFrames = sources.scheduler.framesRendered;
  let previousMs = performance.now();

  function refresh(): void {
    const nowMs = performance.now();
    const frames = sources.scheduler.framesRendered;
    const perSecond = ((frames - previousFrames) * 1000) / Math.max(nowMs - previousMs, 1);
    previousFrames = frames;
    previousMs = nowMs;
    const { calls, triangles } = sources.renderer.info.render;
    element.textContent = [
      perSecond === 0 ? "idle (0 fps)" : `${perSecond.toFixed(0)} fps`,
      `frame p95 ${ms(sources.stats.percentile(95))} ms  mean ${ms(sources.stats.mean())} ms`,
      `draw calls ${calls}  triangles ${triangles}`,
      `pixel ratio ${sources.pixelRatio()} (device ${window.devicePixelRatio})  tier ${sources.tier()}`,
      `frames rendered ${frames}  environment ${ms(sources.environmentMs())} ms`,
    ].join("\n");
  }

  refresh();
  const timer = window.setInterval(refresh, REFRESH_MS);
  return () => {
    window.clearInterval(timer);
    element.remove();
  };
}
