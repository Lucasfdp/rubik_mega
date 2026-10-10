#include "platform.h"
#include "solve/stage_table.h"
#include "solve/bytes.h"

/// Marks an empty hash slot (a depth byte no real depth reaches).
# define STAGE_EMPTY 0xFFu

/// Stands in for every untracked piece in the scratch state a move is
/// applied to: shape_init_solved() would put real piece indices there,
/// and a tracked piece's index must be the only match when its slot is
/// looked up afterwards. No index reaches 255 (shape_is_valid()).
# define STAGE_FILLER 0xFFu

/// A key no state packs to: eight bytes of 0xFF would need a piece byte
/// of 255, but a byte is slot * orientations + orientation, well below.
# define STAGE_BAD_KEY UINT64_MAX

/// A packed piece is one byte of the key.
# define STAGE_PIECE_BITS 8
# define STAGE_PIECE_MASK 0xFFu

/// The search stops here if it somehow never runs out of new states. A
/// depth must stay below STAGE_EMPTY to fit its byte.
# define STAGE_MAX_LEVEL 250

/// Bits in the key.
# define STAGE_KEY_BITS 64

/// Fibonacci hashing: the 64-bit golden-ratio constant, 2^64 / phi,
/// spreads packed keys (whose low bytes are small) across the table
/// when the TOP bits are taken.
# define STAGE_HASH_MULTIPLIER 0x9E3779B97F4A7C15ull

/// Hash capacity is at least max_states * 4 / 3 -- a load factor of at
/// most 3/4, so linear probing stays short without wasting a table
/// twice the size.
# define STAGE_LOAD_NUMERATOR 4
# define STAGE_LOAD_DENOMINATOR 3

/// The top `64 - hash_shift` bits of the mixed key: the table index.
static uint32_t	slot_hash(const t_stage_table *t, uint64_t key)
{
	return ((uint32_t)((key * STAGE_HASH_MULTIPLIER) >> t->hash_shift));
}

/// The slot holding `key`, or the empty slot where it belongs if absent.
static uint32_t	find_slot(const t_stage_table *t, uint64_t key)
{
	uint32_t	slot;

	slot = slot_hash(t, key);
	while (t->depths[slot] != STAGE_EMPTY && t->keys[slot] != key)
		slot = (slot + 1) & (t->capacity - 1);
	return (slot);
}

/// One tracked piece's byte in a key: where it is and how it is turned.
static uint64_t	piece_byte(const t_shape *shape, const t_stage_piece *piece,
	const uint8_t *state)
{
	uint32_t	perm_offset;
	uint32_t	orient_offset;
	uint32_t	slot;
	uint32_t	orientations;

	shape_piece_offsets(shape, piece->piece_type, &perm_offset,
		&orient_offset);
	slot = 0;
	while (slot < shape->piece_types[piece->piece_type].count
		&& state[perm_offset + slot] != piece->piece)
		slot++;
	if (slot == shape->piece_types[piece->piece_type].count)
		return (STAGE_BAD_KEY);
	orientations = shape->piece_types[piece->piece_type].orientations;
	return (slot * orientations + state[orient_offset + slot]);
}

/// Packs where the tracked pieces of `state` are into one integer, a
/// byte per piece, or STAGE_BAD_KEY if a tracked piece is missing from
/// its type's permutation block (a malformed state).
static uint64_t	encode_state(const t_stage_table *t, const uint8_t *state)
{
	uint64_t	key;
	uint64_t	byte;
	uint32_t	i;

	key = 0;
	i = 0;
	while (i < t->tracked_count)
	{
		byte = piece_byte(t->shape, &t->tracked[i], state);
		if (byte == STAGE_BAD_KEY)
			return (STAGE_BAD_KEY);
		key |= byte << (STAGE_PIECE_BITS * i);
		i++;
	}
	return (key);
}

/// Builds a scratch state in which the tracked pieces are where `key`
/// says and everything else is filler. A move only shuffles slots and
/// adds slot-dependent twists, so it moves a tracked piece exactly as it
/// would in the real puzzle whatever the other pieces are.
static void	decode_state(const t_stage_table *t, uint64_t key, uint8_t *state)
{
	uint32_t	perm_offset;
	uint32_t	orient_offset;
	uint32_t	type;
	uint32_t	slot;
	uint32_t	orientations;
	uint32_t	i;

	type = 0;
	while (type < t->shape->piece_type_count)
	{
		shape_piece_offsets(t->shape, type, &perm_offset, &orient_offset);
		slot = 0;
		while (slot < t->shape->piece_types[type].count)
		{
			state[perm_offset + slot] = STAGE_FILLER;
			state[orient_offset + slot++] = 0;
		}
		type++;
	}
	i = 0;
	while (i < t->tracked_count)
	{
		type = t->tracked[i].piece_type;
		orientations = t->shape->piece_types[type].orientations;
		shape_piece_offsets(t->shape, type, &perm_offset, &orient_offset);
		slot = (uint32_t)((key >> (STAGE_PIECE_BITS * i)) & STAGE_PIECE_MASK);
		state[perm_offset + slot / orientations] = t->tracked[i].piece;
		state[orient_offset + slot / orientations] = (uint8_t)(slot
				% orientations);
		i++;
	}
}

/// The key reached from `key` by move `m`.
static uint64_t	step_key(const t_stage_table *t, uint64_t key, uint32_t m)
{
	uint8_t	state[STAGE_MAX_STATE];

	decode_state(t, key, state);
	shape_apply_move(t->shape, t->gens, state, t->moves[m]);
	return (encode_state(t, state));
}

/// The BFS root: the key of the solved puzzle.
static uint64_t	goal_key(const t_stage_table *t)
{
	uint8_t	state[STAGE_MAX_STATE];

	shape_init_solved(t->shape, state);
	return (encode_state(t, state));
}

/// Marks every unvisited neighbour of every state at depth `level` as
/// depth level + 1. Scanning the whole table once per level instead of
/// keeping a queue costs a dozen cheap passes over a table of at most
/// ~100k slots, and needs no memory beyond the table itself.
/// @return How many states it newly reached, or -1 if that would
///         exceed `max_states` in total.
static int	expand_level(t_stage_table *t, uint8_t level, uint32_t max_states)
{
	uint32_t	slot;
	uint32_t	next;
	uint32_t	m;
	uint64_t	key;
	int			added;

	added = 0;
	slot = 0;
	while (slot < t->capacity)
	{
		if (t->depths[slot++] != level)
			continue ;
		m = 0;
		while (m < t->move_count)
		{
			key = step_key(t, t->keys[slot - 1], m++);
			next = find_slot(t, key);
			if (t->depths[next] != STAGE_EMPTY)
				continue ;
			if (t->state_count == max_states)
				return (-1);
			t->keys[next] = key;
			t->depths[next] = (uint8_t)(level + 1);
			t->state_count++;
			added++;
		}
	}
	return (added);
}

static int	build_depths(t_stage_table *t, uint32_t max_states)
{
	uint32_t	slot;
	uint8_t		level;
	int			added;

	t->capacity = 1;
	t->hash_shift = STAGE_KEY_BITS;
	while (t->capacity * STAGE_LOAD_DENOMINATOR
		< max_states * STAGE_LOAD_NUMERATOR)
	{
		t->capacity *= 2;
		t->hash_shift--;
	}
	t->keys = malloc(t->capacity * sizeof(uint64_t));
	t->depths = malloc(t->capacity);
	if (!t->keys || !t->depths)
		return (0);
	slot = 0;
	while (slot < t->capacity)
		t->depths[slot++] = STAGE_EMPTY;
	slot = find_slot(t, goal_key(t));
	t->keys[slot] = goal_key(t);
	t->depths[slot] = 0;
	t->state_count = 1;
	t->max_depth = 0;
	level = 0;
	added = expand_level(t, level, max_states);
	while (added > 0 && level < STAGE_MAX_LEVEL)
	{
		t->max_depth = (uint32_t)level + 1;
		level++;
		added = expand_level(t, level, max_states);
	}
	return (added == 0);
}

int	stage_table_init(t_stage_table *table, const t_shape *shape,
	const t_generator_table *gens, const t_stage_piece *tracked,
	uint32_t tracked_count, const t_move *moves, uint32_t move_count,
	uint32_t max_states)
{
	if (tracked_count > STAGE_MAX_TRACKED || move_count > STAGE_MAX_MOVES
		|| shape_state_size(shape) > STAGE_MAX_STATE || max_states == 0)
		return (0);
	table->shape = shape;
	table->gens = gens;
	table->tracked_count = tracked_count;
	table->move_count = move_count;
	solve_copy_bytes((uint8_t *)table->tracked, (const uint8_t *)tracked,
		tracked_count * (uint32_t)sizeof(t_stage_piece));
	solve_copy_bytes((uint8_t *)table->moves, (const uint8_t *)moves,
		move_count * (uint32_t)sizeof(t_move));
	return (build_depths(table, max_states));
}

/// The depth stored for `key`, or -1 if the table does not hold it.
static int	depth_of(const t_stage_table *t, uint64_t key)
{
	uint32_t	slot;

	if (key == STAGE_BAD_KEY)
		return (-1);
	slot = find_slot(t, key);
	if (t->depths[slot] == STAGE_EMPTY)
		return (-1);
	return (t->depths[slot]);
}

int	stage_table_distance(const t_stage_table *table, const uint8_t *state)
{
	return (depth_of(table, encode_state(table, state)));
}

int	stage_table_descend(const t_stage_table *table, uint8_t *state,
	t_move *out, uint32_t max_moves)
{
	uint64_t	key;
	uint64_t	next;
	int			distance;
	uint32_t	count;
	uint32_t	m;

	key = encode_state(table, state);
	distance = depth_of(table, key);
	if (distance < 0 || (uint32_t)distance > max_moves)
		return (-1);
	count = 0;
	while (distance > 0)
	{
		m = 0;
		while (m < table->move_count
			&& depth_of(table, step_key(table, key, m)) != distance - 1)
			m++;
		if (m == table->move_count)
			return (-1);
		next = step_key(table, key, m);
		shape_apply_move(table->shape, table->gens, state, table->moves[m]);
		out[count++] = table->moves[m];
		key = next;
		distance--;
	}
	return ((int)count);
}
