#ifndef PUZZLE_INTERNAL_H
# define PUZZLE_INTERNAL_H

# include <stdint.h>
# include "puzzle.h"
# include "shape.h"
# include "move.h"
# include "cut_geometry.h"

/// The concrete layout behind puzzle.h's opaque puzzle_t. Only
/// src/puzzle.c includes this -- everything outside core/ only ever
/// holds a pointer. Lives in a header (not puzzle.c) per Lucas's
/// personal coding norm: struct definitions always go in headers.
///
/// - shape:      this instance's piece inventory.
/// - generators: its move set (docs/en/03-architecture.md S2).
/// - state:      its own state buffer, sized by shape_state_size().
/// - facelets:   one colour byte per visible sticker, repainted by
///               puzzle_facelets() on request (docs/en/07-nxn-web-view.md
///               D1); NULL for a family with no facelet layer yet.
/// - family:     the cut-plane family record (Skewb, Pyraminx), or NULL
///               for the cubic family, which has its own tables.
/// - state_size: bytes in `state`. Kept here, not derived from `shape`,
///               because Square-1 has no shape or generator table: its
///               state is a fixed 24-unit buffer (puzzles/square1.h), so
///               for it `shape` is zeroed, `generators` is NULL and
///               neither is consulted.
/// - units:      Square-1's per-unit piece kinds (puzzle_square1_units()),
///               repainted on request; NULL for every other family.
struct s_puzzle
{
	t_puzzle_type			type;
	uint32_t				size;
	t_shape					shape;
	const t_generator_table	*generators;
	uint8_t					*state;
	uint8_t					*facelets;
	const t_cut_family		*family;
	uint32_t				state_size;
	uint8_t					*units;
};

#endif
