#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "puzzle.h"
#include "puzzles/cubic.h"
#include "solve/cube_view.h"
#include "solve/kociemba.h"
#include "solve/layer.h"

/// End-to-end checks for the ported layer-by-layer (beginner) solver
/// (docs/en/06-phase2-solvers.md), same proof standard as
/// test_kociemba.c/test_thistlethwaite.c: applying the returned moves
/// back through puzzle_apply_move() must land on solved. No move-count
/// upper bound is asserted beyond LAYER_MAX_MOVES itself -- unlike
/// Kociemba's ~23 or Thistlethwaite's ~40-45, the beginner method's
/// count is not tightly bounded by design (solve/layer.h).

static t_kociemba_solver	g_kociemba;

static void	build_kociemba(void)
{
	assert(kociemba_init(&g_kociemba));
}

static void	free_kociemba(void)
{
	kociemba_free(&g_kociemba);
}

static void	apply_layer_moves(puzzle_t *p, const int *moves, int count)
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
	puzzle_t		*p;
	t_solve_cube	cube;
	int				moves[LAYER_MAX_MOVES];
	int				phase_bounds[4];
	int				count;

	build_kociemba();
	p = puzzle_create(PUZZLE_CUBIC, 3);
	solve_cube_from_state(&cube, puzzle_state_ptr(p));
	count = layer_solve(&cube, &g_kociemba, moves, phase_bounds);
	assert(count == 0);
	assert(phase_bounds[3] == 0);
	puzzle_destroy(p);
	free_kociemba();
	printf("test_layer: solved cube needs 0 moves -- OK\n");
}

static void	test_invalid_cube_is_refused(void)
{
	t_solve_cube	cube;
	int				moves[LAYER_MAX_MOVES];
	int				phase_bounds[4];

	build_kociemba();
	cube = SOLVED_SOLVE_CUBE;
	cube.corner_orient[0] = 1;
	assert(layer_solve(&cube, &g_kociemba, moves, phase_bounds) == -1);
	free_kociemba();
	printf("test_layer: invalid cube is refused -- OK\n");
}

static void	test_sexy_move_scramble_solves(void)
{
	puzzle_t		*p;
	uint8_t			*solved;
	t_solve_cube	cube;
	int				moves[LAYER_MAX_MOVES];
	int				phase_bounds[4];
	int				count;
	int				i;

	build_kociemba();
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
	count = layer_solve(&cube, &g_kociemba, moves, phase_bounds);
	assert(count >= 0);
	assert(phase_bounds[3] == count);
	apply_layer_moves(p, moves, count);
	assert(memcmp(puzzle_state_ptr(p), solved, puzzle_state_size(p)) == 0);
	free(solved);
	puzzle_destroy(p);
	free_kociemba();
	printf("test_layer: (R U R' U')^5 scramble solves -- OK\n");
}

/// Several seeded scrambles, each solved and the solution applied back,
/// checked against solved -- the same reproducible xorshift32 PRNG every
/// scramble in this project already uses. A wider spread of seeds than
/// the other two algorithms' own tests: with no formal coordinate goal
/// to exercise deliberately (unlike Thistlethwaite's G3 coset), more
/// random scrambles is what actually exercises every corner-twist (0/1/2)
/// and edge-flip (0/1) branch each stage's lookup tables index by.
static void	test_seeded_scrambles_solve(void)
{
	puzzle_t		*p;
	uint8_t			*solved;
	t_solve_cube	cube;
	int				moves[LAYER_MAX_MOVES];
	int				phase_bounds[4];
	int				count;
	uint32_t		seed;

	build_kociemba();
	seed = 1;
	while (seed <= 30)
	{
		p = puzzle_create(PUZZLE_CUBIC, 3);
		solved = malloc(puzzle_state_size(p));
		memcpy(solved, puzzle_state_ptr(p), puzzle_state_size(p));
		puzzle_scramble(p, 25, seed);
		solve_cube_from_state(&cube, puzzle_state_ptr(p));
		assert(solve_cube_is_valid(&cube));
		count = layer_solve(&cube, &g_kociemba, moves, phase_bounds);
		assert(count >= 0);
		assert(count <= LAYER_MAX_MOVES);
		apply_layer_moves(p, moves, count);
		assert(memcmp(puzzle_state_ptr(p), solved, puzzle_state_size(p))
			== 0);
		free(solved);
		puzzle_destroy(p);
		seed++;
	}
	free_kociemba();
	printf("test_layer: 30 seeded 25-move scrambles all solve -- OK\n");
}

/// The check unique to a staged method: after each stage's own moves are
/// applied, the piece group that stage promised is actually placed and
/// oriented -- not just "the overall solve worked", which a stage that
/// quietly left its job to a later, bigger search could still pass by
/// accident.
static void	test_stage_boundaries_place_their_pieces(void)
{
	puzzle_t		*p;
	t_solve_cube	cube;
	int				moves[LAYER_MAX_MOVES];
	int				phase_bounds[4];
	int				count;
	uint32_t		seed;
	int				i;

	build_kociemba();
	seed = 1;
	while (seed <= 10)
	{
		p = puzzle_create(PUZZLE_CUBIC, 3);
		puzzle_scramble(p, 25, seed);
		solve_cube_from_state(&cube, puzzle_state_ptr(p));
		count = layer_solve(&cube, &g_kociemba, moves, phase_bounds);
		assert(count >= 0);
		apply_layer_moves(p, moves, phase_bounds[0]);
		solve_cube_from_state(&cube, puzzle_state_ptr(p));
		i = 0;
		while (i < 4)
		{
			assert(cube.edge_perm[SOLVE_EDGE_DR + i] == SOLVE_EDGE_DR + i);
			assert(cube.edge_orient[SOLVE_EDGE_DR + i] == 0);
			i++;
		}
		apply_layer_moves(p, moves + phase_bounds[0],
			phase_bounds[1] - phase_bounds[0]);
		solve_cube_from_state(&cube, puzzle_state_ptr(p));
		i = 0;
		while (i < 4)
		{
			assert(cube.corner_perm[SOLVE_CORNER_DFR + i]
				== SOLVE_CORNER_DFR + i);
			assert(cube.corner_orient[SOLVE_CORNER_DFR + i] == 0);
			i++;
		}
		apply_layer_moves(p, moves + phase_bounds[1],
			phase_bounds[2] - phase_bounds[1]);
		solve_cube_from_state(&cube, puzzle_state_ptr(p));
		i = 0;
		while (i < 4)
		{
			assert(cube.edge_perm[SOLVE_EDGE_FR + i] == SOLVE_EDGE_FR + i);
			assert(cube.edge_orient[SOLVE_EDGE_FR + i] == 0);
			i++;
		}
		apply_layer_moves(p, moves + phase_bounds[2],
			phase_bounds[3] - phase_bounds[2]);
		assert(puzzle_algorithm_count(p) == 4);
		puzzle_destroy(p);
		seed++;
	}
	free_kociemba();
	printf("test_layer: each stage places its own pieces, "
		"earlier stages survive -- OK\n");
}

/// The PUBLIC API, algorithm id 2 specifically (ids 0/1 already covered
/// by test_kociemba.c/test_thistlethwaite.c's own tests).
static void	test_public_algorithm_api(void)
{
	puzzle_t	*p;
	uint8_t		*solved;
	t_move		moves[LAYER_MAX_MOVES];
	uint32_t	phase_tags[LAYER_MAX_MOVES];
	uint32_t	count;
	uint32_t	i;
	uint32_t	seen_phase;

	p = puzzle_create(PUZZLE_CUBIC, 3);
	assert(puzzle_algorithm_count(p) == 4);
	assert(puzzle_algorithm_name(p, 2) != NULL);
	solved = malloc(puzzle_state_size(p));
	memcpy(solved, puzzle_state_ptr(p), puzzle_state_size(p));
	puzzle_scramble(p, 20, 42);
	count = puzzle_solve(p, 2, moves, LAYER_MAX_MOVES, phase_tags);
	assert(count != 0xFFFFFFFFu && count <= LAYER_MAX_MOVES);
	seen_phase = 0;
	i = 0;
	while (i < count)
	{
		assert(phase_tags[i] <= 3);
		assert(phase_tags[i] >= seen_phase);
		seen_phase = phase_tags[i];
		puzzle_apply_move(p, moves[i].generator, moves[i].amount);
		i++;
	}
	assert(memcmp(puzzle_state_ptr(p), solved, puzzle_state_size(p)) == 0);
	free(solved);
	puzzle_destroy(p);
	printf("test_layer: public puzzle_solve() API, algo 2 -- OK\n");
}

int	main(void)
{
	test_solved_cube_needs_no_moves();
	test_invalid_cube_is_refused();
	test_sexy_move_scramble_solves();
	test_seeded_scrambles_solve();
	test_stage_boundaries_place_their_pieces();
	test_public_algorithm_api();
	printf("test_layer: all assertions passed\n");
	return (0);
}
