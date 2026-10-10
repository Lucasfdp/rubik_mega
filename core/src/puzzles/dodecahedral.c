#include "platform.h"
#include "puzzles/dodecahedral.h"

/// Megaminx and Kilominx (docs/en/11-phase4-dodecahedral.md D2-A, D3, D4).
///
/// The geometry is built once, in floating point, from the regular
/// dodecahedron's vertices; everything that leaves this file is
/// integers: stickers at a fixed scale, turns as an integer axis plus a
/// plane, generators derived by cut_derive_generators_rotated() -- which
/// refuses unless every rotated sticker lands cleanly on exactly one
/// sticker. A wrong geometry therefore fails at init, never in play.
///
/// No libm: the freestanding WASM build has none, and the only function
/// the geometry needs is a square root (dsqrt() below).

/// Real coordinates, filled by build_geometry().
static t_dvec3	g_vertex[DODECA_CORNER_COUNT];
static t_dvec3	g_face_normal[DODECA_FACE_COUNT];
static t_dvec3	g_face_centre[DODECA_FACE_COUNT];
static uint8_t	g_face_corner[DODECA_FACE_COUNT][DODECA_FACE_SIDES];
static uint8_t	g_edge_corner[DODECA_EDGE_COUNT][2];
static double	g_inradius;

static t_sticker		g_stickers[DODECA_MEGAMINX_STICKER_COUNT];
static t_cut_turn		g_turns[DODECA_FACE_COUNT];
static uint8_t			g_colours[DODECA_MEGAMINX_STICKER_COUNT];
static t_cut_facelet_wide	g_facelets[DODECA_MEGAMINX_STICKER_COUNT];
static int				g_geometry_ready = 0;

static const t_shape	MEGAMINX_SHAPE = {
	.piece_type_count = 3,
	.piece_types = {
		[DODECA_CORNER_TYPE] = { DODECA_CORNER_COUNT,
			DODECA_CORNER_ORIENTATIONS },
		[DODECA_EDGE_TYPE] = { DODECA_EDGE_COUNT, DODECA_EDGE_ORIENTATIONS },
		[DODECA_CENTRE_TYPE] = { DODECA_FACE_COUNT,
			DODECA_CENTRE_ORIENTATIONS },
	},
};

static const t_shape	KILOMINX_SHAPE = {
	.piece_type_count = 1,
	.piece_types = {
		[DODECA_CORNER_TYPE] = { DODECA_CORNER_COUNT,
			DODECA_CORNER_ORIENTATIONS },
	},
};

const t_shape	*dodeca_shape(uint32_t size)
{
	if (size == DODECA_MEGAMINX_SIZE)
		return (&MEGAMINX_SHAPE);
	if (size == DODECA_KILOMINX_SIZE)
		return (&KILOMINX_SHAPE);
	return (NULL);
}

/// Names in face-index order; see dodecahedral.h.
static const char	*const FACE_NAMES[DODECA_FACE_COUNT] = {
	"U", "F", "R", "BR", "BL", "L", "DR", "DBR", "DB", "DBL", "DL", "D",
};

const char	*dodeca_face_name(uint32_t face)
{
	if (face >= DODECA_FACE_COUNT)
		return (NULL);
	return (FACE_NAMES[face]);
}

/// Square root by Newton's method, from a starting guess that is never
/// zero. Quadratic convergence: 64 rounds is far past double precision
/// for any input here (all between 1/100 and 100), and a fixed round
/// count keeps the result identical on every target.
# define SQRT_ROUNDS 64

static double	dsqrt(double x)
{
	double	guess;
	int		i;

	guess = (x + 1.0) / 2.0;
	i = 0;
	while (i < SQRT_ROUNDS)
	{
		guess = (guess + x / guess) / 2.0;
		i++;
	}
	return (guess);
}

static double	dabs(double x)
{
	return (x < 0.0 ? -x : x);
}

static t_dvec3	dv(double x, double y, double z)
{
	t_dvec3	v;

	v.x = x;
	v.y = y;
	v.z = z;
	return (v);
}

static t_dvec3	dv_sub(t_dvec3 a, t_dvec3 b)
{
	return (dv(a.x - b.x, a.y - b.y, a.z - b.z));
}

static t_dvec3	dv_add(t_dvec3 a, t_dvec3 b)
{
	return (dv(a.x + b.x, a.y + b.y, a.z + b.z));
}

static t_dvec3	dv_scale(t_dvec3 a, double k)
{
	return (dv(a.x * k, a.y * k, a.z * k));
}

static double	dv_dot(t_dvec3 a, t_dvec3 b)
{
	return (a.x * b.x + a.y * b.y + a.z * b.z);
}

static t_dvec3	dv_cross(t_dvec3 a, t_dvec3 b)
{
	return (dv(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
			a.x * b.y - a.y * b.x));
}

static t_dvec3	dv_unit(t_dvec3 a)
{
	return (dv_scale(a, 1.0 / dsqrt(dv_dot(a, a))));
}

/// A double to the nearest integer, rounding half away from zero so the
/// mapping is symmetric about the origin.
static int32_t	round_to_int(double value)
{
	return ((int32_t)(value < 0.0 ? value - 0.5 : value + 0.5));
}

/// A real value to its fixed-point integer.
static int32_t	to_fixed(double value)
{
	return (round_to_int(value * DODECA_COORD_SCALE));
}

static t_ivec3	to_fixed_vec(t_dvec3 v)
{
	t_ivec3	r;

	r.x = to_fixed(v.x);
	r.y = to_fixed(v.y);
	r.z = to_fixed(v.z);
	return (r);
}

/// An integer vector as doubles, with no scaling: fixed-point vectors
/// stay in fixed-point units through dodeca_turn().
static t_dvec3	int_vec_to_double(t_ivec3 v)
{
	return (dv((double)v.x, (double)v.y, (double)v.z));
}

/// One quarter-step of a face turn: 72 degrees clockwise seen from the
/// tip of `axis`, which is the face's unit normal at fixed scale.
/// Rodrigues' formula with cos 72 = (sqrt 5 - 1) / 4 and sin 72 =
/// sqrt(10 + 2 sqrt 5) / 4; the rotation is by -72 degrees about +axis
/// because a clockwise turn seen from the tip is counter-clockwise
/// about the opposite direction (the same sign choice ivec3_turn
/// makes). The result is rounded: the tolerance in t_cut_rotation
/// absorbs it, and every sticker is checked against that tolerance.
static t_ivec3	dodeca_turn(t_ivec3 axis, t_ivec3 p)
{
	t_dvec3	n;
	t_dvec3	v;
	t_dvec3	r;
	t_ivec3	out;
	double	cos_a;
	double	sin_a;
	double	sqrt5;

	sqrt5 = dsqrt(5.0);
	cos_a = (sqrt5 - 1.0) / 4.0;
	sin_a = -dsqrt(10.0 + 2.0 * sqrt5) / 4.0;
	n = dv_unit(int_vec_to_double(axis));
	v = int_vec_to_double(p);
	r = dv_add(dv_add(dv_scale(v, cos_a), dv_scale(dv_cross(n, v), sin_a)),
			dv_scale(n, dv_dot(n, v) * (1.0 - cos_a)));
	out.x = round_to_int(r.x);
	out.y = round_to_int(r.y);
	out.z = round_to_int(r.z);
	return (out);
}

static const t_cut_rotation	DODECA_ROTATION = {
	dodeca_turn, DODECA_MATCH_TOLERANCE_SQ, DODECA_MATCH_SEPARATION_SQ,
};

/// The 20 vertices and 12 face normals of a regular dodecahedron with
/// vertices (+-1, +-1, +-1) and the cyclic permutations of
/// (0, +-1/phi, +-phi); face normals are the cyclic permutations of
/// (0, +-phi, +-1), normalised. This is the frame the spec's spike used,
/// before reorientation to put one face on top.
static void	raw_solid(t_dvec3 *vertex, t_dvec3 *normal)
{
	double	phi;
	int		v;
	int		n;
	int		a;
	int		b;

	phi = (1.0 + dsqrt(5.0)) / 2.0;
	v = 0;
	n = 0;
	a = -1;
	while (a <= 1)
	{
		b = -1;
		while (b <= 1)
		{
			vertex[v++] = dv(a, b, 1.0);
			vertex[v++] = dv(a, b, -1.0);
			vertex[v++] = dv(0.0, a / phi, b * phi);
			vertex[v++] = dv(a / phi, b * phi, 0.0);
			vertex[v++] = dv(b * phi, 0.0, a / phi);
			normal[n++] = dv_unit(dv(0.0, a * phi, b));
			normal[n++] = dv_unit(dv(a * phi, b, 0.0));
			normal[n++] = dv_unit(dv(b, 0.0, a * phi));
			b += 2;
		}
		a += 2;
	}
}

/// The viewing frame: +y is face 0's normal, +z points at the middle of
/// one neighbouring face (the front), +x completes a right-handed
/// basis. Applies the frame to every vertex and normal.
static void	orient_solid(t_dvec3 *vertex, t_dvec3 *normal)
{
	t_dvec3	ey;
	t_dvec3	ez;
	t_dvec3	ex;
	t_dvec3	front;
	int		i;

	ey = normal[0];
	i = 1;
	while (dv_dot(normal[i], ey) < 0.4 || dv_dot(normal[i], ey) > 0.5)
		i++;
	front = normal[i];
	ez = dv_unit(dv_sub(front, dv_scale(ey, dv_dot(front, ey))));
	ex = dv_cross(ey, ez);
	i = 0;
	while (i < DODECA_CORNER_COUNT)
	{
		vertex[i] = dv(dv_dot(vertex[i], ex), dv_dot(vertex[i], ey),
				dv_dot(vertex[i], ez));
		i++;
	}
	i = 0;
	while (i < DODECA_FACE_COUNT)
	{
		normal[i] = dv(dv_dot(normal[i], ex), dv_dot(normal[i], ey),
				dv_dot(normal[i], ez));
		i++;
	}
}

/// Sorts key for a face: group 0 top, 1 upper ring, 2 lower ring, 3
/// bottom, then a "diamond angle" in [0, 4) of its horizontal direction
/// measured from the front (+z) towards +x, the right as seen from the
/// front (counter-clockwise seen from above): monotonic in
/// the true angle, computed with no trigonometry. Faces of one ring are
/// at least 72 degrees apart, so no two keys ever tie.
static double	face_sort_key(t_dvec3 normal)
{
	double	horizontal;
	double	c;
	double	s;

	if (normal.y > 0.9)
		return (0.0);
	if (normal.y < -0.9)
		return (30.0);
	horizontal = dsqrt(normal.x * normal.x + normal.z * normal.z);
	c = normal.z / horizontal;
	s = normal.x / horizontal;
	return ((normal.y > 0.0 ? 10.0 : 20.0) + (s >= 0.0 ? 1.0 - c : 3.0 + c));
}

static void	sort_faces(t_dvec3 *normal)
{
	int		i;
	int		j;
	t_dvec3	tmp;

	i = 1;
	while (i < DODECA_FACE_COUNT)
	{
		tmp = normal[i];
		j = i - 1;
		while (j >= 0 && face_sort_key(normal[j]) > face_sort_key(tmp))
		{
			normal[j + 1] = normal[j];
			j--;
		}
		normal[j + 1] = tmp;
		i++;
	}
}

/// A total order on vertices that breaks every symmetry, so corner
/// slots are numbered deterministically: the weights are unrelated to
/// any coordinate ratio of the solid, so no two vertices tie.
# define CORNER_ORDER_WEIGHT_Y 0.7351
# define CORNER_ORDER_WEIGHT_Z 0.5129

static double	corner_key(t_dvec3 v)
{
	return (v.x + CORNER_ORDER_WEIGHT_Y * v.y + CORNER_ORDER_WEIGHT_Z * v.z);
}

static void	sort_corners(t_dvec3 *vertex)
{
	int		i;
	int		j;
	t_dvec3	tmp;

	i = 1;
	while (i < DODECA_CORNER_COUNT)
	{
		tmp = vertex[i];
		j = i - 1;
		while (j >= 0 && corner_key(vertex[j]) > corner_key(tmp))
		{
			vertex[j + 1] = vertex[j];
			j--;
		}
		vertex[j + 1] = tmp;
		i++;
	}
}

/// Two vertices are joined by an edge when their squared distance is
/// below this: the edge squared is (2/phi)^2, about 1.53, the next
/// distance (a face diagonal) squared is 4, and 2.75 is between them.
# define ADJACENT_DISTANCE_SQ 2.75

static int	adjacent(int a, int b)
{
	t_dvec3	d;

	d = dv_sub(g_vertex[a], g_vertex[b]);
	return (a != b && dv_dot(d, d) < ADJACENT_DISTANCE_SQ);
}

/// A vertex lies on face `f` when its height along the face's normal
/// equals the face's inradius (within double rounding, 1e-6).
# define ON_FACE_TOLERANCE 1e-6

/// True if `w` comes after `start` going counter-clockwise round face
/// f seen from outside: the cross product of the two corners' offsets
/// from the face centre points the same way as the face's normal.
static int	is_ccw_from(int f, int start, int w)
{
	t_dvec3	a;
	t_dvec3	b;

	a = dv_sub(g_vertex[start], g_face_centre[f]);
	b = dv_sub(g_vertex[w], g_face_centre[f]);
	return (dv_dot(dv_cross(a, b), g_face_normal[f]) > 0.0);
}

/// The corner after position k - 1 in face f's counter-clockwise list:
/// the neighbour of the previous one that is not the one before it (for
/// k == 1, the neighbour of the start that lies counter-clockwise).
static int	next_face_corner(const uint8_t *on, int f, int k)
{
	const uint8_t	*chain;
	int				v;

	chain = g_face_corner[f];
	v = 0;
	while (v < DODECA_FACE_SIDES)
	{
		if (adjacent(chain[k - 1], on[v])
			&& (k == 1 ? is_ccw_from(f, chain[0], on[v])
				: on[v] != chain[k - 2]))
			return (on[v]);
		v++;
	}
	return (-1);
}

/// Lists face f's five corners counter-clockwise seen from outside,
/// starting at its lowest-numbered corner. Returns 0 if the face does
/// not have exactly five corners or the chain does not close.
static int	order_face_corners(int f)
{
	uint8_t	on[DODECA_FACE_SIDES];
	int		n;
	int		v;
	int		k;
	int		next;

	n = 0;
	v = 0;
	while (v < DODECA_CORNER_COUNT)
	{
		if (dabs(dv_dot(g_vertex[v], g_face_normal[f]) - g_inradius)
			< ON_FACE_TOLERANCE)
		{
			if (n == DODECA_FACE_SIDES)
				return (0);
			on[n++] = (uint8_t)v;
		}
		v++;
	}
	g_face_corner[f][0] = on[0];
	k = 1;
	while (n == DODECA_FACE_SIDES && k < DODECA_FACE_SIDES)
	{
		next = next_face_corner(on, f, k);
		if (next < 0)
			return (0);
		g_face_corner[f][k++] = (uint8_t)next;
	}
	return (n == DODECA_FACE_SIDES);
}

/// Edges are numbered by their corner pair in lexicographic order, so
/// slots are deterministic and independent of any face.
static int	number_edges(void)
{
	int	a;
	int	b;
	int	n;

	n = 0;
	a = 0;
	while (a < DODECA_CORNER_COUNT)
	{
		b = a + 1;
		while (b < DODECA_CORNER_COUNT)
		{
			if (adjacent(a, b) && n < DODECA_EDGE_COUNT)
			{
				g_edge_corner[n][0] = (uint8_t)a;
				g_edge_corner[n][1] = (uint8_t)b;
			}
			n += adjacent(a, b);
			b++;
		}
		a++;
	}
	return (n == DODECA_EDGE_COUNT);
}

static int	face_has_corner(int f, int v)
{
	int	k;

	k = 0;
	while (k < DODECA_FACE_SIDES)
	{
		if (g_face_corner[f][k] == v)
			return (1);
		k++;
	}
	return (0);
}

/// The three faces round corner v, ordered counter-clockwise seen from
/// outside (the order t_sticker's orientation convention needs), the
/// lowest-numbered first.
static void	corner_faces(int v, int *out)
{
	int	n;
	int	f;
	int	tmp;

	n = 0;
	f = 0;
	while (f < DODECA_FACE_COUNT)
	{
		if (face_has_corner(f, v))
			out[n++] = f;
		f++;
	}
	if (dv_dot(dv_cross(g_face_normal[out[0]], g_face_normal[out[1]]),
			g_vertex[v]) <= 0.0)
	{
		tmp = out[1];
		out[1] = out[2];
		out[2] = tmp;
	}
}

static t_sticker	make_sticker(int type, int slot, t_dvec3 pos, int face)
{
	t_sticker	s;

	s.pos = to_fixed_vec(pos);
	s.normal = to_fixed_vec(g_face_normal[face]);
	s.piece_type = (uint8_t)type;
	s.slot = (uint8_t)slot;
	return (s);
}

/// The reference point of a sticker: `reach` of the way from the face's
/// centre towards `target` (a corner, or an edge's midpoint).
static t_dvec3	toward(int face, t_dvec3 target, double reach)
{
	return (dv_add(g_face_centre[face],
			dv_scale(dv_sub(target, g_face_centre[face]), reach)));
}

/// Stickers, type-major then slot-major then by rank, as cut_geometry.h
/// requires: 20 corners x 3, 30 edges x 2, 12 centres. The colour of a
/// sticker is the face it lies on.
static void	build_stickers(void)
{
	int			n;
	int			slot;
	int			k;
	int			faces[3];
	t_dvec3		mid;

	n = 0;
	slot = 0;
	while (slot < DODECA_CORNER_COUNT)
	{
		corner_faces(slot, faces);
		k = 0;
		while (k < 3)
		{
			g_colours[n] = (uint8_t)faces[k];
			g_stickers[n++] = make_sticker(DODECA_CORNER_TYPE, slot,
					toward(faces[k], g_vertex[slot],
						DODECA_CORNER_STICKER_REACH), faces[k]);
			k++;
		}
		slot++;
	}
	slot = 0;
	while (slot < DODECA_EDGE_COUNT)
	{
		mid = dv_scale(dv_add(g_vertex[g_edge_corner[slot][0]],
					g_vertex[g_edge_corner[slot][1]]), 0.5);
		k = 0;
		while (k < DODECA_FACE_COUNT)
		{
			if (face_has_corner(k, g_edge_corner[slot][0])
				&& face_has_corner(k, g_edge_corner[slot][1]))
			{
				g_colours[n] = (uint8_t)k;
				g_stickers[n++] = make_sticker(DODECA_EDGE_TYPE, slot,
						toward(k, mid, DODECA_EDGE_STICKER_REACH), k);
			}
			k++;
		}
		slot++;
	}
	slot = 0;
	while (slot < DODECA_FACE_COUNT)
	{
		g_colours[n] = (uint8_t)slot;
		g_stickers[n++] = make_sticker(DODECA_CENTRE_TYPE, slot,
				g_face_centre[slot], slot);
		slot++;
	}
}

/// Turn f: the plane perpendicular to face f's normal, at
/// DODECA_LAYER_CUT_DEPTH of its inradius. The axis is the normal at
/// fixed scale and the threshold is in the same scale squared (the dot
/// product of two fixed-point vectors), as cut_moving_facelets()
/// compares them.
static void	build_turns(void)
{
	int	f;

	f = 0;
	while (f < DODECA_FACE_COUNT)
	{
		g_turns[f].axis = to_fixed_vec(g_face_normal[f]);
		g_turns[f].threshold = to_fixed(DODECA_LAYER_CUT_DEPTH * g_inradius)
			* DODECA_COORD_SCALE;
		f++;
	}
}

static int	build_geometry(void)
{
	int	f;
	int	v;

	raw_solid(g_vertex, g_face_normal);
	orient_solid(g_vertex, g_face_normal);
	sort_faces(g_face_normal);
	sort_corners(g_vertex);
	g_inradius = 0.0;
	v = 0;
	while (v < DODECA_CORNER_COUNT)
	{
		if (dv_dot(g_vertex[v], g_face_normal[0]) > g_inradius)
			g_inradius = dv_dot(g_vertex[v], g_face_normal[0]);
		v++;
	}
	f = 0;
	while (f < DODECA_FACE_COUNT)
	{
		g_face_centre[f] = dv_scale(g_face_normal[f], g_inradius);
		if (!order_face_corners(f))
			return (0);
		f++;
	}
	if (!number_edges())
		return (0);
	build_stickers();
	build_turns();
	return (cut_fill_facelets_wide(g_facelets, g_stickers,
			DODECA_MEGAMINX_STICKER_COUNT));
}

/// One cached family per size: index 0 Kilominx, 1 Megaminx.
# define FAMILY_VARIANTS 2

static t_generator_table	*g_generators[FAMILY_VARIANTS];
static t_cut_family			g_family[FAMILY_VARIANTS];
static int					g_family_ready[FAMILY_VARIANTS];

static uint32_t	sticker_count_for(uint32_t size)
{
	return (size == DODECA_MEGAMINX_SIZE ? DODECA_MEGAMINX_STICKER_COUNT
		: DODECA_KILOMINX_STICKER_COUNT);
}

const t_cut_family	*dodeca_family(uint32_t size)
{
	uint32_t	v;
	uint32_t	count;

	if (!dodeca_shape(size))
		return (NULL);
	v = size - DODECA_KILOMINX_SIZE;
	if (g_family_ready[v])
		return (&g_family[v]);
	if (!g_geometry_ready)
		g_geometry_ready = build_geometry();
	if (!g_geometry_ready)
		return (NULL);
	count = sticker_count_for(size);
	g_generators[v] = cut_derive_generators_rotated(dodeca_shape(size),
			g_stickers, count, g_turns, DODECA_FACE_COUNT, DODECA_STEPS,
			&DODECA_ROTATION);
	if (!g_generators[v])
		return (NULL);
	g_family[v].shape = dodeca_shape(size);
	g_family[v].generators = g_generators[v];
	g_family[v].stickers = g_stickers;
	g_family[v].sticker_count = count;
	g_family[v].turns = g_turns;
	g_family[v].colours = g_colours;
	g_family[v].facelets = (const uint8_t *)g_facelets;
	g_family[v].facelet_stride = (uint32_t)sizeof(t_cut_facelet_wide);
	g_family_ready[v] = 1;
	return (&g_family[v]);
}
