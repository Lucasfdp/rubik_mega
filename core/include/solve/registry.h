#ifndef SOLVE_REGISTRY_H
# define SOLVE_REGISTRY_H

# include <stdint.h>
# include "algorithm.h"
# include "puzzle.h"
# include "solve/kociemba.h"

/// The cubic family's algorithm registry, per size
/// (docs/en/06-phase2-solvers.md, docs/en/08-nxn-reduction.md D4):
/// n == 3 has the four ported rubik solvers, every supported n >= 4 has
/// reduction. Kept in src/solve/, not src/puzzles/cubic.c: this file
/// only ever reads a state buffer and writes moves, it never touches
/// cubic.c's shape/generator-table internals.

/// Largest move count any n == 3 algorithm can return -- the biggest of
/// their own caps (LAYER_MAX_MOVES, solve/layer.h).
# define CUBIC_N3_MAX_MOVES 200

/// @brief How many algorithms are registered for a size-n cubic puzzle
///        (0 for a size nothing is registered for).
uint32_t			cubic_algorithm_count(uint32_t n);

/// @brief Algorithm `id`'s entry for size n -- NULL if `id` is not below
///        cubic_algorithm_count(n).
const t_algorithm	*cubic_algorithm(uint32_t n, uint32_t id);

/// @brief The most moves any algorithm registered for size n can
///        return -- the `max_moves` a caller must allow for. 0 if
///        nothing is registered for n.
uint32_t			cubic_solve_max_moves(uint32_t n);

/// The same three queries for any puzzle family -- what puzzle.c asks.
/// `size` is the cubic side length and is ignored by Skewb and Pyraminx
/// (docs/en/09-phase3-skewb-pyraminx.md D3 and
/// docs/en/10-phase3-human-methods.md: an optimal and a staged
/// algorithm each). For the dodecahedral family `size` is 2 (Kilominx) or
/// 3 (Megaminx), one 3-cycle algorithm each
/// (docs/en/11-phase4-dodecahedral.md). Square-1 has one, the two-phase
/// solver (docs/en/12-phase5-square1.md D8). A family with nothing
/// registered reports 0 and NULL, same as an unsupported cubic size.

/// @brief How many algorithms are registered for (type, size).
uint32_t			registry_algorithm_count(t_puzzle_type type, uint32_t size);

/// @brief Algorithm `id`'s entry for (type, size), or NULL.
const t_algorithm	*registry_algorithm(t_puzzle_type type, uint32_t size,
						uint32_t id);

/// @brief The most moves any algorithm for (type, size) can return.
uint32_t			registry_solve_max_moves(t_puzzle_type type, uint32_t size);

/// @brief The shared, lazily built Kociemba solver (its ~3 MB of move
///        tables are built once per process), for other solvers that
///        finish on a 3x3x3 stage -- NULL if building it failed.
const t_kociemba_solver	*registry_kociemba(void);

#endif
