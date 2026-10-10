import { describe, expect, it } from "vitest";
import { PuzzleType } from "../../src/engine/types.js";
import { PALETTES } from "../../src/render/look.js";
import {
  DEFICIENCIES,
  PALETTE_CHOICES,
  deltaE,
  glyphsFor,
  paletteFor,
  parseHex,
  reviewPalette,
  simulate,
} from "../../src/render/palettes.js";

const TYPES = [PuzzleType.Cubic, PuzzleType.Skewb, PuzzleType.Tetrahedral, PuzzleType.Dodecahedral, PuzzleType.Square1];

describe("paletteFor", () => {
  it("gives every family the same number of colours in both palettes", () => {
    for (const type of TYPES) {
      const classic = paletteFor(type, "classic");
      expect(classic).toBe(PALETTES[type]);
      for (const choice of PALETTE_CHOICES) {
        expect(paletteFor(type, choice)).toHaveLength(classic.length);
        paletteFor(type, choice).forEach((hex) => expect(() => parseHex(hex)).not.toThrow());
      }
    }
  });

  it("never repeats a colour inside a palette", () => {
    for (const type of TYPES) for (const choice of PALETTE_CHOICES) {
      const palette = paletteFor(type, choice);
      expect(new Set(palette.map((c) => c.toLowerCase())).size).toBe(palette.length);
    }
  });
});

describe("colour-vision simulation", () => {
  it("leaves normal vision, white and black alone", () => {
    expect(simulate([10, 200, 30], "normal")).toEqual([10, 200, 30]);
    for (const d of DEFICIENCIES) {
      const white = simulate([255, 255, 255], d);
      white.forEach((c) => expect(c).toBeGreaterThan(250));
      simulate([0, 0, 0], d).forEach((c) => expect(c).toBeLessThan(2));
    }
  });

  it("makes red and green close under protanopia and deuteranopia, which is the problem", () => {
    const red = parseHex("#c41e3a");
    const green = parseHex("#009e60");
    const normal = deltaE(red, green);
    expect(deltaE(simulate(red, "deuteranopia"), simulate(green, "deuteranopia"))).toBeLessThan(normal);
    expect(deltaE(simulate(red, "protanopia"), simulate(green, "protanopia"))).toBeLessThan(normal);
  });

  it("measures black to white as the far end of the scale", () => {
    expect(deltaE([0, 0, 0], [255, 255, 255])).toBeCloseTo(100, 0);
    expect(deltaE([5, 5, 5], [5, 5, 5])).toBe(0);
  });
});

describe("palette review", () => {
  // The review recorded in DECISIONS.md. Judged on the WORST case across
  // normal vision and the three dichromacies: an alternative palette may
  // give up a little under normal vision for a lot under a deficiency.
  const worst = (type: PuzzleType, choice: "classic" | "high-contrast"): number =>
    Math.min(...DEFICIENCIES.map((d) => reviewPalette(paletteFor(type, choice), d).minDeltaE));

  it.each(TYPES.map((t) => [PuzzleType[t], t] as const))("%s: high-contrast's worst case beats classic's", (_name, type) => {
    expect(worst(type, "high-contrast")).toBeGreaterThan(worst(type, "classic"));
  });

  it("the cube's high-contrast colours stay clearly apart for everyone", () => {
    expect(worst(PuzzleType.Cubic, "high-contrast")).toBeGreaterThan(25);
  });

  it("prints the review for the record", () => {
    const lines: string[] = [];
    for (const type of TYPES) for (const choice of PALETTE_CHOICES) {
      const row = DEFICIENCIES.map((d) => {
        const r = reviewPalette(paletteFor(type, choice), d);
        return `${d} ${r.minDeltaE.toFixed(1)} (${r.closest.join("/")})`;
      });
      lines.push(`${PuzzleType[type]} ${choice}: ${row.join("; ")}`);
    }
    console.info(lines.join("\n"));
  });
});

describe("glyphsFor", () => {
  it("has one glyph per colour, none repeated", () => {
    for (const type of TYPES) {
      const glyphs = glyphsFor(type);
      expect(glyphs).toHaveLength(paletteFor(type, "classic").length);
      expect(new Set(glyphs).size).toBe(glyphs.length);
    }
  });
});
