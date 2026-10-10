#include "platform.h"
#include "solve/dodeca_solver.h"
#include "solve/bytes.h"
#include "puzzles/dodecahedral.h"

int	dodeca_solver_ready_corners(t_dodeca_solver *s)
{
	if (!s->corners_ready)
		s->corners_ready = dodeca_table_init(&s->corners,
				dodeca_family(DODECA_KILOMINX_SIZE), DODECA_CORNER_TYPE);
	return (s->corners_ready);
}

int	dodeca_solver_ready_edges(t_dodeca_solver *s)
{
	if (!s->edges_ready)
		s->edges_ready = dodeca_table_init(&s->edges,
				dodeca_family(DODECA_MEGAMINX_SIZE), DODECA_EDGE_TYPE);
	return (s->edges_ready);
}

static void	view_read(const t_dodeca_table *t, const uint8_t *state,
				t_type_view *v)
{
	uint32_t	perm_off;
	uint32_t	orient_off;
	uint32_t	s;

	shape_piece_offsets(t->shape, t->type, &perm_off, &orient_off);
	s = 0;
	while (s < t->slots)
	{
		v->perm[s] = state[perm_off + s];
		v->orient[s] = state[orient_off + s];
		s++;
	}
}

/// Applies one element to a view, in t_cycle's convention (the piece in
/// slot[i] moves to slot[i + 1], and delta[i] is added to the piece that
/// lands in slot[i]) -- the same rule shape_apply_cycle() follows on the
/// real buffer, so the prediction and the real result can be compared.
static void	view_apply(const t_dodeca_table *t, t_type_view *v,
				const t_dodeca_node *n)
{
	uint8_t	perm[3];
	uint8_t	orient[3];
	int		i;

	i = 0;
	while (i < 3)
	{
		perm[i] = v->perm[n->slot[i]];
		orient[i] = v->orient[n->slot[i]];
		i++;
	}
	i = 0;
	while (i < 3)
	{
		v->perm[n->slot[(i + 1) % 3]] = perm[i];
		v->orient[n->slot[(i + 1) % 3]] = (uint8_t)((orient[i]
					+ n->delta[(i + 1) % 3]) % t->orients);
		i++;
	}
}

/// True when slots 0..upto of the view hold their own pieces untwisted:
/// the solved prefix a placement must leave intact, plus the piece it
/// just placed.
static int	prefix_solved(const t_type_view *v, uint32_t upto)
{
	uint32_t	s;

	s = 0;
	while (s <= upto)
	{
		if (v->perm[s] != s || v->orient[s] != 0)
			return (0);
		s++;
	}
	return (1);
}

static uint32_t	node_cost(const t_dodeca_table *t, const t_dodeca_node *n)
{
	return (t->root_len + 2 * (uint32_t)n->depth);
}

/// The twist `delta` value that makes three twists sum to zero modulo
/// the type's orientation count, given the other two.
static uint8_t	closing_delta(const t_dodeca_table *t, uint32_t a, uint32_t b)
{
	return ((uint8_t)((2 * t->orients - (a + b) % t->orients) % t->orients));
}

/// Offers a candidate (one or two elements) to `best` if it leaves slots
/// 0..k solved when applied to `view` and is shorter than what is there.
static void	offer(const t_dodeca_table *t, const t_type_view *view,
				uint32_t k, const t_dodeca_node *first,
				const t_dodeca_node *second, t_step_choice *best)
{
	t_type_view	v;
	uint32_t	cost;

	if (!first || (second && !first))
		return ;
	cost = node_cost(t, first) + (second ? node_cost(t, second) : 0);
	if (cost >= best->length)
		return ;
	v = *view;
	view_apply(t, &v, first);
	if (second)
		view_apply(t, &v, second);
	if (!prefix_solved(&v, k))
		return ;
	best->first = first;
	best->second = second;
	best->length = cost;
}

/// Piece k is in slot s != k: one element on the cycle s -> k -> z for
/// every third slot z and every twist the cycle's remaining slot can
/// take. The twist that lands on k is fixed by what k needs (its own
/// twist undone); the other two are free up to summing to zero.
static void	try_single(const t_dodeca_table *t, const t_type_view *view,
				uint32_t k, uint32_t s, t_step_choice *best)
{
	uint8_t		slot[3];
	uint8_t		delta[3];
	uint32_t	z;
	uint32_t	dz;

	slot[0] = (uint8_t)s;
	slot[1] = (uint8_t)k;
	delta[1] = (uint8_t)((t->orients - view->orient[s]) % t->orients);
	z = 0;
	while (z < t->slots)
	{
		slot[2] = (uint8_t)z;
		dz = 0;
		while (z != s && z != k && dz < t->orients)
		{
			delta[2] = (uint8_t)dz;
			delta[0] = closing_delta(t, delta[1], delta[2]);
			offer(t, view, k, dodeca_table_find(t, slot, delta), NULL, best);
			dz++;
		}
		z++;
	}
}

/// Piece k is already home but twisted (or no single element fixes it):
/// two elements on the same three slots, in opposite directions, so the
/// pieces return to where they were and only twists change. Every pair
/// of other slots (z, w) and every twist pattern of both elements is
/// offered; `offer` keeps the shortest that leaves slots 0..k solved --
/// which, for a twist, may touch a solved slot as long as it gives its
/// twist back.
static void	try_pair(const t_dodeca_table *t, const t_type_view *view,
				uint32_t k, t_step_choice *best)
{
	uint8_t		a[3];
	uint8_t		b[3];
	uint8_t		da[3];
	uint8_t		db[3];
	uint32_t	z;
	uint32_t	w;
	uint32_t	i;

	a[0] = (uint8_t)k;
	b[0] = (uint8_t)k;
	z = 0;
	while (z < t->slots)
	{
		w = 0;
		while (w < t->slots)
		{
			if (z != k && w != k && z != w)
			{
				a[1] = (uint8_t)z;
				a[2] = (uint8_t)w;
				b[1] = (uint8_t)w;
				b[2] = (uint8_t)z;
				i = 0;
				while (i < t->orients * t->orients * t->orients
					* t->orients)
				{
					da[1] = (uint8_t)(i % t->orients);
					da[2] = (uint8_t)(i / t->orients % t->orients);
					db[1] = (uint8_t)(i / (t->orients * t->orients)
							% t->orients);
					db[2] = (uint8_t)(i / (t->orients * t->orients
								* t->orients));
					da[0] = closing_delta(t, da[1], da[2]);
					db[0] = closing_delta(t, db[1], db[2]);
					offer(t, view, k, dodeca_table_find(t, a, da),
						dodeca_table_find(t, b, db), best);
					i++;
				}
			}
			w++;
		}
		z++;
	}
}

/// Adds one move to the solution, merging it into the previous one when
/// it turns the same face in the same phase. Returns 0 if the cap is hit.
static int	emit_move(t_solve_run *run, t_move move, uint32_t phase)
{
	t_move	*last;
	int		amount;

	if (run->count > 0 && run->phase_tags[run->count - 1] == phase
		&& run->out[run->count - 1].generator == move.generator)
	{
		last = &run->out[run->count - 1];
		amount = (last->amount + move.amount) % (DODECA_STEPS + 1);
		if (amount == 0)
			run->count--;
		else
			last->amount = (int8_t)amount;
		return (1);
	}
	if (run->count >= run->max)
		return (0);
	run->out[run->count] = move;
	run->phase_tags[run->count] = phase;
	run->count++;
	return (1);
}

/// Applies an element's word to the real state and records it.
static int	run_node(const t_dodeca_table *t, const t_cut_family *family,
				uint8_t *state, const t_dodeca_node *node, t_solve_run *run,
				uint32_t phase)
{
	t_move		word[DODECA_WORD_MAX];
	uint32_t	len;
	uint32_t	i;

	len = dodeca_table_word(t, node, word, DODECA_WORD_MAX);
	if (len == UINT32_MAX)
		return (0);
	i = 0;
	while (i < len)
	{
		shape_apply_move(family->shape, family->generators, state, word[i]);
		if (!emit_move(run, word[i], phase))
			return (0);
		i++;
	}
	return (1);
}

static int	views_equal(const t_dodeca_table *t, const t_type_view *a,
				const t_type_view *b)
{
	uint32_t	s;

	s = 0;
	while (s < t->slots)
	{
		if (a->perm[s] != b->perm[s] || a->orient[s] != b->orient[s])
			return (0);
		s++;
	}
	return (1);
}

/// Puts piece k home and untwisted without disturbing pieces 0..k-1.
/// @return 1 on success (including "already there"), 0 if nothing in the
///         table does it (an unreachable state) or the cap is hit or the
///         real result differs from the prediction.
static int	place_piece(const t_dodeca_table *t, const t_cut_family *family,
				uint8_t *state, uint32_t k, t_solve_run *run, uint32_t phase)
{
	t_type_view		view;
	t_type_view		predicted;
	t_step_choice	best;
	uint32_t		s;

	view_read(t, state, &view);
	if (prefix_solved(&view, k) || (view.perm[k] == k && view.orient[k] == 0))
		return (1);
	s = 0;
	while (view.perm[s] != k)
		s++;
	best.first = NULL;
	best.second = NULL;
	best.length = UINT32_MAX;
	if (s != k)
		try_single(t, &view, k, s, &best);
	if (!best.first)
		try_pair(t, &view, k, &best);
	if (!best.first)
		return (0);
	predicted = view;
	view_apply(t, &predicted, best.first);
	if (best.second)
		view_apply(t, &predicted, best.second);
	if (!run_node(t, family, state, best.first, run, phase)
		|| (best.second && !run_node(t, family, state, best.second, run,
				phase)))
		return (0);
	view_read(t, state, &view);
	return (views_equal(t, &view, &predicted));
}

/// Solves every piece of the table's type: slots 0 to n-2 by placement,
/// and the last one is then solved by the group's own invariants (its
/// permutation is forced, and its twist makes the total zero) -- which
/// is checked, not assumed.
static int	solve_type(const t_dodeca_table *t, const t_cut_family *family,
				uint8_t *state, t_solve_run *run, uint32_t phase)
{
	t_type_view	view;
	uint32_t	k;

	k = 0;
	while (k + 1 < t->slots)
	{
		if (!place_piece(t, family, state, k, run, phase))
			return (0);
		k++;
	}
	view_read(t, state, &view);
	return (prefix_solved(&view, t->slots - 1));
}

static int	is_solved(const t_cut_family *family, const uint8_t *state)
{
	uint8_t		solved[DODECA_STATE_MAX];
	uint32_t	i;

	shape_init_solved(family->shape, solved);
	i = 0;
	while (i < shape_state_size(family->shape))
	{
		if (solved[i] != state[i])
			return (0);
		i++;
	}
	return (1);
}

int	dodeca_solve(t_dodeca_solver *solver, uint32_t size, const uint8_t *state,
		t_move *out, uint32_t max_moves, uint32_t *phase_tags)
{
	const t_cut_family	*family;
	uint8_t				buf[DODECA_STATE_MAX];
	t_solve_run			run;

	family = dodeca_family(size);
	if (!family || !dodeca_solver_ready_corners(solver)
		|| (size == DODECA_MEGAMINX_SIZE && !dodeca_solver_ready_edges(solver)))
		return (-1);
	solve_copy_bytes(buf, state, shape_state_size(family->shape));
	run.out = out;
	run.phase_tags = phase_tags;
	run.count = 0;
	run.max = max_moves;
	if (!solve_type(&solver->corners, family, buf, &run, DODECA_PHASE_CORNERS))
		return (-1);
	if (size == DODECA_MEGAMINX_SIZE
		&& !solve_type(&solver->edges, family, buf, &run,
			DODECA_PHASE_EDGES))
		return (-1);
	if (!is_solved(family, buf))
		return (-1);
	return ((int)run.count);
}
