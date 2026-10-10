#ifndef SOLVE_IDA_H
# define SOLVE_IDA_H

# include <stdbool.h>
# include <stdint.h>
# include "solve/cube_view.h"
# include "solve/coord.h"
# include "solve/prune.h"

/// Ported from rubik/include/ida.h -- unmodified (pure coordinate
/// search, see solve/coord.h's header comment).
///
/// IDA* ("iterative deepening A*"): try every move sequence depth-first
/// up to a budget B, raising B by 1 and retrying until a solution is
/// found. The first one found is shortest, because every smaller budget
/// already failed. The search never touches a real cube -- a state is
/// three coordinates, a move is a move-table lookup, and the estimate
/// comes from two pruning tables.

# define IDA_MAX_DEPTH 20
# define IDA_PHASE1_CAP 12
# define IDA_PHASE2_CAP 18

/// Everything that makes one phase different from another: which three
/// coordinates, which two pruning tables (always the pairs (x, z) and
/// (y, z)), which moves, and the depth cap.
typedef struct s_ida_phase
{
	const uint16_t		*move_table[3];
	const t_prune_table	*prune_xz;
	const t_prune_table	*prune_yz;
	t_move_mask			moves;
	int					max_depth;
}	t_ida_phase;

/// Internal to ida.c: what one search remembers while it recurses.
typedef struct s_ida_ctx
{
	const t_ida_phase	*phase;
	int					*path;
	int					bound;
	int					length;
}	t_ida_ctx;

void	ida_phase1_setup(t_ida_phase *phase, const t_move_tables *moves,
			const t_prune_tables *prune);
void	ida_phase2_setup(t_ida_phase *phase, const t_move_tables *moves,
			const t_prune_tables *prune);
void	ida_phase1_start(const t_solve_cube *cube, uint16_t start[3]);
void	ida_phase2_start(const t_solve_cube *cube, uint16_t start[3]);
int		ida_estimate(const t_ida_phase *phase, const uint16_t coord[3]);

/// @brief True if `move` is a waste right after `last` -- same face
///        twice, or opposite faces in the "wrong" order (they commute,
///        so only one order is explored). Faces ordered U R F D L B.
bool	ida_move_is_redundant(int last, int move);

/// @brief Shortest move list from `start` to this phase's goal.
/// @param path Buffer of IDA_MAX_DEPTH ints; written path[0..return-1].
/// @return Move count (0 if already at goal), or -1 if none within cap.
int		ida_search(const t_ida_phase *phase, const uint16_t start[3],
			int *path);

#endif
