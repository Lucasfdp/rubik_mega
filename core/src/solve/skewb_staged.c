#include "platform.h"
#include "solve/skewb_staged.h"
#include "solve/bytes.h"

/// The stage-0 and stage-1 tracked pieces: the four corners with the y
/// bit clear, then the top centre.
/// @return How many pieces were written (SKEWB_STAGE1_PIECES).
static uint32_t	top_face_pieces(t_stage_piece *pieces)
{
	uint32_t	count;
	uint32_t	corner;

	count = 0;
	corner = 0;
	while (corner < SKEWB_CORNER_COUNT)
	{
		if (!(corner & SKEWB_CORNER_Y_BIT))
		{
			pieces[count].piece_type = SKEWB_CORNER_TYPE;
			pieces[count++].piece = (uint8_t)corner;
		}
		corner++;
	}
	pieces[count].piece_type = SKEWB_CENTRE_TYPE;
	pieces[count++].piece = SKEWB_CENTRE_TOP;
	return (count);
}

int	skewb_staged_init(t_skewb_staged *staged, const t_skewb_solver *base)
{
	t_stage_piece	pieces[SKEWB_STAGE1_PIECES];

	staged->base = base;
	if (top_face_pieces(pieces) != SKEWB_STAGE1_PIECES)
		return (0);
	return (stage_table_init(&staged->corners, skewb_shape(),
			skewb_generators(), pieces, SKEWB_STAGE0_PIECES,
			base->table.moves, base->table.move_count, SKEWB_STAGE0_STATES)
		&& stage_table_init(&staged->centre, skewb_shape(),
			skewb_generators(), pieces, SKEWB_STAGE1_PIECES,
			base->table.moves, base->table.move_count,
			SKEWB_STAGE1_STATES));
}

/// Stage 2: the optimal table's descent over the whole group, applied to
/// `framed` for symmetry with the other stages.
/// @return The number of moves written, or -1.
static int	finish(const t_skewb_staged *staged, uint8_t *framed, t_move *out,
	uint32_t max_moves)
{
	uint32_t	indices[SKEWB_STAGED_MAX_MOVES];
	int			count;
	int			i;

	count = coord_table_solve(&staged->base->table, framed, indices,
			max_moves);
	i = 0;
	while (i < count)
	{
		out[i] = staged->base->table.moves[indices[i]];
		shape_apply_move(skewb_shape(), skewb_generators(), framed, out[i]);
		i++;
	}
	return (count);
}

int	skewb_staged_solve(const t_skewb_staged *staged, const uint8_t *state,
	t_move *out, uint32_t max_moves, uint32_t *stage_ends)
{
	uint8_t	framed[SKEWB_STATE_BYTES];
	int		frame;
	int		count;
	int		step;

	frame = skewb_find_frame(staged->base, state, framed);
	if (frame < 0)
		return (-1);
	if (max_moves > SKEWB_STAGED_MAX_MOVES)
		max_moves = SKEWB_STAGED_MAX_MOVES;
	count = stage_table_descend(&staged->corners, framed, out, max_moves);
	if (count < 0)
		return (-1);
	stage_ends[0] = (uint32_t)count;
	step = stage_table_descend(&staged->centre, framed, out + count,
			max_moves - (uint32_t)count);
	if (step < 0)
		return (-1);
	count += step;
	stage_ends[1] = (uint32_t)count;
	step = finish(staged, framed, out + count, max_moves - (uint32_t)count);
	if (step < 0)
		return (-1);
	count += step;
	stage_ends[2] = (uint32_t)count;
	step = 0;
	while (step < count)
	{
		out[step] = skewb_unframe_move(staged->base, frame, out[step]);
		step++;
	}
	return (count);
}
