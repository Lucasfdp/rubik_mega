#include "solve/kociemba.h"

/// Ported from rubik/src/solve/solve.c. simplify_moves() itself is
/// unmodified; kociemba_solve() is adapted per kociemba.h's own header
/// comment (no cross-phase seam merge, so phase tagging stays exact).

bool	kociemba_init(t_kociemba_solver *solver)
{
	if (!move_tables_build(&solver->moves))
		return (false);
	if (!prune_tables_build(&solver->prune, &solver->moves))
	{
		move_tables_free(&solver->moves);
		return (false);
	}
	ida_phase1_setup(&solver->phase1, &solver->moves, &solver->prune);
	ida_phase2_setup(&solver->phase2, &solver->moves, &solver->prune);
	return (true);
}

void	kociemba_free(t_kociemba_solver *solver)
{
	prune_tables_free(&solver->prune);
	move_tables_free(&solver->moves);
}

/// @brief Adds the turns of two moves on the same face: 1 + 1 = 2,
///        1 + 3 = 0 (undo each other). A move is face * 3 + (quarter
///        turns - 1), so quarter turns are move % 3 + 1 and add mod 4.
/// @return The combined move, or -1 if they cancel out.
static int	combine_moves(int a, int b)
{
	int	quarters;

	quarters = (a % 3 + 1 + b % 3 + 1) % 4;
	if (quarters == 0)
		return (-1);
	return (a / 3 * 3 + quarters - 1);
}

static void	push_move(int *list, int *size, int move)
{
	int	at;
	int	merged;

	at = *size - 1;
	if (at >= 1 && list[at] / 3 == (move / 3 + 3) % 6
		&& list[at - 1] / 3 == move / 3)
		at--;
	if (at < 0 || list[at] / 3 != move / 3)
	{
		list[(*size)++] = move;
		return ;
	}
	merged = combine_moves(list[at], move);
	if (merged >= 0)
	{
		list[at] = merged;
		return ;
	}
	while (at + 1 < *size)
	{
		list[at] = list[at + 1];
		at++;
	}
	(*size)--;
}

int	simplify_moves(int *moves, int count)
{
	int	before;
	int	size;
	int	i;

	before = -1;
	while (before != count)
	{
		before = count;
		size = 0;
		i = 0;
		while (i < count)
		{
			push_move(moves, &size, moves[i]);
			i++;
		}
		count = size;
	}
	return (count);
}

int	kociemba_solve(const t_kociemba_solver *solver, const t_solve_cube *cube,
	int *out, int *phase1_count)
{
	int				path[IDA_MAX_DEPTH];
	t_solve_cube	work;
	uint8_t			buf[SOLVE_CORNER_COUNT * 2 + SOLVE_EDGE_COUNT * 2];
	uint16_t		start[3];
	int				first;
	int				second;
	int				i;

	if (!solve_cube_is_valid(cube))
		return (-1);
	work = *cube;
	ida_phase1_start(&work, start);
	first = ida_search(&solver->phase1, start, path);
	if (first < 0)
		return (-1);
	i = 0;
	while (i < first)
	{
		out[i] = path[i];
		i++;
	}
	solve_cube_to_state(&work, buf);
	i = 0;
	while (i < first)
		apply_n3_move(buf, path[i++]);
	solve_cube_from_state(&work, buf);
	ida_phase2_start(&work, start);
	second = ida_search(&solver->phase2, start, path);
	if (second < 0)
		return (-1);
	i = 0;
	while (i < second)
	{
		out[first + i] = path[i];
		i++;
	}
	*phase1_count = first;
	return (first + second);
}
