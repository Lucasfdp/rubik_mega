#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "puzzle.h"
#include "puzzle_internal.h"
#include "puzzles/skewb.h"
#include "puzzles/tetrahedral.h"

/// Facelet layer and turn-geometry exports for Skewb and Pyraminx
/// (docs/en/09-phase3-skewb-pyraminx.md increment 2).
///
/// The main check is an independent model: colours are tracked by
/// rotating sticker positions geometrically (ivec3_turn on each moved
/// sticker), never touching cycles, deltas or piece orientation. If the
/// derived orientation deltas or cut_paint()'s convention were wrong,
/// the two would drift apart within a few moves.

#define MAX_STICKERS 36
#define RANDOM_MOVES 300

static int	find(const t_cut_family *f, t_ivec3 pos, t_ivec3 normal)
{
	uint32_t	i;

	i = 0;
	while (i < f->sticker_count)
	{
		if (f->stickers[i].pos.x == pos.x && f->stickers[i].pos.y == pos.y
			&& f->stickers[i].pos.z == pos.z
			&& f->stickers[i].normal.x == normal.x
			&& f->stickers[i].normal.y == normal.y
			&& f->stickers[i].normal.z == normal.z)
			return ((int)i);
		i++;
	}
	return (-1);
}

/// One quarter-step in the independent model.
static void	model_step(const t_cut_family *f, uint32_t gen, uint8_t *model)
{
	uint8_t		next[MAX_STICKERS];
	uint32_t	i;
	int			j;
	t_ivec3		pos;
	t_ivec3		normal;

	memcpy(next, model, f->sticker_count);
	i = 0;
	while (i < f->sticker_count)
	{
		pos = f->stickers[i].pos;
		normal = f->stickers[i].normal;
		if (ivec3_dot(f->turns[gen].axis, pos) > f->turns[gen].threshold)
		{
			pos = ivec3_turn(f->turns[gen].axis, pos);
			normal = ivec3_turn(f->turns[gen].axis, normal);
			j = find(f, pos, normal);
			assert(j >= 0);
			next[j] = model[i];
		}
		i++;
	}
	memcpy(model, next, f->sticker_count);
}

static void	check_family(t_puzzle_type type, const t_cut_family *f,
				uint32_t expected_stickers)
{
	puzzle_t		*p = puzzle_create(type, 0);
	uint8_t			model[MAX_STICKERS];
	const uint8_t	*painted;
	uint32_t		rng = 12345;
	uint32_t		n;
	uint32_t		gen;
	uint32_t		amount;
	uint32_t		a;

	assert(p != NULL && f != NULL);
	assert(f->sticker_count == expected_stickers);
	assert(puzzle_facelet_count(p) == expected_stickers);
	assert(puzzle_facelet_geometry_stride(p) == 8);
	assert(puzzle_facelet_geometry(p) != NULL);
	memcpy(model, f->colours, expected_stickers);
	painted = puzzle_facelets(p);
	assert(memcmp(painted, model, expected_stickers) == 0);
	n = 0;
	while (n < RANDOM_MOVES)
	{
		rng = rng * 1664525u + 1013904223u;
		gen = (rng >> 16) % puzzle_generator_count(p);
		amount = 1 + ((rng >> 8) & 1);
		puzzle_apply_move(p, (int16_t)gen, (int8_t)amount);
		a = 0;
		while (a++ < amount)
			model_step(f, gen, model);
		painted = puzzle_facelets(p);
		assert(memcmp(painted, model, expected_stickers) == 0);
		n++;
	}
	puzzle_destroy(p);
}

/// The facelet records mirror the sticker array, byte for byte in
/// meaning, so web/ never has to re-derive anything.
static void	check_records(const t_cut_family *f)
{
	const t_cut_facelet	*rec = (const t_cut_facelet *)f->facelets;
	uint32_t			i;

	assert(f->facelet_stride == sizeof(t_cut_facelet));
	i = 0;
	while (i < f->sticker_count)
	{
		assert(rec[i].pos[0] == f->stickers[i].pos.x);
		assert(rec[i].pos[1] == f->stickers[i].pos.y);
		assert(rec[i].pos[2] == f->stickers[i].pos.z);
		assert(rec[i].normal[2] == f->stickers[i].normal.z);
		assert(rec[i].slot == f->stickers[i].slot);
		i++;
	}
}

/// Solved colours: every face is one colour, and the 'colour index'
/// range is 0..5 for Skewb, 0..3 for Pyraminx.
static void	check_solved_faces(const t_cut_family *f, uint8_t colour_count)
{
	uint32_t	i;
	uint32_t	j;

	i = 0;
	while (i < f->sticker_count)
	{
		assert(f->colours[i] < colour_count);
		j = 0;
		while (j < f->sticker_count)
		{
			if (f->stickers[i].normal.x == f->stickers[j].normal.x
				&& f->stickers[i].normal.y == f->stickers[j].normal.y
				&& f->stickers[i].normal.z == f->stickers[j].normal.z)
				assert(f->colours[i] == f->colours[j]);
			j++;
		}
		i++;
	}
}

/// The threshold the family itself was built with: Skewb cuts through
/// the middle; a Pyraminx `u` cuts at 15 and a `U` at 3.
static int32_t	f_turn_threshold(t_puzzle_type type, uint32_t gen)
{
	if (type == PUZZLE_SKEWB)
		return (0);
	return ((gen % 2 == 0) ? TETRA_TIP_THRESHOLD : TETRA_LAYER_THRESHOLD);
}

/// How many stickers each generator moves, and the axis export.
static void	check_geometry_exports(t_puzzle_type type, uint32_t moved_a,
				uint32_t moved_b, uint32_t period)
{
	puzzle_t	*p = puzzle_create(type, 0);
	uint8_t		out[MAX_STICKERS];
	int32_t		axis[4];
	uint32_t	g;

	g = 0;
	while (g < puzzle_generator_count(p))
	{
		assert(puzzle_generator_axis(p, g, axis) == 1);
		assert(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2] == 3);
		assert(axis[3] == f_turn_threshold(type, g));
		assert(puzzle_generator_facelets(p, g, out, MAX_STICKERS)
			== ((g % period == 0) ? moved_a : moved_b));
		g++;
	}
	assert(puzzle_generator_axis(p, puzzle_generator_count(p), axis) == 0);
	assert(puzzle_generator_facelets(p, 99, out, MAX_STICKERS) == 0xFFFFFFFFu);
	puzzle_destroy(p);
}

static void	check_cubic_unaffected(void)
{
	puzzle_t	*p = puzzle_create(PUZZLE_CUBIC, 3);
	int32_t		axis[4];
	uint8_t		out[4];

	assert(puzzle_facelet_geometry_stride(p) == 4);
	assert(puzzle_generator_axis(p, 0, axis) == 0);
	assert(puzzle_generator_facelets(p, 0, out, 4) == 0xFFFFFFFFu);
	puzzle_destroy(p);
}

int	main(void)
{
	check_family(PUZZLE_SKEWB, skewb_family(), SKEWB_STICKER_COUNT);
	check_family(PUZZLE_TETRAHEDRAL, tetrahedral_family(),
		TETRA_STICKER_COUNT);
	check_records(skewb_family());
	check_records(tetrahedral_family());
	check_solved_faces(skewb_family(), 6);
	check_solved_faces(tetrahedral_family(), 4);
	check_geometry_exports(PUZZLE_SKEWB, 15, 15, 1);
	check_geometry_exports(PUZZLE_TETRAHEDRAL, 3, 12, 2);
	check_cubic_unaffected();
	printf("test_cut_facelets: all assertions passed\n");
	return (0);
}
