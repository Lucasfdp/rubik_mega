#ifndef PUZZLES_SQUARE1_H
# define PUZZLES_SQUARE1_H

# include <stdint.h>
# include "move.h"

/// Square-1 (docs/en/12-phase5-square1.md). Its own state model behind the
/// same C API (D1-A): every other family moves a fixed set of slots per
/// turn, but a Square-1 corner is two units wide, so which pieces a turn
/// moves depends on the state, and the slice is legal only when no corner
/// straddles the cut. A fixed 24-unit buffer keeps the zero-copy state view.
///
/// Geometry, once, because every constant below hangs off it.
///
/// - A UNIT is a 30-degree wedge of a layer. A layer is 12 units. A corner
///   is two adjacent units carrying the same piece id; an edge is one.
/// - Units are indexed 0..11 CLOCKWISE SEEN FROM ABOVE, in both layers, so
///   the same index is the same angle in the top and the bottom layer.
/// - The CUT is the vertical plane through the centre that the slice turns
///   along. It runs between unit 11 and unit 0 and between unit 5 and unit
///   6, so units 0..5 are the RIGHT half and 6..11 the LEFT half (the left
///   half of the middle layer is the frame and never moves).
/// - The slice is a 180-degree turn of the right half about the horizontal
///   axis PERPENDICULAR to the cut (not about a line in it: only that maps
///   the right half to itself). It sends top unit i to bottom unit 5 - i
///   and back, so a corner's two units swap order.
/// - SOLVED has both layers in the same absolute alignment, because a
///   solved Square-1 is a cube: top and bottom pieces sit directly above
///   one another. From solved the slice is legal but does NOT keep cube
///   shape (the half-turn lands every edge 30 degrees off a face). The spec
///   (D3) put the solved bottom one unit round so that it did; that state is
///   not a cube. See DECISIONS.md, 2026-10-09.

/// Units in one layer, and in the whole puzzle.
# define SQ1_LAYER_UNITS 12
# define SQ1_UNIT_COUNT 24
/// The slice swaps the six units of one side of the cut in each layer.
# define SQ1_HALF_UNITS 6

/// State buffer: top units 0..11, bottom units 12..23 (each holding the id
/// of the piece that covers it), then one byte for the middle layer.
# define SQ1_MIDDLE_INDEX 24
# define SQ1_STATE_SIZE 25
/// Middle byte values: the right half of the middle layer is either as
/// built (0) or flipped over by an odd number of slices (1).
# define SQ1_MIDDLE_SOLVED 0
# define SQ1_MIDDLE_FLIPPED 1

/// Piece ids. Corners 0..7, edges 8..15; ids 0..3 and 8..11 start in the
/// top layer, 4..7 and 12..15 in the bottom. A corner's id is in both of
/// its units.
# define SQ1_CORNER_COUNT 8
# define SQ1_EDGE_COUNT 8
# define SQ1_PIECE_COUNT 16
# define SQ1_FIRST_EDGE_ID 8
/// Pieces that start in each layer, per kind.
# define SQ1_CORNERS_PER_LAYER 4
# define SQ1_EDGES_PER_LAYER 4

/// Generators: 0 = top layer, 1 = bottom layer, 2 = slice. A layer turn is
/// amount 1..11 (30 degrees each, clockwise seen from that generator's axis
/// tip: +Y for the top, -Y for the bottom, as puzzle_generator_axis states
/// for the other families); the slice is amount 1 only (its own inverse).
# define SQ1_GEN_TOP 0
# define SQ1_GEN_BOTTOM 1
# define SQ1_GEN_SLICE 2
# define SQ1_GENERATOR_COUNT 3
# define SQ1_LAYER_STEPS 11
# define SQ1_SLICE_STEPS 1

/// Facelets (D4-A), numbered by UNIT so the numbering is fixed for the
/// puzzle's life: 24 caps (one per unit, top then bottom), 24 sides (one
/// per unit; an edge has one side sticker, each half of a corner one of
/// the corner's two), then 6 on the middle layer: front, side and back of
/// the left half, then of the right half.
# define SQ1_CAP_FACELETS 0
# define SQ1_SIDE_FACELETS 24
# define SQ1_MIDDLE_FACELETS 48
# define SQ1_MIDDLE_FACELET_COUNT 6
# define SQ1_FACELET_COUNT 54

/// What kind of piece covers a unit (puzzle_square1_units()): an edge, the
/// first half of a corner, or its second half, "first" being the one met
/// first going clockwise seen from above.
# define SQ1_UNIT_EDGE 0
# define SQ1_UNIT_CORNER_FIRST 1
# define SQ1_UNIT_CORNER_SECOND 2

/// Faces, in the order the rest of core uses for colours (CUBIC_GEN_*):
/// a facelet byte is the face whose solved colour it shows.
# define SQ1_FACE_U 0
# define SQ1_FACE_R 1
# define SQ1_FACE_F 2
# define SQ1_FACE_D 3
# define SQ1_FACE_L 4
# define SQ1_FACE_B 5

/// What scrambling does: `move_count` is the number of SLICES, each
/// preceded by a random layer turn pair that makes it legal. 25 is past the
/// shape graph's depth of 9 with room to spare (D9).
# define SQ1_SCRAMBLE_SLICES 25
/// Moves one scramble slice can contribute: a top turn, a bottom turn and
/// the slice (a turn of 0 units is not a move, so often fewer).
# define SQ1_SCRAMBLE_MOVES_PER_SLICE 3

/// The colours of a piece, fixed for life. `cap` is the colour of the face
/// that points straight up or down (white or yellow); `side_a` and `side_b`
/// are met in that order going clockwise as seen from outside the cap, so
/// the same piece has its sides the other way round, seen from above, when
/// its cap faces down. An edge has `side_a` only.
typedef struct s_sq1_piece
{
	uint8_t	cap;
	uint8_t	side_a;
	uint8_t	side_b;
}	t_sq1_piece;

/// @brief Writes the solved state: both layers cube-shaped and aligned,
///        middle layer as built.
void	sq1_init_solved(uint8_t *state);

/// @brief Whether `state` is structurally a Square-1: every corner id in
///        exactly two adjacent units of one layer, every edge id in
///        exactly one, a valid middle byte. Says nothing about
///        reachability; every structurally valid state is reachable.
int		sq1_state_is_valid(const uint8_t *state);

/// @brief Whether `generator`/`amount` is a move the puzzle has at all
///        (in range), ignoring the state.
int		sq1_move_in_range(int16_t generator, int8_t amount);

/// @brief Whether the slice can be done now: no corner straddles either
///        end of the cut, in either layer. Layer turns are always legal.
int		sq1_slice_is_legal(const uint8_t *state);

/// @brief Whether `generator`/`amount` is in range AND legal in `state`.
int		sq1_move_is_legal(const uint8_t *state, int16_t generator,
			int8_t amount);

/// @brief Turns the top layer clockwise (seen from above) by `amount`
///        units, 0..11. Always legal.
void	sq1_turn_top(uint8_t *state, uint32_t amount);

/// @brief Turns the bottom layer clockwise seen from BELOW by `amount`
///        units, 0..11 -- which is anticlockwise seen from above.
void	sq1_turn_bottom(uint8_t *state, uint32_t amount);

/// @brief Swaps the right halves of the two layers (D3). Does nothing and
///        returns 0 if the slice is not legal -- never half-applied.
int		sq1_slice(uint8_t *state);

/// @brief Applies one move if it is in range and legal; otherwise leaves
///        `state` byte-identical. Returns 1 if it was applied.
int		sq1_apply_move(uint8_t *state, int16_t generator, int8_t amount);

/// @brief `move_count` random slices, each after a random layer turn pair
///        that makes it legal, seeded by `seed` (same seed, same scramble,
///        on every target -- the PRNG is core's own, prng.h).
void	sq1_scramble(uint8_t *state, uint32_t move_count, uint32_t seed);

/// @brief The moves sq1_scramble() would apply to `state` for the same
///        arguments, written to `out` (up to `max`) without touching
///        `state`. A layer turn of 0 units is not a move and is left out.
///        sq1_scramble() is this function applied, so the two cannot
///        disagree.
/// @return How many moves the scramble has in all (may exceed `max`).
uint32_t	sq1_scramble_moves(const uint8_t *state, uint32_t move_count,
				uint32_t seed, t_move *out, uint32_t max);

/// @brief Whether `state` is the solved state. A Square-1's frame (the
///        left half of the middle layer) never moves, so unlike the other
///        families there is no whole-puzzle rotation that also looks
///        solved: the pieces' colours identify them, and the middle band
///        has to match the sides.
int		sq1_is_solved(const uint8_t *state);

/// @brief Paints SQ1_FACELET_COUNT colour bytes (the face whose solved
///        colour each facelet shows) into `out`, numbered as above.
void	sq1_facelets(const uint8_t *state, uint8_t *out);

/// @brief Writes SQ1_UNIT_COUNT bytes into `out`: SQ1_UNIT_EDGE,
///        SQ1_UNIT_CORNER_FIRST or SQ1_UNIT_CORNER_SECOND for each unit.
///        With the facelets, this is all web/ needs to place pieces.
void	sq1_units(const uint8_t *state, uint8_t *out);

/// @brief The colours of piece `id` (0..15). NULL if out of range.
const t_sq1_piece	*sq1_piece(uint32_t id);

#endif
