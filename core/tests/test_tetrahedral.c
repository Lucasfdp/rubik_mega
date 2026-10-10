#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "puzzle.h"
#include "puzzles/tetrahedral.h"
#include "orbit_count.h"

/// Pyraminx (docs/en/09-phase3-skewb-pyraminx.md increment 1).

/// 4 tips + 4 axial + 6 edges, each a perm byte and an orientation byte.
#define TETRA_STATE_SIZE 28
/// The 'U' turns alone (tips ride along with their axial piece) reach
/// 360 edge permutations x 32 edge flips x 81 axial twists. Tips add a
/// further independent x81 once the `u` turns are included
/// (75,582,720 in all) -- too big to enumerate here, so tip
/// independence is tested separately below.
#define TETRA_LAYER_GROUP_ORDER 933120ull

static void	test_shape_and_table(void)
{
	const t_generator_table	*gens = tetrahedral_generators();

	assert(shape_is_valid(tetrahedral_shape()));
	assert(shape_state_size(tetrahedral_shape()) == TETRA_STATE_SIZE);
	assert(gens != NULL);
	assert(gens->count == TETRA_GENERATOR_COUNT);
	assert(gens->steps == TETRA_STEPS);
}

/// `u` is exactly one tip twist. `U` is a tip twist, an axial twist and
/// one 3-cycle of edges. No cycle ever moves an axial piece, which is
/// what keeps the frame fixed.
static void	test_cycle_structure(void)
{
	const t_generator_table	*gens = tetrahedral_generators();
	uint32_t				v;
	const t_generator		*u;
	const t_generator		*big;
	uint32_t				c;

	v = 0;
	while (v < TETRA_VERTEX_COUNT)
	{
		u = &gens->generators[v * TETRA_DEPTH_COUNT + TETRA_DEPTH_TIP];
		big = &gens->generators[v * TETRA_DEPTH_COUNT + TETRA_DEPTH_LAYER];
		assert(u->cycle_count == 1);
		assert(u->cycles[0].piece_type == TETRA_TIP_TYPE);
		assert(u->cycles[0].length == 1 && u->cycles[0].slots[0] == v);
		assert(big->cycle_count == 3);
		c = 0;
		while (c < big->cycle_count)
		{
			if (big->cycles[c].piece_type == TETRA_EDGE_TYPE)
				assert(big->cycles[c].length == 3);
			else
				assert(big->cycles[c].length == 1
					&& big->cycles[c].slots[0] == v);
			c++;
		}
		v++;
	}
}

static void	test_order_three(void)
{
	const t_generator_table	*gens = tetrahedral_generators();
	uint8_t					solved[TETRA_STATE_SIZE];
	uint8_t					s[TETRA_STATE_SIZE];
	uint32_t				g;

	shape_init_solved(tetrahedral_shape(), solved);
	g = 0;
	while (g < gens->count)
	{
		memcpy(s, solved, sizeof(s));
		orbit_apply(tetrahedral_shape(), gens, s, g, 1);
		assert(memcmp(s, solved, sizeof(s)) != 0);
		orbit_apply(tetrahedral_shape(), gens, s, g, 2);
		assert(memcmp(s, solved, sizeof(s)) == 0);
		g++;
	}
}

static void	test_layer_orbit_size(void)
{
	uint32_t		ids[TETRA_VERTEX_COUNT];
	uint32_t		v;
	t_orbit_spec	spec;

	v = 0;
	while (v < TETRA_VERTEX_COUNT)
	{
		ids[v] = v * TETRA_DEPTH_COUNT + TETRA_DEPTH_LAYER;
		v++;
	}
	spec.shape = tetrahedral_shape();
	spec.gens = tetrahedral_generators();
	spec.gen_ids = ids;
	spec.gen_count = TETRA_VERTEX_COUNT;
	spec.table_bits = 22;
	assert(orbit_count(&spec) == TETRA_LAYER_GROUP_ORDER);
}

/// A tip-only turn commutes with every other move (tips never permute,
/// and orientation changes just add), which is why tips can be fixed
/// last, after the 933,120-state core.
static void	test_tip_turns_commute(void)
{
	const t_generator_table	*gens = tetrahedral_generators();
	uint8_t					a[TETRA_STATE_SIZE];
	uint8_t					b[TETRA_STATE_SIZE];
	uint32_t				v;
	uint32_t				g;
	uint32_t				k;

	v = 0;
	while (v < TETRA_VERTEX_COUNT)
	{
		g = 0;
		while (g < gens->count)
		{
			shape_init_solved(tetrahedral_shape(), a);
			k = 0;
			while (k < 5)
				orbit_apply(tetrahedral_shape(), gens, a, (g + 3 * k++) % 8, 1);
			memcpy(b, a, sizeof(a));
			orbit_apply(tetrahedral_shape(), gens, a,
				v * TETRA_DEPTH_COUNT + TETRA_DEPTH_TIP, 1);
			orbit_apply(tetrahedral_shape(), gens, a, g, 1);
			orbit_apply(tetrahedral_shape(), gens, b, g, 1);
			orbit_apply(tetrahedral_shape(), gens, b,
				v * TETRA_DEPTH_COUNT + TETRA_DEPTH_TIP, 1);
			assert(memcmp(a, b, sizeof(a)) == 0);
			g++;
		}
		v++;
	}
}

static void	test_puzzle_api(void)
{
	puzzle_t	*p = puzzle_create(PUZZLE_TETRAHEDRAL, 0);
	uint8_t		solved[TETRA_STATE_SIZE];

	assert(p != NULL);
	assert(puzzle_generator_count(p) == TETRA_GENERATOR_COUNT);
	assert(puzzle_state_size(p) == TETRA_STATE_SIZE);
	assert(puzzle_algorithm_count(p) == 2);
	assert(puzzle_facelet_count(p) == TETRA_STICKER_COUNT);
	shape_init_solved(tetrahedral_shape(), solved);
	puzzle_scramble(p, 30, 7);
	assert(memcmp(puzzle_state_ptr(p), solved, sizeof(solved)) != 0);
	puzzle_destroy(p);
}

int	main(void)
{
	test_shape_and_table();
	test_cycle_structure();
	test_order_three();
	test_tip_turns_commute();
	test_puzzle_api();
	test_layer_orbit_size();
	printf("test_tetrahedral: all assertions passed\n");
	return (0);
}
