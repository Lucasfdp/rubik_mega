#ifndef PRNG_H
# define PRNG_H

# include <stdint.h>

/// Self-contained xorshift32 PRNG -- not libc rand(): rand()'s sequence
/// for a given seed isn't guaranteed identical across platforms/libcs,
/// and a scramble needs to reproduce byte-for-byte from its seed alone
/// on every target this project ships to (native tests here, WASM in
/// the browser). Must never be called with a zero state (it would stay
/// zero forever); callers guard that with prng_seed().
///
/// Shared by puzzle.c (the generator-table families) and square1.c, so
/// both scramble from the same sequence rather than carrying a copy each.
static inline uint32_t	prng_next(uint32_t *state)
{
	*state ^= *state << 13;
	*state ^= *state >> 17;
	*state ^= *state << 5;
	return (*state);
}

/// @brief The PRNG state for `seed`: a zero seed would stay zero forever,
///        so it is mapped to 1.
static inline uint32_t	prng_seed(uint32_t seed)
{
	return (seed == 0 ? 1u : seed);
}

#endif
