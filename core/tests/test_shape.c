#include <assert.h>
#include <stdio.h>
#include "shape.h"
#include "puzzles/cubic.h"

/// Minimal smoke test for the shape module -- scaffolding-era coverage,
/// expanded as docs/en/04-roadmap.md's phases land.
int	main(void)
{
	uint8_t	state[40];

	assert(shape_is_valid(&CUBIC_3X3X3_SHAPE));
	assert(shape_total_pieces(&CUBIC_3X3X3_SHAPE) == 20);
	assert(shape_state_size(&CUBIC_3X3X3_SHAPE) == 40);
	shape_init_solved(&CUBIC_3X3X3_SHAPE, state);
	assert(state[0] == 0 && state[7] == 7);     // corner perm: identity
	assert(state[16] == 0 && state[19] == 3);   // edge perm starts at offset 16
	assert(state[8] == 0 && state[28] == 0);    // orientations all 0
	printf("test_shape: all assertions passed\n");
	return (0);
}
