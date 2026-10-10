#ifndef SHAPE_H
# define SHAPE_H

# include <stdint.h>
# include <stdbool.h>
# include "move.h"

/// Maximum distinct piece types any supported puzzle needs. Corners,
/// edges, centres and NxN wings covers every family in scope
/// (docs/en/01-scope-and-roles.md) with room to spare; bump this if a
/// puzzle ever needs a fifth kind of piece.
# define MAX_PIECE_TYPES 4

/// One category of piece a puzzle has -- a corner, an edge, a centre,
/// an NxN wing. See docs/en/03-architecture.md S1.
///
/// - count:        how many pieces of this type the puzzle has.
/// - orientations: how many rotations one piece can sit in (3 for a
///                 cubic corner, 2 for a cubic edge, 1 -- meaning "no
///                 orientation tracked at all" -- for a plain centre;
///                 see DECISIONS.md, "Big-cube centre orientation").
typedef struct s_piece_type
{
	uint32_t	count;
	uint32_t	orientations;
}	t_piece_type;

/// A puzzle's piece inventory: how many piece types it has, and each
/// one's count/orientations. Fixed per (puzzle type, size) pair -- a
/// 3x3x3 and a 5x5x5 both have corners and edges, just different wing
/// and centre counts.
///
/// This replaces rubik's hand-declared t_cube (always exactly 8 corners
/// + 12 edges) with data, so a new puzzle family is a new t_shape value,
/// never a new struct.
typedef struct s_shape
{
	uint32_t		piece_type_count;
	t_piece_type	piece_types[MAX_PIECE_TYPES];
}	t_shape;

/// @brief Total number of pieces across every piece type in `shape`.
uint32_t	shape_total_pieces(const t_shape *shape);

/// @brief Bytes needed for one puzzle's state buffer under `shape`: for
///        each piece type in order, its permutation bytes then its
///        orientation bytes (one byte each per piece) -- see
///        shape_piece_offsets().
uint32_t	shape_state_size(const t_shape *shape);

/// @brief True if every piece type in `shape` has a non-zero count and
///        a non-zero orientation count, and no piece type has more than
///        255 pieces (the state buffer's per-piece byte width).
bool		shape_is_valid(const t_shape *shape);

/// @brief Where piece type `piece_type`'s permutation and orientation
///        blocks start within a state buffer sized by
///        shape_state_size(). Piece types are laid out in order, each
///        as [permutation bytes][orientation bytes]; this walks that
///        layout rather than hard-coding any one piece type's position,
///        so it works the same for a 3x3x3 (2 piece types) and anything
///        with more.
void		shape_piece_offsets(const t_shape *shape, uint32_t piece_type,
				uint32_t *perm_offset, uint32_t *orient_offset);

/// @brief Writes the solved state into `state` (sized by
///        shape_state_size(shape)): every piece's permutation slot set
///        to ITS OWN index (identity) and every orientation to 0.
///
/// Not the same as zeroing the buffer -- a zeroed permutation array
/// would put piece 0 in every slot, which is not a valid, let alone
/// solved, puzzle (rubik's own cube.h makes the identical point about
/// SOLVED_CUBE). Call this once at creation; never assume calloc() did
/// it.
void		shape_init_solved(const t_shape *shape, uint8_t *state);

/// @brief Applies one quarter-step of `cycle` to `state` (sized by
///        shape_state_size(shape)): rotates the pieces named in
///        cycle->slots through their type's permutation/orientation
///        blocks, adding cycle->delta modulo that type's own
///        orientation count. See t_cycle's own doc comment (move.h) for
///        the rotation itself.
void		shape_apply_cycle(const t_shape *shape, uint8_t *state,
				const t_cycle *cycle);

/// @brief Applies one whole move -- `move.amount` quarter-steps of
///        generator `move.generator` of `gens` -- to `state`, in place.
///        The caller guarantees the generator and amount are in range
///        (puzzle_apply_move() is the bounds-checked front door). Shared
///        by puzzle_apply_move() and every table builder that has to
///        replay moves on a raw state buffer, so the "apply each cycle,
///        `amount` times over" loop exists exactly once.
void		shape_apply_move(const t_shape *shape,
				const t_generator_table *gens, uint8_t *state, t_move move);

#endif
