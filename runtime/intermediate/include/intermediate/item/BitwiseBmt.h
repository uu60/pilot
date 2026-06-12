#ifndef PILOT_BITWISE_BMT_H
#define PILOT_BITWISE_BMT_H

#include <cstdint>
#include <vector>

#include "utils/Math.h"

struct RawBitwiseBmt {
    int64_t a{};
    int64_t b{};
    int64_t c{};
};

struct BitwiseBmt {
    int64_t _a{};
    int64_t _b{};
    int64_t _c{};

    static BitwiseBmt extract(const std::vector<BitwiseBmt> &bmts, int index, int width) {
        if (width >= 64) {
            return bmts[index];
        }
        const auto &src = bmts[index];
        return BitwiseBmt{
            Math::ring(src._a, width),
            Math::ring(src._b, width),
            Math::ring(src._c, width)
        };
    }
};

#endif
