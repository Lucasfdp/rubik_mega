#include "platform.h"
#include "puzzles/cubic.h"

/// The 3x3x3 case of the cubic family's shape -- corners and edges,
/// exactly rubik's own CORNER_COUNT/EDGE_COUNT (8 and 12) as data
/// instead of #defines. Other NxN sizes are docs/en/04-roadmap.md
/// Phase 1, not implemented yet.
const t_shape	CUBIC_3X3X3_SHAPE = {
	.piece_type_count = 2,
	.piece_types = {
		[CUBIC_CORNER_TYPE] = { .count = 8, .orientations = 3 },
		[CUBIC_EDGE_TYPE] = { .count = 12, .orientations = 2 },
	},
};

/// Corner/edge slot indices, in rubik's own cube.h enum order --
/// URF=0 UFL=1 ULB=2 UBR=3 DFR=4 DLF=5 DBL=6 DRB=7, and
/// UR=0 UF=1 UL=2 UB=3 DR=4 DF=5 DL=6 DB=7 FR=8 FL=9 BL=10 BR=11.
/// Named here, not just numbered, so the cycle tables below read the
/// same way rubik's FACE_TABLES do.
enum
{
	C_URF, C_UFL, C_ULB, C_UBR, C_DFR, C_DLF, C_DBL, C_DRB
};

enum
{
	E_UR, E_UF, E_UL, E_UB, E_DR, E_DF, E_DL, E_DB, E_FR, E_FL, E_BL, E_BR
};

static const t_cycle	U_CYCLES[2] = {
	{ CUBIC_CORNER_TYPE, 4, {C_UBR, C_URF, C_UFL, C_ULB}, {0, 0, 0, 0} },
	{ CUBIC_EDGE_TYPE, 4, {E_UB, E_UR, E_UF, E_UL}, {0, 0, 0, 0} },
};

static const t_cycle	R_CYCLES[2] = {
	{ CUBIC_CORNER_TYPE, 4, {C_DFR, C_URF, C_UBR, C_DRB}, {1, 2, 1, 2} },
	{ CUBIC_EDGE_TYPE, 4, {E_BR, E_DR, E_FR, E_UR}, {0, 0, 0, 0} },
};

static const t_cycle	F_CYCLES[2] = {
	{ CUBIC_CORNER_TYPE, 4, {C_DFR, C_DLF, C_UFL, C_URF}, {2, 1, 2, 1} },
	{ CUBIC_EDGE_TYPE, 4, {E_DF, E_FL, E_UF, E_FR}, {1, 1, 1, 1} },
};

static const t_cycle	D_CYCLES[2] = {
	{ CUBIC_CORNER_TYPE, 4, {C_DBL, C_DLF, C_DFR, C_DRB}, {0, 0, 0, 0} },
	{ CUBIC_EDGE_TYPE, 4, {E_DB, E_DL, E_DF, E_DR}, {0, 0, 0, 0} },
};

static const t_cycle	L_CYCLES[2] = {
	{ CUBIC_CORNER_TYPE, 4, {C_DBL, C_ULB, C_UFL, C_DLF}, {1, 2, 1, 2} },
	{ CUBIC_EDGE_TYPE, 4, {E_BL, E_UL, E_FL, E_DL}, {0, 0, 0, 0} },
};

static const t_cycle	B_CYCLES[2] = {
	{ CUBIC_CORNER_TYPE, 4, {C_DBL, C_DRB, C_UBR, C_ULB}, {2, 1, 2, 1} },
	{ CUBIC_EDGE_TYPE, 4, {E_BL, E_DB, E_BR, E_UB}, {1, 1, 1, 1} },
};

static const t_generator	CUBIC_3X3X3_GENERATORS[CUBIC_FACE_COUNT] = {
	[CUBIC_GEN_U] = { 2, U_CYCLES },
	[CUBIC_GEN_R] = { 2, R_CYCLES },
	[CUBIC_GEN_F] = { 2, F_CYCLES },
	[CUBIC_GEN_D] = { 2, D_CYCLES },
	[CUBIC_GEN_L] = { 2, L_CYCLES },
	[CUBIC_GEN_B] = { 2, B_CYCLES },
};

const t_generator_table	CUBIC_3X3X3_GENERATOR_TABLE = {
	.count = CUBIC_FACE_COUNT,
	.steps = 3,
	.generators = CUBIC_3X3X3_GENERATORS,
};

/// From here down: docs/en/04-roadmap.md Phase 1 -- generalizing the
/// shape and generator table above (n == 3, hand-written, untouched) to
/// any n >= 4. See docs/en/05-phase1-nxn.md for the design this
/// implements, "Option A" (cubic_lattice.c computes wing/centre cycles
/// from coordinates -- no hand or codegen'd tables past n == 3).

/// Face axis/sign, and which existing hand-written corner cycle belongs
/// to each face -- CUBIC_FACES[CUBIC_GEN_U] is U's own entry, and so on,
/// same index space as CUBIC_GEN_U..CUBIC_GEN_B. There used to be a
/// per-face wing_flip column here (F/B = 1, others 0, copied from the
/// edge deltas above); it was only right for depth-0 face turns and is
/// gone -- cubic_lattice.c now derives every wing delta from geometry
/// (DECISIONS.md, 2026-10-04, "Wing orientation").
static const t_cubic_face	CUBIC_FACES[CUBIC_FACE_COUNT] = {
	[CUBIC_GEN_U] = {CUBIC_AXIS_Y, 1, &U_CYCLES[0]},
	[CUBIC_GEN_R] = {CUBIC_AXIS_X, 1, &R_CYCLES[0]},
	[CUBIC_GEN_F] = {CUBIC_AXIS_Z, 1, &F_CYCLES[0]},
	[CUBIC_GEN_D] = {CUBIC_AXIS_Y, -1, &D_CYCLES[0]},
	[CUBIC_GEN_L] = {CUBIC_AXIS_X, -1, &L_CYCLES[0]},
	[CUBIC_GEN_B] = {CUBIC_AXIS_Z, -1, &B_CYCLES[0]},
};

static const t_shape			*g_shape_cache[CUBIC_CACHE_MAX];
static const t_generator_table	*g_table_cache[CUBIC_CACHE_MAX];

static t_shape	*cubic_build_shape(uint32_t n)
{
	t_shape	*shape;

	shape = malloc(sizeof(*shape));
	shape->piece_type_count = 3;
	shape->piece_types[CUBIC_CORNER_TYPE] = (t_piece_type){8, 3};
	if (n == CUBIC_MIN_SIZE)
	{
		shape->piece_type_count = 1;	// a 2x2x2 is corners only
		return (shape);
	}
	shape->piece_types[CUBIC_WING_TYPE] = (t_piece_type){
		cubic_wing_count(n), 2};
	shape->piece_types[CUBIC_CENTRE_TYPE] = (t_piece_type){
		cubic_centre_count(n), 1};
	return (shape);
}

/// @brief One (face, depth) generator: the reused corner cycle (depth 0
///        only) plus this layer's wing and centre cycles, computed by
///        cubic_lattice.c. Wing/centre cycles are built into temporary,
///        generously-sized buffers (cubic_lattice_build_layer()'s own
///        doc comment on its buffer-size contract) then copied into a
///        tightly-sized final array, since t_generator.cycles is one
///        contiguous array, not a list of separately-owned pieces.
static t_generator	cubic_build_generator(uint32_t n, uint32_t face_idx,
	uint32_t depth)
{
	const t_cubic_face	*face;
	t_cycle				*wing_tmp;
	t_cycle				*centre_tmp;
	uint32_t			wing_count;
	uint32_t			centre_count;
	t_cycle				*cycles;
	uint32_t			total;
	uint32_t			i;
	uint32_t			j;

	face = &CUBIC_FACES[face_idx];
	if (n == CUBIC_MIN_SIZE)
	{
		// No wings or centres exist, so the lattice has nothing to cycle:
		// a 2x2x2 face turn is the shared corner cycle alone.
		cycles = malloc(sizeof(*cycles));
		cycles[0] = *face->corner_cycle;
		return ((t_generator){.cycle_count = 1, .cycles = cycles});
	}
	wing_tmp = malloc(sizeof(*wing_tmp) * (cubic_wing_count(n) / 4 + 1));
	centre_tmp = malloc(sizeof(*centre_tmp) * (cubic_centre_count(n) / 4 + 1));
	cubic_lattice_build_layer(n, face->axis, face->sign, depth,
		CUBIC_WING_TYPE, CUBIC_CENTRE_TYPE,
		wing_tmp, &wing_count, centre_tmp, &centre_count);
	total = (depth == 0) + wing_count + centre_count;
	cycles = malloc(sizeof(*cycles) * total);
	i = 0;
	if (depth == 0)
		cycles[i++] = *face->corner_cycle;
	j = 0;
	while (j < wing_count)
		cycles[i++] = wing_tmp[j++];
	j = 0;
	while (j < centre_count)
		cycles[i++] = centre_tmp[j++];
	free(wing_tmp);
	free(centre_tmp);
	return ((t_generator){.cycle_count = total, .cycles = cycles});
}

const t_cubic_face	*cubic_face(uint32_t face)
{
	if (face >= CUBIC_FACE_COUNT)
		return (NULL);
	return (&CUBIC_FACES[face]);
}

uint32_t	cubic_face_for(t_cubic_axis axis, int sign)
{
	uint32_t	face;

	face = 0;
	while (face < CUBIC_FACE_COUNT)
	{
		if (CUBIC_FACES[face].axis == axis && CUBIC_FACES[face].sign == sign)
			return (face);
		face++;
	}
	return (CUBIC_FACE_COUNT);
}

uint32_t	cubic_generator_index(uint32_t n, uint32_t face, uint32_t depth)
{
	if (n == 3)
		return (face);
	return (face * (cubic_max_depth(n) + 1) + depth);
}

int	cubic_generator_layer(uint32_t n, uint32_t generator, uint32_t *face,
	uint32_t *depth)
{
	uint32_t	per_face;

	if (n == 3)
		per_face = 1;
	else
		per_face = cubic_max_depth(n) + 1;
	if (generator >= CUBIC_FACE_COUNT * per_face)
		return (0);
	*face = generator / per_face;
	*depth = generator % per_face;
	return (1);
}

static t_generator_table	*cubic_build_table(uint32_t n)
{
	t_generator_table	*table;
	t_generator			*gens;
	uint32_t			max_depth;
	uint32_t			per_face;
	uint32_t			face;
	uint32_t			depth;

	max_depth = cubic_max_depth(n);
	per_face = max_depth + 1;
	gens = malloc(sizeof(*gens) * CUBIC_FACE_COUNT * per_face);
	face = 0;
	while (face < CUBIC_FACE_COUNT)
	{
		depth = 0;
		while (depth <= max_depth)
		{
			gens[cubic_generator_index(n, face, depth)]
				= cubic_build_generator(n, face, depth);
			depth++;
		}
		face++;
	}
	table = malloc(sizeof(*table));
	table->count = CUBIC_FACE_COUNT * per_face;
	table->steps = 3;
	table->generators = gens;
	return (table);
}

const t_shape	*cubic_shape_for(uint32_t n)
{
	if (n == 3)
		return (&CUBIC_3X3X3_SHAPE);
	if (n < CUBIC_MIN_SIZE || n >= CUBIC_CACHE_MAX)
		return (NULL);
	if (!g_shape_cache[n])
		g_shape_cache[n] = cubic_build_shape(n);
	return (g_shape_cache[n]);
}

const t_generator_table	*cubic_generators_for(uint32_t n)
{
	if (n == 3)
		return (&CUBIC_3X3X3_GENERATOR_TABLE);
	if (n < CUBIC_MIN_SIZE || n >= CUBIC_CACHE_MAX)
		return (NULL);
	if (!g_table_cache[n])
		g_table_cache[n] = cubic_build_table(n);
	return (g_table_cache[n]);
}
