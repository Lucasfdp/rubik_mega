import { describe, expect, it } from "vitest";
import { cursorForFraction, phaseSegments, segmentAt } from "../../src/interaction/phase-segments.js";

describe("phaseSegments", () => {
  it("cuts the moves wherever the phase tag changes", () => {
    expect(phaseSegments([0, 0, 1, 1, 1, 2])).toEqual([
      { phase: 0, start: 0, end: 2 },
      { phase: 1, start: 2, end: 5 },
      { phase: 2, start: 5, end: 6 },
    ]);
  });

  it("gives an empty solution no segments", () => {
    expect(phaseSegments([])).toEqual([]);
  });

  it("does not merge a tag that comes back with its earlier run", () => {
    expect(phaseSegments([0, 1, 0]).map((s) => s.phase)).toEqual([0, 1, 0]);
  });

  it("tiles the moves exactly: each segment starts where the last ended", () => {
    const segments = phaseSegments([0, 0, 0, 1, 2, 2, 3]);
    expect(segments[0]!.start).toBe(0);
    segments.slice(1).forEach((s, i) => expect(s.start).toBe(segments[i]!.end));
    expect(segments.at(-1)!.end).toBe(7);
  });
});

describe("segmentAt", () => {
  const segments = phaseSegments([0, 0, 1, 1]);

  it("finds the segment a move index falls in, at both edges", () => {
    expect(segmentAt(segments, 1)?.phase).toBe(0);
    expect(segmentAt(segments, 2)?.phase).toBe(1);
  });

  it("puts a finished solve's cursor in the last segment", () => {
    expect(segmentAt(segments, 4)?.phase).toBe(1);
  });

  it("is null with nothing to show", () => {
    expect(segmentAt([], 0)).toBeNull();
  });
});

describe("cursorForFraction", () => {
  it("maps the ends of the track to nothing and everything played", () => {
    expect(cursorForFraction(0, 10)).toBe(0);
    expect(cursorForFraction(1, 10)).toBe(10);
  });

  it("snaps to the nearest boundary and clamps out-of-range presses", () => {
    expect(cursorForFraction(0.34, 10)).toBe(3);
    expect(cursorForFraction(-1, 10)).toBe(0);
    expect(cursorForFraction(7, 10)).toBe(10);
  });

  it("is 0 for an empty solution", () => {
    expect(cursorForFraction(0.5, 0)).toBe(0);
  });
});
