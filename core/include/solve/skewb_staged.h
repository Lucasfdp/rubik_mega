#ifndef SOLVE_SKEWB_STAGED_H
# define SOLVE_SKEWB_STAGED_H

# include <stdint.h>
# include "solve/stage_table.h"
# include "solve/skewb_optimal.h"

/// Staged ("layer-first") Skewb solver
/// (docs/en/10-phase3-human-methods.md D2, D3): three named phases, each
/// a cumulative goal, each solved by table descent from where the last
/// one left off.
///
///   0. First-face corners    the four corners of the top (+y) face
///   1. First-face centre     + that face's centre
///   2. Finish                everything (the optimal solver's own
///                            table, reused)
///
/// All three run in the CANONICAL FRAME, exactly as skewb_solve() does
/// (see skewb_optimal.h for why a frame exists): find the rotation that
/// puts the even corners back in their slots, solve there, rename every
/// move back. So "the top face" is the top face of the framed state; on
/// the real puzzle it shows up as a whole face too, but the puzzle ends
/// as a rotated identity, as it does for the optimal solver.

# define SKEWB_STAGE_COUNT 3

/// Corners on the top face: those with the y bit clear (sy = +1).
# define SKEWB_TOP_FACE_CORNERS 4

/// Tracked pieces after stage 0 (the four corners) and stage 1 (+ the
/// centre).
# define SKEWB_STAGE0_PIECES SKEWB_TOP_FACE_CORNERS
# define SKEWB_STAGE1_PIECES (SKEWB_TOP_FACE_CORNERS + 1)

/// How many states each stage's table holds -- measured by breadth-first
/// search over the real generators, and cross-checked against the corner
/// block's 8,748 values (12 placements of the two odd corners x 3^4
/// twists = 972; the centre then multiplies by its 6 positions). The
/// draft spec's 486 and 2,916 were half of these. tests/test_stage_table.c
/// asserts them.
# define SKEWB_STAGE0_STATES 972
# define SKEWB_STAGE1_STATES 5832

/// The deepest level of each stage's table (stage 2's is the optimal
/// table's God's number). A descent from ANY state takes at most its
/// table's depth, so the sum bounds a whole solve -- a proven cap, not a
/// measured one (the tests assert each depth).
# define SKEWB_STAGE0_DEPTH 6
# define SKEWB_STAGE1_DEPTH 7
# define SKEWB_STAGE2_DEPTH 11
# define SKEWB_STAGED_MAX_MOVES (SKEWB_STAGE0_DEPTH + SKEWB_STAGE1_DEPTH \
		+ SKEWB_STAGE2_DEPTH)

typedef struct s_skewb_staged
{
	const t_skewb_solver	*base;
	t_stage_table			corners;
	t_stage_table			centre;
}	t_skewb_staged;

/// @brief Builds the two small stage tables (`base`'s own depth table is
///        the third stage, and its rotations are the frame; `base` must
///        outlive `staged`).
/// @return 1 on success, 0 on allocation failure.
int		skewb_staged_init(t_skewb_staged *staged, const t_skewb_solver *base);

/// @brief Writes a staged solution of `state` to `out`, using only the
///        four canonical turns' corners renamed through the frame;
///        applying it leaves every face one colour. `stage_ends[k]`
///        receives the total move count at the end of stage k (see
///        pyraminx_staged_solve() for the convention).
/// @return The total move count, or -1 if `state` cannot be reached
///         from solved by any turns or the solution exceeds
///         `max_moves`.
int		skewb_staged_solve(const t_skewb_staged *staged,
			const uint8_t *state, t_move *out, uint32_t max_moves,
			uint32_t *stage_ends);

#endif
