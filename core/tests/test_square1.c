#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "puzzle.h"
#include "puzzles/square1.h"
#include "puzzles/cubic.h"

/// Square-1 (docs/en/12-phase5-square1.md increments 2 and 3): state model,
/// moves, legality, scramble, facelets and units.

/// The shape graph's size from the spec's measurements: twistable
/// (top, bottom) arrangements, times the two middle states.
#define TWISTABLE_SHAPES 3678
#define SHAPE_STATE_COUNT (TWISTABLE_SHAPES * 2)
#define SCRAMBLE_COUNT 200
#define SCRAMBLE_SLICES 25

/// Colour totals of a solved puzzle: caps 12 + 12, then each side face is
/// 3 + 3 side units plus its share of the middle layer.
#define COLOUR_COUNT 6
static const uint32_t	SOLVED_COLOUR_COUNTS[COLOUR_COUNT] = {
	12, 7, 8, 12, 7, 8
};

/// A bit set of shape states: top mask (12 bits), bottom mask (12), middle.
#define SHAPE_KEY_BITS 25

static uint8_t	g_seen[(1u << SHAPE_KEY_BITS) / 8];

/// Boundary after unit i of a layer iff the next unit holds another piece.
static uint32_t	layer_mask(const uint8_t *layer)
{
	uint32_t	mask;
	uint32_t	i;

	mask = 0;
	i = 0;
	while (i < SQ1_LAYER_UNITS)
	{
		if (layer[i] != layer[(i + 1) % SQ1_LAYER_UNITS])
			mask |= 1u << i;
		i++;
	}
	return (mask);
}

static uint32_t	shape_key(const uint8_t *state)
{
	return (layer_mask(state) | layer_mask(state + SQ1_LAYER_UNITS) << 12
		| (uint32_t)state[SQ1_MIDDLE_INDEX] << 24);
}

/// Cube shape: [corner, corner, edge] repeated round the layer, in one of
/// its three rotations -- which is to say the boundary mask has a gap at
/// every third unit and nowhere else.
static int	layer_is_cube_shape(const uint8_t *layer)
{
	uint32_t	mask;
	uint32_t	want;
	uint32_t	shift;
	uint32_t	i;

	mask = layer_mask(layer);
	shift = 0;
	while (shift < 3)
	{
		want = 0;
		i = 0;
		while (i < SQ1_LAYER_UNITS)
		{
			if ((i + shift) % 3 != 0)
				want |= 1u << i;
			i++;
		}
		if (mask == want)
			return (1);
		shift++;
	}
	return (0);
}

static int	is_cube_shape(const uint8_t *state)
{
	return (layer_is_cube_shape(state)
		&& layer_is_cube_shape(state + SQ1_LAYER_UNITS));
}

/// D1's invariants: each corner id appears in exactly two units, adjacent
/// in one layer (including across 11|0); each edge id exactly once; each
/// layer is 12 units; the middle byte is 0 or 1.
static void	check_invariants(const uint8_t *state)
{
	uint32_t	count[SQ1_PIECE_COUNT];
	uint32_t	id;
	uint32_t	u;
	uint32_t	base;
	uint32_t	next;

	memset(count, 0, sizeof(count));
	u = 0;
	while (u < SQ1_UNIT_COUNT)
	{
		assert(state[u] < SQ1_PIECE_COUNT);
		count[state[u]]++;
		u++;
	}
	id = 0;
	while (id < SQ1_PIECE_COUNT)
	{
		assert(count[id] == (id < SQ1_FIRST_EDGE_ID ? 2u : 1u));
		id++;
	}
	u = 0;
	while (u < SQ1_UNIT_COUNT)
	{
		if (state[u] < SQ1_FIRST_EDGE_ID)
		{
			base = (u < SQ1_LAYER_UNITS) ? 0 : SQ1_LAYER_UNITS;
			next = base + (u - base + 1) % SQ1_LAYER_UNITS;
			assert(state[next] == state[u]
				|| state[base + (u - base + SQ1_LAYER_UNITS - 1)
					% SQ1_LAYER_UNITS] == state[u]);
		}
		u++;
	}
	assert(state[SQ1_MIDDLE_INDEX] <= 1);
}

/// Splitmix-free reproducible stream, so the tests pick the same moves on
/// every run.
static uint32_t	next_random(uint32_t *rng)
{
	*rng = *rng * 1664525u + 1013904223u;
	return (*rng >> 8);
}

static void	test_solved_state(void)
{
	uint8_t		state[SQ1_STATE_SIZE];
	uint32_t	i;

	sq1_init_solved(state);
	check_invariants(state);
	assert(is_cube_shape(state));
	assert(sq1_slice_is_legal(state));
	i = 0;
	while (i < SQ1_LAYER_UNITS)
	{
		/// Both layers hold [corner, corner, edge] x 4 in the same
		/// absolute alignment, because a solved Square-1 is a cube.
		assert(state[i] == (i % 3 == 2 ? 8 + i / 3 : i / 3));
		assert(state[SQ1_LAYER_UNITS + i]
			== (i % 3 == 2 ? 12 + i / 3 : 4 + i / 3));
		i++;
	}
	assert(state[SQ1_MIDDLE_INDEX] == 0);
}

/// (1,0) has order 12: eleven single-unit turns are not the identity, the
/// twelfth is. A quarter turn (3 units) keeps a cube-shaped layer
/// cube-shaped but moves its pieces.
static void	test_layer_turn_orders(void)
{
	uint8_t		solved[SQ1_STATE_SIZE];
	uint8_t		state[SQ1_STATE_SIZE];
	uint32_t	n;

	sq1_init_solved(solved);
	memcpy(state, solved, sizeof(state));
	n = 1;
	while (n < SQ1_LAYER_UNITS)
	{
		sq1_turn_top(state, 1);
		assert(memcmp(state, solved, sizeof(state)) != 0);
		n++;
	}
	sq1_turn_top(state, 1);
	assert(memcmp(state, solved, sizeof(state)) == 0);
	n = 1;
	while (n < SQ1_LAYER_UNITS)
	{
		sq1_turn_bottom(state, 1);
		assert(memcmp(state, solved, sizeof(state)) != 0);
		n++;
	}
	sq1_turn_bottom(state, 1);
	assert(memcmp(state, solved, sizeof(state)) == 0);
	sq1_turn_top(state, 3);
	assert(memcmp(state, solved, sizeof(state)) != 0);
	assert(layer_is_cube_shape(state));
}

/// Pins the direction of both layer turns against the stored order:
/// clockwise seen from above is increasing unit index.
static void	test_layer_turn_directions(void)
{
	uint8_t	state[SQ1_STATE_SIZE];

	sq1_init_solved(state);
	sq1_turn_top(state, 1);
	assert(state[1] == 0 && state[2] == 0);   // corner 0 was on units 0-1
	assert(state[0] == 11);                   // the edge from unit 11
	sq1_init_solved(state);
	sq1_turn_bottom(state, 1);
	/// Clockwise seen from BELOW is anticlockwise seen from above: the
	/// content of unit i moves to i - 1.
	assert(state[SQ1_LAYER_UNITS + 11] == 4);
	assert(state[SQ1_LAYER_UNITS + 0] == 4);
	assert(state[SQ1_LAYER_UNITS + 1] == 8 + 4);
}

static void	test_slice_geometry(void)
{
	uint8_t		solved[SQ1_STATE_SIZE];
	uint8_t		state[SQ1_STATE_SIZE];
	uint32_t	i;

	sq1_init_solved(solved);
	memcpy(state, solved, sizeof(state));
	assert(sq1_slice(state) == 1);
	check_invariants(state);
	i = 0;
	while (i < SQ1_HALF_UNITS)
	{
		/// Top unit i goes to bottom unit 5 - i, and back.
		assert(state[SQ1_LAYER_UNITS + 5 - i] == solved[i]);
		assert(state[i] == solved[SQ1_LAYER_UNITS + 5 - i]);
		/// The left half does not move.
		assert(state[6 + i] == solved[6 + i]);
		assert(state[SQ1_LAYER_UNITS + 6 + i]
			== solved[SQ1_LAYER_UNITS + 6 + i]);
		i++;
	}
	assert(state[SQ1_MIDDLE_INDEX] == 1);
	/// An involution.
	assert(sq1_slice(state) == 1);
	assert(memcmp(state, solved, sizeof(state)) == 0);
}

/// A solved Square-1 is a cube, and the slice is legal on it, but it does
/// NOT keep cube shape (square1.h; DECISIONS.md 2026-10-09). The turn
/// pairs that do are exactly x in {0,3,6,9} with y in {2,5,8,11}, or x in
/// {1,4,7,10} with y in {0,3,6,9}: layers shifted so the half-turn lands
/// every edge back on a face.
static void	test_cube_shape_after_slice(void)
{
	uint8_t		solved[SQ1_STATE_SIZE];
	uint8_t		state[SQ1_STATE_SIZE];
	uint32_t	x;
	uint32_t	y;
	uint32_t	keeping;
	int			expected;

	sq1_init_solved(solved);
	memcpy(state, solved, sizeof(state));
	assert(sq1_slice(state) == 1);
	assert(!is_cube_shape(state));
	keeping = 0;
	x = 0;
	while (x < SQ1_LAYER_UNITS)
	{
		y = 0;
		while (y < SQ1_LAYER_UNITS)
		{
			memcpy(state, solved, sizeof(state));
			sq1_turn_top(state, x);
			sq1_turn_bottom(state, y);
			if (sq1_slice(state) && is_cube_shape(state))
			{
				keeping++;
				expected = (x % 3 == 0 && y % 3 == 2)
					|| (x % 3 == 1 && y % 3 == 0);
				assert(expected);
			}
			y++;
		}
		x++;
	}
	assert(keeping == 32);
}

/// A blocked slice (a corner straddling the cut) is refused: the state is
/// byte-identical afterwards, through the public API too.
static void	test_blocked_slice(void)
{
	uint8_t		state[SQ1_STATE_SIZE];
	uint8_t		before[SQ1_STATE_SIZE];
	puzzle_t	*p;

	/// Turning the top layer by 11 puts corner 0 (units 0-1) on units 11
	/// and 0: it straddles the cut.
	sq1_init_solved(state);
	sq1_turn_top(state, 11);
	assert(state[11] == 0 && state[0] == 0);
	assert(!sq1_slice_is_legal(state));
	memcpy(before, state, sizeof(before));
	assert(sq1_slice(state) == 0);
	assert(memcmp(state, before, sizeof(state)) == 0);
	assert(sq1_apply_move(state, SQ1_GEN_SLICE, 1) == 0);
	assert(memcmp(state, before, sizeof(state)) == 0);
	/// Layer turns are never blocked, and turning back unblocks.
	assert(sq1_move_is_legal(state, SQ1_GEN_TOP, 1) == 1);
	sq1_turn_top(state, 1);
	assert(sq1_slice_is_legal(state));
	p = puzzle_create(PUZZLE_SQUARE1, 0);
	assert(p != NULL);
	puzzle_apply_move(p, SQ1_GEN_TOP, 11);
	assert(puzzle_move_is_legal(p, SQ1_GEN_SLICE, 1) == 0);
	assert(puzzle_move_is_legal(p, SQ1_GEN_TOP, 5) == 1);
	assert(puzzle_move_is_legal(p, SQ1_GEN_BOTTOM, 12) == 0);   // range
	memcpy(before, puzzle_state_ptr(p), sizeof(before));
	puzzle_apply_move(p, SQ1_GEN_SLICE, 1);
	assert(memcmp(puzzle_state_ptr(p), before, sizeof(before)) == 0);
	puzzle_apply_move(p, 3, 1);                  // no such generator
	puzzle_apply_move(p, SQ1_GEN_TOP, 12);       // amount out of range
	puzzle_apply_move(p, SQ1_GEN_SLICE, 2);
	assert(memcmp(puzzle_state_ptr(p), before, sizeof(before)) == 0);
	puzzle_destroy(p);
}

static void	test_api_shape(void)
{
	puzzle_t		*p = puzzle_create(PUZZLE_SQUARE1, 0);
	puzzle_t		*cubic = puzzle_create(PUZZLE_CUBIC, 3);
	uint8_t			solved[SQ1_STATE_SIZE];
	uint32_t		g;

	assert(p != NULL && cubic != NULL);
	assert(puzzle_state_size(p) == SQ1_STATE_SIZE);
	assert(puzzle_generator_count(p) == SQ1_GENERATOR_COUNT);
	assert(puzzle_generator_steps(p, SQ1_GEN_TOP) == SQ1_LAYER_STEPS);
	assert(puzzle_generator_steps(p, SQ1_GEN_BOTTOM) == SQ1_LAYER_STEPS);
	assert(puzzle_generator_steps(p, SQ1_GEN_SLICE) == SQ1_SLICE_STEPS);
	assert(puzzle_generator_steps(p, 3) == 0);
	assert(puzzle_facelet_count(p) == SQ1_FACELET_COUNT);
	assert(puzzle_facelet_geometry(p) == NULL);
	assert(puzzle_facelet_geometry_stride(p) == 0);
	assert(puzzle_generator_face(p, 0) == -1);
	assert(puzzle_square1_units(p) != NULL);
	assert(puzzle_square1_units(cubic) == NULL);
	sq1_init_solved(solved);
	assert(memcmp(puzzle_state_ptr(p), solved, sizeof(solved)) == 0);
	/// Every other family answers the new calls as it always behaved.
	g = 0;
	while (g < puzzle_generator_count(cubic))
	{
		assert(puzzle_generator_steps(cubic, g) == 3);
		assert(puzzle_move_is_legal(cubic, (int16_t)g, 3) == 1);
		assert(puzzle_move_is_legal(cubic, (int16_t)g, 4) == 0);
		g++;
	}
	assert(puzzle_generator_steps(cubic, puzzle_generator_count(cubic)) == 0);
	puzzle_destroy(cubic);
	puzzle_destroy(p);
}

/// 200 scrambles, with the invariants checked after every single move
/// (turns, slices, and slices attempted while blocked).
static void	test_invariants_over_scrambles(void)
{
	uint8_t		state[SQ1_STATE_SIZE];
	uint8_t		before[SQ1_STATE_SIZE];
	uint32_t	rng;
	uint32_t	run;
	uint32_t	step;
	int16_t		generator;
	int8_t		amount;
	int			legal;

	rng = 12345;
	run = 0;
	while (run < SCRAMBLE_COUNT)
	{
		sq1_init_solved(state);
		step = 0;
		while (step < 60)
		{
			generator = (int16_t)(next_random(&rng) % SQ1_GENERATOR_COUNT);
			amount = (int8_t)(generator == SQ1_GEN_SLICE ? 1
					: 1 + next_random(&rng) % SQ1_LAYER_STEPS);
			memcpy(before, state, sizeof(before));
			legal = sq1_move_is_legal(state, generator, amount);
			assert(sq1_apply_move(state, generator, amount) == legal);
			if (!legal)
				assert(memcmp(before, state, sizeof(before)) == 0);
			check_invariants(state);
			step++;
		}
		run++;
	}
}

static void	test_scramble(void)
{
	puzzle_t	*a = puzzle_create(PUZZLE_SQUARE1, 0);
	puzzle_t	*b = puzzle_create(PUZZLE_SQUARE1, 0);
	puzzle_t	*c = puzzle_create(PUZZLE_SQUARE1, 0);
	uint8_t		solved[SQ1_STATE_SIZE];
	uint32_t	seed;

	assert(a && b && c);
	sq1_init_solved(solved);
	puzzle_scramble(a, SCRAMBLE_SLICES, 7);
	puzzle_scramble(b, SCRAMBLE_SLICES, 7);
	puzzle_scramble(c, SCRAMBLE_SLICES, 8);
	assert(memcmp(puzzle_state_ptr(a), puzzle_state_ptr(b),
			SQ1_STATE_SIZE) == 0);
	assert(memcmp(puzzle_state_ptr(a), puzzle_state_ptr(c),
			SQ1_STATE_SIZE) != 0);
	assert(memcmp(puzzle_state_ptr(a), solved, SQ1_STATE_SIZE) != 0);
	check_invariants(puzzle_state_ptr(a));
	seed = 0;
	while (seed < SCRAMBLE_COUNT)
	{
		puzzle_scramble(c, SCRAMBLE_SLICES, seed);
		check_invariants(puzzle_state_ptr(c));
		seed++;
	}
	/// A zero seed must not stick (the PRNG would stay at zero).
	puzzle_scramble(c, SCRAMBLE_SLICES, 0);
	check_invariants(puzzle_state_ptr(c));
	puzzle_destroy(a);
	puzzle_destroy(b);
	puzzle_destroy(c);
}

/// BFS over shape states (top mask, bottom mask, middle) from solved, a
/// slice at a time with the layers turned freely in between, reaches
/// exactly every twistable shape with both middle states: 7,356.
static void	test_shape_graph(void)
{
	static uint8_t	queue[SHAPE_STATE_COUNT + 16][SQ1_STATE_SIZE];
	uint8_t			next[SQ1_STATE_SIZE];
	uint32_t		head;
	uint32_t		tail;
	uint32_t		key;
	uint32_t		x;
	uint32_t		y;

	memset(g_seen, 0, sizeof(g_seen));
	sq1_init_solved(queue[0]);
	key = shape_key(queue[0]);
	g_seen[key / 8] |= (uint8_t)(1u << (key % 8));
	head = 0;
	tail = 1;
	while (head < tail)
	{
		x = 0;
		while (x < SQ1_LAYER_UNITS)
		{
			y = 0;
			while (y < SQ1_LAYER_UNITS)
			{
				memcpy(next, queue[head], sizeof(next));
				sq1_turn_top(next, x);
				sq1_turn_bottom(next, y);
				if (sq1_slice(next))
				{
					key = shape_key(next);
					if (!(g_seen[key / 8] & (1u << (key % 8))))
					{
						g_seen[key / 8] |= (uint8_t)(1u << (key % 8));
						assert(tail < SHAPE_STATE_COUNT + 16);
						memcpy(queue[tail++], next, sizeof(next));
					}
				}
				y++;
			}
			x++;
		}
		head++;
	}
	assert(tail == SHAPE_STATE_COUNT);
}

static void	colour_counts(const uint8_t *facelets, uint32_t *counts)
{
	uint32_t	i;

	memset(counts, 0, COLOUR_COUNT * sizeof(*counts));
	i = 0;
	while (i < SQ1_FACELET_COUNT)
	{
		assert(facelets[i] < COLOUR_COUNT);
		counts[facelets[i]]++;
		i++;
	}
}

static void	test_facelets_solved(void)
{
	puzzle_t		*p = puzzle_create(PUZZLE_SQUARE1, 0);
	const uint8_t	*f;
	uint32_t		counts[COLOUR_COUNT];
	uint32_t		i;

	assert(p != NULL);
	f = puzzle_facelets(p);
	i = 0;
	while (i < SQ1_LAYER_UNITS)
	{
		assert(f[SQ1_CAP_FACELETS + i] == SQ1_FACE_U);
		assert(f[SQ1_CAP_FACELETS + SQ1_LAYER_UNITS + i] == SQ1_FACE_D);
		i++;
	}
	/// Going clockwise from the back-right: the corner's first half faces
	/// the back, its second the right, then the right edge, and so on.
	{
		static const uint8_t	top_sides[SQ1_LAYER_UNITS] = {
			SQ1_FACE_B, SQ1_FACE_R, SQ1_FACE_R, SQ1_FACE_R, SQ1_FACE_F,
			SQ1_FACE_F, SQ1_FACE_F, SQ1_FACE_L, SQ1_FACE_L, SQ1_FACE_L,
			SQ1_FACE_B, SQ1_FACE_B
		};

		i = 0;
		while (i < SQ1_LAYER_UNITS)
		{
			assert(f[SQ1_SIDE_FACELETS + i] == top_sides[i]);
			assert(f[SQ1_SIDE_FACELETS + SQ1_LAYER_UNITS + i]
				== top_sides[i]);
			i++;
		}
	}
	assert(f[SQ1_MIDDLE_FACELETS + 0] == SQ1_FACE_F);
	assert(f[SQ1_MIDDLE_FACELETS + 1] == SQ1_FACE_L);
	assert(f[SQ1_MIDDLE_FACELETS + 2] == SQ1_FACE_B);
	assert(f[SQ1_MIDDLE_FACELETS + 3] == SQ1_FACE_F);
	assert(f[SQ1_MIDDLE_FACELETS + 4] == SQ1_FACE_R);
	assert(f[SQ1_MIDDLE_FACELETS + 5] == SQ1_FACE_B);
	colour_counts(f, counts);
	assert(memcmp(counts, SOLVED_COLOUR_COUNTS, sizeof(counts)) == 0);
	puzzle_destroy(p);
}

/// After a slice the right middle half's front and back stickers trade
/// places, its right sticker stays, and a layer turn only moves facelets.
static void	test_facelets_moves(void)
{
	puzzle_t	*p = puzzle_create(PUZZLE_SQUARE1, 0);
	uint8_t		before[SQ1_FACELET_COUNT];
	const uint8_t	*f;
	uint32_t	counts[COLOUR_COUNT];
	uint32_t	i;
	uint32_t	rng;

	assert(p != NULL);
	memcpy(before, puzzle_facelets(p), sizeof(before));
	puzzle_apply_move(p, SQ1_GEN_SLICE, 1);
	f = puzzle_facelets(p);
	assert(f[SQ1_MIDDLE_FACELETS + 3] == before[SQ1_MIDDLE_FACELETS + 5]);
	assert(f[SQ1_MIDDLE_FACELETS + 5] == before[SQ1_MIDDLE_FACELETS + 3]);
	assert(f[SQ1_MIDDLE_FACELETS + 4] == before[SQ1_MIDDLE_FACELETS + 4]);
	i = 0;
	while (i < SQ1_MIDDLE_FACELET_COUNT / 2)
	{
		assert(f[SQ1_MIDDLE_FACELETS + i] == before[SQ1_MIDDLE_FACELETS + i]);
		i++;
	}
	/// A top turn of 1: every top cap and side moves one unit round.
	memcpy(before, f, sizeof(before));
	puzzle_apply_move(p, SQ1_GEN_TOP, 1);
	f = puzzle_facelets(p);
	i = 0;
	while (i < SQ1_LAYER_UNITS)
	{
		assert(f[SQ1_CAP_FACELETS + (i + 1) % 12]
			== before[SQ1_CAP_FACELETS + i]);
		assert(f[SQ1_SIDE_FACELETS + (i + 1) % 12]
			== before[SQ1_SIDE_FACELETS + i]);
		i++;
	}
	/// No move ever creates or destroys a sticker: colour totals hold.
	rng = 99;
	i = 0;
	while (i < 300)
	{
		puzzle_apply_move(p, (int16_t)(next_random(&rng) % 3),
			(int8_t)(1 + next_random(&rng) % 11));
		colour_counts(puzzle_facelets(p), counts);
		assert(memcmp(counts, SOLVED_COLOUR_COUNTS, sizeof(counts)) == 0);
		i++;
	}
	puzzle_destroy(p);
}

/// The units view: edge / first half / second half, derived from the state
/// and consistent with it: every corner has exactly one of each half.
static void	test_units_view(void)
{
	puzzle_t		*p = puzzle_create(PUZZLE_SQUARE1, 0);
	const uint8_t	*units;
	const uint8_t	*state;
	uint32_t		i;
	uint32_t		run;

	assert(p != NULL);
	units = puzzle_square1_units(p);
	i = 0;
	while (i < SQ1_LAYER_UNITS)
	{
		assert(units[i] == (i % 3 == 2 ? SQ1_UNIT_EDGE
				: (i % 3 == 0 ? SQ1_UNIT_CORNER_FIRST
					: SQ1_UNIT_CORNER_SECOND)));
		assert(units[SQ1_LAYER_UNITS + i] == units[i]);
		i++;
	}
	run = 0;
	while (run < 50)
	{
		puzzle_scramble(p, 10, run + 1);
		units = puzzle_square1_units(p);
		state = puzzle_state_ptr(p);
		i = 0;
		while (i < SQ1_UNIT_COUNT)
		{
			if (state[i] >= SQ1_FIRST_EDGE_ID)
				assert(units[i] == SQ1_UNIT_EDGE);
			else if (units[i] == SQ1_UNIT_CORNER_FIRST)
				assert(units[(i / 12) * 12 + (i % 12 + 1) % 12]
					== SQ1_UNIT_CORNER_SECOND);
			else
				assert(units[i] == SQ1_UNIT_CORNER_SECOND);
			i++;
		}
		run++;
	}
	puzzle_destroy(p);
}

int	main(void)
{
	test_solved_state();
	test_layer_turn_orders();
	test_layer_turn_directions();
	test_slice_geometry();
	test_cube_shape_after_slice();
	test_blocked_slice();
	test_api_shape();
	test_invariants_over_scrambles();
	test_scramble();
	test_shape_graph();
	test_facelets_solved();
	test_facelets_moves();
	test_units_view();
	printf("test_square1: all assertions passed\n");
	return (0);
}
