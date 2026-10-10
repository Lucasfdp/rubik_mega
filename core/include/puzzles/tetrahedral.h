#ifndef PUZZLES_TETRAHEDRAL_H
# define PUZZLES_TETRAHEDRAL_H

# include "shape.h"
# include "move.h"
# include "cut_geometry.h"

/// Pyraminx (docs/en/09-phase3-skewb-pyraminx.md): a tetrahedron of
/// edge 3 cut into 3 layers from each vertex. 4 tips, 4 axial pieces
/// (the piece under each tip), 6 edges, 36 stickers (9 per face: 3 tips,
/// 3 axial, 3 edge).
///
/// Generators: at each vertex v, `u` turns only the tip, `U` turns the
/// tip, the axial piece and the three edges that meet at v. Neither ever
/// moves an axial piece, so the frame is fixed and the identity state is
/// "solved" (unlike Skewb, D1).

# define TETRA_TIP_TYPE 0
# define TETRA_AXIAL_TYPE 1
# define TETRA_EDGE_TYPE 2

# define TETRA_VERTEX_COUNT 4
# define TETRA_FACE_COUNT 4
# define TETRA_EDGE_COUNT 6
/// Tips and axial pieces sit at a vertex with 3 stickers round it; an
/// edge piece has 2.
# define TETRA_VERTEX_PIECE_ORIENTATIONS 3
# define TETRA_EDGE_ORIENTATIONS 2

/// Each generator turns by 120 degrees, so amount 1 and 2.
# define TETRA_STEPS 2

# define TETRA_STICKERS_PER_FACE 9
# define TETRA_STICKER_COUNT 36

/// Generator index = vertex * TETRA_DEPTH_COUNT + depth. Depth 0 is the
/// tip alone (WCA lowercase u, l, r, b), depth 1 the layer including the
/// tip (WCA U, L, R, B).
# define TETRA_DEPTH_COUNT 2
# define TETRA_DEPTH_TIP 0
# define TETRA_DEPTH_LAYER 1
# define TETRA_GENERATOR_COUNT 8

/// The tetrahedron is drawn in cube coordinates: vertex v at
/// TETRA_VERTEX[v] (see tetrahedral.c). A point is the sum over vertices of S_v * V_v
/// where S_v = 9 * (barycentric weight of vertex v), so S sums to 9 and
/// every sticker centroid is an integer vector at that scale.
# define TETRA_BARYCENTRIC_SCALE 9

/// Layer thresholds, in dot(V_v, pos) at scale 9. With S_v the weight
/// on vertex v, dot(V_v, pos) = 4 * S_v - 9, so "more than 6/9 of the
/// way to v" (the tip) is dot > 15, and "more than 3/9" (tip plus the
/// layer below it) is dot > 3.
# define TETRA_TIP_THRESHOLD 15
# define TETRA_LAYER_THRESHOLD 3

/// @brief The whole family in one record -- shape, generators, stickers,
///        turns, colours, facelet records -- built once. NULL only on
///        allocation failure.
const t_cut_family		*tetrahedral_family(void);

const t_shape			*tetrahedral_shape(void);

/// @brief The 8 turns, derived from geometry once and cached. NULL only
///        on allocation failure.
const t_generator_table	*tetrahedral_generators(void);

/// @brief The edge slot between vertices `a` and `b` (either order,
///        a != b), in the order (0,1) (0,2) (0,3) (1,2) (1,3) (2,3).
///        Public so a solver can name "the three edges of one face"
///        without hard-coding slot numbers.
uint8_t					tetrahedral_edge_slot(uint32_t a, uint32_t b);

/// @brief All 36 stickers, slot-major per piece type: tips (3 per
///        vertex, counter-clockwise from outside), then axial pieces
///        the same way, then edges (2 each).
const t_sticker			*tetrahedral_stickers(void);

#endif
