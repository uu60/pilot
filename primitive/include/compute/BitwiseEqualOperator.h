#ifndef PILOT_BITWISE_EQUAL_OPERATOR_H
#define PILOT_BITWISE_EQUAL_OPERATOR_H

#include "compute/BitwiseOperator.h"
#include "intermediate/item/BitwiseBmt.h"

#include <atomic>

class BitwiseEqualOperator : public BitwiseOperator {
private:
    std::vector<BitwiseBmt> *_bmts{};

public:
    inline static std::atomic_int64_t _totalTime = 0;

    BitwiseEqualOperator(std::vector<int64_t> *xs, std::vector<int64_t> *ys, int width,
                              int clientRank)
        : BitwiseOperator(xs, ys, width, 0, clientRank) {}

    BitwiseEqualOperator *execute() override;
    BitwiseEqualOperator *setBmts(std::vector<BitwiseBmt> *bmts);

    static int tagStride();
    static int bmtCount(int num, int width);
};

#endif
