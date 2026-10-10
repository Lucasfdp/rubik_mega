import { CUBE_PALETTE, PALETTES } from "../../render/look.js";
import { PuzzleType } from "../../engine/types.js";

/// Inline SVG for every control (docs/en/13-phase6-polish.md D7). The
/// Unicode glyphs they replace (the play, pause and step symbols) render
/// differently on every OS and font; these look the same everywhere and
/// take the surrounding text colour, so a theme change recolours them.
///
/// Built with createElementNS, never from a markup string.

const SVG_NS = "http://www.w3.org/2000/svg";

function svg(viewBox: string, className: string): SVGSVGElement {
  const element = document.createElementNS(SVG_NS, "svg");
  element.setAttribute("viewBox", viewBox);
  element.setAttribute("class", className);
  // Decorative: the button around it carries the accessible name.
  element.setAttribute("aria-hidden", "true");
  element.setAttribute("focusable", "false");
  return element;
}

function shape(tag: "path" | "polygon" | "circle" | "line" | "rect", attrs: Record<string, string | number>): SVGElement {
  const element = document.createElementNS(SVG_NS, tag);
  for (const [name, value] of Object.entries(attrs)) element.setAttribute(name, String(value));
  return element;
}

interface IconSpec {
  /// Outlined paths (stroked with the text colour).
  stroke?: string[];
  /// Solid paths (filled with the text colour).
  fill?: string[];
  circles?: [number, number, number][];
}

const ICONS = {
  play: { fill: ["M8 5v14l11-7z"] },
  pause: { fill: ["M7 5h3.5v14H7z", "M13.5 5H17v14h-3.5z"] },
  stepBack: { fill: ["M6 5h2.5v14H6z", "M19 5v14L9.5 12z"] },
  stepForward: { fill: ["M15.5 5H18v14h-2.5z", "M5 5v14l9.5-7z"] },
  skipPhase: { fill: ["M4 5v14l8-7z", "M12 5v14l8-7z"] },
  scramble: {
    stroke: [
      "M3 17h1.6c1.3 0 2.5-.6 3.3-1.7l5-7C13.7 7.2 14.9 6.6 16.2 6.6H21",
      "M18 3.6l3 3-3 3",
      "M3 7h1.6c1.3 0 2.5.6 3.3 1.7",
      "M21 17h-4.8c-1.3 0-2.5-.6-3.3-1.7",
      "M18 14l3 3-3 3",
    ],
  },
  solve: { stroke: ["M12 3l1.9 5.1L19 10l-5.1 1.9L12 17l-1.9-5.1L5 10l5.1-1.9z", "M19 16.5l.7 1.8 1.8.7-1.8.7-.7 1.8-.7-1.8-1.8-.7 1.8-.7z"] },
  settings: {
    stroke: ["M4 6h8", "M18 6h2", "M4 12h2", "M12 12h8", "M4 18h10", "M20 18h0"],
    circles: [[15, 6, 2.2], [9, 12, 2.2], [17, 18, 2.2]],
  },
  chevronUp: { stroke: ["M6 15l6-6 6 6"] },
  chevronDown: { stroke: ["M6 9l6 6 6-6"] },
  close: { stroke: ["M6 6l12 12", "M18 6L6 18"] },
  panel: { stroke: ["M4 5h16v14H4z", "M15 5v14"] },
  check: { stroke: ["M5 12.5l4.5 4.5L19 7.5"] },
  undo: { stroke: ["M9 14L4 9l5-5", "M4 9h10.5a5.5 5.5 0 010 11H11"] },
  redo: { stroke: ["M15 14l5-5-5-5", "M20 9H9.5a5.5 5.5 0 000 11H13"] },
  copy: { stroke: ["M9 9h10v11H9z", "M5 15V4h10"] },
  share: {
    stroke: ["M8.2 10.9l7.6-4.3", "M8.2 13.1l7.6 4.3"],
    circles: [[6, 12, 2.4], [18, 5.4, 2.4], [18, 18.6, 2.4]],
  },
} satisfies Record<string, IconSpec>;

export type IconName = keyof typeof ICONS;

/// One 24x24 icon. Size it in CSS (width/height in rem), so it scales
/// with the interface size setting.
export function icon(name: IconName, className = "icon"): SVGSVGElement {
  const spec: IconSpec = ICONS[name];
  const root = svg("0 0 24 24", className);
  for (const d of spec.fill ?? []) root.append(shape("path", { d, fill: "currentColor" }));
  const outline = { fill: "none", stroke: "currentColor", "stroke-width": 2, "stroke-linecap": "round", "stroke-linejoin": "round" };
  for (const d of spec.stroke ?? []) root.append(shape("path", { d, ...outline }));
  for (const [cx, cy, r] of spec.circles ?? []) root.append(shape("circle", { cx, cy, r, ...outline }));
  return root;
}

/// Which small picture a puzzle gets in the picker.
export type GlyphKind = "cube" | "skewb" | "pyraminx" | "megaminx" | "kilominx" | "square1";

type P = [number, number];
const INK = "#111111";

function polygon(points: readonly P[], fill: string): SVGElement {
  return shape("polygon", { points: points.map((p) => p.join(",")).join(" "), fill, stroke: INK, "stroke-width": 1.4, "stroke-linejoin": "round" });
}
function line(a: P, b: P): SVGElement {
  return shape("line", { x1: a[0], y1: a[1], x2: b[0], y2: b[1], stroke: INK, "stroke-width": 1.1, "stroke-linecap": "round" });
}
const lerp = (a: P, b: P, t: number): P => [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t];

/// A parallelogram face with a grid of `divisions` x `divisions` cells.
function gridFace(root: SVGElement, origin: P, edgeA: P, edgeB: P, fill: string, divisions: number): void {
  const at = (s: number, t: number): P => [origin[0] + edgeA[0] * s + edgeB[0] * t, origin[1] + edgeA[1] * s + edgeB[1] * t];
  root.append(polygon([at(0, 0), at(1, 0), at(1, 1), at(0, 1)], fill));
  for (let i = 1; i < divisions; i++) {
    root.append(line(at(i / divisions, 0), at(i / divisions, 1)));
    root.append(line(at(0, i / divisions), at(1, i / divisions)));
  }
}

function regularPolygon(sides: number, radius: number, centre: P, startDeg = -90): P[] {
  return Array.from({ length: sides }, (_, i): P => {
    const a = ((startDeg + (360 * i) / sides) * Math.PI) / 180;
    return [centre[0] + radius * Math.cos(a), centre[1] + radius * Math.sin(a)];
  });
}

/// The picture for a puzzle in the picker, drawn in the puzzle's own
/// colours so the picker previews what it will open.
export function puzzleGlyph(kind: GlyphKind, className = "glyph"): SVGSVGElement {
  const root = svg("0 0 48 48", className);
  const cube = CUBE_PALETTE;
  switch (kind) {
    case "cube":
    case "skewb": {
      const divisions = kind === "cube" ? 3 : 1;
      gridFace(root, [24, 24], [-18, -9], [18, -9], cube[0]!, divisions); // top
      gridFace(root, [6, 15], [18, 9], [0, 19], cube[2]!, divisions); // front-left
      gridFace(root, [24, 24], [18, -9], [0, 19], cube[1]!, divisions); // right
      if (kind === "skewb") {
        // Skewb's cuts run corner to corner across each face.
        root.append(line([6, 15], [24, 24]), line([24, 24], [42, 15]), line([24, 43], [24, 24]));
        root.append(line([24, 6], [24, 24]));
      }
      break;
    }
    case "pyraminx": {
      const apex: P = [24, 5];
      const left: P = [4, 41];
      const right: P = [44, 41];
      const palette = PALETTES[PuzzleType.Tetrahedral]!;
      root.append(polygon([apex, left, right], palette[3]!));
      for (const t of [1 / 3, 2 / 3]) {
        root.append(line(lerp(apex, left, t), lerp(apex, right, t)));
        root.append(line(lerp(left, apex, t), lerp(left, right, t)));
        root.append(line(lerp(right, apex, t), lerp(right, left, t)));
      }
      root.append(polygon([apex, lerp(apex, left, 1 / 3), lerp(apex, right, 1 / 3)], palette[0]!));
      break;
    }
    case "megaminx":
    case "kilominx": {
      const palette = PALETTES[PuzzleType.Dodecahedral]!;
      const outer = regularPolygon(5, 21, [24, 25]);
      root.append(polygon(outer, palette[kind === "megaminx" ? 0 : 1]!));
      if (kind === "megaminx") {
        const inner = regularPolygon(5, 9, [24, 25]);
        root.append(polygon(inner, palette[1]!));
        inner.forEach((p, i) => root.append(line(p, outer[i]!)));
      } else {
        // Five kites meeting at the centre.
        outer.forEach((p) => root.append(line([24, 25], p)));
        outer.forEach((p, i) => root.append(line(lerp(p, outer[(i + 1) % 5]!, 0.5), [24, 25])));
      }
      break;
    }
    case "square1": {
      root.append(polygon([[6, 6], [42, 6], [42, 42], [6, 42]], cube[0]!));
      // The two halves the slice separates, and the corner/edge wedges.
      root.append(line([24, 6], [24, 42]));
      root.append(line([6, 6], [24, 24]), line([42, 6], [24, 24]), line([6, 42], [24, 24]), line([42, 42], [24, 24]));
      root.append(shape("circle", { cx: 24, cy: 24, r: 5, fill: cube[2]!, stroke: INK, "stroke-width": 1.2 }));
      break;
    }
  }
  return root;
}
