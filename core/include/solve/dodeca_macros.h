#ifndef SOLVE_DODECA_MACROS_H
# define SOLVE_DODECA_MACROS_H

# include <stdint.h>
# include "move.h"
# include "shape.h"
# include "cut_geometry.h"
# include "puzzles/dodecahedral.h"

/// Megaminx and Kilominx solving blocks (docs/en/11-phase4-dodecahedral.md
/// D6, built a little differently from how that spec sketched it -- see
/// DECISIONS.md, "Phase 4 increment 5").
///
/// The one idea: a **3-cycle element** moves exactly three pieces of one
/// type along a cycle, adding a fixed twist to each, and touches nothing
/// else. If C is such an element and S is ANY move sequence, then
/// S C S' (apply S, then C, then S undone) is again a 3-cycle element,
/// on whichever three slots S carries C's three onto, with the twists
/// redistributed by S's own twisting (the redistribution always sums to
/// zero, so the element stays legal). The set of all S C S' over every
/// S is closed under "conjugate by one more turn" -- so a breadth-first
/// search over elements, not over move sequences, enumerates every one of
/// them exactly once: every 3-cycle of corners (or edges) with every
/// legal twist pattern, each with the shortest S that makes it. There
/// are 20 * 19 * 18 / 3 = 2,280 corner cycles with 9 twist patterns each
/// (3 twists, summing to 0 mod 3), and 8,120 edge cycles with 4 flip
/// patterns each (flips summing to 0 mod 2).
///
/// Nothing is typed by hand: the root C comes from a small search over a
/// fixed family of commutators that keeps only sequences a simulation
/// proves are pure (dodeca_find_root), exactly as NxN reduction does for
/// its commutators (solve/reduction.h).
///
/// What the solver then needs is no pure twist or flip macro at all: a
/// piece that has to be both moved and twisted is moved by a 3-cycle
/// element whose twist pattern also twists it right, so orientation is
/// fixed in the same step as position.

/// Every move the puzzle has: 12 faces x 4 amounts. A move index is
/// face * DODECA_STEPS + (amount - 1); index and (face, amount) convert
/// with dodeca_move().
# define DODECA_MOVE_COUNT (DODECA_FACE_COUNT * DODECA_STEPS)

/// The most slots one piece type has (the Megaminx's 30 edges); slot
/// arrays are sized by it.
# define DODECA_SLOT_MAX DODECA_EDGE_COUNT

/// A Megaminx's state buffer: 20 corners x 2 bytes, 30 edges x 2, 12
/// centres x 2 (a permutation byte and an orientation byte each). The
/// largest of the family, so scratch states are sized by it.
# define DODECA_STATE_MAX 124

/// How many 3-cycle elements each type has -- the number of ordered
/// cycles (n * (n - 1) * (n - 2) / 3: three rotations of the same cycle
/// are one element) times twist patterns (orientations ^ 2: the third
/// twist is whatever makes the sum zero). tests/test_dodeca_solve.c
/// asserts the search finds exactly this many, which is the proof the
/// root's conjugacy class is the whole set.
# define DODECA_CORNER_ELEMENTS (20 * 19 * 18 / 3 * 3 * 3)
# define DODECA_EDGE_ELEMENTS (30 * 29 * 28 / 3 * 2 * 2)

/// Elements are looked up by (cycle, twists): slots^3 * orientations^3
/// keys. Corners: 20^3 * 3^3; edges: 30^3 * 2^3; both are exactly
/// 216,000, so one index size serves both.
# define DODECA_INDEX_SIZE 216000

/// Longest root sequence the search may keep, and the longest word any
/// element can expand to (root + two moves per conjugation step). Init
/// fails rather than overflow either.
# define DODECA_ROOT_MAX 32
# define DODECA_WORD_MAX 64

/// One 3-cycle element, in the same convention as t_cycle (move.h): the
/// piece in slot[i] moves to slot[i + 1] (wrapping), and delta[i] is
/// added to the piece that LANDS in slot[i]. Canonical form: slot[0] is
/// the smallest of the three, so one element has exactly one spelling.
typedef struct s_dodeca_node
{
	uint8_t		slot[3];
	uint8_t		delta[3];
	uint16_t	parent;
	uint8_t		move;
	uint8_t		depth;
}	t_dodeca_node;

/// Sentinel `parent` of the root.
# define DODECA_NO_PARENT 0xFFFF

/// All the 3-cycle elements of one piece type, with how to build each.
///
/// - dest/twist: where move m sends the piece that starts in slot s, and
///               what it adds to that piece's orientation -- the move
///               model reduced to one type, read once from the real
///               generators (never retyped).
/// - root:       the sequence the search found for the element nodes[0].
/// - nodes:      every element, breadth-first from the root: an element
///               at depth d is the conjugate of its parent by `move`,
///               and its word is root conjugated d times.
/// - index:      key -> node index + 1 (0 = absent), for O(1) lookup.
typedef struct s_dodeca_table
{
	const t_shape			*shape;
	const t_generator_table	*gens;
	uint32_t				type;
	uint32_t				slots;
	uint32_t				orients;
	uint8_t					dest[DODECA_MOVE_COUNT][DODECA_SLOT_MAX];
	uint8_t					twist[DODECA_MOVE_COUNT][DODECA_SLOT_MAX];
	t_move					root[DODECA_ROOT_MAX];
	uint32_t				root_len;
	uint32_t				node_count;
	uint32_t				max_depth;
	t_dodeca_node			nodes[DODECA_EDGE_ELEMENTS];
	uint16_t				index[DODECA_INDEX_SIZE];
}	t_dodeca_table;

/// @brief The move with index `m` (0..DODECA_MOVE_COUNT-1) as a t_move.
t_move		dodeca_move(uint32_t m);

/// @brief The move that undoes move index `m` (same face, amount
///        DODECA_STEPS + 1 - amount, as a move index).
uint32_t	dodeca_move_inverse(uint32_t m);

/// @brief Finds the root element for piece type `type` of `family` --
///        the shortest sequence in a fixed search family that cycles
///        exactly three pieces of `type`, leaves every earlier piece
///        type exactly where it was (corners, when `type` is the
///        edges), and is proved so by simulation -- then builds the
///        whole table by breadth-first conjugation.
/// @param family  the puzzle family to simulate on and apply words to
///                (the Kilominx's for corners, the Megaminx's for edges)
/// @return 1 on success; 0 if no root exists in the family, the table
///         does not reach the expected element count, or a word would
///         exceed DODECA_WORD_MAX.
int			dodeca_table_init(t_dodeca_table *table,
				const t_cut_family *family, uint32_t type);

/// @brief Reads the effect of `state` (the result of applying some
///        sequence to a solved puzzle) on piece type `table->type` as a
///        3-cycle element.
/// @return 1 and the element in `out` (parent/move/depth left unset) if
///         `state` moves exactly three pieces of that type along a
///         single cycle and leaves every earlier type solved; 0 if it
///         does anything else. This is the purity check the root search
///         trusts, exposed so tests apply the same one to every word.
int			dodeca_extract_element(const t_dodeca_table *table,
				const uint8_t *state, t_dodeca_node *out);

/// @brief The node for the cycle `slot[0] -> slot[1] -> slot[2]` with
///        landing twists `delta[]` (any rotation of the three: it is
///        canonicalised), or NULL if no such element exists -- the
///        slots repeat, a twist is out of range, or the twists do not
///        sum to zero modulo the type's orientations.
const t_dodeca_node	*dodeca_table_find(const t_dodeca_table *table,
						const uint8_t *slot, const uint8_t *delta);

/// @brief Writes element `node`'s word into `out` (up to `max` moves):
///        the conjugating moves outermost first, then the root, then
///        the conjugating moves undone. Applying it to a state changes
///        exactly the element's three pieces, as the node says.
/// @return The word's length, or UINT32_MAX if it does not fit.
uint32_t	dodeca_table_word(const t_dodeca_table *table,
				const t_dodeca_node *node, t_move *out, uint32_t max);

#endif
