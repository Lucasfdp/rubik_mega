#ifndef PUZZLES_CUBIC_LATTICE_H
# define PUZZLES_CUBIC_LATTICE_H

# include <stdint.h>
# include "move.h"

/// Which of the cubic lattice's three orthogonal axes a face's turn
/// spins around. This file is the whole "coordinate math instead of
/// hand-written or codegen'd tables" engine docs/en/05-phase1-nxn.md S4
/// (Option A) describes, kept cubic-specific -- not under src/shape/ --
/// because a 3-orthogonal-axis integer lattice has no meaning for the
/// dodecahedral/tetrahedral/Square-1 families; there is nothing generic
/// to share with them.
typedef enum e_cubic_axis
{
	CUBIC_AXIS_X,
	CUBIC_AXIS_Y,
	CUBIC_AXIS_Z
}	t_cubic_axis;

/// One lattice cell on the NxNxN grid, each coordinate in [0, n - 1].
/// Used only while building cycles in cubic_lattice.c -- never part of
/// a puzzle's own state (shape.h's permutation/orientation bytes are
/// the only state that exists at runtime). Declared here, not in the
/// .c file, per Lucas's personal coding norm: struct definitions always
/// live in a header, even for a type used in exactly one translation
/// unit.
typedef struct s_cubic_point
{
	int	x;
	int	y;
	int	z;
}	t_cubic_point;

/// @brief How many wing pieces sit along one of the 12 edges, summed
///        over all 12, on an NxNxN cube: 12 * (n - 2). Zero for n == 3
///        (where edge positions stay CUBIC_EDGE_TYPE, cubic.c's own
///        existing hand-written piece type -- untouched).
uint32_t	cubic_wing_count(uint32_t n);

/// @brief How many centre pieces fill all 6 faces' inner grids, on an
///        NxNxN cube: 6 * (n - 2)^2. Note this is NOT zero at n == 3
///        (it's 6, one per face) even though the existing 3x3x3 shape
///        tracks no centre piece type at all -- correctly so, since a
///        3x3x3's one-centre-per-face never permutes relative to the
///        others under any move (a well-known fact about the physical
///        cube: the 6 centres are fixed to the core), so there is
///        nothing to track. cubic.c never calls this for n == 3.
uint32_t	cubic_centre_count(uint32_t n);

/// @brief The last valid inner-layer depth for a size-n cube: 0 is the
///        face's own outer layer, up to and including the true middle
///        layer for odd n (shared between a face and its own opposite
///        -- see cubic.c's "Why the middle layer can appear twice"
///        comment for why that overlap is harmless, not a bug).
uint32_t	cubic_max_depth(uint32_t n);

/// @brief Lattice coordinate of wing slot `index` (`wing` true) or
///        centre slot `index` (`wing` false) on a size-n cube -- the
///        same canonical slot order cubic_lattice_build_layer() numbers
///        its cycles by, so callers never re-derive that order
///        themselves. Allocates and walks the whole lattice per call:
///        fine for tests and one-off setup, not for a per-frame path.
t_cubic_point	cubic_lattice_point(uint32_t n, int wing, uint32_t index);

/// @brief Builds every wing and every centre cycle for one quarter-turn
///        of one (axis, sign, depth) layer on a size-n cubic lattice,
///        writing into caller-owned buffers. Corners are NOT produced
///        here: cubic.c reuses its own existing hand-written,
///        rubik-ported corner cycles unchanged for every n, since a
///        corner's behaviour under a face's own depth-0 turn does not
///        depend on the cube's size at all -- only wings and centres
///        are new per docs/en/05-phase1-nxn.md S1.
///
/// @param sign +1 if this face sits at the HIGH end of `axis` (depth 0
///        is coordinate n - 1), -1 if it sits at the LOW end (depth 0
///        is coordinate 0). Also sets the turn direction: every layer
///        turns clockwise as seen from THIS face, matching cubic.c's
///        hand-written corner cycles, so a D-side inner slice turns
///        like D, not like U.
/// @param wing_piece_type, centre_piece_type  the shape's own piece-type
///        indices (CUBIC_WING_TYPE, CUBIC_CENTRE_TYPE in cubic.h) --
///        passed in rather than included from cubic.h, so this file
///        never needs to know cubic.c's own constants.
///        Wing orientation deltas are derived from geometry per cycle
///        step (rubik's U/D-then-F/B edge convention, applied per wing
///        -- see the .c file's wing_delta()); centres never change
///        orientation (orientations == 1, untracked, DECISIONS.md
///        "Big-cube centre orientation").
/// @param wing_cycles, centre_cycles  buffers sized by the caller to at
///        least (their own piece count / 4) + 1 entries -- no cycle is
///        shorter than 4 pieces once it exists at all (a face-turn
///        rotation on this lattice has order 4, and a point's orbit
///        under it is always length 1 -- skipped entirely, see the .c
///        file -- or length 4, never 2 or 3), so that bound can never
///        be exceeded.
/// @return via out params: how many cycles were written to each buffer.
void	cubic_lattice_build_layer(uint32_t n, t_cubic_axis axis, int sign,
			uint32_t depth, uint8_t wing_piece_type,
			uint8_t centre_piece_type,
			t_cycle *wing_cycles, uint32_t *wing_cycle_count,
			t_cycle *centre_cycles, uint32_t *centre_cycle_count);

#endif
