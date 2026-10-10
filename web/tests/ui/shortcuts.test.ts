import { describe, expect, it } from "vitest";
import { PuzzleType } from "../../src/engine/types.js";
import { shortcutsFor } from "../../src/ui/shell/shortcuts-dialog.js";

describe("shortcutsFor", () => {
  it("lists face letters for a cube and number keys for the others", () => {
    const cube = shortcutsFor(PuzzleType.Cubic).map((r) => r.keys);
    const skewb = shortcutsFor(PuzzleType.Skewb).map((r) => r.keys);
    expect(cube).toContain("U R F D L B");
    expect(cube).not.toContain("1 – 9, 0, -, =");
    expect(skewb).toContain("1 – 9, 0, -, =");
    expect(skewb).not.toContain("U R F D L B");
  });

  it("always includes the shared shortcuts", () => {
    for (const type of [PuzzleType.Cubic, PuzzleType.Square1]) {
      const keys = shortcutsFor(type).map((r) => r.keys);
      expect(keys).toContain("Space");
      expect(keys).toContain("?");
    }
  });
});
