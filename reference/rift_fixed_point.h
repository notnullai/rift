#pragma once

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// Q16.16 fixed-point arithmetic
static inline int32_t q16_mul(int32_t a, int32_t b) {
    return (int32_t)(((int64_t)a * (int64_t)b) >> 16);
}

static inline int32_t q16_div(int32_t a, int32_t b) {
    return (int32_t)(((int64_t)a << 16) / b);
}

static inline int32_t q16_clamp(int32_t v, int32_t min, int32_t max) {
    if (v < min) return min;
    if (v > max) return max;
    return v;
}

// Sign-corrected truncating shift for leak
// Matches (int32_t)((float)v * (float)leak_q16 / 65536.0f) over reachable range
static inline int32_t leak_apply(int32_t v, uint32_t leak_q16) {
    const int64_t prod = (int64_t)v * (int64_t)leak_q16;
    const int64_t q = (prod + ((prod >> 63) & 0xFFFF)) >> 16;
    return (int32_t)q;
}

// Truncate-toward-zero divide by 65536
static inline int32_t reflex_trunc_div_q16(int64_t x) {
    return (int32_t)((x + ((x >> 63) & 0xFFFF)) >> 16);
}

#ifdef __cplusplus
}
#endif
