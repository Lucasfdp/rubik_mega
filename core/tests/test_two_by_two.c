#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "puzzle.h"
#include "puzzles/cubic.h"
#include "solve/cube_view.h"
#include "solve/kociemba.h"
#include "solve/two_by_two.h"

/// Tests for two_solve() (solve/two_by_two.h): corner-only solving of a
/// cubic n == 3 state. Same proof standard as the other three
/// algorithms' own tests, plus the two checks that actually prove the
/// "2x2x2 == corners only" design claim: a cube whose corners are
/// solved but whose edges are scrambled already counts as solved, and
/// two cubes with identical corners but different edges get
/// byte-identical solutions, because edge_perm/edge_orient are read by
/// nothing in two_solve().

static t_two_solver			g_solver;
static t_kociemba_solver		g_kociemba;

static void	build_solver(void)
{
	assert(kociemba_init(&g_kociemba));
	assert(two_solver_init(&g_solver, &g_kociemba.moves));
}

static void	free_solver(void)
{
	two_solver_free(&g_solver);
	kociemba_free(&g_kociemba);
}

static void	test_already_solved(void)
{
	int	out[TWOBYTWO_MAX_MOVES];

	build_solver();
	assert(two_solve(&g_solver, &SOLVED_SOLVE_CUBE, out) == 0);
	free_solver();
	printf("test_two_by_two: solved cube needs 0 moves -- OK\n");
}

/// Random scrambles: the solution is never longer than TWOBYTWO_MAX_MOVES
/// (the proven bound, solve/two_by_two.h), and applying it back through
/// the real puzzle_apply_move() solves the corners -- not necessarily
/// the edges, which is the whole point of a 2x2x2.
static void	test_random_scrambles(void)
{
	puzzle_t		*p;
	t_solve_cube	cube;
	int				out[TWOBYTWO_MAX_MOVES];
	uint32_t		seed;
	int				count;
	int				i;

	build_solver();
	seed = 1;
	while (seed <= 50)
	{
		p = puzzle_create(PUZZLE_CUBIC, 3);
		puzzle_scramble(p, 20, seed);
		solve_cube_from_state(&cube, puzzle_state_ptr(p));
		count = two_solve(&g_solver, &cube, out);
		assert(count >= 0 && count <= TWOBYTWO_MAX_MOVES);
		i = 0;
		while (i < count)
		{
			puzzle_apply_move(p, (int16_t)(out[i] / 3),
				(int8_t)(out[i] % 3 + 1));
			i++;
		}
		solve_cube_from_state(&cube, puzzle_state_ptr(p));
		assert(two_cube_is_solved(&cube));
		puzzle_destroy(p);
		seed++;
	}
	free_solver();
	printf("test_two_by_two: 50 seeded 20-move scrambles solve their "
		"corners, within the 11-move bound -- OK\n");
}

/// A cube whose corners are already solved but whose edges are not is
/// still solved as far as two_solve() is concerned -- a 2x2x2 has no
/// edges to be out of place.
static void	test_solved_corners_scrambled_edges(void)
{
	t_solve_cube	cube;
	int				out[TWOBYTWO_MAX_MOVES];

	build_solver();
	cube = SOLVED_SOLVE_CUBE;
	cube.edge_perm[0] = SOLVE_EDGE_UF;
	cube.edge_perm[1] = SOLVE_EDGE_UR;
	assert(two_solve(&g_solver, &cube, out) == 0);
	free_solver();
	printf("test_two_by_two: solved corners, scrambled edges -> "
		"already solved -- OK\n");
}

/// Two cubes with the SAME corners but different edges get the exact
/// same solution: edge_perm and edge_orient are never read.
static void	test_edges_are_ignored(void)
{
	t_solve_cube	a;
	t_solve_cube	b;
	int				out_a[TWOBYTWO_MAX_MOVES];
	int				out_b[TWOBYTWO_MAX_MOVES];
	int				len;

	build_solver();
	a = SOLVED_SOLVE_CUBE;
	solve_cube_apply_move(&a, SOLVE_R1);
	solve_cube_apply_move(&a, SOLVE_U1);
	solve_cube_apply_move(&a, SOLVE_F2);
	b = a;
	b.edge_perm[0] = SOLVE_EDGE_UF;
	b.edge_perm[1] = SOLVE_EDGE_UR;
	b.edge_orient[3] = 1;
	len = two_solve(&g_solver, &a, out_a);
	assert(len > 0);
	assert(two_solve(&g_solver, &b, out_b) == len);
	assert(memcmp(out_a, out_b, (size_t)len * sizeof(int)) == 0);
	free_solver();
	printf("test_two_by_two: edges are ignored -- identical corners give "
		"identical solutions -- OK\n");
}

/// A corner-orientation invariant violation is refused with -1: a 2x2x2
/// has no edges to check parity against, but the twist-sum invariant is
/// still physical -- it is what makes a state reachable at all.
static void	test_invalid_corners_refused(void)
{
	t_solve_cube	cube;
	int				out[TWOBYTWO_MAX_MOVES];

	build_solver();
	cube = SOLVED_SOLVE_CUBE;
	cube.corner_orient[0] = 1;
	assert(two_solve(&g_solver, &cube, out) == -1);
	free_solver();
	printf("test_two_by_two: invalid corner-twist cube is refused -- OK\n");
}

/// The PUBLIC API on a real 2x2x2 (n == 2): its only algorithm, id 0,
/// solves the whole puzzle (it has no edges to leave behind), and the
/// 3x3x3 no longer offers the corners-only solver -- on a 3x3x3 it left
/// the edges scrambled.
static void	test_public_algorithm_api(void)
{
	puzzle_t	*p;
	uint8_t		*solved;
	t_move		moves[TWOBYTWO_MAX_MOVES];
	uint32_t	phase_tags[TWOBYTWO_MAX_MOVES];
	uint32_t	count;
	uint32_t	seed;
	uint32_t	i;

	seed = 1;
	while (seed <= 50)
	{
		p = puzzle_create(PUZZLE_CUBIC, 2);
		assert(p != NULL);
		assert(puzzle_algorithm_count(p) == 1);
		assert(puzzle_algorithm_name(p, 0) != NULL);
		assert(puzzle_algorithm_name(p, 1) == NULL);
		assert(puzzle_solve_max_moves(p) == TWOBYTWO_MAX_MOVES);
		solved = malloc(puzzle_state_size(p));
		memcpy(solved, puzzle_state_ptr(p), puzzle_state_size(p));
		puzzle_scramble(p, 20, seed);
		count = puzzle_solve(p, 0, moves, TWOBYTWO_MAX_MOVES, phase_tags);
		assert(count != 0xFFFFFFFFu && count <= TWOBYTWO_MAX_MOVES);
		i = 0;
		while (i < count)
		{
			assert(phase_tags[i] == 0);
			puzzle_apply_move(p, moves[i].generator, moves[i].amount);
			i++;
		}
		assert(memcmp(puzzle_state_ptr(p), solved, puzzle_state_size(p)) == 0);
		free(solved);
		puzzle_destroy(p);
		seed++;
	}
	p = puzzle_create(PUZZLE_CUBIC, 3);
	assert(puzzle_algorithm_count(p) == 3);
	puzzle_destroy(p);
	printf("test_two_by_two: real 2x2x2 solves fully via puzzle_solve(); "
		"3x3x3 no longer lists it -- OK\n");
}

int	main(void)
{
	test_already_solved();
	test_random_scrambles();
	test_solved_corners_scrambled_edges();
	test_edges_are_ignored();
	test_invalid_corners_refused();
	test_public_algorithm_api();
	printf("test_two_by_two: all assertions passed\n");
	return (0);
}
