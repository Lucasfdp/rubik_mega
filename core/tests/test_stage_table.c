#include <assert.h>
#include <stdio.h>
#include "puzzle.h"
#include "solve/stage_table.h"
#include "solve/bytes.h"
#include "solve/skewb_staged.h"
#include "solve/pyraminx_staged.h"

/// The stage tables (docs/en/10-phase3-human-methods.md D4): their
/// sizes and depths checked against an INDEPENDENT count, descent
/// checked step by step, and malformed states refused.

#define SCRAMBLE_COUNT 300
#define SCRAMBLE_LENGTH 40

/// A table's depth, and a piece's (slot, orientation) packing, never
/// need more than this many radix-24 digits in the projection oracle:
/// a Skewb corner has 8 slots x 3 twists = 24 placements.
#define MAX_PLACEMENTS 24
#define MAX_ORACLE_PIECES 4
#define ORACLE_SEEN_SIZE (MAX_PLACEMENTS * MAX_PLACEMENTS * MAX_PLACEMENTS \
		* MAX_PLACEMENTS)

static t_skewb_solver	g_skewb;
static t_skewb_staged	g_skewb_staged;
static t_pyraminx_solver	g_pyraminx;
static t_pyraminx_staged	g_pyraminx_staged;

/// The oracle: how many distinct (slot, orientation) tuples `pieces`
/// take across every value of block `c` (a whole piece type's
/// permutation and orientation bytes, as coord_table.c enumerated them
/// by its own breadth-first search). A stage over those pieces can
/// reach exactly that many placements of them, so this checks the stage
/// table's count without sharing any of its code -- only the move model
/// underneath both.
static uint32_t	projection_count(const t_coord_component *c,
	uint32_t slots, uint32_t orientations, const uint8_t *pieces,
	uint32_t piece_count)
{
	static uint8_t	seen[ORACLE_SEEN_SIZE];
	const uint8_t	*value;
	uint32_t		row;
	uint32_t		index;
	uint32_t		count;
	uint32_t		i;
	uint32_t		slot;

	for (index = 0; index < ORACLE_SEEN_SIZE; index++)
		seen[index] = 0;
	count = 0;
	for (row = 0; row < c->size; row++)
	{
		value = &c->values[row * c->length];
		index = 0;
		for (i = 0; i < piece_count; i++)
		{
			slot = 0;
			while (slot < slots && value[slot] != pieces[i])
				slot++;
			assert(slot < slots);
			index = index * MAX_PLACEMENTS + slot * orientations
				+ value[slots + slot];
		}
		if (!seen[index])
			count++;
		seen[index] = 1;
	}
	return (count);
}

/// True if tracked piece `p` is home in `state`: in its own slot, not
/// turned. Read straight off the state buffer.
static int	is_home(const t_shape *shape, const uint8_t *state,
	const t_stage_piece *p)
{
	uint32_t	perm_offset;
	uint32_t	orient_offset;

	shape_piece_offsets(shape, p->piece_type, &perm_offset, &orient_offset);
	return (state[perm_offset + p->piece] == p->piece
		&& state[orient_offset + p->piece] == 0);
}

static int	goal_met(const t_stage_table *t, const uint8_t *state)
{
	uint32_t	i;

	i = 0;
	while (i < t->tracked_count)
		if (!is_home(t->shape, state, &t->tracked[i++]))
			return (0);
	return (1);
}

static void	test_sizes_match_independent_count(void)
{
	static const uint8_t	corners[] = {0, 1, 4, 5};
	static const uint8_t	centre[] = {SKEWB_CENTRE_TOP};
	static const uint8_t	axials[] = {0, 1, 2, 3};
	static const uint8_t	edges[] = {3, 4, 5};
	uint32_t				skewb_corners;
	uint32_t				pyraminx_edges;

	assert(skewb_solver_init(&g_skewb));
	assert(skewb_staged_init(&g_skewb_staged, &g_skewb));
	assert(pyraminx_solver_init(&g_pyraminx));
	assert(pyraminx_staged_init(&g_pyraminx_staged, &g_pyraminx));
	skewb_corners = projection_count(&g_skewb.table.components[0],
			SKEWB_CORNER_COUNT, SKEWB_CORNER_ORIENTATIONS, corners, 4);
	assert(skewb_corners == SKEWB_STAGE0_STATES);
	assert(skewb_corners * projection_count(&g_skewb.table.components[1],
			SKEWB_CENTRE_COUNT, SKEWB_CENTRE_ORIENTATIONS, centre, 1)
		== SKEWB_STAGE1_STATES);
	assert(g_skewb_staged.corners.state_count == SKEWB_STAGE0_STATES);
	assert(g_skewb_staged.centre.state_count == SKEWB_STAGE1_STATES);
	assert(g_skewb_staged.corners.max_depth == SKEWB_STAGE0_DEPTH);
	assert(g_skewb_staged.centre.max_depth == SKEWB_STAGE1_DEPTH);
	assert(g_skewb.table.max_depth == SKEWB_STAGE2_DEPTH);
	assert(SKEWB_STAGED_MAX_MOVES > SKEWB_MAX_MOVES);
	assert(projection_count(&g_pyraminx.table.components[0],
			TETRA_VERTEX_COUNT, TETRA_VERTEX_PIECE_ORIENTATIONS, axials, 4)
		== PYRAMINX_STAGE0_STATES);
	pyraminx_edges = projection_count(&g_pyraminx.table.components[1],
			TETRA_EDGE_COUNT, TETRA_EDGE_ORIENTATIONS, edges, 3);
	assert(PYRAMINX_STAGE0_STATES * pyraminx_edges == PYRAMINX_STAGE1_STATES);
	assert(g_pyraminx_staged.axial.state_count == PYRAMINX_STAGE0_STATES);
	assert(g_pyraminx_staged.first_layer.state_count
		== PYRAMINX_STAGE1_STATES);
	assert(g_pyraminx_staged.axial.max_depth == PYRAMINX_STAGE0_DEPTH);
	assert(g_pyraminx_staged.first_layer.max_depth == PYRAMINX_STAGE1_DEPTH);
	assert(g_pyraminx.table.max_depth == PYRAMINX_STAGE2_DEPTH);
	assert(PYRAMINX_STAGED_MAX_MOVES > PYRAMINX_MAX_MOVES);
}

/// Descent from a scrambled state (a Skewb's already in the canonical
/// frame, which is the only place its stage tables are defined): the distance is the move count, every
/// move cuts it by exactly one, and the goal really holds at the end
/// (checked on the state buffer, not through the table).
static void	check_descent(const t_stage_table *t, const uint8_t *state)
{
	uint8_t	copy[STAGE_MAX_STATE];
	uint8_t	replay[STAGE_MAX_STATE];
	t_move	out[STAGE_MAX_MOVES * 4];
	int		distance;
	int		count;
	int		i;

	distance = stage_table_distance(t, state);
	assert(distance >= 0 && distance <= (int)t->max_depth);
	solve_copy_bytes(copy, state, shape_state_size(t->shape));
	count = stage_table_descend(t, copy, out, STAGE_MAX_MOVES * 4);
	assert(count == distance);
	assert(goal_met(t, copy));
	assert(stage_table_distance(t, copy) == 0);
	solve_copy_bytes(replay, state, shape_state_size(t->shape));
	i = 0;
	while (i < count)
	{
		shape_apply_move(t->shape, t->gens, replay, out[i++]);
		assert(stage_table_distance(t, replay) == distance - i);
	}
}

static void	test_descent_from_scrambles(void)
{
	puzzle_t	*skewb;
	puzzle_t	*pyraminx;
	uint8_t		framed[SKEWB_STATE_BYTES];
	uint32_t	seed;

	seed = 1;
	while (seed <= SCRAMBLE_COUNT)
	{
		skewb = puzzle_create(PUZZLE_SKEWB, 0);
		puzzle_scramble(skewb, SCRAMBLE_LENGTH, seed * 2654435761u);
		assert(skewb_find_frame(&g_skewb, puzzle_state_ptr(skewb), framed)
			>= 0);
		check_descent(&g_skewb_staged.corners, framed);
		check_descent(&g_skewb_staged.centre, framed);
		puzzle_destroy(skewb);
		pyraminx = puzzle_create(PUZZLE_TETRAHEDRAL, 0);
		puzzle_scramble(pyraminx, SCRAMBLE_LENGTH, seed * 2654435761u);
		check_descent(&g_pyraminx_staged.axial, puzzle_state_ptr(pyraminx));
		check_descent(&g_pyraminx_staged.first_layer,
			puzzle_state_ptr(pyraminx));
		puzzle_destroy(pyraminx);
		seed++;
	}
}

/// A solved puzzle is already at every stage's goal.
static void	test_solved_is_distance_zero(void)
{
	uint8_t	state[STAGE_MAX_STATE];

	shape_init_solved(skewb_shape(), state);
	assert(stage_table_distance(&g_skewb_staged.corners, state) == 0);
	assert(stage_table_distance(&g_skewb_staged.centre, state) == 0);
	shape_init_solved(tetrahedral_shape(), state);
	assert(stage_table_distance(&g_pyraminx_staged.axial, state) == 0);
	assert(stage_table_distance(&g_pyraminx_staged.first_layer, state) == 0);
}

/// Two kinds of bad state: a tracked piece missing from its block
/// altogether, and every tracked piece present but somewhere the moves
/// cannot put it (Pyraminx axial pieces never change slot).
static void	test_malformed_states_refused(void)
{
	uint8_t		state[STAGE_MAX_STATE];
	uint32_t	perm_offset;
	uint32_t	orient_offset;
	t_move		out[STAGE_MAX_MOVES];

	shape_init_solved(skewb_shape(), state);
	shape_piece_offsets(skewb_shape(), SKEWB_CORNER_TYPE, &perm_offset,
		&orient_offset);
	state[perm_offset] = 0xFF;
	assert(stage_table_distance(&g_skewb_staged.corners, state) == -1);
	assert(stage_table_descend(&g_skewb_staged.corners, state, out,
			STAGE_MAX_MOVES) == -1);
	shape_init_solved(tetrahedral_shape(), state);
	shape_piece_offsets(tetrahedral_shape(), TETRA_AXIAL_TYPE, &perm_offset,
		&orient_offset);
	state[perm_offset] = 1;
	state[perm_offset + 1] = 0;
	assert(stage_table_distance(&g_pyraminx_staged.axial, state) == -1);
}

/// A descent that would need more moves than allowed is refused and
/// leaves the state alone.
static void	test_descent_respects_max_moves(void)
{
	puzzle_t	*p;
	uint8_t		copy[STAGE_MAX_STATE];
	t_move		out[STAGE_MAX_MOVES];
	uint32_t	seed;
	int			distance;

	seed = 1;
	while (seed <= SCRAMBLE_COUNT)
	{
		p = puzzle_create(PUZZLE_TETRAHEDRAL, 0);
		puzzle_scramble(p, SCRAMBLE_LENGTH, seed++ * 2654435761u);
		solve_copy_bytes(copy, puzzle_state_ptr(p),
			shape_state_size(tetrahedral_shape()));
		distance = stage_table_distance(&g_pyraminx_staged.first_layer, copy);
		if (distance > 0)
		{
			assert(stage_table_descend(&g_pyraminx_staged.first_layer, copy,
					out, (uint32_t)distance - 1) == -1);
			assert(stage_table_distance(&g_pyraminx_staged.first_layer, copy)
				== distance);
		}
		puzzle_destroy(p);
	}
}

/// Limits are refused rather than overrun: a table too small for its
/// stage, and more pieces than a key can hold.
static void	test_init_limits(void)
{
	t_stage_table	table;
	t_stage_piece	pieces[STAGE_MAX_TRACKED + 1];
	uint32_t		i;

	i = 0;
	while (i < STAGE_MAX_TRACKED + 1)
	{
		pieces[i].piece_type = TETRA_AXIAL_TYPE;
		pieces[i].piece = (uint8_t)(i % TETRA_VERTEX_COUNT);
		i++;
	}
	assert(!stage_table_init(&table, tetrahedral_shape(),
			tetrahedral_generators(), pieces, TETRA_VERTEX_COUNT,
			g_pyraminx.table.moves, g_pyraminx.table.move_count,
			PYRAMINX_STAGE0_STATES - 1));
	assert(!stage_table_init(&table, tetrahedral_shape(),
			tetrahedral_generators(), pieces, STAGE_MAX_TRACKED + 1,
			g_pyraminx.table.moves, g_pyraminx.table.move_count,
			PYRAMINX_STAGE0_STATES));
	assert(stage_table_init(&table, tetrahedral_shape(),
			tetrahedral_generators(), pieces, TETRA_VERTEX_COUNT,
			g_pyraminx.table.moves, g_pyraminx.table.move_count,
			PYRAMINX_STAGE0_STATES));
}

int	main(void)
{
	test_sizes_match_independent_count();
	test_solved_is_distance_zero();
	test_descent_from_scrambles();
	test_malformed_states_refused();
	test_descent_respects_max_moves();
	test_init_limits();
	printf("test_stage_table: all assertions passed\n");
	return (0);
}
