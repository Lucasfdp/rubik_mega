#ifndef PUZZLES_CUBIC_FACELETS_H
# define PUZZLES_CUBIC_FACELETS_H

# include <stdint.h>
# include "shape.h"
# include "puzzles/cubic_lattice.h"

/// The most stickers one cubic piece shows (a corner) -- sizes the
/// per-slot sticker-face lists below.
# define CUBIC_MAX_STICKERS 3

/// piece_type value marking a facelet that never changes colour: a
/// 3x3x3's centre, which CUBIC_3X3X3_SHAPE does not track as a piece at
/// all (DECISIONS.md, "Big-cube centre orientation"). Outside every real
/// piece-type index (MAX_PIECE_TYPES, shape.h).
# define CUBIC_FIXED_FACELET 0xFF

/// One visible sticker position on a size-n cube: the lattice cell it
/// sits on (each coordinate 0..n-1, x = R, y = U, z = F at the high end)
/// and the face it points out of (CUBIC_GEN_U..CUBIC_GEN_B). Exported
/// to web/ as raw bytes (puzzle_facelet_geometry(), puzzle.h), so the
/// layout is part of the WASM boundary contract -- see the static
/// assert below. docs/en/07-nxn-web-view.md D1.
typedef struct s_cubic_facelet
{
	uint8_t	x;
	uint8_t	y;
	uint8_t	z;
	uint8_t	face;
}	t_cubic_facelet;

/// web/src/engine/puzzle.ts reads the geometry buffer with a fixed
/// 4-byte stride (FACELET_GEOMETRY_BYTES); this fails the native build
/// loudly if the struct ever changes shape instead of silently desyncing.
_Static_assert(sizeof(t_cubic_facelet) == 4,
	"t_cubic_facelet must stay 4 bytes -- web/src/engine/puzzle.ts decodes it with a fixed stride");

/// Where one facelet's colour comes from in the state buffer: the slot
/// (of piece type `piece_type`) under it, and which of that slot's
/// stickers it is (`sticker`, an index into the slot's own face list --
/// see t_cubic_facelet_table.slot_faces). For CUBIC_FIXED_FACELET,
/// `sticker` holds the constant colour itself and `slot` is unused.
typedef struct s_cubic_facelet_source
{
	uint8_t	piece_type;
	uint8_t	slot;
	uint8_t	sticker;
}	t_cubic_facelet_source;

/// Everything needed to colour every facelet of a size-n cube from its
/// state buffer, built once per size and cached (same "build once,
/// reuse" contract as move.h's t_generator_table).
///
/// - count:      6 * n * n facelets.
/// - geometry:   `count` entries, face-major; within a face, the two
///               other axes ascend in x, y, z order. The order carries
///               no meaning beyond being fixed -- consumers read each
///               facelet's cell/face from here, never assume a layout.
/// - sources:    `count` entries, parallel to `geometry`.
/// - slot_faces: per piece type, CUBIC_MAX_STICKERS face indices per
///               slot: the faces that slot's stickers point out of, in
///               orientation order (sticker 0 is the one a piece with
///               orientation 0 shows its own sticker 0 on). The same
///               list, read at a piece's HOME slot, is that piece's own
///               colours -- one table serves both questions.
typedef struct s_cubic_facelet_table
{
	uint32_t						count;
	const t_cubic_facelet			*geometry;
	const t_cubic_facelet_source	*sources;
	const uint8_t					*slot_faces[MAX_PIECE_TYPES];
}	t_cubic_facelet_table;

/// Build-time scratch for cubic_facelets.c: every slot's lattice point
/// and sticker-face list, per piece type. Corners come from that file's
/// CORNER_HIGH, a 3x3x3's edges from EDGE_POINT, wings and centres from
/// cubic_lattice_point() -- the one canonical slot order. Never outlives
/// one table build (the face lists are kept, the points freed).
typedef struct s_facelet_build
{
	uint32_t		n;
	const t_shape	*shape;
	t_cubic_point	*points[MAX_PIECE_TYPES];
	uint8_t			*faces[MAX_PIECE_TYPES];
}	t_facelet_build;

/// @brief This size's facelet table -- NULL for any n cubic_shape_for()
///        rejects.
const t_cubic_facelet_table	*cubic_facelets_for(uint32_t n);

/// @brief Writes one face index (CUBIC_GEN_U..CUBIC_GEN_B) per facelet
///        into `out` (sized by cubic_facelets_for(n)->count): the colour
///        a physical cube in `state` shows at that position, in
///        "colour of face X when solved" terms. A piece with orientation
///        o in a slot shows, on that slot's sticker k, its own sticker
///        (k - o) mod orientations -- rubik's own assign_rotated() rule
///        (src/render/geometry.c), now the single place it lives.
void	cubic_facelets_paint(uint32_t n, const uint8_t *state, uint8_t *out);

/// @brief Which of a 3x3x3's 12 edges (cubic.c's E_UR..E_BR order) the
///        wing-or-edge cell `p` lies on, on a size-n cube; -1 if `p` is
///        not on an edge.
int				cubic_edge_index_for_point(t_cubic_point p, uint32_t n);

/// @brief Lattice coordinate of corner slot `corner` (cubic.c's
///        URF..DRB order) on a size-n cube.
t_cubic_point	cubic_corner_point(uint32_t corner, uint32_t n);

#endif
