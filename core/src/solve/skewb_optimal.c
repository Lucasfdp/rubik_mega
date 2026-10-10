#include "platform.h"
#include "solve/skewb_optimal.h"
#include "solve/bytes.h"

/// Corner permutation bytes' offset inside a Skewb state.
static uint32_t	corner_perm_offset(void)
{
	uint32_t	perm_offset;
	uint32_t	orient_offset;

	shape_piece_offsets(skewb_shape(), SKEWB_CORNER_TYPE, &perm_offset,
		&orient_offset);
	return (perm_offset);
}

static void	apply_word(uint8_t *state, const t_move *word, uint32_t length)
{
	uint32_t	i;

	i = 0;
	while (i < length)
		shape_apply_move(skewb_shape(), skewb_generators(), state, word[i++]);
}

int	skewb_state_is_uniform(const uint8_t *state)
{
	const t_cut_family	*family;
	uint8_t				colours[SKEWB_STICKER_COUNT];
	uint32_t			i;
	uint32_t			j;

	family = skewb_family();
	if (!family)
		return (0);
	cut_paint(family, state, colours);
	i = 0;
	while (i < SKEWB_STICKER_COUNT)
	{
		j = 0;
		while (j < i && (family->stickers[j].normal.x
				!= family->stickers[i].normal.x
				|| family->stickers[j].normal.y != family->stickers[i].normal.y
				|| family->stickers[j].normal.z != family->stickers[i].normal.z))
			j++;
		if (j < i && colours[j] != colours[i])
			return (0);
		i++;
	}
	return (1);
}

/// Records `word` as a rotation if it turns the solved puzzle into a
/// uniform-faced one not seen before. Two words are the same rotation
/// exactly when they send the corners the same way: the rotation group
/// acts faithfully on the 8 corner slots.
static void	try_word(t_skewb_solver *s, const t_move *word, uint32_t length)
{
	uint8_t				state[SKEWB_STATE_BYTES];
	const uint8_t		*perm;
	t_skewb_rotation	*r;
	uint32_t			i;
	uint32_t			j;

	if (s->rotation_count == SKEWB_ROTATION_COUNT)
		return ;
	shape_init_solved(skewb_shape(), state);
	apply_word(state, word, length);
	if (!skewb_state_is_uniform(state))
		return ;
	perm = state + corner_perm_offset();
	i = 0;
	while (i < s->rotation_count)
	{
		j = 0;
		while (j < SKEWB_CORNER_COUNT
			&& s->rotations[i].corner_perm[j] == perm[j])
			j++;
		if (j == SKEWB_CORNER_COUNT)
			return ;
		i++;
	}
	r = &s->rotations[s->rotation_count++];
	r->length = length;
	i = 0;
	while (i < length)
	{
		r->word[i] = word[i];
		i++;
	}
	j = 0;
	while (j < SKEWB_CORNER_COUNT)
	{
		r->corner_perm[j] = perm[j];
		j++;
	}
}

/// Tries every word of exactly `length` turns (SKEWB_MOVE_CHOICES
/// letters, so 16^length words).
static void	try_all_words(t_skewb_solver *s, uint32_t length)
{
	t_move		word[SKEWB_ROTATION_MAX_WORD];
	uint32_t	count;
	uint32_t	code;
	uint32_t	rest;
	uint32_t	i;

	count = 1;
	i = 0;
	while (i++ < length)
		count *= SKEWB_MOVE_CHOICES;
	code = 0;
	while (code < count)
	{
		rest = code;
		i = 0;
		while (i < length)
		{
			word[i].generator = (int16_t)((rest % SKEWB_MOVE_CHOICES)
					/ SKEWB_STEPS);
			word[i].amount = (int8_t)((rest % SKEWB_MOVE_CHOICES)
					% SKEWB_STEPS + 1);
			rest /= SKEWB_MOVE_CHOICES;
			i++;
		}
		try_word(s, word, length);
		code++;
	}
}

/// Appends rotation b's word to rotation a's and tries it.
static void	try_product(t_skewb_solver *s, uint32_t a, uint32_t b)
{
	t_move		word[SKEWB_ROTATION_MAX_WORD];
	uint32_t	length;
	uint32_t	i;

	length = s->rotations[a].length + s->rotations[b].length;
	if (length > SKEWB_ROTATION_MAX_WORD)
		return ;
	i = 0;
	while (i < s->rotations[a].length)
	{
		word[i] = s->rotations[a].word[i];
		i++;
	}
	i = 0;
	while (i < s->rotations[b].length)
	{
		word[s->rotations[a].length + i] = s->rotations[b].word[i];
		i++;
	}
	try_word(s, word, length);
}

/// Finds all 12 rotations. Every word of up to two turns is tried (that
/// reaches the identity and the eight 120-degree rotations); the three
/// 180-degree ones are products of two of those, so closing the set
/// under concatenation finds them in a handful of tries -- far cheaper
/// than trying all 16^4 words of four turns.
static int	find_rotations(t_skewb_solver *s)
{
	uint32_t	before;
	uint32_t	a;
	uint32_t	b;

	s->rotation_count = 0;
	try_all_words(s, 0);
	try_all_words(s, 1);
	try_all_words(s, 2);
	before = 0;
	while (before != s->rotation_count)
	{
		before = s->rotation_count;
		a = 0;
		while (a < before)
		{
			b = 0;
			while (b < before)
				try_product(s, a, b++);
			a++;
		}
	}
	return (s->rotation_count == SKEWB_ROTATION_COUNT);
}

/// The search moves: each canonical corner's turn, both directions. The
/// far-half turns are not here -- the frame correction makes the
/// canonical group enough.
static uint32_t	canonical_moves(t_move *moves)
{
	uint32_t	count;
	uint32_t	corner;
	uint32_t	amount;

	count = 0;
	corner = 0;
	while (corner < SKEWB_CORNER_COUNT)
	{
		amount = 1;
		while (skewb_is_canonical_corner(corner) && amount <= SKEWB_STEPS)
		{
			moves[count].generator = (int16_t)corner;
			moves[count++].amount = (int8_t)amount++;
		}
		corner++;
	}
	return (count);
}

int	skewb_solver_init(t_skewb_solver *solver)
{
	uint32_t	blocks[2];
	t_move		moves[COORD_MAX_MOVES];
	uint32_t	count;

	if (!skewb_generators())
		return (0);
	blocks[0] = SKEWB_CORNER_TYPE;
	blocks[1] = SKEWB_CENTRE_TYPE;
	count = canonical_moves(moves);
	return (coord_table_init(&solver->table, skewb_shape(),
			skewb_generators(), blocks, 2, moves, count)
		&& find_rotations(solver));
}

int	skewb_find_frame(const t_skewb_solver *s, const uint8_t *state,
	uint8_t *framed)
{
	uint32_t	r;
	uint32_t	slot;
	uint32_t	offset;

	offset = corner_perm_offset();
	r = 0;
	while (r < s->rotation_count)
	{
		solve_copy_bytes(framed, state, SKEWB_STATE_BYTES);
		apply_word(framed, s->rotations[r].word, s->rotations[r].length);
		slot = 0;
		while (slot < SKEWB_CORNER_COUNT && (!skewb_is_canonical_corner(slot)
				|| framed[offset + slot] == slot))
			slot++;
		if (slot == SKEWB_CORNER_COUNT)
			return ((int)r);
		r++;
	}
	return (-1);
}

t_move	skewb_unframe_move(const t_skewb_solver *solver, int frame,
	t_move canonical)
{
	t_move	move;

	move.generator = (int16_t)solver->rotations[frame].corner_perm[
		canonical.generator];
	move.amount = canonical.amount;
	return (move);
}

/// The algebra, with p(a) the slot a piece at slot a is moved to by a
/// turn or word (so a state is a map slot -> piece, and applying a turn
/// g to s gives s o p(g)^-1):
///
/// Let w be the rotation word find_frame() picked, T = p(w), so the
/// framed state is s' = s o T^-1, in the canonical group. The table
/// solves s' with canonical turns c'_1..c'_k:  s' o P = identity, where
/// P is the product of their p^-1. Replay those on s itself with each
/// turn conjugated, p(c_i) = T^-1 p(c'_i) T -- a turn about the corner
/// T^-1(c'_i), because conjugating a rotation by a rotation moves its
/// axis -- and the result is s' o P o T = T: a rotation of the solved
/// puzzle, i.e. uniform faces. T^-1 as an array is exactly the corner
/// permutation of w applied to the solved puzzle, so the renaming is
///   c_i = rotation.corner_perm[c'_i],
/// the amount unchanged (a rotation preserves handedness). The tests
/// prove it: 200 scrambles over all eight turns end with uniform faces.
int	skewb_solve(const t_skewb_solver *solver, const uint8_t *state,
	t_move *out, uint32_t max_moves)
{
	uint8_t		framed[SKEWB_STATE_BYTES];
	uint32_t	indices[SKEWB_MAX_MOVES];
	int			frame;
	int			count;
	int			i;

	frame = skewb_find_frame(solver, state, framed);
	if (frame < 0)
		return (-1);
	if (max_moves > SKEWB_MAX_MOVES)
		max_moves = SKEWB_MAX_MOVES;
	count = coord_table_solve(&solver->table, framed, indices, max_moves);
	i = 0;
	while (i < count)
	{
		out[i] = skewb_unframe_move(solver, frame,
				solver->table.moves[indices[i]]);
		i++;
	}
	return (count);
}
