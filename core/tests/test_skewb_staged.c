#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "puzzle.h"
#include "solve/skewb_staged.h"

/// Staged Skewb solver (docs/en/10-phase3-human-methods.md increment 5):
/// the exit gate's 200 scrambles over ALL EIGHT turns, each phase's
/// promise asserted at its own boundary, and the registry wiring.
///
/// Phase promises live in the canonical frame (D3), but the solver's
/// moves are renamed to act on the real puzzle. So the test replays the
/// REAL moves on the real state and, at each boundary, re-frames that
/// state with the rotation the START state chose: by the algebra in
/// skewb_solve()'s comment, the result is exactly the state the framed
/// solve had reached, where the promise can be read off directly.

#define SCRAMBLE_COUNT 200
#define SCRAMBLE_LENGTH 40

static t_skewb_solver	g_base;
static t_skewb_staged	g_staged;

static void	apply_word(uint8_t *state, const t_move *word, uint32_t length)
{
	uint32_t	i;

	i = 0;
	while (i < length)
		shape_apply_move(skewb_shape(), skewb_generators(), state, word[i++]);
}

static int	piece_home(const uint8_t *state, uint32_t type, uint32_t piece)
{
	uint32_t	perm_offset;
	uint32_t	orient_offset;

	shape_piece_offsets(skewb_shape(), type, &perm_offset, &orient_offset);
	return (state[perm_offset + piece] == piece
		&& state[orient_offset + piece] == 0);
}

/// The four corners on the +y face: those whose y bit is clear.
static int	top_corners_home(const uint8_t *framed)
{
	uint32_t	corner;

	corner = 0;
	while (corner < SKEWB_CORNER_COUNT)
	{
		if (!(corner & SKEWB_CORNER_Y_BIT)
			&& !piece_home(framed, SKEWB_CORNER_TYPE, corner))
			return (0);
		corner++;
	}
	return (1);
}

static void	test_init(void)
{
	assert(skewb_solver_init(&g_base));
	assert(skewb_staged_init(&g_staged, &g_base));
	assert(SKEWB_STAGED_MAX_MOVES >= SKEWB_MAX_MOVES);
}

/// A solved puzzle, and each of the 11 rotations that merely look
/// solved, need no moves.
static void	test_solved_needs_nothing(void)
{
	uint8_t		state[SKEWB_STATE_BYTES];
	t_move		out[SKEWB_STAGED_MAX_MOVES];
	uint32_t	ends[SKEWB_STAGE_COUNT];
	uint32_t	i;

	i = 0;
	while (i < g_base.rotation_count)
	{
		shape_init_solved(skewb_shape(), state);
		apply_word(state, g_base.rotations[i].word,
			g_base.rotations[i].length);
		assert(skewb_staged_solve(&g_staged, state, out,
				SKEWB_STAGED_MAX_MOVES, ends) == 0);
		i++;
	}
}

/// Replays the real moves and checks each stage's promise at its end.
static void	check_phases(const uint8_t *start, const t_move *out, int count,
	const uint32_t *ends)
{
	uint8_t		state[SKEWB_STATE_BYTES];
	uint8_t		framed[SKEWB_STATE_BYTES];
	uint8_t		solved[SKEWB_STATE_BYTES];
	int			frame;
	uint32_t	stage;
	int			i;

	frame = skewb_find_frame(&g_base, start, framed);
	assert(frame >= 0);
	memcpy(state, start, sizeof(state));
	shape_init_solved(skewb_shape(), solved);
	i = 0;
	stage = 0;
	while (stage < SKEWB_STAGE_COUNT)
	{
		assert(ends[stage] <= (uint32_t)count);
		assert(stage == 0 || ends[stage] >= ends[stage - 1]);
		while (i < (int)ends[stage])
			shape_apply_move(skewb_shape(), skewb_generators(), state,
				out[i++]);
		memcpy(framed, state, sizeof(framed));
		apply_word(framed, g_base.rotations[frame].word,
			g_base.rotations[frame].length);
		assert(top_corners_home(framed));
		assert(stage < 1 || piece_home(framed, SKEWB_CENTRE_TYPE,
				SKEWB_CENTRE_TOP));
		assert(stage < 2 || memcmp(framed, solved, sizeof(framed)) == 0);
		stage++;
	}
	assert(ends[SKEWB_STAGE_COUNT - 1] == (uint32_t)count);
	assert(skewb_state_is_uniform(state));
}

/// The exit gate: 200 scrambles built from all eight turns (so most
/// start outside the identity frame) solve to uniform faces within the
/// proven cap, every phase keeping its promise.
static void	test_random_scrambles(void)
{
	puzzle_t	*p;
	t_move		out[SKEWB_STAGED_MAX_MOVES];
	uint32_t	ends[SKEWB_STAGE_COUNT];
	int			count;
	uint32_t	seed;
	uint32_t	framed;

	framed = 0;
	seed = 1;
	while (seed <= SCRAMBLE_COUNT)
	{
		p = puzzle_create(PUZZLE_SKEWB, 0);
		puzzle_scramble(p, SCRAMBLE_LENGTH, seed * 2654435761u);
		framed += !skewb_state_is_uniform(puzzle_state_ptr(p));
		count = skewb_staged_solve(&g_staged, puzzle_state_ptr(p), out,
				SKEWB_STAGED_MAX_MOVES, ends);
		assert(count >= 0 && count <= (int)SKEWB_STAGED_MAX_MOVES);
		check_phases(puzzle_state_ptr(p), out, count, ends);
		puzzle_destroy(p);
		seed++;
	}
	assert(framed == SCRAMBLE_COUNT);
}

/// A staged solve is not just the optimal one relabelled: over the same
/// scrambles it is longer on average (measured: about 17 moves against
/// about 8).
static void	test_differs_from_optimal(void)
{
	puzzle_t	*p;
	t_move		out[SKEWB_STAGED_MAX_MOVES];
	uint32_t	ends[SKEWB_STAGE_COUNT];
	uint32_t	staged_total;
	uint32_t	optimal_total;
	uint32_t	seed;

	staged_total = 0;
	optimal_total = 0;
	seed = 1;
	while (seed <= SCRAMBLE_COUNT)
	{
		p = puzzle_create(PUZZLE_SKEWB, 0);
		puzzle_scramble(p, SCRAMBLE_LENGTH, seed++ * 2654435761u);
		staged_total += (uint32_t)skewb_staged_solve(&g_staged,
				puzzle_state_ptr(p), out, SKEWB_STAGED_MAX_MOVES, ends);
		optimal_total += (uint32_t)skewb_solve(&g_base, puzzle_state_ptr(p),
				out, SKEWB_MAX_MOVES);
		puzzle_destroy(p);
	}
	assert(staged_total > optimal_total);
}

/// A state no turns can reach is refused, not "solved" with garbage.
static void	test_unreachable_states_refused(void)
{
	uint8_t		state[SKEWB_STATE_BYTES];
	t_move		out[SKEWB_STAGED_MAX_MOVES];
	uint32_t	ends[SKEWB_STAGE_COUNT];
	uint32_t	perm_offset;
	uint32_t	orient_offset;
	uint8_t		swap;

	shape_piece_offsets(skewb_shape(), SKEWB_CORNER_TYPE, &perm_offset,
		&orient_offset);
	shape_init_solved(skewb_shape(), state);
	state[orient_offset] = 1;
	assert(skewb_staged_solve(&g_staged, state, out, SKEWB_STAGED_MAX_MOVES,
			ends) == -1);
	shape_init_solved(skewb_shape(), state);
	swap = state[perm_offset];
	state[perm_offset] = state[perm_offset + 1];
	state[perm_offset + 1] = swap;
	assert(skewb_staged_solve(&g_staged, state, out, SKEWB_STAGED_MAX_MOVES,
			ends) == -1);
}

/// Too small a `max_moves` is refused, never overrun.
static void	test_max_moves_respected(void)
{
	puzzle_t	*p;
	t_move		out[SKEWB_STAGED_MAX_MOVES];
	uint32_t	ends[SKEWB_STAGE_COUNT];

	p = puzzle_create(PUZZLE_SKEWB, 0);
	puzzle_scramble(p, SCRAMBLE_LENGTH, 5);
	assert(skewb_staged_solve(&g_staged, puzzle_state_ptr(p), out, 1, ends)
		== -1);
	puzzle_destroy(p);
}

/// The PUBLIC API (what web/ calls): the staged algorithm is registered
/// second, its tags are in range and never go backwards, and replaying
/// through puzzle_apply_move() leaves uniform faces.
static void	test_public_api(void)
{
	puzzle_t	*p;
	t_move		out[SKEWB_STAGED_MAX_MOVES];
	uint32_t	tags[SKEWB_STAGED_MAX_MOVES];
	uint32_t	count;
	uint32_t	i;

	p = puzzle_create(PUZZLE_SKEWB, 0);
	assert(puzzle_algorithm_count(p) == 2);
	assert(strcmp(puzzle_algorithm_name(p, 1), "Face-first (staged)") == 0);
	assert(puzzle_solve_max_moves(p) == SKEWB_STAGED_MAX_MOVES);
	puzzle_scramble(p, SCRAMBLE_LENGTH, 99);
	count = puzzle_solve(p, 1, out, puzzle_solve_max_moves(p), tags);
	assert(count != UINT32_MAX && count <= SKEWB_STAGED_MAX_MOVES);
	i = 0;
	while (i < count)
	{
		assert(tags[i] < SKEWB_STAGE_COUNT);
		assert(i == 0 || tags[i] >= tags[i - 1]);
		puzzle_apply_move(p, out[i].generator, out[i].amount);
		i++;
	}
	assert(skewb_state_is_uniform(puzzle_state_ptr(p)));
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
	printf("test_skewb_staged: all assertions passed\n");
	return (0);
}
