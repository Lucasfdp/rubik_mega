#include "platform.h"
#include "solve/pyraminx_optimal.h"
#include "solve/bytes.h"

/// The search moves: the layer turn U at each vertex, both directions.
/// The tip-only turns u are not here -- they are the second phase.
static uint32_t	layer_moves_list(t_move *moves)
{
	uint32_t	count;
	uint32_t	vertex;
	uint32_t	amount;

	count = 0;
	vertex = 0;
	while (vertex < TETRA_VERTEX_COUNT)
	{
		amount = 1;
		while (amount <= TETRA_STEPS)
		{
			moves[count].generator = (int16_t)(vertex * TETRA_DEPTH_COUNT
					+ TETRA_DEPTH_LAYER);
			moves[count++].amount = (int8_t)amount++;
		}
		vertex++;
	}
	return (count);
}

int	pyraminx_solver_init(t_pyraminx_solver *solver)
{
	uint32_t	blocks[PYRAMINX_BLOCK_COUNT];
	t_move		moves[COORD_MAX_MOVES];
	uint32_t	count;

	if (!tetrahedral_generators())
		return (0);
	blocks[0] = TETRA_AXIAL_TYPE;
	blocks[1] = TETRA_EDGE_TYPE;
	count = layer_moves_list(moves);
	return (coord_table_init(&solver->table, tetrahedral_shape(),
			tetrahedral_generators(), blocks, PYRAMINX_BLOCK_COUNT, moves,
			count));
}

/// The tip turn that brings tip `vertex` of `state` home: amount 1 or 2
/// of that vertex's tip-only generator, whichever leaves its orientation
/// 0. Found by trying both on a copy rather than assuming which
/// direction the derived generator twists, so it stays right if the
/// orientation convention ever changes.
/// @return The amount, 0 if the tip is already home, -1 if neither works
///         (a malformed orientation byte).
static int	tip_amount(const uint8_t *state, uint32_t vertex)
{
	uint8_t		copy[PYRAMINX_STATE_BYTES];
	t_move		move;
	uint32_t	perm_offset;
	uint32_t	orient_offset;

	shape_piece_offsets(tetrahedral_shape(), TETRA_TIP_TYPE, &perm_offset,
		&orient_offset);
	if (state[orient_offset + vertex] == 0)
		return (0);
	move.generator = (int16_t)(vertex * TETRA_DEPTH_COUNT + TETRA_DEPTH_TIP);
	move.amount = 1;
	while (move.amount <= (int8_t)TETRA_STEPS)
	{
		solve_copy_bytes(copy, state, PYRAMINX_STATE_BYTES);
		shape_apply_move(tetrahedral_shape(), tetrahedral_generators(), copy,
			move);
		if (copy[orient_offset + vertex] == 0)
			return (move.amount);
		move.amount++;
	}
	return (-1);
}

int	pyraminx_append_tips(const uint8_t *state, t_move *out, uint32_t count,
	uint32_t max_moves)
{
	uint32_t	vertex;
	int			amount;

	vertex = 0;
	while (vertex < TETRA_VERTEX_COUNT)
	{
		amount = tip_amount(state, vertex);
		if (amount < 0)
			return (-1);
		if (amount > 0)
		{
			if (count == max_moves)
				return (-1);
			out[count].generator = (int16_t)(vertex * TETRA_DEPTH_COUNT
					+ TETRA_DEPTH_TIP);
			out[count++].amount = (int8_t)amount;
		}
		vertex++;
	}
	return ((int)count);
}

int	pyraminx_solve(const t_pyraminx_solver *solver, const uint8_t *state,
	t_move *out, uint32_t max_moves, uint32_t *layer_moves)
{
	uint8_t		scratch[PYRAMINX_STATE_BYTES];
	uint32_t	indices[PYRAMINX_MAX_MOVES];
	int			count;
	int			i;

	if (max_moves > PYRAMINX_MAX_MOVES)
		max_moves = PYRAMINX_MAX_MOVES;
	count = coord_table_solve(&solver->table, state, indices, max_moves);
	if (count < 0)
		return (-1);
	solve_copy_bytes(scratch, state, PYRAMINX_STATE_BYTES);
	i = 0;
	while (i < count)
	{
		out[i] = solver->table.moves[indices[i]];
		shape_apply_move(tetrahedral_shape(), tetrahedral_generators(),
			scratch, out[i]);
		i++;
	}
	*layer_moves = (uint32_t)count;
	return (pyraminx_append_tips(scratch, out, (uint32_t)count, max_moves));
}
