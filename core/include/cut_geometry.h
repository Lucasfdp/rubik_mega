#ifndef CUT_GEOMETRY_H
# define CUT_GEOMETRY_H

# include <stdint.h>
# include "shape.h"
# include "move.h"

/// Phase 3 (docs/en/09-phase3-skewb-pyraminx.md, D2-A): a puzzle whose
/// turns are rotations of everything on one side of a plane can
/// describe itself as a list of stickers plus a list of turns, and let
/// this module work out the piece cycles and orientation deltas.
/// Derived, not typed, because the two NxN move-model bugs of
/// 2026-10-04 were exactly hand-written-table mistakes. Skewb,
/// Pyraminx and (Phase 4) Megaminx/Kilominx use it.
///
/// Everything is integer arithmetic, with two ways of turning a point:
///
/// - Exact (Skewb, Pyraminx): a 120-degree turn about an axis whose
///   squared length is 3 (a cube body diagonal, or a tetrahedron vertex
///   in cube coordinates) maps integer points to integer points, so no
///   rounding can hide a bug.
/// - Rounded (Megaminx, Kilominx; docs/en/11-phase4-dodecahedral.md
///   D2-A): a 72-degree turn involves the golden ratio, which no integer
///   lattice holds, so the puzzle supplies its own rotation function and
///   a tolerance. The contract stays "refuse anything that is not a
///   clean rigid turn": a rotated sticker must land within the
///   tolerance of exactly one sticker, with every other sticker much
///   further away (t_cut_rotation), so a wrong geometry fails loudly at
///   init instead of silently in play.

typedef struct s_ivec3
{
	int32_t	x;
	int32_t	y;
	int32_t	z;
}	t_ivec3;

/// One visible sticker. `pos` is its centroid and `normal` the outward
/// direction of the face it lies on, both in the puzzle's own integer
/// coordinates (scale is the puzzle's business; see its header).
/// `piece_type`/`slot` say which piece slot the sticker belongs to.
///
/// Order matters: stickers of one slot must appear in the order the
/// slot's orientation counts from -- that is, orientation `o` means the
/// piece's k-th sticker sits at the slot's ((k + o) mod m)-th sticker
/// position. For 3-sticker pieces that order must be counter-clockwise
/// seen from outside (cut_order_ccw() does it); for 2-sticker pieces
/// any fixed order works.
typedef struct s_sticker
{
	t_ivec3	pos;
	t_ivec3	normal;
	uint8_t	piece_type;
	uint8_t	slot;
}	t_sticker;

/// One generator: turn everything with dot(axis, pos) > threshold by
/// 120 degrees. A positive amount is clockwise seen from the tip of
/// `axis` looking at the centre -- the WCA convention. `axis` must have
/// squared length 3.
typedef struct s_cut_turn
{
	t_ivec3	axis;
	int32_t	threshold;
}	t_cut_turn;

/// How a family rotates a point by one quarter-step of a turn, and how
/// strictly a rotated sticker must land on a sticker. `turn` has
/// ivec3_turn()'s contract (clockwise seen from the tip of `axis`) but
/// may round. `tolerance_sq` is the largest squared distance (position
/// plus normal, summed) between a rotated sticker and the sticker it is
/// matched to; `separation_sq` the squared distance every other sticker
/// must exceed. Both are 0 for the exact path: a match is then exact
/// and merely has to be unique.
typedef struct s_cut_rotation
{
	t_ivec3	(*turn)(t_ivec3 axis, t_ivec3 p);
	int64_t	tolerance_sq;
	int64_t	separation_sq;
}	t_cut_rotation;

/// One sticker as web/ reads it (docs/en/09-phase3-skewb-pyraminx.md
/// D4): 8 bytes, signed, in the puzzle's own integer coordinates. This
/// is the per-family geometry record `puzzle_facelet_geometry_stride`
/// reports. Coordinates fit a byte: Skewb's reach 3, Pyraminx's 9.
typedef struct s_cut_facelet
{
	int8_t	pos[3];
	int8_t	normal[3];
	uint8_t	piece_type;
	uint8_t	slot;
}	t_cut_facelet;

_Static_assert(sizeof(t_cut_facelet) == 8,
	"t_cut_facelet must stay 8 bytes -- it is the facelet geometry stride web/ decodes");

/// The wider record Megaminx and Kilominx need (docs/en/11-phase4-
/// dodecahedral.md D4-A): the same fields as t_cut_facelet with 16-bit
/// coordinates, because a dodecahedron's stickers sit at irrational
/// positions and are stored rounded at a fixed scale. 14 bytes (no
/// padding: every field is 2-byte or 1-byte aligned).
typedef struct s_cut_facelet_wide
{
	int16_t	pos[3];
	int16_t	normal[3];
	uint8_t	piece_type;
	uint8_t	slot;
}	t_cut_facelet_wide;

_Static_assert(sizeof(t_cut_facelet_wide) == 14,
	"t_cut_facelet_wide must stay 14 bytes -- it is the facelet geometry stride web/ decodes");

/// Everything one cut-plane puzzle family is, in one place: its shape,
/// its derived generators, its stickers (the one truth the generators
/// came from), the turns they were derived from, each sticker's solved
/// colour (a colour index) and the facelet records web/ reads. Built
/// once per family; puzzle.c treats every such family the same way.
///
/// `facelets` points at `sticker_count` records of `facelet_stride`
/// bytes each: t_cut_facelet (8) for Skewb and Pyraminx,
/// t_cut_facelet_wide (14) for Megaminx and Kilominx.
typedef struct s_cut_family
{
	const t_shape				*shape;
	const t_generator_table		*generators;
	const t_sticker				*stickers;
	uint32_t					sticker_count;
	const t_cut_turn			*turns;
	const uint8_t				*colours;
	const uint8_t				*facelets;
	uint32_t					facelet_stride;
}	t_cut_family;

/// The exact integer 120-degree rotation Skewb and Pyraminx use
/// (ivec3_turn, zero tolerance).
extern const t_cut_rotation	CUT_ROTATION_EXACT_120;

/// @brief Dot product of two integer vectors.
int32_t			ivec3_dot(t_ivec3 a, t_ivec3 b);

/// @brief Rotates `p` by one quarter-step of a turn about `axis`: 120
///        degrees clockwise seen from the tip of `axis` (so
///        counter-clockwise about -axis, by the right-hand rule).
///        Exact for any `axis` whose squared length is 3.
t_ivec3			ivec3_turn(t_ivec3 axis, t_ivec3 p);

/// @brief Swaps `s[1]` and `s[2]` if the three stickers `s[0..2]`, all on
///        one piece whose outward direction is `outward`, are not
///        counter-clockwise seen from outside. Gives a 3-sticker slot
///        the order t_sticker's doc comment requires.
void			cut_order_ccw(t_sticker *s, t_ivec3 outward);

/// @brief Builds the generator table for `turn_count` turns of a puzzle
///        described by `stickers` and `shape`. Each generator holds one
///        quarter-step: a 3-cycle of pieces, a twist in place (a
///        length-1 cycle with a non-zero delta), and so on, exactly as
///        shape_apply_cycle() consumes them.
///
///        Refuses (NULL) anything that is not a clean rigid turn: a
///        sticker that does not land on a sticker, a piece whose
///        stickers land in different slots or are rotated by different
///        amounts, a sticker count that disagrees with the shape's
///        orientation counts, a cycle longer than MAX_CYCLE_LENGTH.
///        Memory comes from malloc and lives for the process, same
///        contract as every other per-family table.
///
///        This is the exact 120-degree form Skewb and Pyraminx call; it
///        is a thin wrapper over cut_derive_generators_rotated(), so
///        their code and tests did not change when Phase 4 generalised
///        the rotation.
/// @return NULL on any of the above or on allocation failure.
t_generator_table	*cut_derive_generators(const t_shape *shape,
						const t_sticker *stickers, uint32_t sticker_count,
						const t_cut_turn *turns, uint32_t turn_count,
						uint32_t steps);

/// @brief The general form: rotates with `rotation` and matches within
///        its tolerance. Refuses (NULL) additionally when a rotated
///        sticker has no match within `rotation->tolerance_sq`, or when
///        another sticker is not further than `rotation->separation_sq`
///        -- an ambiguous match is a geometry bug, never guessed at.
t_generator_table	*cut_derive_generators_rotated(const t_shape *shape,
						const t_sticker *stickers, uint32_t sticker_count,
						const t_cut_turn *turns, uint32_t turn_count,
						uint32_t steps, const t_cut_rotation *rotation);

/// @brief Converts a sticker array to the 8-byte records web/ reads.
void			cut_fill_facelets(t_cut_facelet *out, const t_sticker *st,
					uint32_t count);

/// @brief Converts a sticker array to the 14-byte wide records.
/// @return 0 if any coordinate does not fit 16 bits (nothing is then
///         trustworthy), 1 otherwise.
int				cut_fill_facelets_wide(t_cut_facelet_wide *out,
					const t_sticker *st, uint32_t count);

/// @brief Index in the sticker array of piece type `type`, slot `slot`,
///        sticker `rank` of that slot. Valid because every family lists
///        stickers type-major, then slot-major, then by rank.
uint32_t		cut_sticker_index(const t_shape *shape, uint32_t type,
					uint32_t slot, uint32_t rank);

/// @brief Colour of every sticker position for `state`, written to
///        `out` (one byte per sticker, array order). The piece in a slot
///        with orientation o shows its own sticker k at position
///        ((k + o) mod m), the same convention the derivation uses.
void			cut_paint(const t_cut_family *family, const uint8_t *state,
					uint8_t *out);

/// @brief Writes the indices of the stickers generator `gen` moves into
///        `out` (up to `max`), in array order -- what the renderer
///        rotates (D5).
/// @return How many, or UINT32_MAX if `gen` is out of range.
uint32_t		cut_moving_facelets(const t_cut_family *family, uint32_t gen,
					uint8_t *out, uint32_t max);

#endif
