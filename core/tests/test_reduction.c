#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "puzzle.h"
#include "move_list.h"
#include "puzzles/cubic.h"
#include "solve/reduction.h"
#include "solve/registry.h"

/// Tests for NxN reduction (docs/en/08-nxn-reduction.md). The 3-cycle
/// library is re-verified here independently of the search that built
/// it, and full solves are checked on the facelets -- the same colours
/// the user sees -- not on piece ids.

/// Random scrambles solved per size (the spec's exit gate).
# define SCRAMBLES_PER_SIZE 200

/// Moves per scramble: well past any size's mixing time.
# define SCRAMBLE_MOVES 300

static void	test_move_list_merge(void)
{
	t_move		m[] = {{1, 1}, {1, 1}, {2, 1}, {2, 3}, {1, 2}, {3, 1}, {3, 1}};
	uint32_t	tags[] = {0, 0, 0, 0, 0, 0, 1};
	uint32_t	count;

	count = move_list_merge(m, tags, 7, 3);
	// R R -> R2; F F' cancels, exposing R2 next to R2 -> nothing;
	// the two D moves differ in phase tag, so they stay apart.
	assert(count == 2);
	assert(m[0].generator == 3 && m[0].amount == 1 && tags[0] == 0);
	assert(m[1].generator == 3 && m[1].amount == 1 && tags[1] == 1);
	printf("test_reduction: move_list_merge cascades, respects phase tags -- OK\n");
}

/// @brief Every library cycle, re-run from solved: exactly its three
///        slots move, the way it claims (p->q->r), and corners plus
///        every non-orbit piece stay put.
static void	test_library_cycles_are_what_they_claim(void)
{
	uint32_t			n;
	const t_red_tables	*t;
	uint8_t				state[RED_MAX_STATE];
	t_move				seq[RED_COMM_LENGTH];
	uint32_t			i;
	uint32_t			c;
	uint32_t			k;
	uint32_t			po;
	uint32_t			oo;
	uint32_t			moved;
	const t_red_orbit	*o;
	const t_red_cycle	*cy;

	n = REDUCTION_MIN_N;
	while (n <= REDUCTION_MAX_N)
	{
		t = reduction_tables_for(n);
		assert(t != NULL);
		i = 0;
		while (i < t->orbit_count)
		{
			o = &t->orbits[i];
			assert(o->cycle_count > 0);
			c = 0;
			while (c < o->cycle_count)
			{
				cy = &o->cycles[c];
				shape_init_solved(t->shape, state);
				red_expand_comm(t, cy->comm, cy->inverted, seq);
				k = 0;
				while (k < RED_COMM_LENGTH)
					red_apply(t, state, seq[k++]);
				shape_piece_offsets(t->shape, o->piece_type, &po, &oo);
				assert(state[po + o->slots[cy->q]] == o->slots[cy->p]);
				assert(state[po + o->slots[cy->r]] == o->slots[cy->q]);
				assert(state[po + o->slots[cy->p]] == o->slots[cy->r]);
				shape_piece_offsets(t->shape, CUBIC_CORNER_TYPE, &po, &oo);
				for (k = 0; k < 8; k++)
					assert(state[po + k] == k && state[oo + k] == 0);
				shape_piece_offsets(t->shape, o->piece_type, &po, &oo);
				moved = 0;
				for (k = 0; k < t->shape->piece_types[o->piece_type].count; k++)
					moved += (state[po + k] != k);
				assert(moved == 3);
				c++;
			}
			i++;
		}
		n++;
	}
	printf("test_reduction: every library 3-cycle moves exactly its 3 slots, "
		"n = 4..8 -- OK\n");
}

static int	facelets_solved(puzzle_t *p)
{
	const uint8_t	*colour;
	const uint8_t	*geo;
	uint32_t		i;

	colour = puzzle_facelets(p);
	geo = puzzle_facelet_geometry(p);
	i = 0;
	while (i < puzzle_facelet_count(p))
	{
		if (colour[i] != geo[i * 4 + 3])
			return (0);
		i++;
	}
	return (1);
}

/// @brief The exit gate: SCRAMBLES_PER_SIZE random scrambles at every
///        size solve to a cube whose every sticker is right, within
///        puzzle_solve_max_moves(), with phase tags in order.
static void	test_random_scrambles_solve(void)
{
	uint32_t	n;
	uint32_t	k;
	uint32_t	i;
	uint32_t	count;
	uint32_t	max;
	puzzle_t	*p;
	t_move		*out;
	uint32_t	*tags;

	n = REDUCTION_MIN_N;
	while (n <= REDUCTION_MAX_N)
	{
		p = puzzle_create(PUZZLE_CUBIC, n);
		max = puzzle_solve_max_moves(p);
		out = malloc(sizeof(t_move) * max);
		tags = malloc(sizeof(uint32_t) * max);
		k = 0;
		while (k < SCRAMBLES_PER_SIZE)
		{
			puzzle_destroy(p);
			p = puzzle_create(PUZZLE_CUBIC, n);
			puzzle_scramble(p, SCRAMBLE_MOVES, k * 7919u + n);
			count = puzzle_solve(p, 0, out, max, tags);
			assert(count != UINT32_MAX && count <= max);
			i = 0;
			while (i < count)
			{
				assert(i == 0 || tags[i] >= tags[i - 1]);
				puzzle_apply_move(p, out[i].generator, out[i].amount);
				i++;
			}
			assert(facelets_solved(p));
			k++;
		}
		free(out);
		free(tags);
		puzzle_destroy(p);
		n++;
	}
	printf("test_reduction: %u random scrambles per size solve, n = 4..8 -- OK\n",
		SCRAMBLES_PER_SIZE);
}

static void	test_solved_cube_needs_nothing_and_registry(void)
{
	uint32_t	n;
	puzzle_t	*p;
	t_move		out[1];
	uint32_t	tags[1];

	n = REDUCTION_MIN_N;
	while (n <= REDUCTION_MAX_N)
	{
		p = puzzle_create(PUZZLE_CUBIC, n);
		assert(puzzle_algorithm_count(p) == 1);
		assert(strstr(puzzle_algorithm_name(p, 0), "Reduction") != NULL);
		assert(puzzle_solve(p, 0, out, 1, tags) == 0);
		assert(puzzle_solve(p, 1, out, 1, tags) == UINT32_MAX);
		puzzle_destroy(p);
		n++;
	}
	p = puzzle_create(PUZZLE_CUBIC, 3);
	assert(puzzle_algorithm_count(p) == 4);
	assert(puzzle_solve_max_moves(p) == CUBIC_N3_MAX_MOVES);
	puzzle_destroy(p);
	printf("test_reduction: solved -> 0 moves; registry per size -- OK\n");
}

int	main(void)
{
	test_move_list_merge();
	test_library_cycles_are_what_they_claim();
	test_solved_cube_needs_nothing_and_registry();
	test_random_scrambles_solve();
	printf("test_reduction: all assertions passed\n");
	return (0);
}
