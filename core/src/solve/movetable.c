#include "platform.h"
#include "solve/movetable.h"
#include "solve/coord.h"
#include "shape.h"
#include "puzzles/cubic.h"

/// @brief Turns a raw n==3 state buffer by one rubik-numbered move
/// (face = move / 3, turn = move % 3 + 1 quarter-turns), via
/// CUBIC_3X3X3_GENERATOR_TABLE -- the same generator table
/// puzzle_apply_move() uses for every n==3 puzzle, not a second,
/// independently-ported copy of the face turn logic. See
/// solve/movetable.h's own header comment on why this, not a ported
/// apply_move(), is what the table builder below turns with.
void	apply_n3_move(uint8_t *state, int move)
{
	const t_generator	*gen;
	int					turn;
	uint32_t			cycle;

	gen = &CUBIC_3X3X3_GENERATOR_TABLE.generators[move / 3];
	turn = 0;
	while (turn < move % 3 + 1)
	{
		cycle = 0;
		while (cycle < gen->cycle_count)
		{
			shape_apply_cycle(&CUBIC_3X3X3_SHAPE, state, &gen->cycles[cycle]);
			cycle++;
		}
		turn++;
	}
}

/// The two function shapes every coordinate has -- see rubik's own
/// movetable.c for the same trick (pass the pair in, one builder makes
/// all six tables).
typedef void		(*t_decode_fn)(t_solve_cube *, uint16_t);
typedef uint16_t	(*t_encode_fn)(const t_solve_cube *);

/// @brief One table cell: rebuild a cube with this coordinate value
///        (from SOLVED_SOLVE_CUBE, so every property the coordinate
///        doesn't describe starts at a valid solved value), turn it,
///        read the coordinate back.
static uint16_t	next_state(t_decode_fn decode, t_encode_fn encode,
	uint16_t state, int move)
{
	t_solve_cube	cube;
	uint8_t			buf[SOLVE_CORNER_COUNT * 2 + SOLVE_EDGE_COUNT * 2];

	cube = SOLVED_SOLVE_CUBE;
	decode(&cube, state);
	solve_cube_to_state(&cube, buf);
	apply_n3_move(buf, move);
	solve_cube_from_state(&cube, buf);
	return (encode(&cube));
}

static void	fill_row(uint16_t *row, t_decode_fn decode, t_encode_fn encode,
	uint16_t state, t_move_mask allowed)
{
	int	move;

	move = 0;
	while (move < MOVE_COUNT)
	{
		if (move_in_mask(allowed, move))
			row[move] = next_state(decode, encode, state, move);
		else
			row[move] = MT_ILLEGAL;
		move++;
	}
}

static uint16_t	*build_table(int count, t_decode_fn decode,
	t_encode_fn encode, t_move_mask allowed)
{
	uint16_t	*table;
	int			state;

	table = malloc((size_t)count * MOVE_COUNT * sizeof(uint16_t));
	if (!table)
		return (NULL);
	state = 0;
	while (state < count)
	{
		fill_row(table + state * MOVE_COUNT, decode, encode,
			(uint16_t)state, allowed);
		state++;
	}
	return (table);
}

void	move_tables_free(t_move_tables *tables)
{
	free(tables->twist);
	free(tables->flip);
	free(tables->slice);
	free(tables->cperm);
	free(tables->eperm);
	free(tables->sperm);
	tables->twist = NULL;
	tables->flip = NULL;
	tables->slice = NULL;
	tables->cperm = NULL;
	tables->eperm = NULL;
	tables->sperm = NULL;
}

bool	move_tables_build(t_move_tables *tables)
{
	tables->twist = build_table(TWIST_COUNT, decode_twist, encode_twist,
			MOVES_ALL);
	tables->flip = build_table(FLIP_COUNT, decode_flip, encode_flip,
			MOVES_ALL);
	tables->slice = build_table(SLICE_COUNT, decode_slice, encode_slice,
			MOVES_ALL);
	tables->cperm = build_table(CPERM_COUNT, decode_cperm, encode_cperm,
			MOVES_ALL);
	tables->eperm = build_table(EPERM_COUNT, decode_eperm, encode_eperm,
			MOVES_PHASE2);
	tables->sperm = build_table(SPERM_COUNT, decode_sperm, encode_sperm,
			MOVES_PHASE2);
	if (!tables->twist || !tables->flip || !tables->slice
		|| !tables->cperm || !tables->eperm || !tables->sperm)
	{
		move_tables_free(tables);
		return (false);
	}
	return (true);
}
