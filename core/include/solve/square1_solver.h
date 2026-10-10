#ifndef SOLVE_SQUARE1_SOLVER_H
# define SOLVE_SQUARE1_SOLVER_H

# include <stdbool.h>
# include <stdint.h>
# include "move.h"
# include "puzzles/square1.h"

/// Square-1's two-phase solver (docs/en/12-phase5-square1.md D8-A), in the
/// style of Chen Shuang's sq12phase: not proven optimal, and named so.
///
/// - PHASE 1 turns both layers into "cube shape" ([corner, corner, edge]
///   repeated round the layer). The search is IDA* over slices with the
///   layers turned freely between them (a layer turn costs nothing in the
///   twist metric), guided by an EXACT table: the distance in slices, from
///   every pair of layer shapes (up to rotation) to cube shape.
/// - PHASE 2 solves the pieces inside cube shape, using only moves that
///   keep it: a layer turned by a quarter (3 units) any number of times,
///   then the slice. See SQ1_P2_* below for why exactly those.
///
/// The slice count is the cost both phases minimise; the layer turns around
/// them are free, so a solution has 2 layer moves per slice at most.

/// Layer arrangements up to rotation: 5 + 10 + 10 + 3 + 1 for 2, 3, 4, 5
/// and 6 corners (docs/en/12 Facts). Bounds the shape table.
# define SQ1_SHAPE_CLASS_COUNT 29
# define SQ1_SHAPE_MASK_COUNT 4096
# define SQ1_SHAPE_UNREACHED 0xFF
/// A layer can hold 2 to 6 corners: 8 corners in all, at least 2 in each.
# define SQ1_LAYER_MIN_CORNERS 2
# define SQ1_LAYER_MAX_CORNERS 6

/// Phase 2's coordinates are permutations of 8 pieces in 8 slots (4 in
/// each layer): 8! of them, for corners and for edges alike.
# define SQ1_PERM_LEN 8
# define SQ1_PERM_COUNT 40320
/// Phase 2 moves. A slice keeps cube shape only when the layers' "shifts"
/// (how many units a layer's pattern is turned off the one the solved top
/// layer has) are (0, 2) -- alignment A -- or (2, 0) -- alignment B. Every
/// phase 2 state is kept in alignment A (a free turn does that), so the
/// moves are:
///   TOP, BOTTOM  a quarter turn (3 units) of that layer; changes no shift
///   SLICE        the slice from alignment A, which stays in A
///   SWITCH       turn the layers (1, 1) to reach alignment B, slice (the
///                other family of shape-keeping slices), then turn (2, 2)
///                back to A
/// The search puts any number of quarter turns before a SLICE or SWITCH, and
/// that pair is one step: one slice, the unit it minimises. Phase 2 solves
/// toward R, the solved state with its bottom layer turned to shift 2; a
/// last turn takes R to solved.
# define SQ1_P2_MOVE_TOP 0
# define SQ1_P2_MOVE_BOTTOM 1
# define SQ1_P2_MOVE_SLICE 2
# define SQ1_P2_MOVE_SWITCH 3
# define SQ1_P2_MOVE_COUNT 4
/// How many ways a step can slice: from A directly, or via B.
# define SQ1_P2_SLICE_KINDS 2
/// The turns around a SWITCH, as generator amounts: before the slice, and
/// after it back to alignment A.
# define SQ1_SWITCH_TURN_IN 1
# define SQ1_SWITCH_TURN_OUT 2
/// A quarter turn is 3 units; a layer has 4 distinct quarter positions.
# define SQ1_QUARTER_UNITS 3
# define SQ1_QUARTER_COUNT 4
/// How many of a layer's units a quarter-turn macro can add up to in one
/// step: 0, 3, 6 or 9 -- SQ1_QUARTER_COUNT choices per layer.
# define SQ1_P2_ROTATIONS (SQ1_QUARTER_COUNT * SQ1_QUARTER_COUNT)
/// The middle layer is part of the coordinate: two states.
# define SQ1_MIDDLE_STATES 2

/// Search limits, in slices. 13 is the proven maximum for the whole puzzle
/// (twist metric), so these are generous; they bound the output buffer.
# define SQ1_P1_MAX_SLICES 10
# define SQ1_P2_MAX_SLICES 16
# define SQ1_ALIGN_MOVES 2
# define SQ1_FINISH_MOVES 2
/// Most moves any solve can write: per slice two layer turns and the slice,
/// plus the two aligning turns between the phases and the two finishing.
# define SQ1_SOLVE_MAX_MOVES \
	(3 * (SQ1_P1_MAX_SLICES + SQ1_P2_MAX_SLICES) \
		+ SQ1_ALIGN_MOVES + SQ1_FINISH_MOVES)

/// Once a first solution exists, keep looking for a shorter one only this
/// many search nodes more (phase 1 prefix + phase 2 IDA* together). Fixed,
/// not a clock, so a solve is the same on every target.
# define SQ1_IMPROVE_NODE_BUDGET 400000ull

/// The two phase tags a move can carry.
# define SQ1_PHASE_CUBE_SHAPE 0
# define SQ1_PHASE_FINISH 1
# define SQ1_PHASE_COUNT 2

/// Everything built once (docs/en/12 D8: under 1 MiB). Static in the
/// registry, not malloc'd, like the dodecahedral tables: it is big and
/// fixed-size and would otherwise eat the WASM arena's headroom.
///
/// - shape_class:  layer mask -> its class (0..28), or SQ1_SHAPE_UNREACHED.
///                 A mask has a bit set where a piece boundary follows that
///                 unit; two masks that are rotations of each other share
///                 a class.
/// - shape_dist:   slices from a (top class, bottom class) pair to cube
///                 shape, exact.
/// - move_corner / move_edge: phase 2 move tables, one per SQ1_P2_MOVE_*,
///                 indexed by the permutation's rank.
/// - prune_corner / prune_edge: slices still needed, by (rank * 2 + middle):
///                 a lower bound for each half of the problem.
typedef struct s_sq1_solver
{
	uint8_t		shape_class[SQ1_SHAPE_MASK_COUNT];
	uint8_t		shape_dist[SQ1_SHAPE_CLASS_COUNT][SQ1_SHAPE_CLASS_COUNT];
	uint16_t	move_corner[SQ1_P2_MOVE_COUNT][SQ1_PERM_COUNT];
	uint16_t	move_edge[SQ1_P2_MOVE_COUNT][SQ1_PERM_COUNT];
	uint8_t		prune_corner[SQ1_PERM_COUNT * SQ1_MIDDLE_STATES];
	uint8_t		prune_edge[SQ1_PERM_COUNT * SQ1_MIDDLE_STATES];
	bool		ready;
}	t_sq1_solver;

/// One finished solution, kept in the pieces it is built from.
/// `p1_top[i]`/`p1_bottom[i]` are generator amounts (0..11) of the layer
/// turns before phase 1's i'th slice; `p2_top[i]`/`p2_bottom[i]` are
/// quarter counts (0..3) before phase 2's, and `p2_switch[i]` says which
/// kind of slice it was (SLICE or SWITCH, as 0 or 1).
typedef struct s_sq1_plan
{
	uint32_t	total;
	uint32_t	p1_len;
	uint8_t		p1_top[SQ1_P1_MAX_SLICES];
	uint8_t		p1_bottom[SQ1_P1_MAX_SLICES];
	uint8_t		align_top;
	uint8_t		align_bottom;
	uint32_t	p2_len;
	uint8_t		p2_top[SQ1_P2_MAX_SLICES];
	uint8_t		p2_bottom[SQ1_P2_MAX_SLICES];
	uint8_t		p2_switch[SQ1_P2_MAX_SLICES];
	uint8_t		finish_top;
	uint8_t		finish_bottom;
}	t_sq1_plan;

/// What one solve remembers while it searches (internal to the solver).
typedef struct s_sq1_search
{
	const t_sq1_solver	*solver;
	t_sq1_plan			best;
	bool				have_best;
	uint8_t				p1_top[SQ1_P1_MAX_SLICES];
	uint8_t				p1_bottom[SQ1_P1_MAX_SLICES];
	uint8_t				p2_top[SQ1_P2_MAX_SLICES];
	uint8_t				p2_bottom[SQ1_P2_MAX_SLICES];
	uint8_t				p2_switch[SQ1_P2_MAX_SLICES];
	uint8_t				finish_top;
	uint8_t				finish_bottom;
	uint64_t			nodes;
	uint64_t			node_limit;
	bool				aborted;
}	t_sq1_search;

/// @brief Builds every table. Returns false on failure (nothing to free).
bool	sq1_solver_init(t_sq1_solver *solver);

/// @brief Slices from `state`'s layer shapes to cube shape (ignoring the
///        middle layer and the pieces), or SQ1_SHAPE_UNREACHED. Exposed
///        for the tests that pin the table against the spec's measurements.
uint32_t	sq1_solver_shape_distance(const t_sq1_solver *solver,
				const uint8_t *state);

/// @brief Solves `state` into `out` (at most `max_moves`), tagging each
///        move with SQ1_PHASE_CUBE_SHAPE or SQ1_PHASE_FINISH in
///        `phase_tags`. If `slices` is not NULL it receives the number of
///        slices in the solution (the twist metric, which the search
///        minimises).
/// @return The move count, or -1 if `state` is not a valid Square-1 or
///         `max_moves` is too small.
int		sq1_solver_solve(const t_sq1_solver *solver, const uint8_t *state,
			t_move *out, uint32_t max_moves, uint32_t *phase_tags,
			uint32_t *slices);

#endif
