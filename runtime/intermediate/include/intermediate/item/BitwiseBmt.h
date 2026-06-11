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
        const int per = 64 / width;
        const auto &src = bmts[index / per];
        const int off = (index % per) * width;
        const int64_t mask = width == 64 ? -1LL : ((1LL << width) - 1);
        return BitwiseBmt{
            Math::ring((src._a >> off) & mask, width),
            Math::ring((src._b >> off) & mask, width),
            Math::ring((src._c >> off) & mask, width)
        };
    }
};

#endif
