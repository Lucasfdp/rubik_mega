#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "puzzle.h"
#include "puzzles/skewb.h"
#include "orbit_count.h"

/// Skewb (docs/en/09-phase3-skewb-pyraminx.md increment 1): shape,
/// derived generators, and the orbit-size oracle.

#define SKEWB_STATE_SIZE 28
/// The classic Skewb group, reached by the four canonical turns.
#define SKEWB_GROUP_ORDER 3149280ull

static void	test_shape_and_table(void)
{
	const t_generator_table	*gens = skewb_generators();

	assert(shape_is_valid(skewb_shape()));
	assert(shape_state_size(skewb_shape()) == SKEWB_STATE_SIZE);
	assert(gens != NULL);
	assert(gens->count == SKEWB_CORNER_COUNT);
	assert(gens->steps == SKEWB_STEPS);
}

/// Every generator is one twist-in-place plus two 3-cycles (corners,
/// centres): the twist is what a Skewb corner turn does to the corner
/// it pivots on, so a missing length-1 cycle would be a real bug.
static void	test_cycle_structure(void)
{
	const t_generator_table	*gens = skewb_generators();
	uint32_t				g;
	uint32_t				c;
	uint32_t				threes;
	uint32_t				ones;
	const t_cycle			*cy;

	g = 0;
	while (g < gens->count)
	{
		threes = 0;
		ones = 0;
		c = 0;
		assert(gens->generators[g].cycle_count == 3);
		while (c < gens->generators[g].cycle_count)
		{
			cy = &gens->generators[g].cycles[c];
			threes += (cy->length == 3);
			ones += (cy->length == 1);
			if (cy->length == 1)
			{
				assert(cy->piece_type == SKEWB_CORNER_TYPE);
				assert(cy->delta[0] == 1 || cy->delta[0] == 2);
			}
			c++;
		}
		assert(threes == 2 && ones == 1);
		g++;
	}
}

/// Three quarter-steps of any generator is a full 360 degrees; one is not.
static void	test_order_three(void)
{
	const t_generator_table	*gens = skewb_generators();
	uint8_t					solved[SKEWB_STATE_SIZE];
	uint8_t					s[SKEWB_STATE_SIZE];
	uint32_t				g;

	shape_init_solved(skewb_shape(), solved);
	g = 0;
	while (g < gens->count)
	{
		memcpy(s, solved, sizeof(s));
		orbit_apply(skewb_shape(), gens, s, g, 1);
		assert(memcmp(s, solved, sizeof(s)) != 0);
		orbit_apply(skewb_shape(), gens, s, g, 2);
		assert(memcmp(s, solved, sizeof(s)) == 0);
		g++;
	}
}

static void	test_orbit_size(void)
{
	uint32_t		ids[4];
	uint32_t		c;
	uint32_t		n;
	t_orbit_spec	spec;

	n = 0;
	c = 0;
	while (c < SKEWB_CORNER_COUNT)
	{
		if (skewb_is_canonical_corner(c))
			ids[n++] = c;
		c++;
	}
	assert(n == 4);
	spec.shape = skewb_shape();
	spec.gens = skewb_generators();
	spec.gen_ids = ids;
	spec.gen_count = 4;
	spec.table_bits = 23;
	assert(orbit_count(&spec) == SKEWB_GROUP_ORDER);
}

static void	test_puzzle_api(void)
{
	puzzle_t	*p = puzzle_create(PUZZLE_SKEWB, 0);
	uint8_t		solved[SKEWB_STATE_SIZE];

	assert(p != NULL);
	assert(puzzle_generator_count(p) == SKEWB_CORNER_COUNT);
	assert(puzzle_state_size(p) == SKEWB_STATE_SIZE);
	assert(puzzle_algorithm_count(p) == 2);
	assert(puzzle_facelet_count(p) == SKEWB_STICKER_COUNT);
	assert(puzzle_generator_face(p, 0) == -1);   // no face/depth layers
	shape_init_solved(skewb_shape(), solved);
	assert(memcmp(puzzle_state_ptr(p), solved, sizeof(solved)) == 0);
	puzzle_scramble(p, 30, 7);
	assert(memcmp(puzzle_state_ptr(p), solved, sizeof(solved)) != 0);
	puzzle_destroy(p);
}

int	main(void)
{
	test_shape_and_table();
	test_cycle_structure();
	test_order_three();
	test_puzzle_api();
	test_orbit_size();
	printf("test_skewb: all assertions passed\n");
	return (0);
}
