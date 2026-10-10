#include "shape.h"

uint32_t	shape_total_pieces(const t_shape *shape)
{
	uint32_t	total;
	uint32_t	i;

	total = 0;
	i = 0;
	while (i < shape->piece_type_count)
	{
		total += shape->piece_types[i].count;
		i++;
	}
	return (total);
}

uint32_t	shape_state_size(const t_shape *shape)
{
	// One byte for the permutation slot, one for the orientation, per
	// piece -- see shape.h's doc comment on shape_state_size().
	return (shape_total_pieces(shape) * 2);
}

bool	shape_is_valid(const t_shape *shape)
{
	uint32_t	i;

	if (shape->piece_type_count == 0
		|| shape->piece_type_count > MAX_PIECE_TYPES)
		return (false);
	i = 0;
	while (i < shape->piece_type_count)
	{
		if (shape->piece_types[i].count == 0
			|| shape->piece_types[i].count > 255
			|| shape->piece_types[i].orientations == 0)
			return (false);
		i++;
	}
	return (true);
}

void	shape_piece_offsets(const t_shape *shape, uint32_t piece_type,
	uint32_t *perm_offset, uint32_t *orient_offset)
{
	uint32_t	offset;
	uint32_t	i;

	offset = 0;
	i = 0;
	while (i < piece_type)
	{
		offset += shape->piece_types[i].count * 2;
		i++;
	}
	*perm_offset = offset;
	*orient_offset = offset + shape->piece_types[piece_type].count;
}

void	shape_init_solved(const t_shape *shape, uint8_t *state)
{
	uint32_t	perm_offset;
	uint32_t	orient_offset;
	uint32_t	type;
	uint32_t	slot;

	type = 0;
	while (type < shape->piece_type_count)
	{
		shape_piece_offsets(shape, type, &perm_offset, &orient_offset);
		slot = 0;
		while (slot < shape->piece_types[type].count)
		{
			state[perm_offset + slot] = (uint8_t)slot;
			state[orient_offset + slot] = 0;
			slot++;
		}
		type++;
	}
}

void	shape_apply_cycle(const t_shape *shape, uint8_t *state,
	const t_cycle *cycle)
{
	uint32_t	perm_offset;
	uint32_t	orient_offset;
	uint32_t	orientations;
	uint8_t		*perm;
	uint8_t		*orient;
	uint8_t		last_perm;
	uint8_t		last_orient;
	int			i;

	shape_piece_offsets(shape, cycle->piece_type, &perm_offset,
		&orient_offset);
	perm = state + perm_offset;
	orient = state + orient_offset;
	orientations = shape->piece_types[cycle->piece_type].orientations;
	last_perm = perm[cycle->slots[cycle->length - 1]];
	last_orient = orient[cycle->slots[cycle->length - 1]];
	i = (int)cycle->length - 1;
	while (i > 0)
	{
		perm[cycle->slots[i]] = perm[cycle->slots[i - 1]];
		orient[cycle->slots[i]] = orient[cycle->slots[i - 1]];
		i--;
	}
	perm[cycle->slots[0]] = last_perm;
	orient[cycle->slots[0]] = last_orient;
	i = 0;
	while (i < (int)cycle->length)
	{
		orient[cycle->slots[i]] = (uint8_t)(((int)orient[cycle->slots[i]]
				+ cycle->delta[i] + (int)orientations) % (int)orientations);
		i++;
	}
}

void	shape_apply_move(const t_shape *shape, const t_generator_table *gens,
	uint8_t *state, t_move move)
{
	const t_generator	*gen;
	uint32_t			cycle;
	int8_t				turn;

	gen = &gens->generators[move.generator];
	turn = 0;
	while (turn < move.amount)
	{
		cycle = 0;
		while (cycle < gen->cycle_count)
		{
			shape_apply_cycle(shape, state, &gen->cycles[cycle]);
			cycle++;
		}
		turn++;
	}
}
