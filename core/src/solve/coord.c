#include <stdbool.h>
#include "solve/coord.h"

/// Ported from rubik/src/coord/{tables,encode,decode}.c -- see
/// solve/coord.h's own header comment. Kept as one file (rubik split it
/// three ways across encode/decode/tables) since nothing outside this
/// translation unit needs g_binom/g_fact/UD_EDGE_COUNT/SLICE_EDGE_COUNT
/// -- rubik's own coord_tables.h existed only to share them between its
/// encode.c and decode.c; here they're both in the same file already.

# define UD_EDGE_COUNT 8
# define SLICE_EDGE_COUNT 4

/// C(n, k) for n = 0..11, k = 0..4 (used by the slice coordinate).
/// Ported from rubik's g_binom (src/coord/tables.c).
static const uint16_t	g_binom[SOLVE_EDGE_COUNT][5] = {
	{1, 0, 0, 0, 0},
	{1, 1, 0, 0, 0},
	{1, 2, 1, 0, 0},
	{1, 3, 3, 1, 0},
	{1, 4, 6, 4, 1},
	{1, 5, 10, 10, 5},
	{1, 6, 15, 20, 15},
	{1, 7, 21, 35, 35},
	{1, 8, 28, 56, 70},
	{1, 9, 36, 84, 126},
	{1, 10, 45, 120, 210},
	{1, 11, 55, 165, 330}
};

/// Factorials 0! .. 7!. Ported from rubik's g_fact (src/coord/tables.c).
static const uint16_t	g_fact[SOLVE_CORNER_COUNT] = {
	1, 1, 2, 6, 24, 120, 720, 5040
};

uint16_t	encode_twist(const t_solve_cube *cube)
{
	uint16_t	twist;
	int			i;

	twist = 0;
	i = 0;
	while (i < SOLVE_CORNER_COUNT - 1)
	{
		twist = (uint16_t)(twist * 3 + cube->corner_orient[i]);
		i++;
	}
	return (twist);
}

void	decode_twist(t_solve_cube *cube, uint16_t twist)
{
	int	sum;
	int	i;

	sum = 0;
	i = SOLVE_CORNER_COUNT - 2;
	while (i >= 0)
	{
		cube->corner_orient[i] = (uint8_t)(twist % 3);
		sum += twist % 3;
		twist = (uint16_t)(twist / 3);
		i--;
	}
	cube->corner_orient[SOLVE_CORNER_COUNT - 1] = (uint8_t)((3 - sum % 3) % 3);
}

uint16_t	encode_flip(const t_solve_cube *cube)
{
	uint16_t	flip;
	int			i;

	flip = 0;
	i = 0;
	while (i < SOLVE_EDGE_COUNT - 1)
	{
		flip = (uint16_t)(flip * 2 + cube->edge_orient[i]);
		i++;
	}
	return (flip);
}

void	decode_flip(t_solve_cube *cube, uint16_t flip)
{
	int	sum;
	int	i;

	sum = 0;
	i = SOLVE_EDGE_COUNT - 2;
	while (i >= 0)
	{
		cube->edge_orient[i] = (uint8_t)(flip % 2);
		sum += flip % 2;
		flip = (uint16_t)(flip / 2);
		i--;
	}
	cube->edge_orient[SOLVE_EDGE_COUNT - 1] = (uint8_t)(sum % 2);
}

uint16_t	encode_slice(const t_solve_cube *cube)
{
	uint16_t	slice;
	int			found;
	int			slot;

	slice = 0;
	found = 0;
	slot = SOLVE_EDGE_COUNT - 1;
	while (slot >= 0)
	{
		if (cube->edge_perm[slot] >= SOLVE_EDGE_FR && found < 4)
		{
			found++;
			slice = (uint16_t)(slice
					+ g_binom[SOLVE_EDGE_COUNT - 1 - slot][found]);
		}
		slot--;
	}
	return (slice);
}

void	decode_slice(t_solve_cube *cube, uint16_t slice)
{
	bool	marked[SOLVE_EDGE_COUNT];
	int		next_slice;
	int		next_other;
	int		d;
	int		k;

	d = 0;
	while (d < SOLVE_EDGE_COUNT)
		marked[d++] = false;
	d = SOLVE_EDGE_COUNT - 1;
	k = 4;
	while (k >= 1)
	{
		while (g_binom[d][k] > slice)
			d--;
		marked[SOLVE_EDGE_COUNT - 1 - d] = true;
		slice = (uint16_t)(slice - g_binom[d][k]);
		d--;
		k--;
	}
	next_slice = SOLVE_EDGE_FR;
	next_other = SOLVE_EDGE_UR;
	d = 0;
	while (d < SOLVE_EDGE_COUNT)
	{
		if (marked[d])
			cube->edge_perm[d] = (uint8_t)next_slice++;
		else
			cube->edge_perm[d] = (uint8_t)next_other++;
		d++;
	}
}

/// Ported from rubik's lehmer_encode() (src/coord/encode.c) -- the Lehmer
/// code of an arrangement, shared by cperm/eperm/sperm.
static int	lehmer_encode(const uint8_t *pieces, int n)
{
	int	value;
	int	smaller;
	int	slot;
	int	later;

	value = 0;
	slot = 0;
	while (slot < n)
	{
		smaller = 0;
		later = slot + 1;
		while (later < n)
		{
			if (pieces[later] < pieces[slot])
				smaller++;
			later++;
		}
		value += smaller * g_fact[n - 1 - slot];
		slot++;
	}
	return (value);
}

/// Ported from rubik's lehmer_decode() (src/coord/decode.c).
static void	lehmer_decode(uint8_t *pieces, int n, int value)
{
	uint8_t	pool[SOLVE_CORNER_COUNT];
	int		left;
	int		slot;
	int		pick;
	int		i;

	i = 0;
	while (i < n)
	{
		pool[i] = (uint8_t)i;
		i++;
	}
	left = n;
	slot = 0;
	while (slot < n)
	{
		pick = value / g_fact[n - 1 - slot];
		value = value % g_fact[n - 1 - slot];
		pieces[slot] = pool[pick];
		i = pick;
		while (i < left - 1)
		{
			pool[i] = pool[i + 1];
			i++;
		}
		left--;
		slot++;
	}
}

uint16_t	encode_cperm(const t_solve_cube *cube)
{
	return ((uint16_t)lehmer_encode(cube->corner_perm, SOLVE_CORNER_COUNT));
}

void	decode_cperm(t_solve_cube *cube, uint16_t cperm)
{
	lehmer_decode(cube->corner_perm, SOLVE_CORNER_COUNT,
		cperm % CPERM_COUNT);
}

uint16_t	encode_eperm(const t_solve_cube *cube)
{
	return ((uint16_t)lehmer_encode(cube->edge_perm, UD_EDGE_COUNT));
}

void	decode_eperm(t_solve_cube *cube, uint16_t eperm)
{
	lehmer_decode(cube->edge_perm, UD_EDGE_COUNT, eperm % EPERM_COUNT);
}

uint16_t	encode_sperm(const t_solve_cube *cube)
{
	return ((uint16_t)lehmer_encode(cube->edge_perm + UD_EDGE_COUNT,
			SLICE_EDGE_COUNT));
}

void	decode_sperm(t_solve_cube *cube, uint16_t sperm)
{
	uint8_t	pieces[SLICE_EDGE_COUNT];
	int		i;

	lehmer_decode(pieces, SLICE_EDGE_COUNT, sperm % SPERM_COUNT);
	i = 0;
	while (i < SLICE_EDGE_COUNT)
	{
		cube->edge_perm[UD_EDGE_COUNT + i] = (uint8_t)(SOLVE_EDGE_FR
				+ pieces[i]);
		i++;
	}
}
