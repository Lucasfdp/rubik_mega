#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "puzzle.h"
#include "puzzles/cubic.h"
#include "puzzles/cubic_facelets.h"

/// Tests for the facelet layer (docs/en/07-nxn-web-view.md D1). The
/// central check is test_facelets_match_geometric_model(): an
/// independent, purely geometric cube -- stickers as points in space,
/// rotated by plain coordinate maths, with no knowledge of slots,
/// cycles or orientation bytes -- must show exactly the colours
/// puzzle_facelets() reports after every move. That catches any
/// disagreement between the move tables, the orientation conventions
/// and the painting rule at once, which is the bug class
/// DECISIONS.md 2026-10-04 found hiding behind the invariant tests.

/// Every size this build supports (cubic.c's shape_is_valid() limit).
# define MIN_N 3
# define MAX_N 8

/// Random moves per size in the geometric-model walk.
# define MODEL_WALK_MOVES 400

/// One facelet as a point one step outside the cube, across its sticker
/// -- (cell + outward normal). Rotating that point with the layer moves
/// the sticker exactly as the physical cube would, so the model needs
/// no separate normal-vector maths.
static t_cubic_point	outward(const uint8_t *geo, uint32_t i, uint32_t n)
{
	t_cubic_point	p;
	int				m;

	m = (int)n - 1;
	p = (t_cubic_point){geo[i * 4], geo[i * 4 + 1], geo[i * 4 + 2]};
	if (geo[i * 4 + 3] == CUBIC_GEN_R)
		p.x = m + 1;
	else if (geo[i * 4 + 3] == CUBIC_GEN_L)
		p.x = -1;
	else if (geo[i * 4 + 3] == CUBIC_GEN_U)
		p.y = m + 1;
	else if (geo[i * 4 + 3] == CUBIC_GEN_D)
		p.y = -1;
	else if (geo[i * 4 + 3] == CUBIC_GEN_F)
		p.z = m + 1;
	else
		p.z = -1;
	return (p);
}

/// @brief One quarter-turn of point `p`, clockwise as seen from the
///        face at `sign`'s end of `axis` -- the physical meaning of
///        puzzle_apply_move(generator, 1). Written independently of
///        cubic_lattice.c on purpose.
static t_cubic_point	model_rotate(t_cubic_point p, t_cubic_axis axis,
	int sign, uint32_t n)
{
	int	m;

	m = (int)n - 1;
	if (axis == CUBIC_AXIS_X)
		return (sign > 0 ? (t_cubic_point){p.x, p.z, m - p.y}
			: (t_cubic_point){p.x, m - p.z, p.y});
	if (axis == CUBIC_AXIS_Y)
		return (sign > 0 ? (t_cubic_point){m - p.z, p.y, p.x}
			: (t_cubic_point){p.z, p.y, m - p.x});
	return (sign > 0 ? (t_cubic_point){p.y, m - p.x, p.z}
		: (t_cubic_point){m - p.y, p.x, p.z});
}

static int	axis_coord(t_cubic_point p, t_cubic_axis axis)
{
	if (axis == CUBIC_AXIS_X)
		return (p.x);
	if (axis == CUBIC_AXIS_Y)
		return (p.y);
	return (p.z);
}

static uint32_t	find_outward(const t_cubic_point *pts, uint32_t count,
	t_cubic_point p)
{
	uint32_t	i;

	i = 0;
	while (i < count && !(pts[i].x == p.x && pts[i].y == p.y && pts[i].z == p.z))
		i++;
	assert(i < count);
	return (i);
}

/// @brief Applies one quarter-turn of generator `g` to the model's
///        colours `ref` in place.
static void	model_apply(puzzle_t *p, uint32_t n, const uint8_t *geo,
	const t_cubic_point *pts, uint8_t *ref, uint32_t g)
{
	const t_cubic_face	*face;
	uint32_t			count;
	uint32_t			depth;
	uint8_t				*next;
	uint32_t			i;
	int					layer;
	t_cubic_point		cell;

	face = cubic_face((uint32_t)puzzle_generator_face(p, g));
	depth = (uint32_t)puzzle_generator_depth(p, g);
	layer = face->sign > 0 ? (int)n - 1 - (int)depth : (int)depth;
	count = puzzle_facelet_count(p);
	next = malloc(count);
	memcpy(next, ref, count);
	i = 0;
	while (i < count)
	{
		cell = (t_cubic_point){geo[i * 4], geo[i * 4 + 1], geo[i * 4 + 2]};
		if (axis_coord(cell, face->axis) == layer)
			next[find_outward(pts, count, model_rotate(pts[i], face->axis,
						face->sign, n))] = ref[i];
		i++;
	}
	memcpy(ref, next, count);
	free(next);
}

static void	test_solved_shows_each_face_colour(void)
{
	uint32_t		n;
	puzzle_t		*p;
	const uint8_t	*geo;
	const uint8_t	*col;
	uint32_t		i;

	n = MIN_N;
	while (n <= MAX_N)
	{
		p = puzzle_create(PUZZLE_CUBIC, n);
		assert(puzzle_facelet_count(p) == CUBIC_FACE_COUNT * n * n);
		geo = puzzle_facelet_geometry(p);
		col = puzzle_facelets(p);
		i = 0;
		while (i < puzzle_facelet_count(p))
		{
			assert(col[i] == geo[i * 4 + 3]);
			i++;
		}
		puzzle_destroy(p);
		n++;
	}
	printf("test_facelets: solved cube shows each face's own colour, n = 3..8 -- OK\n");
}

static void	test_generator_layer_round_trips(void)
{
	uint32_t	n;
	puzzle_t	*p;
	uint32_t	g;

	n = MIN_N;
	while (n <= MAX_N)
	{
		p = puzzle_create(PUZZLE_CUBIC, n);
		g = 0;
		while (g < puzzle_generator_count(p))
		{
			assert(cubic_generator_index(n, (uint32_t)puzzle_generator_face(p, g),
					(uint32_t)puzzle_generator_depth(p, g)) == g);
			g++;
		}
		assert(puzzle_generator_face(p, g) == -1);
		assert(puzzle_generator_depth(p, g) == -1);
		puzzle_destroy(p);
		n++;
	}
	printf("test_facelets: generator <-> (face, depth) round-trips -- OK\n");
}

/// @brief Every generator alone, then a long random walk: after each
///        quarter-turn, puzzle_facelets() must equal the independent
///        geometric model, sticker for sticker, at every size.
static void	test_facelets_match_geometric_model(void)
{
	uint32_t		n;
	puzzle_t		*p;
	const uint8_t	*geo;
	t_cubic_point	*pts;
	uint8_t			*ref;
	uint32_t		count;
	uint32_t		i;
	uint32_t		k;
	uint32_t		g;
	uint32_t		rng;

	n = MIN_N;
	while (n <= MAX_N)
	{
		p = puzzle_create(PUZZLE_CUBIC, n);
		count = puzzle_facelet_count(p);
		geo = puzzle_facelet_geometry(p);
		pts = malloc(sizeof(*pts) * count);
		ref = malloc(count);
		i = 0;
		while (i < count)
		{
			pts[i] = outward(geo, i, n);
			ref[i] = geo[i * 4 + 3];
			i++;
		}
		rng = 2463534242u;
		k = 0;
		while (k < puzzle_generator_count(p) + MODEL_WALK_MOVES)
		{
			g = k;
			if (k >= puzzle_generator_count(p))
			{
				rng ^= rng << 13;
				rng ^= rng >> 17;
				rng ^= rng << 5;
				g = rng % puzzle_generator_count(p);
			}
			puzzle_apply_move(p, (int16_t)g, 1);
			model_apply(p, n, geo, pts, ref, g);
			assert(memcmp(puzzle_facelets(p), ref, count) == 0);
			k++;
		}
		free(pts);
		free(ref);
		puzzle_destroy(p);
		n++;
	}
	printf("test_facelets: facelets match an independent geometric model "
		"after every move, n = 3..8 -- OK\n");
}

int	main(void)
{
	test_solved_shows_each_face_colour();
	test_generator_layer_round_trips();
	test_facelets_match_geometric_model();
	printf("test_facelets: all assertions passed\n");
	return (0);
}
