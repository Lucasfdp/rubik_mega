#include "platform.h"
#include "solve/coord_table.h"
#include "solve/bytes.h"

/// Marks a product point the search has not reached (depth).
# define COORD_UNVISITED 0xFFu

/// The search stops here if it somehow never runs out of new points; a
/// depth must stay below COORD_UNVISITED to fit its byte. Real tables
/// end at 11.
# define COORD_MAX_LEVEL 250

/// Every value of the block being enumerated, flat (`length` bytes
/// each), in discovery order. File-level because the enumeration is
/// one-shot and sized from COORD_MAX_REACHABLE; keeping it off the stack
/// matters in the WASM build, whose stack is small. Copied into an
/// exact-size permanent array once the block's size is known.
static uint8_t	g_values[COORD_MAX_REACHABLE * COORD_MAX_LENGTH];

/// FNV-1a over a block's bytes, folded to a bucket index.
static uint32_t	hash_of(const uint8_t *value, uint32_t length)
{
	uint32_t	hash;
	uint32_t	i;

	hash = 2166136261u;
	i = 0;
	while (i < length)
	{
		hash = (hash ^ value[i++]) * 16777619u;
	}
	return (hash & (COORD_HASH_SIZE - 1));
}

static int	same_bytes(const uint8_t *a, const uint8_t *b, uint32_t length)
{
	uint32_t	i;

	i = 0;
	while (i < length && a[i] == b[i])
		i++;
	return (i == length);
}

/// The compact id of `value` in block `c`, or -1 if it is not a value
/// the block can reach. Linear probing: an empty bucket ends the chain.
static int	id_of(const t_coord_component *c, const uint8_t *value)
{
	uint32_t	slot;
	uint32_t	entry;

	slot = hash_of(value, c->length);
	entry = c->buckets[slot];
	while (entry)
	{
		if (same_bytes(&c->values[(entry - 1) * c->length], value, c->length))
			return ((int)entry - 1);
		slot = (slot + 1) & (COORD_HASH_SIZE - 1);
		entry = c->buckets[slot];
	}
	return (-1);
}

/// Files value number `id` (already stored in c->values) in the hash
/// table.
static void	insert_id(t_coord_component *c, uint32_t id)
{
	uint32_t	slot;

	slot = hash_of(&c->values[id * c->length], c->length);
	while (c->buckets[slot])
		slot = (slot + 1) & (COORD_HASH_SIZE - 1);
	c->buckets[slot] = (uint16_t)(id + 1);
}

/// Applies move `m` to block `c` holding `value`, writing the result to
/// `result`. Done on a scratch solved state: the other blocks are
/// irrelevant because a block is autonomous (see the header), so leaving
/// them solved is as good as any other choice.
static void	step_value(const t_coord_table *t, const t_coord_component *c,
	const uint8_t *value, uint32_t m, uint8_t *result)
{
	uint8_t	state[COORD_MAX_STATE];

	shape_init_solved(t->shape, state);
	solve_copy_bytes(state + c->offset, value, c->length);
	shape_apply_move(t->shape, t->gens, state, t->moves[m]);
	solve_copy_bytes(result, state + c->offset, c->length);
}

/// Sets up block `c` as piece type `piece_type`'s permutation and
/// orientation bytes, which shape.h lays out back to back.
static int	setup_component(t_coord_component *c, const t_shape *shape,
	uint32_t piece_type)
{
	uint32_t	perm_offset;
	uint32_t	orient_offset;
	uint32_t	i;

	shape_piece_offsets(shape, piece_type, &perm_offset, &orient_offset);
	c->offset = perm_offset;
	c->length = 2 * shape->piece_types[piece_type].count;
	if (c->length > COORD_MAX_LENGTH)
		return (0);
	c->buckets = malloc(COORD_HASH_SIZE * sizeof(uint16_t));
	if (!c->buckets)
		return (0);
	i = 0;
	while (i < COORD_HASH_SIZE)
		c->buckets[i++] = 0;
	c->values = g_values;
	return (1);
}

/// Finds every value block `c` can take under the table's moves, by
/// breadth-first search from the solved value. The search queue is
/// c->values itself: ids are discovery order.
static int	enumerate_values(const t_coord_table *t, t_coord_component *c)
{
	uint8_t		state[COORD_MAX_STATE];
	uint8_t		next[COORD_MAX_LENGTH];
	uint32_t	found;
	uint32_t	i;
	uint32_t	m;

	shape_init_solved(t->shape, state);
	solve_copy_bytes(c->values, state + c->offset, c->length);
	insert_id(c, 0);
	found = 1;
	i = 0;
	while (i < found)
	{
		m = 0;
		while (m < t->move_count)
		{
			step_value(t, c, &c->values[i * c->length], m++, next);
			if (id_of(c, next) >= 0)
				continue ;
			if (found == COORD_MAX_REACHABLE)
				return (0);
			solve_copy_bytes(&c->values[found * c->length], next, c->length);
			insert_id(c, found++);
		}
		i++;
	}
	c->size = found;
	return (1);
}

/// Replaces block `c`'s scratch values with an exact-size permanent copy.
static int	keep_values(t_coord_component *c)
{
	uint8_t	*kept;

	kept = malloc(c->size * c->length);
	if (!kept)
		return (0);
	solve_copy_bytes(kept, c->values, c->size * c->length);
	c->values = kept;
	return (1);
}

/// Fills block `c`'s compact move table.
static int	build_moves(const t_coord_table *t, t_coord_component *c)
{
	uint8_t		next[COORD_MAX_LENGTH];
	uint32_t	i;
	uint32_t	m;

	c->next = malloc(c->size * t->move_count * sizeof(uint16_t));
	if (!c->next)
		return (0);
	i = 0;
	while (i < c->size)
	{
		m = 0;
		while (m < t->move_count)
		{
			step_value(t, c, &c->values[i * c->length], m, next);
			c->next[i * t->move_count + m] = (uint16_t)id_of(c, next);
			m++;
		}
		i++;
	}
	return (1);
}

/// The product-space index of the point with coordinate ids `ids`.
static uint32_t	index_of(const t_coord_table *t, const uint32_t *ids)
{
	uint32_t	index;
	uint32_t	k;

	index = 0;
	k = 0;
	while (k < t->component_count)
	{
		index += ids[k] * t->stride[k];
		k++;
	}
	return (index);
}

static void	decode_index(const t_coord_table *t, uint32_t index,
	uint32_t *ids)
{
	uint32_t	k;

	k = 0;
	while (k < t->component_count)
	{
		ids[k] = (index / t->stride[k]) % t->components[k].size;
		k++;
	}
}

/// The index reached from the point `ids` by move `m`.
static uint32_t	neighbour(const t_coord_table *t, const uint32_t *ids,
	uint32_t m)
{
	uint32_t	index;
	uint32_t	k;

	index = 0;
	k = 0;
	while (k < t->component_count)
	{
		index += t->components[k].next[ids[k] * t->move_count + m]
			* t->stride[k];
		k++;
	}
	return (index);
}

/// Marks every unvisited neighbour of every point at depth `level` as
/// depth level + 1. Scanning the whole table once per level instead of
/// keeping a queue costs ~11 cheap passes over a few MB, and saves a
/// 12 MB queue the 16 MiB arena cannot afford (nothing is ever freed).
/// @return How many points it newly reached.
static uint32_t	expand_level(t_coord_table *t, uint8_t level)
{
	uint32_t	ids[COORD_MAX_COMPONENTS];
	uint32_t	index;
	uint32_t	m;
	uint32_t	next;
	uint32_t	added;

	added = 0;
	index = 0;
	while (index < t->state_count)
	{
		if (t->depth[index++] != level)
			continue ;
		decode_index(t, index - 1, ids);
		m = 0;
		while (m < t->move_count)
		{
			next = neighbour(t, ids, m++);
			if (t->depth[next] != COORD_UNVISITED)
				continue ;
			t->depth[next] = (uint8_t)(level + 1);
			added++;
		}
	}
	return (added);
}

static int	build_depth(t_coord_table *t)
{
	uint32_t	added;
	uint8_t		level;
	uint32_t	i;

	t->depth = malloc(t->state_count);
	if (!t->depth)
		return (0);
	i = 0;
	while (i < t->state_count)
		t->depth[i++] = COORD_UNVISITED;
	t->depth[0] = 0;
	t->reached_count = 1;
	t->max_depth = 0;
	level = 0;
	added = expand_level(t, level);
	while (added > 0 && level < COORD_MAX_LEVEL)
	{
		t->reached_count += added;
		t->max_depth = (uint32_t)level + 1;
		level++;
		added = expand_level(t, level);
	}
	return (1);
}

int	coord_table_init(t_coord_table *table, const t_shape *shape,
	const t_generator_table *gens, const uint32_t *piece_types,
	uint32_t piece_type_count, const t_move *moves, uint32_t move_count)
{
	uint32_t	k;

	if (piece_type_count > COORD_MAX_COMPONENTS || move_count > COORD_MAX_MOVES
		|| shape_state_size(shape) > COORD_MAX_STATE)
		return (0);
	table->shape = shape;
	table->gens = gens;
	table->component_count = piece_type_count;
	table->move_count = move_count;
	solve_copy_bytes((uint8_t *)table->moves, (const uint8_t *)moves,
		move_count * (uint32_t)sizeof(t_move));
	table->state_count = 1;
	k = 0;
	while (k < piece_type_count)
	{
		if (!setup_component(&table->components[k], shape, piece_types[k])
			|| !enumerate_values(table, &table->components[k])
			|| !keep_values(&table->components[k])
			|| !build_moves(table, &table->components[k]))
			return (0);
		table->state_count *= table->components[k++].size;
	}
	while (k > 0)
	{
		k--;
		table->stride[k] = (k + 1 == piece_type_count) ? 1
			: table->stride[k + 1] * table->components[k + 1].size;
	}
	return (build_depth(table));
}

int	coord_table_encode(const t_coord_table *table, const uint8_t *state,
	uint32_t *ids)
{
	const t_coord_component	*c;
	int						id;
	uint32_t				k;

	k = 0;
	while (k < table->component_count)
	{
		c = &table->components[k];
		id = id_of(c, state + c->offset);
		if (id < 0)
			return (0);
		ids[k++] = (uint32_t)id;
	}
	return (1);
}

int	coord_table_distance(const t_coord_table *table, const uint8_t *state)
{
	uint32_t	ids[COORD_MAX_COMPONENTS];
	uint8_t		depth;

	if (!coord_table_encode(table, state, ids))
		return (-1);
	depth = table->depth[index_of(table, ids)];
	if (depth == COORD_UNVISITED)
		return (-1);
	return (depth);
}

/// Moves `ids` one step along move `m`, in place.
static void	step_ids(const t_coord_table *t, uint32_t *ids, uint32_t m)
{
	uint32_t	k;

	k = 0;
	while (k < t->component_count)
	{
		ids[k] = t->components[k].next[ids[k] * t->move_count + m];
		k++;
	}
}

int	coord_table_solve(const t_coord_table *table, const uint8_t *state,
	uint32_t *out, uint32_t max_moves)
{
	uint32_t	ids[COORD_MAX_COMPONENTS];
	int			distance;
	uint32_t	count;
	uint32_t	m;

	distance = coord_table_distance(table, state);
	if (distance < 0 || (uint32_t)distance > max_moves)
		return (-1);
	coord_table_encode(table, state, ids);
	count = 0;
	while (distance > 0)
	{
		m = 0;
		while (m < table->move_count
			&& table->depth[neighbour(table, ids, m)] != distance - 1)
			m++;
		if (m == table->move_count)
			return (-1);
		step_ids(table, ids, m);
		out[count++] = m;
		distance--;
	}
	return ((int)count);
}
