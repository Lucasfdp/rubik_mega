#ifndef SOLVE_SKEWB_OPTIMAL_H
# define SOLVE_SKEWB_OPTIMAL_H

# include <stdint.h>
# include "solve/coord_table.h"
# include "puzzles/skewb.h"

/// Optimal Skewb solver (docs/en/09-phase3-skewb-pyraminx.md D3-A, with
/// D1-A's frame correction): a full depth table over the 3,149,280
/// positions the four canonical turns reach, plus a 12-entry frame
/// table that lets it solve a state reached by ALL EIGHT turns.
///
/// Why a frame is needed at all. The 4 even corners only ever twist
/// under the canonical turns; the other 4 turns (the far halves)
/// permute them. A state reached with those is a canonical-group state
/// plus a whole-puzzle rotation -- one of 12, the rotations of a
/// tetrahedron -- and "solved" means the faces are uniform, not that the
/// state is the identity. So: find the rotation that puts the even
/// corners back in their own slots, solve the remainder with the table,
/// and rename each move's corner through the same rotation. See
/// skewb_solve() for the algebra.

/// A rotation of the whole puzzle is reached by two turns (a 120-degree
/// one: both halves of one cut, turned the same way) or four (a
/// 180-degree one: two 120-degree rotations about different axes).
# define SKEWB_ROTATION_MAX_WORD 4

/// The tetrahedral rotation group, which is every whole-puzzle rotation
/// the eight turns can make.
# define SKEWB_ROTATION_COUNT 12

/// Every (corner, amount) choice a rotation word's letter may take.
# define SKEWB_MOVE_CHOICES (SKEWB_CORNER_COUNT * SKEWB_STEPS)

/// Largest solution the canonical turns need: the God's number of the
/// group under this metric (DECISIONS.md records the measured value).
# define SKEWB_MAX_MOVES 16

/// Byte size of a Skewb state: a permutation byte and an orientation
/// byte for each of the 14 pieces.
# define SKEWB_STATE_BYTES (2 * (SKEWB_CORNER_COUNT + SKEWB_CENTRE_COUNT))

/// One whole-puzzle rotation, as the shortest sequence of turns found
/// that makes it, plus where it sends each corner slot.
///
/// - word, length:  the turns, in the order to apply them.
/// - corner_perm:   the corner permutation bytes of the SOLVED puzzle
///                  after the word (perm[slot] = the piece now there).
///                  skewb_solve() reads it to rename a move's corner.
typedef struct s_skewb_rotation
{
	uint32_t	length;
	t_move		word[SKEWB_ROTATION_MAX_WORD];
	uint8_t		corner_perm[SKEWB_CORNER_COUNT];
}	t_skewb_rotation;

typedef struct s_skewb_solver
{
	t_coord_table		table;
	t_skewb_rotation	rotations[SKEWB_ROTATION_COUNT];
	uint32_t			rotation_count;
}	t_skewb_solver;

/// @brief Builds the depth table (a second or so, ~3 MB) and finds the
///        12 rotations.
/// @return 1 on success, 0 on allocation failure or if the rotation
///         group does not come out at exactly 12.
int			skewb_solver_init(t_skewb_solver *solver);

/// @brief True if every face of `state` shows one colour -- the Skewb's
///        notion of solved, which the identity state satisfies but so
///        do 11 whole-puzzle rotations of it.
int			skewb_state_is_uniform(const uint8_t *state);

/// @brief Finds the whole-puzzle rotation that puts `state`'s even
///        corners back in their own slots and writes the rotated state
///        to `framed`. Exactly one of the 12 does this for any
///        reachable state, and what is left is in the four-turn group
///        the depth tables are built over. Shared by every Skewb
///        solver: skewb_solve() and the staged solver both work in the
///        framed state and rename their moves back (skewb_unframe_move).
/// @return The rotation's index into `solver->rotations`, or -1 if no
///         rotation does (a state no turns can reach).
int			skewb_find_frame(const t_skewb_solver *solver,
				const uint8_t *state, uint8_t *framed);

/// @brief Renames a move found in the framed state (a canonical turn)
///        into the move that does the same job on the real puzzle:
///        rotation `frame`'s corner_perm applied to the move's corner,
///        the amount unchanged. The algebra is in skewb_solve()'s
///        comment.
t_move		skewb_unframe_move(const t_skewb_solver *solver, int frame,
				t_move canonical);

/// @brief Writes a shortest solution of `state`, using only the four
///        canonical turns, to `out`; applying it leaves every face one
///        colour.
/// @return The number of moves, or -1 if `state` cannot be reached from
///         solved by any turns or the solution exceeds `max_moves`.
int			skewb_solve(const t_skewb_solver *solver, const uint8_t *state,
				t_move *out, uint32_t max_moves);

#endif
