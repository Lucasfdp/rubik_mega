#ifndef PUZZLES_CUBIC_H
# define PUZZLES_CUBIC_H

# include "shape.h"
# include "move.h"
# include "puzzles/cubic_lattice.h"

/// Piece-type indices into CUBIC_3X3X3_SHAPE.piece_types -- named so
/// cycle tables (cubic.c) read as "corners do this, edges do that"
/// instead of bare 0s and 1s.
# define CUBIC_CORNER_TYPE 0
# define CUBIC_EDGE_TYPE 1
/// n >= 4 only, from here down -- CUBIC_EDGE_TYPE above is n == 3 only.
/// CUBIC_WING_TYPE reuses index 1: it is CUBIC_EDGE_TYPE's own
/// generalization (both are "the flippable, orientations == 2 thing
/// strung along an edge"), and the two never coexist in the same
/// t_shape, so sharing the slot is not an aliasing hazard, just the
/// same role at a different size. CUBIC_CENTRE_TYPE is genuinely new --
/// see docs/en/05-phase1-nxn.md S1.
# define CUBIC_WING_TYPE 1
# define CUBIC_CENTRE_TYPE 2

/// Generator indices, in FACE order U R F D L B -- the same order
/// rubik's own FACE_TABLES uses (src/cube/moves.c), kept identical on
/// purpose so anyone who knows that codebase already knows this one.
# define CUBIC_GEN_U 0
# define CUBIC_GEN_R 1
# define CUBIC_GEN_F 2
# define CUBIC_GEN_D 3
# define CUBIC_GEN_L 4
# define CUBIC_GEN_B 5

/// A cube has six faces -- named rather than written as a bare 6 in
/// every face loop and table size.
# define CUBIC_FACE_COUNT 6

/// Largest size + 1 this build caches a shape/generator/facelet table
/// for. Well above what's useful anyway: shape_is_valid() already
/// rejects any piece type over 255 instances, which centres
/// (6 * (n - 2)^2) hits at n == 9 (294) -- n == 8 (216) is the largest
/// size that can ever pass. 16 leaves headroom without the cache arrays
/// costing anything real (a handful of pointers each). Shared by
/// cubic.c and cubic_facelets.c so both caches cover the same sizes.
# define CUBIC_CACHE_MAX 16

/// The shape of a 3x3x3 -- see src/puzzles/cubic.c for why it's data,
/// not a struct declaration, same pattern as rubik's own SOLVED_CUBE.
extern const t_shape	CUBIC_3X3X3_SHAPE;

/// The 3x3x3's 6 face generators (3 steps each: 90/180/270 clockwise).
/// Cycle data ported 1:1 from rubik's FACE_TABLES (src/cube/moves.c) --
/// see that file's own doc comment for how those values were verified
/// (4 quarter-turns = solved, (R U R' U')^6 = solved, 10,000-move
/// random-scramble invariant checks). Not re-derived from scratch: a
/// wrong sign here is a silent, hard-to-spot bug, so this is a port, not
/// a rewrite.
extern const t_generator_table	CUBIC_3X3X3_GENERATOR_TABLE;

/// One face's turn axis and sign -- see cubic_lattice.h's own doc
/// comments for what axis/sign mean (sign also fixes the turn direction;
/// wing orientation is derived from geometry, not stored per face).
/// `corner_cycle` points at that face's EXISTING depth-0 corner cycle
/// (U_CYCLES[0], R_CYCLES[0], ... in cubic.c), reused unchanged for
/// every size n: corner behaviour under a face's own outer-layer turn
/// does not depend on n. Declared here, not just in cubic.c, per
/// Lucas's personal coding norm (struct definitions live in headers).
typedef struct s_cubic_face
{
	t_cubic_axis	axis;
	int				sign;
	const t_cycle	*corner_cycle;
}	t_cubic_face;

/// @brief This size's piece inventory -- NULL for n < 3 or an n this
///        build doesn't support (see CUBIC_CACHE_MAX above). n == 3
///        returns the existing, unchanged CUBIC_3X3X3_SHAPE; n >= 4
///        builds {corner, wing, centre} once and caches it for the rest
///        of the process's life, same "build once per size, reuse"
///        contract move.h's t_generator_table doc comment already
///        states for generator tables.
const t_shape	*cubic_shape_for(uint32_t n);

/// @brief This size's full generator table -- NULL under the same
///        conditions as cubic_shape_for(). n == 3 returns the existing,
///        unchanged CUBIC_3X3X3_GENERATOR_TABLE (6 generators, face
///        turns only); n >= 4 builds one generator per (face, depth)
///        pair -- see cubic_generator_index() for how a generator's
///        index encodes which (face, depth) it is.
const t_generator_table	*cubic_generators_for(uint32_t n);

/// @brief Face `face`'s (CUBIC_GEN_U..CUBIC_GEN_B) axis, sign and
///        depth-0 corner cycle; NULL for an out-of-range face.
const t_cubic_face	*cubic_face(uint32_t face);

/// @brief The face sitting at `sign`'s end of `axis` (e.g. CUBIC_AXIS_Y,
///        -1 -> CUBIC_GEN_D); CUBIC_FACE_COUNT if `sign` is not +1/-1.
uint32_t	cubic_face_for(t_cubic_axis axis, int sign);

/// @brief The generator index for face `face` (CUBIC_GEN_U..
///        CUBIC_GEN_B) at layer `depth` (0 = the face's own outer
///        layer) on a puzzle of size `n`. For n == 3 this is always
///        `face` itself (`depth` must be 0 -- a 3x3x3 has no inner
///        layers in this build); for n >= 4, generators are laid out
///        face-major, depth-minor, so a bare generator NUMBER for an
///        n >= 4 puzzle means nothing on its own -- always go through
///        this function rather than assuming CUBIC_GEN_R == 1 still
///        picks the R face once n >= 4.
uint32_t	cubic_generator_index(uint32_t n, uint32_t face, uint32_t depth);

/// @brief Inverse of cubic_generator_index(): which (face, depth) a
///        size-n generator index turns. Lets web/ map generators to
///        animated layers without re-deriving the face-major layout.
/// @return 0 (outputs untouched) if `generator` is out of range.
int			cubic_generator_layer(uint32_t n, uint32_t generator,
				uint32_t *face, uint32_t *depth);

#endif
