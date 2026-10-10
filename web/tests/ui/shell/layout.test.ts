import { describe, expect, it } from "vitest";
import { layoutModeFor } from "../../../src/ui/shell/layout.js";

describe("layoutModeFor", () => {
  it.each([
    [390, 844, "compact"],
    [844, 390, "regular"],
    [820, 1180, "compact"],
    [1180, 820, "regular"],
    [1440, 900, "regular"],
    [600, 900, "compact"],
    [599, 400, "compact"],
    [1100, 1400, "regular"],
  ] as const)("%i x %i is %s", (width, height, expected) => {
    expect(layoutModeFor({ width, height })).toBe(expected);
  });
});
