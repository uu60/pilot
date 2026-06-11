#ifndef MPC_PACKAGE_MATH_H
#define MPC_PACKAGE_MATH_H

#include <cstdint>

class Math {
public:
    static int64_t randInt();
    static int64_t randInt(int64_t lowest, int64_t highest);

    static int64_t ring(int64_t num, int width) {
        if (width == 64) return num;
        return num & ((1LL << width) - 1);
    }

    static bool getBit(int64_t v, int i) {
        return (v >> i) & 1;
    }

    static int64_t changeBit(int64_t v, int i, bool b) {
        int64_t mask = ~(1LL << i);
        return (v & mask) | ((b ? 1LL : 0LL) << i);
    }
};

#endif
