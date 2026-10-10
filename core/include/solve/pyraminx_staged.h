#ifndef SOLVE_PYRAMINX_STAGED_H
# define SOLVE_PYRAMINX_STAGED_H

# include <stdint.h>
# include "solve/stage_table.h"
# include "solve/pyraminx_optimal.h"

/// Staged ("layer-first") Pyraminx solver
/// (docs/en/10-phase3-human-methods.md D2): four named phases, each a
/// cumulative goal, each solved by table descent from where the last
/// one left off.
///
///   0. Align axial pieces     the four axials turned home
///   1. First layer edges      + the three edges of one face
///   2. Finish edges and axials   everything but the tips (this is the
///                             optimal solver's own table, reused)
///   3. Tips                   one turn per wrong tip
///
/// The first layer is the face opposite vertex PYRAMINX_STAGED_VERTEX:
/// the vertex held up, so the face built is the one underneath that the
/// player is working on. Axial pieces first mirrors how a beginner
/// starts, and it is cheap -- at most 4 moves.
///
/// What this does and does not do: every stage is a shortest descent,
/// so total length stays near the optimal solver's. What differs is
/// WHAT you watch -- named phases that each finish something visible.

# define PYRAMINX_STAGE_COUNT 4

/// The vertex held up; the first layer is the face opposite it.
# define PYRAMINX_STAGED_VERTEX 0

/// Edge slots in the first layer: the face opposite a vertex has three
/// edges, one between each pair of the other three vertices.
# define PYRAMINX_FIRST_LAYER_EDGES (TETRA_VERTEX_COUNT - 1)

/// Tracked pieces after stage 0 (the four axial pieces) and stage 1 (+ the
/// first layer's edges).
# define PYRAMINX_STAGE0_PIECES TETRA_VERTEX_COUNT
# define PYRAMINX_STAGE1_PIECES (TETRA_VERTEX_COUNT + PYRAMINX_FIRST_LAYER_EDGES)

/// How many states each stage's table holds -- measured by breadth-first
/// search over the real generators (docs/en/10-phase3-human-methods.md
/// "Facts"; tests/test_stage_table.c asserts them): 3^4 axial
/// orientations, then 960 more for where the three edges can be.
# define PYRAMINX_STAGE0_STATES 81
# define PYRAMINX_STAGE1_STATES 77760

/// The deepest level of each stage's table (stage 2's is the optimal
/// table's God's number). A descent from ANY state takes at most its
/// table's depth, so their sum plus the tips bounds a whole solve --
/// a proven cap, not a measured one (the tests assert each depth).
# define PYRAMINX_STAGE0_DEPTH 4
# define PYRAMINX_STAGE1_DEPTH 9
# define PYRAMINX_STAGE2_DEPTH 11
# define PYRAMINX_STAGED_MAX_MOVES (PYRAMINX_STAGE0_DEPTH \
		+ PYRAMINX_STAGE1_DEPTH + PYRAMINX_STAGE2_DEPTH \
		+ PYRAMINX_MAX_TIP_MOVES)

typedef struct s_pyraminx_staged
{
	const t_pyraminx_solver	*base;
	t_stage_table			axial;
	t_stage_table			first_layer;
}	t_pyraminx_staged;

/// @brief Builds the two small stage tables (`base`'s own table is the
///        third stage and is not copied; `base` must outlive `staged`).
/// @return 1 on success, 0 on allocation failure.
int		pyraminx_staged_init(t_pyraminx_staged *staged,
			const t_pyraminx_solver *base);

/// @brief Writes a staged solution of `state` to `out`; applying it
///        leaves the puzzle in the identity (solved) state.
///        `stage_ends[k]` receives the total move count at the end of
///        stage k, so stage k's moves are out[stage_ends[k-1]..
///        stage_ends[k]) (stage_ends[-1] = 0); stages that need no
///        moves have equal ends. Stage ends never decrease.
/// @return The total move count, or -1 if `state` is not a Pyraminx
///         state any turns can reach or the solution exceeds
///         `max_moves`.
int		pyraminx_staged_solve(const t_pyraminx_staged *staged,
			const uint8_t *state, t_move *out, uint32_t max_moves,
			uint32_t *stage_ends);

#endif
