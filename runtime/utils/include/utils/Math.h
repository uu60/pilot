#ifndef MPC_PACKAGE_MATH_H
#define MPC_PACKAGE_MATH_H

#include <cstdint>

class Math {
public:
    static int64_t randInt();
    static int64_t randInt(int64_t lowest, int64_t highest);

    static int64_t ring(int64_t num, int width) {
        if (width == 64) return num;
        const auto mask = (uint64_t{1} << width) - 1;
        return static_cast<int64_t>(static_cast<uint64_t>(num) & mask);
    }

    static bool getBit(int64_t v, int i) {
        return (static_cast<uint64_t>(v) >> i) & uint64_t{1};
    }

    static int64_t changeBit(int64_t v, int i, bool b) {
        const auto bit = uint64_t{1} << i;
        auto value = static_cast<uint64_t>(v) & ~bit;
        if (b) {
            value |= bit;
        }
        return static_cast<int64_t>(value);
    }
};

#endif
