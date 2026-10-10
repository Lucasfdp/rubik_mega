#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "puzzle.h"
#include "puzzles/cubic.h"
#include "puzzles/cubic_facelets.h"

/// Ported-data sanity checks, mirroring exactly the checks rubik's own
/// FACE_TABLES comment (src/cube/moves.c) says it was verified with --
/// if the port has a sign error, one of these fails.

static uint8_t	*snapshot(puzzle_t *p)
{
	uint8_t	*buf;

	buf = malloc(puzzle_state_size(p));
	memcpy(buf, puzzle_state_ptr(p), puzzle_state_size(p));
	return (buf);
}

static void	test_every_face_four_times_is_solved(void)
{
	puzzle_t	*p;
	uint8_t		*solved;
	int			face;
	int			turn;

	p = puzzle_create(PUZZLE_CUBIC, 3);
	solved = snapshot(p);
	face = 0;
	while (face < 6)
	{
		turn = 0;
		while (turn < 4)
		{
			puzzle_apply_move(p, (int16_t)face, 1);
			turn++;
		}
		face++;
	}
	assert(memcmp(puzzle_state_ptr(p), solved, puzzle_state_size(p)) == 0);
	free(solved);
	puzzle_destroy(p);
	printf("test_cubic: every face x4 = solved -- OK\n");
}

static void	test_sexy_move_six_times_is_solved(void)
{
	puzzle_t	*p;
	uint8_t		*solved;
	int			i;

	p = puzzle_create(PUZZLE_CUBIC, 3);
	solved = snapshot(p);
	i = 0;
	while (i < 6)
	{
		puzzle_apply_move(p, CUBIC_GEN_R, 1);
		puzzle_apply_move(p, CUBIC_GEN_U, 1);
		puzzle_apply_move(p, CUBIC_GEN_R, 3);
		puzzle_apply_move(p, CUBIC_GEN_U, 3);
		i++;
	}
	assert(memcmp(puzzle_state_ptr(p), solved, puzzle_state_size(p)) == 0);
	free(solved);
	puzzle_destroy(p);
	printf("test_cubic: (R U R' U')^6 = solved -- OK\n");
}

static void	test_double_turn_is_two_quarter_turns(void)
{
	puzzle_t	*p1;
	puzzle_t	*p2;

	p1 = puzzle_create(PUZZLE_CUBIC, 3);
	p2 = puzzle_create(PUZZLE_CUBIC, 3);
	puzzle_apply_move(p1, CUBIC_GEN_F, 2);
	puzzle_apply_move(p2, CUBIC_GEN_F, 1);
	puzzle_apply_move(p2, CUBIC_GEN_F, 1);
	assert(memcmp(puzzle_state_ptr(p1), puzzle_state_ptr(p2),
			puzzle_state_size(p1)) == 0);
	puzzle_destroy(p1);
	puzzle_destroy(p2);
	printf("test_cubic: amount 2 == two quarter turns -- OK\n");
}

static void	test_scramble_then_inverse_is_solved(void)
{
	puzzle_t	*p;
	uint8_t		*solved;
	t_move		moves[50];
	uint32_t	count;
	uint32_t	i;
	int8_t		inverse_amount[4] = {0, 3, 2, 1};   // amount -> its inverse

	p = puzzle_create(PUZZLE_CUBIC, 3);
	solved = snapshot(p);
	count = 0;
	while (count < 50)
	{
		moves[count].generator = (int16_t)(count % 6);
		moves[count].amount = (int8_t)(1 + count % 3);
		puzzle_apply_move(p, moves[count].generator, moves[count].amount);
		count++;
	}
	i = count;
	while (i > 0)
	{
		i--;
		puzzle_apply_move(p, moves[i].generator,
			inverse_amount[moves[i].amount]);
	}
	assert(memcmp(puzzle_state_ptr(p), solved, puzzle_state_size(p)) == 0);
	free(solved);
	puzzle_destroy(p);
	printf("test_cubic: 50 moves then their exact inverse = solved -- OK\n");
}


/// From here down: docs/en/04-roadmap.md Phase 1 -- the same kind of
/// invariant checks above, generalized across every n this build
/// supports, instead of new sizes getting their own hand-picked move
/// sequences (there is no "known-good scramble" to port for a 5x5x5 the
/// way (R U R' U')^6 was ported from rubik -- these invariants are
/// everything correctness can be checked against until Phase 2 adds a
/// solver).

/// Sizes docs/en/04-roadmap.md Phase 1's exit gate names (5, 7), plus 4
/// and 6 for even-size coverage -- the depth/redundancy maths in
/// docs/en/05-phase1-nxn.md differs by parity, so both get exercised.
static const uint32_t	PHASE1_SIZES[4] = {4, 5, 6, 7};

/// @brief True if `state`'s permutation slice for `piece_type` is a
///        valid bijection on [0, count) -- i.e. turning never loses,
///        duplicates, or corrupts a piece, only ever relabels where it
///        sits. A cycle-table bug (an off-by-one slot, two cycles
///        sharing a slot) shows up here even when it happens not to
///        break the weaker "four turns = solved" check below.
static int	permutation_is_valid(const t_shape *shape, const uint8_t *state,
	uint32_t piece_type)
{
	uint32_t	perm_offset;
	uint32_t	orient_offset;
	uint32_t	count;
	uint8_t		*seen;
	uint32_t	i;
	int			ok;

	shape_piece_offsets(shape, piece_type, &perm_offset, &orient_offset);
	count = shape->piece_types[piece_type].count;
	seen = calloc(count, 1);
	ok = 1;
	i = 0;
	while (i < count)
	{
		if (state[perm_offset + i] >= count || seen[state[perm_offset + i]])
			ok = 0;
		else
			seen[state[perm_offset + i]] = 1;
		i++;
	}
	free(seen);
	return (ok);
}

static void	test_nxn_shape_matches_formulas(void)
{
	uint32_t		i;
	uint32_t		n;
	const t_shape	*shape;

	i = 0;
	while (i < 4)
	{
		n = PHASE1_SIZES[i];
		shape = cubic_shape_for(n);
		assert(shape != NULL);
		assert(shape_is_valid(shape));
		assert(shape->piece_types[CUBIC_CORNER_TYPE].count == 8);
		assert(shape->piece_types[CUBIC_WING_TYPE].count
			== cubic_wing_count(n));
		assert(shape->piece_types[CUBIC_CENTRE_TYPE].count
			== cubic_centre_count(n));
		i++;
	}
	printf("test_cubic: NxN shapes match the wing/centre formulas -- OK\n");
}

/// @brief Every generator, at every size, returns to solved after four
///        quarter-turns -- the NxN analogue of
///        test_every_face_four_times_is_solved() above, covering every
///        depth layer (not just the six outer faces) since a cycle-table
///        bug at an inner depth would otherwise go completely unchecked.
static void	test_nxn_every_generator_four_times_is_solved(void)
{
	uint32_t	s;
	uint32_t	n;
	puzzle_t	*p;
	uint8_t		*solved;
	uint32_t	gen;
	uint32_t	gen_count;
	int			turn;

	s = 0;
	while (s < 4)
	{
		n = PHASE1_SIZES[s];
		p = puzzle_create(PUZZLE_CUBIC, n);
		assert(p != NULL);
		solved = snapshot(p);
		gen_count = cubic_generators_for(n)->count;
		gen = 0;
		while (gen < gen_count)
		{
			turn = 0;
			while (turn < 4)
			{
				puzzle_apply_move(p, (int16_t)gen, 1);
				turn++;
			}
			gen++;
		}
		assert(memcmp(puzzle_state_ptr(p), solved, puzzle_state_size(p))
			== 0);
		free(solved);
		puzzle_destroy(p);
		s++;
	}
	printf("test_cubic: NxN every generator x4 = solved -- OK\n");
}

/// @brief A long scramble followed by its exact inverse returns to
///        solved, AND every intermediate state along the way keeps a
///        valid permutation (permutation_is_valid()) -- the NxN
///        analogue of test_scramble_then_inverse_is_solved(), plus the
///        stronger per-step bijection check that test didn't need
///        (n == 3's hand-written tables were already proven correct;
///        the newly-computed NxN tables haven't been).
static void	test_nxn_scramble_then_inverse_is_solved(void)
{
	uint32_t				s;
	uint32_t				n;
	puzzle_t				*p;
	uint8_t					*solved;
	const t_generator_table	*table;
	t_move					moves[80];
	uint32_t				count;
	uint32_t				i;
	int8_t					inverse_amount[4] = {0, 3, 2, 1};

	s = 0;
	while (s < 4)
	{
		n = PHASE1_SIZES[s];
		p = puzzle_create(PUZZLE_CUBIC, n);
		assert(p != NULL);
		solved = snapshot(p);
		table = cubic_generators_for(n);
		count = 0;
		while (count < 80)
		{
			moves[count].generator = (int16_t)(count % table->count);
			moves[count].amount = (int8_t)(1 + count % table->steps);
			puzzle_apply_move(p, moves[count].generator, moves[count].amount);
			assert(permutation_is_valid(cubic_shape_for(n),
					puzzle_state_ptr(p), CUBIC_CORNER_TYPE));
			assert(permutation_is_valid(cubic_shape_for(n),
					puzzle_state_ptr(p), CUBIC_WING_TYPE));
			assert(permutation_is_valid(cubic_shape_for(n),
					puzzle_state_ptr(p), CUBIC_CENTRE_TYPE));
			count++;
		}
		i = count;
		while (i > 0)
		{
			i--;
			puzzle_apply_move(p, moves[i].generator,
				inverse_amount[moves[i].amount]);
		}
		assert(memcmp(puzzle_state_ptr(p), solved, puzzle_state_size(p))
			== 0);
		free(solved);
		puzzle_destroy(p);
		s++;
	}
	printf("test_cubic: NxN 80 moves then inverse = solved, "
		"permutations stay valid throughout -- OK\n");
}

/// @brief Which way a piece travelled from `from` to `to` under one
///        turn about `axis` (x = 0, y = 1, z = 2): +1 if it went the way
///        that is clockwise seen from the axis's HIGH end, -1 the other
///        way, 0 if neither (not a quarter-turn about that axis at all).
static int	turn_sense(t_cubic_point from, t_cubic_point to, int axis,
	uint32_t n)
{
	int				m;
	t_cubic_point	cw;
	t_cubic_point	ccw;

	m = (int)n - 1;
	if (axis == 0)
	{
		cw = (t_cubic_point){from.x, from.z, m - from.y};
		ccw = (t_cubic_point){from.x, m - from.z, from.y};
	}
	else if (axis == 1)
	{
		cw = (t_cubic_point){m - from.z, from.y, from.x};
		ccw = (t_cubic_point){from.z, from.y, m - from.x};
	}
	else
	{
		cw = (t_cubic_point){from.y, m - from.x, from.z};
		ccw = (t_cubic_point){m - from.y, from.x, from.z};
	}
	if (cw.x == to.x && cw.y == to.y && cw.z == to.z)
		return (1);
	if (ccw.x == to.x && ccw.y == to.y && ccw.z == to.z)
		return (-1);
	return (0);
}

/// @brief One face turn moves corners, wings and centres the SAME way.
///        The invariants above (x4 = solved, bijections) can't see this:
///        before 2026-10-04 the D/L/B generators turned corners one way
///        and wings/centres the other, and every one of those tests
///        still passed (DECISIONS.md, "NxN rotation direction"). Checks
///        every displaced piece of every type after one quarter-turn of
///        each face's depth-0 generator, at every size.
static void	test_nxn_face_turn_moves_all_pieces_one_way(void)
{
	static const int	face_axis[6] = {1, 0, 2, 1, 0, 2};
	static const int	face_sign[6] = {1, 1, 1, -1, -1, -1};
	uint32_t			s;
	uint32_t			n;
	uint32_t			face;
	puzzle_t			*p;
	const t_shape		*shape;
	const uint8_t		*st;
	uint32_t			perm;
	uint32_t			orient;
	uint32_t			i;
	uint32_t			moved;

	s = 0;
	while (s < 4)
	{
		n = PHASE1_SIZES[s];
		shape = cubic_shape_for(n);
		face = 0;
		while (face < 6)
		{
			p = puzzle_create(PUZZLE_CUBIC, n);
			puzzle_apply_move(p, (int16_t)cubic_generator_index(n, face, 0), 1);
			st = puzzle_state_ptr(p);
			moved = 0;
			shape_piece_offsets(shape, CUBIC_CORNER_TYPE, &perm, &orient);
			i = 0;
			while (i < 8)
			{
				if (st[perm + i] != i && ++moved)
					assert(turn_sense(cubic_corner_point(st[perm + i], n),
							cubic_corner_point(i, n), face_axis[face], n)
						== face_sign[face]);
				i++;
			}
			shape_piece_offsets(shape, CUBIC_WING_TYPE, &perm, &orient);
			i = 0;
			while (i < cubic_wing_count(n))
			{
				if (st[perm + i] != i && ++moved)
					assert(turn_sense(cubic_lattice_point(n, 1, st[perm + i]),
							cubic_lattice_point(n, 1, i), face_axis[face], n)
						== face_sign[face]);
				i++;
			}
			shape_piece_offsets(shape, CUBIC_CENTRE_TYPE, &perm, &orient);
			i = 0;
			while (i < cubic_centre_count(n))
			{
				if (st[perm + i] != i && ++moved)
					assert(turn_sense(cubic_lattice_point(n, 0, st[perm + i]),
							cubic_lattice_point(n, 0, i), face_axis[face], n)
						== face_sign[face]);
				i++;
			}
			assert(moved > 0);
			puzzle_destroy(p);
			face++;
		}
		s++;
	}
	printf("test_cubic: NxN face turns move corners, wings and centres "
		"the same way -- OK\n");
}

/// Random moves for the wing-orientation check below -- enough that
/// every wing visits every slot it can reach many times over.
# define WING_ORIENT_SCRAMBLE_MOVES 20000

/// @brief True for a "midge": the one wing at the exact middle of an
///        edge on an odd-n cube. It behaves like a 3x3x3 edge (it can
///        be flipped in place) unlike every other wing, so the checks
///        below treat it separately.
static int	is_midge(t_cubic_point p, uint32_t n)
{
	int	m;
	int	c;

	m = (int)n - 1;
	if (p.x != 0 && p.x != m)
		c = p.x;
	else if (p.y != 0 && p.y != m)
		c = p.y;
	else
		c = p.z;
	return (n % 2 == 1 && 2 * c == m);
}

/// @brief Wing orientation behaves like a physical big cube's: a
///        non-midge wing can never be flipped in place, so its tracked
///        orientation must be a pure function of (which piece, which
///        slot) -- seeing the same piece in the same slot with two
///        different orientations means the deltas are wrong. Midges
///        CAN flip, but only in pairs, like 3x3x3 edges: their flip sum
///        stays even. Before 2026-10-04 the first check failed millions
///        of times per size (DECISIONS.md, "Wing orientation").
static void	test_nxn_wing_orientation_follows_position(void)
{
	uint32_t				s;
	uint32_t				n;
	uint32_t				w;
	puzzle_t				*p;
	const t_generator_table	*table;
	const uint8_t			*st;
	int8_t					*seen;
	uint8_t					*midge;
	uint32_t				perm;
	uint32_t				orient;
	uint32_t				k;
	uint32_t				i;
	uint32_t				rng;
	uint32_t				flip_sum;

	s = 0;
	while (s < 4)
	{
		n = PHASE1_SIZES[s];
		w = cubic_wing_count(n);
		table = cubic_generators_for(n);
		seen = malloc(w * w);
		memset(seen, -1, w * w);
		midge = malloc(w);
		i = 0;
		while (i < w)
		{
			midge[i] = (uint8_t)is_midge(cubic_lattice_point(n, 1, i), n);
			i++;
		}
		shape_piece_offsets(cubic_shape_for(n), CUBIC_WING_TYPE, &perm, &orient);
		p = puzzle_create(PUZZLE_CUBIC, n);
		st = puzzle_state_ptr(p);
		rng = 12345u;
		k = 0;
		while (k < WING_ORIENT_SCRAMBLE_MOVES)
		{
			rng = rng * 1103515245u + 12345u;
			puzzle_apply_move(p, (int16_t)((rng >> 8) % table->count), 1);
			flip_sum = 0;
			i = 0;
			while (i < w)
			{
				if (midge[i])
					flip_sum += st[orient + i];
				else if (seen[st[perm + i] * w + i] < 0)
					seen[st[perm + i] * w + i] = (int8_t)st[orient + i];
				else
					assert(seen[st[perm + i] * w + i] == st[orient + i]);
				i++;
			}
			assert(flip_sum % 2 == 0);
			k++;
		}
		puzzle_destroy(p);
		free(seen);
		free(midge);
		s++;
	}
	printf("test_cubic: NxN wing orientation is fixed by position "
		"(midges flip in pairs) -- OK\n");
}

int	main(void)
{
	test_every_face_four_times_is_solved();
	test_sexy_move_six_times_is_solved();
	test_double_turn_is_two_quarter_turns();
	test_scramble_then_inverse_is_solved();
	test_nxn_shape_matches_formulas();
	test_nxn_every_generator_four_times_is_solved();
	test_nxn_scramble_then_inverse_is_solved();
	test_nxn_face_turn_moves_all_pieces_one_way();
	test_nxn_wing_orientation_follows_position();
	printf("test_cubic: all assertions passed\n");
	return (0);
}
