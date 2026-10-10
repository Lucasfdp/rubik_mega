#include "solve/bytes.h"

void	solve_copy_bytes(uint8_t *to, const uint8_t *from, uint32_t count)
{
	uint32_t	i;

	i = 0;
	while (i < count)
	{
		to[i] = from[i];
		i++;
	}
}

void	solve_zero_bytes(void *block, uint32_t count)
{
	uint8_t		*bytes;
	uint32_t	i;

	bytes = (uint8_t *)block;
	i = 0;
	while (i < count)
	{
		bytes[i] = 0;
		i++;
	}
}
