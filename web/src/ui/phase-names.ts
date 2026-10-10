/// Human-readable phase/stage names per algorithm, keyed by the
/// algorithm's own name as core reports it (puzzle_algorithm_name) and
/// indexed by each algorithm's 0-based phase_tags values. Keyed by name,
/// not registry index, because indices restart per size: algorithm 0 is
/// Kociemba on a 3x3x3 but reduction on a 5x5x5
/// (docs/en/08-nxn-reduction.md D4). Deliberately a plain table kept in
/// this one file -- core never names its own phases, only bounds them --
/// so a change to a solver's name or phase count in
/// core/src/solve/registry.c MUST update this table by hand.
///
/// Phase descriptions are taken from each solver's own doc comments
/// (core/src/solve/thistlethwaite.c's phase1-4 @brief tags; core/src/
/// solve/layer.c's stage comments; core/src/solve/ida.c's
/// ida_phase1_setup/ida_phase2_setup for Kociemba; core/include/solve/
/// reduction.h's REDUCTION_PHASE_* for reduction), not invented for the UI.
const PHASE_NAMES: Readonly<Record<string, readonly string[]>> = {
  // Kociemba -- registry.c's kociemba_algorithm_solve
  // DOES tag two phase values (phase_tags[i] = (i < phase1) ? 0u : 1u),
  // one per entry here. This table previously had only one entry,
  // which made phaseName(0, 1) throw partway through every real
  // Kociemba solve (any solve that actually reaches phase 2) -- that
  // throw happened inside SolvePlayback's onSettled, before
  // #tryAdvance() could run, which is what was freezing playback
  // around move 8-10. ida_phase1_setup() reduces twist/flip/slice to
  // zero using every move (reaching the G1 coset <U,D,L,R,F2,B2>);
  // ida_phase2_setup() then finishes cperm/eperm/sperm using only
  // G1's own moves (MOVES_PHASE2).
  "Kociemba (two-phase)": ["Phase 1: reduce to the G1 coset", "Phase 2: finish from G1"],
  // Thistlethwaite -- thistlethwaite.c phase1_setup..phase4_setup.
  "Thistlethwaite (four-phase)": [
    "Phase 1: orient edges",
    "Phase 2: orient corners, place middle-slice edges",
    "Phase 3: place corners and edges into their final slice",
    "Phase 4: finish with half turns",
  ],
  // Layer-by-layer -- layer.c's stage comments.
  "Layer-by-layer (beginner)": [
    "Stage 1: bottom cross",
    "Stage 2: first-layer corners",
    "Stage 3: second-layer edges",
    "Stage 4: last layer",
  ],
  // 2x2x2 -- two_by_two.c solves corners in one pass;
  // edges are deliberately left untouched (see core's own test "edges
  // are ignored"), so there is only ever one phase to name.
  "2x2x2 (corners only)": ["Solving corners"],
  // Skewb -- core/src/solve/skewb_optimal.c: one table descent, so one
  // phase (registry.c tags every move 0).
  "Optimal (full depth table)": ["Optimal solve"],
  // Pyraminx -- core/src/solve/pyraminx_optimal.c: the edges and axial
  // pieces by table descent (layer turns, phase 0), then the tips (one
  // tip turn each, phase 1; registry.c's pyraminx_algorithm_solve).
  "Optimal, then tips (full depth table)": ["Edges and axial pieces", "Tips"],
  // Staged Skewb -- core/include/solve/skewb_staged.h: three cumulative
  // goals in the canonical frame (registry.c's skewb_staged_algorithm_solve
  // tags each move with the stage it belongs to). The "first face" is the
  // top face of the framed state; see D3 in docs/en/10-phase3-human-methods.md.
  "Face-first (staged)": ["First-face corners", "First-face centre", "Finish"],
  // Staged Pyraminx -- core/include/solve/pyraminx_staged.h: axial pieces,
  // then the three edges of the face opposite vertex 0, then everything
  // else by the optimal table, then the tips (pyraminx_staged_algorithm_solve).
  "Layer-first (staged)": [
    "Align axial pieces",
    "First layer edges",
    "Finish edges and axials",
    "Tips",
  ],
  // Kilominx -- core/src/solve/dodeca_solver.c: every corner placed with
  // its twist in one pass of 3-cycles (DODECA_PHASE_CORNERS), so one
  // phase. "(not optimal)" is part of the name core reports, on purpose.
  "Corner 3-cycles (not optimal)": ["Corners"],
  // Megaminx -- the same corner pass, then every edge placed with its
  // flip (DODECA_PHASE_EDGES); centres never move.
  "Corner then edge 3-cycles (not optimal)": ["Corners", "Edges"],
  // Square-1 -- core/include/solve/square1_solver.h's SQ1_PHASE_*: every
  // move up to the point both layers are cube-shaped is phase 0, the rest
  // (which keeps the cube shape and puts the pieces in place) phase 1.
  "Two-phase (near-optimal)": ["Cube shape", "Finish"],
  // NxN reduction -- core/include/solve/reduction.h's REDUCTION_PHASE_*.
  // The 3x3x3 part runs before wings and centres (see run_stages() in
  // core/src/solve/reduction.c for why).
  "Reduction (corners, edges, centres)": [
    "Setup: fixed centres and parity",
    "Corners and middle edges: reduce to G1",
    "Corners and middle edges: finish",
    "Edges: every wing home",
    "Centres",
  ],
};

/// All phase names for the algorithm core calls `algorithmName`, in
/// phase order.
export function phaseNamesForAlgorithm(algorithmName: string): readonly string[] {
  const names = PHASE_NAMES[algorithmName];
  if (!names) {
    throw new Error(
      `phaseNamesForAlgorithm: no phase names recorded for algorithm "${algorithmName}" -- ` +
        "ui/phase-names.ts has drifted from core/src/solve/registry.c",
    );
  }
  return names;
}

/// The name of one specific phase -- what the solve panel actually
/// shows next to the move counter while a solve plays back.
///
/// Never throws: an unknown algorithm or out-of-range phase falls back
/// to a generic label and logs. This lookup used to throw (Kociemba's
/// table was missing phase 1), and because solve-panel.ts calls it from
/// inside SolvePlayback's onSettled -- BEFORE #tryAdvance() runs in that
/// same callback -- the throw permanently froze autoplay with no visible
/// error beyond the console. A table drifting from registry.c again
/// should degrade to a vague-but-correct status line, not wedge playback.
export function phaseName(algorithmName: string, phaseTag: number): string {
  const name = PHASE_NAMES[algorithmName]?.[phaseTag];
  if (name === undefined) {
    console.error(
      `phaseName: no name for phase ${phaseTag} of "${algorithmName}" -- ` +
        "ui/phase-names.ts has drifted from core/src/solve/registry.c; falling back to a generic label",
    );
    return "Solving";
  }
  return name;
}
