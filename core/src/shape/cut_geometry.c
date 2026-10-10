#include "platform.h"
#include "cut_geometry.h"

int32_t	ivec3_dot(t_ivec3 a, t_ivec3 b)
{
	return (a.x * b.x + a.y * b.y + a.z * b.z);
}

static t_ivec3	ivec3_cross(t_ivec3 a, t_ivec3 b)
{
	t_ivec3	c;

	c.x = a.y * b.z - a.z * b.y;
	c.y = a.z * b.x - a.x * b.z;
	c.z = a.x * b.y - a.y * b.x;
	return (c);
}

/// Rodrigues' formula at 120 degrees. cos = -1/2 and sin = sqrt(3)/2,
/// and for an axis n with |n|^2 == 3 the sqrt(3) in sin cancels against
/// the one in the unit axis, leaving R = (-I + K + n n^T) / 2 with K the
/// cross-product matrix of n: every entry an integer over 2, and every
/// product R*p an integer for any integer p whose coordinates the
/// puzzle chose to be compatible (checked by the stickers landing on
/// stickers, in cut_derive_generators()). That is a counter-clockwise
/// rotation about +n seen from n's tip; a clockwise turn is the same
/// rotation about -n, so n is negated up front.
t_ivec3	ivec3_turn(t_ivec3 axis, t_ivec3 p)
{
	t_ivec3	n;
	t_ivec3	k;
	int32_t	d;
	t_ivec3	r;

	n.x = -axis.x;
	n.y = -axis.y;
	n.z = -axis.z;
	k = ivec3_cross(n, p);
	d = ivec3_dot(n, p);
	r.x = (-p.x + k.x + n.x * d) / 2;
	r.y = (-p.y + k.y + n.y * d) / 2;
	r.z = (-p.z + k.z + n.z * d) / 2;
	return (r);
}

/// Skewb's and Pyraminx's rotation: exact, so a match must be exact
/// (tolerance 0) and merely unique (separation above 0).
const t_cut_rotation	CUT_ROTATION_EXACT_120 = { ivec3_turn, 0, 0 };

void	cut_order_ccw(t_sticker *s, t_ivec3 outward)
{
	t_sticker	tmp;

	if (ivec3_dot(ivec3_cross(s[0].normal, s[1].normal), outward) > 0)
		return ;
	tmp = s[1];
	s[1] = s[2];
	s[2] = tmp;
}

/// Combined squared distance between two stickers' positions plus
/// squared distance between their normals. Widened to 64 bits: a
/// Megaminx coordinate is about 7000, so a squared difference is
/// already 5e7 and three of them, twice, would approach the int32 edge.
static int64_t	sticker_distance_sq(t_ivec3 pos_a, t_ivec3 normal_a,
					t_ivec3 pos_b, t_ivec3 normal_b)
{
	int64_t	dx;
	int64_t	dy;
	int64_t	dz;

	dx = (int64_t)pos_a.x - pos_b.x;
	dy = (int64_t)pos_a.y - pos_b.y;
	dz = (int64_t)pos_a.z - pos_b.z;
	return (dx * dx + dy * dy + dz * dz + ((int64_t)normal_a.x - normal_b.x)
		* ((int64_t)normal_a.x - normal_b.x) + ((int64_t)normal_a.y
			- normal_b.y) * ((int64_t)normal_a.y - normal_b.y)
		+ ((int64_t)normal_a.z - normal_b.z) * ((int64_t)normal_a.z
			- normal_b.z));
}

/// Index of the sticker that (pos, normal) lands on, or -1 unless the
/// match is clean: the nearest sticker within `rot->tolerance_sq` and
/// every other one strictly further than `rot->separation_sq`. A
/// linear scan: there are at most a few hundred stickers and this runs
/// once per puzzle family per process. For the exact path both limits
/// are 0, so it reduces to "exactly one sticker is exactly here".
static int	find_sticker(const t_sticker *st, uint32_t count, t_ivec3 pos,
				t_ivec3 normal, const t_cut_rotation *rot)
{
	uint32_t	i;
	int			best;
	int64_t		best_d;
	int64_t		second_d;
	int64_t		d;

	best = -1;
	best_d = INT64_MAX;
	second_d = INT64_MAX;
	i = 0;
	while (i < count)
	{
		d = sticker_distance_sq(pos, normal, st[i].pos, st[i].normal);
		if (d < best_d)
		{
			second_d = best_d;
			best_d = d;
			best = (int)i;
		}
		else if (d < second_d)
			second_d = d;
		i++;
	}
	if (best < 0 || best_d > rot->tolerance_sq
		|| second_d <= rot->separation_sq)
		return (-1);
	return (best);
}

/// Where one quarter-step of `turn` sends each sticker, and how each
/// slot's pieces end up: `dest_slot[t][s]` is the slot the piece in slot
/// s of type t lands in (== s if it stays), `delta[t][s]` its
/// orientation change. SLOT_MAX bounds both: a Megaminx's 30 edges are
/// the most slots one type has; larger puzzles are not a target of
/// this module.
# define CUT_SLOT_MAX 32

typedef struct s_slot_map
{
	int8_t	dest[MAX_PIECE_TYPES][CUT_SLOT_MAX];
	int8_t	delta[MAX_PIECE_TYPES][CUT_SLOT_MAX];
}	t_slot_map;

/// Position of sticker `idx` among its own slot's stickers (0-based),
/// counted in array order.
static int	rank_in_slot(const t_sticker *st, int idx)
{
	int	i;
	int	rank;

	rank = 0;
	i = 0;
	while (i < idx)
	{
		if (st[i].piece_type == st[idx].piece_type
			&& st[i].slot == st[idx].slot)
			rank++;
		i++;
	}
	return (rank);
}

static void	slot_map_clear(t_slot_map *map)
{
	int	t;
	int	s;

	t = 0;
	while (t < MAX_PIECE_TYPES)
	{
		s = 0;
		while (s < CUT_SLOT_MAX)
		{
			map->dest[t][s] = -1;
			map->delta[t][s] = 0;
			s++;
		}
		t++;
	}
}

/// Records where sticker `i` goes. Fails (0) if the sticker lands on no
/// sticker, changes piece type, or disagrees with an earlier sticker of
/// the same piece about the destination slot or the rotation amount.
static int	record_sticker(const t_shape *shape, const t_sticker *st,
				uint32_t count, const t_cut_turn *turn, int i,
				t_slot_map *map, const t_cut_rotation *rot)
{
	t_ivec3	pos;
	t_ivec3	normal;
	int		j;
	int		m;
	int		delta;

	pos = st[i].pos;
	normal = st[i].normal;
	if (ivec3_dot(turn->axis, pos) > turn->threshold)
	{
		pos = rot->turn(turn->axis, pos);
		normal = rot->turn(turn->axis, normal);
	}
	j = find_sticker(st, count, pos, normal, rot);
	if (j < 0 || st[j].piece_type != st[i].piece_type
		|| st[i].slot >= CUT_SLOT_MAX)
		return (0);
	m = (int)shape->piece_types[st[i].piece_type].orientations;
	delta = ((rank_in_slot(st, j) - rank_in_slot(st, i)) % m + m) % m;
	if (map->dest[st[i].piece_type][st[i].slot] < 0)
	{
		map->dest[st[i].piece_type][st[i].slot] = (int8_t)st[j].slot;
		map->delta[st[i].piece_type][st[i].slot] = (int8_t)delta;
		return (1);
	}
	return (map->dest[st[i].piece_type][st[i].slot] == (int8_t)st[j].slot
		&& map->delta[st[i].piece_type][st[i].slot] == (int8_t)delta);
}

/// Every slot of every type must have exactly `orientations` stickers --
/// otherwise "orientation" has no meaning for it.
static int	check_sticker_counts(const t_shape *shape, const t_sticker *st,
				uint32_t count)
{
	uint32_t	t;
	uint32_t	s;
	uint32_t	i;
	uint32_t	n;

	t = 0;
	while (t < shape->piece_type_count)
	{
		if (shape->piece_types[t].count > CUT_SLOT_MAX)
			return (0);
		s = 0;
		while (s < shape->piece_types[t].count)
		{
			n = 0;
			i = 0;
			while (i < count)
			{
				if (st[i].piece_type == t && st[i].slot == s)
					n++;
				i++;
			}
			if (n != shape->piece_types[t].orientations)
				return (0);
			s++;
		}
		t++;
	}
	return (1);
}

static uint32_t	count_cycles(const t_shape *shape, const t_slot_map *map)
{
	uint32_t	t;
	uint32_t	s;
	uint32_t	total;

	total = 0;
	t = 0;
	while (t < shape->piece_type_count)
	{
		s = 0;
		while (s < shape->piece_types[t].count)
		{
			if (map->dest[t][s] != (int8_t)s || map->delta[t][s] != 0)
				total++;
			s++;
		}
		t++;
	}
	return (total);
}

/// Walks the slot permutation of one type into t_cycle records. Each
/// slot belongs to exactly one chain; a slot that stays put with a
/// non-zero delta is a length-1 cycle (a twist in place). `seen` stops
/// a chain being emitted once per member. Returns cycles written, or -1
/// if a chain is longer than MAX_CYCLE_LENGTH.
static int	emit_type_cycles(uint32_t type, uint32_t slot_count,
				const t_slot_map *map, t_cycle *out)
{
	uint8_t		seen[CUT_SLOT_MAX];
	uint32_t	s;
	uint32_t	cur;
	int			written;
	t_cycle		*c;

	written = 0;
	s = 0;
	while (s < CUT_SLOT_MAX)
		seen[s++] = 0;
	s = 0;
	while (s < slot_count)
	{
		if (!seen[s] && (map->dest[type][s] != (int8_t)s
				|| map->delta[type][s] != 0))
		{
			c = &out[written++];
			c->piece_type = (uint8_t)type;
			c->length = 0;
			cur = s;
			while (!seen[cur])
			{
				seen[cur] = 1;
				if (c->length >= MAX_CYCLE_LENGTH)
					return (-1);
				c->slots[c->length] = (uint8_t)cur;
				c->length++;
				cur = (uint32_t)map->dest[type][cur];
			}
		}
		s++;
	}
	return (written);
}

/// shape_apply_cycle()'s delta[i] is added to the piece LANDING in
/// slots[i], i.e. the one that came from slots[i - 1] -- so each
/// cycle's deltas are the previous slot's delta, wrapping at 0.
static void	fill_deltas(uint32_t type, const t_slot_map *map, t_cycle *c)
{
	uint32_t	i;
	uint32_t	prev;

	i = 0;
	while (i < MAX_CYCLE_LENGTH)
		c->delta[i++] = 0;
	i = 0;
	while (i < c->length)
	{
		prev = (i + c->length - 1) % c->length;
		c->delta[i] = map->delta[type][c->slots[prev]];
		i++;
	}
}

static int	derive_one(const t_shape *shape, const t_sticker *st,
				uint32_t count, const t_cut_turn *turn, t_generator *gen,
				const t_cut_rotation *rot)
{
	t_slot_map	map;
	uint32_t	i;
	uint32_t	t;
	int			n;
	t_cycle		*cycles;

	slot_map_clear(&map);
	i = 0;
	while (i < count)
		if (!record_sticker(shape, st, count, turn, (int)i++, &map, rot))
			return (0);
	cycles = malloc(count_cycles(shape, &map) * sizeof(t_cycle));
	if (!cycles)
		return (0);
	gen->cycle_count = 0;
	t = 0;
	while (t < shape->piece_type_count)
	{
		n = emit_type_cycles(t, shape->piece_types[t].count, &map,
				&cycles[gen->cycle_count]);
		if (n < 0)
			return (0);
		i = 0;
		while (i < (uint32_t)n)
			fill_deltas(t, &map, &cycles[gen->cycle_count + i++]);
		gen->cycle_count += (uint32_t)n;
		t++;
	}
	gen->cycles = cycles;
	return (1);
}

t_generator_table	*cut_derive_generators(const t_shape *shape,
						const t_sticker *stickers, uint32_t sticker_count,
						const t_cut_turn *turns, uint32_t turn_count,
						uint32_t steps)
{
	return (cut_derive_generators_rotated(shape, stickers, sticker_count,
			turns, turn_count, steps, &CUT_ROTATION_EXACT_120));
}

t_generator_table	*cut_derive_generators_rotated(const t_shape *shape,
						const t_sticker *stickers, uint32_t sticker_count,
						const t_cut_turn *turns, uint32_t turn_count,
						uint32_t steps, const t_cut_rotation *rotation)
{
	t_generator_table	*table;
	t_generator			*gens;
	uint32_t			i;

	if (!shape_is_valid(shape)
		|| !check_sticker_counts(shape, stickers, sticker_count))
		return (NULL);
	table = malloc(sizeof(t_generator_table));
	gens = malloc(turn_count * sizeof(t_generator));
	if (!table || !gens)
		return (NULL);
	i = 0;
	while (i < turn_count)
	{
		if (!derive_one(shape, stickers, sticker_count, &turns[i], &gens[i],
				rotation))
			return (NULL);
		i++;
	}
	table->count = turn_count;
	table->steps = steps;
	table->generators = gens;
	return (table);
}

void	cut_fill_facelets(t_cut_facelet *out, const t_sticker *st,
			uint32_t count)
{
	uint32_t	i;

	i = 0;
	while (i < count)
	{
		out[i].pos[0] = (int8_t)st[i].pos.x;
		out[i].pos[1] = (int8_t)st[i].pos.y;
		out[i].pos[2] = (int8_t)st[i].pos.z;
		out[i].normal[0] = (int8_t)st[i].normal.x;
		out[i].normal[1] = (int8_t)st[i].normal.y;
		out[i].normal[2] = (int8_t)st[i].normal.z;
		out[i].piece_type = st[i].piece_type;
		out[i].slot = st[i].slot;
		i++;
	}
}

int	cut_fill_facelets_wide(t_cut_facelet_wide *out, const t_sticker *st,
		uint32_t count)
{
	uint32_t		i;
	uint32_t		k;
	const int32_t	*pos;
	const int32_t	*normal;

	i = 0;
	while (i < count)
	{
		pos = &st[i].pos.x;
		normal = &st[i].normal.x;
		k = 0;
		while (k < 3)
		{
			if (pos[k] < INT16_MIN || pos[k] > INT16_MAX
				|| normal[k] < INT16_MIN || normal[k] > INT16_MAX)
				return (0);
			out[i].pos[k] = (int16_t)pos[k];
			out[i].normal[k] = (int16_t)normal[k];
			k++;
		}
		out[i].piece_type = st[i].piece_type;
		out[i].slot = st[i].slot;
		i++;
	}
	return (1);
}

uint32_t	cut_sticker_index(const t_shape *shape, uint32_t type,
				uint32_t slot, uint32_t rank)
{
	uint32_t	t;
	uint32_t	index;

	index = 0;
	t = 0;
	while (t < type)
	{
		index += shape->piece_types[t].count * shape->piece_types[t].orientations;
		t++;
	}
	return (index + slot * shape->piece_types[type].orientations + rank);
}

void	cut_paint(const t_cut_family *family, const uint8_t *state,
			uint8_t *out)
{
	const t_shape	*shape;
	uint32_t		t;
	uint32_t		s;
	uint32_t		r;
	uint32_t		perm_off;
	uint32_t		orient_off;
	uint32_t		m;

	shape = family->shape;
	t = 0;
	while (t < shape->piece_type_count)
	{
		shape_piece_offsets(shape, t, &perm_off, &orient_off);
		m = shape->piece_types[t].orientations;
		s = 0;
		while (s < shape->piece_types[t].count)
		{
			r = 0;
			while (r < m)
			{
				out[cut_sticker_index(shape, t, s, r)] = family->colours[
					cut_sticker_index(shape, t, state[perm_off + s],
						(r + m - state[orient_off + s]) % m)];
				r++;
			}
			s++;
		}
		t++;
	}
}

uint32_t	cut_moving_facelets(const t_cut_family *family, uint32_t gen,
				uint8_t *out, uint32_t max)
{
	uint32_t	i;
	uint32_t	n;

	if (gen >= family->generators->count)
		return (0xFFFFFFFFu);
	n = 0;
	i = 0;
	while (i < family->sticker_count)
	{
		if (ivec3_dot(family->turns[gen].axis, family->stickers[i].pos)
			> family->turns[gen].threshold)
		{
			if (n < max)
				out[n] = (uint8_t)i;
			n++;
		}
		i++;
	}
	return (n);
}
