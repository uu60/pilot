#ifndef MPC_PACKAGE_MATH_H
#define MPC_PACKAGE_MATH_H
#include <cstdint>

class Math {
public:
    int64_t ring(int64_t num, int width) {
        if (width == 64) {
            return num;
        }
        return num & ((1LL << width) - 1);
    }
};


#endif
