#ifndef SOLVE_BYTES_H
# define SOLVE_BYTES_H

# include <stdint.h>

/// @brief Copies `count` bytes from `from` to `to`. The WASM build is
///        freestanding (no libc, so no memcpy), and every solver that
///        snapshots a state buffer or a table block needs this exact
///        loop -- it lives here once instead of once per solver. The
///        ranges must not overlap.
void	solve_copy_bytes(uint8_t *to, const uint8_t *from, uint32_t count);

/// @brief Sets `count` bytes at `block` to zero -- the freestanding
///        stand-in for memset(0), for tables a builder must be able to
///        rebuild from scratch (a static table is zero only the first
///        time).
void	solve_zero_bytes(void *block, uint32_t count);

#endif
