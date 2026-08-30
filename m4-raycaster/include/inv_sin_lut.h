#ifndef INV_SIN_LUT_H
#define INV_SIN_LUT_H

#include <tonc.h>

#define INV_SIN_LUT_SIZE 512

#define INV_SIN_INF 0x7FFFFFFF

extern const s32 inv_sin_lut[512];

static inline s32 lu_inv_abs_sin(uint theta)
{
    return inv_sin_lut[(theta >> 7) & 0x1FF];
}

static inline s32 lu_inv_abs_cos(uint theta)
{
    return inv_sin_lut[((theta >> 7) + 128) & 0x1FF];
}

#endif
