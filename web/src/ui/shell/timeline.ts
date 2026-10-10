import type { SolveResult } from "../../engine/types.js";
import { cursorForFraction, phaseSegments, type PhaseSegment } from "../../interaction/phase-segments.js";
import { el } from "./dom.js";

/// The solve timeline (docs/en/13-phase6-polish.md D8): the solution as a
/// bar cut into one coloured segment per phase, wide in proportion to its
/// moves, filling as the solve plays. Press or drag anywhere on it to
/// jump there; it is also a keyboard slider (arrows step one move, Page
/// Up/Down jump a phase, Home/End the ends), so seeking never needs a
/// pointer.
export interface TimelineOptions {
  /// The user asked to be at this cursor position (0..moves).
  onSeek(cursor: number): void;
  /// A phase's name, for the hint and the slider's spoken value.
  phaseLabel(phase: number): string;
}

export interface Timeline {
  element: HTMLElement;
  /// Redraws for the loaded solution (null: none) and its cursor.
  render(result: SolveResult | null, cursor: number): void;
}

/// Segments are tinted by position in the solve, so adjacent phases always
/// differ; a hue per phase index would repeat for a long solution.
const PHASE_HUES = [210, 150, 40, 330, 270, 10, 180, 90];

export function createTimeline(options: TimelineOptions): Timeline {
  const track = el("div", { class: "timeline__track" });
  const hint = el("div", { class: "timeline__hint", attrs: { "aria-hidden": "true" } });
  const element = el("div", {
    class: "timeline",
    attrs: { role: "slider", tabindex: "0", "aria-label": "Solution progress", "aria-orientation": "horizontal" },
  });
  element.append(track, hint);

  let result: SolveResult | null = null;
  let segments: PhaseSegment[] = [];
  let fills: HTMLElement[] = [];
  let cursor = 0;
  let pressed = false;

  const moveCount = (): number => result?.moves.length ?? 0;

  function build(next: SolveResult): void {
    segments = phaseSegments(next.phaseTags);
    fills = segments.map((segment, index) => {
      const fill = el("div", { class: "timeline__fill" });
      const piece = el("div", { class: "timeline__segment" }, fill);
      piece.style.flexGrow = String(segment.end - segment.start);
      piece.style.setProperty("--hue", String(PHASE_HUES[index % PHASE_HUES.length]));
      return fill;
    });
    track.replaceChildren(...fills.map((fill) => fill.parentElement!));
  }

  function paint(): void {
    segments.forEach((segment, i) => {
      const length = segment.end - segment.start;
      const done = Math.min(Math.max(cursor - segment.start, 0), length);
      fills[i]!.style.width = `${(done / length) * 100}%`;
    });
    const total = moveCount();
    const phase = segments.find((s) => cursor >= s.start && cursor < s.end) ?? segments[segments.length - 1];
    element.setAttribute("aria-valuemin", "0");
    element.setAttribute("aria-valuemax", String(total));
    element.setAttribute("aria-valuenow", String(cursor));
    element.setAttribute(
      "aria-valuetext",
      phase ? `${cursor} of ${total} moves, ${options.phaseLabel(phase.phase)}` : "No moves",
    );
  }

  const fractionAt = (event: PointerEvent): number => {
    const box = track.getBoundingClientRect();
    return box.width > 0 ? (event.clientX - box.left) / box.width : 0;
  };

  function showHint(event: PointerEvent): void {
    const total = moveCount();
    const at = cursorForFraction(fractionAt(event), total);
    const segment = segments.find((s) => at >= s.start && at < s.end) ?? segments[segments.length - 1];
    hint.textContent = segment ? `${options.phaseLabel(segment.phase)} · move ${at} of ${total}` : "";
    hint.dataset.shown = "true";
  }

  element.addEventListener("pointerdown", (event) => {
    if (!result || event.button !== 0) return;
    pressed = true;
    element.setPointerCapture(event.pointerId);
    showHint(event);
    options.onSeek(cursorForFraction(fractionAt(event), moveCount()));
  });
  element.addEventListener("pointermove", (event) => {
    if (!result) return;
    if (pressed) options.onSeek(cursorForFraction(fractionAt(event), moveCount()));
    showHint(event);
  });
  const release = (): void => {
    pressed = false;
  };
  element.addEventListener("pointerup", release);
  element.addEventListener("pointercancel", release);
  element.addEventListener("pointerleave", () => {
    if (!pressed) delete hint.dataset.shown;
  });

  element.addEventListener("keydown", (event) => {
    if (!result) return;
    const total = moveCount();
    let target: number | null = null;
    if (event.key === "ArrowRight" || event.key === "ArrowUp") target = cursor + 1;
    else if (event.key === "ArrowLeft" || event.key === "ArrowDown") target = cursor - 1;
    else if (event.key === "Home") target = 0;
    else if (event.key === "End") target = total;
    else if (event.key === "PageDown") target = segments.find((s) => s.start > cursor)?.start ?? total;
    else if (event.key === "PageUp") {
      const current = segments.find((s) => cursor > s.start && cursor <= s.end);
      target = current ? current.start : 0;
    }
    if (target === null) return;
    event.preventDefault();
    event.stopPropagation(); // the global keymap also has arrows
    options.onSeek(Math.min(Math.max(target, 0), total));
  });

  return {
    element,
    render(next, nextCursor) {
      if (next !== result) {
        result = next;
        if (next) build(next);
        else {
          segments = [];
          fills = [];
          track.replaceChildren();
          delete hint.dataset.shown;
        }
      }
      cursor = nextCursor;
      paint();
    },
  };
}
