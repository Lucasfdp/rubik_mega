#include "platform.h"
#include "puzzles/skewb.h"
#include "puzzles/cubic.h"

static t_sticker	g_stickers[SKEWB_STICKER_COUNT];
static t_cut_turn	g_turns[SKEWB_CORNER_COUNT];
static int			g_stickers_ready = 0;

static const t_shape	SKEWB_SHAPE = {
	.piece_type_count = 2,
	.piece_types = {
		[SKEWB_CORNER_TYPE] = { SKEWB_CORNER_COUNT, SKEWB_CORNER_ORIENTATIONS },
		[SKEWB_CENTRE_TYPE] = { SKEWB_CENTRE_COUNT, SKEWB_CENTRE_ORIENTATIONS },
	},
};

const t_shape	*skewb_shape(void)
{
	return (&SKEWB_SHAPE);
}

static int	corner_sign(uint32_t corner, uint32_t bit)
{
	return ((corner & bit) ? -1 : 1);
}

static t_ivec3	corner_vec(uint32_t corner)
{
	t_ivec3	v;

	v.x = corner_sign(corner, SKEWB_CORNER_X_BIT);
	v.y = corner_sign(corner, SKEWB_CORNER_Y_BIT);
	v.z = corner_sign(corner, SKEWB_CORNER_Z_BIT);
	return (v);
}

static int32_t	*axis_of(t_ivec3 *v, uint32_t axis)
{
	if (axis == 0)
		return (&v->x);
	if (axis == 1)
		return (&v->y);
	return (&v->z);
}

/// The corner triangle of corner `corner` on the face perpendicular to
/// `axis`: its centroid is 3 along the face axis and 2 along each of
/// the other two (the cut lines run between edge midpoints, so the
/// triangle's corners are (3,3), (0,3), (3,0) in face coordinates and
/// its centroid (2,2)). The normal is the face's unit direction.
static t_sticker	corner_sticker(uint32_t corner, uint32_t axis)
{
	t_sticker	s;
	t_ivec3		c;
	uint32_t	a;

	c = corner_vec(corner);
	s.piece_type = SKEWB_CORNER_TYPE;
	s.slot = (uint8_t)corner;
	s.pos = c;
	s.normal.x = 0;
	s.normal.y = 0;
	s.normal.z = 0;
	a = 0;
	while (a < SKEWB_AXIS_COUNT)
	{
		*axis_of(&s.pos, a) *= (a == axis) ? SKEWB_SCALE : SKEWB_CORNER_INSET;
		a++;
	}
	*axis_of(&s.normal, axis) = *axis_of(&c, axis);
	return (s);
}

static t_sticker	centre_sticker(uint32_t slot)
{
	t_sticker	s;
	int			sign;

	sign = (slot % 2) ? -1 : 1;
	s.piece_type = SKEWB_CENTRE_TYPE;
	s.slot = (uint8_t)slot;
	s.pos.x = 0;
	s.pos.y = 0;
	s.pos.z = 0;
	*axis_of(&s.pos, slot / 2) = sign * SKEWB_SCALE;
	s.normal.x = 0;
	s.normal.y = 0;
	s.normal.z = 0;
	*axis_of(&s.normal, slot / 2) = sign;
	return (s);
}

static void	build_stickers(void)
{
	uint32_t	c;
	uint32_t	k;
	uint32_t	n;

	n = 0;
	c = 0;
	while (c < SKEWB_CORNER_COUNT)
	{
		k = 0;
		while (k < SKEWB_AXIS_COUNT)
		{
			g_stickers[n + k] = corner_sticker(c, k);
			k++;
		}
		cut_order_ccw(&g_stickers[n], corner_vec(c));
		n += SKEWB_AXIS_COUNT;
		c++;
	}
	c = 0;
	while (c < SKEWB_CENTRE_COUNT)
		g_stickers[n++] = centre_sticker(c++);
}

const t_sticker	*skewb_stickers(void)
{
	if (!g_stickers_ready)
	{
		build_stickers();
		g_stickers_ready = 1;
	}
	return (g_stickers);
}

int	skewb_is_canonical_corner(uint32_t corner)
{
	t_ivec3	v;

	v = corner_vec(corner);
	return (v.x * v.y * v.z > 0);
}

static t_generator_table	*g_generators = NULL;

/// Turn c cuts through the middle (threshold 0): everything on the
/// corner's side of the plane through the centre perpendicular to the
/// body diagonal moves. No sticker lies on a plane -- every dot product
/// is odd (2a + 2b + 3c) or 3 -- so the split is never ambiguous.
const t_generator_table	*skewb_generators(void)
{
	uint32_t	c;

	if (g_generators)
		return (g_generators);
	c = 0;
	while (c < SKEWB_CORNER_COUNT)
	{
		g_turns[c].axis = corner_vec(c);
		g_turns[c].threshold = 0;
		c++;
	}
	g_generators = cut_derive_generators(skewb_shape(), skewb_stickers(),
			SKEWB_STICKER_COUNT, g_turns, SKEWB_CORNER_COUNT, SKEWB_STEPS);
	return (g_generators);
}

static uint8_t			g_colours[SKEWB_STICKER_COUNT];
static t_cut_facelet	g_facelets[SKEWB_STICKER_COUNT];
static t_cut_family		g_family;
static int				g_family_ready = 0;

/// A sticker's solved colour is the face it lies on, numbered in the
/// cubic family's own order (U R F D L B = CUBIC_GEN_*), so web/ can
/// reuse one palette for cubes and Skewb.
static uint8_t	face_colour(t_ivec3 normal)
{
	if (normal.y != 0)
		return ((uint8_t)(normal.y > 0 ? CUBIC_GEN_U : CUBIC_GEN_D));
	if (normal.x != 0)
		return ((uint8_t)(normal.x > 0 ? CUBIC_GEN_R : CUBIC_GEN_L));
	return ((uint8_t)(normal.z > 0 ? CUBIC_GEN_F : CUBIC_GEN_B));
}

const t_cut_family	*skewb_family(void)
{
	uint32_t	i;

	if (g_family_ready)
		return (&g_family);
	if (!skewb_generators())
		return (NULL);
	i = 0;
	while (i < SKEWB_STICKER_COUNT)
	{
		g_colours[i] = face_colour(g_stickers[i].normal);
		i++;
	}
	cut_fill_facelets(g_facelets, g_stickers, SKEWB_STICKER_COUNT);
	g_family.shape = skewb_shape();
	g_family.generators = g_generators;
	g_family.stickers = g_stickers;
	g_family.sticker_count = SKEWB_STICKER_COUNT;
	g_family.turns = g_turns;
	g_family.colours = g_colours;
	g_family.facelets = (const uint8_t *)g_facelets;
	g_family.facelet_stride = (uint32_t)sizeof(t_cut_facelet);
	g_family_ready = 1;
	return (&g_family);
}
