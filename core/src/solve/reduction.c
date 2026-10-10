#include "platform.h"
#include "move_list.h"
#include "puzzles/cubic.h"
#include "solve/cube_view.h"
#include "solve/kociemba.h"
#include "solve/registry.h"
#include "solve/reduction.h"

/// The reduction solve itself -- stages in order, each appending moves
/// to the run's raw list while applying them to its working state.
/// Tables (orbits, 3-cycle library) come from reduction_tables.c; the
/// method is described in solve/reduction.h's header comment.

/// Longest setup sequence a placement may use. The breadth-first search
/// finds the shortest one; in practice 0-3 moves. Hitting this bound
/// means something is broken, and the solve fails cleanly.
# define RED_MAX_SETUP 32

/// Longest middle-slice sequence the odd-n fixed-centre search tries.
/// Three moves reach all 24 rotations of the 6 fixed centres.
# define RED_FIXED_CENTRE_DEPTH 3

/// Shared working memory -- see t_red_scratch's doc comment for why it
/// is allocated once and kept.
static t_red_scratch	*g_scratch;

static void	emit(t_red_run *r, t_move m)
{
	if (r->count >= RED_RAW_CAPACITY)
	{
		r->overflow = true;
		return ;
	}
	red_apply(r->t, r->s->work, m);
	r->s->raw[r->count] = m;
	r->s->raw_tags[r->count] = r->phase;
	r->count++;
}

static uint32_t	offset_perm(const t_red_tables *t, uint32_t type)
{
	uint32_t	po;
	uint32_t	oo;

	shape_piece_offsets(t->shape, type, &po, &oo);
	return (po);
}

/// @brief Local index (0..23) of real slot `slot` within orbit `o`.
static uint32_t	local_of(const t_red_orbit *o, uint32_t slot)
{
	uint32_t	i;

	i = 0;
	while (i < RED_ORBIT_SIZE && o->slots[i] != slot)
		i++;
	return (i);
}

/// @brief Parity (0 even, 1 odd) of `perm` on `count` elements, by
///        counting cycles.
static uint32_t	parity(const uint8_t *perm, uint32_t count)
{
	uint8_t		seen[RED_ORBIT_SIZE];
	uint32_t	i;
	uint32_t	j;
	uint32_t	swaps;

	i = 0;
	while (i < count)
		seen[i++] = 0;
	swaps = 0;
	i = 0;
	while (i < count)
	{
		j = i;
		while (!seen[j])
		{
			seen[j] = 1;
			j = perm[j];
			if (!seen[j])
				swaps++;
		}
		i++;
	}
	return (swaps % 2);
}

/// @brief Which local slot's piece sits in each local slot of `o`.
static void	orbit_pieces(const t_red_run *r, const t_red_orbit *o,
	uint8_t *pieces)
{
	uint32_t	po;
	uint32_t	i;

	po = offset_perm(r->t, o->piece_type);
	i = 0;
	while (i < RED_ORBIT_SIZE)
	{
		pieces[i] = (uint8_t)local_of(o, r->s->work[po + o->slots[i]]);
		i++;
	}
}

/// @brief Odd n: turns the six fixed centres home with at most
///        RED_FIXED_CENTRE_DEPTH middle-layer moves -- they can only be
///        out of place by a whole-cube rotation, and three moves reach
///        every one of the 24.
static bool	fixed_centres_home(const t_red_run *r, const uint8_t *state)
{
	uint32_t	po;
	uint32_t	s;

	po = offset_perm(r->t, CUBIC_CENTRE_TYPE);
	s = 0;
	while (s < r->t->shape->piece_types[CUBIC_CENTRE_TYPE].count)
	{
		if (r->t->orbit_of[CUBIC_CENTRE_TYPE][s] == RED_NO_ORBIT
			&& state[po + s] != s)
			return (false);
		s++;
	}
	return (true);
}

static bool	solve_fixed_centres(t_red_run *r)
{
	uint8_t		trial[RED_MAX_STATE];
	uint32_t	code;
	uint32_t	limit;
	uint32_t	depth;
	uint32_t	k;
	uint32_t	c;
	t_move		seq[RED_FIXED_CENTRE_DEPTH];
	uint32_t	size;
	uint32_t	i;

	size = shape_state_size(r->t->shape);
	depth = 0;
	limit = 1;
	while (depth <= RED_FIXED_CENTRE_DEPTH)
	{
		code = 0;
		while (code < limit)
		{
			i = 0;
			while (i < size)
			{
				trial[i] = r->s->work[i];
				i++;
			}
			c = code;
			k = 0;
			while (k < depth)
			{
				seq[k] = (t_move){(int16_t)r->t->fixed_centre_moves[c % 9 / 3],
					(int8_t)(c % 3 + 1)};
				red_apply(r->t, trial, seq[k]);
				c /= 9;
				k++;
			}
			if (fixed_centres_home(r, trial))
			{
				k = 0;
				while (k < depth)
					emit(r, seq[k++]);
				return (true);
			}
			code++;
		}
		depth++;
		limit *= 9;
	}
	return (false);
}

/// @brief Even n only: a corner permutation of odd parity can't be
///        solved by a 3x3x3 whose edges (the paired wings) are all home,
///        so one U quarter turn flips it first. (Odd n never needs this:
///        with the fixed centres home, corner and midge parity always
///        agree -- every quarter turn changes both or, for a middle
///        slice, changes centres too.)
static void	fix_corner_parity(t_red_run *r)
{
	if (parity(r->s->work + offset_perm(r->t, CUBIC_CORNER_TYPE), 8))
		emit(r, (t_move){(int16_t)cubic_generator_index(r->t->n,
				CUBIC_GEN_U, 0), 1});
}

/// @brief A wing orbit with odd permutation parity can't be solved by
///        3-cycles, so one inner-layer quarter turn that moves only that
///        orbit's wings (a 4-cycle: odd) fixes it first. Inner non-middle
///        layers never touch corners, midges or fixed centres; it does
///        stir centres, which are solved last anyway.
static bool	fix_wing_parity(t_red_run *r, const t_red_orbit *o)
{
	uint8_t		pieces[RED_ORBIT_SIZE];
	uint32_t	m;
	uint32_t	face;
	uint32_t	depth;

	orbit_pieces(r, o, pieces);
	if (!parity(pieces, RED_ORBIT_SIZE))
		return (true);
	m = 0;
	while (m < r->t->move_count)
	{
		cubic_generator_layer(r->t->n, r->t->moves[m].generator, &face, &depth);
		if (depth > 0 && r->t->moves[m].amount == 1
			&& parity(o->perm + m * RED_ORBIT_SIZE, RED_ORBIT_SIZE))
		{
			emit(r, r->t->moves[m]);
			return (true);
		}
		m++;
	}
	return (false);
}

/// @brief Breadth-first search for the shortest setup S carrying some
///        start triple (a, b, x) onto a library cycle's (p, q, r).
///        `starts` are packed triple indices. On success writes S's
///        move indices into `path` and the cycle into `cycle`.
static bool	find_setup(t_red_run *r, const t_red_orbit *o,
	const int32_t *starts, uint32_t start_count, uint32_t *path,
	uint32_t *path_len, int16_t *cycle)
{
	t_red_scratch	*s;
	uint32_t		head;
	uint32_t		tail;
	int32_t			node;
	int32_t			next;
	uint32_t		m;
	const uint8_t	*pm;

	s = r->s;
	node = 0;
	while (node < RED_TRIPLES)
		s->via[node++] = -2;
	head = 0;
	tail = 0;
	while (tail < start_count)
	{
		s->via[starts[tail]] = -1;
		s->queue[tail] = starts[tail];
		tail++;
	}
	while (head < tail)
	{
		node = s->queue[head++];
		if (o->goal[node] >= 0)
		{
			*cycle = o->goal[node];
			*path_len = 0;
			while (s->via[node] >= 0 && *path_len < RED_MAX_SETUP)
			{
				path[(*path_len)++] = (uint32_t)s->via[node];
				node = s->parent[node];
			}
			return (s->via[node] < 0);
		}
		m = 0;
		while (m < r->t->move_count)
		{
			pm = o->perm + m * RED_ORBIT_SIZE;
			next = (pm[node / (RED_ORBIT_SIZE * RED_ORBIT_SIZE)]
					* RED_ORBIT_SIZE + pm[node / RED_ORBIT_SIZE % RED_ORBIT_SIZE])
				* RED_ORBIT_SIZE + pm[node % RED_ORBIT_SIZE];
			if (s->via[next] == -2)
			{
				s->via[next] = (int16_t)m;
				s->parent[next] = node;
				s->queue[tail++] = next;
			}
			m++;
		}
	}
	return (false);
}

/// @brief Emits S, the cycle's commutator, then S inverse. `path` holds
///        S backwards (as find_setup() walks it).
static void	emit_placement(t_red_run *r, const t_red_orbit *o,
	const uint32_t *path, uint32_t path_len, int16_t cycle)
{
	t_move		comm[RED_COMM_LENGTH];
	uint32_t	i;

	i = path_len;
	while (i > 0)
		emit(r, r->t->moves[path[--i]]);
	red_expand_comm(r->t, o->cycles[cycle].comm, o->cycles[cycle].inverted,
		comm);
	i = 0;
	while (i < RED_COMM_LENGTH)
		emit(r, comm[i++]);
	i = 0;
	while (i < path_len)
		emit(r, red_inverse(r->t, r->t->moves[path[i++]]));
}

static int32_t	triple(uint32_t a, uint32_t b, uint32_t x)
{
	return ((int32_t)((a * RED_ORBIT_SIZE + b) * RED_ORBIT_SIZE + x));
}

/// @brief Places every wing of orbit `o` in its home slot, one 3-cycle
///        per misplaced wing, in local slot order. A slot is final once
///        it holds its own wing; the cycle's third slot is always
///        chosen from the slots not yet final. Parity was fixed first,
///        so the last two slots can never be left swapped.
static bool	solve_wing_orbit(t_red_run *r, const t_red_orbit *o)
{
	uint8_t		pieces[RED_ORBIT_SIZE];
	int32_t		starts[RED_ORBIT_SIZE];
	uint32_t	path[RED_MAX_SETUP];
	uint32_t	path_len;
	int16_t		cycle;
	uint32_t	b;
	uint32_t	a;
	uint32_t	x;
	uint32_t	n_starts;

	b = 0;
	while (b < RED_ORBIT_SIZE && !r->overflow)
	{
		orbit_pieces(r, o, pieces);
		if (pieces[b] != b)
		{
			a = 0;
			while (pieces[a] != b)
				a++;
			n_starts = 0;
			x = b + 1;
			while (x < RED_ORBIT_SIZE)
			{
				if (x != a)
					starts[n_starts++] = triple(a, b, x);
				x++;
			}
			if (!find_setup(r, o, starts, n_starts, path, &path_len, &cycle))
				return (false);
			emit_placement(r, o, path, path_len, cycle);
			orbit_pieces(r, o, pieces);
			if (pieces[b] != b)
				return (false);
		}
		b++;
	}
	return (true);
}

/// @brief Centre slots of `o` in solving order: by face (U R F D L B),
///        then local index -- so whole faces finish one at a time and
///        the last face falls into place on its own.
static void	centre_order(const t_red_orbit *o, uint8_t *order)
{
	uint32_t	face;
	uint32_t	i;
	uint32_t	k;

	k = 0;
	face = 0;
	while (face < CUBIC_FACE_COUNT)
	{
		i = 0;
		while (i < RED_ORBIT_SIZE)
		{
			if (o->colour[i] == face)
				order[k++] = (uint8_t)i;
			i++;
		}
		face++;
	}
}

/// @brief Fills every centre slot of orbit `o` with a centre of its own
///        colour. Centres of one colour are interchangeable, so any
///        right-coloured piece outside the finished slots can be the
///        source, and the search is free to pick whichever source and
///        third slot give the shortest setup.
static bool	solve_centre_orbit(t_red_run *r, const t_red_orbit *o)
{
	uint8_t		pieces[RED_ORBIT_SIZE];
	uint8_t		order[RED_ORBIT_SIZE];
	uint8_t		done[RED_ORBIT_SIZE];
	int32_t		starts[RED_ORBIT_SIZE * RED_ORBIT_SIZE];
	uint32_t	path[RED_MAX_SETUP];
	uint32_t	path_len;
	int16_t		cycle;
	uint32_t	k;
	uint32_t	b;
	uint32_t	a;
	uint32_t	x;
	uint32_t	n_starts;

	centre_order(o, order);
	k = 0;
	while (k < RED_ORBIT_SIZE)
		done[k++] = 0;
	k = 0;
	while (k < RED_ORBIT_SIZE && !r->overflow)
	{
		b = order[k];
		orbit_pieces(r, o, pieces);
		if (o->colour[pieces[b]] != o->colour[b])
		{
			n_starts = 0;
			a = 0;
			while (a < RED_ORBIT_SIZE)
			{
				x = 0;
				while (!done[a] && a != b && o->colour[pieces[a]] == o->colour[b]
					&& x < RED_ORBIT_SIZE)
				{
					if (!done[x] && x != a && x != b)
						starts[n_starts++] = triple(a, b, x);
					x++;
				}
				a++;
			}
			if (n_starts == 0
				|| !find_setup(r, o, starts, n_starts, path, &path_len, &cycle))
				return (false);
			emit_placement(r, o, path, path_len, cycle);
			orbit_pieces(r, o, pieces);
			if (o->colour[pieces[b]] != o->colour[b])
				return (false);
		}
		done[b] = 1;
		k++;
	}
	return (true);
}

/// @brief Reads the cube's 3x3x3 part as a t_solve_cube: corners
///        straight from the state (same layout as a 3x3x3's), edges from
///        the midges on odd n. Even n has no midges, so its edges read as
///        solved -- which is why even n fixes corner parity first (a
///        3x3x3 with solved edges must have even corner parity).
static void	reduced_cube(const t_red_run *r, t_solve_cube *cube)
{
	uint32_t	po;
	uint32_t	oo;
	uint32_t	i;
	uint32_t	e;

	shape_piece_offsets(r->t->shape, CUBIC_CORNER_TYPE, &po, &oo);
	i = 0;
	while (i < SOLVE_CORNER_COUNT)
	{
		cube->corner_perm[i] = r->s->work[po + i];
		cube->corner_orient[i] = r->s->work[oo + i];
		i++;
	}
	shape_piece_offsets(r->t->shape, CUBIC_WING_TYPE, &po, &oo);
	i = 0;
	while (i < SOLVE_EDGE_COUNT)
	{
		cube->edge_perm[i] = (uint8_t)i;
		cube->edge_orient[i] = 0;
		if (r->t->n % 2 == 1)
		{
			e = 0;
			while (r->t->midge_slot[e] != r->s->work[po + r->t->midge_slot[i]])
				e++;
			cube->edge_perm[i] = (uint8_t)e;
			cube->edge_orient[i] = r->s->work[oo + r->t->midge_slot[i]];
		}
		i++;
	}
}

/// @brief The 3x3x3 stage: Kociemba on corners + midges, its face turns
///        mapped to this size's depth-0 generators. Wings and centres
///        get stirred up along the way; the 3-cycle stages after this
///        never touch corners or midges, so they stay solved.
static bool	solve_reduced(t_red_run *r)
{
	const t_kociemba_solver	*koc;
	t_solve_cube			cube;
	int						moves[KOCIEMBA_MAX_MOVES];
	int						phase1;
	int						count;
	int						i;

	koc = registry_kociemba();
	if (!koc)
		return (false);
	reduced_cube(r, &cube);
	count = kociemba_solve(koc, &cube, moves, &phase1);
	if (count < 0)
		return (false);
	i = 0;
	while (i < count)
	{
		r->phase = REDUCTION_PHASE_G1;
		if (i >= phase1)
			r->phase = REDUCTION_PHASE_FINISH;
		emit(r, (t_move){(int16_t)cubic_generator_index(r->t->n,
				(uint32_t)(moves[i] / 3), 0), (int8_t)(moves[i] % 3 + 1)});
		i++;
	}
	return (true);
}

/// @brief Stage order. The 3x3x3 stage runs FIRST, not last: corners
///        and midges are the only pieces it has to solve, and every
///        later stage is made of pure 3-cycles that never touch them --
///        so placing wings at their absolute home slots afterwards is
///        always right. (Doing it in the textbook order, wings first,
///        breaks on odd n: wings home but midges not, and the 3x3x3
///        stage then drags the wings away again. DECISIONS.md,
///        2026-10-04, "Reduction built".)
static bool	run_stages(t_red_run *r)
{
	uint32_t	i;

	r->phase = REDUCTION_PHASE_SETUP;
	if (r->t->n % 2 == 1 && !solve_fixed_centres(r))
		return (false);
	if (r->t->n % 2 == 0)
		fix_corner_parity(r);
	if (!solve_reduced(r))
		return (false);
	r->phase = REDUCTION_PHASE_EDGES;
	i = 0;
	while (i < r->t->orbit_count)
	{
		if (r->t->orbits[i].piece_type == CUBIC_WING_TYPE
			&& (!fix_wing_parity(r, &r->t->orbits[i])
				|| !solve_wing_orbit(r, &r->t->orbits[i])))
			return (false);
		i++;
	}
	r->phase = REDUCTION_PHASE_CENTRES;
	i = 0;
	while (i < r->t->orbit_count)
	{
		if (r->t->orbits[i].piece_type == CUBIC_CENTRE_TYPE
			&& !solve_centre_orbit(r, &r->t->orbits[i]))
			return (false);
		i++;
	}
	return (!r->overflow);
}

uint32_t	reduction_solve(uint32_t n, const uint8_t *state, t_move *out,
	uint32_t max_moves, uint32_t *phase_tags)
{
	t_red_run	r;
	uint32_t	size;
	uint32_t	i;

	r = (t_red_run){reduction_tables_for(n), NULL, 0, 0, false};
	if (!r.t)
		return (UINT32_MAX);
	if (!g_scratch)
		g_scratch = malloc(sizeof(*g_scratch));
	if (!g_scratch)
		return (UINT32_MAX);
	r.s = g_scratch;
	size = shape_state_size(r.t->shape);
	i = 0;
	while (i < size)
	{
		r.s->work[i] = state[i];
		i++;
	}
	if (!run_stages(&r))
		return (UINT32_MAX);
	r.count = move_list_merge(r.s->raw, r.s->raw_tags, r.count,
			r.t->gens->steps);
	if (r.count > max_moves)
		return (UINT32_MAX);
	i = 0;
	while (i < r.count)
	{
		out[i] = r.s->raw[i];
		phase_tags[i] = r.s->raw_tags[i];
		i++;
	}
	return (r.count);
}

/// Worst case measured over 200 random scrambles per size (DECISIONS.md,
/// 2026-10-04, "Reduction built": 363 / 506 / 944 / 1190 / 1783 for
/// n = 4..8), times ~1.5 for headroom -- test_reduction.c re-checks
/// every solve against these. Indexed by n - REDUCTION_MIN_N.
static const uint32_t	REDUCTION_MAX_MOVES[REDUCTION_MAX_N - REDUCTION_MIN_N + 1]
	= {600, 800, 1500, 1800, 2700};

uint32_t	reduction_max_moves(uint32_t n)
{
	if (!reduction_supports(n))
		return (0);
	return (REDUCTION_MAX_MOVES[n - REDUCTION_MIN_N]);
}
