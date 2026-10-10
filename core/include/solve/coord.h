#ifndef SOLVE_COORD_H
# define SOLVE_COORD_H

# include <stdint.h>
# include "solve/cube_view.h"

/// Ported from rubik/include/coord.h, operating on t_solve_cube instead
/// of rubik's own t_cube -- the coordinate maths itself (Lehmer codes,
/// base-3/base-2 digit packing, the C(n,k) counting trick for slice) is
/// pure combinatorics over permutation/orientation arrays, unchanged by
/// which struct holds them, so this is a direct, not an adapted, port.
///
/// A coordinate is ONE small number describing ONE property of a cube.
/// Two rules hold for every coordinate here:
///   - The solved cube always gives 0.
///   - decode(encode(cube)) rebuilds that one property, and
///     encode(decode(n)) == n for every n below the coordinate's COUNT.

# define TWIST_COUNT 2187
# define FLIP_COUNT 2048
# define SLICE_COUNT 495
# define CPERM_COUNT 40320
# define EPERM_COUNT 40320
# define SPERM_COUNT 24

uint16_t	encode_twist(const t_solve_cube *cube);
void		decode_twist(t_solve_cube *cube, uint16_t twist);
uint16_t	encode_flip(const t_solve_cube *cube);
void		decode_flip(t_solve_cube *cube, uint16_t flip);
uint16_t	encode_slice(const t_solve_cube *cube);
void		decode_slice(t_solve_cube *cube, uint16_t slice);
uint16_t	encode_cperm(const t_solve_cube *cube);
void		decode_cperm(t_solve_cube *cube, uint16_t cperm);
uint16_t	encode_eperm(const t_solve_cube *cube);
void		decode_eperm(t_solve_cube *cube, uint16_t eperm);
uint16_t	encode_sperm(const t_solve_cube *cube);
void		decode_sperm(t_solve_cube *cube, uint16_t sperm);

#endif
