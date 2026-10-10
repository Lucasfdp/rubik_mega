#include "solve/two_by_two.h"

/// Ported from rubik/src/solve/two_by_two.c -- unmodified logic
/// (corner-only IDA* search), only the move-table ownership adapted
/// (solve/two_by_two.h's own header comment).

/// A coordinate that never changes: its only value is 0, and every move
/// maps 0 to 0. This lets the generic prune_build()/t_ida_phase
/// machinery -- built for THREE coordinates and two pruning PAIRS --
/// treat twist and cperm as two independent 1-D tables: prune_dist
/// (table, x, 0) is just "distance of x alone" once the table's second
/// coordinate has exactly one possible value. Zero new code in
/// solve/prune.c or solve/ida.c for that: this static array is the
/// entire trick (same one thistlethwaite.c's own DUMMY_MOVES uses for
/// phase 1's flip-alone table).
static const uint16_t	ZERO_MOVES[MOVE_COUNT];

bool	two_cube_is_solved(const t_solve_cube *cube)
{
	int	i;

	i = 0;
	while (i < SOLVE_CORNER_COUNT)
	{
		if (cube->corner_perm[i] != i || cube->corner_orient[i] != 0)
			return (false);
		i++;
	}
	return (true);
}

bool	two_solver_init(t_two_solver *solver, const t_move_tables *moves)
{
	solver->moves = moves;
	if (!prune_build(&solver->twist_prune, moves->twist, TWIST_COUNT,
			ZERO_MOVES, 1, MOVES_ALL))
		return (false);
	if (!prune_build(&solver->cperm_prune, moves->cperm, CPERM_COUNT,
			ZERO_MOVES, 1, MOVES_ALL))
	{
		prune_free(&solver->twist_prune);
		return (false);
	}
	solver->phase.move_table[0] = moves->twist;
	solver->phase.move_table[1] = moves->cperm;
	solver->phase.move_table[2] = ZERO_MOVES;
	solver->phase.prune_xz = &solver->twist_prune;
	solver->phase.prune_yz = &solver->cperm_prune;
	solver->phase.moves = MOVES_ALL;
	solver->phase.max_depth = TWOBYTWO_MAX_MOVES;
	return (true);
}

void	two_solver_free(t_two_solver *solver)
{
	prune_free(&solver->twist_prune);
	prune_free(&solver->cperm_prune);
}

/// One IDA* search, not two phases: a 2x2x2 has only one goal to reach
/// (corners solved), so there is nothing for a phase boundary to split.
int	two_solve(const t_two_solver *solver, const t_solve_cube *cube,
	int *out)
{
	uint16_t	start[3];

	if (solve_cube_corner_twist_sum(cube) != 0)
		return (-1);
	if (two_cube_is_solved(cube))
		return (0);
	start[0] = encode_twist(cube);
	start[1] = encode_cperm(cube);
	start[2] = 0;
	return (ida_search(&solver->phase, start, out));
}
