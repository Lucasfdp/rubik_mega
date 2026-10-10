#ifndef ALGORITHM_H
# define ALGORITHM_H

# include <stdint.h>
# include "move.h"

/// One registered solving algorithm for a puzzle type/size -- the
/// registry docs/en/03-architecture.md S4 describes. `solve()` reads a
/// puzzle's own state buffer (the same bytes puzzle_state_ptr() exposes,
/// never mutated) and writes up to `max_moves` moves into `out`, tagging
/// each one in `phase_tags` with which phase of the algorithm produced
/// it (0, 1, 2, ... -- what each number means is specific to that
/// algorithm; `04-roadmap.md` Phase 2's point is that the HUD can show
/// which phase is animating, not that phase numbers mean the same thing
/// across algorithms).
///
/// One deviation from 03-architecture.md S4's own sketch: `state` here
/// is `const uint8_t *`, not `const void *` -- a puzzle's state buffer
/// is always bytes (shape.h), so the more precise type costs nothing and
/// catches a wrong-pointer-type mistake at compile time instead of
/// silently accepting one.
///
/// @return Number of moves written, or UINT32_MAX if `state` is not a
///         solvable (valid) puzzle state for this algorithm.
///
/// `size` is the puzzle's size parameter (n for an NxNxN) -- added for
/// docs/en/08-nxn-reduction.md D4: an NxN solver can't tell n from the
/// state buffer alone. Fixed-size algorithms ignore it.
typedef struct s_algorithm
{
	const char	*name;
	uint32_t	(*solve)(const uint8_t *state, uint32_t size, t_move *out,
					uint32_t max_moves, uint32_t *phase_tags);
}	t_algorithm;

#endif
