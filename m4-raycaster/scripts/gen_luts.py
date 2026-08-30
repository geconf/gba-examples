from pathlib import Path
import math

LUT_SIZE = 512
FIXED_SHIFT = 12
FIXED_ONE = 1 << FIXED_SHIFT

# Large sentinel for "infinity".
FX_INF = 0x7FFFFFFF

ROOT = Path(__file__).resolve().parent.parent
SOURCE_DIR = ROOT / 'source'
INCLUDE_DIR = ROOT / 'include'


SCREEN_HEIGHT = 160
TILE_SIZE = 8
LINE_HEIGHT_FRAC_BITS = 5
RAY_LENGTH = 100


def generate_inv_sin():
    with open(SOURCE_DIR / 'inv_sin_lut.c', 'w') as f:
        print('#include <tonc.h>', file=f)
        print('#include "inv_sin_lut.h"', file=f)
        print(file=f)
        print(f'const s32 inv_sin_lut[{LUT_SIZE}] = {{', file=f)

        for i in range(LUT_SIZE):
            angle = (2.0 * math.pi * i) / LUT_SIZE
            s = abs(math.sin(angle))

            if s < 1e-12:
                value = FX_INF
            else:
                value = round((1.0 / s) * FIXED_ONE)

            if i % 8 == 0:
                print('    ', end='', file=f)

            print(f'0x{value:08X}', end='', file=f)

            if i != LUT_SIZE - 1:
                print(', ', end='', file=f)

            if i % 8 == 7:
                print(file=f)
        print('};', file=f)

    with open(INCLUDE_DIR / 'inv_sin_lut.h', 'w') as f:
        print('#ifndef INV_SIN_LUT_H', file=f)
        print('#define INV_SIN_LUT_H', file=f)
        print(file=f)
        print('#include <tonc.h>', file=f)
        print(file=f)
        print(f'#define INV_SIN_LUT_SIZE {LUT_SIZE}', file=f)
        print(file=f)
        print(f'#define INV_SIN_INF 0x{FX_INF:08X}', file=f)
        print(file=f)
        print(f'extern const s32 inv_sin_lut[{LUT_SIZE}];', file=f)
        print(file=f)
        print('static inline s32 lu_inv_abs_sin(uint theta)', file=f)
        print('{', file=f)
        print('    return inv_sin_lut[(theta >> 7) & 0x1FF];', file=f)
        print('}', file=f)
        print(file=f)
        print('static inline s32 lu_inv_abs_cos(uint theta)', file=f)
        print('{', file=f)
        print('    return inv_sin_lut[((theta >> 7) + 128) & 0x1FF];', file=f)
        print('}', file=f)
        print(file=f)
        print('#endif', file=f)


def generate_line_height():
    frac_scale = 1 << LINE_HEIGHT_FRAC_BITS

    # Maximum LUT index = maximum distance in tiles * samples per tile.
    max_index = (
        RAY_LENGTH * frac_scale + TILE_SIZE - 1
    ) // TILE_SIZE

    lut_size = max_index + 1

    with open(SOURCE_DIR / 'line_height_lut.c', 'w') as f:
        print('#include <tonc.h>', file=f)
        print('#include "line_height_lut.h"', file=f)
        print(file=f)

        print(
            f'const s32 line_height_lut[{lut_size}] = {{',
            file=f
        )

        for i in range(lut_size):

            if i == 0:
                # Distance zero would produce infinity.
                # Since the renderer clamps to screen height anyway,
                # store the maximum useful wall height.
                line_height = SCREEN_HEIGHT

            else:
                # i represents tile distance with LINE_HEIGHT_FRAC_BITS
                # fractional bits.
                #
                # tile_dist = i / frac_scale
                #
                # line_height = SCREEN_HEIGHT / tile_dist
                #             = SCREEN_HEIGHT * frac_scale / i
                line_height = (SCREEN_HEIGHT * frac_scale) / i

                if line_height > SCREEN_HEIGHT:
                    line_height = SCREEN_HEIGHT

            value = round(line_height * FIXED_ONE)

            if i % 8 == 0:
                print('    ', end='', file=f)

            print(f'0x{value:08X}', end='', file=f)

            if i != lut_size - 1:
                print(', ', end='', file=f)

            if i % 8 == 7:
                print(file=f)

        if lut_size % 8 != 0:
            print(file=f)

        print('};', file=f)

    with open(INCLUDE_DIR / 'line_height_lut.h', 'w') as f:
        print('#ifndef LINE_HEIGHT_LUT_H', file=f)
        print('#define LINE_HEIGHT_LUT_H', file=f)
        print(file=f)

        print('#include <tonc.h>', file=f)
        print(file=f)

        print(
            f'#define LINE_HEIGHT_LUT_SIZE {lut_size}',
            file=f
        )
        print(
            f'#define LINE_HEIGHT_LUT_FRAC_BITS {LINE_HEIGHT_FRAC_BITS}',
            file=f
        )
        print(file=f)

        print(
            f'extern const s32 line_height_lut[{lut_size}];',
            file=f
        )

        print(file=f)
        print('#endif', file=f)


if __name__ == '__main__':
    generate_inv_sin()
    generate_line_height()
