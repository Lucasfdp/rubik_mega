#include "platform.h"
#include "solve/prune.h"
#include "solve/coord.h"

/// Ported from rubik/src/coord/prune.c -- unmodified (pure coordinate
/// BFS, no cube type involved).

static int	expand_cell(t_prune_table *table, const uint16_t *move_a,
	const uint16_t *move_b, t_move_mask moves, int cell, int depth)
{
	int	found;
	int	move;
	int	next;

	found = 0;
	move = 0;
	while (move < MOVE_COUNT)
	{
		if (move_in_mask(moves, move))
		{
			next = move_a[(cell / table->count_b) * MOVE_COUNT + move]
				* table->count_b
				+ move_b[(cell % table->count_b) * MOVE_COUNT + move];
			if (table->dist[next] == PRUNE_UNVISITED)
			{
				table->dist[next] = (uint8_t)(depth + 1);
				found++;
			}
		}
		move++;
	}
	return (found);
}

static void	bfs_fill(t_prune_table *table, const uint16_t *move_a,
	const uint16_t *move_b, t_move_mask moves)
{
	int	depth;
	int	cell;
	int	found;

	depth = 0;
	found = 1;
	while (found > 0)
	{
		found = 0;
		cell = 0;
		while (cell < table->size)
		{
			if (table->dist[cell] == depth)
				found += expand_cell(table, move_a, move_b, moves, cell,
						depth);
			cell++;
		}
		if (found > 0)
			table->depth = ++depth;
	}
}

bool	prune_build(t_prune_table *table, const uint16_t *move_a, int count_a,
	const uint16_t *move_b, int count_b, t_move_mask moves)
{
	int	i;

	table->count_b = count_b;
	table->size = count_a * count_b;
	table->depth = 0;
	table->dist = malloc((size_t)table->size);
	if (!table->dist)
		return (false);
	i = 0;
	while (i < table->size)
		table->dist[i++] = PRUNE_UNVISITED;
	table->dist[0] = 0;
	bfs_fill(table, move_a, move_b, moves);
	return (true);
}

bool	*reachable_set(const uint16_t *table, int count, t_move_mask moves)
{
	bool		*seen;
	bool		growing;
	int			state;
	int			move;
	uint16_t	next;

	seen = calloc((size_t)count, sizeof(bool));
	if (!seen)
		return (NULL);
	seen[0] = true;
	growing = true;
	while (growing)
	{
		growing = false;
		state = 0;
		while (state < count)
		{
			move = 0;
			while (seen[state] && move < MOVE_COUNT)
			{
				if (move_in_mask(moves, move))
				{
					next = table[state * MOVE_COUNT + move];
					if (!seen[next])
					{
						seen[next] = true;
						growing = true;
					}
				}
				move++;
			}
			state++;
		}
	}
	return (seen);
}

bool	prune_build_coset(t_prune_table *table, const uint16_t *move_a,
	int count_a, const bool *goal_a, const uint16_t *move_b, int count_b,
	const bool *goal_b, t_move_mask moves)
{
	int	a;
	int	b;
	int	i;

	table->count_b = count_b;
	table->size = count_a * count_b;
	table->depth = 0;
	table->dist = malloc((size_t)table->size);
	if (!table->dist)
		return (false);
	i = 0;
	while (i < table->size)
		table->dist[i++] = PRUNE_UNVISITED;
	a = 0;
	while (a < count_a)
	{
		b = 0;
		while (b < count_b)
		{
			if (goal_a[a] && goal_b[b])
				table->dist[a * count_b + b] = 0;
			b++;
		}
		a++;
	}
	bfs_fill(table, move_a, move_b, moves);
	return (true);
}

void	prune_free(t_prune_table *table)
{
	free(table->dist);
	table->dist = NULL;
	table->count_b = 0;
	table->size = 0;
	table->depth = 0;
}

void	prune_tables_free(t_prune_tables *tables)
{
	prune_free(&tables->twist_slice);
	prune_free(&tables->flip_slice);
	prune_free(&tables->cperm_sperm);
	prune_free(&tables->eperm_sperm);
}

bool	prune_tables_build(t_prune_tables *tables, const t_move_tables *moves)
{
	if (!prune_build(&tables->twist_slice, moves->twist, TWIST_COUNT,
			moves->slice, SLICE_COUNT, MOVES_ALL)
		|| !prune_build(&tables->flip_slice, moves->flip, FLIP_COUNT,
			moves->slice, SLICE_COUNT, MOVES_ALL)
		|| !prune_build(&tables->cperm_sperm, moves->cperm, CPERM_COUNT,
			moves->sperm, SPERM_COUNT, MOVES_PHASE2)
		|| !prune_build(&tables->eperm_sperm, moves->eperm, EPERM_COUNT,
			moves->sperm, SPERM_COUNT, MOVES_PHASE2))
	{
		prune_tables_free(tables);
		return (false);
	}
	return (true);
}
