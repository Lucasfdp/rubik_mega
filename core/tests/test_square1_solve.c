#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "puzzle.h"
#include "puzzles/square1.h"
#include "solve/square1_solver.h"
#include "solve/registry.h"

/// Square-1's two-phase solver (docs/en/12-phase5-square1.md increment 5):
/// its tables against the spec's measurements, then 200 seeded scrambles
/// solved to exactly the solved state through the public API.

#define SCRAMBLE_COUNT 200
#define SCRAMBLE_SLICES 25
/// Measured over these 200 scrambles when the solver was written, with a
/// little room: the proven maximum for the whole puzzle is 13 (twist
/// metric), and a two-phase solver is not optimal.
#define SOLVE_SLICE_BOUND 16
/// The spec's measurement: at most 8 slices from any shape to cube shape
/// with the middle layer ignored (7 here, between layer shapes up to
/// rotation, which is what the table holds).
#define SHAPE_DISTANCE_BOUND 8
#define SQ1_ALGORITHM_NAME "Two-phase (near-optimal)"

static t_sq1_solver	g_solver;

static int	is_cube_layer(const uint8_t *layer)
{
	uint32_t	shift;
	uint32_t	i;
	int			ok;

	shift = 0;
	while (shift < 3)
	{
		ok = 1;
		i = 0;
		while (i < SQ1_LAYER_UNITS)
		{
			/// A corner's two units are equal; a unit is the second half of
			/// a corner exactly when it equals the one before.
			if (((i + shift) % 3 == 1) != (layer[i] == layer[(i + 11) % 12]))
				ok = 0;
			i++;
		}
		if (ok)
			return (1);
		shift++;
	}
	return (0);
}

static void	test_tables(void)
{
	uint8_t		state[SQ1_STATE_SIZE];
	uint32_t	a;
	uint32_t	b;
	uint32_t	reached;
	uint32_t	seed;

	assert(sq1_solver_init(&g_solver));
	reached = 0;
	a = 0;
	while (a < SQ1_SHAPE_CLASS_COUNT)
	{
		b = 0;
		while (b < SQ1_SHAPE_CLASS_COUNT)
		{
			if (g_solver.shape_dist[a][b] != SQ1_SHAPE_UNREACHED)
			{
				assert(g_solver.shape_dist[a][b] <= SHAPE_DISTANCE_BOUND);
				reached++;
			}
			b++;
		}
		a++;
	}
	/// Solved is cube shape: distance 0.
	sq1_init_solved(state);
	assert(sq1_solver_shape_distance(&g_solver, state) == 0);
	assert(reached > 1);
	/// Every scrambled state is some shape pair the table knows.
	seed = 0;
	while (seed < 50)
	{
		sq1_init_solved(state);
		sq1_scramble(state, SCRAMBLE_SLICES, seed + 1);
		assert(sq1_solver_shape_distance(&g_solver, state)
			<= SHAPE_DISTANCE_BOUND);
		seed++;
	}
	/// Every prune cell is reachable: corners and edges are each a full
	/// 8! of positions, with either middle state.
	a = 0;
	while (a < SQ1_PERM_COUNT * SQ1_MIDDLE_STATES)
	{
		assert(g_solver.prune_corner[a] != SQ1_SHAPE_UNREACHED);
		assert(g_solver.prune_edge[a] != SQ1_SHAPE_UNREACHED);
		a++;
	}
}

static void	test_solved_needs_nothing(void)
{
	uint8_t		state[SQ1_STATE_SIZE];
	t_move		out[SQ1_SOLVE_MAX_MOVES];
	uint32_t	tags[SQ1_SOLVE_MAX_MOVES];
	uint32_t	slices;

	sq1_init_solved(state);
	assert(sq1_solver_solve(&g_solver, state, out, SQ1_SOLVE_MAX_MOVES, tags,
			&slices) == 0);
	assert(slices == 0);
}

/// Replays `moves` on a copy, checking every one is in range and legal.
static void	replay_to_solved(const uint8_t *start, const t_move *moves,
	uint32_t count)
{
	uint8_t		work[SQ1_STATE_SIZE];
	uint8_t		solved[SQ1_STATE_SIZE];
	uint32_t	i;

	memcpy(work, start, sizeof(work));
	i = 0;
	while (i < count)
	{
		assert(sq1_move_is_legal(work, moves[i].generator, moves[i].amount));
		sq1_apply_move(work, moves[i].generator, moves[i].amount);
		i++;
	}
	sq1_init_solved(solved);
	assert(memcmp(work, solved, sizeof(work)) == 0);
}

/// 200 seeded scrambles through the public API: each solves to exactly
/// solved, every move legal, within the length bound; phase tags only ever
/// go 0 then 1, and applying just the phase-0 moves leaves both layers in
/// cube shape.
static void	test_scrambles(void)
{
	puzzle_t	*p = puzzle_create(PUZZLE_SQUARE1, 0);
	t_move		out[SQ1_SOLVE_MAX_MOVES];
	uint32_t	tags[SQ1_SOLVE_MAX_MOVES];
	uint8_t		start[SQ1_STATE_SIZE];
	uint8_t		work[SQ1_STATE_SIZE];
	uint32_t	seed;
	uint32_t	count;
	uint32_t	i;
	uint32_t	slices;
	uint32_t	total_slices;
	uint32_t	max_slices;

	assert(p != NULL);
	total_slices = 0;
	max_slices = 0;
	seed = 0;
	while (seed < SCRAMBLE_COUNT)
	{
		sq1_init_solved(start);
		sq1_scramble(start, SCRAMBLE_SLICES, seed + 1000);
		sq1_init_solved(work);
		puzzle_destroy(p);
		p = puzzle_create(PUZZLE_SQUARE1, 0);
		assert(p != NULL);
		puzzle_scramble(p, SCRAMBLE_SLICES, seed + 1000);
		assert(memcmp(puzzle_state_ptr(p), start, SQ1_STATE_SIZE) == 0);
		count = puzzle_solve(p, 0, out, puzzle_solve_max_moves(p), tags);
		assert(count != UINT32_MAX && count <= SQ1_SOLVE_MAX_MOVES);
		replay_to_solved(start, out, count);
		slices = 0;
		i = 0;
		while (i < count)
		{
			slices += (out[i].generator == SQ1_GEN_SLICE);
			assert(tags[i] < SQ1_PHASE_COUNT);
			assert(i == 0 || tags[i] >= tags[i - 1]);
			i++;
		}
		assert(slices <= SOLVE_SLICE_BOUND);
		total_slices += slices;
		if (slices > max_slices)
			max_slices = slices;
		/// The phase-0 prefix ends in cube shape.
		memcpy(work, start, sizeof(work));
		i = 0;
		while (i < count && tags[i] == SQ1_PHASE_CUBE_SHAPE)
		{
			sq1_apply_move(work, out[i].generator, out[i].amount);
			i++;
		}
		assert(is_cube_layer(work) && is_cube_layer(work + SQ1_LAYER_UNITS));
		/// puzzle_solve must not touch the puzzle.
		assert(memcmp(puzzle_state_ptr(p), start, SQ1_STATE_SIZE) == 0);
		seed++;
	}
	printf("test_square1_solve: %d scrambles, %.2f slices on average, "
		"%u at most\n", SCRAMBLE_COUNT,
		(double)total_slices / SCRAMBLE_COUNT, max_slices);
	puzzle_destroy(p);
}

static void	test_refusals(void)
{
	puzzle_t	*p = puzzle_create(PUZZLE_SQUARE1, 0);
	t_move		out[SQ1_SOLVE_MAX_MOVES];
	uint32_t	tags[SQ1_SOLVE_MAX_MOVES];
	uint8_t		bad[SQ1_STATE_SIZE];

	assert(p != NULL);
	assert(puzzle_algorithm_count(p) == 1);
	assert(strcmp(puzzle_algorithm_name(p, 0), SQ1_ALGORITHM_NAME) == 0);
	assert(puzzle_algorithm_name(p, 1) == NULL);
	assert(puzzle_solve_max_moves(p) == SQ1_SOLVE_MAX_MOVES);
	assert(puzzle_solve(p, 1, out, SQ1_SOLVE_MAX_MOVES, tags) == UINT32_MAX);
	/// Not a Square-1: a corner id in the wrong places.
	sq1_init_solved(bad);
	bad[0] = 9;
	assert(sq1_solver_solve(&g_solver, bad, out, SQ1_SOLVE_MAX_MOVES, tags,
			NULL) == -1);
	/// Too small a buffer is refused, not overrun.
	puzzle_scramble(p, SCRAMBLE_SLICES, 5);
	assert(puzzle_solve(p, 0, out, 2, tags) == UINT32_MAX);
	puzzle_destroy(p);
}

int	main(void)
{
	test_tables();
	test_solved_needs_nothing();
	test_scrambles();
	test_refusals();
	printf("test_square1_solve: all assertions passed\n");
	return (0);
}
