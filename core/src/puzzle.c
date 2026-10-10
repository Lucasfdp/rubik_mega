#include "platform.h"
#include "puzzle.h"
#include "puzzle_internal.h"
#include "puzzles/cubic_facelets.h"
#include "shape.h"
#include "puzzles/cubic.h"
#include "puzzles/skewb.h"
#include "puzzles/tetrahedral.h"
#include "puzzles/dodecahedral.h"
#include "puzzles/square1.h"
#include "prng.h"
#include "solve/registry.h"

/// Colour groups puzzle_is_solved() tells apart: one per face, and a
/// Megaminx has the most (12). Sized with room to spare, because the group
/// is a byte; SOLVED_GROUP_UNSEEN marks a group no sticker has been read
/// from yet and is not a colour any puzzle has.
# define SOLVED_GROUP_LIMIT 32
# define SOLVED_GROUP_UNSEEN 0xFF

/// The cut-plane family a (type, size) pair builds, or NULL (cubic, or
/// an unsupported pair). Only the dodecahedral family reads `size`
/// (2 Kilominx, 3 Megaminx).
static const t_cut_family	*cut_family_for(t_puzzle_type type, uint32_t size)
{
	if (type == PUZZLE_SKEWB)
		return (skewb_family());
	if (type == PUZZLE_TETRAHEDRAL)
		return (tetrahedral_family());
	if (type == PUZZLE_DODECAHEDRAL)
		return (dodeca_family(size));
	return (NULL);
}

/// Which shape and generator table a (type, size) pair builds, or both
/// NULL if unsupported. Skewb and Pyraminx have no size parameter.
static void	family_tables(t_puzzle_type type, uint32_t size,
				const t_shape **shape, const t_generator_table **generators)
{
	const t_cut_family	*family;

	*shape = NULL;
	*generators = NULL;
	family = cut_family_for(type, size);
	if (type == PUZZLE_CUBIC && size >= 3)
	{
		*shape = cubic_shape_for(size);
		*generators = cubic_generators_for(size);
	}
	else if (family)
	{
		*shape = family->shape;
		*generators = family->generators;
	}
}

/// Every supported family now has a facelet layer: the cubic one
/// (cubic_facelets.c), the cut-plane ones (cut_geometry.c) and Square-1's
/// (numbered by unit, puzzles/square1.h).
static int	has_facelets(const puzzle_t *puzzle)
{
	return (puzzle && (puzzle->type == PUZZLE_CUBIC || puzzle->family
			|| puzzle->type == PUZZLE_SQUARE1));
}

/// Square-1 has its own state model, so it skips the shape/generator-table
/// path everywhere it differs (docs/en/12-phase5-square1.md D1-A).
static int	is_square1(const puzzle_t *puzzle)
{
	return (puzzle && puzzle->type == PUZZLE_SQUARE1);
}

/// Builds a Square-1. No shape, no generator table, no cut family: just a
/// 25-byte state, a 54-byte facelet buffer and a 24-byte units buffer.
static puzzle_t	*square1_create(void)
{
	puzzle_t	*puzzle;

	puzzle = calloc(1, sizeof(puzzle_t));
	if (!puzzle)
		return (NULL);
	puzzle->type = PUZZLE_SQUARE1;
	puzzle->state_size = SQ1_STATE_SIZE;
	puzzle->state = malloc(SQ1_STATE_SIZE);
	puzzle->facelets = malloc(SQ1_FACELET_COUNT);
	puzzle->units = malloc(SQ1_UNIT_COUNT);
	if (!puzzle->state || !puzzle->facelets || !puzzle->units)
	{
		free(puzzle->units);
		free(puzzle->facelets);
		free(puzzle->state);
		free(puzzle);
		return (NULL);
	}
	sq1_init_solved(puzzle->state);
	return (puzzle);
}

/// Bytes of colour buffer a new puzzle needs for its facelets.
static uint32_t	facelet_buffer_size(uint32_t size, const t_cut_family *family)
{
	if (family)
		return (family->sticker_count);
	return (cubic_facelets_for(size)->count);
}

puzzle_t	*puzzle_create(t_puzzle_type type, uint32_t size)
{
	puzzle_t				*puzzle;
	const t_shape			*shape;
	const t_generator_table	*generators;

	if (type == PUZZLE_SQUARE1)
		return (square1_create());
	family_tables(type, size, &shape, &generators);
	if (!shape || !generators || !shape_is_valid(shape))
		return (NULL);   // unsupported (type, size) -- docs/en/05-phase1-nxn.md
	puzzle = malloc(sizeof(puzzle_t));
	if (!puzzle)
		return (NULL);
	puzzle->type = type;
	puzzle->size = (type == PUZZLE_CUBIC || type == PUZZLE_DODECAHEDRAL)
		? size : 0;
	puzzle->shape = *shape;
	puzzle->generators = generators;
	puzzle->family = cut_family_for(type, size);
	puzzle->units = NULL;
	puzzle->state_size = shape_state_size(&puzzle->shape);
	puzzle->state = malloc(puzzle->state_size);
	puzzle->facelets = malloc(facelet_buffer_size(size, puzzle->family));
	if (!puzzle->state || !puzzle->facelets)
	{
		free(puzzle->facelets);
		free(puzzle->state);
		free(puzzle);
		return (NULL);
	}
	shape_init_solved(&puzzle->shape, puzzle->state);
	return (puzzle);
}

void	puzzle_destroy(puzzle_t *puzzle)
{
	if (!puzzle)
		return ;
	free(puzzle->units);
	free(puzzle->facelets);
	free(puzzle->state);
	free(puzzle);
}

void	puzzle_apply_move(puzzle_t *puzzle, int16_t generator, int8_t amount)
{
	t_move	move;

	if (is_square1(puzzle))
	{
		sq1_apply_move(puzzle->state, generator, amount);   // refuses an
		return ;   // illegal slice itself, byte-identical, never half-applied
	}
	if (!puzzle || generator < 0
		|| (uint32_t)generator >= puzzle->generators->count
		|| amount < 1 || (uint32_t)amount > puzzle->generators->steps)
		return ;   // valid amounts are 1..steps inclusive (e.g. 1/2/3 for
	                   // a cubic face's 90/180/270 -- steps COUNTS them,
	                   // it is not itself an exclusive upper bound
	move.generator = generator;
	move.amount = amount;
	shape_apply_move(&puzzle->shape, puzzle->generators, puzzle->state, move);
}

/// One random move of the generator-table families: a generator, then an
/// amount, drawn in that order. The order is spelled out as two statements
/// on purpose: this used to be two prng_next() calls inside one argument
/// list, and C leaves the order of evaluating arguments unspecified, so a
/// native gcc build and the WASM build's clang could have disagreed about
/// what a seed means. A seed in a shared link has to mean one scramble
/// everywhere (docs/en/13-phase6-polish.md D10).
static t_move	scramble_draw(const t_generator_table *generators,
					uint32_t *rng)
{
	t_move	move;

	move.generator = (int16_t)(prng_next(rng) % generators->count);
	move.amount = (int8_t)(1 + prng_next(rng) % generators->steps);
	return (move);
}

void	puzzle_scramble(puzzle_t *puzzle, uint32_t move_count, uint32_t seed)
{
	uint32_t	rng;
	uint32_t	i;
	t_move		move;

	if (!puzzle)
		return ;
	if (is_square1(puzzle))
	{
		sq1_scramble(puzzle->state, move_count, seed);
		return ;
	}
	rng = prng_seed(seed);
	i = 0;
	while (i < move_count)
	{
		move = scramble_draw(puzzle->generators, &rng);
		puzzle_apply_move(puzzle, move.generator, move.amount);
		i++;
	}
}

uint32_t	puzzle_scramble_moves(const puzzle_t *puzzle, t_move *out,
				uint32_t max, uint32_t move_count, uint32_t seed)
{
	uint32_t	rng;
	uint32_t	i;
	t_move		move;

	if (!puzzle)
		return (0);
	if (is_square1(puzzle))
		return (sq1_scramble_moves(puzzle->state, move_count, seed, out,
				max));
	rng = prng_seed(seed);
	i = 0;
	while (i < move_count)
	{
		move = scramble_draw(puzzle->generators, &rng);
		if (out && i < max)
			out[i] = move;
		i++;
	}
	return (move_count);
}

uint32_t	puzzle_scramble_max_moves(const puzzle_t *puzzle,
				uint32_t move_count)
{
	if (is_square1(puzzle))
		return (move_count * SQ1_SCRAMBLE_MOVES_PER_SLICE);
	return (move_count);
}

/// The face a facelet belongs to when the puzzle is solved: the key its
/// colour is compared within. Every sticker of one face has the same solved
/// colour, so "all stickers of one solved colour show one colour" is
/// "every face is a single colour".
static uint8_t	facelet_group(const puzzle_t *puzzle, uint32_t index)
{
	if (puzzle->family)
		return (puzzle->family->colours[index]);
	return (cubic_facelets_for(puzzle->size)->geometry[index].face);
}

uint32_t	puzzle_is_solved(puzzle_t *puzzle)
{
	uint8_t			first[SOLVED_GROUP_LIMIT];
	const uint8_t	*painted;
	uint32_t		count;
	uint32_t		i;
	uint8_t			group;

	if (!puzzle)
		return (0);
	if (is_square1(puzzle))
		return ((uint32_t)sq1_is_solved(puzzle->state));
	painted = puzzle_facelets(puzzle);
	count = puzzle_facelet_count(puzzle);
	i = 0;
	while (i < SOLVED_GROUP_LIMIT)
		first[i++] = SOLVED_GROUP_UNSEEN;
	i = 0;
	while (i < count)
	{
		group = facelet_group(puzzle, i);
		if (first[group] == SOLVED_GROUP_UNSEEN)
			first[group] = painted[i];
		else if (first[group] != painted[i])
			return (0);
		i++;
	}
	return (1);
}

const uint8_t	*puzzle_state_ptr(const puzzle_t *puzzle)
{
	return (puzzle->state);
}

uint32_t	puzzle_state_size(const puzzle_t *puzzle)
{
	return (puzzle->state_size);
}

/// Which families have solvers, and how many algorithms each has per
/// size, is solve/registry.c's business, not this file's.
uint32_t	puzzle_algorithm_count(const puzzle_t *puzzle)
{
	if (!puzzle)
		return (0);
	return (registry_algorithm_count(puzzle->type, puzzle->size));
}

const char	*puzzle_algorithm_name(const puzzle_t *puzzle, uint32_t algo_id)
{
	const t_algorithm	*algo;

	if (!puzzle)
		return (NULL);
	algo = registry_algorithm(puzzle->type, puzzle->size, algo_id);
	if (!algo)
		return (NULL);
	return (algo->name);
}

uint32_t	puzzle_solve_max_moves(const puzzle_t *puzzle)
{
	if (!puzzle)
		return (0);
	return (registry_solve_max_moves(puzzle->type, puzzle->size));
}

uint32_t	puzzle_solve(puzzle_t *puzzle, uint32_t algo_id, t_move *out,
				uint32_t max_moves, uint32_t *out_phase_tags)
{
	const t_algorithm	*algo;

	if (!puzzle)
		return (0xFFFFFFFFu);
	algo = registry_algorithm(puzzle->type, puzzle->size, algo_id);
	if (!algo)
		return (0xFFFFFFFFu);
	return (algo->solve(puzzle->state, puzzle->size, out, max_moves,
			out_phase_tags));
}

uint32_t	puzzle_facelet_count(const puzzle_t *puzzle)
{
	if (!has_facelets(puzzle))
		return (0);
	if (is_square1(puzzle))
		return (SQ1_FACELET_COUNT);
	if (puzzle->family)
		return (puzzle->family->sticker_count);
	return (cubic_facelets_for(puzzle->size)->count);
}

const uint8_t	*puzzle_facelet_geometry(const puzzle_t *puzzle)
{
	if (!has_facelets(puzzle) || is_square1(puzzle))
		return (NULL);   // Square-1's facelets are numbered by unit: no
	                     // per-facelet geometry to read (square1.h)
	if (puzzle->family)
		return (puzzle->family->facelets);
	return ((const uint8_t *)cubic_facelets_for(puzzle->size)->geometry);
}

const uint8_t	*puzzle_facelets(puzzle_t *puzzle)
{
	if (!has_facelets(puzzle))
		return (NULL);
	if (is_square1(puzzle))
		sq1_facelets(puzzle->state, puzzle->facelets);
	else if (puzzle->family)
		cut_paint(puzzle->family, puzzle->state, puzzle->facelets);
	else
		cubic_facelets_paint(puzzle->size, puzzle->state, puzzle->facelets);
	return (puzzle->facelets);
}

uint32_t	puzzle_generator_count(const puzzle_t *puzzle)
{
	if (is_square1(puzzle))
		return (SQ1_GENERATOR_COUNT);
	return (puzzle->generators->count);
}

int32_t	puzzle_generator_face(const puzzle_t *puzzle, uint32_t generator)
{
	uint32_t	face;
	uint32_t	depth;

	if (puzzle->type != PUZZLE_CUBIC
		|| !cubic_generator_layer(puzzle->size, generator, &face, &depth))
		return (-1);
	return ((int32_t)face);
}

int32_t	puzzle_generator_depth(const puzzle_t *puzzle, uint32_t generator)
{
	uint32_t	face;
	uint32_t	depth;

	if (puzzle->type != PUZZLE_CUBIC
		|| !cubic_generator_layer(puzzle->size, generator, &face, &depth))
		return (-1);
	return ((int32_t)depth);
}

uint32_t	puzzle_facelet_geometry_stride(const puzzle_t *puzzle)
{
	if (!has_facelets(puzzle) || is_square1(puzzle))
		return (0);
	if (puzzle->family)
		return (puzzle->family->facelet_stride);
	return ((uint32_t)sizeof(t_cubic_facelet));
}

uint32_t	puzzle_generator_axis(const puzzle_t *puzzle, uint32_t generator,
				int32_t *out)
{
	const t_ivec3	*axis;

	if (!puzzle || !puzzle->family || !out
		|| generator >= puzzle->generators->count)
		return (0);
	axis = &puzzle->family->turns[generator].axis;
	out[0] = axis->x;
	out[1] = axis->y;
	out[2] = axis->z;
	out[3] = puzzle->family->turns[generator].threshold;
	return (1);
}

uint32_t	puzzle_generator_facelets(const puzzle_t *puzzle,
				uint32_t generator, uint8_t *out, uint32_t max)
{
	if (!puzzle || !puzzle->family)
		return (0xFFFFFFFFu);
	return (cut_moving_facelets(puzzle->family, generator, out, max));
}

uint32_t	puzzle_generator_steps(const puzzle_t *puzzle, uint32_t generator)
{
	if (!puzzle || generator >= puzzle_generator_count(puzzle))
		return (0);
	if (is_square1(puzzle))
		return (generator == SQ1_GEN_SLICE ? SQ1_SLICE_STEPS
			: SQ1_LAYER_STEPS);
	return (puzzle->generators->steps);
}

uint32_t	puzzle_move_is_legal(const puzzle_t *puzzle, int16_t generator,
				int8_t amount)
{
	if (!puzzle || generator < 0 || amount < 1
		|| (uint32_t)amount > puzzle_generator_steps(puzzle,
			(uint32_t)generator))
		return (0);
	if (is_square1(puzzle))
		return ((uint32_t)sq1_move_is_legal(puzzle->state, generator,
				amount));
	return (1);
}

const uint8_t	*puzzle_square1_units(puzzle_t *puzzle)
{
	if (!is_square1(puzzle))
		return (NULL);
	sq1_units(puzzle->state, puzzle->units);
	return (puzzle->units);
}
