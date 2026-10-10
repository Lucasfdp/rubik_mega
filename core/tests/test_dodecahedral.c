#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "puzzle.h"
#include "puzzles/dodecahedral.h"

/// Megaminx and Kilominx (docs/en/11-phase4-dodecahedral.md increments
/// 2 and 3): shape, derived generators, structure, invariants and the
/// facelet layer. The group order itself (10^68) cannot be enumerated;
/// it was checked once, outside the repo, against the closed forms
/// 20! 3^19 30! 2^29 / 4 and 20! 3^19 / 2 (DECISIONS.md), and the
/// structural oracles below stand in for it here: a wrong move model
/// almost never keeps all of them.

#define MEGAMINX_STATE_SIZE 124
#define KILOMINX_STATE_SIZE 40
#define SCRAMBLE_COUNT 200
#define SCRAMBLE_LENGTH 60

static const t_cut_family	*family(uint32_t size)
{
	const t_cut_family	*f = dodeca_family(size);

	assert(f != NULL);
	return (f);
}

static const t_cycle	*find_cycle(const t_generator *gen, uint32_t type)
{
	uint32_t	c;

	c = 0;
	while (c < gen->cycle_count)
	{
		if (gen->cycles[c].piece_type == type)
			return (&gen->cycles[c]);
		c++;
	}
	return (NULL);
}

static void	test_shape_and_table(void)
{
	const t_cut_family	*mega = family(DODECA_MEGAMINX_SIZE);
	const t_cut_family	*kilo = family(DODECA_KILOMINX_SIZE);

	assert(shape_is_valid(mega->shape) && shape_is_valid(kilo->shape));
	assert(shape_state_size(mega->shape) == MEGAMINX_STATE_SIZE);
	assert(shape_state_size(kilo->shape) == KILOMINX_STATE_SIZE);
	assert(mega->sticker_count == DODECA_MEGAMINX_STICKER_COUNT);
	assert(kilo->sticker_count == DODECA_KILOMINX_STICKER_COUNT);
	assert(mega->generators->count == DODECA_FACE_COUNT);
	assert(kilo->generators->count == DODECA_FACE_COUNT);
	assert(mega->generators->steps == DODECA_STEPS);
	assert(kilo->generators->steps == DODECA_STEPS);
	assert(mega->facelet_stride == 14 && kilo->facelet_stride == 14);
	assert(dodeca_family(1) == NULL && dodeca_family(4) == NULL
		&& dodeca_family(0) == NULL);
}

/// A face turn is one 5-cycle of corners and (Megaminx) one 5-cycle of
/// edges: the first time `steps` of 4 and MAX_CYCLE_LENGTH of 5 are
/// exercised at their documented limits.
static void	test_cycle_structure(void)
{
	uint32_t	size;
	uint32_t	g;
	const t_cut_family	*f;
	const t_generator	*gen;
	const t_cycle		*cy;
	uint32_t			delta_sum;
	uint32_t			i;

	size = DODECA_KILOMINX_SIZE;
	while (size <= DODECA_MEGAMINX_SIZE)
	{
		f = family(size);
		g = 0;
		while (g < DODECA_FACE_COUNT)
		{
			gen = &f->generators->generators[g];
			assert(gen->cycle_count == f->shape->piece_type_count - (size == DODECA_MEGAMINX_SIZE));
			cy = find_cycle(gen, DODECA_CORNER_TYPE);
			assert(cy && cy->length == DODECA_FACE_SIDES);
			delta_sum = 0;
			i = 0;
			while (i < cy->length)
				delta_sum += (uint32_t)cy->delta[i++];
			assert(delta_sum % DODECA_CORNER_ORIENTATIONS == 0);
			if (size == DODECA_MEGAMINX_SIZE)
			{
				cy = find_cycle(gen, DODECA_EDGE_TYPE);
				assert(cy && cy->length == DODECA_FACE_SIDES);
				delta_sum = 0;
				i = 0;
				while (i < cy->length)
					delta_sum += (uint32_t)cy->delta[i++];
				assert(delta_sum % DODECA_EDGE_ORIENTATIONS == 0);
				assert(find_cycle(gen, DODECA_CENTRE_TYPE) == NULL);
			}
			g++;
		}
		size++;
	}
}

/// Kilominx's generators are Megaminx's corner cycles, slot for slot and
/// delta for delta (the Phase 4 exit gate's third point): the same
/// derivation run over the corner stickers only.
static void	test_kilominx_is_megaminx_corners(void)
{
	const t_cut_family	*mega = family(DODECA_MEGAMINX_SIZE);
	const t_cut_family	*kilo = family(DODECA_KILOMINX_SIZE);
	uint32_t			g;
	const t_cycle		*a;
	const t_cycle		*b;

	g = 0;
	while (g < DODECA_FACE_COUNT)
	{
		a = find_cycle(&mega->generators->generators[g], DODECA_CORNER_TYPE);
		b = find_cycle(&kilo->generators->generators[g], DODECA_CORNER_TYPE);
		assert(a && b && memcmp(a, b, sizeof(t_cycle)) == 0);
		g++;
	}
}

/// Five quarter-steps of any face is a full turn; four are not.
static void	test_order_five(void)
{
	const t_cut_family	*f = family(DODECA_MEGAMINX_SIZE);
	uint8_t				solved[MEGAMINX_STATE_SIZE];
	uint8_t				s[MEGAMINX_STATE_SIZE];
	uint32_t			g;
	int8_t				k;

	shape_init_solved(f->shape, solved);
	g = 0;
	while (g < DODECA_FACE_COUNT)
	{
		memcpy(s, solved, sizeof(s));
		k = 0;
		while (k < DODECA_STEPS)
		{
			shape_apply_move(f->shape, f->generators, s,
				(t_move){(int16_t)g, 1});
			assert(memcmp(s, solved, sizeof(s)) != 0);
			k++;
		}
		shape_apply_move(f->shape, f->generators, s, (t_move){(int16_t)g, 1});
		assert(memcmp(s, solved, sizeof(s)) == 0);
		g++;
	}
}

static int	cycle_touches(const t_generator *gen, uint32_t type, uint32_t slot)
{
	const t_cycle	*cy = find_cycle(gen, type);
	uint32_t		i;

	i = 0;
	while (cy && i < cy->length)
		if (cy->slots[i++] == slot)
			return (1);
	return (0);
}

/// Every corner sits on exactly three faces and every edge on exactly
/// two -- the dodecahedron's own incidence counts, read back out of the
/// derived generators.
static void	test_incidence(void)
{
	const t_cut_family	*f = family(DODECA_MEGAMINX_SIZE);
	uint32_t			slot;
	uint32_t			g;
	uint32_t			n;

	slot = 0;
	while (slot < DODECA_CORNER_COUNT)
	{
		n = 0;
		g = 0;
		while (g < DODECA_FACE_COUNT)
			n += (uint32_t)cycle_touches(&f->generators->generators[g++],
					DODECA_CORNER_TYPE, slot);
		assert(n == 3);
		slot++;
	}
	slot = 0;
	while (slot < DODECA_EDGE_COUNT)
	{
		n = 0;
		g = 0;
		while (g < DODECA_FACE_COUNT)
			n += (uint32_t)cycle_touches(&f->generators->generators[g++],
					DODECA_EDGE_TYPE, slot);
		assert(n == 2);
		slot++;
	}
}

static int	shares_piece(const t_generator *a, const t_generator *b)
{
	uint32_t	slot;

	slot = 0;
	while (slot < DODECA_CORNER_COUNT)
	{
		if (cycle_touches(a, DODECA_CORNER_TYPE, slot)
			&& cycle_touches(b, DODECA_CORNER_TYPE, slot))
			return (1);
		slot++;
	}
	return (0);
}

/// Two faces commute exactly when no corner is shared: adjacent faces
/// (five per face) do not, the other six do. Counted both ways, so it
/// pins the move model's adjacency to the real dodecahedron's.
static void	test_commutation(void)
{
	const t_cut_family	*f = family(DODECA_MEGAMINX_SIZE);
	uint8_t				ab[MEGAMINX_STATE_SIZE];
	uint8_t				ba[MEGAMINX_STATE_SIZE];
	uint32_t			a;
	uint32_t			b;
	uint32_t			adjacent;
	int					commutes;

	a = 0;
	while (a < DODECA_FACE_COUNT)
	{
		adjacent = 0;
		b = 0;
		while (b < DODECA_FACE_COUNT)
		{
			shape_init_solved(f->shape, ab);
			shape_apply_move(f->shape, f->generators, ab, (t_move){(int16_t)a, 1});
			shape_apply_move(f->shape, f->generators, ab, (t_move){(int16_t)b, 1});
			shape_init_solved(f->shape, ba);
			shape_apply_move(f->shape, f->generators, ba, (t_move){(int16_t)b, 1});
			shape_apply_move(f->shape, f->generators, ba, (t_move){(int16_t)a, 1});
			commutes = (memcmp(ab, ba, sizeof(ab)) == 0);
			if (a != b)
			{
				assert(commutes == !shares_piece(
						&f->generators->generators[a],
						&f->generators->generators[b]));
				adjacent += (uint32_t)!commutes;
			}
			b++;
		}
		assert(adjacent == DODECA_FACE_SIDES);
		a++;
	}
}

/// Parity of a permutation of `count` entries, by counting cycles.
static int	perm_is_even(const uint8_t *perm, uint32_t count)
{
	uint8_t		seen[DODECA_EDGE_COUNT];
	uint32_t	i;
	uint32_t	j;
	uint32_t	cycles;

	memset(seen, 0, sizeof(seen));
	cycles = 0;
	i = 0;
	while (i < count)
	{
		if (!seen[i])
		{
			cycles++;
			j = i;
			while (!seen[j])
			{
				seen[j] = 1;
				j = perm[j];
			}
		}
		i++;
	}
	return ((count - cycles) % 2 == 0);
}

static uint32_t	orient_sum(const uint8_t *orient, uint32_t count)
{
	uint32_t	sum;
	uint32_t	i;

	sum = 0;
	i = 0;
	while (i < count)
		sum += orient[i++];
	return (sum);
}

/// The invariants D2 lists, on 200 random scrambles of each puzzle:
/// corner twist sum 0 mod 3, edge flip sum 0 mod 2, both permutations
/// even (a face turn is a 5-cycle, and 5-cycles are even), and the
/// centres never move. A wrong move model almost never keeps all of
/// them.
static void	test_invariants(uint32_t size)
{
	puzzle_t		*p;
	const uint8_t	*state;
	uint32_t		perm_off;
	uint32_t		orient_off;
	uint32_t		seed;

	seed = 1;
	while (seed <= SCRAMBLE_COUNT)
	{
		p = puzzle_create(PUZZLE_DODECAHEDRAL, size);
		assert(p != NULL);
		puzzle_scramble(p, SCRAMBLE_LENGTH, seed);
		state = puzzle_state_ptr(p);
		shape_piece_offsets(family(size)->shape, DODECA_CORNER_TYPE,
			&perm_off, &orient_off);
		assert(perm_is_even(state + perm_off, DODECA_CORNER_COUNT));
		assert(orient_sum(state + orient_off, DODECA_CORNER_COUNT)
			% DODECA_CORNER_ORIENTATIONS == 0);
		if (size == DODECA_MEGAMINX_SIZE)
		{
			shape_piece_offsets(family(size)->shape, DODECA_EDGE_TYPE,
				&perm_off, &orient_off);
			assert(perm_is_even(state + perm_off, DODECA_EDGE_COUNT));
			assert(orient_sum(state + orient_off, DODECA_EDGE_COUNT)
				% DODECA_EDGE_ORIENTATIONS == 0);
			shape_piece_offsets(family(size)->shape, DODECA_CENTRE_TYPE,
				&perm_off, &orient_off);
			assert(memcmp(state + perm_off, (uint8_t[]){0, 1, 2, 3, 4, 5,
					6, 7, 8, 9, 10, 11}, DODECA_FACE_COUNT) == 0);
		}
		puzzle_destroy(p);
		seed++;
	}
}

/// A face turn moves exactly 25 Megaminx stickers and 15 Kilominx ones:
/// the pieces its cycles carry, times their sticker counts.
static void	test_moved_stickers(void)
{
	uint32_t			size;
	const t_cut_family	*f;
	uint32_t			g;
	uint32_t			c;
	uint32_t			total;
	const t_cycle		*cy;

	size = DODECA_KILOMINX_SIZE;
	while (size <= DODECA_MEGAMINX_SIZE)
	{
		f = family(size);
		g = 0;
		while (g < DODECA_FACE_COUNT)
		{
			total = 0;
			c = 0;
			while (c < f->generators->generators[g].cycle_count)
			{
				cy = &f->generators->generators[g].cycles[c++];
				total += cy->length
					* f->shape->piece_types[cy->piece_type].orientations;
			}
			assert(total == (size == DODECA_MEGAMINX_SIZE
					? DODECA_MEGAMINX_MOVED_STICKERS
					: DODECA_KILOMINX_MOVED_STICKERS));
			g++;
		}
		size++;
	}
}

static void	test_puzzle_api(void)
{
	puzzle_t	*mega = puzzle_create(PUZZLE_DODECAHEDRAL, DODECA_MEGAMINX_SIZE);
	puzzle_t	*kilo = puzzle_create(PUZZLE_DODECAHEDRAL, DODECA_KILOMINX_SIZE);
	uint8_t		solved[MEGAMINX_STATE_SIZE];
	int32_t		axis[4];
	uint8_t		moving[DODECA_MEGAMINX_STICKER_COUNT];

	assert(mega && kilo);
	assert(puzzle_create(PUZZLE_DODECAHEDRAL, 0) == NULL);
	assert(puzzle_create(PUZZLE_DODECAHEDRAL, 4) == NULL);
	assert(puzzle_generator_count(mega) == DODECA_FACE_COUNT);
	assert(puzzle_state_size(mega) == MEGAMINX_STATE_SIZE);
	assert(puzzle_state_size(kilo) == KILOMINX_STATE_SIZE);
	assert(puzzle_facelet_count(mega) == DODECA_MEGAMINX_STICKER_COUNT);
	assert(puzzle_facelet_count(kilo) == DODECA_KILOMINX_STICKER_COUNT);
	assert(puzzle_facelet_geometry_stride(mega) == 14);
	assert(puzzle_facelet_geometry(mega) != NULL);
	assert(puzzle_generator_face(mega, 0) == -1);
	shape_init_solved(dodeca_shape(DODECA_MEGAMINX_SIZE), solved);
	assert(memcmp(puzzle_state_ptr(mega), solved, MEGAMINX_STATE_SIZE) == 0);
	puzzle_scramble(mega, 30, 7);
	assert(memcmp(puzzle_state_ptr(mega), solved, MEGAMINX_STATE_SIZE) != 0);
	assert(puzzle_generator_axis(mega, 0, axis) == 1);
	assert(axis[0] == 0 && axis[1] == DODECA_COORD_SCALE && axis[2] == 0);
	assert(axis[3] > 0);
	assert(puzzle_generator_axis(mega, DODECA_FACE_COUNT, axis) == 0);
	assert(puzzle_generator_facelets(mega, 0, moving, sizeof(moving)) == 26);
	assert(puzzle_generator_facelets(kilo, 0, moving, sizeof(moving)) == 15);
	puzzle_destroy(mega);
	puzzle_destroy(kilo);
}

/// The facelet layer on a solved puzzle shows each face's own colour
/// on all its stickers, 11 per face on a Megaminx and 5 on a Kilominx.
static void	test_solved_colours(uint32_t size)
{
	puzzle_t		*p = puzzle_create(PUZZLE_DODECAHEDRAL, size);
	const uint8_t	*colours;
	uint32_t		per_face[DODECA_FACE_COUNT];
	uint32_t		i;

	assert(p != NULL);
	colours = puzzle_facelets(p);
	memset(per_face, 0, sizeof(per_face));
	i = 0;
	while (i < puzzle_facelet_count(p))
	{
		assert(colours[i] < DODECA_FACE_COUNT);
		per_face[colours[i]]++;
		i++;
	}
	i = 0;
	while (i < DODECA_FACE_COUNT)
		assert(per_face[i++] == (size == DODECA_MEGAMINX_SIZE
				? DODECA_STICKERS_PER_FACE : DODECA_FACE_SIDES));
	puzzle_destroy(p);
}

/// The wide record carries each sticker's position and normal at scale
/// 4096: normals are unit length, positions lie on their face's plane
/// (dot with the normal equals the inradius 1.3764 for every sticker),
/// and the record's slot/type match the sticker table.
static void	test_wide_records(void)
{
	const t_cut_family	*f = family(DODECA_MEGAMINX_SIZE);
	const t_cut_facelet_wide	*w = (const t_cut_facelet_wide *)f->facelets;
	uint32_t			i;
	double				len;
	double				height;
	double				x;
	double				y;
	double				z;

	i = 0;
	while (i < f->sticker_count)
	{
		x = w[i].normal[0];
		y = w[i].normal[1];
		z = w[i].normal[2];
		len = (x * x + y * y + z * z) / ((double)DODECA_COORD_SCALE
				* DODECA_COORD_SCALE);
		assert(len > 0.999 && len < 1.001);
		height = (x * w[i].pos[0] + y * w[i].pos[1] + z * w[i].pos[2])
			/ ((double)DODECA_COORD_SCALE * DODECA_COORD_SCALE);
		assert(height > 1.3763 - 0.001 && height < 1.3764 + 0.001);
		assert(w[i].piece_type == f->stickers[i].piece_type);
		assert(w[i].slot == f->stickers[i].slot);
		i++;
	}
}

static void	test_face_names(void)
{
	assert(strcmp(dodeca_face_name(0), "U") == 0);
	assert(strcmp(dodeca_face_name(1), "F") == 0);
	assert(strcmp(dodeca_face_name(11), "D") == 0);
	assert(dodeca_face_name(DODECA_FACE_COUNT) == NULL);
}

int	main(void)
{
	test_shape_and_table();
	test_cycle_structure();
	test_kilominx_is_megaminx_corners();
	test_order_five();
	test_incidence();
	test_commutation();
	test_moved_stickers();
	test_invariants(DODECA_KILOMINX_SIZE);
	test_invariants(DODECA_MEGAMINX_SIZE);
	test_puzzle_api();
	test_solved_colours(DODECA_KILOMINX_SIZE);
	test_solved_colours(DODECA_MEGAMINX_SIZE);
	test_wide_records();
	test_face_names();
	printf("test_dodecahedral: all assertions passed\n");
	return (0);
}
