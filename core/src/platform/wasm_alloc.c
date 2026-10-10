#include <stddef.h>

/// Minimal bump allocator for the freestanding WASM build only -- native
/// builds (`make test`) use libc's real malloc/calloc/free instead; this
/// file is excluded from the native build for exactly that reason (see
/// ../../Makefile's NATIVE_SRC vs WASM_SRC split).
///
/// There is no real free(): puzzle_t instances are few, long-lived, and
/// a browser tab's own lifetime bounds the module's memory lifetime
/// anyway. Reclaiming individual allocations would add real complexity
/// for a cost nobody pays here -- see CLAUDE.md's code-quality bar:
/// "as clean as possible" means no more machinery than the problem
/// needs, not the most sophisticated machinery available.
///
/// 4 MiB (this constant's original size, before Phase 2) covered every
/// puzzle shape/generator table Phase 0-1 ever built, but Kociemba's own
/// coordinate move tables (~2.9 MiB) plus pruning tables (~3.8 MiB,
/// docs/en/06-phase2-solvers.md) alone need ~6.8 MiB -- confirmed by
/// running the compiled module in Node: kociemba_init() returned false
/// (puzzle_solve() came back UINT32_MAX) at the old size, every table
/// past that point starved for arena space. 16 MiB leaves real headroom
/// for that plus Thistlethwaite's own additional pruning tables
/// (04-roadmap.md Phase 2, not built yet) without needing another bump.
///
/// 24 MiB since Phase 3 increment 4: the Skewb and Pyraminx depth tables
/// (~3.7 MB and ~1.1 MB with their move tables) are built on first use
/// and, like everything here, never freed. A session that visits every
/// family in one Worker (all four 3x3x3 algorithms, reduction at n = 4
/// to 8, then Skewb and Pyraminx) was measured in Node: it failed at
/// 16 MiB and at 20 MiB, and passed at 24. A user who stays on one
/// family uses a fraction of it.
# define ARENA_SIZE (24 * 1024 * 1024)

static unsigned char	arena[ARENA_SIZE];
static size_t			arena_used = 0;

void	*malloc(size_t size)
{
	void	*ptr;

	size = (size + 7) & ~(size_t)7;   // 8-byte align: safe for every
	                                   // type this project's structs use
	if (arena_used + size > ARENA_SIZE)
		return (NULL);
	ptr = &arena[arena_used];
	arena_used += size;
	return (ptr);
}

void	*calloc(size_t count, size_t size)
{
	unsigned char	*ptr;
	size_t			total;
	size_t			i;

	total = count * size;
	ptr = malloc(total);
	if (!ptr)
		return (NULL);
	i = 0;
	while (i < total)
	{
		ptr[i] = 0;
		i++;
	}
	return (ptr);
}

void	free(void *ptr)
{
	(void)ptr;   // no-op -- see this file's header comment
}
