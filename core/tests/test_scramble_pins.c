#include <assert.h>
#include <stdio.h>
#include "puzzle.h"

/// A seed in a shared link has to mean ONE scramble, on every build of core
/// (docs/en/13-phase6-polish.md D10). These pin the state a fixed seed
/// leaves each family in, as a hash, so a change to the PRNG, to the order
/// a scramble draws its numbers in, or to a family's turns fails here
/// instead of silently breaking every link already shared. The same table
/// is checked against the compiled WASM module in
/// web/tests/engine/scramble-pins.test.ts.
///
/// To update after a DELIBERATE change: print the hashes (run this test
/// with PIN_PRINT defined), paste them here and in the web test, and say
/// in DECISIONS.md that old shared scramble links now open differently.

#define PIN_SEED 12345u

typedef struct s_pin
{
	t_puzzle_type	type;
	uint32_t		size;
	uint32_t		moves;
	const char		*name;
	uint32_t		hash;
}	t_pin;

/// FNV-1a over the state bytes.
static uint32_t	fnv1a(const uint8_t *bytes, uint32_t count)
{
	uint32_t	hash = 2166136261u;

	while (count--)
	{
		hash ^= *bytes++;
		hash *= 16777619u;
	}
	return (hash);
}

static const t_pin	PINS[] = {
	{PUZZLE_CUBIC, 3, 25, "cube 3", 0xc69836dcu},
	{PUZZLE_CUBIC, 4, 40, "cube 4", 0x04490edbu},
	{PUZZLE_CUBIC, 8, 120, "cube 8", 0x76b93417u},
	{PUZZLE_SKEWB, 0, 25, "skewb", 0xf6e19702u},
	{PUZZLE_TETRAHEDRAL, 0, 25, "pyraminx", 0x38ed35c6u},
	{PUZZLE_DODECAHEDRAL, 2, 40, "kilominx", 0x6219ca27u},
	{PUZZLE_DODECAHEDRAL, 3, 70, "megaminx", 0x40151d5cu},
	{PUZZLE_SQUARE1, 0, 25, "square-1", 0x94fa06cau},
};

int	main(void)
{
	uint32_t	i;
	uint32_t	hash;
	puzzle_t	*p;

	for (i = 0; i < sizeof(PINS) / sizeof(PINS[0]); i++)
	{
		p = puzzle_create(PINS[i].type, PINS[i].size);
		assert(p != NULL);
		puzzle_scramble(p, PINS[i].moves, PIN_SEED);
		hash = fnv1a(puzzle_state_ptr(p), puzzle_state_size(p));
#ifdef PIN_PRINT
		printf("%s 0x%08xu\n", PINS[i].name, hash);
#else
		if (hash != PINS[i].hash)
			printf("test_scramble_pins: %s: got 0x%08x, pinned 0x%08x\n",
				PINS[i].name, hash, PINS[i].hash);
		assert(hash == PINS[i].hash);
#endif
		puzzle_destroy(p);
	}
	printf("test_scramble_pins: seed %u leaves every family where it is pinned\n", PIN_SEED);
	return (0);
}
