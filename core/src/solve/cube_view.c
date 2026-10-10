#include "solve/cube_view.h"
#include "solve/movetable.h"

const t_solve_cube	SOLVED_SOLVE_CUBE = {
	.corner_perm = {
		SOLVE_CORNER_URF, SOLVE_CORNER_UFL, SOLVE_CORNER_ULB,
		SOLVE_CORNER_UBR, SOLVE_CORNER_DFR, SOLVE_CORNER_DLF,
		SOLVE_CORNER_DBL, SOLVE_CORNER_DRB
	},
	.corner_orient = {0, 0, 0, 0, 0, 0, 0, 0},
	.edge_perm = {
		SOLVE_EDGE_UR, SOLVE_EDGE_UF, SOLVE_EDGE_UL, SOLVE_EDGE_UB,
		SOLVE_EDGE_DR, SOLVE_EDGE_DF, SOLVE_EDGE_DL, SOLVE_EDGE_DB,
		SOLVE_EDGE_FR, SOLVE_EDGE_FL, SOLVE_EDGE_BL, SOLVE_EDGE_BR
	},
	.edge_orient = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
};

/// Slot offsets within a generic n==3 state buffer -- see this file's
/// header comment on why these are fixed constants here rather than
/// computed via shape_piece_offsets(): keeping solve/ from depending on
/// shape.h at all (it only ever touches t_solve_cube, never a generic
/// t_shape) is deliberate -- the one place that boundary is crossed is
/// here, explicitly, not threaded through every coordinate/table file
/// this directory ports.
# define STATE_CORNER_PERM 0
# define STATE_CORNER_ORIENT 8
# define STATE_EDGE_PERM 16
# define STATE_EDGE_ORIENT 28

void	solve_cube_from_state(t_solve_cube *cube, const uint8_t *state)
{
	int	i;

	i = 0;
	while (i < SOLVE_CORNER_COUNT)
	{
		cube->corner_perm[i] = state[STATE_CORNER_PERM + i];
		cube->corner_orient[i] = state[STATE_CORNER_ORIENT + i];
		i++;
	}
	i = 0;
	while (i < SOLVE_EDGE_COUNT)
	{
		cube->edge_perm[i] = state[STATE_EDGE_PERM + i];
		cube->edge_orient[i] = state[STATE_EDGE_ORIENT + i];
		i++;
	}
}

void	solve_cube_to_state(const t_solve_cube *cube, uint8_t *state)
{
	int	i;

	i = 0;
	while (i < SOLVE_CORNER_COUNT)
	{
		state[STATE_CORNER_PERM + i] = cube->corner_perm[i];
		state[STATE_CORNER_ORIENT + i] = cube->corner_orient[i];
		i++;
	}
	i = 0;
	while (i < SOLVE_EDGE_COUNT)
	{
		state[STATE_EDGE_PERM + i] = cube->edge_perm[i];
		state[STATE_EDGE_ORIENT + i] = cube->edge_orient[i];
		i++;
	}
}

bool	solve_cube_equal(const t_solve_cube *a, const t_solve_cube *b)
{
	int	i;

	i = 0;
	while (i < SOLVE_CORNER_COUNT)
	{
		if (a->corner_perm[i] != b->corner_perm[i]
			|| a->corner_orient[i] != b->corner_orient[i])
			return (false);
		i++;
	}
	i = 0;
	while (i < SOLVE_EDGE_COUNT)
	{
		if (a->edge_perm[i] != b->edge_perm[i]
			|| a->edge_orient[i] != b->edge_orient[i])
			return (false);
		i++;
	}
	return (true);
}

bool	solve_cube_is_solved(const t_solve_cube *cube)
{
	return (solve_cube_equal(cube, &SOLVED_SOLVE_CUBE));
}

void	solve_cube_apply_move(t_solve_cube *cube, int move)
{
	uint8_t	buf[SOLVE_CORNER_COUNT * 2 + SOLVE_EDGE_COUNT * 2];

	solve_cube_to_state(cube, buf);
	apply_n3_move(buf, move);
	solve_cube_from_state(cube, buf);
}

int	solve_cube_corner_twist_sum(const t_solve_cube *cube)
{
	int	sum;
	int	i;

	sum = 0;
	i = 0;
	while (i < SOLVE_CORNER_COUNT)
	{
		sum += cube->corner_orient[i];
		i++;
	}
	return (sum % 3);
}

int	solve_cube_edge_flip_sum(const t_solve_cube *cube)
{
	int	sum;
	int	i;

	sum = 0;
	i = 0;
	while (i < SOLVE_EDGE_COUNT)
	{
		sum += cube->edge_orient[i];
		i++;
	}
	return (sum % 2);
}

/// Ported from rubik's permutation_is_odd() (src/cube/cubie.c): counts
/// inversions. Generic, nothing cube-specific; O(n^2), fine for n = 8/12.
static bool	permutation_is_odd(const uint8_t *perm, int n)
{
	int	inversions;
	int	i;
	int	j;

	inversions = 0;
	i = 0;
	while (i < n)
	{
		j = i + 1;
		while (j < n)
		{
			if (perm[i] > perm[j])
				inversions++;
			j++;
		}
		i++;
	}
	return (inversions % 2 != 0);
}

bool	solve_cube_is_valid(const t_solve_cube *cube)
{
	if (solve_cube_corner_twist_sum(cube) != 0)
		return (false);
	if (solve_cube_edge_flip_sum(cube) != 0)
		return (false);
	return (permutation_is_odd(cube->corner_perm, SOLVE_CORNER_COUNT)
		== permutation_is_odd(cube->edge_perm, SOLVE_EDGE_COUNT));
}
