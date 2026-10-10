/// The solve timeline's pieces (docs/en/13-phase6-polish.md D8): a
/// solution's moves, cut wherever the algorithm moves on to its next
/// phase. Pure, so the boundaries are tested without a DOM.

/// A run of consecutive moves from one phase. `start` is the index of its
/// first move and `end` one past its last, so `end - start` is its length
/// and a cursor `c` (the index of the NEXT move to play) is inside it when
/// `start <= c < end`.
export interface PhaseSegment {
  phase: number;
  start: number;
  end: number;
}

/// Cuts `phaseTags` (one 0-indexed phase per move, SolveResult.phaseTags)
/// into runs of equal tags. An algorithm only ever moves forward through
/// its phases, but this makes no assumption: a tag that comes back is a
/// new segment, not merged with the earlier run of the same number.
export function phaseSegments(phaseTags: readonly number[]): PhaseSegment[] {
  const segments: PhaseSegment[] = [];
  phaseTags.forEach((phase, index) => {
    const last = segments[segments.length - 1];
    if (last && last.phase === phase) last.end = index + 1;
    else segments.push({ phase, start: index, end: index + 1 });
  });
  return segments;
}

/// The segment move `index` belongs to, or null for an empty solution.
/// An index past the end belongs to the last segment (the cursor of a
/// finished solve), so the timeline never has nothing to show.
export function segmentAt(segments: readonly PhaseSegment[], index: number): PhaseSegment | null {
  if (segments.length === 0) return null;
  const found = segments.find((s) => index >= s.start && index < s.end);
  return found ?? (index < 0 ? segments[0]! : segments[segments.length - 1]!);
}

/// The cursor position (0..moveCount) a press at `fraction` (0..1) along
/// the timeline's track means: the nearest boundary between two moves, so
/// the far left is "nothing played" and the far right "all played".
export function cursorForFraction(fraction: number, moveCount: number): number {
  if (moveCount <= 0) return 0;
  const clamped = Math.min(Math.max(fraction, 0), 1);
  return Math.round(clamped * moveCount);
}
