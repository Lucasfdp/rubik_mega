#ifndef PUZZLES_DODECAHEDRAL_H
# define PUZZLES_DODECAHEDRAL_H

# include <stdint.h>
# include "shape.h"
# include "move.h"
# include "cut_geometry.h"

/// Megaminx and Kilominx (docs/en/11-phase4-dodecahedral.md): one
/// family, two sizes. A face turn rotates one pentagonal face and the
/// layer under it by a multiple of 72 degrees. Every one of the twelve
/// faces can turn, centres never move, so the solved state is the
/// identity (no frame correction, unlike Skewb).
///
/// Kilominx is exactly the Megaminx's generators restricted to the
/// corner pieces (D1-A): the same twelve turns, derived by the same
/// code from the same corner stickers, with the edge and centre pieces
/// left out.

/// `size` argument of puzzle_create(): layers, as the architecture doc
/// counts them (docs/en/03-architecture.md).
# define DODECA_KILOMINX_SIZE 2
# define DODECA_MEGAMINX_SIZE 3

/// Piece-type indices into the shape. A Kilominx has only the first.
# define DODECA_CORNER_TYPE 0
# define DODECA_EDGE_TYPE 1
# define DODECA_CENTRE_TYPE 2

# define DODECA_FACE_COUNT 12
# define DODECA_CORNER_COUNT 20
# define DODECA_EDGE_COUNT 30
/// A pentagon has five corners and five edges.
# define DODECA_FACE_SIDES 5

/// A corner has three stickers, an edge two. Centres are a single
/// sticker whose own rotation is not tracked (same call as the cubic
/// and Skewb centres, DECISIONS.md).
# define DODECA_CORNER_ORIENTATIONS 3
# define DODECA_EDGE_ORIENTATIONS 2
# define DODECA_CENTRE_ORIENTATIONS 1

/// A face turn is 72 degrees, a fifth of a full turn: amounts 1 to 4 are
/// the four non-trivial turns (amount 5 would be the identity).
# define DODECA_STEPS 4

/// Per face: one centre, five corner stickers, five edge stickers.
# define DODECA_STICKERS_PER_FACE 11
# define DODECA_MEGAMINX_STICKER_COUNT 132
/// Corner stickers only: 20 corners x 3.
# define DODECA_KILOMINX_STICKER_COUNT 60

/// Stickers a single face turn moves, counted as the pieces it carries:
/// five corners of three and five edges of two (Megaminx), five
/// corners of three (Kilominx). The centre's own sticker turns in place
/// and is not counted -- it never changes slot.
# define DODECA_MEGAMINX_MOVED_STICKERS 25
# define DODECA_KILOMINX_MOVED_STICKERS 15

/// Fixed-point scale of every sticker coordinate and normal: the real
/// value times 4096 (docs/en/11-phase4-dodecahedral.md D4-A). The
/// biggest coordinate is a vertex at radius sqrt(3), about 7100, well
/// inside 16 bits.
# define DODECA_COORD_SCALE 4096

/// The layer a turn carries is everything above a plane parallel to the
/// face, at this fraction of the face's own distance from the centre.
/// Between 0.575 and 0.834 every sticker is classed correctly (measured
/// by a throwaway script; web/ clips pieces by the same planes), and
/// 0.7 sits inside that gap with room either side.
# define DODECA_LAYER_CUT_DEPTH 0.7

/// A corner sticker sits this fraction of the way from its face's
/// centre towards the corner; an edge sticker the same towards the
/// edge's midpoint. Both only place a sticker's reference point inside
/// its own piece's patch of the face -- the pieces themselves are cut
/// by the planes above.
# define DODECA_CORNER_STICKER_REACH 0.7
# define DODECA_EDGE_STICKER_REACH 0.75

/// Matching a rotated sticker to a sticker (cut_geometry.h,
/// t_cut_rotation), in squared fixed-point units. Rounding both the
/// sticker and the rotated copy costs under a few units; the next
/// nearest sticker is over a thousand away (asserted in
/// tests/test_dodecahedral.c), so these leave a wide margin both ways.
# define DODECA_MATCH_TOLERANCE_SQ 100
# define DODECA_MATCH_SEPARATION_SQ 10000

/// A point or direction in real (unscaled) coordinates, used only while
/// the geometry is being built: nothing floating-point survives into the
/// sticker table, the generators or the state.
typedef struct s_dvec3
{
	double	x;
	double	y;
	double	z;
}	t_dvec3;

/// @brief The whole family for `size` (DODECA_KILOMINX_SIZE or
///        DODECA_MEGAMINX_SIZE) in one record -- shape, generators,
///        stickers, turns, colours, wide facelet records -- built once
///        per size. NULL for any other size, or on allocation failure
///        or if the geometry fails its own rigid-turn checks.
const t_cut_family	*dodeca_family(uint32_t size);

/// @brief Shape of the given size (NULL for an unsupported one):
///        Kilominx 20 corners x3; Megaminx adds 30 edges x2 and 12
///        centres x1.
const t_shape		*dodeca_shape(uint32_t size);

/// @brief Face names in face-index order, the order generators,
///        colours and centre slots all share: "U" (top), then the five
///        faces round it, starting at the front and sweeping towards
///        the right ("F" "R" "BR" "BL" "L"), then the five faces of the
///        lower ring in the same sweep, each named for the upper faces
///        it touches ("DR" below F and R, "DBR", "DB", "DBL", "DL"),
///        then "D". NULL if `face` is out of range.
const char			*dodeca_face_name(uint32_t face);

#endif
