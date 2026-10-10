#ifndef PUZZLES_SKEWB_H
# define PUZZLES_SKEWB_H

# include "shape.h"
# include "move.h"
# include "cut_geometry.h"

/// Skewb (docs/en/09-phase3-skewb-pyraminx.md): a cube cut on its four
/// body-diagonal planes through the centre. Turning either half of a
/// cut by 120 degrees moves one corner piece in place (a twist), cycles
/// the three corners next to it, and cycles the three centres next to
/// it. All 8 corners can be turned (D1-A): the far half of a cut is a
/// real generator, not "the near half backwards plus a whole-puzzle
/// rotation", so what animates is what the state says.

/// Piece-type indices into the Skewb shape.
# define SKEWB_CORNER_TYPE 0
# define SKEWB_CENTRE_TYPE 1

# define SKEWB_CORNER_COUNT 8
# define SKEWB_CENTRE_COUNT 6
/// Corners carry 3 orientations; centres are a single diamond sticker,
/// so none is tracked (same call as big-cube centres, DECISIONS.md).
# define SKEWB_CORNER_ORIENTATIONS 3
# define SKEWB_CENTRE_ORIENTATIONS 1

/// Each generator turns by 120 degrees, so amount 1 and 2 are the two
/// distinct non-trivial turns (amount 3 would be the identity).
# define SKEWB_STEPS 2

/// 6 faces x (4 corner triangles + 1 centre diamond).
# define SKEWB_STICKER_COUNT 30

/// Corner slot c (0..7) sits at (sx, sy, sz), each +1 or -1, with the
/// bit set meaning -1: bit 0 for x, bit 1 for y, bit 2 for z. Generator
/// c turns the half-space around corner c, so generator and corner
/// numbering are the same.
# define SKEWB_CORNER_X_BIT 1
# define SKEWB_CORNER_Y_BIT 2
# define SKEWB_CORNER_Z_BIT 4

/// Centre slots, by the face they sit on: +x, -x, +y, -y, +z, -z.
/// SKEWB_CENTRE_TOP is the +y one, the face the staged solver builds.
# define SKEWB_CENTRE_TOP 2

/// A cube has three axes; named because both the three faces round a
/// corner and the three coordinates of a vector loop over it.
# define SKEWB_AXIS_COUNT 3

/// Coordinates are scaled by 3 so every sticker centroid is an integer:
/// a centre sits at distance 3 from the middle, a corner triangle's
/// centroid at (2, 2, 3) up to signs and order. See skewb.c.
# define SKEWB_SCALE 3

/// A corner triangle's centroid is 2 along each of the two in-face axes
/// at that scale: the cut lines run between edge midpoints (at 0 and
/// +-3), so the triangle's vertices are at 3 and 0 and its centroid at
/// (3 + 3 + 0) / 3 = 2.
# define SKEWB_CORNER_INSET 2

/// @brief The whole family in one record -- shape, generators, stickers,
///        turns, colours, facelet records -- built once. NULL only on
///        allocation failure. What puzzle.c and the facelet layer use.
const t_cut_family		*skewb_family(void);

/// @brief Skewb's piece inventory -- 8 corners x3, 6 centres x1.
const t_shape			*skewb_shape(void);

/// @brief The 8 corner turns, derived from geometry once and cached.
///        NULL only on allocation failure.
const t_generator_table	*skewb_generators(void);

/// @brief All 30 stickers, slot-major within each piece type (corners
///        first, 3 per slot in counter-clockwise order from outside;
///        then the 6 centres). The one truth the generators were derived
///        from, and what the facelet layer will paint from.
const t_sticker			*skewb_stickers(void);

/// @brief True for the four corners whose turns generate the classic
///        3,149,280-position group (the WCA R L U B): those with
///        sx * sy * sz == +1, i.e. an even number of bits set.
int						skewb_is_canonical_corner(uint32_t corner);

#endif
