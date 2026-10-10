#ifndef SOLVE_KOCIEMBA_H
# define SOLVE_KOCIEMBA_H

# include <stdbool.h>
# include "solve/cube_view.h"
# include "solve/movetable.h"
# include "solve/prune.h"
# include "solve/ida.h"

/// Ported from rubik/include/solve.h -- Kociemba's two-phase method.
/// See solve/coord.h's header comment for the overall porting approach.
///
/// One deliberate change from the port: rubik's own solve() runs
/// simplify_moves() across the phase 1/phase 2 seam (the last phase 1
/// move and first phase 2 move can merge or cancel), trimming the
/// result by at most one move. This adapter skips that: the registry
/// (docs/en/03-architecture.md S4) wants every output move tagged with
/// which phase produced it, and a seam-merged move has no single honest
/// phase to belong to. Within one phase there is nothing to simplify --
/// ida_move_is_redundant() already keeps the search itself from ever
/// producing a same-face-twice waste -- so this costs at most one extra
/// move on the rare solve where the seam would have merged, against
/// always-unambiguous phase tags. Matches the project's own stated
/// priority (01-scope-and-roles.md: "what matters is watching them, not
/// raw speed"), so simplify_moves() is ported (solve/kociemba.c) but not
/// called here; it's available if a later use wants raw move-count back.

# define KOCIEMBA_MAX_MOVES (IDA_PHASE1_CAP + IDA_PHASE2_CAP)

/// Everything the solver needs, built once at startup. phase1 and phase2
/// point INTO this struct, so build it in place and never copy it.
typedef struct s_kociemba_solver
{
	t_move_tables	moves;
	t_prune_tables	prune;
	t_ida_phase		phase1;
	t_ida_phase		phase2;
}	t_kociemba_solver;

/// @brief Builds all the tables. Call once.
bool	kociemba_init(t_kociemba_solver *solver);

/// @brief Frees the tables. Safe to call twice.
void	kociemba_free(t_kociemba_solver *solver);

/// @brief Shortens a move list in place by merging/cancelling moves on
///        the same or opposite face. Ported from rubik's
///        simplify_moves() (include/solve.h); not used by
///        kociemba_solve() below -- see this file's header comment.
int		simplify_moves(int *moves, int count);

/// @brief Solves a cube in two phases.
/// @param out           Buffer of KOCIEMBA_MAX_MOVES ints.
/// @param phase1_count  Set to how many of the returned moves are phase
///                      1 (the rest are phase 2) -- the phase-boundary
///                      info the registry adapter (solve/registry.c)
///                      turns into phase_tags.
/// @return Move count (0 if already solved), or -1 if `cube` is not
///         valid or no solution was found within the depth caps.
int		kociemba_solve(const t_kociemba_solver *solver,
			const t_solve_cube *cube, int *out, int *phase1_count);

#endif
