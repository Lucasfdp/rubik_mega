#include "platform.h"
#include "puzzles/cubic.h"
#include "puzzles/cubic_facelets.h"
#include "solve/reduction.h"

/// Builds and caches everything reduction needs for one size: the move
/// alphabet, per-move slot permutations, the 24-slot orbits, and each
/// orbit's library of proven-pure 3-cycles (docs/en/08-nxn-reduction.md;
/// see solve/reduction.h's header comment for the method).

static const t_red_tables	*g_tables[REDUCTION_MAX_N + 1];
static bool					g_tables_failed[REDUCTION_MAX_N + 1];

void	red_apply(const t_red_tables *t, uint8_t *state, t_move move)
{
	const t_generator	*gen;
	uint32_t			c;
	int					turn;

	gen = &t->gens->generators[move.generator];
	turn = 0;
	while (turn < move.amount)
	{
		c = 0;
		while (c < gen->cycle_count)
			shape_apply_cycle(t->shape, state, &gen->cycles[c++]);
		turn++;
	}
}

t_move	red_inverse(const t_red_tables *t, t_move move)
{
	return ((t_move){move.generator,
		(int8_t)((int)t->gens->steps + 1 - move.amount)});
}

void	red_expand_comm(const t_red_tables *t, t_red_comm c, bool inverted,
	t_move *out)
{
	t_move	seq[RED_COMM_LENGTH];
	int		i;

	seq[0] = c.x;
	seq[1] = c.a;
	seq[2] = c.b;
	seq[3] = red_inverse(t, c.a);
	seq[4] = red_inverse(t, c.x);
	seq[5] = c.a;
	seq[6] = red_inverse(t, c.b);
	seq[7] = red_inverse(t, c.a);
	i = 0;
	while (i < RED_COMM_LENGTH)
	{
		if (inverted)
			out[i] = red_inverse(t, seq[RED_COMM_LENGTH - 1 - i]);
		else
			out[i] = seq[i];
		i++;
	}
}

/// @brief Every (generator, amount) pair, generator-major.
static void	build_moves(t_red_tables *t)
{
	uint32_t	g;
	uint32_t	a;

	t->move_count = t->gens->count * t->gens->steps;
	t->moves = malloc(sizeof(t_move) * t->move_count);
	g = 0;
	while (g < t->gens->count)
	{
		a = 0;
		while (a < t->gens->steps)
		{
			t->moves[g * t->gens->steps + a] = (t_move){(int16_t)g,
				(int8_t)(a + 1)};
			a++;
		}
		g++;
	}
}

/// @brief slot_perm for one piece type: simulate each move on a
///        permutation-only copy of that type's slots, then invert
///        "which piece is here" into "where did each piece go".
static void	build_slot_perm(t_red_tables *t, uint32_t type)
{
	uint32_t		count;
	uint8_t			*at;
	uint8_t			*next;
	uint32_t		m;
	uint32_t		i;
	const t_cycle	*cyc;
	uint32_t		c;
	int				turn;
	uint32_t		k;

	count = t->shape->piece_types[type].count;
	t->slot_perm[type] = malloc(t->move_count * count);
	at = malloc(count);
	next = malloc(count);
	m = 0;
	while (m < t->move_count)
	{
		i = 0;
		while (i < count)
		{
			at[i] = (uint8_t)i;
			i++;
		}
		turn = 0;
		while (turn < t->moves[m].amount)
		{
			c = 0;
			while (c < t->gens->generators[t->moves[m].generator].cycle_count)
			{
				cyc = &t->gens->generators[t->moves[m].generator].cycles[c++];
				if (cyc->piece_type != type)
					continue ;
				k = 0;
				while (k < cyc->length)
				{
					next[k] = at[cyc->slots[k]];
					k++;
				}
				k = 0;
				while (k < cyc->length)
				{
					at[cyc->slots[(k + 1) % cyc->length]] = next[k];
					k++;
				}
			}
			turn++;
		}
		i = 0;
		while (i < count)
		{
			t->slot_perm[type][m * count + at[i]] = (uint8_t)i;
			i++;
		}
		m++;
	}
	free(at);
	free(next);
}

static uint32_t	uf_find(uint8_t *parent, uint32_t x)
{
	while (parent[x] != x)
		x = parent[x];
	return (x);
}

/// @brief Groups one piece type's slots into orbits (slots any sequence
///        of moves can carry a piece between) and registers every
///        24-slot orbit; smaller ones (midges, fixed centres) are
///        marked RED_NO_ORBIT.
static void	build_orbits(t_red_tables *t, uint32_t type)
{
	uint32_t	count;
	uint8_t		*parent;
	uint32_t	*size;
	uint32_t	m;
	uint32_t	s;
	uint32_t	root;
	t_red_orbit	*o;
	uint32_t	k;

	count = t->shape->piece_types[type].count;
	parent = malloc(count);
	size = calloc(count, sizeof(uint32_t));
	t->orbit_of[type] = malloc(count);
	s = 0;
	while (s < count)
	{
		parent[s] = (uint8_t)s;
		t->orbit_of[type][s] = RED_NO_ORBIT;
		s++;
	}
	m = 0;
	while (m < t->move_count)
	{
		s = 0;
		while (s < count)
		{
			root = uf_find(parent, s);
			k = uf_find(parent, t->slot_perm[type][m * count + s]);
			if (root != k)
				parent[root] = (uint8_t)k;
			s++;
		}
		m++;
	}
	s = 0;
	while (s < count)
		size[uf_find(parent, s++)]++;
	s = 0;
	while (s < count)
	{
		root = uf_find(parent, s);
		if (size[root] == RED_ORBIT_SIZE && t->orbit_of[type][root] == RED_NO_ORBIT
			&& t->orbit_count < RED_MAX_ORBITS)
		{
			o = &t->orbits[t->orbit_count];
			o->piece_type = (uint8_t)type;
			k = 0;
			m = 0;
			while (m < count)
			{
				if (uf_find(parent, m) == root)
				{
					t->orbit_of[type][m] = (uint8_t)t->orbit_count;
					o->slots[k++] = (uint8_t)m;
				}
				m++;
			}
			t->orbit_count++;
		}
		s++;
	}
	free(parent);
	free(size);
}

/// @brief The face a centre slot sits on (its one extreme coordinate).
static uint8_t	centre_slot_face(uint32_t n, uint32_t slot)
{
	t_cubic_point	p;
	int				last;

	p = cubic_lattice_point(n, 0, slot);
	last = (int)n - 1;
	if (p.x == 0 || p.x == last)
		return ((uint8_t)cubic_face_for(CUBIC_AXIS_X, p.x == last ? 1 : -1));
	if (p.y == 0 || p.y == last)
		return ((uint8_t)cubic_face_for(CUBIC_AXIS_Y, p.y == last ? 1 : -1));
	return ((uint8_t)cubic_face_for(CUBIC_AXIS_Z, p.z == last ? 1 : -1));
}

/// @brief Orbit-local move permutations, centre colours, and an empty
///        goal table.
static void	build_orbit_perms(t_red_tables *t, t_red_orbit *o)
{
	uint32_t	count;
	uint32_t	m;
	uint32_t	i;
	uint32_t	local[256];
	uint32_t	k;

	count = t->shape->piece_types[o->piece_type].count;
	i = 0;
	while (i < RED_ORBIT_SIZE)
	{
		local[o->slots[i]] = i;
		i++;
	}
	o->perm = malloc(t->move_count * RED_ORBIT_SIZE);
	m = 0;
	while (m < t->move_count)
	{
		i = 0;
		while (i < RED_ORBIT_SIZE)
		{
			o->perm[m * RED_ORBIT_SIZE + i] = (uint8_t)local[
				t->slot_perm[o->piece_type][m * count + o->slots[i]]];
			i++;
		}
		m++;
	}
	if (o->piece_type == CUBIC_CENTRE_TYPE)
	{
		i = 0;
		while (i < RED_ORBIT_SIZE)
		{
			o->colour[i] = centre_slot_face(t->n, o->slots[i]);
			i++;
		}
	}
	o->goal = malloc(sizeof(int16_t) * RED_TRIPLES);
	k = 0;
	while (k < RED_TRIPLES)
		o->goal[k++] = -1;
	o->cycle_count = 0;
	o->cycles = NULL;
}

/// @brief Where each wing and centre slot's piece ends up after the
///        8-move commutator `seq` -- composed from slot_perm alone, no
///        state simulation, since this runs for every candidate.
static void	compose(const t_red_tables *t, const t_move *seq, uint32_t type,
	uint8_t *where)
{
	uint32_t	count;
	uint32_t	s;
	uint32_t	i;
	uint32_t	m;

	count = t->shape->piece_types[type].count;
	s = 0;
	while (s < count)
	{
		where[s] = (uint8_t)s;
		s++;
	}
	i = 0;
	while (i < RED_COMM_LENGTH)
	{
		m = (uint32_t)seq[i].generator * t->gens->steps
			+ (uint32_t)seq[i].amount - 1;
		s = 0;
		while (s < count)
		{
			where[s] = t->slot_perm[type][m * count + where[s]];
			s++;
		}
		i++;
	}
}

/// @brief Collects up to 4 moved slots of `type` into `moved`; returns
///        how many slots moved in total.
static uint32_t	moved_slots(const t_red_tables *t, uint32_t type,
	const uint8_t *where, uint8_t *moved)
{
	uint32_t	count;
	uint32_t	n_moved;
	uint32_t	s;

	count = t->shape->piece_types[type].count;
	n_moved = 0;
	s = 0;
	while (s < count)
	{
		if (where[s] != s)
		{
			if (n_moved < 4)
				moved[n_moved] = (uint8_t)s;
			n_moved++;
		}
		s++;
	}
	return (n_moved);
}

/// @brief Full-state check of a candidate that passed the permutation
///        filter: corners, midges (orientation included -- a midge
///        flipped in place is invisible to a permutation) and odd n's
///        fixed centres must be exactly as solved, since no later stage
///        repairs them; for a centre cycle, every wing must also be home.
static bool	is_pure(const t_red_tables *t, const t_move *seq, uint32_t type)
{
	uint8_t		state[RED_MAX_STATE];
	uint32_t	po;
	uint32_t	oo;
	uint32_t	i;

	shape_init_solved(t->shape, state);
	i = 0;
	while (i < RED_COMM_LENGTH)
		red_apply(t, state, seq[i++]);
	shape_piece_offsets(t->shape, CUBIC_CORNER_TYPE, &po, &oo);
	i = 0;
	while (i < t->shape->piece_types[CUBIC_CORNER_TYPE].count)
	{
		if (state[po + i] != i || state[oo + i] != 0)
			return (false);
		i++;
	}
	shape_piece_offsets(t->shape, CUBIC_WING_TYPE, &po, &oo);
	i = 0;
	while (i < t->shape->piece_types[CUBIC_WING_TYPE].count)
	{
		if (t->orbit_of[CUBIC_WING_TYPE][i] == RED_NO_ORBIT
			&& (state[po + i] != i || state[oo + i] != 0))
			return (false);
		if (type == CUBIC_CENTRE_TYPE && state[po + i] != i)
			return (false);
		i++;
	}
	shape_piece_offsets(t->shape, CUBIC_CENTRE_TYPE, &po, &oo);
	i = 0;
	while (i < t->shape->piece_types[CUBIC_CENTRE_TYPE].count)
	{
		if (t->orbit_of[CUBIC_CENTRE_TYPE][i] == RED_NO_ORBIT
			&& state[po + i] != i)
			return (false);
		i++;
	}
	return (true);
}

/// @brief Registers one direction of a cycle (all three rotations of
///        its triple) unless already known. With `cycles` NULL this is
///        the counting pass: it only claims the goal entries.
static void	register_cycle(t_red_orbit *o, uint8_t p, uint8_t q, uint8_t r,
	t_red_comm comm, uint8_t inverted)
{
	int16_t	index;

	if (o->goal[(p * RED_ORBIT_SIZE + q) * RED_ORBIT_SIZE + r] != -1)
		return ;
	index = (int16_t)o->cycle_count;
	if (o->cycles)
		o->cycles[o->cycle_count] = (t_red_cycle){p, q, r, inverted, comm};
	o->cycle_count++;
	o->goal[(p * RED_ORBIT_SIZE + q) * RED_ORBIT_SIZE + r] = index;
	o->goal[(q * RED_ORBIT_SIZE + r) * RED_ORBIT_SIZE + p] = index;
	o->goal[(r * RED_ORBIT_SIZE + p) * RED_ORBIT_SIZE + q] = index;
}

/// @brief If `seq` (commutator `comm`) is a pure 3-cycle of one piece
///        type inside one orbit, records it -- and its inverse -- in
///        that orbit.
static void	consider(t_red_tables *t, t_red_comm comm, uint8_t *where_w,
	uint8_t *where_c)
{
	t_move		seq[RED_COMM_LENGTH];
	uint8_t		moved[4];
	uint32_t	type;
	uint32_t	n_wings;
	uint32_t	n_centres;
	t_red_orbit	*o;
	uint8_t		local[3];
	uint32_t	k;
	uint8_t		*where;

	red_expand_comm(t, comm, false, seq);
	compose(t, seq, CUBIC_WING_TYPE, where_w);
	n_wings = moved_slots(t, CUBIC_WING_TYPE, where_w, moved);
	type = CUBIC_WING_TYPE;
	where = where_w;
	if (n_wings == 0)
	{
		compose(t, seq, CUBIC_CENTRE_TYPE, where_c);
		n_centres = moved_slots(t, CUBIC_CENTRE_TYPE, where_c, moved);
		if (n_centres != 3)
			return ;
		type = CUBIC_CENTRE_TYPE;
		where = where_c;
	}
	else if (n_wings != 3)
		return ;
	if (t->orbit_of[type][moved[0]] == RED_NO_ORBIT || !is_pure(t, seq, type))
		return ;
	o = &t->orbits[t->orbit_of[type][moved[0]]];
	k = 0;
	while (k < RED_ORBIT_SIZE)
	{
		if (o->slots[k] == moved[0])
			local[0] = (uint8_t)k;
		if (o->slots[k] == where[moved[0]])
			local[1] = (uint8_t)k;
		if (o->slots[k] == where[where[moved[0]]])
			local[2] = (uint8_t)k;
		k++;
	}
	register_cycle(o, local[0], local[1], local[2], comm, 0);
	register_cycle(o, local[0], local[2], local[1], comm, 1);
}

/// @brief Enumerates the commutator family [X, A B A']: X every inner-
///        layer generator, quarter turn; A every outer-face move; B
///        every move. The order is fixed, so the library -- and every
///        solve built from it -- is fully deterministic.
static void	search_commutators(t_red_tables *t)
{
	uint8_t		where_w[256];
	uint8_t		where_c[256];
	uint32_t	g;
	uint32_t	face;
	uint32_t	depth;
	uint32_t	a;
	uint32_t	b;
	t_red_comm	comm;

	g = 0;
	while (g < t->gens->count)
	{
		cubic_generator_layer(t->n, g, &face, &depth);
		if (depth > 0)
		{
			a = 0;
			while (a < t->move_count)
			{
				cubic_generator_layer(t->n, t->moves[a].generator, &face, &depth);
				b = 0;
				while (depth == 0 && b < t->move_count)
				{
					comm = (t_red_comm){(t_move){(int16_t)g, 1}, t->moves[a],
						t->moves[b]};
					consider(t, comm, where_w, where_c);
					b++;
				}
				a++;
			}
		}
		g++;
	}
}

/// @brief Runs the commutator search twice: once to count each orbit's
///        distinct cycles, once to fill exactly-sized arrays (the WASM
///        allocator never frees, so no oversized temporary buffer).
static bool	build_library(t_red_tables *t)
{
	uint32_t	i;
	uint32_t	k;

	search_commutators(t);
	i = 0;
	while (i < t->orbit_count)
	{
		if (t->orbits[i].cycle_count == 0)
			return (false);
		t->orbits[i].cycles = malloc(sizeof(t_red_cycle)
				* t->orbits[i].cycle_count);
		t->orbits[i].cycle_count = 0;
		k = 0;
		while (k < RED_TRIPLES)
			t->orbits[i].goal[k++] = -1;
		i++;
	}
	search_commutators(t);
	return (true);
}

/// @brief Odd n: the middle-layer generator of each axis (U, R, F side),
///        the only moves that carry a fixed centre anywhere; and where
///        each midge sits, for the 3x3x3 stage.
static void	find_fixed_centre_moves(t_red_tables *t)
{
	uint32_t	middle;
	uint32_t	s;
	int			e;

	middle = (t->n - 1) / 2;
	t->fixed_centre_moves[0] = cubic_generator_index(t->n, CUBIC_GEN_U, middle);
	t->fixed_centre_moves[1] = cubic_generator_index(t->n, CUBIC_GEN_R, middle);
	t->fixed_centre_moves[2] = cubic_generator_index(t->n, CUBIC_GEN_F, middle);
	s = 0;
	while (s < t->shape->piece_types[CUBIC_WING_TYPE].count)
	{
		if (t->orbit_of[CUBIC_WING_TYPE][s] == RED_NO_ORBIT)
		{
			e = cubic_edge_index_for_point(cubic_lattice_point(t->n, 1, s), t->n);
			t->midge_slot[e] = (uint8_t)s;
		}
		s++;
	}
}

static const t_red_tables	*build_tables(uint32_t n)
{
	t_red_tables	*t;
	uint32_t		i;

	t = calloc(1, sizeof(*t));
	t->n = n;
	t->shape = cubic_shape_for(n);
	t->gens = cubic_generators_for(n);
	if (shape_state_size(t->shape) > RED_MAX_STATE)
		return (NULL);
	build_moves(t);
	i = 0;
	while (i < t->shape->piece_type_count)
		build_slot_perm(t, i++);
	build_orbits(t, CUBIC_WING_TYPE);
	build_orbits(t, CUBIC_CENTRE_TYPE);
	i = 0;
	while (i < t->orbit_count)
		build_orbit_perms(t, &t->orbits[i++]);
	if (!build_library(t))
		return (NULL);
	if (n % 2 == 1)
		find_fixed_centre_moves(t);
	return (t);
}

bool	reduction_supports(uint32_t n)
{
	return (n >= REDUCTION_MIN_N && n <= REDUCTION_MAX_N);
}

const t_red_tables	*reduction_tables_for(uint32_t n)
{
	if (!reduction_supports(n) || g_tables_failed[n])
		return (NULL);
	if (!g_tables[n])
	{
		g_tables[n] = build_tables(n);
		g_tables_failed[n] = (g_tables[n] == NULL);
	}
	return (g_tables[n]);
}
