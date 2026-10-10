import { describe, expect, it } from "vitest";

// Scaffolding-era smoke test: proves the test runner itself works.
// Replaced by real coverage (coordinate encoders, lattice generators,
// pointer-drag math) as docs/en/04-roadmap.md's phases land.
describe("scaffolding", () => {
  it("test runner is wired up", () => {
    expect(1 + 1).toBe(2);
  });
});
