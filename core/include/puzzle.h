#ifndef PUZZLE_H
# define PUZZLE_H

# include <stdint.h>
# include "move.h"
# include "platform.h"

/// Which puzzle family a puzzle_create() call builds. One value per
/// family in docs/en/01-scope-and-roles.md; PUZZLE_TYPE_COUNT is not a
/// puzzle, it's how many there are.
typedef enum e_puzzle_type
{
	PUZZLE_CUBIC,          // NxNxN, any size -- src/puzzles/cubic.c
	PUZZLE_SKEWB,
	PUZZLE_DODECAHEDRAL,   // Megaminx, Kilominx -- src/puzzles/dodecahedral.c
	PUZZLE_TETRAHEDRAL,    // Pyraminx -- src/puzzles/tetrahedral.c
	PUZZLE_SQUARE1,        // its own state model -- src/puzzles/square1.c
	PUZZLE_TYPE_COUNT
}	t_puzzle_type;

/// Opaque handle to one live puzzle instance. The web/ side (and
/// anything else outside core/) only ever holds a pointer to this -- it
/// never reaches into the struct directly. See docs/en/03-architecture.md
/// S3 for the full contract this header is the C side of.
typedef struct s_puzzle puzzle_t;

/// @brief Creates one puzzle instance. `size` is the NxN side length for
///        PUZZLE_CUBIC, and the layer count for PUZZLE_DODECAHEDRAL (2 is
///        a Kilominx, 3 a Megaminx); ignored, pass 0, for families with no
///        size parameter. PUZZLE_CUBIC, PUZZLE_SKEWB, PUZZLE_TETRAHEDRAL
///        and PUZZLE_DODECAHEDRAL are implemented so far -- see
///        docs/en/04-roadmap.md for build order.
/// @return NULL on an unsupported (type, size) pair or allocation
///         failure.
WASM_EXPORT(puzzle_create)
puzzle_t	*puzzle_create(t_puzzle_type type, uint32_t size);

/// @brief Frees a puzzle created by puzzle_create(). Safe to call on
///        NULL.
WASM_EXPORT(puzzle_destroy)
void		puzzle_destroy(puzzle_t *puzzle);

/// @brief Applies one move to the puzzle, in place. `generator` must be
///        below the puzzle's generator count and `amount` in
///        [1, puzzle_generator_steps()] (see move.h's t_generator_table)
///        -- anything else is a silent no-op by design, not a crash. So
///        is a move that is in range but illegal in this state (only
///        Square-1's blocked slice): never half-applied.
WASM_EXPORT(puzzle_apply_move)
void		puzzle_apply_move(puzzle_t *puzzle, int16_t generator,
				int8_t amount);

/// @brief Scrambles the puzzle with `move_count` random legal moves
///        (for Square-1: `move_count` random slices, each after a layer
///        turn pair that makes it legal),
///        seeded by `seed` -- the same seed always produces the same
///        scramble, on any target this project builds for (native or
///        WASM), because it uses its own PRNG rather than libc's
///        rand() (see puzzle.c).
WASM_EXPORT(puzzle_scramble)
void		puzzle_scramble(puzzle_t *puzzle, uint32_t move_count,
				uint32_t seed);

/// @brief The moves puzzle_scramble() applies for the same `move_count` and
///        `seed`, written to `out` (up to `max`) in order WITHOUT applying
///        them. Applying the returned list to the puzzle in the state it
///        had at this call gives exactly the state puzzle_scramble() would
///        have left it in, which is what lets web/ animate a scramble
///        instead of snapping to it (docs/en/13-phase6-polish.md D9).
///        Square-1's moves depend on the state they are applied to (each
///        slice is preceded by the layer turns that make it legal), so for
///        it the list is only valid from the CURRENT state.
/// @return How many moves the scramble has in all (may exceed `max`, in
///         which case only the first `max` were written). Never more than
///         puzzle_scramble_max_moves() for `move_count`.
WASM_EXPORT(puzzle_scramble_moves)
uint32_t	puzzle_scramble_moves(const puzzle_t *puzzle, t_move *out,
				uint32_t max, uint32_t move_count, uint32_t seed);

/// @brief An upper bound on puzzle_scramble_moves()'s return value for
///        `move_count`: the count itself, except Square-1, whose every
///        slice comes with up to two layer turns.
WASM_EXPORT(puzzle_scramble_max_moves)
uint32_t	puzzle_scramble_max_moves(const puzzle_t *puzzle,
				uint32_t move_count);

/// @brief Whether the puzzle LOOKS solved (1) or not (0): every face shows
///        one colour. Deliberately not "the state equals the solved
///        state": a puzzle with no fixed centres (every even cube, and an
///        odd one past 3x3x3, whose centre pieces move; a Skewb; a
///        Kilominx) can be turned as a whole to a state that is a
///        different point in the group and yet looks solved, and a player
///        has solved it. Square-1 has a fixed frame (the middle layer), so
///        for it only the solved state itself counts.
///        Repaints the puzzle's facelet buffer on the way, which any
///        puzzle_facelets() call overwrites again.
WASM_EXPORT(puzzle_is_solved)
uint32_t	puzzle_is_solved(puzzle_t *puzzle);

/// @brief Read-only pointer into the puzzle's own state buffer -- a
///        Uint8Array view on the web/ side reads this directly, with no
///        copy. See docs/en/03-architecture.md S3 ("zero-copy").
WASM_EXPORT(puzzle_state_ptr)
const uint8_t	*puzzle_state_ptr(const puzzle_t *puzzle);

/// @brief Size, in bytes, of the buffer puzzle_state_ptr() points at.
WASM_EXPORT(puzzle_state_size)
uint32_t	puzzle_state_size(const puzzle_t *puzzle);

/// @brief How many solving algorithms are registered for this puzzle's
///        type. 0 until docs/en/04-roadmap.md's solving phases land for
///        that family.
WASM_EXPORT(puzzle_algorithm_count)
uint32_t	puzzle_algorithm_count(const puzzle_t *puzzle);

/// @brief Human-readable name of algorithm `algo_id` (below
///        puzzle_algorithm_count()'s return value), e.g. "Kociemba
///        (two-phase)".
WASM_EXPORT(puzzle_algorithm_name)
const char	*puzzle_algorithm_name(const puzzle_t *puzzle,
				uint32_t algo_id);

/// @brief The `max_moves` puzzle_solve() needs to be given for any of
///        this puzzle's algorithms to always fit -- 0 if it has none.
///        Size-dependent: a reduction solve on a big cube runs to
///        thousands of moves (docs/en/08-nxn-reduction.md D4), so web/
///        sizes its output buffer from this rather than a constant.
WASM_EXPORT(puzzle_solve_max_moves)
uint32_t	puzzle_solve_max_moves(const puzzle_t *puzzle);

/// @brief Solves the puzzle with algorithm `algo_id`, writing up to
///        `max_moves` moves into `out` (and, for each move written,
///        which phase of the algorithm produced it into the matching
///        slot of `out_phase_tags` -- see docs/en/03-architecture.md S4).
///        Does not mutate the puzzle itself.
/// @return Number of moves written, or UINT32_MAX if algorithm `algo_id`
///         does not exist or the puzzle has no solvers registered yet.
WASM_EXPORT(puzzle_solve)
uint32_t	puzzle_solve(puzzle_t *puzzle, uint32_t algo_id, t_move *out,
				uint32_t max_moves, uint32_t *out_phase_tags);

/// From here down: the rendering surface docs/en/07-nxn-web-view.md
/// specifies (D1, Option A) -- core/ owns every colour/orientation
/// rule, web/ only paints what these return. Cubic family only so far
/// (the only family puzzle_create() builds).

/// @brief How many visible stickers ("facelets") the puzzle has:
///        6 * n * n for an NxNxN.
WASM_EXPORT(puzzle_facelet_count)
uint32_t	puzzle_facelet_count(const puzzle_t *puzzle);

/// @brief Fixed per-size geometry, 4 bytes per facelet: lattice cell
///        x, y, z (0..n-1; x = R, y = U, z = F at the high end) and the
///        face it points out of (0..5 = U R F D L B). Never changes for
///        the puzzle's lifetime -- read it once. Layout:
///        puzzles/cubic_facelets.h's t_cubic_facelet.
WASM_EXPORT(puzzle_facelet_geometry)
const uint8_t	*puzzle_facelet_geometry(const puzzle_t *puzzle);

/// @brief Repaints and returns the current colour of every facelet --
///        one byte each, the face (0..5, same order as geometry) whose
///        solved colour it shows -- parallel to puzzle_facelet_geometry().
///        The buffer is owned by the puzzle and overwritten by the next
///        call; copy it if it must outlive that.
WASM_EXPORT(puzzle_facelets)
const uint8_t	*puzzle_facelets(puzzle_t *puzzle);

/// @brief How many generators (turnable layers) the puzzle has -- valid
///        `generator` values for puzzle_apply_move() are 0..count-1.
WASM_EXPORT(puzzle_generator_count)
uint32_t	puzzle_generator_count(const puzzle_t *puzzle);

/// @brief Which face (0..5 = U R F D L B) generator `generator` turns,
///        or -1 if out of range. With puzzle_generator_depth(), lets
///        web/ map generators to layers without re-deriving core's
///        generator numbering.
WASM_EXPORT(puzzle_generator_face)
int32_t		puzzle_generator_face(const puzzle_t *puzzle, uint32_t generator);

/// @brief How many layers in from its face generator `generator` sits
///        (0 = the outer layer), or -1 if out of range.
WASM_EXPORT(puzzle_generator_depth)
int32_t		puzzle_generator_depth(const puzzle_t *puzzle, uint32_t generator);

/// From here down: Phase 3's per-family additions
/// (docs/en/09-phase3-skewb-pyraminx.md D4, D5), for the cut-plane
/// families (PUZZLE_SKEWB, PUZZLE_TETRAHEDRAL). Cubic puzzles keep the
/// face/depth calls above and report "not supported" here.

/// @brief Bytes per record in puzzle_facelet_geometry()'s buffer: 4 for
///        cubic (x, y, z, face), 8 for Skewb and Pyraminx
///        (cut_geometry.h's t_cut_facelet: signed pos[3], normal[3],
///        then piece type and slot), 14 for Megaminx and Kilominx
///        (t_cut_facelet_wide: the same fields with 16-bit coordinates,
///        at scale 1/4096). 0 if the puzzle has no facelets. web/ decodes
///        the record per family.
WASM_EXPORT(puzzle_facelet_geometry_stride)
uint32_t	puzzle_facelet_geometry_stride(const puzzle_t *puzzle);

/// @brief Writes generator `generator`'s cut to `out` as 4 integers:
///        the rotation axis, then the threshold, in the same coordinates
///        as the facelet geometry. For Skewb and Pyraminx the axis is
///        an integer vector of squared length 3; for Megaminx and
///        Kilominx it is the face's unit normal at scale 4096, so the
///        threshold is in scale 4096 squared (the dot product of two
///        such vectors). The generator turns every facelet with
///        dot(axis, pos) > threshold. A positive amount turns clockwise
///        seen from the axis tip looking at the centre.
/// @return 1 on success, 0 if the family has no axis API (cubic) or
///         `generator` is out of range.
WASM_EXPORT(puzzle_generator_axis)
uint32_t	puzzle_generator_axis(const puzzle_t *puzzle, uint32_t generator,
				int32_t *out);

/// @brief Writes the facelet indices generator `generator` moves into
///        `out` (up to `max`) -- what the renderer rotates by one
///        quarter-step (360 / (steps + 1) degrees: 120 for Skewb and
///        Pyraminx, 72 for Megaminx and Kilominx) times the amount about
///        puzzle_generator_axis(). The up-to-`max` buffer is bytes, so a
///        family has at most 256 facelets in a moving layer.
/// @return How many facelets move (may exceed `max`), or UINT32_MAX if
///         unsupported or out of range.
WASM_EXPORT(puzzle_generator_facelets)
uint32_t	puzzle_generator_facelets(const puzzle_t *puzzle,
				uint32_t generator, uint8_t *out, uint32_t max);

/// From here down: Phase 5's additions for Square-1
/// (docs/en/12-phase5-square1.md D2, D4). Every other family answers
/// them as it always behaved: one steps value, every in-range move legal,
/// no units.

/// @brief How many amounts generator `generator` has: amounts are
///        [1, steps]. Per generator because Square-1's layers have 11 and
///        its slice 1; every other family has one value for all of its
///        generators. 0 if `generator` is out of range.
WASM_EXPORT(puzzle_generator_steps)
uint32_t	puzzle_generator_steps(const puzzle_t *puzzle, uint32_t generator);

/// @brief Whether `generator`/`amount` is in range AND legal in the
///        puzzle's CURRENT state (1) or not (0). Only Square-1 has
///        in-range moves that are illegal -- a slice with a corner
///        straddling the cut -- so web/ asks this to show a blocked slice
///        instead of keeping its own copy of the rule.
WASM_EXPORT(puzzle_move_is_legal)
uint32_t	puzzle_move_is_legal(const puzzle_t *puzzle, int16_t generator,
				int8_t amount);

/// @brief Square-1 only: SQ1_UNIT_COUNT (24) bytes, one per unit (top
///        0..11, bottom 12..23), each 0 = edge, 1 = first half of a
///        corner, 2 = second half (clockwise seen from above). With the
///        facelets this says which kind of piece sits where; the
///        facelet numbering itself is fixed by unit (puzzles/square1.h).
///        The buffer is owned by the puzzle and overwritten by the next
///        call. NULL for every other family.
WASM_EXPORT(puzzle_square1_units)
const uint8_t	*puzzle_square1_units(puzzle_t *puzzle);

#endif
