#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "puzzle.h"
#include "puzzles/cubic.h"
#include "solve/cube_view.h"
#include "solve/kociemba.h"

/// End-to-end checks for the ported Kociemba solver
/// (docs/en/06-phase2-solvers.md). The real proof isn't "does it return
/// SOME move list" -- it's "does applying that move list back to the
/// scrambled puzzle, through puzzle_apply_move() (the same turning code
/// the renderer will use), actually reach solved." That exercises
/// encode/decode, the move tables, the pruning tables and the IDA*
/// search all at once, against the one thing that matters: the real
/// cubic-family engine, not a second copy of it.

static void	apply_kociemba_moves(puzzle_t *p, const int *moves, int count)
{
	int	i;

	i = 0;
	while (i < count)
	{
		puzzle_apply_move(p, (int16_t)(moves[i] / 3),
			(int8_t)(moves[i] % 3 + 1));
		i++;
	}
}

static void	test_solved_cube_needs_no_moves(void)
{
	t_kociemba_solver	solver;
	puzzle_t			*p;
	t_solve_cube		cube;
	int					moves[KOCIEMBA_MAX_MOVES];
	int					phase1;
	int					count;

	assert(kociemba_init(&solver));
	p = puzzle_create(PUZZLE_CUBIC, 3);
	solve_cube_from_state(&cube, puzzle_state_ptr(p));
	count = kociemba_solve(&solver, &cube, moves, &phase1);
	assert(count == 0);
	puzzle_destroy(p);
	kociemba_free(&solver);
	printf("test_kociemba: solved cube needs 0 moves -- OK\n");
}

static void	test_sexy_move_scramble_solves(void)
{
	t_kociemba_solver	solver;
	puzzle_t			*p;
	uint8_t				*solved;
	t_solve_cube		cube;
	int					moves[KOCIEMBA_MAX_MOVES];
	int					phase1;
	int					count;
	int					i;

	assert(kociemba_init(&solver));
	p = puzzle_create(PUZZLE_CUBIC, 3);
	solved = malloc(puzzle_state_size(p));
	memcpy(solved, puzzle_state_ptr(p), puzzle_state_size(p));
	i = 0;
	while (i < 5)
	{
		puzzle_apply_move(p, CUBIC_GEN_R, 1);
		puzzle_apply_move(p, CUBIC_GEN_U, 1);
		puzzle_apply_move(p, CUBIC_GEN_R, 3);
		puzzle_apply_move(p, CUBIC_GEN_U, 3);
		i++;
	}
	assert(memcmp(puzzle_state_ptr(p), solved, puzzle_state_size(p)) != 0);
	solve_cube_from_state(&cube, puzzle_state_ptr(p));
	count = kociemba_solve(&solver, &cube, moves, &phase1);
	assert(count >= 0);
	assert(phase1 >= 0 && phase1 <= count);
	apply_kociemba_moves(p, moves, count);
	assert(memcmp(puzzle_state_ptr(p), solved, puzzle_state_size(p)) == 0);
	free(solved);
	puzzle_destroy(p);
	kociemba_free(&solver);
	printf("test_kociemba: (R U R' U')^5 scramble solves -- OK\n");
}

/// Several seeded scrambles (puzzle_scramble() -- same reproducible
/// xorshift32 PRNG every scramble in this project already uses), each
/// solved and the solution applied back, checked against solved.
static void	test_seeded_scrambles_solve(void)
{
	t_kociemba_solver	solver;
	puzzle_t			*p;
	uint8_t				*solved;
	t_solve_cube		cube;
	int					moves[KOCIEMBA_MAX_MOVES];
	int					phase1;
	int					count;
	uint32_t			seed;

	assert(kociemba_init(&solver));
	seed = 1;
	while (seed <= 12)
	{
		p = puzzle_create(PUZZLE_CUBIC, 3);
		solved = malloc(puzzle_state_size(p));
		memcpy(solved, puzzle_state_ptr(p), puzzle_state_size(p));
		puzzle_scramble(p, 25, seed);
		solve_cube_from_state(&cube, puzzle_state_ptr(p));
		assert(solve_cube_is_valid(&cube));
		count = kociemba_solve(&solver, &cube, moves, &phase1);
		assert(count >= 0);
		assert(count <= KOCIEMBA_MAX_MOVES);
		apply_kociemba_moves(p, moves, count);
		assert(memcmp(puzzle_state_ptr(p), solved, puzzle_state_size(p))
			== 0);
		free(solved);
		puzzle_destroy(p);
		seed++;
	}
	kociemba_free(&solver);
	printf("test_kociemba: 12 seeded 25-move scrambles all solve -- OK\n");
}

/// Round-tripping every coordinate's own encode(decode(n)) == n, for a
/// spread of values (not just 0) -- catches a Lehmer-code or
/// binomial-table transcription slip that a full solve could still
/// mask (a wrong-but-consistent table can still let IDA* find SOME
/// working path through it).
static void	test_coordinate_round_trip(void)
{
	t_solve_cube	cube;
	uint16_t		values[5];
	int				i;

	values[0] = 0;
	values[1] = 1;
	values[2] = 17;
	values[3] = 100;
	values[4] = 2000;
	i = 0;
	while (i < 5)
	{
		cube = SOLVED_SOLVE_CUBE;
		decode_twist(&cube, values[i] % TWIST_COUNT);
		assert(encode_twist(&cube) == values[i] % TWIST_COUNT);
		cube = SOLVED_SOLVE_CUBE;
		decode_flip(&cube, values[i] % FLIP_COUNT);
		assert(encode_flip(&cube) == values[i] % FLIP_COUNT);
		cube = SOLVED_SOLVE_CUBE;
		decode_slice(&cube, values[i] % SLICE_COUNT);
		assert(encode_slice(&cube) == values[i] % SLICE_COUNT);
		cube = SOLVED_SOLVE_CUBE;
		decode_cperm(&cube, values[i] % CPERM_COUNT);
		assert(encode_cperm(&cube) == values[i] % CPERM_COUNT);
		cube = SOLVED_SOLVE_CUBE;
		decode_slice(&cube, 0);
		decode_eperm(&cube, values[i] % EPERM_COUNT);
		assert(encode_eperm(&cube) == values[i] % EPERM_COUNT);
		cube = SOLVED_SOLVE_CUBE;
		decode_slice(&cube, 0);
		decode_sperm(&cube, values[i] % SPERM_COUNT);
		assert(encode_sperm(&cube) == values[i] % SPERM_COUNT);
		i++;
	}
	printf("test_kociemba: coordinate encode(decode(n)) == n -- OK\n");
}

/// The PUBLIC API (puzzle_algorithm_count/name/puzzle_solve), not the
/// internal kociemba_solve() the tests above exercise directly -- this
/// is what web/ will actually call. Checks the registry only exists for
/// (PUZZLE_CUBIC, 3), and that going through puzzle_solve() solves a
/// scrambled puzzle exactly like the direct kociemba_solve() path did.
static void	test_public_algorithm_api(void)
{
	puzzle_t	*p3;
	puzzle_t	*p5;
	uint8_t		*solved;
	t_move		moves[64];
	uint32_t	phase_tags[64];
	uint32_t	count;
	uint32_t	i;

	p3 = puzzle_create(PUZZLE_CUBIC, 3);
	assert(puzzle_algorithm_count(p3) == 4);
	assert(puzzle_algorithm_name(p3, 0) != NULL);
	assert(puzzle_algorithm_name(p3, 1) != NULL);
	assert(puzzle_algorithm_name(p3, 2) != NULL);
	assert(puzzle_algorithm_name(p3, 3) != NULL);
	assert(puzzle_algorithm_name(p3, 4) == NULL);
	solved = malloc(puzzle_state_size(p3));
	memcpy(solved, puzzle_state_ptr(p3), puzzle_state_size(p3));
	puzzle_scramble(p3, 20, 42);
	count = puzzle_solve(p3, 0, moves, 64, phase_tags);
	assert(count != 0xFFFFFFFFu && count <= 64);
	i = 0;
	while (i < count)
	{
		assert(phase_tags[i] == 0 || phase_tags[i] == 1);
		puzzle_apply_move(p3, moves[i].generator, moves[i].amount);
		i++;
	}
	assert(memcmp(puzzle_state_ptr(p3), solved, puzzle_state_size(p3)) == 0);
	free(solved);
	puzzle_destroy(p3);
	// n == 5 has its own registry since docs/en/08-nxn-reduction.md: one
	// algorithm (reduction), none of the 3x3x3 ones -- test_reduction.c
	// covers it in full; this only pins the n == 3 list not leaking over.
	p5 = puzzle_create(PUZZLE_CUBIC, 5);
	assert(puzzle_algorithm_count(p5) == 1);
	assert(puzzle_algorithm_name(p5, 1) == NULL);
	assert(puzzle_solve(p5, 1, moves, 64, phase_tags) == 0xFFFFFFFFu);
	puzzle_destroy(p5);
	printf("test_kociemba: public puzzle_solve() API, n==3 list is n==3's own -- OK\n");
}

int	main(void)
{
	test_coordinate_round_trip();
	test_solved_cube_needs_no_moves();
	test_sexy_move_scramble_solves();
	test_seeded_scrambles_solve();
	test_public_algorithm_api();
	printf("test_kociemba: all assertions passed\n");
	return (0);
}
