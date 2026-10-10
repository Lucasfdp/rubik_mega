#include "move_list.h"

uint32_t	move_list_merge(t_move *moves, uint32_t *phase_tags,
	uint32_t count, uint32_t steps)
{
	uint32_t	top;
	uint32_t	i;
	uint32_t	full_turn;
	int			amount;

	full_turn = steps + 1;
	top = 0;
	i = 0;
	while (i < count)
	{
		if (top > 0 && moves[top - 1].generator == moves[i].generator
			&& phase_tags[top - 1] == phase_tags[i])
		{
			amount = (moves[top - 1].amount + moves[i].amount)
				% (int)full_turn;
			if (amount == 0)
				top--;
			else
				moves[top - 1].amount = (int8_t)amount;
		}
		else
		{
			moves[top] = moves[i];
			phase_tags[top] = phase_tags[i];
			top++;
		}
		i++;
	}
	return (top);
}
