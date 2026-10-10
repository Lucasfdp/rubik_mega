#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "puzzle.h"
#include "solve/pyraminx_optimal.h"
#include "solve/pyraminx_staged.h"

/// Optimal Pyraminx solver (docs/en/09-phase3-skewb-pyraminx.md
/// increment 4): the depth table over edges and axial pieces, the tip
/// finish, and the exit gate's "200 random scrambles solve".

/// Edges (360 even permutations x 32 orientations) x axial pieces (81):
/// the published 933,120 for a Pyraminx with its tips ignored, and the
/// published God's number for it.
#define PYRAMINX_GROUP_ORDER 933120u
#define PYRAMINX_GODS_NUMBER 11u
#define SCRAMBLE_COUNT 200
#define SCRAMBLE_LENGTH 40

static t_pyraminx_solver	g_solver;

static void	test_table_matches_published_numbers(void)
{
	assert(pyraminx_solver_init(&g_solver));
	assert(g_solver.table.state_count == PYRAMINX_GROUP_ORDER);
	assert(g_solver.table.reached_count == PYRAMINX_GROUP_ORDER);
	assert(g_solver.table.max_depth == PYRAMINX_GODS_NUMBER);
	assert(PYRAMINX_MAX_MOVES >= PYRAMINX_GODS_NUMBER
		+ PYRAMINX_MAX_TIP_MOVES);
}

static void	test_solved_needs_nothing(void)
{
	uint8_t		state[PYRAMINX_STATE_BYTES];
	t_move		out[PYRAMINX_MAX_MOVES];
	uint32_t	layer;

	shape_init_solved(tetrahedral_shape(), state);
	assert(pyraminx_solve(&g_solver, state, out, PYRAMINX_MAX_MOVES, &layer)
		== 0);
	assert(layer == 0);
}

/// The exit gate: 200 scrambles of all eight turns (tips included)
/// solve to the identity, layer moves first, tips last, never more than
/// 11 + 4 moves, and no tip move among the layer moves or the reverse.
static void	test_random_scrambles(void)
{
	puzzle_t	*p;
	uint8_t		solved[PYRAMINX_STATE_BYTES];
	uint8_t		copy[PYRAMINX_STATE_BYTES];
	t_move		out[PYRAMINX_MAX_MOVES];
	uint32_t	layer;
	int			count;
	int			i;
	uint32_t	seed;

	shape_init_solved(tetrahedral_shape(), solved);
	seed = 1;
	while (seed <= SCRAMBLE_COUNT)
	{
		p = puzzle_create(PUZZLE_TETRAHEDRAL, 0);
		puzzle_scramble(p, SCRAMBLE_LENGTH, seed * 2654435761u);
		count = pyraminx_solve(&g_solver, puzzle_state_ptr(p), out,
				PYRAMINX_MAX_MOVES, &layer);
		assert(count >= 0 && count <= (int)PYRAMINX_MAX_MOVES);
		assert(layer <= PYRAMINX_GODS_NUMBER);
		assert(count - (int)layer <= (int)PYRAMINX_MAX_TIP_MOVES);
		memcpy(copy, puzzle_state_ptr(p), sizeof(copy));
		i = 0;
		while (i < count)
		{
			assert((out[i].generator % TETRA_DEPTH_COUNT
					== TETRA_DEPTH_LAYER) == (i < (int)layer));
			shape_apply_move(tetrahedral_shape(), tetrahedral_generators(),
				copy, out[i++]);
		}
		assert(memcmp(copy, solved, sizeof(copy)) == 0);
		puzzle_destroy(p);
		seed++;
	}
}

/// Optimality: a scramble of k layer turns never needs more than k
/// layer moves.
static void	test_never_longer_than_the_scramble(void)
{
	uint8_t		state[PYRAMINX_STATE_BYTES];
	t_move		out[PYRAMINX_MAX_MOVES];
	t_move		move;
	uint32_t	rng;
	uint32_t	layer;
	uint32_t	k;
	uint32_t	trial;
	uint32_t	i;

	rng = 54321;
	trial = 0;
	while (trial < 300)
	{
		k = 1 + trial % 8;
		shape_init_solved(tetrahedral_shape(), state);
		i = 0;
		while (i++ < k)
		{
			rng = rng * 1664525u + 1013904223u;
			move.generator = (int16_t)(((rng >> 16) % TETRA_VERTEX_COUNT)
					* TETRA_DEPTH_COUNT + TETRA_DEPTH_LAYER);
			move.amount = (int8_t)(1 + ((rng >> 8) & 1));
			shape_apply_move(tetrahedral_shape(), tetrahedral_generators(),
				state, move);
		}
		assert(pyraminx_solve(&g_solver, state, out, PYRAMINX_MAX_MOVES,
				&layer) >= 0);
		assert(layer <= k);
		trial++;
	}
}

/// Each wrong tip costs one move, and a tip turned both ways round
/// costs one move too (amount 1 or 2, whichever undoes it).
static void	test_tips_cost_one_move_each(void)
{
	uint8_t		state[PYRAMINX_STATE_BYTES];
	t_move		out[PYRAMINX_MAX_MOVES];
	t_move		move;
	uint32_t	layer;
	uint32_t	vertex;
	int8_t		amount;

	vertex = 0;
	while (vertex < TETRA_VERTEX_COUNT)
	{
		amount = 1;
		while (amount <= (int8_t)TETRA_STEPS)
		{
			shape_init_solved(tetrahedral_shape(), state);
			move.generator = (int16_t)(vertex * TETRA_DEPTH_COUNT
					+ TETRA_DEPTH_TIP);
			move.amount = amount++;
			shape_apply_move(tetrahedral_shape(), tetrahedral_generators(),
				state, move);
			assert(pyraminx_solve(&g_solver, state, out, PYRAMINX_MAX_MOVES,
					&layer) == 1);
			assert(layer == 0 && out[0].generator == move.generator);
		}
		vertex++;
	}
}

/// A state no turns can reach is refused, not "solved" with garbage.
static void	test_unreachable_states_refused(void)
{
	uint8_t		state[PYRAMINX_STATE_BYTES];
	t_move		out[PYRAMINX_MAX_MOVES];
	uint32_t	layer;
	uint32_t	perm_offset;
	uint32_t	orient_offset;
	uint8_t		swap;

	shape_piece_offsets(tetrahedral_shape(), TETRA_EDGE_TYPE, &perm_offset,
		&orient_offset);
	shape_init_solved(tetrahedral_shape(), state);
	state[orient_offset] = 1;
	assert(pyraminx_solve(&g_solver, state, out, PYRAMINX_MAX_MOVES, &layer)
		== -1);
	shape_init_solved(tetrahedral_shape(), state);
	swap = state[perm_offset];
	state[perm_offset] = state[perm_offset + 1];
	state[perm_offset + 1] = swap;
	assert(pyraminx_solve(&g_solver, state, out, PYRAMINX_MAX_MOVES, &layer)
		== -1);
}

/// The PUBLIC API (puzzle_algorithm_*, puzzle_solve): what web/ calls.
/// Phase tags are 0 for layer moves and 1 for tips; replaying the moves
/// through puzzle_apply_move() restores the identity.
static void	test_public_api(void)
{
	puzzle_t	*p;
	uint8_t		solved[PYRAMINX_STATE_BYTES];
	t_move		out[PYRAMINX_STAGED_MAX_MOVES];
	uint32_t	tags[PYRAMINX_STAGED_MAX_MOVES];
	uint32_t	count;
	uint32_t	i;

	p = puzzle_create(PUZZLE_TETRAHEDRAL, 0);
	assert(puzzle_algorithm_count(p) == 2);
	assert(strcmp(puzzle_algorithm_name(p, 0),
			"Optimal, then tips (full depth table)") == 0);
	assert(puzzle_algorithm_name(p, 2) == NULL);
	assert(puzzle_solve_max_moves(p) == PYRAMINX_STAGED_MAX_MOVES);
	assert(puzzle_solve(p, 2, out, PYRAMINX_MAX_MOVES, tags) == UINT32_MAX);
	puzzle_scramble(p, SCRAMBLE_LENGTH, 99);
	count = puzzle_solve(p, 0, out, puzzle_solve_max_moves(p), tags);
	assert(count != UINT32_MAX && count <= PYRAMINX_MAX_MOVES);
	i = 0;
	while (i < count)
	{
		assert(tags[i] == (uint32_t)(out[i].generator % TETRA_DEPTH_COUNT
				== TETRA_DEPTH_TIP));
		puzzle_apply_move(p, out[i].generator, out[i].amount);
		i++;
	}
	shape_init_solved(tetrahedral_shape(), solved);
	assert(memcmp(puzzle_state_ptr(p), solved, sizeof(solved)) == 0);
	puzzle_destroy(p);
}

int	main(void)
{
	test_table_matches_published_numbers();
	test_solved_needs_nothing();
	test_random_scrambles();
	test_never_longer_than_the_scramble();
	test_tips_cost_one_move_each();
	test_unreachable_states_refused();
	test_public_api();
	printf("test_pyraminx_solve: all assertions passed\n");
	return (0);
}
