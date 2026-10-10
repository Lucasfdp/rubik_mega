#ifndef SOLVE_REDUCTION_H
# define SOLVE_REDUCTION_H

# include <stdint.h>
# include <stdbool.h>
# include "move.h"
# include "shape.h"

/// NxN solving by reduction -- docs/en/08-nxn-reduction.md. Order of
/// work, each a phase tag:
///
///   0 REDUCTION_PHASE_SETUP    odd n: turn the fixed middle centres
///                              home; even n: fix corner parity with
///                              one face quarter turn.
///   1 REDUCTION_PHASE_G1       Kociemba phase 1 on corners + midges
///   2 REDUCTION_PHASE_FINISH   Kociemba phase 2 (the cube's "3x3x3
///                              part" -- solved first, see run_stages()
///                              in reduction.c for why).
///   3 REDUCTION_PHASE_EDGES    every wing to its home slot (after one
///                              parity-fixing slice turn if needed).
///   4 REDUCTION_PHASE_CENTRES  every centre to a slot of its colour.
///
/// Wings and centres are placed with *conjugated 3-cycles*: S C S',
/// where C is a commutator known to cycle exactly three pieces and
/// touch nothing else, and S is a short setup that carries the three
/// pieces we want onto C's three slots. S C S' cycles exactly those
/// three, whatever S is -- so S can be any moves at all, found by a
/// tiny breadth-first search over where three pieces go. The C's are
/// found once per size by enumerating a fixed commutator family and
/// keeping only the ones a simulation proves pure; nothing about them
/// is hand-derived, so nothing about them can be hand-derived wrong.
/// (DECISIONS.md, 2026-10-04, "Reduction built" -- why this is
/// Option A's shape with search-verified templates.)
///
/// Parity never needs its own algorithm here: a wrong-parity wing orbit
/// is fixed with one quarter turn of an inner layer that touches only
/// that orbit (and centres, which are solved last), before its 3-cycles
/// run.

# define REDUCTION_PHASE_SETUP 0
# define REDUCTION_PHASE_G1 1
# define REDUCTION_PHASE_FINISH 2
# define REDUCTION_PHASE_EDGES 3
# define REDUCTION_PHASE_CENTRES 4

/// Every 3-cycle orbit has exactly 24 slots: a non-middle wing orbit
/// (the 12 wings at along-edge index k plus the 12 at n-1-k) or a
/// non-middle centre orbit (4 per face). The two smaller orbits -- the
/// 12 midges and the 6 fixed centres on odd n -- never use 3-cycles
/// (midges are the 3x3x3 stage's edges; fixed centres get turned home
/// directly).
# define RED_ORBIT_SIZE 24

/// Triples of orbit-local slots: RED_ORBIT_SIZE^3, the breadth-first
/// search's whole state space.
# define RED_TRIPLES (RED_ORBIT_SIZE * RED_ORBIT_SIZE * RED_ORBIT_SIZE)

/// Upper bound on 3-cycle orbits for any supported n (<= 8): 3 wing
/// orbits + 9 centre orbits at n == 8.
# define RED_MAX_ORBITS 16

/// Moves in one commutator X A B A' X' A B' A' (see t_red_comm).
# define RED_COMM_LENGTH 8

/// Smallest and largest size reduction is registered for (8 is
/// core/'s own largest buildable cube -- cubic.h's CUBIC_CACHE_MAX).
# define REDUCTION_MIN_N 4
# define REDUCTION_MAX_N 8

/// A commutator [X, A B A'] = X (A B A') X' (A B' A'). X is always one
/// inner-layer quarter turn; A a face turn; B any move. Stored as its
/// three defining moves; the 8-move sequence is expanded on emit.
typedef struct s_red_comm
{
	t_move	x;
	t_move	a;
	t_move	b;
}	t_red_comm;

/// One proven-pure 3-cycle within an orbit, in orbit-local slots: the
/// piece in slot p moves to q, q's to r, r's to p, and nothing else on
/// the cube changes (for a centre cycle) or nothing but centres
/// changes (for a wing cycle -- centres are solved after wings).
/// `inverted` means "emit the commutator's inverse", which runs the
/// same slots the other way round; storing both directions doubles
/// the choice of cycles at no search cost.
typedef struct s_red_cycle
{
	uint8_t		p;
	uint8_t		q;
	uint8_t		r;
	uint8_t		inverted;
	t_red_comm	comm;
}	t_red_cycle;

/// One 24-slot orbit and everything the setup search needs about it.
///
/// - piece_type: CUBIC_WING_TYPE or CUBIC_CENTRE_TYPE.
/// - slots:      local index -> real slot index.
/// - perm:       move_count * RED_ORBIT_SIZE: perm[m * 24 + i] is the
///               local slot the piece in local slot i lands on after
///               move m.
/// - colour:     centre orbits only: the face (CUBIC_GEN_*) each slot
///               sits on, which is also the colour of the piece whose
///               home that slot is.
/// - goal:       RED_TRIPLES entries: goal[(p * 24 + q) * 24 + r] is
///               the index into `cycles` of a cycle sending p->q->r,
///               or -1.
typedef struct s_red_orbit
{
	uint8_t		piece_type;
	uint8_t		slots[RED_ORBIT_SIZE];
	uint8_t		colour[RED_ORBIT_SIZE];
	uint8_t		*perm;
	int16_t		*goal;
	uint32_t	cycle_count;
	t_red_cycle	*cycles;
}	t_red_orbit;

/// Everything reduction needs for one size, built once and cached.
///
/// - moves:        every (generator, amount) pair, move_count of them
///                 -- the alphabet setups and the parity search use.
/// - slot_perm:    per piece type, move_count * piece_count: where the
///                 piece in each slot lands after each move.
/// - orbit_of:     per piece type, slot -> index into `orbits`, or
///                 RED_NO_ORBIT for a midge / fixed centre.
/// - midge_slot:   odd n only, 3x3x3 edge index (cubic.c's E_UR..E_BR
///                 order) -> the midge slot at that edge, so the reduced
///                 cube's edges read straight into a t_solve_cube.
/// - fixed_centre_moves: odd n only, the three middle-layer generators
///                 (one per axis) the fixed centres are turned home
///                 with.
typedef struct s_red_tables
{
	uint32_t				n;
	const t_shape			*shape;
	const t_generator_table	*gens;
	uint32_t				move_count;
	t_move					*moves;
	uint8_t					*slot_perm[MAX_PIECE_TYPES];
	uint8_t					*orbit_of[MAX_PIECE_TYPES];
	uint32_t				orbit_count;
	t_red_orbit				orbits[RED_MAX_ORBITS];
	uint32_t				fixed_centre_moves[3];
	uint8_t					midge_slot[12];
}	t_red_tables;

/// orbit_of value for a slot in no 3-cycle orbit.
# define RED_NO_ORBIT 0xFF

/// Biggest state buffer any supported size needs (n == 8: 8 corners,
/// 72 wings, 216 centres, perm + orient byte each = 592), rounded up.
# define RED_MAX_STATE 1024

/// Raw (pre-merge) move capacity for one solve. Comfortably above the
/// longest raw solve measured at n == 8 (DECISIONS.md records the
/// numbers); a solve that would overflow it fails cleanly
/// (UINT32_MAX), it never writes past the end.
# define RED_RAW_CAPACITY 16384

/// Working memory for one solve, allocated ONCE per process and reused:
/// the WASM build's allocator never frees (src/platform/wasm_alloc.c),
/// so a per-solve allocation would leak on every Solve click.
///
/// - parent/via/queue: the setup search's breadth-first bookkeeping
///   over orbit-slot triples.
/// - work:            the state being solved, mutated move by move.
/// - raw/raw_tags:    the move list before move_list_merge().
typedef struct s_red_scratch
{
	int32_t		parent[RED_TRIPLES];
	int16_t		via[RED_TRIPLES];
	int32_t		queue[RED_TRIPLES];
	uint8_t		work[RED_MAX_STATE];
	t_move		raw[RED_RAW_CAPACITY];
	uint32_t	raw_tags[RED_RAW_CAPACITY];
}	t_red_scratch;

/// A solve in progress: tables, scratch, how many raw moves so far,
/// which phase they belong to, and whether capacity ran out.
typedef struct s_red_run
{
	const t_red_tables	*t;
	t_red_scratch		*s;
	uint32_t			count;
	uint32_t			phase;
	bool				overflow;
}	t_red_run;

/// @brief True if reduction is registered for size n.
bool				reduction_supports(uint32_t n);

/// @brief Most moves reduction_solve() can return at size n (the
///        measured worst case with headroom -- see the .c file).
uint32_t			reduction_max_moves(uint32_t n);

/// @brief This size's tables, built on first use -- NULL if n is not
///        supported or the 3-cycle library came out incomplete (every
///        orbit needs at least one cycle; test_reduction.c checks it).
const t_red_tables	*reduction_tables_for(uint32_t n);

/// @brief t_algorithm's solve() for any supported n: writes the moves
///        that solve `state` (a size-n cubic state buffer, unmodified)
///        into `out`, tagged with the REDUCTION_PHASE_* above.
/// @return Move count, or UINT32_MAX if n is unsupported, the state is
///         unsolvable, or the solution would exceed `max_moves`.
uint32_t			reduction_solve(uint32_t n, const uint8_t *state,
						t_move *out, uint32_t max_moves, uint32_t *phase_tags);

// From here down: internal to src/solve/reduction*.c, declared here
// because the work is split across two files.

/// @brief Applies `move` to `state` (size-n buffer).
void				red_apply(const t_red_tables *t, uint8_t *state,
						t_move move);

/// @brief The inverse of `move` (same generator, amount steps+1-amount).
t_move				red_inverse(const t_red_tables *t, t_move move);

/// @brief Writes commutator `c` (or its inverse) as RED_COMM_LENGTH
///        moves into `out`.
void				red_expand_comm(const t_red_tables *t, t_red_comm c,
						bool inverted, t_move *out);

#endif
