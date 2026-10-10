#include "platform.h"
#include "puzzles/cubic.h"
#include "puzzles/cubic_facelets.h"

/// The facelet layer: turns a cubic state buffer into "which colour is
/// where", for any n. docs/en/07-nxn-web-view.md D1 (Option A) put this
/// in core/ rather than web/ so every orientation convention -- corner
/// twist, 3x3x3 edge flip, wing primary-sticker rule -- lives in exactly
/// one language, next to the move tables that define it.

/// Corner stickers, per slot, in orientation order -- ported from
/// rubik/src/render/geometry.c's CORNER_DIRS (via web/'s former
/// render/lattice/cubic.ts port of it): the U/D sticker first, then
/// clockwise round the corner. Same slot order as cubic.c's C_URF..
/// C_DRB.
static const uint8_t	CORNER_FACES[8][CUBIC_MAX_STICKERS] = {
	{CUBIC_GEN_U, CUBIC_GEN_R, CUBIC_GEN_F},
	{CUBIC_GEN_U, CUBIC_GEN_F, CUBIC_GEN_L},
	{CUBIC_GEN_U, CUBIC_GEN_L, CUBIC_GEN_B},
	{CUBIC_GEN_U, CUBIC_GEN_B, CUBIC_GEN_R},
	{CUBIC_GEN_D, CUBIC_GEN_F, CUBIC_GEN_R},
	{CUBIC_GEN_D, CUBIC_GEN_L, CUBIC_GEN_F},
	{CUBIC_GEN_D, CUBIC_GEN_B, CUBIC_GEN_L},
	{CUBIC_GEN_D, CUBIC_GEN_R, CUBIC_GEN_B},
};

/// Which end of x, y, z each corner slot sits at (1 = high: R, U, F).
static const uint8_t	CORNER_HIGH[8][3] = {
	{1, 1, 1}, {0, 1, 1}, {0, 1, 0}, {1, 1, 0},
	{1, 0, 1}, {0, 0, 1}, {0, 0, 0}, {1, 0, 0},
};

/// 3x3x3 edge stickers, per slot, primary first (U/D, else F/B -- the
/// same convention cubic_lattice.c's wing_delta() uses for wings).
/// Ported from geometry.c's EDGE_DIRS; slot order is cubic.c's E_UR..
/// E_BR.
static const uint8_t	EDGE_FACES[12][2] = {
	{CUBIC_GEN_U, CUBIC_GEN_R}, {CUBIC_GEN_U, CUBIC_GEN_F},
	{CUBIC_GEN_U, CUBIC_GEN_L}, {CUBIC_GEN_U, CUBIC_GEN_B},
	{CUBIC_GEN_D, CUBIC_GEN_R}, {CUBIC_GEN_D, CUBIC_GEN_F},
	{CUBIC_GEN_D, CUBIC_GEN_L}, {CUBIC_GEN_D, CUBIC_GEN_B},
	{CUBIC_GEN_F, CUBIC_GEN_R}, {CUBIC_GEN_F, CUBIC_GEN_L},
	{CUBIC_GEN_B, CUBIC_GEN_L}, {CUBIC_GEN_B, CUBIC_GEN_R},
};

/// 3x3x3 edge slot coordinates on the 0..2 lattice, same order as
/// EDGE_FACES.
static const uint8_t	EDGE_POINT[12][3] = {
	{2, 2, 1}, {1, 2, 2}, {0, 2, 1}, {1, 2, 0},
	{2, 0, 1}, {1, 0, 2}, {0, 0, 1}, {1, 0, 0},
	{2, 1, 2}, {0, 1, 2}, {0, 1, 0}, {2, 1, 0},
};

# define CUBIC_N3 3

static const t_cubic_facelet_table	*g_facelet_cache[CUBIC_CACHE_MAX];

t_cubic_point	cubic_corner_point(uint32_t corner, uint32_t n)
{
	int	m;

	m = (int)n - 1;
	return ((t_cubic_point){CORNER_HIGH[corner][0] * m,
		CORNER_HIGH[corner][1] * m, CORNER_HIGH[corner][2] * m});
}

/// @brief Collapses one size-n coordinate onto the 0..2 lattice the
///        EDGE_POINT table uses: low end 0, high end 2, anything between 1.
static int	to_n3(int v, uint32_t n)
{
	if (v == 0)
		return (0);
	if (v == (int)n - 1)
		return (2);
	return (1);
}

int	cubic_edge_index_for_point(t_cubic_point p, uint32_t n)
{
	int	e;

	e = 0;
	while (e < 12)
	{
		if (EDGE_POINT[e][0] == to_n3(p.x, n) && EDGE_POINT[e][1] == to_n3(p.y, n)
			&& EDGE_POINT[e][2] == to_n3(p.z, n))
			return (e);
		e++;
	}
	return (-1);
}

static int	coord(t_cubic_point p, t_cubic_axis axis)
{
	if (axis == CUBIC_AXIS_X)
		return (p.x);
	if (axis == CUBIC_AXIS_Y)
		return (p.y);
	return (p.z);
}

static int	is_extreme(int v, uint32_t n)
{
	return (v == 0 || v == (int)n - 1);
}

/// @brief The face `p` touches on `axis`, or CUBIC_FACE_COUNT if `p`
///        is not at either end of that axis.
static uint32_t	face_on_axis(t_cubic_point p, t_cubic_axis axis, uint32_t n)
{
	int	v;

	v = coord(p, axis);
	if (v == (int)n - 1)
		return (cubic_face_for(axis, 1));
	if (v == 0)
		return (cubic_face_for(axis, -1));
	return (CUBIC_FACE_COUNT);
}

static int	same_point(t_cubic_point a, t_cubic_point b)
{
	return (a.x == b.x && a.y == b.y && a.z == b.z);
}

/// @brief Fills a wing (or 3x3x3-edge-shaped) slot's two sticker
///        faces, primary first: its U/D face if it has one, else its
///        F/B face -- cubic_lattice.c's primary_facelet() rule -- then
///        the face on its other extreme axis.
static void	wing_faces(t_cubic_point p, uint32_t n, uint8_t *out)
{
	t_cubic_axis	primary;
	t_cubic_axis	other;

	primary = CUBIC_AXIS_Z;
	if (is_extreme(p.y, n))
		primary = CUBIC_AXIS_Y;
	other = CUBIC_AXIS_X;
	if (!is_extreme(p.x, n))
		other = CUBIC_AXIS_Z;
	if (other == primary)
		other = CUBIC_AXIS_Y;
	out[0] = (uint8_t)face_on_axis(p, primary, n);
	out[1] = (uint8_t)face_on_axis(p, other, n);
}

/// @brief The one face a centre slot sits on.
static uint8_t	centre_face(t_cubic_point p, uint32_t n)
{
	uint32_t	face;

	face = face_on_axis(p, CUBIC_AXIS_X, n);
	if (face == CUBIC_FACE_COUNT)
		face = face_on_axis(p, CUBIC_AXIS_Y, n);
	if (face == CUBIC_FACE_COUNT)
		face = face_on_axis(p, CUBIC_AXIS_Z, n);
	return ((uint8_t)face);
}

/// @brief Index of `face` in a slot's sticker-face list of `len` entries.
static uint8_t	sticker_of(const uint8_t *faces, uint32_t len, uint32_t face)
{
	uint32_t	k;

	k = 0;
	while (k < len && faces[k] != face)
		k++;
	return ((uint8_t)k);
}

/// @brief Slot index of the point `p` within `points` (`count` long).
static uint8_t	slot_of(const t_cubic_point *points, uint32_t count,
	t_cubic_point p)
{
	uint32_t	i;

	i = 0;
	while (i < count && !same_point(points[i], p))
		i++;
	return ((uint8_t)i);
}

static void	copy_faces(const uint8_t *src, uint32_t len, uint8_t *dst)
{
	uint32_t	k;

	k = 0;
	while (k < len)
	{
		dst[k] = src[k];
		k++;
	}
}

/// @brief Fills b->points/b->faces for every piece type of b->shape.
static void	collect_slots(t_facelet_build *b)
{
	uint32_t	type;
	uint32_t	count;
	uint32_t	i;

	type = 0;
	while (type < b->shape->piece_type_count)
	{
		count = b->shape->piece_types[type].count;
		b->points[type] = malloc(sizeof(t_cubic_point) * count);
		b->faces[type] = calloc(count * CUBIC_MAX_STICKERS, 1);
		i = 0;
		while (i < count)
		{
			if (type == CUBIC_CORNER_TYPE)
				b->points[type][i] = cubic_corner_point(i, b->n);
			else if (b->n == CUBIC_N3)
				b->points[type][i] = (t_cubic_point){EDGE_POINT[i][0],
					EDGE_POINT[i][1], EDGE_POINT[i][2]};
			else
				b->points[type][i] = cubic_lattice_point(b->n,
						type == CUBIC_WING_TYPE, i);
			if (type == CUBIC_CORNER_TYPE)
				copy_faces(CORNER_FACES[i], CUBIC_MAX_STICKERS,
					&b->faces[type][i * CUBIC_MAX_STICKERS]);
			else if (b->n == CUBIC_N3)
				copy_faces(EDGE_FACES[i], 2,
					&b->faces[type][i * CUBIC_MAX_STICKERS]);
			else if (type == CUBIC_WING_TYPE)
				wing_faces(b->points[type][i], b->n,
					&b->faces[type][i * CUBIC_MAX_STICKERS]);
			else
				b->faces[type][i * CUBIC_MAX_STICKERS]
					= centre_face(b->points[type][i], b->n);
			i++;
		}
		type++;
	}
}

/// @brief Which slot sits under lattice cell `p`, and which of its
///        stickers faces `face`.
static t_cubic_facelet_source	source_for(const t_facelet_build *b,
	t_cubic_point p, uint32_t face)
{
	t_cubic_facelet_source	src;
	uint32_t				extremes;
	uint32_t				type;

	extremes = is_extreme(p.x, b->n) + is_extreme(p.y, b->n)
		+ is_extreme(p.z, b->n);
	if (extremes == 1 && b->n == CUBIC_N3)
		return ((t_cubic_facelet_source){CUBIC_FIXED_FACELET, 0,
			(uint8_t)face});
	if (extremes == 3)
		type = CUBIC_CORNER_TYPE;
	else if (extremes == 2)
		type = CUBIC_WING_TYPE;
	else
		type = CUBIC_CENTRE_TYPE;
	src.piece_type = (uint8_t)type;
	src.slot = slot_of(b->points[type], b->shape->piece_types[type].count, p);
	src.sticker = sticker_of(&b->faces[type][src.slot * CUBIC_MAX_STICKERS],
			b->shape->piece_types[type].orientations, face);
	return (src);
}

/// @brief The cell at (u, v) on face `face`'s outer layer: the face's
///        own axis pinned to its extreme, the other two axes taking u
///        and v in x, y, z order.
static t_cubic_point	face_cell(uint32_t face, uint32_t n, int u, int v)
{
	const t_cubic_face	*f;
	int					pinned;

	f = cubic_face(face);
	pinned = 0;
	if (f->sign > 0)
		pinned = (int)n - 1;
	if (f->axis == CUBIC_AXIS_X)
		return ((t_cubic_point){pinned, u, v});
	if (f->axis == CUBIC_AXIS_Y)
		return ((t_cubic_point){u, pinned, v});
	return ((t_cubic_point){u, v, pinned});
}

static t_cubic_facelet_table	*build_table(uint32_t n)
{
	t_facelet_build			b;
	t_cubic_facelet_table	*table;
	t_cubic_facelet			*geometry;
	t_cubic_facelet_source	*sources;
	t_cubic_point			cell;
	uint32_t				i;

	b.n = n;
	b.shape = cubic_shape_for(n);
	collect_slots(&b);
	table = malloc(sizeof(*table));
	table->count = CUBIC_FACE_COUNT * n * n;
	geometry = malloc(sizeof(*geometry) * table->count);
	sources = malloc(sizeof(*sources) * table->count);
	i = 0;
	while (i < table->count)
	{
		cell = face_cell(i / (n * n), n, (int)(i % (n * n) / n), (int)(i % n));
		geometry[i] = (t_cubic_facelet){(uint8_t)cell.x, (uint8_t)cell.y,
			(uint8_t)cell.z, (uint8_t)(i / (n * n))};
		sources[i] = source_for(&b, cell, i / (n * n));
		i++;
	}
	i = 0;
	while (i < MAX_PIECE_TYPES)
	{
		table->slot_faces[i] = NULL;
		if (i < b.shape->piece_type_count)
		{
			free(b.points[i]);
			table->slot_faces[i] = b.faces[i];
		}
		i++;
	}
	table->geometry = geometry;
	table->sources = sources;
	return (table);
}

const t_cubic_facelet_table	*cubic_facelets_for(uint32_t n)
{
	if (!cubic_shape_for(n) || n >= CUBIC_CACHE_MAX)
		return (NULL);
	if (!g_facelet_cache[n])
		g_facelet_cache[n] = build_table(n);
	return (g_facelet_cache[n]);
}

void	cubic_facelets_paint(uint32_t n, const uint8_t *state, uint8_t *out)
{
	const t_cubic_facelet_table		*table;
	const t_shape					*shape;
	const t_cubic_facelet_source	*src;
	uint32_t						perm[MAX_PIECE_TYPES];
	uint32_t						orient[MAX_PIECE_TYPES];
	uint32_t						i;
	uint32_t						ors;
	uint32_t						piece;
	uint32_t						own;

	table = cubic_facelets_for(n);
	shape = cubic_shape_for(n);
	i = 0;
	while (i < shape->piece_type_count)
	{
		shape_piece_offsets(shape, i, &perm[i], &orient[i]);
		i++;
	}
	i = 0;
	while (i < table->count)
	{
		src = &table->sources[i];
		if (src->piece_type == CUBIC_FIXED_FACELET)
			out[i] = src->sticker;
		else
		{
			ors = shape->piece_types[src->piece_type].orientations;
			piece = state[perm[src->piece_type] + src->slot];
			own = (src->sticker + ors
					- state[orient[src->piece_type] + src->slot] % ors) % ors;
			out[i] = table->slot_faces[src->piece_type]
			[piece * CUBIC_MAX_STICKERS + own];
		}
		i++;
	}
}
