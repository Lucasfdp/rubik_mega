#include "platform.h"
#include "solve/pyraminx_staged.h"
#include "solve/bytes.h"

/// The stage-0 and stage-1 tracked pieces: the four axial pieces, then
/// the three edges of the face opposite PYRAMINX_STAGED_VERTEX. Edge
/// slots come from tetrahedral_edge_slot() rather than a hard-coded
/// list, so the stage stays right if the slot numbering ever changes.
/// @return How many pieces were written (PYRAMINX_STAGE1_PIECES).
static uint32_t	first_layer_pieces(t_stage_piece *pieces)
{
	uint32_t	count;
	uint32_t	a;
	uint32_t	b;

	count = 0;
	while (count < PYRAMINX_STAGE0_PIECES)
	{
		pieces[count].piece_type = TETRA_AXIAL_TYPE;
		pieces[count].piece = (uint8_t)count;
		count++;
	}
	a = 0;
	while (a < TETRA_VERTEX_COUNT)
	{
		b = a + 1;
		while (b < TETRA_VERTEX_COUNT && a != PYRAMINX_STAGED_VERTEX)
		{
			if (b != PYRAMINX_STAGED_VERTEX)
			{
				pieces[count].piece_type = TETRA_EDGE_TYPE;
				pieces[count++].piece = tetrahedral_edge_slot(a, b);
			}
			b++;
		}
		a++;
	}
	return (count);
}

int	pyraminx_staged_init(t_pyraminx_staged *staged,
	const t_pyraminx_solver *base)
{
	t_stage_piece	pieces[PYRAMINX_STAGE1_PIECES];

	staged->base = base;
	if (first_layer_pieces(pieces) != PYRAMINX_STAGE1_PIECES)
		return (0);
	return (stage_table_init(&staged->axial, tetrahedral_shape(),
			tetrahedral_generators(), pieces, PYRAMINX_STAGE0_PIECES,
			base->table.moves, base->table.move_count, PYRAMINX_STAGE0_STATES)
		&& stage_table_init(&staged->first_layer, tetrahedral_shape(),
			tetrahedral_generators(), pieces, PYRAMINX_STAGE1_PIECES,
			base->table.moves, base->table.move_count,
			PYRAMINX_STAGE1_STATES));
}

/// Stage 2: the optimal table's descent over edges and axial pieces,
/// applied to `scratch` so the tips stage sees the result.
/// @return The number of moves written, or -1.
static int	finish_edges(const t_pyraminx_staged *staged, uint8_t *scratch,
	t_move *out, uint32_t max_moves)
{
	uint32_t	indices[PYRAMINX_STAGED_MAX_MOVES];
	int			count;
	int			i;

	count = coord_table_solve(&staged->base->table, scratch, indices,
			max_moves);
	i = 0;
	while (i < count)
	{
		out[i] = staged->base->table.moves[indices[i]];
		shape_apply_move(tetrahedral_shape(), tetrahedral_generators(),
			scratch, out[i]);
		i++;
	}
	return (count);
}

int	pyraminx_staged_solve(const t_pyraminx_staged *staged,
	const uint8_t *state, t_move *out, uint32_t max_moves,
	uint32_t *stage_ends)
{
	uint8_t	scratch[PYRAMINX_STATE_BYTES];
	int		count;
	int		step;

	if (max_moves > PYRAMINX_STAGED_MAX_MOVES)
		max_moves = PYRAMINX_STAGED_MAX_MOVES;
	solve_copy_bytes(scratch, state, PYRAMINX_STATE_BYTES);
	count = stage_table_descend(&staged->axial, scratch, out, max_moves);
	if (count < 0)
		return (-1);
	stage_ends[0] = (uint32_t)count;
	step = stage_table_descend(&staged->first_layer, scratch, out + count,
			max_moves - (uint32_t)count);
	if (step < 0)
		return (-1);
	count += step;
	stage_ends[1] = (uint32_t)count;
	step = finish_edges(staged, scratch, out + count,
			max_moves - (uint32_t)count);
	if (step < 0)
		return (-1);
	count += step;
	stage_ends[2] = (uint32_t)count;
	count = pyraminx_append_tips(scratch, out, (uint32_t)count, max_moves);
	if (count < 0)
		return (-1);
	stage_ends[3] = (uint32_t)count;
	return (count);
}
