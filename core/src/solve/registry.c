#include <stddef.h>
#include <stdint.h>
#include "solve/registry.h"
#include "solve/cube_view.h"
#include "solve/kociemba.h"
#include "solve/thistlethwaite.h"
#include "solve/layer.h"
#include "solve/two_by_two.h"
#include "solve/reduction.h"
#include "solve/skewb_optimal.h"
#include "solve/pyraminx_optimal.h"
#include "solve/skewb_staged.h"
#include "solve/pyraminx_staged.h"
#include "solve/dodeca_solver.h"
#include "solve/square1_solver.h"
#include "puzzles/cubic.h"

/// Built once, on first use, and kept for the rest of the process's
/// life -- same "build once, reuse" contract move.h's t_generator_table
/// doc comment already states for move tables, now extended to the
/// (much bigger) coordinate/move/pruning tables Kociemba needs. Not
/// freed: a browser tab's own lifetime bounds it, same reasoning
/// src/platform/wasm_alloc.c's own header comment gives for never
/// calling a real free() in the WASM build.
static t_kociemba_solver	g_kociemba;
static int					g_kociemba_ready = 0;

static int	ensure_kociemba(void)
{
	if (!g_kociemba_ready)
		g_kociemba_ready = kociemba_init(&g_kociemba);
	return (g_kociemba_ready);
}

/// Same "build once, reuse" contract as g_kociemba above. Owns no move
/// tables of its own -- thistle_init() is passed g_kociemba's
/// (solve/thistlethwaite.h: "does NOT own moves"), so Kociemba's own
/// solver is always built first; the two never duplicate the ~3 MB of
/// move tables between them.
static t_thistle_solver	g_thistle;
static int					g_thistle_ready = 0;

static int	ensure_thistle(void)
{
	if (!g_thistle_ready)
	{
		if (!ensure_kociemba())
			return (0);
		g_thistle_ready = thistle_init(&g_thistle, &g_kociemba.moves);
	}
	return (g_thistle_ready);
}

/// Same "build once, reuse" contract as g_kociemba/g_thistle above.
/// Shares g_kociemba's own move tables too (solve/two_by_two.h: "does
/// NOT own moves") -- its own tables are just the two small 1-D prune
/// tables twist_prune/cperm_prune.
static t_two_solver	g_two;
static int				g_two_ready = 0;

static int	ensure_two(void)
{
	if (!g_two_ready)
	{
		if (!ensure_kociemba())
			return (0);
		g_two_ready = two_solver_init(&g_two, &g_kociemba.moves);
	}
	return (g_two_ready);
}

/// @brief t_algorithm's solve() shape, for Kociemba. Converts the
///        generic state buffer to/from t_solve_cube and this directory's
///        own int-numbered moves to/from the generic t_move{generator,
///        amount} -- the one place those two representations meet (see
///        solve/movetable.h's own header comment on why the numbering
///        lines up with no lookup table needed).
static uint32_t	kociemba_algorithm_solve(const uint8_t *state, uint32_t size,
	t_move *out, uint32_t max_moves, uint32_t *phase_tags)
{
	t_solve_cube	cube;
	int				moves[KOCIEMBA_MAX_MOVES];
	int				phase1;
	int				count;
	int				i;

	(void)size;
	if (!ensure_kociemba())
		return (UINT32_MAX);
	solve_cube_from_state(&cube, state);
	count = kociemba_solve(&g_kociemba, &cube, moves, &phase1);
	if (count < 0 || (uint32_t)count > max_moves)
		return (UINT32_MAX);
	i = 0;
	while (i < count)
	{
		out[i].generator = (int16_t)cubic_generator_index(3,
				(uint32_t)(moves[i] / 3), 0);
		out[i].amount = (int8_t)(moves[i] % 3 + 1);
		phase_tags[i] = (i < phase1) ? 0u : 1u;
		i++;
	}
	return ((uint32_t)count);
}

/// @brief t_algorithm's solve() shape, for Thistlethwaite. Same
///        state-buffer / move-numbering conversion as
///        kociemba_algorithm_solve() above; phase_bounds (four
///        cumulative move counts, one per phase -- thistle_solve()'s
///        own doc comment) is turned into phase_tags 0-3 instead of
///        kociemba's single 0/1 split.
static uint32_t	thistle_algorithm_solve(const uint8_t *state, uint32_t size,
	t_move *out, uint32_t max_moves, uint32_t *phase_tags)
{
	t_solve_cube	cube;
	int				moves[THISTLE_MAX_MOVES];
	int				phase_bounds[4];
	int				count;
	uint32_t		phase;
	uint32_t		i;

	(void)size;
	if (!ensure_thistle())
		return (UINT32_MAX);
	solve_cube_from_state(&cube, state);
	count = thistle_solve(&g_thistle, &cube, moves, phase_bounds);
	if (count < 0 || (uint32_t)count > max_moves)
		return (UINT32_MAX);
	i = 0;
	while (i < (uint32_t)count)
	{
		out[i].generator = (int16_t)cubic_generator_index(3,
				(uint32_t)(moves[i] / 3), 0);
		out[i].amount = (int8_t)(moves[i] % 3 + 1);
		phase = 0;
		while (phase < 3 && (int)i >= phase_bounds[phase])
			phase++;
		phase_tags[i] = phase;
		i++;
	}
	return ((uint32_t)count);
}

/// @brief t_algorithm's solve() shape, for the beginner (layer-by-
///        layer) method. Same conversion pattern as the other two
///        algorithms above; phase_bounds (four cumulative move counts,
///        one per stage -- layer_solve()'s own doc comment) becomes
///        phase_tags 0-3, same scheme Thistlethwaite's adapter uses,
///        just with these four stages instead of those four phases:
///        0 = cross, 1 = first-layer corners, 2 = second-layer edges,
///        3 = last layer.
static uint32_t	layer_algorithm_solve(const uint8_t *state, uint32_t size,
	t_move *out, uint32_t max_moves, uint32_t *phase_tags)
{
	t_solve_cube	cube;
	int				moves[LAYER_MAX_MOVES];
	int				phase_bounds[4];
	int				count;
	uint32_t		phase;
	uint32_t		i;

	(void)size;
	if (!ensure_kociemba())
		return (UINT32_MAX);
	solve_cube_from_state(&cube, state);
	count = layer_solve(&cube, &g_kociemba, moves, phase_bounds);
	if (count < 0 || (uint32_t)count > max_moves)
		return (UINT32_MAX);
	i = 0;
	while (i < (uint32_t)count)
	{
		out[i].generator = (int16_t)cubic_generator_index(3,
				(uint32_t)(moves[i] / 3), 0);
		out[i].amount = (int8_t)(moves[i] % 3 + 1);
		phase = 0;
		while (phase < 3 && (int)i >= phase_bounds[phase])
			phase++;
		phase_tags[i] = phase;
		i++;
	}
	return ((uint32_t)count);
}

/// @brief Reads a 2x2x2 state buffer (corner perm then corner orient,
///        the first 16 bytes of the 3x3x3 layout) into `cube`, leaving
///        its edges solved -- the solver never reads them.
static void	copy_corners_from_state(t_solve_cube *cube, const uint8_t *state)
{
	uint32_t	i;

	i = 0;
	while (i < SOLVE_CORNER_COUNT)
	{
		cube->corner_perm[i] = state[i];
		cube->corner_orient[i] = state[SOLVE_CORNER_COUNT + i];
		i++;
	}
}

/// @brief t_algorithm's solve() shape, for the 2x2x2 (corners-only)
///        method. Same conversion pattern as the other three algorithms
///        above, but a single IDA* search with no stage/phase split
///        (solve/two_by_two.h), so every phase_tags entry is 0 -- there
///        is only one phase to tag.
static uint32_t	two_algorithm_solve(const uint8_t *state, uint32_t size,
	t_move *out, uint32_t max_moves, uint32_t *phase_tags)
{
	t_solve_cube	cube;
	int				moves[TWOBYTWO_MAX_MOVES];
	int				count;
	uint32_t		i;

	(void)size;
	if (!ensure_two())
		return (UINT32_MAX);
	cube = SOLVED_SOLVE_CUBE;
	copy_corners_from_state(&cube, state);
	count = two_solve(&g_two, &cube, moves);
	if (count < 0 || (uint32_t)count > max_moves)
		return (UINT32_MAX);
	i = 0;
	while (i < (uint32_t)count)
	{
		out[i].generator = (int16_t)cubic_generator_index(3,
				(uint32_t)(moves[i] / 3), 0);
		out[i].amount = (int8_t)(moves[i] % 3 + 1);
		phase_tags[i] = 0u;
		i++;
	}
	return ((uint32_t)count);
}

/// @brief t_algorithm's solve() shape, for NxN reduction
///        (docs/en/08-nxn-reduction.md) -- solve/reduction.h does all
///        the work; this only exists so the registry has one uniform
///        entry type.
static uint32_t	reduction_algorithm_solve(const uint8_t *state,
	uint32_t size, t_move *out, uint32_t max_moves, uint32_t *phase_tags)
{
	return (reduction_solve(size, state, out, max_moves, phase_tags));
}

/// Same "build once, reuse" contract as g_kociemba above, for the Skewb
/// depth table (~3 MB, built on the first Skewb solve -- a user who
/// never picks Skewb never pays for it).
static t_skewb_solver	g_skewb;
static int				g_skewb_ready = 0;

static int	ensure_skewb(void)
{
	if (!g_skewb_ready)
		g_skewb_ready = skewb_solver_init(&g_skewb);
	return (g_skewb_ready);
}

/// Same contract, for the Pyraminx depth table (~1 MB).
static t_pyraminx_solver	g_pyraminx;
static int					g_pyraminx_ready = 0;

static int	ensure_pyraminx(void)
{
	if (!g_pyraminx_ready)
		g_pyraminx_ready = pyraminx_solver_init(&g_pyraminx);
	return (g_pyraminx_ready);
}

/// @brief t_algorithm's solve() shape, for the Skewb optimal solver.
///        skewb_solve() already speaks t_move, so this only adds the
///        single phase tag (0 -- one optimal pass, nothing to
///        separate) and the UINT32_MAX failure convention.
static uint32_t	skewb_algorithm_solve(const uint8_t *state, uint32_t size,
	t_move *out, uint32_t max_moves, uint32_t *phase_tags)
{
	int	count;
	int	i;

	(void)size;
	if (!ensure_skewb())
		return (UINT32_MAX);
	count = skewb_solve(&g_skewb, state, out, max_moves);
	if (count < 0)
		return (UINT32_MAX);
	i = 0;
	while (i < count)
		phase_tags[i++] = 0u;
	return ((uint32_t)count);
}

/// @brief t_algorithm's solve() shape, for the Pyraminx optimal solver.
///        Two phases: 0 for the edge/axial moves (layer turns), 1 for
///        the tip turns that finish.
static uint32_t	pyraminx_algorithm_solve(const uint8_t *state, uint32_t size,
	t_move *out, uint32_t max_moves, uint32_t *phase_tags)
{
	uint32_t	layer_moves;
	int			count;
	int			i;

	(void)size;
	if (!ensure_pyraminx())
		return (UINT32_MAX);
	count = pyraminx_solve(&g_pyraminx, state, out, max_moves, &layer_moves);
	if (count < 0)
		return (UINT32_MAX);
	i = 0;
	while (i < count)
	{
		phase_tags[i] = ((uint32_t)i < layer_moves) ? 0u : 1u;
		i++;
	}
	return ((uint32_t)count);
}

/// Same contract again, for the staged solvers' small stage tables
/// (docs/en/10-phase3-human-methods.md). Each reuses its family's
/// optimal solver for the final stage, so that is built first and the
/// big table is never duplicated.
static t_skewb_staged	g_skewb_staged;
static int				g_skewb_staged_ready = 0;

static int	ensure_skewb_staged(void)
{
	if (!g_skewb_staged_ready)
	{
		if (!ensure_skewb())
			return (0);
		g_skewb_staged_ready = skewb_staged_init(&g_skewb_staged, &g_skewb);
	}
	return (g_skewb_staged_ready);
}

static t_pyraminx_staged	g_pyraminx_staged;
static int					g_pyraminx_staged_ready = 0;

static int	ensure_pyraminx_staged(void)
{
	if (!g_pyraminx_staged_ready)
	{
		if (!ensure_pyraminx())
			return (0);
		g_pyraminx_staged_ready = pyraminx_staged_init(&g_pyraminx_staged,
				&g_pyraminx);
	}
	return (g_pyraminx_staged_ready);
}

/// Turns a staged solver's cumulative stage ends into per-move phase
/// tags: move i is in the first stage whose end lies beyond i. Stages
/// that needed no moves (equal ends) simply never appear, and the tags
/// never decrease. Shared by both staged adapters.
static void	tags_from_stage_ends(const uint32_t *stage_ends,
	uint32_t stage_count, uint32_t move_count, uint32_t *phase_tags)
{
	uint32_t	stage;
	uint32_t	i;

	stage = 0;
	i = 0;
	while (i < move_count)
	{
		while (stage + 1 < stage_count && i >= stage_ends[stage])
			stage++;
		phase_tags[i++] = stage;
	}
}

/// @brief t_algorithm's solve() shape, for the staged Skewb solver:
///        phase tags 0 (first-face corners), 1 (first-face centre),
///        2 (finish).
static uint32_t	skewb_staged_algorithm_solve(const uint8_t *state,
	uint32_t size, t_move *out, uint32_t max_moves, uint32_t *phase_tags)
{
	uint32_t	stage_ends[SKEWB_STAGE_COUNT];
	int			count;

	(void)size;
	if (!ensure_skewb_staged())
		return (UINT32_MAX);
	count = skewb_staged_solve(&g_skewb_staged, state, out, max_moves,
			stage_ends);
	if (count < 0)
		return (UINT32_MAX);
	tags_from_stage_ends(stage_ends, SKEWB_STAGE_COUNT, (uint32_t)count,
		phase_tags);
	return ((uint32_t)count);
}

/// @brief t_algorithm's solve() shape, for the staged Pyraminx solver:
///        phase tags 0 (axial pieces), 1 (first-layer edges), 2 (finish
///        edges and axials), 3 (tips).
static uint32_t	pyraminx_staged_algorithm_solve(const uint8_t *state,
	uint32_t size, t_move *out, uint32_t max_moves, uint32_t *phase_tags)
{
	uint32_t	stage_ends[PYRAMINX_STAGE_COUNT];
	int			count;

	(void)size;
	if (!ensure_pyraminx_staged())
		return (UINT32_MAX);
	count = pyraminx_staged_solve(&g_pyraminx_staged, state, out, max_moves,
			stage_ends);
	if (count < 0)
		return (UINT32_MAX);
	tags_from_stage_ends(stage_ends, PYRAMINX_STAGE_COUNT, (uint32_t)count,
		phase_tags);
	return ((uint32_t)count);
}

/// Same "build once, reuse" contract as g_kociemba above, for the
/// Megaminx/Kilominx 3-cycle element tables (solve/dodeca_macros.h): the
/// corner table (~0.8 MB) on the first solve of either puzzle, the edge
/// table on the first Megaminx solve. Static, not malloc'd: they are
/// big, fixed-size and would otherwise eat the WASM arena's headroom.
static t_dodeca_solver	g_dodeca;

/// @brief t_algorithm's solve() shape for Megaminx and Kilominx.
///        `size` picks the puzzle (2 or 3); phase tags are 0 (corners)
///        and, for a Megaminx, 1 (edges).
static uint32_t	dodeca_algorithm_solve(const uint8_t *state, uint32_t size,
	t_move *out, uint32_t max_moves, uint32_t *phase_tags)
{
	int	count;

	count = dodeca_solve(&g_dodeca, size, state, out, max_moves, phase_tags);
	if (count < 0)
		return (UINT32_MAX);
	return ((uint32_t)count);
}

/// Square-1's tables (shape distances, phase 2 move and prune tables,
/// about 0.7 MB) are built once, on the first solve, and kept: same "build
/// once, reuse" contract as g_dodeca above, and static for the same reason.
static t_sq1_solver	g_square1;

/// @brief t_algorithm's solve() shape for Square-1's two-phase solver.
///        Phase tags are SQ1_PHASE_CUBE_SHAPE (0) and SQ1_PHASE_FINISH (1).
static uint32_t	square1_algorithm_solve(const uint8_t *state, uint32_t size,
	t_move *out, uint32_t max_moves, uint32_t *phase_tags)
{
	int	count;

	(void)size;
	if (!g_square1.ready && !sq1_solver_init(&g_square1))
		return (UINT32_MAX);
	count = sq1_solver_solve(&g_square1, state, out, max_moves, phase_tags,
			NULL);
	if (count < 0)
		return (UINT32_MAX);
	return ((uint32_t)count);
}

/// Names are what ui/phase-names.ts keys its phase table by -- change
/// one, change the other (a test in web/ catches drift).
static const t_algorithm	SQUARE1_ALGORITHMS[] = {
	{"Two-phase (near-optimal)", square1_algorithm_solve},
};

# define SQUARE1_ALGORITHM_COUNT \
	(sizeof(SQUARE1_ALGORITHMS) / sizeof(SQUARE1_ALGORITHMS[0]))

/// Names are what ui/phase-names.ts keys its phase table by -- change
/// one, change the other (a test in web/ catches drift).
static const t_algorithm	KILOMINX_ALGORITHMS[] = {
	{"Corner 3-cycles (not optimal)", dodeca_algorithm_solve},
};

static const t_algorithm	MEGAMINX_ALGORITHMS[] = {
	{"Corner then edge 3-cycles (not optimal)", dodeca_algorithm_solve},
};

# define KILOMINX_ALGORITHM_COUNT \
	(sizeof(KILOMINX_ALGORITHMS) / sizeof(KILOMINX_ALGORITHMS[0]))
# define MEGAMINX_ALGORITHM_COUNT \
	(sizeof(MEGAMINX_ALGORITHMS) / sizeof(MEGAMINX_ALGORITHMS[0]))

static const t_algorithm	CUBIC_N3_ALGORITHMS[] = {
	{"Kociemba (two-phase)", kociemba_algorithm_solve},
	{"Thistlethwaite (four-phase)", thistle_algorithm_solve},
	{"Layer-by-layer (beginner)", layer_algorithm_solve},
};

# define CUBIC_N3_ALGORITHM_COUNT \
	(sizeof(CUBIC_N3_ALGORITHMS) / sizeof(CUBIC_N3_ALGORITHMS[0]))

/// A real 2x2x2 (n == 2): its corners-only solver, now that the puzzle
/// itself exists. It used to be offered on the 3x3x3, where it left the
/// edges scrambled.
static const t_algorithm	CUBIC_N2_ALGORITHMS[] = {
	{"2x2x2 (corners only)", two_algorithm_solve},
};

# define CUBIC_N2_ALGORITHM_COUNT \
	(sizeof(CUBIC_N2_ALGORITHMS) / sizeof(CUBIC_N2_ALGORITHMS[0]))

/// Every n >= 4 gets exactly one algorithm: reduction to a 3x3x3.
static const t_algorithm	CUBIC_NXN_ALGORITHMS[] = {
	{"Reduction (corners, edges, centres)", reduction_algorithm_solve},
};

# define CUBIC_NXN_ALGORITHM_COUNT \
	(sizeof(CUBIC_NXN_ALGORITHMS) / sizeof(CUBIC_NXN_ALGORITHMS[0]))

const t_kociemba_solver	*registry_kociemba(void)
{
	if (!ensure_kociemba())
		return (NULL);
	return (&g_kociemba);
}

uint32_t	cubic_algorithm_count(uint32_t n)
{
	if (n == CUBIC_MIN_SIZE)
		return (CUBIC_N2_ALGORITHM_COUNT);
	if (n == 3)
		return (CUBIC_N3_ALGORITHM_COUNT);
	if (reduction_supports(n))
		return (CUBIC_NXN_ALGORITHM_COUNT);
	return (0);
}

const t_algorithm	*cubic_algorithm(uint32_t n, uint32_t id)
{
	if (id >= cubic_algorithm_count(n))
		return (NULL);
	if (n == CUBIC_MIN_SIZE)
		return (&CUBIC_N2_ALGORITHMS[id]);
	if (n == 3)
		return (&CUBIC_N3_ALGORITHMS[id]);
	return (&CUBIC_NXN_ALGORITHMS[id]);
}

uint32_t	cubic_solve_max_moves(uint32_t n)
{
	if (n == CUBIC_MIN_SIZE)
		return (TWOBYTWO_MAX_MOVES);
	if (n == 3)
		return (CUBIC_N3_MAX_MOVES);
	if (reduction_supports(n))
		return (reduction_max_moves(n));
	return (0);
}

/// Names are what ui/phase-names.ts keys its phase table by -- change
/// one, change the other (a test in web/ catches drift).
static const t_algorithm	SKEWB_ALGORITHMS[] = {
	{"Optimal (full depth table)", skewb_algorithm_solve},
	{"Face-first (staged)", skewb_staged_algorithm_solve},
};

static const t_algorithm	PYRAMINX_ALGORITHMS[] = {
	{"Optimal, then tips (full depth table)", pyraminx_algorithm_solve},
	{"Layer-first (staged)", pyraminx_staged_algorithm_solve},
};

# define SKEWB_ALGORITHM_COUNT \
	(sizeof(SKEWB_ALGORITHMS) / sizeof(SKEWB_ALGORITHMS[0]))
# define PYRAMINX_ALGORITHM_COUNT \
	(sizeof(PYRAMINX_ALGORITHMS) / sizeof(PYRAMINX_ALGORITHMS[0]))

uint32_t	registry_algorithm_count(t_puzzle_type type, uint32_t size)
{
	if (type == PUZZLE_CUBIC)
		return (cubic_algorithm_count(size));
	if (type == PUZZLE_SKEWB)
		return (SKEWB_ALGORITHM_COUNT);
	if (type == PUZZLE_TETRAHEDRAL)
		return (PYRAMINX_ALGORITHM_COUNT);
	if (type == PUZZLE_DODECAHEDRAL && size == DODECA_KILOMINX_SIZE)
		return (KILOMINX_ALGORITHM_COUNT);
	if (type == PUZZLE_DODECAHEDRAL && size == DODECA_MEGAMINX_SIZE)
		return (MEGAMINX_ALGORITHM_COUNT);
	if (type == PUZZLE_SQUARE1)
		return (SQUARE1_ALGORITHM_COUNT);
	return (0);
}

const t_algorithm	*registry_algorithm(t_puzzle_type type, uint32_t size,
	uint32_t id)
{
	if (id >= registry_algorithm_count(type, size))
		return (NULL);
	if (type == PUZZLE_CUBIC)
		return (cubic_algorithm(size, id));
	if (type == PUZZLE_SKEWB)
		return (&SKEWB_ALGORITHMS[id]);
	if (type == PUZZLE_SQUARE1)
		return (&SQUARE1_ALGORITHMS[id]);
	if (type == PUZZLE_DODECAHEDRAL)
		return (size == DODECA_KILOMINX_SIZE ? &KILOMINX_ALGORITHMS[id]
			: &MEGAMINX_ALGORITHMS[id]);
	return (&PYRAMINX_ALGORITHMS[id]);
}

/// For Skewb and Pyraminx the cap is the staged solver's: the sum of its
/// stage depths, which exceeds the optimal solver's (SKEWB_MAX_MOVES,
/// PYRAMINX_MAX_MOVES) -- the tests assert it does, so one cap serves
/// every algorithm of the family.
uint32_t	registry_solve_max_moves(t_puzzle_type type, uint32_t size)
{
	if (type == PUZZLE_CUBIC)
		return (cubic_solve_max_moves(size));
	if (type == PUZZLE_SKEWB)
		return (SKEWB_STAGED_MAX_MOVES);
	if (type == PUZZLE_TETRAHEDRAL)
		return (PYRAMINX_STAGED_MAX_MOVES);
	if (type == PUZZLE_DODECAHEDRAL && size == DODECA_KILOMINX_SIZE)
		return (DODECA_KILOMINX_MAX_MOVES);
	if (type == PUZZLE_DODECAHEDRAL && size == DODECA_MEGAMINX_SIZE)
		return (DODECA_MEGAMINX_MAX_MOVES);
	if (type == PUZZLE_SQUARE1)
		return (SQ1_SOLVE_MAX_MOVES);
	return (0);
}
