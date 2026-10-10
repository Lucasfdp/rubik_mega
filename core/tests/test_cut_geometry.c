#include <assert.h>
#include <stdio.h>
#include "cut_geometry.h"
#include "puzzles/skewb.h"

/// Tests for the generic cut-plane geometry (cut_geometry.h) in
/// isolation, before any puzzle relies on it.

static int	same(t_ivec3 a, t_ivec3 b)
{
	return (a.x == b.x && a.y == b.y && a.z == b.z);
}

/// A clockwise turn seen from the tip of (1,1,1) sends x to z: looking
/// from (1,1,1) at the origin, x, y, z appear counter-clockwise in that
/// order, so clockwise is x -> z -> y -> x.
static void	test_turn_direction(void)
{
	t_ivec3	axis = { 1, 1, 1 };
	t_ivec3	x = { 1, 0, 0 };
	t_ivec3	y = { 0, 1, 0 };
	t_ivec3	z = { 0, 0, 1 };
	t_ivec3	p;

	assert(same(ivec3_turn(axis, x), z));
	assert(same(ivec3_turn(axis, z), y));
	assert(same(ivec3_turn(axis, y), x));
	p = ivec3_turn(axis, ivec3_turn(axis, ivec3_turn(axis, x)));
	assert(same(p, x));
}

/// The axis itself is fixed by its own turn, for every sign pattern.
static void	test_axis_fixed(void)
{
	uint32_t	c;
	t_ivec3		v;

	c = 0;
	while (c < SKEWB_CORNER_COUNT)
	{
		v.x = (c & SKEWB_CORNER_X_BIT) ? -1 : 1;
		v.y = (c & SKEWB_CORNER_Y_BIT) ? -1 : 1;
		v.z = (c & SKEWB_CORNER_Z_BIT) ? -1 : 1;
		assert(same(ivec3_turn(v, v), v));
		c++;
	}
}

/// An axis of the wrong length does not map stickers onto stickers, so
/// derivation must refuse rather than build nonsense.
static void	test_refuses_bad_axis(void)
{
	t_cut_turn	bad;

	bad.axis.x = 1;
	bad.axis.y = 0;
	bad.axis.z = 0;
	bad.threshold = 0;
	assert(cut_derive_generators(skewb_shape(), skewb_stickers(),
			SKEWB_STICKER_COUNT, &bad, 1, SKEWB_STEPS) == NULL);
}

int	main(void)
{
	test_turn_direction();
	test_axis_fixed();
	test_refuses_bad_axis();
	printf("test_cut_geometry: all assertions passed\n");
	return (0);
}
