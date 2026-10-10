#include "solve/layer.h"
#include "solve/movetable.h"

/// Ported from rubik/src/solve/layer.c. Every table below is copied
/// unmodified (only the naming convention changes: rubik's MOVE_xx ->
/// this project's SOLVE_xx, same numbers, same faces -- solve/movetable.h's
/// own header comment on why the two numbering schemes already agree
/// with no lookup table needed; rubik's CORNER_xx/EDGE_xx piece ids ->
/// SOLVE_CORNER_xx/SOLVE_EDGE_xx, same order and numbering, confirmed
/// by solve/cube_view.h's own header comment). These are pure
/// combinatorics over the corner/edge perm+orient model -- which struct
/// holds the arrays doesn't change a single move in any table, exactly
/// the reasoning solve/coord.h's header comment already gives for why
/// this whole directory is a near-verbatim port, not a rewrite.
///
/// Beginner (layer-by-layer) method: white cross, first-layer corners,
/// second-layer edges, then the last layer, in that order. Every stage
/// after the first works the same way: locate the one piece this step
/// cares about, pop it out of the way if something is sitting where it
/// needs to go, align it above its slot with U turns, then apply one
/// short fixed algorithm -- never a search, exactly the lookup-table
/// approach a person follows by hand. All of stages 1-3 stay off the D
/// face entirely, so nothing already placed can be disturbed by a later
/// step. Every table below was found by exhaustive search against the
/// real move tables in rubik (not copied from a reference), then
/// independently verified there to leave every already-placed piece
/// untouched before being trusted here -- see rubik's own
/// src/solve/layer.c for the full derivation history, including a real
/// bug that verification pass caught (an early twist-0 first-layer
/// corner insert and the second-layer edge inserts only checked that
/// the CURRENT piece landed correctly, and missed checking that
/// already-placed pieces survived).

/// One short, fixed move sequence, looked up by table index. 8 slots is
/// enough for every table below (the longest entry is 7 moves).
typedef struct s_seq
{
	int	mv[8];
	int	len;
}	t_seq;

/// D-edge slots (the cross), U-edge slots directly above them, and
/// middle-edge slots, in a consistent axis order: 0=R-ish, 1=F-ish,
/// 2=L-ish, 3=B-ish for the cross/last-layer axes, and FR/FL/BL/BR for
/// the middle edges. Matches solve/cube_view.h's own SOLVE_EDGE_* order,
/// so slot - SOLVE_EDGE_DR / slot - SOLVE_EDGE_FR recovers the axis
/// directly.
static const int	DE[4] = {SOLVE_EDGE_DR, SOLVE_EDGE_DF, SOLVE_EDGE_DL,
	SOLVE_EDGE_DB};
static const int	UE[4] = {SOLVE_EDGE_UR, SOLVE_EDGE_UF, SOLVE_EDGE_UL,
	SOLVE_EDGE_UB};
static const int	ME[4] = {SOLVE_EDGE_FR, SOLVE_EDGE_FL, SOLVE_EDGE_BL,
	SOLVE_EDGE_BR};

/// D-corner slots (first-layer corners) and the U-corner slots directly
/// above them. Same enum-order trick as the edges above.
static const int	DC[4] = {SOLVE_CORNER_DFR, SOLVE_CORNER_DLF,
	SOLVE_CORNER_DBL, SOLVE_CORNER_DRB};

/// @brief One quarter-turn-doubled per D-edge axis: R2/F2/L2/B2. A half
///        turn of any face never changes net edge orientation, so this
///        is its own safe, generic "pop this D-edge slot's occupant up
///        to U, touch nothing else" AND, applied again from the aligned
///        U slot with the piece already correctly oriented, its own
///        "insert" -- one move either way, verified positional
///        (independent of which piece is actually sitting there).
static const int	EDGE_POP[4] = {SOLVE_R2, SOLVE_F2, SOLVE_L2, SOLVE_B2};

/// @brief Cross-edge insertion when the piece is aligned above its
///        target D-edge slot but flipped (flip == 1). Verified against
///        the real move tables: preserves every other already-placed
///        cross edge.
static const t_seq	CROSS_FLIP[4] = {
	{{SOLVE_U1, SOLVE_F1, SOLVE_R3, SOLVE_F3}, 4},
	{{SOLVE_U1, SOLVE_L1, SOLVE_F3, SOLVE_L3}, 4},
	{{SOLVE_U1, SOLVE_B1, SOLVE_L3, SOLVE_B3}, 4},
	{{SOLVE_U1, SOLVE_R1, SOLVE_B3, SOLVE_R3}, 4},
};

/// @brief Pops whatever corner is sitting in first-layer slot DC[axis]
///        up to the U layer, wherever it lands, leaving the other three
///        D-corner slots and all four D-edges untouched. Verified
///        positional: it does not matter whether the piece there is
///        misplaced, or is DC[axis]'s own piece sitting at the wrong
///        twist -- the effect on every OTHER slot is identical either
///        way, so one table serves both cases.
static const t_seq	CORNER_POP[4] = {
	{{SOLVE_R1, SOLVE_U1, SOLVE_R3}, 3},
	{{SOLVE_F1, SOLVE_U1, SOLVE_F3}, 3},
	{{SOLVE_L1, SOLVE_U1, SOLVE_L3}, 3},
	{{SOLVE_R3, SOLVE_U1, SOLVE_R1}, 3},
};

/// @brief Inserts a first-layer corner already aligned above its target
///        slot, indexed [target axis][current twist]. Verified: leaves
///        every already-solved cross edge AND every already-placed
///        first-layer corner untouched.
static const t_seq	CORNER_INSERT[4][3] = {
	{
		{{SOLVE_R1, SOLVE_F1, SOLVE_R2, SOLVE_F3, SOLVE_R3}, 5},
		{{SOLVE_R1, SOLVE_U1, SOLVE_R3}, 3},
		{{SOLVE_F3, SOLVE_U3, SOLVE_F1}, 3},
	},
	{
		{{SOLVE_F1, SOLVE_R1, SOLVE_U2, SOLVE_R3, SOLVE_F3}, 5},
		{{SOLVE_F1, SOLVE_U1, SOLVE_F3}, 3},
		{{SOLVE_L3, SOLVE_U3, SOLVE_L1}, 3},
	},
	{
		{{SOLVE_L1, SOLVE_F1, SOLVE_U2, SOLVE_F3, SOLVE_L3}, 5},
		{{SOLVE_L1, SOLVE_U1, SOLVE_L3}, 3},
		{{SOLVE_B3, SOLVE_U3, SOLVE_B1}, 3},
	},
	{
		{{SOLVE_R2, SOLVE_U3, SOLVE_R2, SOLVE_U1, SOLVE_R2}, 5},
		{{SOLVE_B1, SOLVE_U1, SOLVE_B3}, 3},
		{{SOLVE_R3, SOLVE_U3, SOLVE_R1}, 3},
	},
};

/// @brief Pops whatever middle edge is sitting in slot ME[axis] up to
///        the U layer, leaving all four D-edges, all four D-corners, and
///        the other three middle-edge slots untouched. Only needed once
///        first-layer corners are already done (a middle-edge piece can
///        never end up in a D-edge slot at this stage, since those are
///        already correct).
static const t_seq	MID_POP[4] = {
	{{SOLVE_R1, SOLVE_U1, SOLVE_R3, SOLVE_U3, SOLVE_F3, SOLVE_U3, SOLVE_F1},
		7},
	{{SOLVE_R3, SOLVE_F2, SOLVE_U3, SOLVE_F1, SOLVE_U1, SOLVE_F2, SOLVE_R1},
		7},
	{{SOLVE_R1, SOLVE_B2, SOLVE_U1, SOLVE_B1, SOLVE_U3, SOLVE_B2, SOLVE_R3},
		7},
	{{SOLVE_R1, SOLVE_U2, SOLVE_F1, SOLVE_R1, SOLVE_F3, SOLVE_U2, SOLVE_R3},
		7},
};

/// @brief Inserts a middle edge already aligned above its target slot's
///        own axis (UE[axis]), indexed [target axis][current flip].
///        Verified: leaves the cross and first-layer corners, and every
///        already-placed middle edge, untouched -- these are the full
///        7-move edge-insert triggers (the "R U R' U' F' U F" family).
static const t_seq	MID_INSERT[4][2] = {
	{
		{{SOLVE_R3, SOLVE_U2, SOLVE_B3, SOLVE_R3, SOLVE_B1, SOLVE_U2,
			SOLVE_R1}, 7},
		{{SOLVE_R1, SOLVE_U2, SOLVE_R3, SOLVE_U2, SOLVE_F3, SOLVE_U3,
			SOLVE_F1}, 7},
	},
	{
		{{SOLVE_F1, SOLVE_U2, SOLVE_R1, SOLVE_U3, SOLVE_R3, SOLVE_U2,
			SOLVE_F3}, 7},
		{{SOLVE_R3, SOLVE_F2, SOLVE_U3, SOLVE_F3, SOLVE_U1, SOLVE_F2,
			SOLVE_R1}, 7},
	},
	{
		{{SOLVE_F3, SOLVE_L2, SOLVE_U3, SOLVE_L3, SOLVE_U1, SOLVE_L2,
			SOLVE_F1}, 7},
		{{SOLVE_R1, SOLVE_B2, SOLVE_U1, SOLVE_B3, SOLVE_U3, SOLVE_B2,
			SOLVE_R3}, 7},
	},
	{
		{{SOLVE_F1, SOLVE_R2, SOLVE_U1, SOLVE_R3, SOLVE_U3, SOLVE_R2,
			SOLVE_F3}, 7},
		{{SOLVE_R1, SOLVE_U2, SOLVE_F1, SOLVE_R3, SOLVE_F3, SOLVE_U2,
			SOLVE_R3}, 7},
	},
};

/// One U turn per aligning rotation, indexed by how many quarter turns
/// (0-3) are needed; index 0 means "already aligned, nothing to do".
static const int	AUF[4] = {SOLVE_U1, SOLVE_U1, SOLVE_U2, SOLVE_U3};

static int	find_corner(const t_solve_cube *cube, int piece)
{
	int	slot;

	slot = 0;
	while (slot < SOLVE_CORNER_COUNT && cube->corner_perm[slot] != piece)
		slot++;
	return (slot);
}

static int	find_edge(const t_solve_cube *cube, int piece)
{
	int	slot;

	slot = 0;
	while (slot < SOLVE_EDGE_COUNT && cube->edge_perm[slot] != piece)
		slot++;
	return (slot);
}

/// @brief Applies one move to `cube` and records it into `out`, unless
///        that would overflow LAYER_MAX_MOVES.
/// @return false on overflow (nothing further should be attempted).
static bool	push(t_solve_cube *cube, int *out, int *total, int move)
{
	if (*total >= LAYER_MAX_MOVES)
		return (false);
	solve_cube_apply_move(cube, move);
	out[*total] = move;
	(*total)++;
	return (true);
}

static bool	push_seq(t_solve_cube *cube, int *out, int *total,
	const t_seq *seq)
{
	int	i;

	i = 0;
	while (i < seq->len)
	{
		if (!push(cube, out, total, seq->mv[i]))
			return (false);
		i++;
	}
	return (true);
}

/// @brief Rotates the U face (0-3 quarter turns) until `piece` sits at
///        `target`, one of the four U slots directly above a first-
///        layer/cross/middle-edge target. `piece` must already be
///        somewhere in the U layer when this is called.
static bool	align_edge(t_solve_cube *cube, int *out, int *total,
	int target, int piece)
{
	int				k;
	t_solve_cube	probe;

	k = 0;
	while (k < 4)
	{
		probe = *cube;
		if (k > 0)
			solve_cube_apply_move(&probe, AUF[k]);
		if (probe.edge_perm[target] == piece)
			break ;
		k++;
	}
	if (k == 0)
		return (true);
	return (push(cube, out, total, AUF[k]));
}

static bool	align_corner(t_solve_cube *cube, int *out, int *total,
	int target, int piece)
{
	int				k;
	t_solve_cube	probe;

	k = 0;
	while (k < 4)
	{
		probe = *cube;
		if (k > 0)
			solve_cube_apply_move(&probe, AUF[k]);
		if (probe.corner_perm[target] == piece)
			break ;
		k++;
	}
	if (k == 0)
		return (true);
	return (push(cube, out, total, AUF[k]));
}

/// @brief Solves the white cross: places D-edge DE[axis] with flip 0,
///        without disturbing any cross edge already placed.
static bool	place_cross_edge(t_solve_cube *cube, int *out, int *total,
	int axis)
{
	int	piece;
	int	slot;

	piece = DE[axis];
	slot = find_edge(cube, piece);
	if (slot == DE[axis] && cube->edge_orient[slot] == 0)
		return (true);
	if (slot >= SOLVE_EDGE_DR && slot <= SOLVE_EDGE_DB)
	{
		if (!push(cube, out, total, EDGE_POP[slot - SOLVE_EDGE_DR]))
			return (false);
		slot = find_edge(cube, piece);
	}
	else if (slot >= SOLVE_EDGE_FR)
	{
		if (!push_seq(cube, out, total, &MID_POP[slot - SOLVE_EDGE_FR]))
			return (false);
		slot = find_edge(cube, piece);
	}
	if (!align_edge(cube, out, total, UE[axis], piece))
		return (false);
	if (cube->edge_orient[UE[axis]] == 0)
		return (push(cube, out, total, EDGE_POP[axis]));
	return (push_seq(cube, out, total, &CROSS_FLIP[axis]));
}

/// @brief Solves one first-layer corner: places D-corner DC[axis] with
///        twist 0, without disturbing the cross or any first-layer
///        corner already placed.
static bool	place_corner(t_solve_cube *cube, int *out, int *total,
	int axis)
{
	int	piece;
	int	slot;
	int	twist;

	piece = DC[axis];
	slot = find_corner(cube, piece);
	if (slot == DC[axis] && cube->corner_orient[slot] == 0)
		return (true);
	if (slot >= SOLVE_CORNER_DFR)
	{
		if (!push_seq(cube, out, total, &CORNER_POP[slot - SOLVE_CORNER_DFR]))
			return (false);
		slot = find_corner(cube, piece);
	}
	if (!align_corner(cube, out, total, SOLVE_CORNER_URF + axis, piece))
		return (false);
	twist = cube->corner_orient[SOLVE_CORNER_URF + axis];
	return (push_seq(cube, out, total, &CORNER_INSERT[axis][twist]));
}

/// @brief Solves one second-layer (middle) edge: places ME[axis] with
///        flip 0, without disturbing the cross, first-layer corners, or
///        any middle edge already placed.
static bool	place_mid_edge(t_solve_cube *cube, int *out, int *total,
	int axis)
{
	int	piece;
	int	slot;
	int	flip;

	piece = ME[axis];
	slot = find_edge(cube, piece);
	if (slot == ME[axis] && cube->edge_orient[slot] == 0)
		return (true);
	if (slot >= SOLVE_EDGE_FR)
	{
		if (!push_seq(cube, out, total, &MID_POP[slot - SOLVE_EDGE_FR]))
			return (false);
		slot = find_edge(cube, piece);
	}
	if (!align_edge(cube, out, total, UE[axis], piece))
		return (false);
	flip = cube->edge_orient[UE[axis]];
	return (push_seq(cube, out, total, &MID_INSERT[axis][flip]));
}

/// @brief Finishes whatever remains of the last layer with the already-
///        built Kociemba solver (layer.h: not owned or freed here). By
///        this point the cross, first layer, and second layer are
///        solved, so this is always a last-layer-only finish -- but
///        kociemba_solve() is handed the whole cube, exactly like
///        solve/registry.c's own kociemba_algorithm_solve(), never told
///        that.
static bool	finish_last_layer(t_solve_cube *cube,
	const t_kociemba_solver *kociemba, int *out, int *total)
{
	int	solution[KOCIEMBA_MAX_MOVES];
	int	phase1;
	int	count;
	int	i;

	count = kociemba_solve(kociemba, cube, solution, &phase1);
	if (count < 0)
		return (false);
	i = 0;
	while (i < count)
	{
		if (!push(cube, out, total, solution[i]))
			return (false);
		i++;
	}
	return (true);
}

int	layer_solve(const t_solve_cube *cube, const t_kociemba_solver *kociemba,
	int *out, int phase_bounds[4])
{
	t_solve_cube	cur;
	int				total;
	int				i;

	if (!solve_cube_is_valid(cube))
		return (-1);
	cur = *cube;
	total = 0;
	i = 0;
	while (i < 4)
	{
		if (!place_cross_edge(&cur, out, &total, i))
			return (-1);
		i++;
	}
	phase_bounds[0] = total;
	i = 0;
	while (i < 4)
	{
		if (!place_corner(&cur, out, &total, i))
			return (-1);
		i++;
	}
	phase_bounds[1] = total;
	i = 0;
	while (i < 4)
	{
		if (!place_mid_edge(&cur, out, &total, i))
			return (-1);
		i++;
	}
	phase_bounds[2] = total;
	if (!solve_cube_is_solved(&cur)
		&& !finish_last_layer(&cur, kociemba, out, &total))
		return (-1);
	phase_bounds[3] = total;
	return (total);
}
