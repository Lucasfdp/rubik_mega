#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "puzzle.h"
#include "solve/skewb_optimal.h"
#include "solve/skewb_staged.h"

/// Optimal Skewb solver (docs/en/09-phase3-skewb-pyraminx.md increment
/// 4): the depth table, the 12-rotation frame correction, and the exit
/// gate's "200 random scrambles solve" on states reached with all
/// eight turns.

/// The classic group (the four canonical turns) and its God's number
/// under "any turn of any canonical corner, either way, is one move".
#define SKEWB_GROUP_ORDER 3149280u
#define SKEWB_GODS_NUMBER 11u
#define SCRAMBLE_COUNT 200
#define SCRAMBLE_LENGTH 40

static t_skewb_solver	g_solver;

/// The oracle: the table's breadth-first search visits exactly the
/// published group order, and nothing is deeper than the published
/// God's number. A wrong move model almost never lands on either.
static void	test_table_matches_published_numbers(void)
{
	assert(skewb_solver_init(&g_solver));
	assert(g_solver.table.state_count == SKEWB_GROUP_ORDER);
	assert(g_solver.table.reached_count == SKEWB_GROUP_ORDER);
	assert(g_solver.table.max_depth == SKEWB_GODS_NUMBER);
	assert(g_solver.rotation_count == SKEWB_ROTATION_COUNT);
	assert(SKEWB_MAX_MOVES >= SKEWB_GODS_NUMBER);
}

/// Each rotation word really is a rotation: uniform faces, and every
/// rotation is a different one.
static void	test_rotations(void)
{
	uint8_t		state[SKEWB_STATE_BYTES];
	uint32_t	i;
	uint32_t	j;

	i = 0;
	while (i < g_solver.rotation_count)
	{
		shape_init_solved(skewb_shape(), state);
		j = 0;
		while (j < g_solver.rotations[i].length)
			shape_apply_move(skewb_shape(), skewb_generators(), state,
				g_solver.rotations[i].word[j++]);
		assert(skewb_state_is_uniform(state));
		j = 0;
		while (j < i)
			assert(memcmp(g_solver.rotations[j++].corner_perm,
					g_solver.rotations[i].corner_perm,
					SKEWB_CORNER_COUNT) != 0);
		i++;
	}
}

/// A solved puzzle, and each of the 11 other states that merely look
/// solved (a whole-puzzle rotation), need no moves.
static void	test_solved_needs_nothing(void)
{
	uint8_t		state[SKEWB_STATE_BYTES];
	t_move		out[SKEWB_MAX_MOVES];
	uint32_t	i;
	uint32_t	j;

	i = 0;
	while (i < g_solver.rotation_count)
	{
		shape_init_solved(skewb_shape(), state);
		j = 0;
		while (j < g_solver.rotations[i].length)
			shape_apply_move(skewb_shape(), skewb_generators(), state,
				g_solver.rotations[i].word[j++]);
		assert(skewb_solve(&g_solver, state, out, SKEWB_MAX_MOVES) == 0);
		i++;
	}
}

/// Applies `count` moves to `state` and says whether the faces ended
/// uniform.
static int	solves(const uint8_t *state, const t_move *moves, int count)
{
	uint8_t	copy[SKEWB_STATE_BYTES];
	int		i;

	memcpy(copy, state, sizeof(copy));
	i = 0;
	while (i < count)
		shape_apply_move(skewb_shape(), skewb_generators(), copy, moves[i++]);
	return (skewb_state_is_uniform(copy));
}

/// The exit gate: 200 scrambles built from ALL EIGHT turns (so most
/// land outside the identity frame) each solve to uniform faces in at
/// most God's number of moves, using canonical turns only.
static void	test_random_scrambles(void)
{
	puzzle_t	*p;
	t_move		out[SKEWB_MAX_MOVES];
	int			count;
	int			i;
	uint32_t	seed;
	uint32_t	framed;

	framed = 0;
	seed = 1;
	while (seed <= SCRAMBLE_COUNT)
	{
		p = puzzle_create(PUZZLE_SKEWB, 0);
		puzzle_scramble(p, SCRAMBLE_LENGTH, seed * 2654435761u);
		framed += !skewb_state_is_uniform(puzzle_state_ptr(p));
		count = skewb_solve(&g_solver, puzzle_state_ptr(p), out,
				SKEWB_MAX_MOVES);
		assert(count >= 0 && count <= (int)SKEWB_GODS_NUMBER);
		assert(solves(puzzle_state_ptr(p), out, count));
		i = 0;
		while (i < count)
			assert(skewb_is_canonical_corner((uint32_t)out[i++].generator));
		puzzle_destroy(p);
		seed++;
	}
	assert(framed == SCRAMBLE_COUNT);
}

/// Optimality: a scramble of k canonical turns is never solved in more
/// than k moves.
static void	test_never_longer_than_the_scramble(void)
{
	uint8_t		state[SKEWB_STATE_BYTES];
	t_move		out[SKEWB_MAX_MOVES];
	t_move		move;
	uint32_t	rng;
	uint32_t	k;
	uint32_t	trial;
	uint32_t	i;

	rng = 12345;
	trial = 0;
	while (trial < 300)
	{
		k = 1 + trial % 8;
		shape_init_solved(skewb_shape(), state);
		i = 0;
		while (i++ < k)
		{
			rng = rng * 1664525u + 1013904223u;
			do
				move.generator = (int16_t)((rng >> 16) % SKEWB_CORNER_COUNT);
			while (!skewb_is_canonical_corner((uint32_t)move.generator)
				&& (rng = rng * 1664525u + 1013904223u));
			move.amount = (int8_t)(1 + ((rng >> 8) & 1));
			shape_apply_move(skewb_shape(), skewb_generators(), state, move);
		}
		assert(skewb_solve(&g_solver, state, out, SKEWB_MAX_MOVES)
			<= (int)k);
		trial++;
	}
}

/// A state no turns can reach is refused, not "solved" with garbage.
static void	test_unreachable_states_refused(void)
{
	uint8_t		state[SKEWB_STATE_BYTES];
	t_move		out[SKEWB_MAX_MOVES];
	uint32_t	perm_offset;
	uint32_t	orient_offset;
	uint8_t		swap;

	shape_piece_offsets(skewb_shape(), SKEWB_CORNER_TYPE, &perm_offset,
		&orient_offset);
	shape_init_solved(skewb_shape(), state);
	state[orient_offset] = 1;
	assert(skewb_solve(&g_solver, state, out, SKEWB_MAX_MOVES) == -1);
	shape_init_solved(skewb_shape(), state);
	swap = state[perm_offset];
	state[perm_offset] = state[perm_offset + 1];
	state[perm_offset + 1] = swap;
	assert(skewb_solve(&g_solver, state, out, SKEWB_MAX_MOVES) == -1);
	shape_piece_offsets(skewb_shape(), SKEWB_CENTRE_TYPE, &perm_offset,
		&orient_offset);
	shape_init_solved(skewb_shape(), state);
	swap = state[perm_offset];
	state[perm_offset] = state[perm_offset + 1];
	state[perm_offset + 1] = swap;
	assert(skewb_solve(&g_solver, state, out, SKEWB_MAX_MOVES) == -1);
}

/// The PUBLIC API (puzzle_algorithm_*, puzzle_solve): what web/ calls.
/// Solving a scrambled puzzle and replaying the moves through
/// puzzle_apply_move() leaves it uniform.
static void	test_public_api(void)
{
	puzzle_t	*p;
	t_move		out[SKEWB_STAGED_MAX_MOVES];
	uint32_t	tags[SKEWB_STAGED_MAX_MOVES];
	uint32_t	count;
	uint32_t	i;

	p = puzzle_create(PUZZLE_SKEWB, 0);
	assert(puzzle_algorithm_count(p) == 2);
	assert(strcmp(puzzle_algorithm_name(p, 0), "Optimal (full depth table)")
		== 0);
	assert(puzzle_algorithm_name(p, 2) == NULL);
	assert(puzzle_solve_max_moves(p) == SKEWB_STAGED_MAX_MOVES);
	assert(puzzle_solve(p, 2, out, SKEWB_MAX_MOVES, tags) == UINT32_MAX);
	puzzle_scramble(p, SCRAMBLE_LENGTH, 99);
	count = puzzle_solve(p, 0, out, puzzle_solve_max_moves(p), tags);
	assert(count != UINT32_MAX && count <= SKEWB_GODS_NUMBER);
	i = 0;
	while (i < count)
	{
		assert(tags[i] == 0);
		puzzle_apply_move(p, out[i].generator, out[i].amount);
		i++;
	}
	assert(skewb_state_is_uniform(puzzle_state_ptr(p)));
	puzzle_destroy(p);
}

int	main(void)
{
	test_table_matches_published_numbers();
	test_rotations();
	test_solved_needs_nothing();
	test_random_scrambles();
	test_never_longer_than_the_scramble();
	test_unreachable_states_refused();
	test_public_api();
	printf("test_skewb_solve: all assertions passed\n");
	return (0);
}
