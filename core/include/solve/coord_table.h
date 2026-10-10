#ifndef SOLVE_COORD_TABLE_H
# define SOLVE_COORD_TABLE_H

# include <stdint.h>
# include "shape.h"
# include "move.h"

/// Full-depth-table solving (docs/en/09-phase3-skewb-pyraminx.md D3-A)
/// for a puzzle whose state splits into a few autonomous blocks.
///
/// A "coordinate" here is one piece type's whole block of the state
/// buffer -- its permutation AND orientation bytes together. Piece types
/// are autonomous by construction: a cycle (move.h) touches exactly one
/// piece type's block, so the block's next value depends on nothing
/// outside it. Skewb's corners (permutation x orientation, 8,748
/// reachable values) and centres (360), Pyraminx's edges (11,520) and
/// axial pieces (81).
///
/// Permutation and orientation are deliberately NOT split into separate
/// coordinates. They look independent but are not: Skewb's corner
/// orientations alone take 3^7 values, yet only 3^6 for any one corner
/// permutation (the permutation group A4 has a Z3 quotient that the
/// twist sum tracks), so splitting them makes the product space 3x the
/// real group and the depth table 3x too big for the WASM arena.
///
/// A block's values are found by breadth-first search from solved and
/// numbered compactly; the solver then searches the product of those
/// compact spaces. The product can still be bigger than the group if
/// two BLOCKS are coupled; `reached_count == state_count` in the tests
/// proves it is not.
///
/// Why a table and not IDA*: both groups are small enough (3 MB and
/// 0.9 MB at one byte a state) that a depth table beats any search --
/// every solve is a table descent, optimal by construction, with no
/// hand-derived move sequence to get wrong.
///
/// Everything here lives for the process, like every other per-family
/// table (wasm_alloc.c never frees).

/// Most blocks one puzzle may split into; Skewb and Pyraminx use 2.
# define COORD_MAX_COMPONENTS 4

/// Most moves a table may search over; both puzzles use 8
/// (4 generators x amounts 1 and 2). The move set must be closed under
/// inverses, or "distance from solved" would not equal "distance to
/// solved" and the descent would walk the wrong way.
# define COORD_MAX_MOVES 8

/// Longest block, in bytes: a permutation byte plus an orientation byte
/// for each of Skewb's 8 corners.
# define COORD_MAX_LENGTH 16

/// Most distinct values one block may take on. The largest is Pyraminx's
/// edge block at 11,520; the enumeration scratch is sized from this.
# define COORD_MAX_REACHABLE 16384

/// Slots in each block's hash table: a power of two, at least twice
/// COORD_MAX_REACHABLE so linear probing stays short.
# define COORD_HASH_SIZE 32768

/// Biggest state buffer this module scratches with (Skewb and Pyraminx
/// are 28 bytes).
# define COORD_MAX_STATE 64

/// One block once built.
///
/// - offset, length: its bytes within the state buffer.
/// - size:           how many values are reachable. They get compact ids
///                   0..size-1 in the order the search met them, so the
///                   solved value is id 0.
/// - values:         the values, `size` rows of `length` bytes.
/// - buckets:        hash table of `COORD_HASH_SIZE` slots, each id + 1
///                   (0 = empty). Looking a state's bytes up here is how
///                   a malformed state -- a block no move sequence can
///                   produce -- is spotted.
/// - next:           compact move table, `size` rows x `move_count`
///                   columns: the id a value moves to under each move.
typedef struct s_coord_component
{
	uint32_t	offset;
	uint32_t	length;
	uint32_t	size;
	uint8_t		*values;
	uint16_t	*buckets;
	uint16_t	*next;
}	t_coord_component;

/// A built table. `depth` has one byte per point of the product space
/// (index = ids mixed-radix, component 0 most significant): the fewest
/// moves from that point to solved.
///
/// - state_count:   size of the product space.
/// - reached_count: how many of those points the search visited. Equal
///                  to state_count exactly when the reachable set is the
///                  whole product -- the tests assert that.
/// - max_depth:     the deepest level, i.e. the God's number of this
///                  move set (11 for both puzzles).
typedef struct s_coord_table
{
	const t_shape			*shape;
	const t_generator_table	*gens;
	t_coord_component		components[COORD_MAX_COMPONENTS];
	uint32_t				component_count;
	uint32_t				stride[COORD_MAX_COMPONENTS];
	t_move					moves[COORD_MAX_MOVES];
	uint32_t				move_count;
	uint8_t					*depth;
	uint32_t				state_count;
	uint32_t				reached_count;
	uint32_t				max_depth;
}	t_coord_table;

/// @brief Builds the blocks' compact move tables and the depth table,
///        searching over `moves` only. `piece_types` names the piece
///        types whose blocks are the coordinates; any other type (a
///        Pyraminx's tips) is ignored by the table.
/// @return 1 on success, 0 on allocation failure or if a block exceeds
///         the COORD_MAX_* limits.
int			coord_table_init(t_coord_table *table, const t_shape *shape,
				const t_generator_table *gens, const uint32_t *piece_types,
				uint32_t piece_type_count, const t_move *moves,
				uint32_t move_count);

/// @brief Reads `state`'s compact id for every block into `ids`.
/// @return 1 on success, 0 if a block holds a value no sequence of the
///         table's moves can produce (a malformed state).
int			coord_table_encode(const t_coord_table *table,
				const uint8_t *state, uint32_t *ids);

/// @brief Fewest moves from `state` to solved, or -1 if `state` is
///        malformed (see coord_table_encode()).
int			coord_table_distance(const t_coord_table *table,
				const uint8_t *state);

/// @brief Writes a shortest solution of `state` as indices into the
///        table's own `moves` array, in the order to apply them.
/// @return The number of moves, or -1 if `state` is malformed or the
///         solution is longer than `max_moves`.
int			coord_table_solve(const t_coord_table *table,
				const uint8_t *state, uint32_t *out, uint32_t max_moves);

#endif
