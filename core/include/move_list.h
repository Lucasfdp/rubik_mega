#ifndef MOVE_LIST_H
# define MOVE_LIST_H

# include <stdint.h>
# include "move.h"

/// @brief Shortens a move list in place by merging runs of moves on the
///        same generator: `R R` -> `R2`, `R R'` -> nothing, cascading
///        (`R U U' R'` collapses entirely). Generic -- works for any
///        family, since it only uses each generator's own step count.
///        Moves with different phase tags are never merged, so phase
///        boundaries stay exactly where the solver put them.
///        docs/en/08-nxn-reduction.md D4: commutator chains produce a
///        lot of these at their seams (a setup's inverse meeting the
///        next setup).
/// @param moves      `count` moves, rewritten in place.
/// @param phase_tags parallel to `moves`, rewritten in place alongside.
/// @param steps      the generator table's own `steps` (3 for cubic:
///                   amounts 1..3, so a full turn is steps + 1 = 4).
/// @return The new, shorter count.
uint32_t	move_list_merge(t_move *moves, uint32_t *phase_tags,
				uint32_t count, uint32_t steps);

#endif
