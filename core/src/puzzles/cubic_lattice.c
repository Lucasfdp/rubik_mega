#include "puzzles/cubic_lattice.h"
#include "platform.h"

/// True if coordinate `v` sits at either end of the size-n axis.
static int	is_extreme(uint32_t v, uint32_t n)
{
	return (v == 0 || v == n - 1);
}

uint32_t	cubic_wing_count(uint32_t n)
{
	return (12 * (n - 2));
}

uint32_t	cubic_centre_count(uint32_t n)
{
	return (6 * (n - 2) * (n - 2));
}

uint32_t	cubic_max_depth(uint32_t n)
{
	return ((n - 1) / 2);
}

/// @brief Fills `coords` with every wing (want_wing true) or centre
///        (want_wing false) position on the whole NxNxN lattice, in a
///        fixed canonical order (x outer, y middle, z inner). This
///        order is arbitrary -- it only has to be the SAME order every
///        call, since a point's position in this array IS its slot
///        index (nothing else assigns slot numbers; cubic.c never looks
///        a wing or centre up any other way). Caller sizes `coords` to
///        cubic_wing_count(n) / cubic_centre_count(n).
static uint32_t	enumerate(uint32_t n, int want_wing, t_cubic_point *coords)
{
	uint32_t	count;
	int			x;
	int			y;
	int			z;
	int			extreme;

	count = 0;
	x = 0;
	while (x < (int)n)
	{
		y = 0;
		while (y < (int)n)
		{
			z = 0;
			while (z < (int)n)
			{
				extreme = is_extreme((uint32_t)x, n) + is_extreme((uint32_t)y, n)
					+ is_extreme((uint32_t)z, n);
				if ((want_wing && extreme == 2) || (!want_wing && extreme == 1))
				{
					coords[count].x = x;
					coords[count].y = y;
					coords[count].z = z;
					count++;
				}
				z++;
			}
			y++;
		}
		x++;
	}
	return (count);
}

static int	same_point(t_cubic_point a, t_cubic_point b)
{
	return (a.x == b.x && a.y == b.y && a.z == b.z);
}

/// @brief One 90-degree rotation step about `axis`, in the direction
///        that is clockwise when viewed from the axis's HIGH end (the
///        U, R or F face). The direction is not arbitrary: cubic.c's
///        hand-written corner cycles turn each face clockwise as seen
///        from that face, so the wing/centre cycles built here must
///        turn the same way or one generator would move corners and
///        wings in opposite directions (DECISIONS.md, 2026-10-04,
///        "NxN rotation direction"). rotate_quarter() picks the right
///        direction per face; call that, not this, when building cycles.
static t_cubic_point	rotate90(t_cubic_point p, t_cubic_axis axis, uint32_t n)
{
	int	m;

	m = (int)n - 1;
	if (axis == CUBIC_AXIS_X)
		return ((t_cubic_point){p.x, p.z, m - p.y});
	if (axis == CUBIC_AXIS_Y)
		return ((t_cubic_point){m - p.z, p.y, p.x});
	return ((t_cubic_point){p.y, m - p.x, p.z});
}

/// @brief One clockwise quarter-turn as seen from the face at `sign`'s
///        end of `axis`. rotate90() is already clockwise from the high
///        end (sign +1); a low-end face (D, L, B -- sign -1) looks at
///        the same axis from the opposite side, so its clockwise is
///        rotate90()'s inverse -- three applications of it.
static t_cubic_point	rotate_quarter(t_cubic_point p, t_cubic_axis axis,
	int sign, uint32_t n)
{
	p = rotate90(p, axis, n);
	if (sign < 0)
		p = rotate90(rotate90(p, axis, n), axis, n);
	return (p);
}

/// @brief The lattice point one step outside wing slot `p`, across its
///        PRIMARY sticker: the U/D sticker if the slot touches U or D,
///        otherwise its F/B sticker. This is rubik's own 3x3x3 edge
///        orientation convention (an edge is "good" when its U/D -- or,
///        for a middle-layer edge, F/B -- sticker sits on the U/D/F/B
///        facelet of its slot), applied unchanged to every wing.
///        Representing a sticker as "the point just outside the cube
///        that it faces" means rotate_quarter() moves stickers exactly
///        the way it moves pieces, with no separate normal-vector maths.
static t_cubic_point	primary_facelet(t_cubic_point p, uint32_t n)
{
	int	m;
	int	outside_low;
	int	outside_high;

	m = (int)n - 1;
	outside_low = -1;
	outside_high = m + 1;
	if (p.y == 0 || p.y == m)
	{
		if (p.y == 0)
			p.y = outside_low;
		else
			p.y = outside_high;
		return (p);
	}
	if (p.z == 0)
		p.z = outside_low;
	else
		p.z = outside_high;
	return (p);
}

/// @brief Orientation change for the wing that lands in `to` after one
///        quarter-turn from `from`: 0 if its primary sticker lands on
///        `to`'s primary facelet, 1 if it lands on the other one.
///        Derived from geometry per cycle step, replacing the old
///        per-axis "F/B flips, nothing else does" rule, which was only
///        right for depth-0 face turns -- an inner slice on the R/L
///        axis (an M-style slice) carries wings between U/D and F/B
///        exactly like a 3x3x3's M move flips edges, which that rule
///        never did (DECISIONS.md, 2026-10-04, "Wing orientation").
static int8_t	wing_delta(t_cubic_point from, t_cubic_point to,
	t_cubic_axis axis, int sign, uint32_t n)
{
	t_cubic_point	moved;

	moved = rotate_quarter(primary_facelet(from, n), axis, sign, n);
	return ((int8_t)!same_point(moved, primary_facelet(to, n)));
}

static int	axis_value(t_cubic_point p, t_cubic_axis axis)
{
	if (axis == CUBIC_AXIS_X)
		return (p.x);
	if (axis == CUBIC_AXIS_Y)
		return (p.y);
	return (p.z);
}

static uint32_t	find_index(const t_cubic_point *coords, uint32_t count,
	t_cubic_point p)
{
	uint32_t	i;

	i = 0;
	while (i < count)
	{
		if (same_point(coords[i], p))
			return (i);
		i++;
	}
	return (count);   // never reached if the lattice geometry is sound
}

/// @brief depth 0 is the face's own extreme coordinate (n - 1 for
///        sign +1, 0 for sign -1); depth d > 0 moves inward one cell
///        per step, regardless of sign.
static int	depth_coord(uint32_t n, int sign, uint32_t depth)
{
	if (sign > 0)
		return ((int)n - 1 - (int)depth);
	return ((int)depth);
}

/// @brief Groups every wing-or-centre point at this layer into its
///        4-cycle under rotate_quarter(), or drops it silently if the
///        rotation fixes it in place (the lattice's exact centre cell on an odd
///        (n - 2) grid -- proved in docs/en/05-phase1-nxn.md's
///        implementation notes to never produce an orbit of length 2 or
///        3, only 1 or 4, so there is nothing between "skip" and "emit
///        a full length-4 cycle" to handle).
static uint32_t	build_cycles(uint32_t n, t_cubic_axis axis, int sign,
	int target, int want_wing, uint8_t piece_type, t_cycle *out)
{
	uint32_t		count;
	t_cubic_point	*coords;
	uint8_t			*visited;
	uint32_t		cycle_count;
	uint32_t		i;
	uint32_t		step;
	t_cubic_point	cur;
	t_cubic_point	prev;
	uint32_t		idx;

	if (want_wing)
		count = cubic_wing_count(n);
	else
		count = cubic_centre_count(n);
	coords = malloc(sizeof(*coords) * count);
	visited = calloc(count, 1);
	enumerate(n, want_wing, coords);
	cycle_count = 0;
	i = 0;
	while (i < count)
	{
		if (!visited[i] && axis_value(coords[i], axis) == target)
		{
			if (same_point(rotate_quarter(coords[i], axis, sign, n), coords[i]))
				visited[i] = 1;
			else
			{
				out[cycle_count].piece_type = piece_type;
				out[cycle_count].length = 4;
				cur = coords[i];
				prev = rotate_quarter(rotate_quarter(rotate_quarter(cur,
								axis, sign, n), axis, sign, n), axis, sign, n);
				step = 0;
				while (step < 4)
				{
					idx = find_index(coords, count, cur);
					visited[idx] = 1;
					out[cycle_count].slots[step] = (uint8_t)idx;
					out[cycle_count].delta[step] = 0;
					if (want_wing)
						out[cycle_count].delta[step]
							= wing_delta(prev, cur, axis, sign, n);
					prev = cur;
					cur = rotate_quarter(cur, axis, sign, n);
					step++;
				}
				cycle_count++;
			}
		}
		i++;
	}
	free(coords);
	free(visited);
	return (cycle_count);
}

t_cubic_point	cubic_lattice_point(uint32_t n, int wing, uint32_t index)
{
	t_cubic_point	*coords;
	t_cubic_point	point;
	uint32_t		count;

	if (wing)
		count = cubic_wing_count(n);
	else
		count = cubic_centre_count(n);
	coords = malloc(sizeof(*coords) * count);
	enumerate(n, wing, coords);
	point = coords[index];
	free(coords);
	return (point);
}

void	cubic_lattice_build_layer(uint32_t n, t_cubic_axis axis, int sign,
	uint32_t depth, uint8_t wing_piece_type, uint8_t centre_piece_type,
	t_cycle *wing_cycles, uint32_t *wing_cycle_count,
	t_cycle *centre_cycles, uint32_t *centre_cycle_count)
{
	int	target;

	target = depth_coord(n, sign, depth);
	*wing_cycle_count = build_cycles(n, axis, sign, target, 1,
			wing_piece_type, wing_cycles);
	*centre_cycle_count = build_cycles(n, axis, sign, target, 0,
			centre_piece_type, centre_cycles);
}
