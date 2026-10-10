import type { Viewport } from "../../interaction/framing.js";

/// Which arrangement the interface takes (docs/en/13-phase6-polish.md
/// D7). Decided here in one pure function, and written to a
/// `data-layout` attribute that the CSS keys on, so JavaScript and CSS
/// can never disagree about it -- there are no duplicated media queries.
///
///  - compact: puzzle fills the screen, controls live in a bottom sheet.
///    Phones in portrait, and portrait tablets (a side panel there would
///    squeeze the puzzle into a narrow strip).
///  - regular: puzzle on the left, controls in a side panel on the right.
///    Desktops, landscape tablets and landscape phones.
export type LayoutMode = "compact" | "regular";

/// Narrower than this is always compact.
export const COMPACT_MAX_WIDTH_PX = 600;
/// Up to this wide, a portrait screen (taller than wide) is compact too.
export const PORTRAIT_COMPACT_MAX_WIDTH_PX = 1024;

export function layoutModeFor(viewport: Viewport): LayoutMode {
  if (viewport.width < COMPACT_MAX_WIDTH_PX) return "compact";
  if (viewport.width <= PORTRAIT_COMPACT_MAX_WIDTH_PX && viewport.height > viewport.width) return "compact";
  return "regular";
}
