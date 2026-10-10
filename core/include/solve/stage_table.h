#ifndef SOLVE_STAGE_TABLE_H
# define SOLVE_STAGE_TABLE_H

# include <stdint.h>
# include "shape.h"
# include "move.h"
# include "solve/coord_table.h"

/// Staged ("human method") solving (docs/en/10-phase3-human-methods.md
/// D4): one small distance table per stage goal, each solved by
/// descent from wherever the previous stage left the puzzle.
///
/// A stage goal is "this list of pieces is home": each tracked piece
/// sits in its own slot with orientation 0. A stage's state is just
/// where those pieces are -- (slot, orientation) for each -- and the
/// table maps every such state the moves can reach to its distance from
/// the goal. Pieces NOT in the list are invisible to it, which is the
/// whole point: a stage promises only what it names.
///
/// Why this is not coord_table.c. A coord_table block is a whole piece
/// type, found by BFS and numbered into a dense product space; that is
/// right for a full-solve table whose blocks are autonomous and few.
/// A stage tracks an arbitrary subset of pieces (three of a Pyraminx's
/// six edges, the four corners of one Skewb face), so its state does not
/// line up with any block, and the reachable set is not a clean product.
/// A hash from the packed state to its depth is the simplest thing that
/// is right for any subset.
///
/// Why descent is correct. The move set is closed under inverses, so
/// "distance from the goal" equals "distance to the goal", and stepping
/// to any neighbour one level nearer reaches the goal in exactly
/// `distance` moves. Goals are cumulative: each stage's list includes
/// the previous stage's, so a state that meets stage k meets every
/// earlier stage too, and a later descent ends with the earlier pieces
/// home again (it may move them on the way, and it must put them back,
/// because they are part of its own goal).
///
/// Everything here lives for the process, like every other table
/// (wasm_alloc.c never frees).

/// Most pieces one stage may track: the packed state is one byte per
/// piece in a uint64.
# define STAGE_MAX_TRACKED 8

/// Move-set and state-buffer limits, shared with coord_table.c because
/// the same two puzzles feed both.
# define STAGE_MAX_MOVES COORD_MAX_MOVES
# define STAGE_MAX_STATE COORD_MAX_STATE

/// A piece to track: its type and its index within the type. A piece's
/// index is the slot it occupies when the puzzle is solved.
typedef struct s_stage_piece
{
	uint8_t	piece_type;
	uint8_t	piece;
}	t_stage_piece;

/// A built stage table.
///
/// - tracked, tracked_count: the pieces whose positions form the state.
/// - moves, move_count:      the moves the search runs over; closed
///                           under inverses (see above).
/// - keys, depths:           open-addressed hash, `capacity` slots. A
///                           slot is empty when its depth byte is
///                           STAGE_EMPTY; otherwise keys[slot] is a
///                           packed state and depths[slot] its distance.
/// - hash_shift:             64 - log2(capacity): the shift that keeps
///                           the top bits of a mixed key as its slot.
/// - capacity:               a power of two, at least 4/3 of the states
///                           the caller said to expect, so linear
///                           probing stays short.
/// - state_count:            how many states the search reached.
/// - max_depth:              the deepest level -- the most moves a
///                           descent of this stage can ever take.
typedef struct s_stage_table
{
	const t_shape			*shape;
	const t_generator_table	*gens;
	t_stage_piece			tracked[STAGE_MAX_TRACKED];
	uint32_t				tracked_count;
	t_move					moves[STAGE_MAX_MOVES];
	uint32_t				move_count;
	uint64_t				*keys;
	uint8_t					*depths;
	uint32_t				capacity;
	uint32_t				hash_shift;
	uint32_t				state_count;
	uint32_t				max_depth;
}	t_stage_table;

/// @brief Builds the table by breadth-first search from the goal over
///        `moves`.
/// @param max_states An upper bound on the stage's state count (the
///        callers pass the measured count): sizes the hash, and the
///        build fails if the search finds more.
/// @return 1 on success, 0 on allocation failure, too many tracked
///         pieces or moves, or more than `max_states` states.
int		stage_table_init(t_stage_table *table, const t_shape *shape,
			const t_generator_table *gens, const t_stage_piece *tracked,
			uint32_t tracked_count, const t_move *moves,
			uint32_t move_count, uint32_t max_states);

/// @brief Fewest moves from `state` to this stage's goal, or -1 if the
///        tracked pieces are somewhere no sequence of the table's moves
///        can put them (a malformed state).
int		stage_table_distance(const t_stage_table *table,
			const uint8_t *state);

/// @brief Walks `state` to the stage's goal by shortest moves: writes
///        each move to `out` and APPLIES it to `state`, so on return
///        `state` is where the next stage starts.
/// @return The number of moves, or -1 if `state` is malformed or needs
///         more than `max_moves`. A refusal leaves `state` untouched.
int		stage_table_descend(const t_stage_table *table, uint8_t *state,
			t_move *out, uint32_t max_moves);

#endif
