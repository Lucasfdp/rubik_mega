#include "solve/square1_solver.h"

/// The solver itself: docs/en/12-phase5-square1.md D8-A, and the header for
/// the overall method. Written against the engine in puzzles/square1.c, not
/// beside it: every move table here is read off that engine, so the two can
/// never disagree about what a move does.

# define LAYER_BOUNDARY_A 11
# define LAYER_BOUNDARY_B 5
# define ALL_LAYER_BITS (SQ1_SHAPE_MASK_COUNT - 1)
# define SQ1_CUBE_PATTERN_UNITS 3

/// R: the solved state with its bottom layer turned by this many units
/// (generator amount). Phase 2's goal, and the reason: see the header.
# define R_BOTTOM_AMOUNT 2
/// ... and the amount that turns R back into the solved state.
# define R_TO_SOLVED_AMOUNT (SQ1_LAYER_UNITS - R_BOTTOM_AMOUNT)
/// The shift phase 2 wants each layer at (see the header).
# define P2_TOP_SHIFT 0
# define P2_BOTTOM_SHIFT 2

/* ---- layer shapes ----------------------------------------------------- */

/// Bit i is set iff a piece boundary follows unit i of the layer.
static uint32_t	layer_mask(const uint8_t *layer)
{
	uint32_t	mask;
	uint32_t	i;

	mask = 0;
	i = 0;
	while (i < SQ1_LAYER_UNITS)
	{
		if (layer[i] != layer[(i + 1) % SQ1_LAYER_UNITS])
			mask |= 1u << i;
		i++;
	}
	return (mask);
}

/// The mask of the same layer turned clockwise by `turn` units.
static uint32_t	mask_rotate(uint32_t mask, uint32_t turn)
{
	return (((mask << turn) | (mask >> (SQ1_LAYER_UNITS - turn)))
		& ALL_LAYER_BITS);
}

/// A mask is a real layer iff every run between boundaries is one unit (an
/// edge) or two (a corner) and the corner count is one a layer can have.
static bool	mask_is_valid(uint32_t mask)
{
	uint32_t	first;
	uint32_t	prev;
	uint32_t	i;
	uint32_t	run;
	uint32_t	corners;

	if (mask == 0)
		return (false);
	first = 0;
	while (!((mask >> first) & 1u))
		first++;
	corners = 0;
	prev = first;
	i = first + 1;
	while (i <= first + SQ1_LAYER_UNITS)
	{
		if ((mask >> (i % SQ1_LAYER_UNITS)) & 1u)
		{
			run = i - prev;
			if (run > 2)
				return (false);
			corners += (run == 2);
			prev = i;
		}
		i++;
	}
	return (corners >= SQ1_LAYER_MIN_CORNERS
		&& corners <= SQ1_LAYER_MAX_CORNERS);
}

static uint32_t	mask_canonical(uint32_t mask)
{
	uint32_t	best;
	uint32_t	turn;
	uint32_t	rotated;

	best = mask;
	turn = 1;
	while (turn < SQ1_LAYER_UNITS)
	{
		rotated = mask_rotate(mask, turn);
		if (rotated < best)
			best = rotated;
		turn++;
	}
	return (best);
}

/// Numbers every valid layer shape up to rotation, in order of its smallest
/// mask. Returns the representative masks through `reps`; false if the
/// count is not the 29 the spec measured.
static bool	build_shape_classes(t_sq1_solver *solver, uint32_t *reps)
{
	uint32_t	mask;
	uint32_t	count;

	mask = 0;
	while (mask < SQ1_SHAPE_MASK_COUNT)
		solver->shape_class[mask++] = SQ1_SHAPE_UNREACHED;
	count = 0;
	mask = 1;
	while (mask < SQ1_SHAPE_MASK_COUNT)
	{
		if (mask_is_valid(mask) && mask_canonical(mask) == mask)
		{
			if (count >= SQ1_SHAPE_CLASS_COUNT)
				return (false);
			reps[count++] = mask;
		}
		mask++;
	}
	if (count != SQ1_SHAPE_CLASS_COUNT)
		return (false);
	mask = 1;
	while (mask < SQ1_SHAPE_MASK_COUNT)
	{
		if (mask_is_valid(mask))
		{
			count = 0;
			while (reps[count] != mask_canonical(mask))
				count++;
			solver->shape_class[mask] = (uint8_t)count;
		}
		mask++;
	}
	return (true);
}

/// A layer, rotated clockwise by `turn`, has a piece boundary at both ends
/// of the cut -- which is exactly when the slice is legal for that layer.
static bool	mask_is_clear_after(uint32_t mask, uint32_t turn)
{
	uint32_t	rotated;

	rotated = mask_rotate(mask, turn);
	return (((rotated >> LAYER_BOUNDARY_A) & 1u)
		&& ((rotated >> LAYER_BOUNDARY_B) & 1u));
}

/// The two layer masks after a slice, from the masks before (both clear of
/// the cut). The right half of each layer is the other layer's, reversed:
/// the boundary after new unit i is the boundary after old unit 4 - i of
/// the other layer. The boundaries at the two ends of the cut separate
/// pieces of different layers, so they are always there.
static void	mask_slice(uint32_t top, uint32_t bottom, uint32_t *new_top,
	uint32_t *new_bottom)
{
	uint32_t	nt;
	uint32_t	nb;
	uint32_t	i;
	uint32_t	cut_bits;

	cut_bits = (1u << LAYER_BOUNDARY_A) | (1u << LAYER_BOUNDARY_B);
	nt = cut_bits;
	nb = cut_bits;
	i = 0;
	while (i < SQ1_HALF_UNITS - 1)
	{
		nt |= ((bottom >> (SQ1_HALF_UNITS - 2 - i)) & 1u) << i;
		nb |= ((top >> (SQ1_HALF_UNITS - 2 - i)) & 1u) << i;
		i++;
	}
	i = SQ1_HALF_UNITS;
	while (i < SQ1_LAYER_UNITS - 1)
	{
		nt |= top & (1u << i);
		nb |= bottom & (1u << i);
		i++;
	}
	*new_top = nt;
	*new_bottom = nb;
}

/// Every pair of shapes one slice from (a, b), with the layers turned
/// freely first: sets the ones not yet reached to depth + 1 and returns how
/// many that was.
static uint32_t	expand_shape_pair(t_sq1_solver *solver, const uint32_t *reps,
	uint32_t a, uint32_t b, uint32_t depth)
{
	uint32_t	top_turn;
	uint32_t	bottom_turn;
	uint32_t	top;
	uint32_t	bottom;
	uint32_t	new_top;
	uint32_t	new_bottom;
	uint32_t	found;

	found = 0;
	top_turn = 0;
	while (top_turn < SQ1_LAYER_UNITS)
	{
		top = mask_rotate(reps[a], top_turn++);
		bottom_turn = 0;
		while (bottom_turn < SQ1_LAYER_UNITS)
		{
			bottom = mask_rotate(reps[b], bottom_turn++);
			if (!mask_is_clear_after(top, 0) || !mask_is_clear_after(bottom, 0))
				continue ;
			mask_slice(top, bottom, &new_top, &new_bottom);
			if (solver->shape_dist[solver->shape_class[new_top]]
				[solver->shape_class[new_bottom]] == SQ1_SHAPE_UNREACHED)
			{
				solver->shape_dist[solver->shape_class[new_top]]
				[solver->shape_class[new_bottom]] = (uint8_t)(depth + 1);
				found++;
			}
		}
	}
	return (found);
}

/// Fills `shape_dist` by breadth-first search from (cube, cube): the
/// distance, in slices with the layers turned freely between, from each
/// pair of shapes. The graph is symmetric (a slice undoes itself), so a
/// forward search from the goal gives the distance TO it.
static void	build_shape_distances(t_sq1_solver *solver, const uint32_t *reps)
{
	uint32_t	cube;
	uint32_t	a;
	uint32_t	b;
	uint32_t	depth;
	uint32_t	found;

	a = 0;
	while (a < SQ1_SHAPE_CLASS_COUNT)
	{
		b = 0;
		while (b < SQ1_SHAPE_CLASS_COUNT)
			solver->shape_dist[a][b++] = SQ1_SHAPE_UNREACHED;
		a++;
	}
	/// The cube-shaped layer: boundaries after every unit but one in three.
	cube = 0;
	a = 0;
	while (a < SQ1_LAYER_UNITS)
	{
		if (a % SQ1_CUBE_PATTERN_UNITS != 0)
			cube |= 1u << a;
		a++;
	}
	a = solver->shape_class[cube];
	solver->shape_dist[a][a] = 0;
	depth = 0;
	found = 1;
	while (found > 0)
	{
		found = 0;
		a = 0;
		while (a < SQ1_SHAPE_CLASS_COUNT)
		{
			b = 0;
			while (b < SQ1_SHAPE_CLASS_COUNT)
			{
				if (solver->shape_dist[a][b] == depth)
					found += expand_shape_pair(solver, reps, a, b, depth);
				b++;
			}
			a++;
		}
		depth++;
	}
}

static uint32_t	shape_distance_of(const t_sq1_solver *solver,
	uint32_t top_mask, uint32_t bottom_mask)
{
	uint8_t	top;
	uint8_t	bottom;

	top = solver->shape_class[top_mask];
	bottom = solver->shape_class[bottom_mask];
	if (top == SQ1_SHAPE_UNREACHED || bottom == SQ1_SHAPE_UNREACHED)
		return (SQ1_SHAPE_UNREACHED);
	return (solver->shape_dist[top][bottom]);
}

uint32_t	sq1_solver_shape_distance(const t_sq1_solver *solver,
	const uint8_t *state)
{
	return (shape_distance_of(solver, layer_mask(state),
			layer_mask(state + SQ1_LAYER_UNITS)));
}

/* ---- permutations ----------------------------------------------------- */

/// Rank of a permutation of 0..7 in the factorial number system: digit i
/// is how many later entries are smaller. 0 is the identity.
static uint32_t	perm_rank(const uint8_t *perm)
{
	uint32_t	rank;
	uint32_t	smaller;
	uint32_t	i;
	uint32_t	j;

	rank = 0;
	i = 0;
	while (i < SQ1_PERM_LEN)
	{
		smaller = 0;
		j = i + 1;
		while (j < SQ1_PERM_LEN)
			smaller += (perm[j++] < perm[i]);
		rank = rank * (SQ1_PERM_LEN - i) + smaller;
		i++;
	}
	return (rank);
}

static void	perm_unrank(uint32_t rank, uint8_t *perm)
{
	uint8_t		digit[SQ1_PERM_LEN];
	uint8_t		left[SQ1_PERM_LEN];
	uint32_t	i;
	uint32_t	j;

	i = SQ1_PERM_LEN;
	while (i-- > 0)
	{
		digit[i] = (uint8_t)(rank % (SQ1_PERM_LEN - i));
		rank /= SQ1_PERM_LEN - i;
	}
	i = 0;
	while (i < SQ1_PERM_LEN)
	{
		left[i] = (uint8_t)i;
		i++;
	}
	i = 0;
	while (i < SQ1_PERM_LEN)
	{
		perm[i] = left[digit[i]];
		j = digit[i];
		while (j + 1 < SQ1_PERM_LEN - i)
		{
			left[j] = left[j + 1];
			j++;
		}
		i++;
	}
}

/// 1 for an odd permutation (an odd number of inversions), else 0.
static uint32_t	perm_parity(const uint8_t *perm)
{
	uint32_t	odd;
	uint32_t	i;
	uint32_t	j;

	odd = 0;
	i = 0;
	while (i < SQ1_PERM_LEN)
	{
		j = i + 1;
		while (j < SQ1_PERM_LEN)
			odd ^= (perm[j++] < perm[i]);
		i++;
	}
	return (odd);
}

/* ---- phase 2 coordinates ---------------------------------------------- */

/// Phase 2 states have the layers at shifts (0, 2), so a slot is a fixed
/// place: 4 corner slots and 4 edge slots in each layer. Reading slot j of
/// the top: its corner starts at unit 3j and its edge is unit 3j + 2. The
/// bottom is R's: that layer is solved-then-turned by R_BOTTOM_AMOUNT
/// units, so its corner j starts at 3j - 2 (mod 12) and its edge is at 3j.
# define SLOT_TOP_EDGE_OFFSET 2
# define SLOT_BOTTOM_CORNER_OFFSET (SQ1_LAYER_UNITS - R_BOTTOM_AMOUNT)

static void	read_slots(const uint8_t *state, uint8_t *corner, uint8_t *edge)
{
	uint32_t	j;

	j = 0;
	while (j < SQ1_CORNERS_PER_LAYER)
	{
		corner[j] = state[SQ1_QUARTER_UNITS * j];
		corner[SQ1_CORNERS_PER_LAYER + j] = state[SQ1_LAYER_UNITS
			+ (SQ1_QUARTER_UNITS * j + SLOT_BOTTOM_CORNER_OFFSET)
			% SQ1_LAYER_UNITS];
		edge[j] = (uint8_t)(state[SQ1_QUARTER_UNITS * j
				+ SLOT_TOP_EDGE_OFFSET] - SQ1_FIRST_EDGE_ID);
		edge[SQ1_EDGES_PER_LAYER + j] = (uint8_t)(state[SQ1_LAYER_UNITS
				+ SQ1_QUARTER_UNITS * j] - SQ1_FIRST_EDGE_ID);
		j++;
	}
}

/// R as a state.
static void	make_r(uint8_t *state)
{
	sq1_init_solved(state);
	sq1_turn_bottom(state, R_BOTTOM_AMOUNT);
}

/// Applies phase 2 move `move` to a state in R's alignment.
static void	apply_p2_move(uint8_t *state, uint32_t move)
{
	if (move == SQ1_P2_MOVE_TOP)
		sq1_turn_top(state, SQ1_QUARTER_UNITS);
	else if (move == SQ1_P2_MOVE_BOTTOM)
		sq1_turn_bottom(state, SQ1_QUARTER_UNITS);
	else if (move == SQ1_P2_MOVE_SLICE)
		sq1_slice(state);
	else
	{
		sq1_turn_top(state, SQ1_SWITCH_TURN_IN);
		sq1_turn_bottom(state, SQ1_SWITCH_TURN_IN);
		sq1_slice(state);
		sq1_turn_top(state, SQ1_SWITCH_TURN_OUT);
		sq1_turn_bottom(state, SQ1_SWITCH_TURN_OUT);
	}
}

/// Builds the move tables by asking the engine what each move does to R,
/// whose slots hold the identity: where slot t's piece came FROM is what
/// the engine leaves there. A move only permutes slots, whatever they hold,
/// so every other permutation follows. Returns false if the slot reading
/// does not give the identity on R (the layout above is wrong).
static bool	build_move_tables(t_sq1_solver *solver)
{
	uint8_t		state[SQ1_STATE_SIZE];
	uint8_t		corner_src[SQ1_P2_MOVE_COUNT][SQ1_PERM_LEN];
	uint8_t		edge_src[SQ1_P2_MOVE_COUNT][SQ1_PERM_LEN];
	uint8_t		perm[SQ1_PERM_LEN];
	uint8_t		moved[SQ1_PERM_LEN];
	uint32_t	move;
	uint32_t	rank;
	uint32_t	slot;

	make_r(state);
	read_slots(state, corner_src[0], edge_src[0]);
	slot = 0;
	while (slot < SQ1_PERM_LEN)
	{
		if (corner_src[0][slot] != slot || edge_src[0][slot] != slot)
			return (false);
		slot++;
	}
	move = 0;
	while (move < SQ1_P2_MOVE_COUNT)
	{
		make_r(state);
		apply_p2_move(state, move);
		read_slots(state, corner_src[move], edge_src[move]);
		move++;
	}
	rank = 0;
	while (rank < SQ1_PERM_COUNT)
	{
		perm_unrank(rank, perm);
		move = 0;
		while (move < SQ1_P2_MOVE_COUNT)
		{
			slot = 0;
			while (slot < SQ1_PERM_LEN)
			{
				moved[slot] = perm[corner_src[move][slot]];
				slot++;
			}
			solver->move_corner[move][rank] = (uint16_t)perm_rank(moved);
			slot = 0;
			while (slot < SQ1_PERM_LEN)
			{
				moved[slot] = perm[edge_src[move][slot]];
				slot++;
			}
			solver->move_edge[move][rank] = (uint16_t)perm_rank(moved);
			move++;
		}
		rank++;
	}
	return (true);
}

/// The two moves a phase 2 step can end in, indexed by kind.
static const uint8_t	SLICE_KIND_MOVE[SQ1_P2_SLICE_KINDS] = {
	SQ1_P2_MOVE_SLICE, SQ1_P2_MOVE_SWITCH
};

/// Expands one cell of a prune table backwards: every state one step before
/// it. A step is "quarter turns, then a slice of either kind", so going
/// back is "slice of either kind, then quarter turns" -- the distance does
/// not change when a state is quarter-turned (the first step absorbs the
/// turn), so the sets stay closed under it and the order does not matter.
static void	table_prune_expand(uint16_t (*moves)[SQ1_PERM_COUNT],
	uint8_t *prune, uint32_t cell, uint32_t depth, uint32_t *found)
{
	uint32_t	kind;
	uint32_t	top;
	uint32_t	bottom;
	uint32_t	rank;
	uint32_t	after_top;
	uint32_t	middle;

	middle = (cell % SQ1_MIDDLE_STATES) ^ 1u;
	kind = 0;
	while (kind < SQ1_P2_SLICE_KINDS)
	{
		rank = moves[SLICE_KIND_MOVE[kind]][cell / SQ1_MIDDLE_STATES];
		top = 0;
		while (top < SQ1_QUARTER_COUNT)
		{
			after_top = rank;
			bottom = 0;
			while (bottom < SQ1_QUARTER_COUNT)
			{
				if (prune[after_top * SQ1_MIDDLE_STATES + middle]
					== SQ1_SHAPE_UNREACHED)
				{
					prune[after_top * SQ1_MIDDLE_STATES + middle]
						= (uint8_t)(depth + 1);
					(*found)++;
				}
				after_top = moves[SQ1_P2_MOVE_BOTTOM][after_top];
				bottom++;
			}
			rank = moves[SQ1_P2_MOVE_TOP][rank];
			top++;
		}
		kind++;
	}
}

/// Builds one prune table: slices to reach R up to turning the layers by
/// quarters, for the corners or the edges alone. Searched backwards from
/// the goal, where a step is "slice, then turn" (the reverse of the
/// search's "turn, then slice"; the set of turns is closed under inverses).
static void	build_prune(uint16_t (*moves)[SQ1_PERM_COUNT], uint8_t *prune)
{
	uint32_t	cell;
	uint32_t	top;
	uint32_t	bottom;
	uint32_t	rank;
	uint32_t	depth;
	uint32_t	found;

	cell = 0;
	while (cell < SQ1_PERM_COUNT * SQ1_MIDDLE_STATES)
		prune[cell++] = SQ1_SHAPE_UNREACHED;
	top = 0;
	rank = 0;
	while (top < SQ1_QUARTER_COUNT)
	{
		cell = rank;
		bottom = 0;
		while (bottom < SQ1_QUARTER_COUNT)
		{
			prune[cell * SQ1_MIDDLE_STATES] = 0;
			cell = moves[SQ1_P2_MOVE_BOTTOM][cell];
			bottom++;
		}
		rank = moves[SQ1_P2_MOVE_TOP][rank];
		top++;
	}
	depth = 0;
	found = 1;
	while (found > 0)
	{
		found = 0;
		cell = 0;
		while (cell < SQ1_PERM_COUNT * SQ1_MIDDLE_STATES)
		{
			if (prune[cell] == depth)
				table_prune_expand(moves, prune, cell, depth, &found);
			cell++;
		}
		depth++;
	}
}

bool	sq1_solver_init(t_sq1_solver *solver)
{
	uint32_t	reps[SQ1_SHAPE_CLASS_COUNT];

	solver->ready = false;
	if (!build_shape_classes(solver, reps))
		return (false);
	build_shape_distances(solver, reps);
	if (!build_move_tables(solver))
		return (false);
	build_prune(solver->move_corner, solver->prune_corner);
	build_prune(solver->move_edge, solver->prune_edge);
	solver->ready = true;
	return (true);
}

/* ---- the search ------------------------------------------------------- */

static uint32_t	max_u32(uint32_t a, uint32_t b)
{
	return (a > b ? a : b);
}

/// True once the node budget (set when a first solution exists) is spent.
static bool	budget_spent(t_sq1_search *ctx)
{
	ctx->nodes++;
	if (ctx->node_limit != 0 && ctx->nodes > ctx->node_limit)
		ctx->aborted = true;
	return (ctx->aborted);
}

/// At the end of phase 2: m must be back to 0 and some quarter-turn of the
/// layers must bring both permutations to the identity (R). Records which.
static bool	p2_is_goal(t_sq1_search *ctx, uint32_t corner, uint32_t edge,
	uint32_t middle)
{
	const t_sq1_solver	*s;
	uint32_t			top;
	uint32_t			bottom;
	uint32_t			c;
	uint32_t			e;
	uint32_t			c_row;
	uint32_t			e_row;

	if (middle != 0)
		return (false);
	s = ctx->solver;
	top = 0;
	c_row = corner;
	e_row = edge;
	while (top < SQ1_QUARTER_COUNT)
	{
		c = c_row;
		e = e_row;
		bottom = 0;
		while (bottom < SQ1_QUARTER_COUNT)
		{
			if (c == 0 && e == 0)
			{
				ctx->finish_top = (uint8_t)top;
				ctx->finish_bottom = (uint8_t)bottom;
				return (true);
			}
			c = s->move_corner[SQ1_P2_MOVE_BOTTOM][c];
			e = s->move_edge[SQ1_P2_MOVE_BOTTOM][e];
			bottom++;
		}
		c_row = s->move_corner[SQ1_P2_MOVE_TOP][c_row];
		e_row = s->move_edge[SQ1_P2_MOVE_TOP][e_row];
		top++;
	}
	return (false);
}

/// Depth-limited phase 2 search: `left` more slices, each preceded by a
/// quarter-turn count for each layer and of one of two kinds. Prunes by the
/// larger of the two tables. A plain slice straight after a slice (no
/// turns) undoes it, so it is skipped after the first step.
static bool	p2_dfs(t_sq1_search *ctx, uint32_t corner, uint32_t edge,
	uint32_t middle, uint32_t left, uint32_t depth)
{
	const t_sq1_solver	*s;
	uint32_t			top;
	uint32_t			bottom;
	uint32_t			kind;
	uint32_t			c_top;
	uint32_t			e_top;
	uint32_t			c;
	uint32_t			e;

	s = ctx->solver;
	if (max_u32(s->prune_corner[corner * SQ1_MIDDLE_STATES + middle],
			s->prune_edge[edge * SQ1_MIDDLE_STATES + middle]) > left
		|| budget_spent(ctx))
		return (false);
	if (left == 0)
		return (p2_is_goal(ctx, corner, edge, middle));
	c_top = corner;
	e_top = edge;
	top = 0;
	while (top < SQ1_QUARTER_COUNT)
	{
		c = c_top;
		e = e_top;
		bottom = 0;
		while (bottom < SQ1_QUARTER_COUNT)
		{
			kind = 0;
			while (kind < SQ1_P2_SLICE_KINDS)
			{
				if (kind == 1 || depth > 0 || top > 0 || bottom > 0)
				{
					ctx->p2_top[depth] = (uint8_t)top;
					ctx->p2_bottom[depth] = (uint8_t)bottom;
					ctx->p2_switch[depth] = (uint8_t)kind;
					if (p2_dfs(ctx,
							s->move_corner[SLICE_KIND_MOVE[kind]][c],
							s->move_edge[SLICE_KIND_MOVE[kind]][e],
							middle ^ 1u, left - 1, depth + 1))
						return (true);
					if (ctx->aborted)
						return (false);
				}
				kind++;
			}
			c = s->move_corner[SQ1_P2_MOVE_BOTTOM][c];
			e = s->move_edge[SQ1_P2_MOVE_BOTTOM][e];
			bottom++;
		}
		c_top = s->move_corner[SQ1_P2_MOVE_TOP][c_top];
		e_top = s->move_edge[SQ1_P2_MOVE_TOP][e_top];
		top++;
	}
	return (false);
}

/// Iterative deepening over phase 2: the fewest slices (at most `max_len`)
/// that solve this coordinate, or -1.
static int	p2_solve(t_sq1_search *ctx, uint32_t corner, uint32_t edge,
	uint32_t middle, uint32_t max_len)
{
	const t_sq1_solver	*s;
	uint32_t			len;

	s = ctx->solver;
	len = max_u32(s->prune_corner[corner * SQ1_MIDDLE_STATES + middle],
			s->prune_edge[edge * SQ1_MIDDLE_STATES + middle]);
	while (len <= max_len)
	{
		if (p2_dfs(ctx, corner, edge, middle, len, 0))
			return ((int)len);
		if (ctx->aborted)
			return (-1);
		len++;
	}
	return (-1);
}

/// How far a cube-shaped layer's pattern is turned: the smallest s with
/// the unit at index i the first half of a corner when (i + s) % 3 == 0.
/// The solved top layer has s = 0. Turning a layer clockwise by k units
/// subtracts k.
static uint32_t	cube_shift(uint32_t mask)
{
	uint32_t	first;

	first = 0;
	while ((mask >> first) & 1u)
		first++;
	return ((SQ1_CUBE_PATTERN_UNITS - first % SQ1_CUBE_PATTERN_UNITS)
		% SQ1_CUBE_PATTERN_UNITS);
}

/// The end of a phase 1 candidate: both layers are cube-shaped. Turn them
/// to the shifts phase 2 needs; if the permutation parity is one phase 2
/// can reach (corners and edges must agree: every phase 2 move changes
/// both or neither), solve the rest and keep it if it is the shortest yet.
static void	p1_leaf(t_sq1_search *ctx, const uint8_t *state, uint32_t p1_len)
{
	uint8_t		aligned[SQ1_STATE_SIZE];
	uint8_t		corner[SQ1_PERM_LEN];
	uint8_t		edge[SQ1_PERM_LEN];
	uint32_t	top_turn;
	uint32_t	bottom_turn;
	uint32_t	max_len;
	uint32_t	i;
	int			len2;
	bool		had_best;

	max_len = SQ1_P2_MAX_SLICES;
	if (ctx->have_best)
	{
		if (p1_len + 1 > ctx->best.total)
			return ;
		max_len = ctx->best.total - p1_len - 1;
	}
	i = 0;
	while (i < SQ1_STATE_SIZE)
	{
		aligned[i] = state[i];
		i++;
	}
	top_turn = (cube_shift(layer_mask(state)) + SQ1_CUBE_PATTERN_UNITS
			- P2_TOP_SHIFT) % SQ1_CUBE_PATTERN_UNITS;
	bottom_turn = (cube_shift(layer_mask(state + SQ1_LAYER_UNITS))
			+ SQ1_CUBE_PATTERN_UNITS - P2_BOTTOM_SHIFT)
		% SQ1_CUBE_PATTERN_UNITS;
	sq1_turn_top(aligned, top_turn);
	sq1_turn_bottom(aligned, (SQ1_LAYER_UNITS - bottom_turn) % SQ1_LAYER_UNITS);
	read_slots(aligned, corner, edge);
	if (perm_parity(corner) != perm_parity(edge))
		return ;
	len2 = p2_solve(ctx, perm_rank(corner), perm_rank(edge),
			aligned[SQ1_MIDDLE_INDEX], max_len);
	if (len2 < 0)
		return ;
	had_best = ctx->have_best;
	ctx->have_best = true;
	ctx->best.total = p1_len + (uint32_t)len2;
	ctx->best.p1_len = p1_len;
	ctx->best.p2_len = (uint32_t)len2;
	ctx->best.align_top = (uint8_t)top_turn;
	ctx->best.align_bottom
		= (uint8_t)((SQ1_LAYER_UNITS - bottom_turn) % SQ1_LAYER_UNITS);
	ctx->best.finish_top = ctx->finish_top;
	ctx->best.finish_bottom = ctx->finish_bottom;
	i = 0;
	while (i < SQ1_P1_MAX_SLICES)
	{
		ctx->best.p1_top[i] = ctx->p1_top[i];
		ctx->best.p1_bottom[i] = ctx->p1_bottom[i];
		i++;
	}
	i = 0;
	while (i < SQ1_P2_MAX_SLICES)
	{
		ctx->best.p2_top[i] = ctx->p2_top[i];
		ctx->best.p2_bottom[i] = ctx->p2_bottom[i];
		ctx->best.p2_switch[i] = ctx->p2_switch[i];
		i++;
	}
	if (!had_best)
		ctx->node_limit = ctx->nodes + SQ1_IMPROVE_NODE_BUDGET;
}

/// Depth-limited phase 1 search: `left` more slices to reach cube shape, a
/// layer turn pair before each. The shape table is exact, so a branch is
/// cut the moment it can no longer finish in the slices left.
static void	p1_dfs(t_sq1_search *ctx, const uint8_t *state, uint32_t left,
	uint32_t depth)
{
	uint8_t		next[SQ1_STATE_SIZE];
	uint32_t	top_mask;
	uint32_t	bottom_mask;
	uint32_t	top;
	uint32_t	bottom;
	uint32_t	i;

	top_mask = layer_mask(state);
	bottom_mask = layer_mask(state + SQ1_LAYER_UNITS);
	if (shape_distance_of(ctx->solver, top_mask, bottom_mask) > left
		|| budget_spent(ctx))
		return ;
	if (left == 0)
	{
		p1_leaf(ctx, state, depth);
		return ;
	}
	top = 0;
	while (top < SQ1_LAYER_UNITS)
	{
		bottom = 0;
		while (bottom < SQ1_LAYER_UNITS)
		{
			/// `bottom` is a generator amount: the layer turns by 12 - it
			/// seen from above.
			if (mask_is_clear_after(top_mask, top) && mask_is_clear_after(
					bottom_mask, (SQ1_LAYER_UNITS - bottom) % SQ1_LAYER_UNITS)
				&& (depth > 0 || top > 0 || bottom > 0))
			{
				i = 0;
				while (i < SQ1_STATE_SIZE)
				{
					next[i] = state[i];
					i++;
				}
				sq1_turn_top(next, top);
				sq1_turn_bottom(next, bottom);
				sq1_slice(next);
				ctx->p1_top[depth] = (uint8_t)top;
				ctx->p1_bottom[depth] = (uint8_t)bottom;
				p1_dfs(ctx, next, left - 1, depth + 1);
				if (ctx->aborted)
					return ;
			}
			bottom++;
		}
		top++;
	}
}

/* ---- turning a plan into moves ---------------------------------------- */

/// Turns are collected and merged until the next slice (or the end): two
/// turns of the same layer in a row are one move, and a turn of 0 is none.
typedef struct s_sq1_writer
{
	t_move		*out;
	uint32_t	*tags;
	uint32_t	max;
	uint32_t	count;
	uint32_t	pending_top;
	uint32_t	pending_bottom;
	bool		overflow;
}	t_sq1_writer;

/// Appends one move.
static void	write_move(t_sq1_writer *w, int16_t generator, uint32_t amount,
	uint32_t tag)
{
	if (w->count >= w->max)
	{
		w->overflow = true;
		return ;
	}
	w->out[w->count].generator = generator;
	w->out[w->count].amount = (int8_t)amount;
	w->tags[w->count] = tag;
	w->count++;
}

static void	queue_turns(t_sq1_writer *w, uint32_t top, uint32_t bottom)
{
	w->pending_top = (w->pending_top + top) % SQ1_LAYER_UNITS;
	w->pending_bottom = (w->pending_bottom + bottom) % SQ1_LAYER_UNITS;
}

/// Writes the merged pending turns, if any, then clears them.
static void	flush_turns(t_sq1_writer *w, uint32_t tag)
{
	if (w->pending_top != 0)
		write_move(w, SQ1_GEN_TOP, w->pending_top, tag);
	if (w->pending_bottom != 0)
		write_move(w, SQ1_GEN_BOTTOM, w->pending_bottom, tag);
	w->pending_top = 0;
	w->pending_bottom = 0;
}

/// A slice, after the turns queued for it; both carry the slice's phase.
static void	write_slice(t_sq1_writer *w, uint32_t tag)
{
	flush_turns(w, tag);
	write_move(w, SQ1_GEN_SLICE, 1, tag);
}

static void	write_plan(t_sq1_writer *w, const t_sq1_plan *plan)
{
	uint32_t	i;

	i = 0;
	while (i < plan->p1_len)
	{
		queue_turns(w, plan->p1_top[i], plan->p1_bottom[i]);
		write_slice(w, SQ1_PHASE_CUBE_SHAPE);
		i++;
	}
	/// The aligning turn, and every turn up to the next slice, belong to
	/// phase 2's first step: that is where the pieces start to be solved.
	queue_turns(w, plan->align_top, plan->align_bottom);
	i = 0;
	while (i < plan->p2_len)
	{
		queue_turns(w, plan->p2_top[i] * SQ1_QUARTER_UNITS,
			plan->p2_bottom[i] * SQ1_QUARTER_UNITS);
		if (plan->p2_switch[i])
			queue_turns(w, SQ1_SWITCH_TURN_IN, SQ1_SWITCH_TURN_IN);
		write_slice(w, SQ1_PHASE_FINISH);
		if (plan->p2_switch[i])
			queue_turns(w, SQ1_SWITCH_TURN_OUT, SQ1_SWITCH_TURN_OUT);
		i++;
	}
	/// The quarter-turns that bring the pieces to R, then R's bottom layer
	/// back to where the solved state has it.
	queue_turns(w, plan->finish_top * SQ1_QUARTER_UNITS,
		plan->finish_bottom * SQ1_QUARTER_UNITS + R_TO_SOLVED_AMOUNT);
	flush_turns(w, SQ1_PHASE_FINISH);
}

/// Replays `moves` on a copy of `state`; true iff it ends solved. The
/// solver's own proof that what it wrote is a solution.
static bool	moves_solve(const uint8_t *state, const t_move *moves,
	uint32_t count)
{
	uint8_t		work[SQ1_STATE_SIZE];
	uint8_t		solved[SQ1_STATE_SIZE];
	uint32_t	i;

	i = 0;
	while (i < SQ1_STATE_SIZE)
	{
		work[i] = state[i];
		i++;
	}
	i = 0;
	while (i < count)
	{
		if (!sq1_apply_move(work, moves[i].generator, moves[i].amount))
			return (false);
		i++;
	}
	sq1_init_solved(solved);
	i = 0;
	while (i < SQ1_STATE_SIZE)
	{
		if (work[i] != solved[i])
			return (false);
		i++;
	}
	return (true);
}

int	sq1_solver_solve(const t_sq1_solver *solver, const uint8_t *state,
	t_move *out, uint32_t max_moves, uint32_t *phase_tags, uint32_t *slices)
{
	t_sq1_search	ctx;
	t_sq1_writer	writer;
	uint32_t		len1;
	uint32_t		first;
	uint8_t			*bytes;
	uint32_t		i;

	if (!solver->ready || !sq1_state_is_valid(state))
		return (-1);
	bytes = (uint8_t *)&ctx;
	i = 0;
	while (i < sizeof(ctx))
		bytes[i++] = 0;
	ctx.solver = solver;
	first = shape_distance_of(solver, layer_mask(state),
			layer_mask(state + SQ1_LAYER_UNITS));
	if (first == SQ1_SHAPE_UNREACHED)
		return (-1);
	len1 = first;
	while (len1 <= SQ1_P1_MAX_SLICES && !ctx.aborted
		&& !(ctx.have_best && len1 >= ctx.best.total))
	{
		p1_dfs(&ctx, state, len1, 0);
		len1++;
	}
	if (!ctx.have_best)
		return (-1);
	writer.out = out;
	writer.tags = phase_tags;
	writer.max = max_moves;
	writer.count = 0;
	writer.pending_top = 0;
	writer.pending_bottom = 0;
	writer.overflow = false;
	write_plan(&writer, &ctx.best);
	if (writer.overflow || !moves_solve(state, out, writer.count))
		return (-1);
	if (slices)
		*slices = ctx.best.total;
	return ((int)writer.count);
}
