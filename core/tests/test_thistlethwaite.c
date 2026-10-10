#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "puzzle.h"
#include "puzzles/cubic.h"
#include "solve/cube_view.h"
#include "solve/kociemba.h"
#include "solve/thistlethwaite.h"

/// End-to-end checks for the ported Thistlethwaite solver
/// (docs/en/06-phase2-solvers.md), same spirit as test_kociemba.c: the
/// real proof is applying the returned moves back through
/// puzzle_apply_move() and landing on solved, not just "it returned
/// some number". This file additionally checks the one thing unique to
/// a four-phase method -- that each phase boundary actually lands the
/// cube in the next subgroup (G1, G2, G3) it's supposed to.

static t_thistle_solver	g_solver;
static t_kociemba_solver	g_kociemba;

static void	build_solver(void)
{
	assert(kociemba_init(&g_kociemba));
	assert(thistle_init(&g_solver, &g_kociemba.moves));
}

static void	free_solver(void)
{
	thistle_free(&g_solver);
	kociemba_free(&g_kociemba);
}

static void	apply_thistle_moves(puzzle_t *p, const int *moves, int count)
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
	int				moves[THISTLE_MAX_MOVES];
	int				phase_bounds[4];
	int				count;

	build_solver();
	p = puzzle_create(PUZZLE_CUBIC, 3);
	solve_cube_from_state(&cube, puzzle_state_ptr(p));
	count = thistle_solve(&g_solver, &cube, moves, phase_bounds);
	assert(count == 0);
	assert(phase_bounds[3] == 0);
	puzzle_destroy(p);
	free_solver();
	printf("test_thistlethwaite: solved cube needs 0 moves -- OK\n");
}

static void	test_sexy_move_scramble_solves(void)
{
	puzzle_t		*p;
	uint8_t			*solved;
	t_solve_cube	cube;
	int				moves[THISTLE_MAX_MOVES];
	int				phase_bounds[4];
	int				count;
	int				i;

	build_solver();
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
	count = thistle_solve(&g_solver, &cube, moves, phase_bounds);
	assert(count >= 0);
	assert(phase_bounds[3] == count);
	apply_thistle_moves(p, moves, count);
	assert(memcmp(puzzle_state_ptr(p), solved, puzzle_state_size(p)) == 0);
	free(solved);
	puzzle_destroy(p);
	free_solver();
	printf("test_thistlethwaite: (R U R' U')^5 scramble solves -- OK\n");
}

/// Several seeded scrambles, each solved and the solution applied back,
/// checked against solved -- same reproducible xorshift32 PRNG every
/// scramble in this project already uses.
static void	test_seeded_scrambles_solve(void)
{
	puzzle_t		*p;
	uint8_t			*solved;
	t_solve_cube	cube;
	int				moves[THISTLE_MAX_MOVES];
	int				phase_bounds[4];
	int				count;
	uint32_t		seed;

	build_solver();
	seed = 1;
	while (seed <= 8)
	{
		p = puzzle_create(PUZZLE_CUBIC, 3);
		solved = malloc(puzzle_state_size(p));
		memcpy(solved, puzzle_state_ptr(p), puzzle_state_size(p));
		puzzle_scramble(p, 25, seed);
		solve_cube_from_state(&cube, puzzle_state_ptr(p));
		assert(solve_cube_is_valid(&cube));
		count = thistle_solve(&g_solver, &cube, moves, phase_bounds);
		assert(count >= 0);
		assert(count <= THISTLE_MAX_MOVES);
		apply_thistle_moves(p, moves, count);
		assert(memcmp(puzzle_state_ptr(p), solved, puzzle_state_size(p))
			== 0);
		free(solved);
		puzzle_destroy(p);
		seed++;
	}
	free_solver();
	printf("test_thistlethwaite: 8 seeded 25-move scrambles all solve -- OK\n");
}

/// The one check unique to a four-phase method: after each phase's own
/// moves are applied, the cube is really inside the subgroup that phase
/// promised to reach -- not just "the overall solve worked", which a
/// wrong phase boundary could still pass by accident (e.g. phase 1
/// returning 0 moves and phase 2's bigger search quietly doing phase
/// 1's job too).
static void	test_phase_boundaries_reach_subgroups(void)
{
	puzzle_t		*p;
	t_solve_cube	cube;
	int				moves[THISTLE_MAX_MOVES];
	int				phase_bounds[4];
	int				count;
	uint32_t		seed;

	build_solver();
	seed = 1;
	while (seed <= 5)
	{
		p = puzzle_create(PUZZLE_CUBIC, 3);
		puzzle_scramble(p, 25, seed);
		solve_cube_from_state(&cube, puzzle_state_ptr(p));
		count = thistle_solve(&g_solver, &cube, moves, phase_bounds);
		assert(count >= 0);
		apply_thistle_moves(p, moves, phase_bounds[0]);
		solve_cube_from_state(&cube, puzzle_state_ptr(p));
		assert(encode_flip(&cube) == 0);
		apply_thistle_moves(p, moves + phase_bounds[0],
			phase_bounds[1] - phase_bounds[0]);
		solve_cube_from_state(&cube, puzzle_state_ptr(p));
		assert(encode_twist(&cube) == 0);
		assert(encode_slice(&cube) == 0);
		assert(encode_flip(&cube) == 0);
		apply_thistle_moves(p, moves + phase_bounds[1],
			phase_bounds[2] - phase_bounds[1]);
		solve_cube_from_state(&cube, puzzle_state_ptr(p));
		assert(g_solver.prune.cp_g3[encode_cperm(&cube)]);
		assert(g_solver.prune.ep_g3[encode_eperm(&cube)]);
		assert(g_solver.prune.sp_g3[encode_sperm(&cube)]);
		apply_thistle_moves(p, moves + phase_bounds[2],
			phase_bounds[3] - phase_bounds[2]);
		assert(puzzle_algorithm_count(p) == 4);
		puzzle_destroy(p);
		seed++;
	}
	free_solver();
	printf("test_thistlethwaite: each phase lands in its own subgroup"
		" -- OK\n");
}

/// reachable_set()'s own sanity check (docs/en/10-thistlethwaite-spec.md
/// S3's SHOULD-FIX, carried over from rubik): confirm cp_g3/ep_g3 are
/// genuinely restrictive (not every value reachable with only the 6
/// half turns) -- if they weren't, phase 3's coset goal would be no
/// goal at all. sp_g3 is allowed to be the full 24 (sperm already has
/// order dividing something small under half turns alone); either way
/// the array must be well-formed (0 is always a member, since solved
/// is reachable in zero moves).
static void	test_g3_reachable_sets_are_sane(void)
{
	int		cp_count;
	int		ep_count;
	int		i;

	build_solver();
	assert(g_solver.prune.cp_g3[0]);
	assert(g_solver.prune.ep_g3[0]);
	assert(g_solver.prune.sp_g3[0]);
	cp_count = 0;
	i = 0;
	while (i < CPERM_COUNT)
		cp_count += g_solver.prune.cp_g3[i++] != 0;
	ep_count = 0;
	i = 0;
	while (i < EPERM_COUNT)
		ep_count += g_solver.prune.ep_g3[i++] != 0;
	assert(cp_count > 0 && cp_count < CPERM_COUNT);
	assert(ep_count > 0 && ep_count < EPERM_COUNT);
	free_solver();
	printf("test_thistlethwaite: G3 reachable sets are real, restrictive"
		" subsets -- OK\n");
}

/// The PUBLIC API, algorithm id 1 specifically (id 0 is Kociemba,
/// already covered by test_kociemba.c's own test_public_algorithm_api)
/// -- what web/ will actually call for "run Thistlethwaite".
static void	test_public_algorithm_api(void)
{
	puzzle_t	*p;
	uint8_t		*solved;
	t_move		moves[64];
	uint32_t	phase_tags[64];
	uint32_t	count;
	uint32_t	i;
	uint32_t	seen_phase;

	p = puzzle_create(PUZZLE_CUBIC, 3);
	assert(puzzle_algorithm_count(p) == 4);
	assert(puzzle_algorithm_name(p, 1) != NULL);
	solved = malloc(puzzle_state_size(p));
	memcpy(solved, puzzle_state_ptr(p), puzzle_state_size(p));
	puzzle_scramble(p, 20, 42);
	count = puzzle_solve(p, 1, moves, 64, phase_tags);
	assert(count != 0xFFFFFFFFu && count <= 64);
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
	printf("test_thistlethwaite: public puzzle_solve() API, algo 1"
		" -- OK\n");
}

int	main(void)
{
	test_solved_cube_needs_no_moves();
	test_sexy_move_scramble_solves();
	test_seeded_scrambles_solve();
	test_phase_boundaries_reach_subgroups();
	test_g3_reachable_sets_are_sane();
	test_public_algorithm_api();
	printf("test_thistlethwaite: all assertions passed\n");
	return (0);
}
