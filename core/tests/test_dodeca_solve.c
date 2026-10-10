#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "puzzle.h"
#include "puzzles/dodecahedral.h"
#include "solve/dodeca_macros.h"
#include "solve/dodeca_solver.h"
#include "solve/registry.h"

/// Megaminx and Kilominx solving (docs/en/11-phase4-dodecahedral.md
/// increment 5): the 3-cycle element tables and the solver built on them.

#define SCRAMBLE_COUNT 200
#define SCRAMBLE_LENGTH 80
#define PAIR_COUNT 100
#define OUT_CAP 4000

/// Big (about 1.5 MB), so static rather than on the stack.
static t_dodeca_solver	g_solver;

static void	apply_word(const t_cut_family *f, uint8_t *state,
				const t_move *word, uint32_t len)
{
	uint32_t	i;

	i = 0;
	while (i < len)
		shape_apply_move(f->shape, f->generators, state, word[i++]);
}

/// The tables have every element exactly once, and the root really is
/// pure. The root's conjugacy class being the WHOLE set (20,520 corner
/// elements, 32,480 edge elements) is the proof that every 3-cycle with
/// every legal twist pattern is reachable, which the solver relies on.
static void	test_tables(void)
{
	assert(dodeca_solver_ready_corners(&g_solver));
	assert(dodeca_solver_ready_edges(&g_solver));
	assert(g_solver.corners.node_count == DODECA_CORNER_ELEMENTS);
	assert(g_solver.edges.node_count == DODECA_EDGE_ELEMENTS);
	assert(g_solver.corners.root_len <= DODECA_ROOT_MAX);
	assert(g_solver.edges.root_len <= DODECA_ROOT_MAX);
	assert(g_solver.corners.root_len + 2 * g_solver.corners.max_depth
		<= DODECA_WORD_MAX);
	assert(g_solver.edges.root_len + 2 * g_solver.edges.max_depth
		<= DODECA_WORD_MAX);
	printf("test_dodeca_solve: corner root %u moves, depth %u; "
		"edge root %u moves, depth %u\n", g_solver.corners.root_len,
		g_solver.corners.max_depth, g_solver.edges.root_len,
		g_solver.edges.max_depth);
}

/// Every element's word, replayed on a solved puzzle, does exactly what
/// the element says -- and, for edges, leaves every corner alone (the
/// extraction refuses anything else).
static void	check_every_word(const t_dodeca_table *t, const t_cut_family *f)
{
	uint8_t			state[DODECA_STATE_MAX];
	t_move			word[DODECA_WORD_MAX];
	t_dodeca_node	e;
	uint32_t		n;
	uint32_t		len;

	n = 0;
	while (n < t->node_count)
	{
		len = dodeca_table_word(t, &t->nodes[n], word, DODECA_WORD_MAX);
		assert(len != UINT32_MAX);
		shape_init_solved(f->shape, state);
		apply_word(f, state, word, len);
		assert(dodeca_extract_element(t, state, &e));
		assert(memcmp(e.slot, t->nodes[n].slot, 3) == 0);
		assert(memcmp(e.delta, t->nodes[n].delta, 3) == 0);
		assert(dodeca_table_find(t, e.slot, e.delta) == &t->nodes[n]);
		n++;
	}
}

static void	test_words_replay(void)
{
	check_every_word(&g_solver.corners, dodeca_family(DODECA_KILOMINX_SIZE));
	check_every_word(&g_solver.corners, dodeca_family(DODECA_MEGAMINX_SIZE));
	check_every_word(&g_solver.edges, dodeca_family(DODECA_MEGAMINX_SIZE));
}

static void	test_find_rejects(void)
{
	uint8_t	slot[3] = {0, 1, 2};
	uint8_t	delta[3] = {0, 0, 0};
	uint8_t	repeated[3] = {0, 1, 1};
	uint8_t	bad_sum[3] = {1, 0, 0};
	uint8_t	too_big[3] = {0, 0, 3};

	assert(dodeca_table_find(&g_solver.corners, slot, delta) != NULL);
	assert(dodeca_table_find(&g_solver.corners, repeated, delta) == NULL);
	assert(dodeca_table_find(&g_solver.corners, slot, bad_sum) == NULL);
	assert(dodeca_table_find(&g_solver.corners, slot, too_big) == NULL);
	slot[2] = DODECA_CORNER_COUNT;
	assert(dodeca_table_find(&g_solver.corners, slot, delta) == NULL);
	assert(dodeca_move_inverse(dodeca_move_inverse(7)) == 7);
}

/// A solution applied to a copy of the scrambled puzzle solves it.
static void	check_solution(puzzle_t *p, uint32_t size, const t_move *out,
				const uint32_t *tags, int count)
{
	const t_cut_family	*f = dodeca_family(size);
	uint8_t				solved[DODECA_STATE_MAX];
	uint8_t				state[DODECA_STATE_MAX];
	uint32_t			perm_off;
	uint32_t			orient_off;
	int					i;
	uint32_t			s;

	memcpy(state, puzzle_state_ptr(p), shape_state_size(f->shape));
	shape_init_solved(f->shape, solved);
	i = 0;
	while (i < count)
	{
		assert(out[i].amount >= 1 && out[i].amount <= DODECA_STEPS);
		assert(out[i].generator >= 0 && out[i].generator < DODECA_FACE_COUNT);
		assert(i == 0 || tags[i] >= tags[i - 1]);
		assert(tags[i] < (size == DODECA_MEGAMINX_SIZE ? 2u : 1u));
		shape_apply_move(f->shape, f->generators, state, out[i]);
		if (i + 1 == count || tags[i + 1] != tags[i])
		{
			if (tags[i] == DODECA_PHASE_CORNERS)
			{
				shape_piece_offsets(f->shape, DODECA_CORNER_TYPE, &perm_off,
					&orient_off);
				s = 0;
				while (s < DODECA_CORNER_COUNT)
				{
					assert(state[perm_off + s] == s
						&& state[orient_off + s] == 0);
					s++;
				}
			}
		}
		i++;
	}
	assert(memcmp(state, solved, shape_state_size(f->shape)) == 0);
}

/// 200 random scrambles of each puzzle solve completely; the phase
/// boundary leaves the corners solved; lengths stay under the caps.
static void	test_random_solves(uint32_t size, uint32_t cap)
{
	t_move		out[OUT_CAP];
	uint32_t	tags[OUT_CAP];
	puzzle_t	*p;
	uint32_t	seed;
	uint32_t	longest;
	uint32_t	total;
	int			count;

	longest = 0;
	total = 0;
	seed = 1;
	while (seed <= SCRAMBLE_COUNT)
	{
		p = puzzle_create(PUZZLE_DODECAHEDRAL, size);
		puzzle_scramble(p, SCRAMBLE_LENGTH, seed);
		count = dodeca_solve(&g_solver, size, puzzle_state_ptr(p), out,
				cap, tags);
		assert(count > 0);
		check_solution(p, size, out, tags, count);
		longest = ((uint32_t)count > longest) ? (uint32_t)count : longest;
		total += (uint32_t)count;
		puzzle_destroy(p);
		seed++;
	}
	printf("test_dodeca_solve: size %u: average %u, longest %u moves "
		"(cap %u)\n", size, total / SCRAMBLE_COUNT, longest, cap);
	assert(longest <= cap);
}

/// States built as the product of two random elements: small-support
/// states that put pieces home-but-twisted and swap pieces in tight
/// clusters, which random scrambles hit only rarely.
static void	test_small_support_states(uint32_t size, const t_dodeca_table *t)
{
	const t_cut_family	*f = dodeca_family(size);
	t_move				out[OUT_CAP];
	uint32_t			tags[OUT_CAP];
	t_move				word[DODECA_WORD_MAX];
	uint8_t				state[DODECA_STATE_MAX];
	uint8_t				check[DODECA_STATE_MAX];
	uint32_t			i;
	uint32_t			a;
	uint32_t			b;
	uint32_t			len;
	int					count;

	i = 0;
	while (i < PAIR_COUNT)
	{
		a = (i * 7919u + 13u) % t->node_count;
		b = (i * 104729u + 29u) % t->node_count;
		shape_init_solved(f->shape, state);
		len = dodeca_table_word(t, &t->nodes[a], word, DODECA_WORD_MAX);
		apply_word(f, state, word, len);
		len = dodeca_table_word(t, &t->nodes[b], word, DODECA_WORD_MAX);
		apply_word(f, state, word, len);
		count = dodeca_solve(&g_solver, size, state, out, OUT_CAP, tags);
		assert(count >= 0);
		memcpy(check, state, shape_state_size(f->shape));
		apply_word(f, check, out, (uint32_t)count);
		shape_init_solved(f->shape, state);
		assert(memcmp(check, state, shape_state_size(f->shape)) == 0);
		i++;
	}
}

static void	test_solved_and_invalid(void)
{
	t_move		out[OUT_CAP];
	uint32_t	tags[OUT_CAP];
	uint8_t		state[DODECA_STATE_MAX];
	uint32_t	perm_off;
	uint32_t	orient_off;

	shape_init_solved(dodeca_shape(DODECA_MEGAMINX_SIZE), state);
	assert(dodeca_solve(&g_solver, DODECA_MEGAMINX_SIZE, state, out, OUT_CAP,
			tags) == 0);
	shape_init_solved(dodeca_shape(DODECA_KILOMINX_SIZE), state);
	assert(dodeca_solve(&g_solver, DODECA_KILOMINX_SIZE, state, out, OUT_CAP,
			tags) == 0);
	shape_piece_offsets(dodeca_shape(DODECA_KILOMINX_SIZE),
		DODECA_CORNER_TYPE, &perm_off, &orient_off);
	state[orient_off] = 1;
	assert(dodeca_solve(&g_solver, DODECA_KILOMINX_SIZE, state, out, OUT_CAP,
			tags) == -1);
	shape_init_solved(dodeca_shape(DODECA_KILOMINX_SIZE), state);
	state[perm_off] = 1;
	state[perm_off + 1] = 0;
	assert(dodeca_solve(&g_solver, DODECA_KILOMINX_SIZE, state, out, OUT_CAP,
			tags) == -1);
	assert(dodeca_solve(&g_solver, 4, state, out, OUT_CAP, tags) == -1);
}

/// A solution that does not fit the buffer is refused, not truncated.
static void	test_cap_is_enforced(void)
{
	t_move		out[8];
	uint32_t	tags[8];
	puzzle_t	*p = puzzle_create(PUZZLE_DODECAHEDRAL, DODECA_KILOMINX_SIZE);

	puzzle_scramble(p, SCRAMBLE_LENGTH, 3);
	assert(dodeca_solve(&g_solver, DODECA_KILOMINX_SIZE, puzzle_state_ptr(p),
			out, 8, tags) == -1);
	puzzle_destroy(p);
}

/// The public API: one algorithm per puzzle, sized for its cap.
static void	test_puzzle_api(void)
{
	puzzle_t	*kilo = puzzle_create(PUZZLE_DODECAHEDRAL, DODECA_KILOMINX_SIZE);
	puzzle_t	*mega = puzzle_create(PUZZLE_DODECAHEDRAL, DODECA_MEGAMINX_SIZE);
	t_move		out[OUT_CAP];
	uint32_t	tags[OUT_CAP];
	uint32_t	count;
	uint32_t	i;

	assert(puzzle_algorithm_count(kilo) == 1 && puzzle_algorithm_count(mega) == 1);
	assert(puzzle_algorithm_name(kilo, 0) != NULL);
	assert(puzzle_algorithm_name(kilo, 1) == NULL);
	assert(puzzle_solve_max_moves(kilo) == DODECA_KILOMINX_MAX_MOVES);
	assert(puzzle_solve_max_moves(mega) == DODECA_MEGAMINX_MAX_MOVES);
	puzzle_scramble(mega, SCRAMBLE_LENGTH, 11);
	count = puzzle_solve(mega, 0, out, OUT_CAP, tags);
	assert(count != UINT32_MAX && count > 0);
	i = 0;
	while (i < count)
	{
		puzzle_apply_move(mega, out[i].generator, out[i].amount);
		i++;
	}
	assert(puzzle_solve(mega, 0, out, OUT_CAP, tags) == 0);
	assert(puzzle_solve(mega, 1, out, OUT_CAP, tags) == UINT32_MAX);
	puzzle_scramble(kilo, SCRAMBLE_LENGTH, 12);
	count = puzzle_solve(kilo, 0, out, OUT_CAP, tags);
	assert(count != UINT32_MAX && count > 0);
	puzzle_destroy(kilo);
	puzzle_destroy(mega);
}

int	main(void)
{
	test_tables();
	test_words_replay();
	test_find_rejects();
	test_random_solves(DODECA_KILOMINX_SIZE, DODECA_KILOMINX_MAX_MOVES);
	test_random_solves(DODECA_MEGAMINX_SIZE, DODECA_MEGAMINX_MAX_MOVES);
	test_small_support_states(DODECA_KILOMINX_SIZE, &g_solver.corners);
	test_small_support_states(DODECA_MEGAMINX_SIZE, &g_solver.corners);
	test_small_support_states(DODECA_MEGAMINX_SIZE, &g_solver.edges);
	test_solved_and_invalid();
	test_cap_is_enforced();
	test_puzzle_api();
	printf("test_dodeca_solve: all assertions passed\n");
	return (0);
}
