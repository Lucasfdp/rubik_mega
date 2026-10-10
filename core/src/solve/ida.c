#include "solve/ida.h"

/// Ported from rubik/src/solve/ida.c -- unmodified logic. One deliberate
/// naming change: rubik's t_move (its own flat 18-value enum) is called
/// `int` here throughout, to avoid colliding with this project's own
/// t_move (move.h's {generator, amount} struct) -- the two never mix;
/// solve/kociemba.c is the only file that converts between them.

void	ida_phase1_setup(t_ida_phase *phase, const t_move_tables *moves,
	const t_prune_tables *prune)
{
	phase->move_table[0] = moves->twist;
	phase->move_table[1] = moves->flip;
	phase->move_table[2] = moves->slice;
	phase->prune_xz = &prune->twist_slice;
	phase->prune_yz = &prune->flip_slice;
	phase->moves = MOVES_ALL;
	phase->max_depth = IDA_PHASE1_CAP;
}

void	ida_phase2_setup(t_ida_phase *phase, const t_move_tables *moves,
	const t_prune_tables *prune)
{
	phase->move_table[0] = moves->cperm;
	phase->move_table[1] = moves->eperm;
	phase->move_table[2] = moves->sperm;
	phase->prune_xz = &prune->cperm_sperm;
	phase->prune_yz = &prune->eperm_sperm;
	phase->moves = MOVES_PHASE2;
	phase->max_depth = IDA_PHASE2_CAP;
}

void	ida_phase1_start(const t_solve_cube *cube, uint16_t start[3])
{
	start[0] = encode_twist(cube);
	start[1] = encode_flip(cube);
	start[2] = encode_slice(cube);
}

void	ida_phase2_start(const t_solve_cube *cube, uint16_t start[3])
{
	start[0] = encode_cperm(cube);
	start[1] = encode_eperm(cube);
	start[2] = encode_sperm(cube);
}

int	ida_estimate(const t_ida_phase *phase, const uint16_t coord[3])
{
	int	a;
	int	b;

	a = prune_dist(phase->prune_xz, coord[0], coord[2]);
	b = prune_dist(phase->prune_yz, coord[1], coord[2]);
	if (a > b)
		return (a);
	return (b);
}

/// Faces are ordered U R F D L B, so the opposite of face f is (f+3)%6.
bool	ida_move_is_redundant(int last, int move)
{
	int	face;
	int	last_face;

	if (last < 0)
		return (false);
	face = move / 3;
	last_face = last / 3;
	if (face == last_face)
		return (true);
	return (face == (last_face + 3) % 6 && face < last_face);
}

static bool	search(t_ida_ctx *ctx, const uint16_t coord[3], int depth,
	int last)
{
	uint16_t	next[3];
	int			left;
	int			move;

	left = ida_estimate(ctx->phase, coord);
	if (left == 0)
	{
		ctx->length = depth;
		return (true);
	}
	if (depth + left > ctx->bound)
		return (false);
	move = -1;
	while (++move < MOVE_COUNT)
	{
		if (!move_in_mask(ctx->phase->moves, move)
			|| ida_move_is_redundant(last, move))
			continue ;
		next[0] = move_table_next(ctx->phase->move_table[0], coord[0], move);
		next[1] = move_table_next(ctx->phase->move_table[1], coord[1], move);
		next[2] = move_table_next(ctx->phase->move_table[2], coord[2], move);
		ctx->path[depth] = move;
		if (search(ctx, next, depth + 1, move))
			return (true);
	}
	return (false);
}

int	ida_search(const t_ida_phase *phase, const uint16_t start[3], int *path)
{
	t_ida_ctx	ctx;
	int			cap;

	ctx.phase = phase;
	ctx.path = path;
	ctx.length = -1;
	cap = phase->max_depth;
	if (cap > IDA_MAX_DEPTH)
		cap = IDA_MAX_DEPTH;
	ctx.bound = ida_estimate(phase, start);
	while (ctx.bound <= cap)
	{
		if (search(&ctx, start, 0, -1))
			return (ctx.length);
		ctx.bound++;
	}
	return (-1);
}
