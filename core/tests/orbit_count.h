#ifndef ORBIT_COUNT_H
# define ORBIT_COUNT_H

# include <stdint.h>
# include <stdlib.h>
# include <string.h>
# include "shape.h"
# include "move.h"

/// Test helper: how many distinct states a set of generators reaches
/// from solved. This is the test oracle docs/en/09-phase3-skewb-
/// pyraminx.md D2 asks for: a puzzle's published group order is a
/// number a wrong move model almost never lands on, which is exactly
/// what the invariant tests missed in the two NxN bugs of 2026-10-04.

typedef struct s_orbit_spec
{
	const t_shape				*shape;
	const t_generator_table		*gens;
	const uint32_t				*gen_ids;
	uint32_t					gen_count;
	uint32_t					table_bits;
}	t_orbit_spec;

/// Applies `amount` quarter-steps of generator `gen` to a raw state.
static inline void	orbit_apply(const t_shape *shape,
						const t_generator_table *gens, uint8_t *state,
						uint32_t gen, uint32_t amount)
{
	uint32_t	a;
	uint32_t	c;

	a = 0;
	while (a < amount)
	{
		c = 0;
		while (c < gens->generators[gen].cycle_count)
		{
			shape_apply_cycle(shape, state, &gens->generators[gen].cycles[c]);
			c++;
		}
		a++;
	}
}

/// Mixed-radix packing of a whole state into 64 bits: injective, so
/// "same key" means "same state" with no hash-collision caveat. Fits
/// for every puzzle in this project's tests (checked by the caller's
/// radix product being < 2^64).
static inline uint64_t	orbit_key(const t_shape *shape, const uint8_t *state)
{
	uint64_t	key;
	uint32_t	t;
	uint32_t	s;
	uint32_t	perm_off;
	uint32_t	orient_off;

	key = 0;
	t = 0;
	while (t < shape->piece_type_count)
	{
		shape_piece_offsets(shape, t, &perm_off, &orient_off);
		s = 0;
		while (s < shape->piece_types[t].count)
		{
			key = key * shape->piece_types[t].count + state[perm_off + s];
			key = key * shape->piece_types[t].orientations
				+ state[orient_off + s];
			s++;
		}
		t++;
	}
	return (key);
}

/// Open-addressing set of keys, stored as key + 1 so 0 means empty.
/// Returns 1 if `key` was new.
static inline int	orbit_insert(uint64_t *table, uint32_t bits, uint64_t key)
{
	uint64_t	mask;
	uint64_t	h;

	mask = ((uint64_t)1 << bits) - 1;
	h = (key * 0x9E3779B97F4A7C15ull) >> (64 - bits);
	while (table[h] != 0)
	{
		if (table[h] == key + 1)
			return (0);
		h = (h + 1) & mask;
	}
	table[h] = key + 1;
	return (1);
}

/// Breadth-first search level by level; returns the number of distinct
/// states reached, solved included. Frontiers hold raw states.
static inline uint64_t	orbit_count(const t_orbit_spec *spec)
{
	uint32_t	size;
	uint64_t	*table;
	uint8_t		*cur;
	uint8_t		*next;
	uint64_t	cur_n;
	uint64_t	next_n;
	uint64_t	total;
	uint64_t	i;
	uint32_t	g;
	uint32_t	amount;
	uint8_t		*tmp;
	uint64_t	cap;

	size = shape_state_size(spec->shape);
	table = calloc((size_t)1 << spec->table_bits, sizeof(uint64_t));
	cap = (uint64_t)1 << (spec->table_bits - 1);
	cur = malloc(cap * size);
	next = malloc(cap * size);
	shape_init_solved(spec->shape, cur);
	cur_n = 1;
	total = 1;
	orbit_insert(table, spec->table_bits, orbit_key(spec->shape, cur));
	while (cur_n > 0)
	{
		next_n = 0;
		i = 0;
		while (i < cur_n)
		{
			g = 0;
			while (g < spec->gen_count)
			{
				amount = 1;
				while (amount <= spec->gens->steps)
				{
					memcpy(&next[next_n * size], &cur[i * size], size);
					orbit_apply(spec->shape, spec->gens, &next[next_n * size],
						spec->gen_ids[g], amount);
					if (orbit_insert(table, spec->table_bits,
							orbit_key(spec->shape, &next[next_n * size])))
					{
						next_n++;
						total++;
					}
					amount++;
				}
				g++;
			}
			i++;
		}
		tmp = cur;
		cur = next;
		next = tmp;
		cur_n = next_n;
	}
	free(table);
	free(cur);
	free(next);
	return (total);
}

#endif
