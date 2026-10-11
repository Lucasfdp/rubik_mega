#ifndef SOLVE_TWO_BY_TWO_H
# define SOLVE_TWO_BY_TWO_H

# include <stdbool.h>
# include "solve/cube_view.h"
# include "solve/movetable.h"
# include "solve/prune.h"
# include "solve/ida.h"

/// Ported from rubik/include/twobytwo.h and rubik/src/solve/two_by_two.c.
///
/// A 2x2x2 has corners and nothing else -- no edges, no centres.
/// Solving one from a cubic n==3 state is therefore corner-only
/// Kociemba: a SINGLE search (there is only one goal, so there is no
/// phase 1 / phase 2 split) over twist and cperm, with the full 18-move
/// set, never reading edge_perm or edge_orient. Every coordinate
/// encoder, move table, prune builder and the IDA* driver itself are
/// the exact same code the other three cubic n==3 algorithms already
/// use (solve/coord.h, solve/movetable.h, solve/prune.h, solve/ida.h)
/// -- nothing new was written for those layers, only this module's
/// setup of them (same claim rubik's own twobytwo.h makes about its own
/// codebase, equally true here).
///
/// A proven result (ported with the solver from rubik, whose own
/// docs/en/14-other-puzzles.md records the provenance: full
/// breadth-first enumeration of the 3,674,160-state 2x2x2 group under
/// the usual {U, R, F} speedcubing generator set) bounds any 2x2x2 at
/// 11 moves in the half-turn metric this project's full 18-move,
/// six-face generator set is a strict superset of (a bigger generator
/// set can only ever find an optimal solution at least as short, never
/// longer). `TWOBYTWO_MAX_MOVES` is that proven bound, not a guess --
/// ida_search() can never need more.
///
/// Registered as the one algorithm of the real 2x2x2 (cubic n == 2,
/// corners only, see CUBIC_MIN_SIZE). It is deliberately NOT offered on
/// the 3x3x3: there it solved only the corners and left the edges
/// scrambled. The solver itself still reads a t_solve_cube, so the
/// registry fills one from the 2x2x2's 16-byte state (edges left solved).
# define TWOBYTWO_MAX_MOVES 11

/// Everything the 2x2x2 solver needs, built once at startup. Unlike
/// rubik's own two_solver_init() (which builds a full, standalone set
/// of six move tables), this one does NOT own `moves`: it takes the
/// SAME t_move_tables the registry's shared Kociemba solver already
/// built (solve/kociemba.h), only ever reading ->twist and ->cperm --
/// same does-not-own contract thistle_init() already has, so choosing
/// "2x2x2" in the HUD never pays for a second, redundant ~2.9 MB set of
/// move tables.
typedef struct s_two_solver
{
	const t_move_tables	*moves;
	t_prune_table		twist_prune;
	t_prune_table		cperm_prune;
	t_ida_phase			phase;
}	t_two_solver;

/// @brief Builds the solver's own two small 1-D pruning tables (twist
///        alone, cperm alone -- see solve/two_by_two.c's own
///        ZERO_MOVES trick for how a 1-D table reuses the
///        two-coordinate-pair t_ida_phase machinery unmodified). Call
///        once.
///
/// @param solver Filled in. Its old contents are ignored.
/// @param moves  Already-built move tables (kept by pointer, not
///               copied; must outlive `solver`).
/// @return true on success. On false (malloc failed) nothing is left
///         allocated.
bool	two_solver_init(t_two_solver *solver, const t_move_tables *moves);

/// @brief Frees the two pruning tables two_solver_init() built. Never
///        frees `moves`. Safe to call twice.
void	two_solver_free(t_two_solver *solver);

/// @brief True if the cube's corners alone are solved. Edges are never
///        looked at: this, not solve_cube_is_solved(), is "solved" for
///        a 2x2x2, since it has no edges to be out of place.
bool	two_cube_is_solved(const t_solve_cube *cube);

/// @brief Solves the CORNERS of a cube (a 2x2x2 has nothing else).
///
/// @param solver An initialised solver.
/// @param cube   The cube to solve. Only the corner-twist invariant is
///               checked (solve_cube_corner_twist_sum(cube) == 0): a
///               2x2x2 has no edges, so unlike the other three
///               algorithms there is no permutation-parity invariant
///               linking corners to anything else.
/// @param out    Buffer of TWOBYTWO_MAX_MOVES moves; the solution is
///               written to out[0 .. return - 1], in order, in
///               solve/movetable.h's own flat move numbering.
/// @return The number of moves (0 if the corners are already solved),
///         or -1 if the corner invariant fails or no solution was
///         found within TWOBYTWO_MAX_MOVES (should never happen, given
///         the proven bound above).
int	two_solve(const t_two_solver *solver, const t_solve_cube *cube,
		int *out);

#endif
