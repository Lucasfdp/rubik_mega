#ifndef SOLVE_LAYER_H
# define SOLVE_LAYER_H

# include "solve/cube_view.h"
# include "solve/kociemba.h"

/// Ported from rubik/include/layer.h and rubik/src/solve/layer.c -- the
/// beginner method (layer-by-layer): white cross, first-layer corners,
/// second-layer edges, then the last layer. Unlike Kociemba/
/// Thistlethwaite, stages 1-3 need no coordinates, move tables or
/// pruning tables at all -- they locate each piece directly in the
/// t_solve_cube's own perm/orient arrays and apply one short, fixed,
/// pre-verified move sequence per case, exactly the lookup-table
/// approach a person follows by hand (see solve/layer.c's own header
/// comment for how those sequences were derived and verified). The last
/// layer is handed to the already-ported Kociemba solver
/// (solve/kociemba.h) rather than a hand-built OLL/PLL table -- a
/// genuinely complete beginner-method finish needs 57 OLL cases and 21
/// PLL cases, speedcubing-reference-book scope, not "derive and verify
/// against this engine" scope (rubik's own
/// docs/en/13-layer-by-layer-and-benchmark-spec.md, carried over
/// unchanged -- nothing about that trade-off is specific to rubik).

/// Move-count ceiling for the beginner method's output. Unlike Kociemba
/// (~23 moves) or Thistlethwaite (~40-45), the beginner method has no
/// tight theoretical bound -- 200 is a comfortably generous ceiling
/// derived from the worst case across every stage's pop+align+insert
/// (cross 4x12 + corners 4x9 + mid-edges 4x15 + a 30-move Kociemba
/// finish = 174 worst case, rubik's own derivation), verified in
/// practice never to be approached.
# define LAYER_MAX_MOVES 200

/// @brief Solves `cube` with the layer-by-layer (beginner) method.
///
/// @param cube         The cube to solve. Must be a real cube
///                      (solve_cube_is_valid()).
/// @param kociemba      An already-initialised Kociemba solver
///                      (solve/kociemba.h), used ONLY for the last
///                      layer. Not owned or freed here -- same
///                      does-not-own contract thistle_init() already
///                      has for its own move tables, so the registry's
///                      one shared, already-built solver
///                      (solve/registry.c's g_kociemba) is reused
///                      rather than rebuilding its ~6.8 MB of tables
///                      on every call.
/// @param out          Buffer of LAYER_MAX_MOVES moves; the solution is
///                      written to out[0 .. return - 1], in order, in
///                      solve/movetable.h's own flat move numbering.
/// @param phase_bounds Set to the CUMULATIVE move count ending each of
///                      the four stages (phase_bounds[0] = the white
///                      cross, [1] = first-layer corners, [2] =
///                      second-layer edges, [3] = the last layer, == the
///                      return value) -- same role as thistle_solve()'s
///                      own phase_bounds (solve/thistlethwaite.h), for
///                      solve/registry.c's adapter to turn into
///                      phase_tags.
/// @return The number of moves (0 if already solved), or -1 if the cube
///         is not valid, a stage overflowed LAYER_MAX_MOVES, or the
///         last layer's Kociemba finish found no solution.
int	layer_solve(const t_solve_cube *cube, const t_kociemba_solver *kociemba,
		int *out, int phase_bounds[4]);

#endif
