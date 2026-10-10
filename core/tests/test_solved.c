#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "puzzle.h"
#include "puzzles/cubic.h"
#include "puzzles/square1.h"

/// puzzle_is_solved() and puzzle_scramble_moves()
/// (docs/en/13-phase6-polish.md D9, increment 7).
///
/// "Solved" means every face shows one colour, not "the state equals the
/// solved state", so the interesting cases are the states that are a
/// different point in the group and still look solved: an even cube turned
/// as a whole, a Skewb in another frame. Those are checked against an
/// independent oracle where one exists (a solver that finds nothing to do),
/// and by construction where it does not (all layers of a face turned
/// together).

typedef struct s_family
{
	t_puzzle_type	type;
	uint32_t		size;
	const char		*name;
}	t_family;

static const t_family	FAMILIES[] = {
	{PUZZLE_CUBIC, 3, "cube 3"}, {PUZZLE_CUBIC, 4, "cube 4"},
	{PUZZLE_CUBIC, 5, "cube 5"}, {PUZZLE_CUBIC, 6, "cube 6"},
	{PUZZLE_CUBIC, 7, "cube 7"}, {PUZZLE_CUBIC, 8, "cube 8"},
	{PUZZLE_SKEWB, 0, "skewb"}, {PUZZLE_TETRAHEDRAL, 0, "pyraminx"},
	{PUZZLE_DODECAHEDRAL, 2, "kilominx"}, {PUZZLE_DODECAHEDRAL, 3, "megaminx"},
	{PUZZLE_SQUARE1, 0, "square-1"},
};
#define FAMILY_COUNT (sizeof(FAMILIES) / sizeof(FAMILIES[0]))

/// Cube faces by generator face number (core/include/puzzles/cubic.h).
#define FACE_R 1
#define FACE_L 4
/// How many scrambles each solver check runs, per family.
#define SOLVER_SEEDS 3
/// Deepest move sequence (and options per step) the oracle search walks.
#define ORACLE_DEPTH_SKEWB 4
#define ORACLE_DEPTH_PYRAMINX 3
/// Scramble lengths used by the scramble checks.
#define SCRAMBLE_LENGTH 25
#define SCRAMBLE_SEEDS 20

static puzzle_t	*make(const t_family *f)
{
	puzzle_t	*p = puzzle_create(f->type, f->size);

	assert(p != NULL);
	return (p);
}

static int	same_state(const puzzle_t *a, const puzzle_t *b)
{
	return (puzzle_state_size(a) == puzzle_state_size(b)
		&& memcmp(puzzle_state_ptr(a), puzzle_state_ptr(b),
			puzzle_state_size(a)) == 0);
}

static void	apply_list(puzzle_t *p, const t_move *moves, uint32_t count)
{
	uint32_t	i = 0;

	while (i < count)
	{
		puzzle_apply_move(p, moves[i].generator, moves[i].amount);
		i++;
	}
}

/// A fresh puzzle is solved; so is any puzzle after a move and its inverse;
/// and no single move from solved leaves it solved.
static void	test_basics(void)
{
	uint32_t	f;
	uint32_t	g;
	uint32_t	a;
	uint32_t	steps;
	puzzle_t	*p;

	for (f = 0; f < FAMILY_COUNT; f++)
	{
		p = make(&FAMILIES[f]);
		assert(puzzle_is_solved(p) == 1);
		for (g = 0; g < puzzle_generator_count(p); g++)
		{
			steps = puzzle_generator_steps(p, g);
			for (a = 1; a <= steps; a++)
			{
				puzzle_apply_move(p, (int16_t)g, (int8_t)a);
				assert(puzzle_is_solved(p) == 0);
				puzzle_apply_move(p, (int16_t)g, (int8_t)(steps + 1 - a));
				assert(puzzle_is_solved(p) == 1);
			}
		}
		puzzle_destroy(p);
	}
	assert(puzzle_is_solved(NULL) == 0);
	printf("  basics: every family solved fresh, unsolved after any one move\n");
}

/// Turning every layer along one axis together is a whole-cube rotation: a
/// different state, and a solved-looking one. Needs centres that can
/// move, so n >= 4 (a 3x3x3's centres are fixed and not part of its state).
/// A face's generators reach only the half of the layers nearest it, so the
/// R generators turn one half clockwise and the L generators the other
/// half the same way round (an L turn is the opposite sense, hence amount
/// 3); an odd cube's middle layer belongs to both and is turned once.
static void	test_whole_cube_rotation(void)
{
	uint32_t	n;
	uint32_t	g;
	puzzle_t	*p;
	puzzle_t	*fresh;
	uint32_t	turned;

	for (n = 4; n <= 8; n++)
	{
		p = puzzle_create(PUZZLE_CUBIC, n);
		fresh = puzzle_create(PUZZLE_CUBIC, n);
		turned = 0;
		for (g = 0; g < puzzle_generator_count(p); g++)
		{
			if (puzzle_generator_face(p, g) == FACE_R)
				puzzle_apply_move(p, (int16_t)g, 1);
			else if (puzzle_generator_face(p, g) == FACE_L
				&& !(n % 2 == 1 && puzzle_generator_depth(p, g)
					== (int32_t)((n - 1) / 2)))
				puzzle_apply_move(p, (int16_t)g, 3);
			else
				continue ;
			turned++;
		}
		assert(turned == n);   // every layer turned exactly once
		assert(!same_state(p, fresh));
		assert(puzzle_is_solved(p) == 1);
		puzzle_apply_move(p, 0, 1);   // one more turn spoils it
		assert(puzzle_is_solved(p) == 0);
		puzzle_destroy(p);
		puzzle_destroy(fresh);
	}
	printf("  even and odd cubes 4..8: a whole-cube rotation counts as solved\n");
}


/// How many moves algorithm 0 needs to solve the puzzle's current state.
static uint32_t	solver_moves(puzzle_t *p)
{
	static t_move	out[8192];
	static uint32_t	tags[8192];
	uint32_t		count;

	assert(puzzle_solve_max_moves(p) <= 8192);
	count = puzzle_solve(p, 0, out, 8192, tags);
	assert(count != 0xFFFFFFFFu);
	return (count);
}

static uint32_t	g_oracle_checked;
static uint32_t	g_oracle_rotated;

/// Walks every move sequence up to `depth`, and at each state checks that
/// "looks solved" agrees with "the solver has nothing to do". Counts the
/// solved-looking states that are not the identity: those are the ones a
/// state comparison would get wrong.
static void	oracle_walk(puzzle_t *p, const puzzle_t *fresh, uint32_t depth)
{
	uint32_t	g;
	uint32_t	a;
	uint32_t	steps;
	uint8_t		saved[64];
	uint32_t	size = puzzle_state_size(p);

	assert(size <= sizeof(saved));
	g_oracle_checked++;
	if (puzzle_is_solved(p))
	{
		assert(solver_moves(p) == 0);
		if (!same_state(p, fresh))
			g_oracle_rotated++;
	}
	else
		assert(solver_moves(p) != 0);
	if (depth == 0)
		return ;
	for (g = 0; g < puzzle_generator_count(p); g++)
	{
		steps = puzzle_generator_steps(p, g);
		for (a = 1; a <= steps; a++)
		{
			memcpy(saved, puzzle_state_ptr(p), size);
			puzzle_apply_move(p, (int16_t)g, (int8_t)a);
			oracle_walk(p, fresh, depth - 1);
			puzzle_apply_move(p, (int16_t)g, (int8_t)(steps + 1 - a));
			assert(memcmp(saved, puzzle_state_ptr(p), size) == 0);
		}
	}
}

/// Skewb and Pyraminx: the solver is an independent judge of "already
/// solved" (the Skewb one even works out which of the 12 whole-puzzle
/// rotations the puzzle is in), so is_solved() must agree with it on every
/// state a short walk reaches.
static void	test_oracle(void)
{
	puzzle_t	*p;
	puzzle_t	*fresh;

	g_oracle_checked = 0;
	g_oracle_rotated = 0;
	p = puzzle_create(PUZZLE_SKEWB, 0);
	fresh = puzzle_create(PUZZLE_SKEWB, 0);
	oracle_walk(p, fresh, ORACLE_DEPTH_SKEWB);
	printf("  skewb: agrees with the solver on %u states, %u of them solved-looking rotations\n",
		g_oracle_checked, g_oracle_rotated);
	assert(g_oracle_rotated > 0);   // the case a state comparison gets wrong
	puzzle_destroy(p);
	puzzle_destroy(fresh);
	g_oracle_checked = 0;
	g_oracle_rotated = 0;
	p = puzzle_create(PUZZLE_TETRAHEDRAL, 0);
	fresh = puzzle_create(PUZZLE_TETRAHEDRAL, 0);
	oracle_walk(p, fresh, ORACLE_DEPTH_PYRAMINX);
	printf("  pyraminx: agrees with the solver on %u states\n", g_oracle_checked);
	puzzle_destroy(p);
	puzzle_destroy(fresh);
}

/// Every family: scramble, apply what the solver says, and the puzzle
/// looks solved -- including the even cubes, where the reduction may finish
/// with the whole cube turned.
static void	test_solver_endpoints(void)
{
	static t_move	out[8192];
	static uint32_t	tags[8192];
	uint32_t		f;
	uint32_t		seed;
	uint32_t		algo;
	uint32_t		count;
	puzzle_t		*p;

	for (f = 0; f < FAMILY_COUNT; f++)
	{
		/// The big cubes' reductions are slow for what they add here.
		if (FAMILIES[f].type == PUZZLE_CUBIC && FAMILIES[f].size > 5)
			continue ;
		p = make(&FAMILIES[f]);
		for (algo = 0; algo < puzzle_algorithm_count(p); algo++)
		{
			/// The 2x2x2 method solves only the corners, on purpose.
			if (strncmp(puzzle_algorithm_name(p, algo), "2x2x2", 5) == 0)
				continue ;
			for (seed = 1; seed <= SOLVER_SEEDS; seed++)
			{
				puzzle_scramble(p, SCRAMBLE_LENGTH, seed * 7919u);
				assert(puzzle_is_solved(p) == 0);
				count = puzzle_solve(p, algo, out, 8192, tags);
				assert(count != 0xFFFFFFFFu && count > 0);
				apply_list(p, out, count);
				if (!puzzle_is_solved(p))
					printf("  FAIL: %s algorithm %u (%s) seed %u\n",
						FAMILIES[f].name, algo, puzzle_algorithm_name(p, algo),
						seed);
				assert(puzzle_is_solved(p) == 1);
			}
		}
		puzzle_destroy(p);
	}
	printf("  solvers: every algorithm ends on a solved-looking puzzle\n");
}

/// The animated scramble must end where the instant one does, from the
/// solved state and from a state already scrambled (Square-1's moves
/// depend on the state they start from).
static void	test_scramble_moves(void)
{
	static t_move	list[4096];
	uint32_t		f;
	uint32_t		seed;
	uint32_t		count;
	uint32_t		bound;
	puzzle_t		*instant;
	puzzle_t		*animated;

	for (f = 0; f < FAMILY_COUNT; f++)
	{
		instant = make(&FAMILIES[f]);
		animated = make(&FAMILIES[f]);
		for (seed = 1; seed <= SCRAMBLE_SEEDS; seed++)
		{
			bound = puzzle_scramble_max_moves(animated, SCRAMBLE_LENGTH);
			assert(bound <= 4096);
			count = puzzle_scramble_moves(animated, list, 4096,
					SCRAMBLE_LENGTH, seed);
			assert(count <= bound && count > 0);
			apply_list(animated, list, count);
			puzzle_scramble(instant, SCRAMBLE_LENGTH, seed);
			assert(same_state(instant, animated));
		}
		/// A short buffer takes the first moves and still reports the whole
		/// length, so a caller can tell it was cut off.
		count = puzzle_scramble_moves(animated, list, 2, SCRAMBLE_LENGTH, 99);
		assert(count >= 2);
		puzzle_destroy(instant);
		puzzle_destroy(animated);
	}
	assert(puzzle_scramble_moves(NULL, list, 4096, 5, 1) == 0);
	printf("  scramble_moves: applying the list equals puzzle_scramble(), all families\n");
}

/// A move list never contains a move the puzzle would refuse.
static void	test_scramble_moves_are_legal(void)
{
	static t_move	list[4096];
	uint32_t		f;
	uint32_t		i;
	uint32_t		count;
	puzzle_t		*p;

	for (f = 0; f < FAMILY_COUNT; f++)
	{
		p = make(&FAMILIES[f]);
		count = puzzle_scramble_moves(p, list, 4096, SCRAMBLE_LENGTH, 4242);
		for (i = 0; i < count; i++)
		{
			assert(list[i].generator >= 0
				&& (uint32_t)list[i].generator < puzzle_generator_count(p));
			assert(list[i].amount >= 1
				&& (uint32_t)list[i].amount
				<= puzzle_generator_steps(p, (uint32_t)list[i].generator));
			assert(puzzle_move_is_legal(p, list[i].generator, list[i].amount));
			puzzle_apply_move(p, list[i].generator, list[i].amount);
		}
		puzzle_destroy(p);
	}
	printf("  scramble_moves: every move is in range and legal where it is played\n");
}

int	main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);   // a failed assert should not eat the log
	printf("test_solved\n");
	test_basics();
	test_whole_cube_rotation();
	test_oracle();
	test_solver_endpoints();
	test_scramble_moves();
	test_scramble_moves_are_legal();
	printf("test_solved: ok\n");
	return (0);
}
