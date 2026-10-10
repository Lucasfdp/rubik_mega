#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "puzzle.h"
#include "solve/pyraminx_staged.h"

/// Staged Pyraminx solver (docs/en/10-phase3-human-methods.md increment
/// 5): the exit gate's 200 scrambles, each phase's promise asserted at
/// its own boundary, and the registry wiring.

#define SCRAMBLE_COUNT 200
#define SCRAMBLE_LENGTH 40

static t_pyraminx_solver	g_base;
static t_pyraminx_staged	g_staged;

static int	piece_home(const uint8_t *state, uint32_t type, uint32_t piece)
{
	uint32_t	perm_offset;
	uint32_t	orient_offset;

	shape_piece_offsets(tetrahedral_shape(), type, &perm_offset,
		&orient_offset);
	return (state[perm_offset + piece] == piece
		&& state[orient_offset + piece] == 0);
}

static int	axials_home(const uint8_t *state)
{
	uint32_t	v;

	v = 0;
	while (v < TETRA_VERTEX_COUNT)
		if (!piece_home(state, TETRA_AXIAL_TYPE, v++))
			return (0);
	return (1);
}

/// The three edges of the face opposite PYRAMINX_STAGED_VERTEX: slots
/// found from the vertex pairs, independently of the solver's own list.
static int	first_layer_edges_home(const uint8_t *state)
{
	uint32_t	a;
	uint32_t	b;

	a = 0;
	while (a < TETRA_VERTEX_COUNT)
	{
		b = a + 1;
		while (b < TETRA_VERTEX_COUNT)
		{
			if (a != PYRAMINX_STAGED_VERTEX && b != PYRAMINX_STAGED_VERTEX
				&& !piece_home(state, TETRA_EDGE_TYPE,
					tetrahedral_edge_slot(a, b)))
				return (0);
			b++;
		}
		a++;
	}
	return (1);
}

static int	edges_home(const uint8_t *state)
{
	uint32_t	e;

	e = 0;
	while (e < TETRA_EDGE_COUNT)
		if (!piece_home(state, TETRA_EDGE_TYPE, e++))
			return (0);
	return (1);
}

static void	test_init(void)
{
	assert(pyraminx_solver_init(&g_base));
	assert(pyraminx_staged_init(&g_staged, &g_base));
	assert(PYRAMINX_STAGED_MAX_MOVES >= PYRAMINX_MAX_MOVES);
}

static void	test_solved_needs_nothing(void)
{
	uint8_t		state[PYRAMINX_STATE_BYTES];
	t_move		out[PYRAMINX_STAGED_MAX_MOVES];
	uint32_t	ends[PYRAMINX_STAGE_COUNT];

	shape_init_solved(tetrahedral_shape(), state);
	assert(pyraminx_staged_solve(&g_staged, state, out,
			PYRAMINX_STAGED_MAX_MOVES, ends) == 0);
	assert(ends[0] == 0 && ends[1] == 0 && ends[2] == 0 && ends[3] == 0);
}

/// Replays `count` moves one at a time and checks, at each stage's end,
/// what that stage promised -- on the state buffer itself, not through
/// any table. Stages that needed no moves are checked too (equal ends
/// are fine).
static void	check_phases(const uint8_t *start, const t_move *out, int count,
	const uint32_t *ends)
{
	uint8_t		state[PYRAMINX_STATE_BYTES];
	uint8_t		solved[PYRAMINX_STATE_BYTES];
	uint32_t	stage;
	int			i;

	memcpy(state, start, sizeof(state));
	shape_init_solved(tetrahedral_shape(), solved);
	i = 0;
	stage = 0;
	while (stage < PYRAMINX_STAGE_COUNT)
	{
		assert(ends[stage] <= (uint32_t)count);
		assert(stage == 0 || ends[stage] >= ends[stage - 1]);
		while (i < (int)ends[stage])
			shape_apply_move(tetrahedral_shape(), tetrahedral_generators(),
				state, out[i++]);
		assert(axials_home(state));
		assert(stage < 1 || first_layer_edges_home(state));
		assert(stage < 2 || (edges_home(state) && axials_home(state)));
		assert(stage < 3 || memcmp(state, solved, sizeof(state)) == 0);
		stage++;
	}
	assert(ends[PYRAMINX_STAGE_COUNT - 1] == (uint32_t)count);
}

/// The stage boundaries also respect move kinds: only the last stage
/// (tips) uses tip turns, and it uses nothing else.
static void	check_move_kinds(const t_move *out, const uint32_t *ends)
{
	uint32_t	i;

	i = 0;
	while (i < ends[PYRAMINX_STAGE_COUNT - 1])
	{
		assert((out[i].generator % TETRA_DEPTH_COUNT == TETRA_DEPTH_TIP)
			== (i >= ends[PYRAMINX_STAGE_COUNT - 2]));
		i++;
	}
}

/// The exit gate: 200 scrambles of all eight turns solve to the
/// identity within the proven cap, with every phase's promise kept.
static void	test_random_scrambles(void)
{
	puzzle_t	*p;
	t_move		out[PYRAMINX_STAGED_MAX_MOVES];
	uint32_t	ends[PYRAMINX_STAGE_COUNT];
	int			count;
	uint32_t	seed;

	seed = 1;
	while (seed <= SCRAMBLE_COUNT)
	{
		p = puzzle_create(PUZZLE_TETRAHEDRAL, 0);
		puzzle_scramble(p, SCRAMBLE_LENGTH, seed * 2654435761u);
		count = pyraminx_staged_solve(&g_staged, puzzle_state_ptr(p), out,
				PYRAMINX_STAGED_MAX_MOVES, ends);
		assert(count >= 0 && count <= (int)PYRAMINX_STAGED_MAX_MOVES);
		check_phases(puzzle_state_ptr(p), out, count, ends);
		check_move_kinds(out, ends);
		puzzle_destroy(p);
		seed++;
	}
}

/// A staged solve is not just the optimal one relabelled: over the same
/// scrambles it is longer on average (the spec's "indistinguishable"
/// risk, measured: about 18 moves against about 10).
static void	test_differs_from_optimal(void)
{
	puzzle_t	*p;
	t_move		out[PYRAMINX_STAGED_MAX_MOVES];
	uint32_t	ends[PYRAMINX_STAGE_COUNT];
	uint32_t	layer;
	uint32_t	staged_total;
	uint32_t	optimal_total;
	uint32_t	seed;

	staged_total = 0;
	optimal_total = 0;
	seed = 1;
	while (seed <= SCRAMBLE_COUNT)
	{
		p = puzzle_create(PUZZLE_TETRAHEDRAL, 0);
		puzzle_scramble(p, SCRAMBLE_LENGTH, seed++ * 2654435761u);
		staged_total += (uint32_t)pyraminx_staged_solve(&g_staged,
				puzzle_state_ptr(p), out, PYRAMINX_STAGED_MAX_MOVES, ends);
		optimal_total += (uint32_t)pyraminx_solve(&g_base,
				puzzle_state_ptr(p), out, PYRAMINX_MAX_MOVES, &layer);
		puzzle_destroy(p);
	}
	assert(staged_total > optimal_total);
}

static void	test_unreachable_states_refused(void)
{
	uint8_t		state[PYRAMINX_STATE_BYTES];
	t_move		out[PYRAMINX_STAGED_MAX_MOVES];
	uint32_t	ends[PYRAMINX_STAGE_COUNT];
	uint32_t	perm_offset;
	uint32_t	orient_offset;
	uint8_t		swap;

	shape_piece_offsets(tetrahedral_shape(), TETRA_EDGE_TYPE, &perm_offset,
		&orient_offset);
	shape_init_solved(tetrahedral_shape(), state);
	state[orient_offset] = 1;
	assert(pyraminx_staged_solve(&g_staged, state, out,
			PYRAMINX_STAGED_MAX_MOVES, ends) == -1);
	shape_init_solved(tetrahedral_shape(), state);
	swap = state[perm_offset];
	state[perm_offset] = state[perm_offset + 1];
	state[perm_offset + 1] = swap;
	assert(pyraminx_staged_solve(&g_staged, state, out,
			PYRAMINX_STAGED_MAX_MOVES, ends) == -1);
}

/// Too small a `max_moves` is refused, never overrun.
static void	test_max_moves_respected(void)
{
	puzzle_t	*p;
	t_move		out[PYRAMINX_STAGED_MAX_MOVES];
	uint32_t	ends[PYRAMINX_STAGE_COUNT];

	p = puzzle_create(PUZZLE_TETRAHEDRAL, 0);
	puzzle_scramble(p, SCRAMBLE_LENGTH, 5);
	assert(pyraminx_staged_solve(&g_staged, puzzle_state_ptr(p), out, 1,
			ends) == -1);
	puzzle_destroy(p);
}

/// The PUBLIC API (what web/ calls): the staged algorithm is registered
/// second, its tags are in range and never go backwards, and replaying
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
	assert(strcmp(puzzle_algorithm_name(p, 1), "Layer-first (staged)") == 0);
	assert(puzzle_solve_max_moves(p) == PYRAMINX_STAGED_MAX_MOVES);
	puzzle_scramble(p, SCRAMBLE_LENGTH, 99);
	count = puzzle_solve(p, 1, out, puzzle_solve_max_moves(p), tags);
	assert(count != UINT32_MAX && count <= PYRAMINX_STAGED_MAX_MOVES);
	i = 0;
	while (i < count)
	{
		assert(tags[i] < PYRAMINX_STAGE_COUNT);
		assert(i == 0 || tags[i] >= tags[i - 1]);
		puzzle_apply_move(p, out[i].generator, out[i].amount);
		i++;
	}
	shape_init_solved(tetrahedral_shape(), solved);
	assert(memcmp(puzzle_state_ptr(p), solved, sizeof(solved)) == 0);
	puzzle_destroy(p);
}

int	main(void)
{
	test_init();
	test_solved_needs_nothing();
	test_random_scrambles();
	test_differs_from_optimal();
	test_unreachable_states_refused();
	test_max_moves_respected();
	test_public_api();
	printf("test_pyraminx_staged: all assertions passed\n");
	return (0);
}
