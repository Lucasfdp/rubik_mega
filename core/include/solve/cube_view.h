#ifndef SOLVE_CUBE_VIEW_H
# define SOLVE_CUBE_VIEW_H

# include <stdint.h>
# include <stdbool.h>

/// From here down: `core/src/solve/`, Phase 2 (docs/en/06-phase2-solvers.md).
/// This directory ports rubik's own coord.h/movetable.h/prune.h/ida.h/
/// solve.h machinery (Kociemba's two-phase method) nearly unchanged --
/// see that doc's "Why no Option A/B here" for why a near-verbatim port,
/// not a from-scratch generic rewrite, is the right call.
///
/// `t_solve_cube` is this directory's own view of a 3x3x3: the same
/// corner/edge permutation+orientation idea as rubik's own `t_cube`
/// (rubik/include/cube.h), but every field is a plain `uint8_t` array,
/// not an enum array -- deliberately chosen so this struct's object
/// representation is byte-for-byte the cubic family's own n==3 state
/// buffer layout (`shape_piece_offsets()` for `CUBIC_3X3X3_SHAPE`:
/// corner perm, corner orient, edge perm, edge orient, in that order,
/// no gaps -- confirmed by both sides agreeing on 40 bytes). That is
/// what makes `solve_cube_from_state()`/`solve_cube_to_state()` below
/// plain byte copies instead of a field-by-field unpack/repack.

# define SOLVE_CORNER_COUNT 8
# define SOLVE_EDGE_COUNT 12

/// Corner piece identities, rubik's own Kociemba-convention order and
/// numbering (rubik/include/cube.h's t_corner) -- the SAME order and
/// numbering `core/src/puzzles/cubic.c`'s own `C_URF..C_DRB` enum
/// already uses for its hand-written corner cycle tables, so there is
/// nothing to reconcile between the two, only to confirm (and
/// `test_solve_cube.c` does, see below).
# define SOLVE_CORNER_URF 0
# define SOLVE_CORNER_UFL 1
# define SOLVE_CORNER_ULB 2
# define SOLVE_CORNER_UBR 3
# define SOLVE_CORNER_DFR 4
# define SOLVE_CORNER_DLF 5
# define SOLVE_CORNER_DBL 6
# define SOLVE_CORNER_DRB 7

/// Edge piece identities, same convention, matching `cubic.c`'s own
/// `E_UR..E_BR` enum order.
# define SOLVE_EDGE_UR 0
# define SOLVE_EDGE_UF 1
# define SOLVE_EDGE_UL 2
# define SOLVE_EDGE_UB 3
# define SOLVE_EDGE_DR 4
# define SOLVE_EDGE_DF 5
# define SOLVE_EDGE_DL 6
# define SOLVE_EDGE_DB 7
# define SOLVE_EDGE_FR 8
# define SOLVE_EDGE_FL 9
# define SOLVE_EDGE_BL 10
# define SOLVE_EDGE_BR 11

/// Ported from rubik/include/cube.h's t_cube -- same four arrays, same
/// order, just `uint8_t` throughout instead of enum types (see this
/// file's own header comment for why).
typedef struct s_solve_cube
{
	uint8_t	corner_perm[SOLVE_CORNER_COUNT];
	uint8_t	corner_orient[SOLVE_CORNER_COUNT];
	uint8_t	edge_perm[SOLVE_EDGE_COUNT];
	uint8_t	edge_orient[SOLVE_EDGE_COUNT];
}	t_solve_cube;

/// The solved cube: every piece in its own slot, every orientation 0.
/// Ported from rubik's SOLVED_CUBE (rubik/src/cube/cubie.c).
extern const t_solve_cube	SOLVED_SOLVE_CUBE;

/// @brief Reads a `t_solve_cube` out of a generic cubic-family state
///        buffer (any buffer `shape_state_size(&CUBIC_3X3X3_SHAPE)`
///        bytes long -- in practice always `puzzle_state_ptr()` on an
///        n==3 puzzle). An explicit byte-by-byte copy, not a pointer
///        reinterpret, so nothing here depends on struct-layout
///        assumptions surviving a future compiler or flag change.
void	solve_cube_from_state(t_solve_cube *cube, const uint8_t *state);

/// @brief The reverse of solve_cube_from_state() -- writes `cube` back
///        into a generic n==3 state buffer.
void	solve_cube_to_state(const t_solve_cube *cube, uint8_t *state);

/// @brief True if both cubes have exactly the same state. Ported from
///        rubik's cube_equal().
bool	solve_cube_equal(const t_solve_cube *a, const t_solve_cube *b);

/// @brief True if `cube` equals SOLVED_SOLVE_CUBE. Ported from rubik's
///        cube_is_solved().
bool	solve_cube_is_solved(const t_solve_cube *cube);

/// @brief Applies one move (solve/movetable.h's own flat numbering) to
///        `cube` in place: a thin convenience over
///        solve_cube_to_state()/apply_n3_move()/solve_cube_from_state()
///        for callers that apply moves one at a time while reading the
///        cube's own perm/orient arrays in between (solve/layer.c's
///        stage logic -- Kociemba's and Thistlethwaite's own IDA*
///        searches apply a whole phase's moves at once instead, so they
///        round-trip the buffer directly rather than through this
///        per-move wrapper). Declared here, not solve/movetable.h: this
///        is a t_solve_cube-level operation, the same level as every
///        other function in this file.
void	solve_cube_apply_move(t_solve_cube *cube, int move);

/// @brief Sum of all corner twists, mod 3 (0 on a valid cube). Ported
///        from rubik's cube_corner_twist_sum().
int		solve_cube_corner_twist_sum(const t_solve_cube *cube);

/// @brief Sum of all edge flips, mod 2 (0 on a valid cube). Ported from
///        rubik's cube_edge_flip_sum().
int		solve_cube_edge_flip_sum(const t_solve_cube *cube);

/// @brief True if the cube could come from a real, physically turned
///        cube: twist sum 0 mod 3, flip sum 0 mod 2, and corner/edge
///        permutation parity agree. Ported from rubik's cube_is_valid().
///        Assumes corner_perm/edge_perm are each a real permutation
///        (every piece exactly once) -- not checked here, same as the
///        port's own source.
bool	solve_cube_is_valid(const t_solve_cube *cube);

#endif
