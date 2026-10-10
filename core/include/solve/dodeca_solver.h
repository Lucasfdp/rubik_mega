#ifndef SOLVE_DODECA_SOLVER_H
# define SOLVE_DODECA_SOLVER_H

# include <stdint.h>
# include "move.h"
# include "solve/dodeca_macros.h"

/// Megaminx and Kilominx solver (docs/en/11-phase4-dodecahedral.md D6,
/// Tier 2: correct, not optimal -- it says so in the algorithm's name).
///
/// Pieces are placed one at a time, in slot order, by the 3-cycle
/// elements of solve/dodeca_macros.h: to put piece k home, pick the
/// element that carries it there (and sets its twist right in the same
/// step) while leaving every earlier piece where it is. If the piece is
/// already home but twisted, two elements on the same three slots do it.
/// Every choice is the shortest of all candidates, and every step is
/// checked by applying the element to a scratch copy first and by
/// comparing the real result with that prediction afterwards, so a wrong
/// element can only make the solve fail loudly, never return a wrong
/// answer.
///
/// Phases (the tags web/ turns into names, ui/phase-names.ts):
///   0  corners  all twenty, position and twist together (the whole
///               solve, for a Kilominx)
///   1  edges    all thirty, position and flip together; the 3-cycles
///               used here are pure -- they leave the corners alone
///
/// A Megaminx's centres never move, so there is nothing to solve there.
# define DODECA_PHASE_CORNERS 0
# define DODECA_PHASE_EDGES 1
# define DODECA_PHASE_COUNT 2

/// Caps on a solution's length, measured over 200 random scrambles per
/// puzzle in tests/test_dodeca_solve.c with a wide margin above the worst
/// seen. These are measurements, not proofs: the solve itself fails
/// loudly (-1) if it would exceed `max_moves`.
# define DODECA_KILOMINX_MAX_MOVES 800
# define DODECA_MEGAMINX_MAX_MOVES 2400

/// The two element tables. Corners are built from the Kilominx family
/// (their words are identical on a Megaminx, which has the same corner
/// cycles); edges from the Megaminx's. Each is built on first use, so a
/// Kilominx solve never pays for the edge table.
typedef struct s_dodeca_solver
{
	t_dodeca_table	corners;
	t_dodeca_table	edges;
	int				corners_ready;
	int				edges_ready;
}	t_dodeca_solver;

/// One piece type's slice of a state buffer, copied out so candidate
/// elements can be tried on it without touching the real puzzle:
/// perm[s] is the piece in slot s, orient[s] its twist.
typedef struct s_type_view
{
	uint8_t	perm[DODECA_SLOT_MAX];
	uint8_t	orient[DODECA_SLOT_MAX];
}	t_type_view;

/// The best way found so far to finish one placement step: one element,
/// or two applied in order (`second` NULL for one), and their combined
/// word length. `length` is UINT32_MAX until something is found.
typedef struct s_step_choice
{
	const t_dodeca_node	*first;
	const t_dodeca_node	*second;
	uint32_t			length;
}	t_step_choice;

/// The solution being written: output buffers, how many moves are in
/// them, and the cap. Moves are merged as they are added (a turn of a
/// face right after another turn of the same face of the same phase is
/// one turn, or none if they cancel).
typedef struct s_solve_run
{
	t_move		*out;
	uint32_t	*phase_tags;
	uint32_t	count;
	uint32_t	max;
}	t_solve_run;

/// @brief Builds the corner table if it is not built yet.
/// @return 1 if it is ready, 0 on failure.
int			dodeca_solver_ready_corners(t_dodeca_solver *solver);

/// @brief Builds the edge table if it is not built yet.
/// @return 1 if it is ready, 0 on failure.
int			dodeca_solver_ready_edges(t_dodeca_solver *solver);

/// @brief Writes a solution of `state` (a puzzle of `size`, 2 or 3) to
///        `out`; applying it leaves the puzzle solved. `phase_tags[i]` is
///        the phase of move i; tags never decrease.
/// @return The move count, or -1 if `state` is not a state the turns
///         can reach, the tables cannot be built, or the solution would
///         exceed `max_moves`.
int			dodeca_solve(t_dodeca_solver *solver, uint32_t size,
				const uint8_t *state, t_move *out, uint32_t max_moves,
				uint32_t *phase_tags);

#endif
