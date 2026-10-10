#include <stddef.h>
#include "puzzles/square1.h"
#include "prng.h"

/// Square-1's state model and moves (docs/en/12-phase5-square1.md D1-D4).
/// The geometry every index below means is in puzzles/square1.h.

# define SQ1_TOP_BASE 0
# define SQ1_BOTTOM_BASE SQ1_LAYER_UNITS

/// Where the cut falls: between unit 11 and unit 0, and between unit 5 and
/// unit 6 of a layer. The slice is legal when a piece boundary lies at both.
# define SQ1_CUT_A_BEFORE 0
# define SQ1_CUT_A_AFTER 11
# define SQ1_CUT_B_BEFORE 5
# define SQ1_CUT_B_AFTER 6

/// The solved layer is [corner, corner, edge] repeated four times, so the
/// unit at index i is the (i % 3)'th of its group.
# define SQ1_GROUP_UNITS 3
# define SQ1_GROUP_COUNT 4
# define SQ1_GROUP_EDGE_POSITION 2

/// A piece's colours. Top corners run clockwise from above starting at the
/// back-right: BR, RF, FL, LB, with the first-met side first. Bottom pieces
/// sit under the same four positions; their sides are listed in the order
/// seen from outside their (downward) cap, which is the reverse of the
/// order seen from above (square1.h, t_sq1_piece).
static const t_sq1_piece	PIECES[SQ1_PIECE_COUNT] = {
	{SQ1_FACE_U, SQ1_FACE_B, SQ1_FACE_R},
	{SQ1_FACE_U, SQ1_FACE_R, SQ1_FACE_F},
	{SQ1_FACE_U, SQ1_FACE_F, SQ1_FACE_L},
	{SQ1_FACE_U, SQ1_FACE_L, SQ1_FACE_B},
	{SQ1_FACE_D, SQ1_FACE_R, SQ1_FACE_B},
	{SQ1_FACE_D, SQ1_FACE_F, SQ1_FACE_R},
	{SQ1_FACE_D, SQ1_FACE_L, SQ1_FACE_F},
	{SQ1_FACE_D, SQ1_FACE_B, SQ1_FACE_L},
	{SQ1_FACE_U, SQ1_FACE_R, 0},
	{SQ1_FACE_U, SQ1_FACE_F, 0},
	{SQ1_FACE_U, SQ1_FACE_L, 0},
	{SQ1_FACE_U, SQ1_FACE_B, 0},
	{SQ1_FACE_D, SQ1_FACE_R, 0},
	{SQ1_FACE_D, SQ1_FACE_F, 0},
	{SQ1_FACE_D, SQ1_FACE_L, 0},
	{SQ1_FACE_D, SQ1_FACE_B, 0},
};

const t_sq1_piece	*sq1_piece(uint32_t id)
{
	if (id >= SQ1_PIECE_COUNT)
		return (NULL);
	return (&PIECES[id]);
}

/// Fills one layer solved: [corner, corner, edge] x 4, corners numbered
/// from `first_corner` and edges from `first_edge`.
static void	init_layer(uint8_t *layer, uint8_t first_corner,
	uint8_t first_edge)
{
	uint32_t	i;

	i = 0;
	while (i < SQ1_LAYER_UNITS)
	{
		if (i % SQ1_GROUP_UNITS == SQ1_GROUP_EDGE_POSITION)
			layer[i] = (uint8_t)(first_edge + i / SQ1_GROUP_UNITS);
		else
			layer[i] = (uint8_t)(first_corner + i / SQ1_GROUP_UNITS);
		i++;
	}
}

void	sq1_init_solved(uint8_t *state)
{
	init_layer(state + SQ1_TOP_BASE, 0, SQ1_FIRST_EDGE_ID);
	init_layer(state + SQ1_BOTTOM_BASE, SQ1_CORNERS_PER_LAYER,
		SQ1_FIRST_EDGE_ID + SQ1_EDGES_PER_LAYER);
	state[SQ1_MIDDLE_INDEX] = SQ1_MIDDLE_SOLVED;
}

/// The unit after `unit` going clockwise seen from above, in its layer.
static uint32_t	next_unit(uint32_t unit)
{
	uint32_t	base;

	base = (unit < SQ1_LAYER_UNITS) ? SQ1_TOP_BASE : SQ1_BOTTOM_BASE;
	return (base + (unit - base + 1) % SQ1_LAYER_UNITS);
}

int	sq1_state_is_valid(const uint8_t *state)
{
	uint32_t	count[SQ1_PIECE_COUNT];
	uint32_t	u;
	uint32_t	id;
	uint32_t	base;

	if (state[SQ1_MIDDLE_INDEX] > SQ1_MIDDLE_FLIPPED)
		return (0);
	id = 0;
	while (id < SQ1_PIECE_COUNT)
		count[id++] = 0;
	u = 0;
	while (u < SQ1_UNIT_COUNT)
	{
		if (state[u] >= SQ1_PIECE_COUNT)
			return (0);
		count[state[u]]++;
		u++;
	}
	id = 0;
	while (id < SQ1_PIECE_COUNT)
	{
		if (count[id] != (id < SQ1_FIRST_EDGE_ID ? 2u : 1u))
			return (0);
		id++;
	}
	/// A corner's two units must be neighbours in one layer. (Each id
	/// appears twice, so it is enough that the unit after it holds the
	/// same id for one of the two.)
	u = 0;
	while (u < SQ1_UNIT_COUNT)
	{
		base = (u < SQ1_LAYER_UNITS) ? SQ1_TOP_BASE : SQ1_BOTTOM_BASE;
		if (state[u] < SQ1_FIRST_EDGE_ID && state[next_unit(u)] != state[u]
			&& state[base + (u - base + SQ1_LAYER_UNITS - 1)
				% SQ1_LAYER_UNITS] != state[u])
			return (0);
		u++;
	}
	return (1);
}

int	sq1_move_in_range(int16_t generator, int8_t amount)
{
	if (generator == SQ1_GEN_TOP || generator == SQ1_GEN_BOTTOM)
		return (amount >= 1 && amount <= SQ1_LAYER_STEPS);
	if (generator == SQ1_GEN_SLICE)
		return (amount >= 1 && amount <= SQ1_SLICE_STEPS);
	return (0);
}

/// Whether a layer, as it now stands, has a piece boundary at both ends of
/// the cut. A corner is the only piece two units wide, so "the same id on
/// both sides" is exactly "a corner straddles".
static int	layer_is_clear(const uint8_t *layer)
{
	return (layer[SQ1_CUT_A_BEFORE] != layer[SQ1_CUT_A_AFTER]
		&& layer[SQ1_CUT_B_BEFORE] != layer[SQ1_CUT_B_AFTER]);
}

int	sq1_slice_is_legal(const uint8_t *state)
{
	return (layer_is_clear(state + SQ1_TOP_BASE)
		&& layer_is_clear(state + SQ1_BOTTOM_BASE));
}

int	sq1_move_is_legal(const uint8_t *state, int16_t generator, int8_t amount)
{
	if (!sq1_move_in_range(generator, amount))
		return (0);
	if (generator == SQ1_GEN_SLICE)
		return (sq1_slice_is_legal(state));
	return (1);
}

/// Moves the content of unit i to unit i + shift (clockwise seen from
/// above), wrapping round the layer.
static void	rotate_layer(uint8_t *layer, uint32_t shift)
{
	uint8_t		old[SQ1_LAYER_UNITS];
	uint32_t	i;

	i = 0;
	while (i < SQ1_LAYER_UNITS)
	{
		old[i] = layer[i];
		i++;
	}
	i = 0;
	while (i < SQ1_LAYER_UNITS)
	{
		layer[(i + shift) % SQ1_LAYER_UNITS] = old[i];
		i++;
	}
}

void	sq1_turn_top(uint8_t *state, uint32_t amount)
{
	rotate_layer(state + SQ1_TOP_BASE, amount % SQ1_LAYER_UNITS);
}

void	sq1_turn_bottom(uint8_t *state, uint32_t amount)
{
	rotate_layer(state + SQ1_BOTTOM_BASE,
		(SQ1_LAYER_UNITS - amount % SQ1_LAYER_UNITS) % SQ1_LAYER_UNITS);
}

int	sq1_slice(uint8_t *state)
{
	uint8_t		held;
	uint32_t	i;

	if (!sq1_slice_is_legal(state))
		return (0);
	i = 0;
	while (i < SQ1_HALF_UNITS)
	{
		held = state[SQ1_TOP_BASE + i];
		state[SQ1_TOP_BASE + i]
			= state[SQ1_BOTTOM_BASE + SQ1_HALF_UNITS - 1 - i];
		state[SQ1_BOTTOM_BASE + SQ1_HALF_UNITS - 1 - i] = held;
		i++;
	}
	state[SQ1_MIDDLE_INDEX] ^= 1;
	return (1);
}

int	sq1_apply_move(uint8_t *state, int16_t generator, int8_t amount)
{
	if (!sq1_move_is_legal(state, generator, amount))
		return (0);
	if (generator == SQ1_GEN_TOP)
		sq1_turn_top(state, (uint32_t)amount);
	else if (generator == SQ1_GEN_BOTTOM)
		sq1_turn_bottom(state, (uint32_t)amount);
	else
		sq1_slice(state);
	return (1);
}

/// Lists the turns (0..11, clockwise from above) after which `layer` is
/// clear of the cut, into `turns`; returns how many. Never empty for a
/// layer that came out of a legal slice or from solved, which is the only
/// way a layer is ever built here.
static uint32_t	clear_turns(const uint8_t *layer, uint32_t *turns)
{
	uint8_t		copy[SQ1_LAYER_UNITS];
	uint32_t	turn;
	uint32_t	count;
	uint32_t	i;

	count = 0;
	turn = 0;
	while (turn < SQ1_LAYER_UNITS)
	{
		i = 0;
		while (i < SQ1_LAYER_UNITS)
		{
			copy[i] = layer[i];
			i++;
		}
		rotate_layer(copy, turn);
		if (layer_is_clear(copy))
			turns[count++] = turn;
		turn++;
	}
	return (count);
}

/// Appends one move to the scramble's list. A turn of 0 units does
/// nothing and is not a move. `out` may be NULL (just applying), and a
/// list that outgrows `max` keeps counting so the caller learns the
/// length it would have needed.
static void	record_move(t_move *out, uint32_t max, uint32_t *count,
				int16_t generator, uint32_t amount)
{
	if (amount == 0)
		return ;
	if (out && *count < max)
	{
		out[*count].generator = generator;
		out[*count].amount = (int8_t)amount;
	}
	(*count)++;
}

/// The one scramble loop: applies every move to `state` as it picks it,
/// recording it if `out` is given. sq1_scramble() and sq1_scramble_moves()
/// both run exactly this, so the moves a caller animates are by
/// construction the moves the instant scramble applies.
static uint32_t	scramble_run(uint8_t *state, uint32_t move_count,
					uint32_t seed, t_move *out, uint32_t max)
{
	uint32_t	rng;
	uint32_t	top_turns[SQ1_LAYER_UNITS];
	uint32_t	bottom_turns[SQ1_LAYER_UNITS];
	uint32_t	top_count;
	uint32_t	bottom_count;
	uint32_t	amount;
	uint32_t	count;
	uint32_t	i;

	rng = prng_seed(seed);
	count = 0;
	i = 0;
	while (i < move_count)
	{
		top_count = clear_turns(state + SQ1_TOP_BASE, top_turns);
		bottom_count = clear_turns(state + SQ1_BOTTOM_BASE, bottom_turns);
		if (top_count == 0 || bottom_count == 0)
			return (count);
		amount = top_turns[prng_next(&rng) % top_count];
		sq1_turn_top(state, amount);
		record_move(out, max, &count, SQ1_GEN_TOP, amount);
		/// The layer's turns were listed clockwise from above, so a bottom
		/// turn of `t` from above is a generator amount of 12 - t.
		amount = (SQ1_LAYER_UNITS
				- bottom_turns[prng_next(&rng) % bottom_count])
			% SQ1_LAYER_UNITS;
		sq1_turn_bottom(state, amount);
		record_move(out, max, &count, SQ1_GEN_BOTTOM, amount);
		sq1_slice(state);
		record_move(out, max, &count, SQ1_GEN_SLICE, 1);
		i++;
	}
	return (count);
}

void	sq1_scramble(uint8_t *state, uint32_t move_count, uint32_t seed)
{
	scramble_run(state, move_count, seed, NULL, 0);
}

uint32_t	sq1_scramble_moves(const uint8_t *state, uint32_t move_count,
				uint32_t seed, t_move *out, uint32_t max)
{
	uint8_t	scratch[SQ1_STATE_SIZE];
	uint32_t	i;

	i = 0;
	while (i < SQ1_STATE_SIZE)
	{
		scratch[i] = state[i];
		i++;
	}
	return (scramble_run(scratch, move_count, seed, out, max));
}

int	sq1_is_solved(const uint8_t *state)
{
	uint8_t		solved[SQ1_STATE_SIZE];
	uint32_t	i;

	sq1_init_solved(solved);
	i = 0;
	while (i < SQ1_STATE_SIZE)
	{
		if (state[i] != solved[i])
			return (0);
		i++;
	}
	return (1);
}

void	sq1_units(const uint8_t *state, uint8_t *out)
{
	uint32_t	u;

	u = 0;
	while (u < SQ1_UNIT_COUNT)
	{
		if (state[u] >= SQ1_FIRST_EDGE_ID)
			out[u] = SQ1_UNIT_EDGE;
		else if (state[next_unit(u)] == state[u])
			out[u] = SQ1_UNIT_CORNER_FIRST;
		else
			out[u] = SQ1_UNIT_CORNER_SECOND;
		u++;
	}
}

/// The colour of the side sticker on unit `u`. A corner's two sides go to
/// its two units in the order seen from outside its cap; from above that is
/// the order as stored in the top layer and reversed in the bottom one.
static uint8_t	side_colour(const uint8_t *state, uint32_t u)
{
	const t_sq1_piece	*piece;
	int					first;
	int					in_top;

	piece = &PIECES[state[u]];
	if (state[u] >= SQ1_FIRST_EDGE_ID)
		return (piece->side_a);
	first = (state[next_unit(u)] == state[u]);
	in_top = (u < SQ1_LAYER_UNITS);
	return ((first == in_top) ? piece->side_a : piece->side_b);
}

void	sq1_facelets(const uint8_t *state, uint8_t *out)
{
	uint32_t	u;
	uint8_t		*mid;
	int			flipped;

	u = 0;
	while (u < SQ1_UNIT_COUNT)
	{
		out[SQ1_CAP_FACELETS + u] = PIECES[state[u]].cap;
		out[SQ1_SIDE_FACELETS + u] = side_colour(state, u);
		u++;
	}
	/// The left half is the frame and never moves. Flipping the right half
	/// swaps its front and back stickers; its side sticker stays (upside
	/// down).
	flipped = (state[SQ1_MIDDLE_INDEX] == SQ1_MIDDLE_FLIPPED);
	mid = out + SQ1_MIDDLE_FACELETS;
	mid[0] = SQ1_FACE_F;
	mid[1] = SQ1_FACE_L;
	mid[2] = SQ1_FACE_B;
	mid[3] = flipped ? SQ1_FACE_B : SQ1_FACE_F;
	mid[4] = SQ1_FACE_R;
	mid[5] = flipped ? SQ1_FACE_F : SQ1_FACE_B;
}
