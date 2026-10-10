#ifndef SOLVE_MOVETABLE_H
# define SOLVE_MOVETABLE_H

# include <stdbool.h>
# include <stdint.h>
# include "solve/cube_view.h"

/// Ported from rubik/include/movetable.h -- see solve/coord.h's header
/// comment on the porting approach. One real adaptation: rubik's own
/// table builder turns a real t_cube with its own apply_move()
/// (rubik/src/cube/moves.c's FACE_TABLES); this build instead turns a
/// state buffer with `core/src/puzzles/cubic.c`'s ALREADY-VALIDATED
/// CUBIC_3X3X3_GENERATOR_TABLE (via shape_apply_cycle()), so the 18
/// moves these tables are built from and the 18 moves
/// puzzle_apply_move() turns the visible cube with are provably the
/// same code, not two independently-ported copies of the same face
/// tables that could silently drift apart (CLAUDE.md: "no duplicated
/// logic"). See solve/movetable.c's build_table().
///
/// A move table answers "if the cube is in coordinate state N and I turn
/// move M, which state is it in afterwards?" with one array lookup:
/// table[N * MOVE_COUNT + M].

/// The 18 legal moves: 6 faces x {clockwise, 180, counter-clockwise}, in
/// rubik's own flat numbering and order (rubik/include/cube.h's t_move:
/// face * 3 + turn, faces U R F D L B) -- the exact numbering
/// `core/src/puzzles/cubic.c`'s CUBIC_GEN_U..CUBIC_GEN_B (face) and
/// amount 1/2/3 (turn + 1) already use for n == 3, so a value here and a
/// generic t_move{generator, amount} convert with no lookup table, only
/// `face = move / 3`, `amount = move % 3 + 1` and back -- see
/// solve/kociemba.c's move_to_generic()/move_from_generic(). Named here
/// (not left as bare integers) so MOVES_PHASE2 below is checkable by eye
/// against rubik's own, rather than a transcription a reader has to trust.
typedef enum e_solve_move
{
	SOLVE_U1, SOLVE_U2, SOLVE_U3,
	SOLVE_R1, SOLVE_R2, SOLVE_R3,
	SOLVE_F1, SOLVE_F2, SOLVE_F3,
	SOLVE_D1, SOLVE_D2, SOLVE_D3,
	SOLVE_L1, SOLVE_L2, SOLVE_L3,
	SOLVE_B1, SOLVE_B2, SOLVE_B3,
	SOLVE_MOVE_COUNT
}	t_solve_move;

# define MOVE_COUNT SOLVE_MOVE_COUNT

/// A SENTINEL for "no real answer in this cell" (eperm/sperm columns for
/// the 8 moves outside MOVES_PHASE2 -- see coord.h's own doc comment on
/// why those coordinates only mean something once phase 1 is done).
/// Real coordinates never reach 65535, so it can never be mistaken for
/// one.
# define MT_ILLEGAL 0xFFFF

/// A set of moves packed into one number: bit m is 1 when move m is in
/// the set.
typedef uint32_t	t_move_mask;

# define MOVE_BIT(m) (1u << (m))
# define MOVES_ALL ((1u << MOVE_COUNT) - 1)

/// The 10 moves phase 2 may use: any turn of U/D, only the 180 of
/// R/F/L/B -- exactly the moves that keep phase 1's goal (twist 0,
/// flip 0, slice 0) true. Ported from rubik's MOVES_PHASE2
/// (rubik/include/movetable.h) -- same 10 moves, same reasoning.
# define MOVES_PHASE2 ( \
	MOVE_BIT(SOLVE_U1) | MOVE_BIT(SOLVE_U2) | MOVE_BIT(SOLVE_U3) \
	| MOVE_BIT(SOLVE_D1) | MOVE_BIT(SOLVE_D2) | MOVE_BIT(SOLVE_D3) \
	| MOVE_BIT(SOLVE_R2) | MOVE_BIT(SOLVE_F2) \
	| MOVE_BIT(SOLVE_L2) | MOVE_BIT(SOLVE_B2))

/// Thistlethwaite's own phase 2 search mask (solve/thistlethwaite.h,
/// docs/en/06-phase2-solvers.md): G1's 14 moves. Any U, D, L or R turn
/// is safe -- none of them ever flip an edge. Only F and B are locked
/// to half-turn: a quarter F or B flips 4 edges (breaking flip == 0);
/// F2/B2 flip each of those edges twice, net zero.
# define MOVES_G1 ( \
	MOVE_BIT(SOLVE_U1) | MOVE_BIT(SOLVE_U2) | MOVE_BIT(SOLVE_U3) \
	| MOVE_BIT(SOLVE_D1) | MOVE_BIT(SOLVE_D2) | MOVE_BIT(SOLVE_D3) \
	| MOVE_BIT(SOLVE_L1) | MOVE_BIT(SOLVE_L2) | MOVE_BIT(SOLVE_L3) \
	| MOVE_BIT(SOLVE_R1) | MOVE_BIT(SOLVE_R2) | MOVE_BIT(SOLVE_R3) \
	| MOVE_BIT(SOLVE_F2) | MOVE_BIT(SOLVE_B2))

/// Thistlethwaite's own phase 4 search mask: G3's 6 half turns. A
/// strict subset of MOVES_PHASE2, so eperm/sperm already have real
/// answers for every one of these columns -- no table changes needed
/// here either.
# define MOVES_G3 ( \
	MOVE_BIT(SOLVE_U2) | MOVE_BIT(SOLVE_D2) | MOVE_BIT(SOLVE_L2) \
	| MOVE_BIT(SOLVE_R2) | MOVE_BIT(SOLVE_F2) | MOVE_BIT(SOLVE_B2))

static inline bool	move_in_mask(t_move_mask mask, int move)
{
	return ((mask & MOVE_BIT(move)) != 0);
}

/// All six coordinate move tables -- see this file's header comment.
/// twist/flip/slice/cperm: every move has a real answer. eperm/sperm:
/// only the 10 MOVES_PHASE2 columns do; the rest hold MT_ILLEGAL.
typedef struct s_move_tables
{
	uint16_t	*twist;
	uint16_t	*flip;
	uint16_t	*slice;
	uint16_t	*cperm;
	uint16_t	*eperm;
	uint16_t	*sperm;
}	t_move_tables;

static inline uint16_t	move_table_next(const uint16_t *table, uint16_t state,
	int move)
{
	return (table[state * MOVE_COUNT + move]);
}

/// @brief Builds all six tables (allocates them). Call once.
bool	move_tables_build(t_move_tables *tables);

/// @brief Frees all six tables and zeroes the struct. Safe to call twice.
void	move_tables_free(t_move_tables *tables);

/// @brief Turns a raw n==3 state buffer (shape_state_size(&CUBIC_3X3X3_SHAPE)
///        bytes) by one move in this file's own numbering (face = move / 3,
///        turn = move % 3 + 1 quarter-turns), via
///        CUBIC_3X3X3_GENERATOR_TABLE -- see this file's own header comment
///        on why this, not a ported apply_move(), is what the table builder
///        (and solve/kociemba.c, applying its own phase 1 solution before
///        starting phase 2's search) turns with.
void	apply_n3_move(uint8_t *state, int move);

#endif
