#include "platform.h"
#include "solve/dodeca_macros.h"
#include "solve/bytes.h"

t_move	dodeca_move(uint32_t m)
{
	t_move	move;

	move.generator = (int16_t)(m / DODECA_STEPS);
	move.amount = (int8_t)(m % DODECA_STEPS + 1);
	return (move);
}

uint32_t	dodeca_move_inverse(uint32_t m)
{
	uint32_t	face;
	uint32_t	amount;

	face = m / DODECA_STEPS;
	amount = m % DODECA_STEPS + 1;
	return (face * DODECA_STEPS + (DODECA_STEPS + 1 - amount) - 1);
}

/// Applies a move index to a raw state buffer.
static void	apply_move_index(const t_dodeca_table *t, uint8_t *state,
				uint32_t m)
{
	shape_apply_move(t->shape, t->gens, state, dodeca_move(m));
}

/// Reads the move model of piece type `t->type` out of the real
/// generators: apply each move to a solved state, and the piece that
/// started in slot s is the one now sitting in the slot whose
/// permutation byte is s.
static void	read_move_model(t_dodeca_table *t)
{
	uint8_t		state[DODECA_STATE_MAX];
	uint32_t	perm_off;
	uint32_t	orient_off;
	uint32_t	m;
	uint32_t	y;

	shape_piece_offsets(t->shape, t->type, &perm_off, &orient_off);
	m = 0;
	while (m < DODECA_MOVE_COUNT)
	{
		shape_init_solved(t->shape, state);
		apply_move_index(t, state, m);
		y = 0;
		while (y < t->slots)
		{
			t->dest[m][state[perm_off + y]] = (uint8_t)y;
			t->twist[m][state[perm_off + y]] = state[orient_off + y];
			y++;
		}
		m++;
	}
}

/// Rotates a canonical element's spelling until slot[0] is smallest;
/// the deltas rotate with the slots they belong to.
static void	canonicalise(t_dodeca_node *n)
{
	uint8_t	s[3];
	uint8_t	d[3];
	int		first;
	int		i;

	first = 0;
	i = 1;
	while (i < 3)
	{
		if (n->slot[i] < n->slot[first])
			first = i;
		i++;
	}
	i = 0;
	while (i < 3)
	{
		s[i] = n->slot[(first + i) % 3];
		d[i] = n->delta[(first + i) % 3];
		i++;
	}
	i = 0;
	while (i < 3)
	{
		n->slot[i] = s[i];
		n->delta[i] = d[i];
		i++;
	}
}

/// The lookup key of a canonical element: its three slots, then its
/// three landing twists, as one mixed-radix number.
static uint32_t	key_of(const t_dodeca_table *t, const uint8_t *slot,
					const uint8_t *delta)
{
	uint32_t	key;
	int			i;

	key = 0;
	i = 0;
	while (i < 3)
		key = key * t->slots + slot[i++];
	i = 0;
	while (i < 3)
		key = key * t->orients + delta[i++];
	return (key);
}

/// How many elements a type should have: ordered cycles times twist
/// patterns (see DODECA_CORNER_ELEMENTS).
static uint32_t	expected_elements(const t_dodeca_table *t)
{
	return (t->slots * (t->slots - 1) * (t->slots - 2) / 3
		* t->orients * t->orients);
}

/// An element is legal when its slots differ, its twists are in range,
/// and they sum to zero modulo the orientation count (a twist cannot
/// appear from nowhere: the group's twist total is invariant).
static int	element_is_legal(const t_dodeca_table *t, const uint8_t *slot,
				const uint8_t *delta)
{
	uint32_t	sum;
	int			i;

	if (slot[0] == slot[1] || slot[1] == slot[2] || slot[0] == slot[2])
		return (0);
	sum = 0;
	i = 0;
	while (i < 3)
	{
		if (slot[i] >= t->slots || delta[i] >= t->orients)
			return (0);
		sum += delta[i++];
	}
	return (sum % t->orients == 0);
}

const t_dodeca_node	*dodeca_table_find(const t_dodeca_table *t,
						const uint8_t *slot, const uint8_t *delta)
{
	t_dodeca_node	probe;
	uint16_t		found;
	int				i;

	if (!element_is_legal(t, slot, delta))
		return (NULL);
	i = 0;
	while (i < 3)
	{
		probe.slot[i] = slot[i];
		probe.delta[i] = delta[i];
		i++;
	}
	canonicalise(&probe);
	found = t->index[key_of(t, probe.slot, probe.delta)];
	if (found == 0)
		return (NULL);
	return (&t->nodes[found - 1]);
}

/// Follows the three moved slots round: slot[i + 1] is the slot whose
/// permutation byte names slot[i], i.e. where the piece from slot[i]
/// landed. Succeeds only if the three form one cycle (no piece stays
/// put with a twist, no two swap) -- then `out` holds the element in
/// canonical form with landing twists read off the orientation bytes.
static int	trace_cycle(const uint8_t *perm, const uint8_t *orient,
				const uint8_t *moved, t_dodeca_node *out)
{
	int	i;
	int	j;
	int	landed;

	out->slot[0] = moved[0];
	i = 0;
	while (i < 3)
	{
		landed = -1;
		j = 0;
		while (j < 3)
		{
			if (perm[moved[j]] == out->slot[i] && moved[j] != out->slot[i])
				landed = moved[j];
			j++;
		}
		if (landed < 0)
			return (0);
		out->slot[(i + 1) % 3] = (uint8_t)landed;
		i++;
	}
	if (out->slot[0] != moved[0] || out->slot[1] == out->slot[2])
		return (0);
	i = 0;
	while (i < 3)
	{
		out->delta[i] = orient[out->slot[i]];
		i++;
	}
	canonicalise(out);
	return (1);
}

int	dodeca_extract_element(const t_dodeca_table *t, const uint8_t *state,
		t_dodeca_node *out)
{
	uint32_t	perm_off;
	uint32_t	orient_off;
	uint32_t	u;
	uint32_t	y;
	uint32_t	n;
	uint8_t		moved[3];

	u = 0;
	while (u < t->type)
	{
		shape_piece_offsets(t->shape, u, &perm_off, &orient_off);
		y = 0;
		while (y < t->shape->piece_types[u].count)
		{
			if (state[perm_off + y] != y || state[orient_off + y] != 0)
				return (0);
			y++;
		}
		u++;
	}
	shape_piece_offsets(t->shape, t->type, &perm_off, &orient_off);
	n = 0;
	y = 0;
	while (y < t->slots)
	{
		if (state[perm_off + y] != y || state[orient_off + y] != 0)
		{
			if (n == 3)
				return (0);
			moved[n++] = (uint8_t)y;
		}
		y++;
	}
	if (n != 3)
		return (0);
	return (trace_cycle(state + perm_off, state + orient_off, moved, out));
}

/// Applies `len` moves of `word` to a solved state, then reads it as an
/// element. The single place a candidate root is judged.
static int	word_is_element(const t_dodeca_table *t, const t_move *word,
				uint32_t len, t_dodeca_node *out)
{
	uint8_t		state[DODECA_STATE_MAX];
	uint32_t	i;

	shape_init_solved(t->shape, state);
	i = 0;
	while (i < len)
		shape_apply_move(t->shape, t->gens, state, word[i++]);
	return (dodeca_extract_element(t, state, out));
}

/// The root search keeps the shortest candidate that is an element.
typedef struct s_root_search
{
	t_move			best[DODECA_ROOT_MAX];
	uint32_t		best_len;
	t_dodeca_node	best_node;
}	t_root_search;

static void	consider(const t_dodeca_table *t, t_root_search *rs,
				const t_move *word, uint32_t len)
{
	t_dodeca_node	node;
	uint32_t		i;

	if (len >= rs->best_len || !word_is_element(t, word, len, &node))
		return ;
	i = 0;
	while (i < len)
	{
		rs->best[i] = word[i];
		i++;
	}
	rs->best_len = len;
	rs->best_node = node;
}

/// Face 0 stands for every face: the dodecahedron's rotations carry any
/// face onto any other, and they map this search family onto itself, so
/// looking at one face's amounts is looking at all of them.
# define SEARCH_FACE 0

/// Family 1: conjugated commutators [A B A', C] -- A a turn of the
/// search face, B and C any move. The pure corner 3-cycle the spec's
/// spike found is of this shape (8 moves).
static void	search_conjugated(const t_dodeca_table *t, t_root_search *rs)
{
	t_move		w[8];
	uint32_t	a;
	uint32_t	b;
	uint32_t	c;

	a = 0;
	while (a < DODECA_STEPS)
	{
		b = 0;
		while (b < DODECA_MOVE_COUNT)
		{
			c = 0;
			while (c < DODECA_MOVE_COUNT)
			{
				w[0] = dodeca_move(SEARCH_FACE * DODECA_STEPS + a);
				w[1] = dodeca_move(b);
				w[2] = dodeca_move(dodeca_move_inverse(SEARCH_FACE
							* DODECA_STEPS + a));
				w[3] = dodeca_move(c);
				w[4] = w[0];
				w[5] = dodeca_move(dodeca_move_inverse(b));
				w[6] = w[2];
				w[7] = dodeca_move(dodeca_move_inverse(c));
				consider(t, rs, w, 8);
				c++;
			}
			b++;
		}
		a++;
	}
}

/// Family 2: powers of one commutator, [A, B]^p for p = 1..6. The pure
/// edge 3-cycle (20 moves) is [f0, f1^2]^5 of this shape.
# define MAX_COMMUTATOR_POWER 6

static void	search_powers(const t_dodeca_table *t, t_root_search *rs)
{
	t_move		w[4 * MAX_COMMUTATOR_POWER];
	uint32_t	a;
	uint32_t	b;
	uint32_t	p;
	uint32_t	i;

	a = 0;
	while (a < DODECA_STEPS)
	{
		b = 0;
		while (b < DODECA_MOVE_COUNT)
		{
			p = 1;
			while (p <= MAX_COMMUTATOR_POWER)
			{
				i = 0;
				while (i < p)
				{
					w[4 * i] = dodeca_move(SEARCH_FACE * DODECA_STEPS + a);
					w[4 * i + 1] = dodeca_move(b);
					w[4 * i + 2] = dodeca_move(dodeca_move_inverse(
								SEARCH_FACE * DODECA_STEPS + a));
					w[4 * i + 3] = dodeca_move(dodeca_move_inverse(b));
					i++;
				}
				consider(t, rs, w, 4 * p);
				p++;
			}
			b++;
		}
		a++;
	}
}

/// Conjugates element `w` by move `m`: the element of the word
/// "m, then w's word, then m undone". Derived in docs/en/11-phase4-
/// dodecahedral.md's build record; in short: the piece that ends up in
/// w's slot i came from the slot x_i that m sends there, and picks up
/// m's own twist on the way in and gives it back on the way out, so
/// each twist changes by the difference of m's twists at x_i and
/// x_(i+1). tests/test_dodeca_solve.c replays words to check it.
static void	conjugate(const t_dodeca_table *t, const t_dodeca_node *w,
				uint32_t m, t_dodeca_node *out)
{
	uint32_t	inverse;
	uint32_t	x[3];
	uint32_t	moving[3];
	uint32_t	i;
	uint32_t	next;

	inverse = dodeca_move_inverse(m);
	i = 0;
	while (i < 3)
	{
		x[i] = t->dest[inverse][w->slot[i]];
		moving[i] = w->delta[(i + 1) % 3];
		i++;
	}
	i = 0;
	while (i < 3)
	{
		next = (i + 1) % 3;
		out->slot[i] = (uint8_t)x[i];
		out->delta[next] = (uint8_t)((moving[i] + t->twist[m][x[i]]
					+ t->orients - t->twist[m][x[next]]) % t->orients);
		i++;
	}
	canonicalise(out);
}

/// Breadth-first search over elements: every node is conjugated by every
/// move, and a conjugate not seen before becomes the next node. The
/// queue is the node array itself.
static int	build_nodes(t_dodeca_table *t, const t_dodeca_node *root)
{
	uint32_t		head;
	uint32_t		m;
	t_dodeca_node	next;
	uint32_t		key;

	t->nodes[0] = *root;
	t->nodes[0].parent = DODECA_NO_PARENT;
	t->nodes[0].move = 0;
	t->nodes[0].depth = 0;
	t->index[key_of(t, root->slot, root->delta)] = 1;
	t->node_count = 1;
	head = 0;
	while (head < t->node_count)
	{
		m = 0;
		while (m < DODECA_MOVE_COUNT)
		{
			conjugate(t, &t->nodes[head], m, &next);
			key = key_of(t, next.slot, next.delta);
			if (t->index[key] == 0)
			{
				if (t->node_count >= DODECA_EDGE_ELEMENTS)
					return (0);
				next.parent = (uint16_t)head;
				next.move = (uint8_t)m;
				next.depth = (uint8_t)(t->nodes[head].depth + 1);
				t->nodes[t->node_count++] = next;
				t->index[key] = (uint16_t)t->node_count;
				if (next.depth > t->max_depth)
					t->max_depth = next.depth;
			}
			m++;
		}
		head++;
	}
	return (1);
}

int	dodeca_table_init(t_dodeca_table *t, const t_cut_family *family,
		uint32_t type)
{
	t_root_search	rs;
	uint32_t		i;

	t->shape = family->shape;
	t->gens = family->generators;
	t->type = type;
	t->slots = family->shape->piece_types[type].count;
	t->orients = family->shape->piece_types[type].orientations;
	t->node_count = 0;
	t->max_depth = 0;
	solve_zero_bytes(t->index, sizeof(t->index));
	read_move_model(t);
	rs.best_len = DODECA_ROOT_MAX + 1;
	search_conjugated(t, &rs);
	search_powers(t, &rs);
	if (rs.best_len > DODECA_ROOT_MAX)
		return (0);
	t->root_len = rs.best_len;
	i = 0;
	while (i < rs.best_len)
	{
		t->root[i] = rs.best[i];
		i++;
	}
	if (!build_nodes(t, &rs.best_node))
		return (0);
	return (t->node_count == expected_elements(t)
		&& t->root_len + 2 * t->max_depth <= DODECA_WORD_MAX);
}

uint32_t	dodeca_table_word(const t_dodeca_table *t, const t_dodeca_node *node,
				t_move *out, uint32_t max)
{
	uint32_t			prefix[DODECA_WORD_MAX];
	const t_dodeca_node	*cur;
	uint32_t			d;
	uint32_t			i;

	d = node->depth;
	if (t->root_len + 2 * d > max || d > DODECA_WORD_MAX)
		return (UINT32_MAX);
	cur = node;
	i = 0;
	while (i < d)
	{
		prefix[i++] = cur->move;
		cur = &t->nodes[cur->parent];
	}
	i = 0;
	while (i < d)
	{
		out[i] = dodeca_move(prefix[i]);
		i++;
	}
	i = 0;
	while (i < t->root_len)
	{
		out[d + i] = t->root[i];
		i++;
	}
	i = 0;
	while (i < d)
	{
		out[d + t->root_len + i] = dodeca_move(dodeca_move_inverse(
					prefix[d - 1 - i]));
		i++;
	}
	return (t->root_len + 2 * d);
}
