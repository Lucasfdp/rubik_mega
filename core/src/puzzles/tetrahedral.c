#include "platform.h"
#include "puzzles/tetrahedral.h"

static t_sticker	g_stickers[TETRA_STICKER_COUNT];
static t_cut_turn	g_turns[TETRA_GENERATOR_COUNT];
static int			g_stickers_ready = 0;

static const t_shape	TETRA_SHAPE = {
	.piece_type_count = 3,
	.piece_types = {
		[TETRA_TIP_TYPE] = { TETRA_VERTEX_COUNT,
			TETRA_VERTEX_PIECE_ORIENTATIONS },
		[TETRA_AXIAL_TYPE] = { TETRA_VERTEX_COUNT,
			TETRA_VERTEX_PIECE_ORIENTATIONS },
		[TETRA_EDGE_TYPE] = { TETRA_EDGE_COUNT, TETRA_EDGE_ORIENTATIONS },
	},
};

const t_shape	*tetrahedral_shape(void)
{
	return (&TETRA_SHAPE);
}

/// A regular tetrahedron inscribed in the cube [-1, 1]^3: the four
/// corners with an even number of -1 coordinates: (1,1,1),
/// (1,-1,-1), (-1,1,-1), (-1,-1,1). Each has squared length 3, which is
/// what ivec3_turn() needs. Face f is the one opposite vertex f, with
/// outward normal -V_f.
static const t_ivec3	TETRA_VERTEX[TETRA_VERTEX_COUNT] = {
	{ 1, 1, 1 }, { 1, -1, -1 }, { -1, 1, -1 }, { -1, -1, 1 },
};

static t_ivec3	vec_neg(t_ivec3 v)
{
	t_ivec3	n;

	n.x = -v.x;
	n.y = -v.y;
	n.z = -v.z;
	return (n);
}

uint8_t	tetrahedral_edge_slot(uint32_t a, uint32_t b)
{
	static const int8_t	slot[TETRA_VERTEX_COUNT][TETRA_VERTEX_COUNT] = {
		{ -1, 0, 1, 2 }, { 0, -1, 3, 4 }, { 1, 3, -1, 5 }, { 2, 4, 5, -1 },
	};

	return ((uint8_t)slot[a][b]);
}

/// Sum of a lattice triangle's three vertex coordinates, for the vertex
/// it is closest to: a tip triangle is (7, 1, 1), an axial triangle
/// (5, 2, 2), an edge triangle (4, 4, 1). Each triple sums to 9 =
/// TETRA_BARYCENTRIC_SCALE. Read off a Pyraminx face's 3x3 lattice.
# define TETRA_TIP_WEIGHT 7
# define TETRA_AXIAL_WEIGHT 5

/// One lattice triangle of face `face`, given by the sum S[0..2] of its
/// three lattice-vertex coordinates over the face's three vertices
/// `vs[0..2]` (every vertex except `face`, ascending). S_v is already 9
/// times the centroid's barycentric weight, so the position is
/// sum S_v * V_v. The largest weight names the piece: 7 a tip, 5 an
/// axial piece (both at that vertex), 4 an edge -- between the two
/// vertices that are NOT the lightest.
static t_sticker	make_sticker(int face, const int *vs, const int *s)
{
	t_sticker	st;
	int			best;
	int			low;
	int			a;
	int			b;
	int			i;

	best = 0;
	low = 0;
	st.pos.x = s[0] * TETRA_VERTEX[vs[0]].x;
	st.pos.y = s[0] * TETRA_VERTEX[vs[0]].y;
	st.pos.z = s[0] * TETRA_VERTEX[vs[0]].z;
	i = 1;
	while (i < 3)
	{
		best = (s[i] > s[best]) ? i : best;
		low = (s[i] < s[low]) ? i : low;
		st.pos.x += s[i] * TETRA_VERTEX[vs[i]].x;
		st.pos.y += s[i] * TETRA_VERTEX[vs[i]].y;
		st.pos.z += s[i] * TETRA_VERTEX[vs[i]].z;
		i++;
	}
	st.normal = vec_neg(TETRA_VERTEX[face]);
	st.slot = (uint8_t)vs[best];
	st.piece_type = (s[best] == TETRA_TIP_WEIGHT) ? TETRA_TIP_TYPE
		: TETRA_AXIAL_TYPE;
	if (s[best] != TETRA_TIP_WEIGHT && s[best] != TETRA_AXIAL_WEIGHT)
	{
		st.piece_type = TETRA_EDGE_TYPE;
		a = (low + 1) % 3;
		b = (low + 2) % 3;
		st.slot = (a < b) ? tetrahedral_edge_slot((uint32_t)vs[a], (uint32_t)vs[b])
			: tetrahedral_edge_slot((uint32_t)vs[b], (uint32_t)vs[a]);
	}
	return (st);
}

/// Appends face `face`'s nine stickers to `raw`, unsorted. Up triangles
/// sit on lattice base (i, j, k) with i + j + k == 2 and have vertex
/// sums (3i+1, 3j+1, 3k+1); down triangles sit on i + j + k == 1 with
/// sums (3i+2, 3j+2, 3k+2). Six up + three down = nine.
static int	face_stickers(int face, t_sticker *raw, int n)
{
	int	vs[3];
	int	i;
	int	j;
	int	s[3];

	j = 0;
	i = 0;
	while (i < TETRA_VERTEX_COUNT)
	{
		if (i != face)
			vs[j++] = i;
		i++;
	}
	i = 0;
	while (i < 3)
	{
		j = 0;
		while (j < 3 - i)
		{
			s[0] = 3 * i + 1;
			s[1] = 3 * j + 1;
			s[2] = 3 * (2 - i - j) + 1;
			raw[n++] = make_sticker(face, vs, s);
			j++;
		}
		i++;
	}
	i = 0;
	while (i < 2)
	{
		j = 0;
		while (j < 2 - i)
		{
			s[0] = 3 * i + 2;
			s[1] = 3 * j + 2;
			s[2] = 3 * (1 - i - j) + 2;
			raw[n++] = make_sticker(face, vs, s);
			j++;
		}
		i++;
	}
	return (n);
}

/// Copies the stickers of piece (type, slot) out of `raw`, in face
/// order, into `out`; returns how many.
static int	gather_slot(const t_sticker *raw, int type, int slot,
				t_sticker *out)
{
	int	i;
	int	n;

	n = 0;
	i = 0;
	while (i < TETRA_STICKER_COUNT)
	{
		if (raw[i].piece_type == type && raw[i].slot == slot)
			out[n++] = raw[i];
		i++;
	}
	return (n);
}

static void	build_stickers(void)
{
	t_sticker	raw[TETRA_STICKER_COUNT];
	int			n;
	int			type;
	int			slot;

	n = 0;
	slot = 0;
	while (slot < TETRA_FACE_COUNT)
		n = face_stickers(slot++, raw, n);
	n = 0;
	type = 0;
	while (type < (int)TETRA_SHAPE.piece_type_count)
	{
		slot = 0;
		while (slot < (int)TETRA_SHAPE.piece_types[type].count)
		{
			if (gather_slot(raw, type, slot, &g_stickers[n])
				== TETRA_VERTEX_PIECE_ORIENTATIONS)
				cut_order_ccw(&g_stickers[n], TETRA_VERTEX[slot]);
			n += (int)TETRA_SHAPE.piece_types[type].orientations;
			slot++;
		}
		type++;
	}
}

const t_sticker	*tetrahedral_stickers(void)
{
	if (!g_stickers_ready)
	{
		build_stickers();
		g_stickers_ready = 1;
	}
	return (g_stickers);
}

static t_generator_table	*g_generators = NULL;

/// Generator vertex * 2 + depth: dot(V_v, pos) above the tip threshold
/// is the tip alone, above the layer threshold is tip + layer. Same
/// "no sticker on the plane" reasoning as Skewb: dot = 4 * S_v - 9 is
/// odd, and both thresholds are odd, so no sticker lies exactly on a cut.
const t_generator_table	*tetrahedral_generators(void)
{
	int	v;

	if (g_generators)
		return (g_generators);
	v = 0;
	while (v < TETRA_VERTEX_COUNT)
	{
		g_turns[v * TETRA_DEPTH_COUNT + TETRA_DEPTH_TIP].axis
			= TETRA_VERTEX[v];
		g_turns[v * TETRA_DEPTH_COUNT + TETRA_DEPTH_TIP].threshold
			= TETRA_TIP_THRESHOLD;
		g_turns[v * TETRA_DEPTH_COUNT + TETRA_DEPTH_LAYER].axis
			= TETRA_VERTEX[v];
		g_turns[v * TETRA_DEPTH_COUNT + TETRA_DEPTH_LAYER].threshold
			= TETRA_LAYER_THRESHOLD;
		v++;
	}
	g_generators = cut_derive_generators(tetrahedral_shape(),
			tetrahedral_stickers(), TETRA_STICKER_COUNT, g_turns,
			TETRA_GENERATOR_COUNT, TETRA_STEPS);
	return (g_generators);
}

static uint8_t			g_colours[TETRA_STICKER_COUNT];
static t_cut_facelet	g_facelets[TETRA_STICKER_COUNT];
static t_cut_family		g_family;
static int				g_family_ready = 0;

/// A sticker's solved colour is the face it lies on: face f, the one
/// opposite vertex f, has outward normal -V_f, so the colour is f.
static uint8_t	face_colour(t_ivec3 normal)
{
	uint8_t	f;

	f = 0;
	while (f < TETRA_FACE_COUNT)
	{
		if (normal.x == -TETRA_VERTEX[f].x && normal.y == -TETRA_VERTEX[f].y
			&& normal.z == -TETRA_VERTEX[f].z)
			return (f);
		f++;
	}
	return (0);
}

const t_cut_family	*tetrahedral_family(void)
{
	uint32_t	i;

	if (g_family_ready)
		return (&g_family);
	if (!tetrahedral_generators())
		return (NULL);
	i = 0;
	while (i < TETRA_STICKER_COUNT)
	{
		g_colours[i] = face_colour(g_stickers[i].normal);
		i++;
	}
	cut_fill_facelets(g_facelets, g_stickers, TETRA_STICKER_COUNT);
	g_family.shape = tetrahedral_shape();
	g_family.generators = g_generators;
	g_family.stickers = g_stickers;
	g_family.sticker_count = TETRA_STICKER_COUNT;
	g_family.turns = g_turns;
	g_family.colours = g_colours;
	g_family.facelets = (const uint8_t *)g_facelets;
	g_family.facelet_stride = (uint32_t)sizeof(t_cut_facelet);
	g_family_ready = 1;
	return (&g_family);
}
