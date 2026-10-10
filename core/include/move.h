#ifndef MOVE_H
# define MOVE_H

# include <stdint.h>

/// Most slots any single cycle needs: 4 for a cubic face (corners/edges),
/// 3 for a tetrahedral face (Pyraminx), 5 for a dodecahedral face
/// (Megaminx/Kilominx). Bump if a future family needs more.
# define MAX_CYCLE_LENGTH 5

/// One applied move: which turnable "thing" on the puzzle (a face at a
/// layer depth, a Megaminx face, a Square-1 layer -- see
/// docs/en/03-architecture.md S2) and how far to turn it, in that
/// puzzle's own step unit (3 states for a quarter-turn cubic face: 90,
/// 180, 270; 4 for a Megaminx face).
///
/// Replaces rubik's flat 18-value t_move enum: a puzzle with inner-slice
/// moves (4x4x4+) or a bigger step count (Megaminx) just gets a bigger
/// generator table, never a new move *kind*.
typedef struct s_move
{
	int16_t	generator;
	int8_t	amount;
}	t_move;

/// web/src/engine/puzzle.ts's solve() decodes `t_move` out of raw WASM
/// memory with a hand-written DataView stride (generator: int16 at
/// offset 0, amount: int8 at offset 2), rather than going through any
/// kind of shared layout description -- this guard makes a future
/// change to this struct (reordering, a new field, explicit packing)
/// fail the native build loudly instead of silently desyncing with
/// that JS-side assumption.
_Static_assert(sizeof(t_move) == 4, "t_move must stay 4 bytes -- web/src/engine/puzzle.ts decodes it with a fixed stride");

/// One group of pieces (all of the SAME piece type) that a single
/// quarter-step of one generator cycles through. `length` of `slots`/
/// `delta` are used; the piece in slots[i] moves to slots[i + 1] (slots
/// [length - 1] wraps to slots[0]), and delta[i] is added, modulo that
/// piece type's own orientation count, to the piece that lands in
/// slots[i]. Ported 1:1 from rubik's FACE_TABLES (src/cube/moves.c) for
/// the cubic 3x3x3 case -- see src/puzzles/cubic.c.
typedef struct s_cycle
{
	uint8_t	piece_type;
	uint8_t	length;
	uint8_t	slots[MAX_CYCLE_LENGTH];
	int8_t	delta[MAX_CYCLE_LENGTH];
}	t_cycle;

/// One quarter-step of one generator: every piece-type cycle it moves.
/// A cubic face has 2 (corners, edges); a puzzle with centres or wings
/// would have more.
typedef struct s_generator
{
	uint32_t		cycle_count;
	const t_cycle	*cycles;
}	t_generator;

/// A puzzle's full set of turnable things, built once per (puzzle type,
/// size) and reused -- see docs/en/03-architecture.md S2.
///
/// - count:      how many generators (6 faces for a 3x3x3; more once
///               inner-slice generators exist for bigger NxN).
/// - steps:      how many distinct turn amounts each generator supports
///               (3 for a cubic quarter-turn face: amount 1/2/3 = 90/
///               180/270 degrees clockwise).
/// - generators: `count` entries, each applied `amount` times for a move.
typedef struct s_generator_table
{
	uint32_t			count;
	uint32_t			steps;
	const t_generator	*generators;
}	t_generator_table;

#endif
