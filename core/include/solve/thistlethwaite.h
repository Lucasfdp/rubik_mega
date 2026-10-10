#ifndef SOLVE_THISTLETHWAITE_H
# define SOLVE_THISTLETHWAITE_H

# include <stdbool.h>
# include <stdint.h>
# include "solve/cube_view.h"
# include "solve/movetable.h"
# include "solve/prune.h"
# include "solve/ida.h"

/// Ported from rubik/include/thistlethwaite.h -- Thistlethwaite's
/// four-phase method, descending through the same nested subgroups
/// Kociemba's two-phase solver already uses one of (G2 below is exactly
/// Kociemba's own intermediate group), just in four smaller steps
/// instead of two bigger ones. See solve/coord.h's own header comment
/// for the overall porting approach, and docs/en/06-phase2-solvers.md
/// for why this phase needed no table changes at all -- every
/// coordinate here is one move_tables_build() (solve/movetable.c)
/// already built; this file adds only new goals and the two new move
/// masks (MOVES_G1, MOVES_G3 -- solve/movetable.h) for the existing
/// ones.
///
/// G0 = <U, D, L, R, F, B>          all 18 moves
/// G1 = <U, D, L, R, F2, B2>        edge orientation fixed   (flip == 0)
/// G2 = <U, D, L2, R2, F2, B2>      + corner orientation, slice (== MOVES_PHASE2)
/// G3 = <U2, D2, L2, R2, F2, B2>    half turns only
/// G4 = {identity}                    solved

/// Naming alias only: Thistlethwaite's G2 and Kociemba's own
/// intermediate group (MOVES_PHASE2, solve/movetable.h) are the same
/// set of moves. A reader of this file doesn't have to already know
/// that to follow it.
# define MOVES_G2 MOVES_PHASE2

/// Known worst-case depth per phase (ported with the solver from
/// rubik, whose own docs/en/02-algorithms.md S3.B records it): 7, 10,
/// 13, 15 moves, 45 total. Safety ceilings for ida_search(), not
/// tightness requirements -- IDA* still finds the shortest solution
/// within whatever cap it's given, it just fails past it.
# define THISTLE_PHASE1_CAP 7
# define THISTLE_PHASE2_CAP 10
# define THISTLE_PHASE3_CAP 13
# define THISTLE_PHASE4_CAP 15

/// Size of the move buffer thistle_solve() needs: the four caps added
/// up.
# define THISTLE_MAX_MOVES ( \
	THISTLE_PHASE1_CAP + THISTLE_PHASE2_CAP \
	+ THISTLE_PHASE3_CAP + THISTLE_PHASE4_CAP)

/// Everything thistle_solve() needs, built once at startup.
///
/// - flip_dummy: phase 1's table. flip has only one real coordinate, so
///   it is paired with a dummy coordinate of a single, always-0 value
///   (this file's own thistle_phase1_setup(), solve/thistlethwaite.c)
///   instead of adding a 1-D table shape to solve/prune.h.
/// - twist_slice: phase 2's table, built tight with MOVES_G1.
/// - cperm_sperm3 / eperm_sperm3: phase 3's tables. Their goal is a SET
///   of pairs (every pair reachable in G3), built with
///   prune_build_coset() over the reachable sets below.
/// - cperm_sperm4 / eperm_sperm4: phase 4's tables, built tight with
///   MOVES_G3, goal is the single pair (0, 0) like every Kociemba
///   table.
/// - cp_g3 / ep_g3 / sp_g3: which cperm / eperm / sperm values a cube
///   can have while still inside G3 (reachable_set() from 0 with
///   MOVES_G3, solve/prune.h). Owned by this struct; freed by
///   thistle_free().
typedef struct s_thistle_tables
{
	t_prune_table	flip_dummy;
	t_prune_table	twist_slice;
	t_prune_table	cperm_sperm3;
	t_prune_table	eperm_sperm3;
	t_prune_table	cperm_sperm4;
	t_prune_table	eperm_sperm4;
	bool			*cp_g3;
	bool			*ep_g3;
	bool			*sp_g3;
}	t_thistle_tables;

/// A Thistlethwaite solver. Does NOT own `moves`: pass the same
/// t_move_tables a t_kociemba_solver (solve/kociemba.h) already built,
/// or build one standalone with move_tables_build() -- either way,
/// thistle_free() never touches it.
typedef struct s_thistle_solver
{
	const t_move_tables	*moves;
	t_thistle_tables	prune;
	t_ida_phase			phase1;
	t_ida_phase			phase2;
	t_ida_phase			phase3;
	t_ida_phase			phase4;
}	t_thistle_solver;

/// @brief Builds every Thistlethwaite table and phase setup.
///
/// @param solver Filled in. Its old contents are ignored.
/// @param moves  Already-built move tables (kept by pointer, not
///               copied; must outlive `solver`).
/// @return true on success. On false (malloc failed) nothing is left
///         allocated.
bool	thistle_init(t_thistle_solver *solver, const t_move_tables *moves);

/// @brief Frees everything thistle_init() allocated. Never frees
///        `moves`. Safe to call twice.
void	thistle_free(t_thistle_solver *solver);

/// @brief Solves a cube in four phases.
///
/// Each phase is shortest for its own step; the four joined together
/// average 40-45 moves -- more than Kociemba's ~23, but a genuinely
/// different algorithm built almost entirely from code Kociemba
/// already needed (docs/en/06-phase2-solvers.md). Unlike rubik's own
/// thistle_solve(), this adapter does NOT run simplify_moves() across
/// the phase seams afterwards -- see this file's solve/thistlethwaite.c
/// header comment for why, matching solve/kociemba.h's own documented
/// choice for the same reason.
///
/// @param solver       An initialised solver.
/// @param cube         The cube to solve. Must be a real cube
///                     (solve_cube_is_valid()).
/// @param out          Buffer of THISTLE_MAX_MOVES moves; the solution
///                     is written to out[0 .. return - 1], in order,
///                     in solve/movetable.h's own flat move numbering.
/// @param phase_bounds Set to the CUMULATIVE move count ending each of
///                     the four phases (phase_bounds[0] = phase 1's own
///                     count, ..., phase_bounds[3] == the return value)
///                     -- how solve/registry.c's adapter turns this
///                     into phase_tags, the same role kociemba_solve()'s
///                     own phase1_count plays for two phases instead of
///                     four.
/// @return The number of moves (0 if already solved), or -1 if the
///         cube is not valid or no solution was found within the depth
///         caps.
int	thistle_solve(const t_thistle_solver *solver, const t_solve_cube *cube,
		int *out, int phase_bounds[4]);

#endif
