#ifndef SOLVE_PYRAMINX_OPTIMAL_H
# define SOLVE_PYRAMINX_OPTIMAL_H

# include <stdint.h>
# include "solve/coord_table.h"
# include "puzzles/tetrahedral.h"

/// Optimal Pyraminx solver (docs/en/09-phase3-skewb-pyraminx.md D3-A): a
/// full depth table over the 933,120 positions of the edges and axial
/// pieces, then at most four tip turns.
///
/// Two phases, because the tips are the one part of a Pyraminx that is
/// independent of everything else. A layer turn U drags its tip along
/// with it, so the tips cannot be left to chance during the main solve;
/// but a tip turn u touches nothing except its own tip, so once the
/// edges and axial pieces are home each wrong tip costs exactly one
/// more move, and 81 tip states cost at most 4. (75,582,720 positions
/// in all: 933,120 x 81.)
///
/// There is no frame correction here, unlike Skewb: no turn ever moves
/// an axial piece, so the identity state is solved.

/// The edge and axial blocks are the table's coordinates; tips are
/// handled afterwards.
# define PYRAMINX_BLOCK_COUNT 2

/// Four tips: one move each, at most.
# define PYRAMINX_MAX_TIP_MOVES TETRA_VERTEX_COUNT

/// Largest solution: God's number for the edges and axial pieces (11)
/// plus the tips (4).
# define PYRAMINX_MAX_MOVES 16

/// Byte size of a Pyraminx state: a permutation byte and an orientation
/// byte for each of the 4 tips, 4 axial pieces and 6 edges.
# define PYRAMINX_STATE_BYTES (2 * (2 * TETRA_VERTEX_COUNT + TETRA_EDGE_COUNT))

typedef struct s_pyraminx_solver
{
	t_coord_table	table;
}	t_pyraminx_solver;

/// @brief Builds the depth table (a fraction of a second, ~1 MB).
/// @return 1 on success, 0 on allocation failure.
int			pyraminx_solver_init(t_pyraminx_solver *solver);

/// @brief Appends the tip turns that finish `state` (which must already
///        have its edges and axial pieces home) to `out`, after the
///        `count` moves already there. Public because the staged solver
///        (solve/pyraminx_staged.h) ends the same way.
/// @return The new move count, or -1 if a tip is malformed or the moves
///         do not fit in `max_moves`.
int			pyraminx_append_tips(const uint8_t *state, t_move *out,
				uint32_t count, uint32_t max_moves);

/// @brief Writes a shortest edges-and-axial solution of `state`, then
///        the tip turns, to `out`; applying it leaves the puzzle in the
///        identity (solved) state. `*layer_moves` receives how many of
///        the returned moves come before the tip turns.
/// @return The total number of moves, or -1 if `state` is not a
///         Pyraminx state any turns can reach or the solution exceeds
///         `max_moves`.
int			pyraminx_solve(const t_pyraminx_solver *solver,
				const uint8_t *state, t_move *out, uint32_t max_moves,
				uint32_t *layer_moves);

#endif
